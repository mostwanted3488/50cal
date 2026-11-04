// F.RANGE — PHASER : 4/6/8 étages passe-tout, balayage exponentiel, résonance saine (jamais d'auto-oscillation), décalage stéréo.
#pragma once
#include "Common.h"

namespace frange {

class Phaser {
public:
    struct Params {
        float rate = 0.35f;      // Hz
        float depth = 0.7f;      // 0..1 (±2.2 octaves)
        float centerHz = 700.0f; // 150..3000
        float feedback = 0.45f;  // -0.9..0.9
        int   stages = 6;        // 4, 6 ou 8
        float width = 0.6f;      // 0..1 décalage de phase G/D
        float mix = 0.5f;
        bool active = true;
    };
    void prepare(double sampleRate) { fs = sampleRate; sMix.prepare(fs, 25); sRate.prepare(fs, 60); sFb.prepare(fs, 40); reset(); }
    void reset() {
        for (int c = 0; c < 2; ++c) { for (auto& z : st[c]) z = 0; fbState[c] = 0; }
        ph = 0; sMix.reset(target.active ? target.mix : 0.0f); sRate.reset(target.rate); sFb.reset(target.feedback);
    }
    void setParams(const Params& p) { target = p; }
    int latencySamples() const { return 0; }
    void process(float* l, float* r, int n) {
        float* io[2] = { l, r };
        for (int i = 0; i < n; ++i) {
            float m = sMix.next(target.active ? target.mix : 0.0f), rate = sRate.next(target.rate);
            float fb = std::max(-0.9f, std::min(0.9f, sFb.next(target.feedback)));
            ph += (double) rate * (2 * kPi / fs); if (ph > 2 * kPi) ph -= 2 * kPi;
            int ns = target.stages >= 8 ? 8 : (target.stages >= 6 ? 6 : 4);
            for (int c = 0; c < 2; ++c) {
                float lfo = std::sin(ph + (c ? target.width * 1.5707963f : 0.0f));
                float f = target.centerHz * std::exp2(lfo * target.depth * 2.2f);
                f = std::min(0.45f * (float) fs, std::max(30.0f, f));
                float t = std::tan((float) kPi * f / (float) fs), a = (t - 1.0f) / (t + 1.0f);
                float x = io[c][i];
                float v = x + fb * fbState[c];
                for (int s = 0; s < ns; ++s) { float y = a * v + st[c][s]; st[c][s] = flushDenormal(v - a * y); v = y; }
                fbState[c] = flushDenormal(v);
                io[c][i] = x * (1.0f - 0.5f * m) + v * (0.5f * m);
            }
        }
    }
private:
    double fs = 48000.0; Params target; float st[2][8] = {}, fbState[2] = {}; double ph = 0; Smoother sMix, sRate, sFb;
};

} // namespace frange
