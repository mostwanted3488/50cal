// Sur-échantillonnage x4 (2 étages demi-bande FIR Kaiser, phase linéaire).
// Latence fixe : 52 échantillons à la fréquence d'origine (annoncée à l'hôte).
#pragma once
#include "Common.h"

namespace frange {

inline double besselI0(double x) {
    double s = 1, t = 1;
    for (int k = 1; k < 60; ++k) { double q = x / (2.0 * k); t *= q * q; s += t; }
    return s;
}

inline std::vector<double> designHalfband(int L, double beta) {
    std::vector<double> h((size_t) L);
    int c = (L - 1) / 2;
    double sum = 0;
    for (int n = 0; n < L; ++n) {
        int m = n - c;
        double sinc = (m == 0) ? 0.5 : std::sin(kPi * m * 0.5) / (kPi * m);
        double r = 2.0 * m / (L - 1);
        double w = besselI0(beta * std::sqrt(std::max(0.0, 1 - r * r))) / besselI0(beta);
        h[(size_t) n] = sinc * w;
        sum += h[(size_t) n];
    }
    for (auto& v : h) v /= sum;
    return h;
}

// x(fs) -> deux échantillons à 2fs
struct HalfbandUp2 {
    std::vector<double> p0, p1, dl;
    int pos = 0, n0 = 0, n1 = 0;
    void init(const std::vector<double>& h) {
        n0 = ((int) h.size() + 1) / 2; n1 = (int) h.size() / 2;
        p0.assign((size_t) n0, 0); p1.assign((size_t) n1, 0);
        for (int k = 0; k < n0; ++k) p0[(size_t) k] = 2.0 * h[(size_t) (2 * k)];
        for (int k = 0; k < n1; ++k) p1[(size_t) k] = 2.0 * h[(size_t) (2 * k + 1)];
        dl.assign((size_t) n0, 0); pos = 0;
    }
    void reset() { std::fill(dl.begin(), dl.end(), 0.0); pos = 0; }
    inline void process(double x, double& y0, double& y1) {
        pos = (pos + n0 - 1) % n0; dl[(size_t) pos] = x;
        double a = 0, b = 0;
        for (int k = 0; k < n0; ++k) a += p0[(size_t) k] * dl[(size_t) ((pos + k) % n0)];
        for (int k = 0; k < n1; ++k) b += p1[(size_t) k] * dl[(size_t) ((pos + k) % n0)];
        y0 = a; y1 = b;
    }
};

// deux échantillons à 2fs -> un à fs
struct HalfbandDown2 {
    std::vector<double> p0, p1, de, dO;
    int pe = 0, po = 0, n0 = 0, n1 = 0;
    void init(const std::vector<double>& h) {
        n0 = ((int) h.size() + 1) / 2; n1 = (int) h.size() / 2;
        p0.assign((size_t) n0, 0); p1.assign((size_t) n1, 0);
        for (int k = 0; k < n0; ++k) p0[(size_t) k] = h[(size_t) (2 * k)];
        for (int k = 0; k < n1; ++k) p1[(size_t) k] = h[(size_t) (2 * k + 1)];
        de.assign((size_t) n0, 0); dO.assign((size_t) n1, 0); pe = po = 0;
    }
    void reset() { std::fill(de.begin(), de.end(), 0.0); std::fill(dO.begin(), dO.end(), 0.0); pe = po = 0; }
    inline double process(double xe, double xo) {
        pe = (pe + n0 - 1) % n0; de[(size_t) pe] = xe;
        double y = 0;
        for (int k = 0; k < n0; ++k) y += p0[(size_t) k] * de[(size_t) ((pe + k) % n0)];
        for (int k = 0; k < n1; ++k) y += p1[(size_t) k] * dO[(size_t) ((po + k) % n1)];
        po = (po + n1 - 1) % n1; dO[(size_t) po] = xo;
        return y;
    }
};

struct Oversampler4 {
    static constexpr int kLenB = 95, kLenA = 21;
    static constexpr int kLatency = (kLenB - 1) / 2 + (kLenA - 1) / 4; // = 52
    HalfbandUp2 upB, upA;
    HalfbandDown2 dnA, dnB;
    void prepare() {
        auto hB = designHalfband(kLenB, 9.0), hA = designHalfband(kLenA, 8.0);
        upB.init(hB); upA.init(hA); dnA.init(hA); dnB.init(hB);
    }
    void reset() { upB.reset(); upA.reset(); dnA.reset(); dnB.reset(); }
    inline void up(float x, float out[4]) {
        double a0, a1, b0, b1, b2, b3;
        upB.process(x, a0, a1);
        upA.process(a0, b0, b1);
        upA.process(a1, b2, b3);
        out[0] = (float) b0; out[1] = (float) b1; out[2] = (float) b2; out[3] = (float) b3;
    }
    inline float down(const float in[4]) {
        double m0 = dnA.process(in[0], in[1]);
        double m1 = dnA.process(in[2], in[3]);
        return (float) dnB.process(m0, m1);
    }
};

} // namespace frange
