// F.RANGE — REVERB : réseau à retards rétroactifs (FDN 8x8, matrice de Hadamard) sombre et large.
//  - réflexions précoces façon "couloir de béton", puis queue lisse, modulée très doucement
//  - aucune source de bruit : la queue décroît jusqu'au silence exact, sans souffle
//  - activation = on ouvre/ferme l'envoi, la queue en cours s'éteint naturellement (pas de coupure sèche)
#pragma once
#include "Common.h"

namespace frange {
static inline int parity3(unsigned x) { int c = 0; while (x) { x &= x - 1; ++c; } return c; }  // nombre de bits à 1 (portable MSVC/clang/gcc)


class Reverb {
public:
    struct Params {
        float size = 0.55f;        // 0..1
        float decaySec = 2.8f;     // RT60 basses fréquences (0.3 .. 12 s)
        float damp = 0.55f;        // 0..1 : plus haut = aigus qui meurent plus vite
        float predelayMs = 12.0f;  // 0..200
        float mod = 0.35f;         // 0..1  modulation lente des retards (épaissit, sans vibrato audible)
        float width = 1.0f;        // 0..1.5
        float mix = 0.30f;         // 0..1
        float lowCutHz = 120.0f;   // nettoie le bas de la queue
        float highCutHz = 9500.0f;
        float warmth = 0.0f;       // 0..1 saturation douce dans la boucle (0 = linéaire)
        bool active = true;
    };

    void prepare(double sampleRate) {
        fs = sampleRate;
        int maxD = (int) (fs * 0.32) + 16;
        for (auto& d : line) d.init(maxD);
        pre[0].init((int) (fs * 0.22) + 8); pre[1].init((int) (fs * 0.22) + 8);
        erL.init((int) (fs * 0.09) + 8); erR.init((int) (fs * 0.09) + 8);
        const double apMs[2][4] = { { 4.7, 3.7, 12.3, 9.1 }, { 4.9, 3.9, 12.9, 9.5 } };
        for (int c = 0; c < 2; ++c) for (int s = 0; s < 4; ++s) {
            ap[c][s].len = std::max(2, (int) std::lround(apMs[c][s] * 0.001 * fs));
            ap[c][s].dl.init(ap[c][s].len + 8);
        }
        sSend.prepare(fs, 12); sDry.prepare(fs, 25); sWet.prepare(fs, 25);
        sSize.prepare(fs, 250); sDecay.prepare(fs, 120); sDamp.prepare(fs, 120);
        sMod.prepare(fs, 100); sWidth.prepare(fs, 50);
        reset();
    }
    void reset() {
        for (auto& d : line) d.clear();
        pre[0].clear(); pre[1].clear(); erL.clear(); erR.clear();
        for (auto& c : ap) for (auto& a : c) a.dl.clear();
        for (int i = 0; i < kN; ++i) { lp[i] = 0; phase[i] = (float) i * 0.7853981f; }
        for (auto& b : hp) b.reset();
        for (auto& b : lpf) b.reset();
        erLpL = erLpR = 0;
        sSend.reset(target.active ? 1.0f : 0.0f);
        sDry.reset(dryTarget()); sWet.reset(wetTarget());
        sSize.reset(target.size); sDecay.reset(target.decaySec); sDamp.reset(target.damp);
        sMod.reset(target.mod); sWidth.reset(target.width);
        counter = 0; updateFilters(); updateGains(target.size, target.decaySec, target.damp);
        for (int i = 0; i < kN; ++i) curLen[i] = lenFor(i, target.size);
    }
    void setParams(const Params& p) { target = p; }
    int latencySamples() const { return 0; }

