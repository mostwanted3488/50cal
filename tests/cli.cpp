// Outil en ligne de commande : traite un fichier float32 stéréo entrelacé avec un effet DOT50.
// usage: frange_cli <drive|reverb|delay|chorus|crush|filter|phaser|flanger> <fs> <in.f32> <out.f32> [tail=SEC] [at=SEC:active=0|1] [clé=valeur ...]
#include "../dsp/Effects.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

using namespace frange;

template <class E, class P>
static void runFx(E& fx, P& p, double fs, std::vector<float>& L, std::vector<float>& R, const std::vector<std::pair<double, float>>& events) {
    fx.prepare(fs); fx.setParams(p); fx.reset();
    const size_t B = 64, n = L.size(); size_t ev = 0;
    for (size_t pos = 0; pos < n; pos += B) {
        size_t m = std::min(B, n - pos);
        while (ev < events.size() && events[ev].first * fs <= (double) pos) { p.active = events[ev].second > 0.5f; fx.setParams(p); ++ev; }
        fx.process(&L[pos], &R[pos], (int) m);
    }
}

int main(int argc, char** argv) {
    if (argc < 5) { std::fprintf(stderr, "usage\n"); return 1; }
    std::string fx = argv[1]; double fs = std::atof(argv[2]);
    std::map<std::string, float> kv; double tail = 0; std::vector<std::pair<double, float>> events;
    for (int i = 5; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("at=", 0) == 0) { size_t c = a.find(':'); double t = std::atof(a.substr(3, c - 3).c_str());
            size_t e = a.find('=', c); events.push_back({ t, (float) std::atof(a.substr(e + 1).c_str()) }); continue; }
        size_t e = a.find('='); if (e == std::string::npos) continue;
        std::string k = a.substr(0, e); float v = (float) std::atof(a.substr(e + 1).c_str());
        if (k == "tail") tail = v; else kv[k] = v;
    }
    FILE* f = std::fopen(argv[3], "rb"); if (!f) { std::perror("in"); return 1; }
    std::fseek(f, 0, SEEK_END); long sz = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    std::vector<float> raw((size_t) (sz / 4)); if (std::fread(raw.data(), 4, raw.size(), f) != raw.size()) return 1; std::fclose(f);
    size_t nIn = raw.size() / 2, nTail = (size_t) (tail * fs), n = nIn + nTail;
    std::vector<float> L(n, 0.f), R(n, 0.f);
    for (size_t i = 0; i < nIn; ++i) { L[i] = raw[2 * i]; R[i] = raw[2 * i + 1]; }
    auto get = [&](const char* k, float d) { auto it = kv.find(k); return it == kv.end() ? d : it->second; };
    bool act = get("active", 1) > 0.5f;

    if (fx == "drive") { Drive e; Drive::Params p; p.drive = get("drive", p.drive); p.tone = get("tone", p.tone); p.bias = get("bias", p.bias);
        p.outputDb = get("out", p.outputDb); p.mix = get("mix", p.mix); p.active = act; runFx(e, p, fs, L, R, events); }
    else if (fx == "reverb") { Reverb e; Reverb::Params p; p.size = get("size", p.size); p.decaySec = get("decay", p.decaySec); p.damp = get("damp", p.damp);
        p.predelayMs = get("pre", p.predelayMs); p.mod = get("mod", p.mod); p.width = get("width", p.width); p.mix = get("mix", p.mix);
        p.lowCutHz = get("lowcut", p.lowCutHz); p.highCutHz = get("highcut", p.highCutHz); p.warmth = get("warmth", p.warmth); p.active = act; runFx(e, p, fs, L, R, events); }
    else if (fx == "delay") { Delay e; Delay::Params p; p.timeMs = get("time", p.timeMs); p.syncDiv = (int) get("sync", 0); p.bpm = get("bpm", 120);
        p.feedback = get("fb", p.feedback); p.toneHz = get("tone", p.toneHz); p.lowCutHz = get("lowcut", p.lowCutHz); p.pingpong = get("pp", p.pingpong);
        p.warmth = get("warmth", p.warmth); p.duck = get("duck", p.duck); p.mod = get("mod", p.mod); p.mix = get("mix", p.mix); p.active = act; runFx(e, p, fs, L, R, events); }
    else if (fx == "chorus") { Chorus e; Chorus::Params p; p.rate = get("rate", p.rate); p.depth = get("depth", p.depth); p.width = get("width", p.width);
        p.toneHz = get("tone", p.toneHz); p.mix = get("mix", p.mix); p.active = act; runFx(e, p, fs, L, R, events); }
    else if (fx == "crush") { Crush e; Crush::Params p; p.bits = get("bits", p.bits); p.rate = get("rate", p.rate); p.smoothHz = get("smooth", p.smoothHz);
        p.mix = get("mix", p.mix); p.active = act; runFx(e, p, fs, L, R, events); }
    else if (fx == "filter") { Filter e; Filter::Params p; p.cutoffHz = get("cutoff", p.cutoffHz); p.resonance = get("res", p.resonance); p.drive = get("drive", p.drive);
        p.mode = (int) get("mode", 0); p.envAmount = get("env", p.envAmount); p.mix = get("mix", p.mix); p.active = act; runFx(e, p, fs, L, R, events); }
    else if (fx == "phaser") { Phaser e; Phaser::Params p; p.rate = get("rate", p.rate); p.depth = get("depth", p.depth); p.centerHz = get("center", p.centerHz);
        p.feedback = get("fb", p.feedback); p.stages = (int) get("stages", 6); p.width = get("width", p.width); p.mix = get("mix", p.mix); p.active = act; runFx(e, p, fs, L, R, events); }
    else if (fx == "flanger") { Flanger e; Flanger::Params p; p.rate = get("rate", p.rate); p.depth = get("depth", p.depth); p.manualMs = get("manual", p.manualMs);
        p.feedback = get("fb", p.feedback); p.width = get("width", p.width); p.mix = get("mix", p.mix); p.active = act; runFx(e, p, fs, L, R, events); }
    else { std::fprintf(stderr, "effet inconnu\n"); return 1; }

    std::vector<float> out(2 * n);
    for (size_t i = 0; i < n; ++i) { out[2 * i] = L[i]; out[2 * i + 1] = R[i]; }
    FILE* o = std::fopen(argv[4], "wb"); std::fwrite(out.data(), 4, out.size(), o); std::fclose(o);
    return 0;
}
