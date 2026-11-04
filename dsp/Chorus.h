// F.RANGE — CHORUS : 3 voix par canal, retards modulés lentement (profondeur limitée : pas de vibrato "mal de mer").
// Aucun bruit, aucune rétroaction. Mix à 0 = signal d'entrée identique au bit près.
#pragma once
#include "Common.h"

namespace frange {

class Chorus {
public:
    struct Params {
        float rate = 0.6f;       // Hz 0.05..5
        float depth = 0.5f;      // 0..1
        float width = 0.8f;      // 0..1 décalage de phase entre G et D
        float toneHz = 11000.0f; // adoucit les voix
        float mix = 0.4f;
        bool active = true;
    };
    void prepare(double sampleRate) {
        fs = sampleRate;
        for (auto& l : line) l.init((int) (fs * 0.06) + 16);
        sMix.prepare(fs, 25); sRate.prepare(fs, 60); sDepth.prepare(fs, 60);
        reset();
    }
    void reset() {
        for (auto& l : line) l.clear();
        for (auto& b : lp) b.reset();
        sMix.reset(target.active ? target.mix : 0.0f); sRate.reset(target.rate); sDepth.reset(target.depth);
        for (int c = 0; c < 2; ++c) for (int v = 0; v < 3; ++v) ph[c][v] = (float) (2 * kPi) * v / 3.0f;
        counter = 0; updateFilters();
    }
    void setParams(const Params& p) { target = p; }
    int latencySamples() const { return 0; }
    void process(float* l, float* r, int n) {
        float* io[2] = { l, r };
        const float base[3] = { 11.0f, 16.5f, 22.0f };
        for (int i = 0; i < n; ++i) {
            float m = sMix.next(target.active ? target.mix : 0.0f), rate = sRate.next(target.rate), depth = sDepth.next(target.depth);
            if ((counter++ & 63) == 0) updateFilters();
            float dMs = depth * 3.0f;
            double inc = (double) rate * (2 * kPi / fs);
            for (int c = 0; c < 2; ++c) {
                float x = io[c][i];
                line[c].push(x);
                float acc = 0;
                for (int v = 0; v < 3; ++v) {
                    ph[c][v] += inc * (1.0 + 0.07 * v); if (ph[c][v] > 2 * kPi) ph[c][v] -= 2 * kPi;
                    float off = (c == 1) ? target.width * 1.5707963f : 0.0f;
                    float d = (base[v] + dMs * std::sin(ph[c][v] + off)) * 0.001f * (float) fs;
                    acc += line[c].read(std::max(6.0f, d));
                }
                float wet = lp[c].process(acc * 0.5774f);
                io[c][i] = x * (1.0f - 0.35f * m) + wet * (0.65f * m);
            }
        }
    }
private:
    void updateFilters() { for (auto& b : lp) b.lowpass(fs, std::min(0.45f * (float) fs, std::max(3000.0f, target.toneHz))); }
    double fs = 48000.0; Params target; DelayLine line[2]; Biquad lp[2];
    Smoother sMix, sRate, sDepth; double ph[2][3] = {}; unsigned counter = 0;
};

} // namespace frange
