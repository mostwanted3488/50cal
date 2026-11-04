// F.RANGE — FLANGER : retard très court modulé avec rétroaction (positive ou négative), filtrée pour rester douce.
// Zéro latence, aucun bruit, rétroaction bornée (|fb| ≤ 0.9).
#pragma once
#include "Common.h"

namespace frange {

class Flanger {
public:
    struct Params {
        float rate = 0.25f;      // Hz
        float depth = 0.6f;      // 0..1
        float manualMs = 1.8f;   // 0.3..6 centre du balayage
        float feedback = 0.5f;   // -0.9..0.9
        float width = 0.5f;      // 0..1 décalage G/D
        float mix = 0.5f;
        bool active = true;
    };
    void prepare(double sampleRate) {
        fs = sampleRate; for (auto& l : line) l.init((int) (fs * 0.02) + 16);
        sMix.prepare(fs, 25); sRate.prepare(fs, 60); sFb.prepare(fs, 40); sMan.prepare(fs, 60); reset();
    }
    void reset() {
        for (auto& l : line) l.clear();
        for (int c = 0; c < 2; ++c) { fbLp[c] = 0; hp[c].reset(); hp[c].highpass(fs, 40.0); }
        ph = 0; sMix.reset(target.active ? target.mix : 0.0f); sRate.reset(target.rate); sFb.reset(target.feedback); sMan.reset(target.manualMs);
    }
    void setParams(const Params& p) { target = p; }
    int latencySamples() const { return 0; }
    void process(float* l, float* r, int n) {
        float* io[2] = { l, r };
        for (int i = 0; i < n; ++i) {
            float m = sMix.next(target.active ? target.mix : 0.0f), rate = sRate.next(target.rate), man = sMan.next(target.manualMs);
            float fb = std::max(-0.9f, std::min(0.9f, sFb.next(target.feedback)));
            ph += (double) rate * (2 * kPi / fs); if (ph > 2 * kPi) ph -= 2 * kPi;
            for (int c = 0; c < 2; ++c) {
                float lfo = std::sin(ph + (c ? target.width * 1.5707963f : 0.0f));
                float dMs = std::max(0.25f, man * (1.0f + 0.9f * target.depth * lfo));
                float x = io[c][i];
                float wet = line[c].read(std::max(5.0f, dMs * 0.001f * (float) fs));
                fbLp[c] += 0.6f * (wet - fbLp[c]); fbLp[c] = flushDenormal(fbLp[c]);
                line[c].push(flushDenormal(x + fb * hp[c].process(fbLp[c])));
                io[c][i] = x * (1.0f - 0.5f * m) + wet * (0.5f * m);
            }
        }
    }
private:
    double fs = 48000.0; Params target; DelayLine line[2]; Biquad hp[2]; float fbLp[2] = {}; double ph = 0; Smoother sMix, sRate, sFb, sMan;
};

} // namespace frange
