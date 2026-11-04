// F.RANGE — DRIVE : saturation asymétrique type lampe/bande, sur-échantillonnée x4.
//  - sans bruit ajouté (silence -> silence exact), sans repliement audible (aliasing), sans clic à l'activation
//  - gain de sortie compensé pour comparer à niveau égal
#pragma once
#include "Common.h"
#include "Oversampler.h"

namespace frange {

class Drive {
public:
    struct Params {
        float drive = 0.45f;     // 0..1  -> 0..38 dB de gain avant saturation
        float tone = 0.0f;       // -1..1 (sombre -> brillant), bascule autour de 1 kHz
        float bias = 0.35f;      // 0..1  asymétrie (harmoniques paires, chaleur)
        float outputDb = 0.0f;   // -18..+12
        float mix = 1.0f;        // 0..1  parallèle (dry aligné en phase)
        bool active = true;
    };

    void prepare(double sampleRate) {
        fs = sampleRate;
        for (auto& c : ch) {
            c.os.prepare(); c.dc.prepare(fs, 8.0);
            c.hpIn.highpass(fs, 28.0);
            c.dry.init(Oversampler4::kLatency + 8);
        }
        sDrive.prepare(fs, 25); sTone.prepare(fs, 40); sBias.prepare(fs, 40);
        sOut.prepare(fs, 25); sMix.prepare(fs, 25); sAct.prepare(fs, 15);
        reset();
    }
    void reset() {
        for (auto& c : ch) { c.os.reset(); c.dc.reset(); c.hpIn.reset(); c.lowSh.reset(); c.highSh.reset(); c.dry.clear(); }
        sDrive.reset(target.drive); sTone.reset(target.tone); sBias.reset(target.bias);
        sOut.reset(target.outputDb); sMix.reset(target.mix); sAct.reset(target.active ? 1.0f : 0.0f);
        counter = 0; compNow = 1.0f; updateTone(target.tone);
        for (auto& c : ch) { c.first = true; c.prevA = c.prevF = 0.0; }
    }
    int latencySamples() const { return Oversampler4::kLatency; }
    void setParams(const Params& p) { target = p; }

    void process(float* l, float* r, int n) {
        float* io[2] = { l, r };
        for (int i = 0; i < n; ++i) {
            float drv = sDrive.next(target.drive), tone = sTone.next(target.tone), bias = sBias.next(target.bias);
            float outDb = sOut.next(target.outputDb), mix = sMix.next(target.mix), act = sAct.next(target.active ? 1.0f : 0.0f);
            if ((counter++ & 31) == 0) updateTone(tone);
            float k = dbToLin(drv * 38.0f);
            float c = bias * 0.9f;
            float norm = 1.0f / (1.0f - std::tanh(c) * std::tanh(c));
            double tc = std::tanh((double) c);
            if ((counter & 31) == 1) compNow = computeComp(k, c, norm);
            float comp = compNow * dbToLin(outDb);
            float wetAmt = act * mix;
            for (int chn = 0; chn < 2; ++chn) {
                Chan& C = ch[chn];
                float x = io[chn][i];
                C.dry.push(x);
                float dryD = C.dry.readInt(Oversampler4::kLatency + 1);
                float pre = C.hpIn.process(x) * k;
                float u[4];
                C.os.up(pre, u);
                for (int s = 0; s < 4; ++s) u[s] = (float) ((adaaTanh(C, (double) u[s] + c) - tc) * norm);
                float y = C.os.down(u);
                y = C.dc.process(y);
                y = C.highSh.process(C.lowSh.process(y));
                y *= comp;
                io[chn][i] = dryD * (1.0f - wetAmt) + y * wetAmt;
            }
        }
    }

private:
    struct Chan { Oversampler4 os; Biquad hpIn, lowSh, highSh; DCBlocker dc; DelayLine dry; double prevA = 0.0, prevF = 0.0; bool first = true; };
    // log(cosh(a)) stable : primitive de tanh, sert à l'anti-repliement par antidérivée (ADAA 1er ordre)
    static inline double logcosh(double a) { double x = std::fabs(a); return x + std::log1p(std::exp(-2.0 * x)) - 0.6931471805599453; }
    // sortie de tanh(a) moyennée proprement entre deux échantillons successifs
    static inline double adaaTanh(Chan& C, double a) {
        double F = logcosh(a), d = a - C.prevA, y;
        if (C.first) { y = std::tanh(a); C.first = false; }
        else if (std::fabs(d) > 1e-5) y = (F - C.prevF) / d;
        else y = std::tanh(0.5 * (a + C.prevA));
        C.prevA = a; C.prevF = F;
        return y;
    }
    // compensation de niveau : une sinusoïde de référence (-18 dBFS) garde le même RMS quel que soit le drive
    static float computeComp(float k, float c, float norm) {
        const float A = 0.12f; double acc = 0.0; const int N = 32; const double tc = std::tanh((double) c);
        for (int i = 0; i < N; ++i) { double v = (std::tanh(k * A * std::sin(2.0 * kPi * (i + 0.5) / N) + c) - tc) * norm; acc += v * v; }
        double rms = std::sqrt(acc / N), inRms = A * 0.70710678;
        return (float) (inRms / std::max(rms, 1e-4));
    }
    void updateTone(float tone) {
        for (auto& c : ch) {
            c.lowSh.lowShelf(fs, 320.0, -tone * 3.5);
            c.highSh.highShelf(fs, 2800.0, tone * 6.0);
        }
    }
    double fs = 48000.0;
    Params target;
    Chan ch[2];
    Smoother sDrive, sTone, sBias, sOut, sMix, sAct;
    unsigned counter = 0;
    float compNow = 1.0f;
};

} // namespace frange
