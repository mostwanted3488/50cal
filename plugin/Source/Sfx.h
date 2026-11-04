#pragma once
#include <atomic>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <vector>

// SFX de l'interface (tir, impact, clic, victoire) synthétisés dans le plugin : aucun fichier audio, aucune latence.
// Même sons que la preview web. Ils sont mixés APRÈS les effets (donc jamais traités par les effets).
namespace frange
{
class Sfx
{
public:
    enum Type { Shot = 0, HitOn, HitOff, Click, Win, Reload, Tick, NumTypes };

    // sons échantillonnés (tir, rechargement) : stéréo entrelacé non nécessaire, deux canaux séparés
    void setSample (Type t, std::vector<float> l, std::vector<float> r, double srcRate, float gain)
    { auto& s = samples[(int) t]; s.l = std::move (l); s.r = std::move (r); s.rate = srcRate; s.gain = gain; }

    void prepare (double sampleRate) { sr = sampleRate; for (auto& v : voices) v.active = false; }

    // appelé depuis le thread UI
    void trigger (Type t) { pending[(int) t].fetch_add (1, std::memory_order_relaxed); }

    // appelé depuis le thread audio : ajoute les SFX au signal (gain = volume global)
    void process (float* l, float* r, int n, float gain)
    {
        for (int t = 0; t < NumTypes; ++t)
        {
            int k = pending[t].exchange (0, std::memory_order_relaxed);
            k = std::min (k, 2);
            while (k-- > 0) start ((Type) t);
        }
        for (auto& v : voices)
        {
            if (! v.active) continue;
            for (int i = 0; i < n; ++i)
            {
                if (v.type == Shot || v.type == Reload)
                {
                    const auto& sm = samples[(int) v.type];
                    if (! sm.l.empty())
                    {
                        const size_t n0 = sm.l.size();
                        const double pos = v.pos; const size_t i0 = (size_t) pos;
                        if (i0 + 1 >= n0) { v.active = false; break; }
                        const float f = (float) (pos - (double) i0);
                        const float a = (sm.l[i0] + f * (sm.l[i0 + 1] - sm.l[i0])) * sm.gain * gain;
                        const float b = (sm.r[i0] + f * (sm.r[i0 + 1] - sm.r[i0])) * sm.gain * gain;
                        l[i] += a; r[i] += b;
                        v.pos += sm.rate / sr * v.speed;
                        continue;
                    }
                }
                const float s = render (v) * gain;
                l[i] += s; r[i] += s;
                if (! v.active) break;
            }
        }
    }

private:
    struct Voice
    {
        bool active = false;
        Type type = Shot;
        long n = 0;           // échantillons écoulés
        double ph1 = 0, ph2 = 0;
        float lp = 0;         // filtre du bruit
        uint32_t seed = 1;
        double pos = 0, speed = 1;   // lecture d'un son échantillonné
    };
    struct Sample { std::vector<float> l, r; double rate = 44100.0; float gain = 1.f; };

    void start (Type t)
    {
        Voice* slot = nullptr;
        for (auto& v : voices) if (! v.active) { slot = &v; break; }
        if (slot == nullptr) slot = &voices[0];
        *slot = Voice();
        slot->active = true; slot->type = t; slot->seed = 12345u + (uint32_t) (++counter * 7919);
        slot->speed = (t == Shot) ? 0.98 + 0.04 * (double) ((slot->seed >> 8) & 1023) / 1023.0 : 1.0;
    }

    float noise (Voice& v) { v.seed = v.seed * 1664525u + 1013904223u; return (float) (v.seed >> 8) / 8388608.0f - 1.0f; }

