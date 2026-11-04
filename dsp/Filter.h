// F.RANGE — FILTER : filtre en échelle à 4 pôles (ZDF / TPT) avec saturation douce à l'entrée.
// Modes : LP24, LP12, BP, HP24. Suiveur d'enveloppe optionnel (auto-wah). Aucun bruit : silence -> silence exact.
#pragma once
#include "Common.h"

namespace frange {

class Filter {
public:
    struct Params {
        float cutoffHz = 2400.0f;  // 20..20000
        float resonance = 0.35f;   // 0..1
        float drive = 0.2f;        // 0..1 (max ≈ +12 dB avant saturation)
        int   mode = 0;            // 0 LP24, 1 LP12, 2 BP, 3 HP24
        float envAmount = 0.0f;    // -1..1  (en octaves, ±4)
        float mix = 1.0f;
        bool active = true;
    };
    void prepare(double sampleRate) {
        fs = sampleRate; sMix.prepare(fs, 20); sCut.prepare(fs, 15); sRes.prepare(fs, 25); sDrv.prepare(fs, 25); reset();
    }
    void reset() {
        for (auto& c : ch) { for (auto& s : c.s) s = 0; c.env = 0; c.pz = c.pF = 0.0; c.first = true; }
        sMix.reset(target.active ? target.mix : 0.0f); sCut.reset(std::log(target.cutoffHz)); sRes.reset(target.resonance); sDrv.reset(target.drive);
    }
    void setParams(const Params& p) { target = p; }
    int latencySamples() const { return 0; }
    void process(float* l, float* r, int n) {
        float* io[2] = { l, r };
        const float atk = 1.0f - std::exp(-1.0f / (0.003f * (float) fs)), rel = 1.0f - std::exp(-1.0f / (0.15f * (float) fs));
        for (int i = 0; i < n; ++i) {
            float m = sMix.next(target.active ? target.mix : 0.0f), lc = sCut.next(std::log(std::min(20000.0f, std::max(20.0f, target.cutoffHz))));
            float res = sRes.next(target.resonance), drv = sDrv.next(target.drive);
            float k = res * 3.95f, a = 1.0f + 3.0f * drv;
            float base = std::exp(lc);
            for (int c = 0; c < 2; ++c) {
                Chan& C = ch[c];
                float x = io[c][i];
                float ax = std::fabs(x); C.env += (ax > C.env ? atk : rel) * (ax - C.env); C.env = flushDenormal(C.env);
                float fc = base * std::exp2(target.envAmount * 4.0f * std::min(1.0f, C.env * 3.0f));
                fc = std::min(0.45f * (float) fs, std::max(15.0f, fc));
                float g = std::tan((float) kPi * fc / (float) fs), G = g / (1.0f + g);
                float G2 = G * G, G4 = G2 * G2;
                float S = (1.0f - G) * (G2 * G * C.s[0] + G2 * C.s[1] + G * C.s[2] + C.s[3]);
                float u = (x - k * S) / (1.0f + k * G4);
                u = (float) (adaaTanh(C, (double) a * u) / a);   // saturation douce anti-repliement, jamais > entrée
                float y1 = G * u + (1.0f - G) * C.s[0];
                float y2 = G * y1 + (1.0f - G) * C.s[1];
                float y3 = G * y2 + (1.0f - G) * C.s[2];
                float y4 = G * y3 + (1.0f - G) * C.s[3];
                C.s[0] = flushDenormal(2.0f * y1 - C.s[0]); C.s[1] = flushDenormal(2.0f * y2 - C.s[1]);
                C.s[2] = flushDenormal(2.0f * y3 - C.s[2]); C.s[3] = flushDenormal(2.0f * y4 - C.s[3]);
                float out;
                switch (target.mode) {
                    case 1: out = y2; break;
                    case 2: out = (y2 - y4) * 2.0f; break;
                    case 3: out = u - 4.0f * y1 + 6.0f * y2 - 4.0f * y3 + y4; break;
                    default: out = y4; break;
                }
                out *= 1.0f + 0.35f * k * (target.mode == 3 ? 0.4f : 1.0f);     // compense la perte de niveau due à la résonance
                io[c][i] = x * (1.0f - m) + out * m;
            }
        }
    }
private:
    struct Chan { float s[4] = {}; float env = 0; double pz = 0.0, pF = 0.0; bool first = true; };
    static inline double logcosh(double a) { double x = std::fabs(a); return x + std::log1p(std::exp(-2.0 * x)) - 0.6931471805599453; }
    // tanh anti-repliement (antidérivée 1er ordre)
    static inline double adaaTanh(Chan& C, double z) {
        double F = logcosh(z), d = z - C.pz, y;
        if (C.first) { y = std::tanh(z); C.first = false; }
        else if (std::fabs(d) > 1e-5) y = (F - C.pF) / d;
        else y = std::tanh(0.5 * (z + C.pz));
        C.pz = z; C.pF = F; return y;
    }
    double fs = 48000.0; Params target; Chan ch[2]; Smoother sMix, sCut, sRes, sDrv;
};

} // namespace frange