    void process(float* l, float* r, int n) {
        for (int i = 0; i < n; ++i) {
            float size = sSize.next(target.size), decay = sDecay.next(target.decaySec), damp = sDamp.next(target.damp);
            float mod = sMod.next(target.mod), width = sWidth.next(target.width);
            float send = sSend.next(target.active ? 1.0f : 0.0f);
            float dryG = sDry.next(dryTarget()), wetG = sWet.next(wetTarget());
            if ((counter++ & 31) == 0) { updateGains(size, decay, damp); updateFilters(); }

            float inL = l[i], inR = r[i];
            // pré-délai
            pre[0].push(inL * send); pre[1].push(inR * send);
            float pd = std::max(4.0f, target.predelayMs * 0.001f * (float) fs);
            float xL = pre[0].read(pd), xR = pre[1].read(pd);

            // réflexions précoces (couloir de béton)
            erL.push(xL); erR.push(xR);
            float erOutL = 0, erOutR = 0;
            const float tL[7] = { 4.3f, 9.7f, 15.8f, 23.1f, 31.9f, 41.7f, 52.3f };
            const float tR[7] = { 6.1f, 12.4f, 19.9f, 27.2f, 36.8f, 46.1f, 58.0f };
            float tsc = 0.6f + 0.8f * size;
            for (int k = 0; k < 7; ++k) {
                float a = 0.62f * std::exp(-0.26f * (float) k) * ((k & 1) ? -1.0f : 1.0f);
                erOutL += a * erL.read(std::max(4.0f, tL[k] * tsc * 0.001f * (float) fs));
                erOutR += a * erR.read(std::max(4.0f, tR[k] * tsc * 0.001f * (float) fs));
            }
            erLpL += 0.55f * (erOutL - erLpL); erLpR += 0.55f * (erOutR - erLpR);
            erLpL = flushDenormal(erLpL); erLpR = flushDenormal(erLpR);

            // diffusion d'entrée (4 allpass par voie)
            float dL = xL, dR = xR;
            for (int s = 0; s < 4; ++s) { dL = ap[0][s].process(dL); dR = ap[1][s].process(dR); }

            // FDN
            float d[kN], in[kN];
            float modDepth = mod * 2.2f * (float) (fs / 48000.0);
            for (int j = 0; j < kN; ++j) {
                float wantLen = lenFor(j, size);
                curLen[j] += 0.0008f * (wantLen - curLen[j]);
                phase[j] += (double) rate[j] * (2 * kPi / fs); if (phase[j] > 2 * kPi) phase[j] -= 2 * kPi;
                float dl = std::max(5.0f, curLen[j] + modDepth * (float) std::sin(phase[j]));
                float s = line[j].read(dl);
                lp[j] = flushDenormal((1.0f - lpA[j]) * s + lpA[j] * lp[j]);
                float v = lp[j] * gLine[j];
                if (target.warmth > 0.001f) {
                    float w = target.warmth;
                    v = (1.0f - w) * v + w * std::tanh(v * 1.6f) * 0.625f;
                }
                d[j] = v;
                { int q = j & 3; float sL = (q == 0) ? 1.0f : (q == 2 ? -1.0f : 0.0f), sR = (q == 1) ? 1.0f : (q == 3 ? -1.0f : 0.0f); in[j] = 0.3536f * (sL * dL + sR * dR); }
            }
            // Hadamard 8 (orthogonale -> conserve l'énergie)
            float m[kN];
            for (int j = 0; j < kN; ++j) m[j] = d[j];
            for (int len = 1; len < kN; len <<= 1)
                for (int a = 0; a < kN; a += len << 1)
                    for (int b = a; b < a + len; ++b) { float x = m[b], y = m[b + len]; m[b] = x + y; m[b + len] = x - y; }
            const float hs = 0.25f;
            for (int j = 0; j < kN; ++j) line[j].push(flushDenormal(in[j] + m[j] * hs));

            float wl = 0, wr = 0;
            for (int j = 0; j < kN; ++j) {
                float oL = (parity3((unsigned) (j & 5)) & 1) ? -1.0f : 1.0f;
                float oR = (parity3((unsigned) (j & 6)) & 1) ? -1.0f : 1.0f;
                wl += oL * d[j]; wr += oR * d[j];
            }
            wl = wl * 0.3536f + erLpL * 0.55f; wr = wr * 0.3536f + erLpR * 0.55f;

            // filtres de sortie + largeur
            wl = lpf[0].process(hp[0].process(wl)); wr = lpf[1].process(hp[1].process(wr));
            float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * width;
            wl = mid + side; wr = mid - side;

            l[i] = inL * dryG + wl * wetG;
            r[i] = inR * dryG + wr * wetG;
        }
    }

private:
    static constexpr int kN = 16;
    struct AP { DelayLine dl; int len = 2; float g = 0.62f;
        float process(float x) { float dd = dl.readInt(len); float v = x - g * dd; dl.push(flushDenormal(v)); return dd + g * v; } };

    float dryTarget() const { return target.active ? std::cos(target.mix * 1.5707963f) : 1.0f; }
    float wetTarget() const { return std::sin(target.mix * 1.5707963f) * 1.0f; }
    float lenFor(int i, float size) const {
        static const double baseMs[kN] = { 17.3, 21.1, 24.7, 28.9, 32.3, 36.1, 40.9, 45.7, 50.3, 55.9, 61.1, 66.7, 72.1, 78.3, 84.7, 91.9 };
        return (float) (baseMs[i] * 0.001 * fs * (0.6 + 1.0 * size));
    }
    void updateGains(float size, float decay, float damp) {
        float rt = std::max(0.2f, decay);
        float rtHf = std::max(0.06f, rt * (1.0f - 0.93f * damp));
        const float wRef = 2.0f * (float) kPi * std::min(6000.0f, 0.45f * (float) fs) / (float) fs, cw = std::cos(wRef);
        for (int i = 0; i < kN; ++i) {
            float len = lenFor(i, size);
            float g = std::pow(10.0f, -3.0f * len / ((float) fs * rt));
            float r = std::pow(10.0f, -3.0f * len / (float) fs * (1.0f / rtHf - 1.0f / rt));
            r = std::min(1.0f, std::max(1e-3f, r));
            gLine[i] = std::min(0.99995f, g);
            // coefficient du filtre 1 pôle dont le gain à 6 kHz vaut r (DC = 1) : résolution par dichotomie
            float lo = 0.0f, hi = 0.9995f;
            for (int it = 0; it < 22; ++it) {
                float a = 0.5f * (lo + hi), mag = (1.0f - a) / std::sqrt(1.0f - 2.0f * a * cw + a * a);
                if (mag > r) lo = a; else hi = a;
            }
            lpA[i] = (r >= 0.9999f) ? 0.0f : 0.5f * (lo + hi);
        }
    }
    void updateFilters() {
        for (int c = 0; c < 2; ++c) {
            hp[c].highpass(fs, std::max(20.0f, target.lowCutHz));
            lpf[c].lowpass(fs, std::min(0.45f * (float) fs, std::max(1000.0f, target.highCutHz)));
        }
    }

    double fs = 48000.0;
    Params target;
    DelayLine line[kN], pre[2], erL, erR;
    AP ap[2][4];
    float lp[kN] = {}, lpA[kN] = {}, gLine[kN] = {}, curLen[kN] = {}; double phase[kN] = {};
    const float rate[kN] = { 0.071f, 0.083f, 0.097f, 0.113f, 0.127f, 0.137f, 0.151f, 0.173f, 0.181f, 0.191f, 0.211f, 0.233f, 0.257f, 0.289f, 0.307f, 0.317f };
    Biquad hp[2], lpf[2];
    float erLpL = 0, erLpR = 0;
    Smoother sSend, sDry, sWet, sSize, sDecay, sDamp, sMod, sWidth;
    unsigned counter = 0;
};

} // namespace frange