    float render (Voice& v)
    {
        const double t = (double) v.n / sr;
        float out = 0.f;
        switch (v.type)
        {
            case Shot:
            {
                const double len = 0.25;
                if (t >= len) { v.active = false; break; }
                // bruit filtré (passe-bas qui se ferme de 5 kHz à 300 Hz) + "thump" sinus 160 -> 40 Hz
                const double fc = 5000.0 * std::pow (300.0 / 5000.0, std::min (1.0, t / 0.2));
                const float a = (float) (1.0 - std::exp (-6.283185307 * fc / sr));
                const float env = std::pow (1.f - (float) (t / len), 3.f);
                v.lp += a * (noise (v) * env - v.lp);
                out = v.lp * 0.35f * 1.2f;
                if (t < 0.18)
                {
                    const double f = 160.0 * std::pow (40.0 / 160.0, std::min (1.0, t / 0.15));
                    v.ph1 += f / sr; v.ph1 -= std::floor (v.ph1);
                    const float e = 0.4f * std::pow (0.001f / 0.4f, (float) (t / 0.18));
                    out += (float) std::sin (6.283185307 * v.ph1) * e;
                }
                break;
            }
            case HitOn: case HitOff:
            {
                const double f[2] = { v.type == HitOn ? 660.0 : 520.0, v.type == HitOn ? 880.0 : 330.0 };
                bool any = false;
                for (int k = 0; k < 2; ++k)
                {
                    const double tk = t - k * 0.07;
                    if (tk < 0 || tk >= 0.23) { if (tk < 0) any = true; continue; }
                    any = true;
                    double& ph = k ? v.ph2 : v.ph1;
                    ph += f[k] / sr; ph -= std::floor (ph);
                    const float tri = (float) (4.0 * std::fabs (ph - 0.5) - 1.0);
                    const float e = tk < 0.01 ? 0.0001f * std::pow (0.22f / 0.0001f, (float) (tk / 0.01))
                                              : 0.22f * std::pow (0.0001f / 0.22f, (float) ((tk - 0.01) / 0.21));
                    out += tri * e;
                }
                if (! any) v.active = false;
                break;
            }
            case Click:
            {
                if (t >= 0.06) { v.active = false; break; }
                v.ph1 += 90.0 / sr; v.ph1 -= std::floor (v.ph1);
                out = (v.ph1 < 0.5 ? 1.f : -1.f) * 0.12f * std::pow (0.001f / 0.12f, (float) std::min (1.0, t / 0.05));
                break;
            }
            case Win:
            {
                static const double fr[4] = { 523.25, 659.25, 783.99, 1046.5 };
                bool any = false;
                for (int k = 0; k < 4; ++k)
                {
                    const double tk = t - k * 0.09;
                    if (tk < 0) { any = true; continue; }
                    if (tk >= 0.25) continue;
                    any = true;
                    const float e = tk < 0.01 ? 0.07f * (float) (tk / 0.01)
                                              : 0.07f * std::pow (0.0008f / 0.07f, (float) ((tk - 0.01) / 0.21));
                    const double ph = std::fmod (fr[k] * tk, 1.0);
                    out += (ph < 0.5 ? 1.f : -1.f) * e;
                }
                if (! any) v.active = false;
                break;
            }
            case Tick:
            {
                if (t >= 0.08) { v.active = false; break; }
                const double fr[2] = { 1900.0, 3800.0 }; const float am[2] = { 0.2f, 0.07f };
                for (int k = 0; k < 2; ++k)
                {
                    const double f = fr[k] * (1.0 - 0.08 * std::min (1.0, t / 0.07));
                    double& ph = k ? v.ph2 : v.ph1;
                    ph += f / sr; ph -= std::floor (ph);
                    out += (float) std::sin (6.283185307 * ph) * am[k] * std::pow (0.0001f / am[k], (float) std::min (1.0, t / 0.07));
                }
                break;
            }
            default: v.active = false;
        }
        ++v.n;
        return out;
    }

    double sr = 48000.0;
    uint32_t counter = 0;
    Voice voices[12];
    Sample samples[NumTypes];
    std::atomic<int> pending[NumTypes] = {};
};
}
