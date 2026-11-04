// F.RANGE — BUYORBEALOSER
// Briques DSP communes (C++17, sans dépendance). Aucun générateur de bruit : silence en entrée = silence en sortie.
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstdint>

namespace frange {

constexpr double kPi = 3.14159265358979323846;

inline float flushDenormal(float x) { return (std::fabs(x) < 1.0e-20f) ? 0.0f : x; }
inline double flushDenormal(double x) { return (std::fabs(x) < 1.0e-30) ? 0.0 : x; }
inline float dbToLin(float db) { return std::pow(10.0f, db * 0.05f); }

// Lissage exponentiel (évite les clics et le "zipper noise" sur les paramètres).
struct Smoother {
    float z = 0.0f, a = 0.0f;
    void prepare(double fs, float ms) { a = std::exp(-1.0f / (0.001f * ms * (float) fs)); }
    void reset(float v) { z = v; }
    float next(float target) { z = target + (z - target) * a; return z; }
};

// Filtre coupe-continu 1er ordre.
struct DCBlocker {
    float x1 = 0.0f, y1 = 0.0f, R = 0.999f;
    void prepare(double fs, double fc) { R = (float) (1.0 - 2.0 * kPi * fc / fs); }
    void reset() { x1 = y1 = 0.0f; }
    float process(float x) { float y = x - x1 + R * y1; x1 = x; y1 = flushDenormal(y); return y1; }
};

// Biquad RBJ (forme transposée II, double précision).
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    float process(float x) {
        double y = b0 * x + z1;
        z1 = flushDenormal(b1 * x - a1 * y + z2);
        z2 = flushDenormal(b2 * x - a2 * y);
        return (float) y;
    }
    void set(double B0, double B1, double B2, double A0, double A1, double A2) {
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    void lowpass(double fs, double f, double q = 0.70710678) {
        double w = 2 * kPi * f / fs, c = std::cos(w), s = std::sin(w), al = s / (2 * q);
        set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highpass(double fs, double f, double q = 0.70710678) {
        double w = 2 * kPi * f / fs, c = std::cos(w), s = std::sin(w), al = s / (2 * q);
        set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void lowShelf(double fs, double f, double gainDb, double slope = 0.7) {
        double A = std::pow(10.0, gainDb / 40), w = 2 * kPi * f / fs, c = std::cos(w), s = std::sin(w);
        double al = s / 2 * std::sqrt((A + 1 / A) * (1 / slope - 1) + 2), tb = 2 * std::sqrt(A) * al;
        set(A * ((A + 1) - (A - 1) * c + tb), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - tb),
            (A + 1) + (A - 1) * c + tb, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - tb);
    }
    void highShelf(double fs, double f, double gainDb, double slope = 0.7) {
        double A = std::pow(10.0, gainDb / 40), w = 2 * kPi * f / fs, c = std::cos(w), s = std::sin(w);
        double al = s / 2 * std::sqrt((A + 1 / A) * (1 / slope - 1) + 2), tb = 2 * std::sqrt(A) * al;
        set(A * ((A + 1) + (A - 1) * c + tb), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - tb),
            (A + 1) - (A - 1) * c + tb, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - tb);
    }
};

// Ligne à retard à lecture fractionnaire (Catmull-Rom). Délai minimum conseillé : 4 échantillons.
struct DelayLine {
    std::vector<float> buf;
    int mask = 0, w = 0;
    void init(int minSize) {
        int s = 8;
        while (s < minSize + 8) s <<= 1;
        buf.assign((size_t) s, 0.0f);
        mask = s - 1; w = 0;
    }
    void clear() { std::fill(buf.begin(), buf.end(), 0.0f); w = 0; }
    inline void push(float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    // d = 1 -> dernier échantillon écrit
    inline float readInt(int d) const { return buf[(size_t) ((w - d) & mask)]; }
    inline float read(float d) const {
        float pos = (float) w - d;
        int i = (int) std::floor(pos);
        float f = pos - (float) i;
        float p0 = buf[(size_t) ((i - 1) & mask)], p1 = buf[(size_t) (i & mask)];
        float p2 = buf[(size_t) ((i + 1) & mask)], p3 = buf[(size_t) ((i + 2) & mask)];
        return p1 + 0.5f * f * (p2 - p0 + f * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + f * (3.0f * (p1 - p2) + p3 - p0)));
    }
};

} // namespace frange
