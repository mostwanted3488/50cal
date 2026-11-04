// F.RANGE — CRUSH : réducteur de résolution + de fréquence d'échantillonnage, sans dither ni bruit ajouté.
//  - bits continus (4.. 16), rapport de fréquence continu, lissage final réglable
//  - le grain vient de la quantification elle-même ; silence en entrée = silence exact
#pragma once
#include "Common.h"

namespace frange {

class Crush {
public:
    struct Params {
        float bits = 8.0f;       // 3..16
        float rate = 6.0f;       // facteur de réduction 1..48 (1 = pas de réduction)
        float smoothHz = 12000.0f;
        float mix = 1.0f;
        bool active = true;
    };
    void prepare(double sampleRate) {
        fs = sampleRate; sMix.prepare(fs, 20); sBits.prepare(fs, 40); sRate.prepare(fs, 40); reset();
    }
    void reset() {
        for (int c = 0; c < 2; ++c) { held[c] = 0; phase[c] = 1.0f; lp[c].reset(); }
        sMix.reset(target.active ? target.mix : 0.0f); sBits.reset(target.bits); sRate.reset(target.rate);
        counter = 0; updateFilters();
    }
    void setParams(const Params& p) { target = p; }
    int latencySamples() const { return 0; }
    void process(float* l, float* r, int n) {
        float* io[2] = { l, r };
        for (int i = 0; i < n; ++i) {
            float m = sMix.next(target.active ? target.mix : 0.0f), bits = sBits.next(target.bits), rate = std::max(1.0f, sRate.next(target.rate));
            if ((counter++ & 63) == 0) updateFilters();
            float levels = std::pow(2.0f, bits - 1.0f);
            for (int c = 0; c < 2; ++c) {
                float x = io[c][i];
                phase[c] += 1.0f;
                if (phase[c] >= rate) {
                    phase[c] -= rate;
                    float q = std::round(std::min(1.0f, std::max(-1.0f, x)) * levels) / levels;   // pas de dither : 0 reste 0
                    held[c] = q;
                }
                float y = (target.smoothHz < 19500.0f) ? lp[c].process(held[c]) : held[c];
                io[c][i] = x * (1.0f - m) + y * m;
            }
        }
    }
private:
    void updateFilters() { for (auto& b : lp) b.lowpass(fs, std::min(0.45f * (float) fs, std::max(1500.0f, target.smoothHz))); }
    double fs = 48000.0; Params target; float held[2] = {}, phase[2] = {}; Biquad lp[2];
    Smoother sMix, sBits, sRate; unsigned counter = 0;
};

} // namespace frange
