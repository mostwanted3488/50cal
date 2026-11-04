// F.RANGE — DELAY : écho analogique propre (ping-pong, filtre et saturation douce DANS la boucle, ducking).
//  - aucune source de bruit ; rétroaction toujours < 1 (stable) ; les échos s'éteignent jusqu'au silence exact
//  - changement de temps lissé (glissement de hauteur "bande", jamais de clic)
//  - activation : on ferme l'envoi, les échos en cours finissent naturellement
#pragma once
#include "Common.h"

namespace frange {

class Delay {
public:
    struct Params {
        float timeMs = 350.0f;     // 20..1500 (si syncDiv = 0)
        int   syncDiv = 0;         // 0 libre, 1 = 1/4, 2 = 1/8, 3 = 1/8 pointée, 4 = 1/16, 5 = 1/8 triolet
        double bpm = 120.0;
        float feedback = 0.38f;    // 0..0.92
        float toneHz = 4500.0f;    // filtre passe-bas des répétitions
        float lowCutHz = 90.0f;
        float pingpong = 0.6f;     // 0..1
        float warmth = 0.25f;      // 0..1 saturation douce dans la boucle
        float duck = 0.35f;        // 0..1 baisse l'écho tant que le son joue
        float mod = 0.2f;          // 0..1 léger "wow" de bande
        float mix = 0.25f;
        bool active = true;
    };

    void prepare(double sampleRate) {
        fs = sampleRate;
        int maxD = (int) (fs * 3.3);
        for (auto& l : line) l.init(maxD);
        sSend.prepare(fs, 12); sDry.prepare(fs, 25); sWet.prepare(fs, 25); sFb.prepare(fs, 30);
        reset();
    }
    void reset() {
        for (auto& l : line) l.clear();
        for (auto& b : hp) b.reset();
        for (auto& b : lp) b.reset();
        sSend.reset(target.active ? 1.0f : 0.0f); sDry.reset(dryTarget()); sWet.reset(wetTarget()); sFb.reset(target.feedback);
        curDelay = targetSamples(); env = 0; duckG = 1.0f; phase[0] = 0.0f; phase[1] = 1.7f; counter = 0; updateFilters();
    }
    void setParams(const Params& p) { target = p; }
    int latencySamples() const { return 0; }

    void process(float* l, float* r, int n) {
        float* io[2] = { l, r };
        for (int i = 0; i < n; ++i) {
            float send = sSend.next(target.active ? 1.0f : 0.0f), dryG = sDry.next(dryTarget()), wetG = sWet.next(wetTarget());
            float fb = sFb.next(std::min(0.92f, target.feedback));
            if ((counter++ & 31) == 0) updateFilters();
            curDelay += 0.00025f * (targetSamples() - curDelay);
            float inL = io[0][i], inR = io[1][i];
            // enveloppe pour le ducking
            float a = std::max(std::fabs(inL), std::fabs(inR));
            env += (a > env ? 0.02f : 0.00012f) * (a - env); env = flushDenormal(env);
            float duckT = 1.0f - target.duck * std::min(1.0f, env * 3.0f);
            duckG += 0.004f * (duckT - duckG);

            float wowDepth = target.mod * 0.0007f * (float) fs;   // jusqu'à 0.7 ms
            float dly[2], f[2];
            for (int c = 0; c < 2; ++c) {
                phase[c] += (0.37 + 0.11 * c) * (2 * kPi / fs); if (phase[c] > 2 * kPi) phase[c] -= 2 * kPi;
                float d = std::max(8.0f, curDelay + wowDepth * (float) std::sin(phase[c]));
                dly[c] = line[c].read(d);
                float y = lp[c].process(hp[c].process(dly[c]));
                float g = 1.0f + 3.0f * target.warmth;               // saturation douce, jamais plus fort que l'entrée
                f[c] = (target.warmth > 0.001f) ? std::tanh(g * y) / g : y;
            }
            float pp = target.pingpong;
            float mono = 0.5f * (inL + inR);
            float wl = (inL * (1.0f - pp) + mono * pp) * send, wr = (inR * (1.0f - pp)) * send;
            float fbL = fb * ((1.0f - pp) * f[0] + pp * f[1]), fbR = fb * ((1.0f - pp) * f[1] + pp * f[0]);
            line[0].push(flushDenormal(wl + fbL));
            line[1].push(flushDenormal(wr + fbR));
            io[0][i] = inL * dryG + f[0] * wetG * duckG;
            io[1][i] = inR * dryG + f[1] * wetG * duckG;
        }
    }

private:
    float dryTarget() const { return target.active ? std::cos(target.mix * 0.5f * 1.5707963f) : 1.0f; }
    float wetTarget() const { return std::sin(target.mix * 1.5707963f); }
    float targetSamples() const {
        double ms = target.timeMs;
        if (target.syncDiv > 0) {
            static const double beats[] = { 0, 1.0, 0.5, 0.75, 0.25, 1.0 / 3.0 };
            ms = 60000.0 / std::max(20.0, target.bpm) * beats[std::min(5, target.syncDiv)];
        }
        ms = std::min(3000.0, std::max(10.0, ms));
        return (float) (ms * 0.001 * fs);
    }
    void updateFilters() {
        for (int c = 0; c < 2; ++c) {
            hp[c].highpass(fs, std::max(20.0f, target.lowCutHz));
            lp[c].lowpass(fs, std::min(0.45f * (float) fs, std::max(800.0f, target.toneHz)));
        }
    }
    double fs = 48000.0;
    Params target;
    DelayLine line[2];
    Biquad hp[2], lp[2];
    Smoother sSend, sDry, sWet, sFb;
    float curDelay = 1000.0f, env = 0.0f, duckG = 1.0f; double phase[2] = { 0.0, 1.7 };
    unsigned counter = 0;
};

} // namespace frange
