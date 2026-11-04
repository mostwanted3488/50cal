#include "PluginEditor.h"
#include "BinaryData.h"
#include "ClipData.h"
#include <webp/decode.h>
#include <cmath>
#include <cstring>
#include <cstdint>

using juce::Colour; using juce::Graphics; using juce::Path; using juce::Rectangle; using juce::Point;

namespace
{
    constexpr float PI = 3.14159265358979f, TAU = 6.28318530717959f;
    const Colour ORG (0xffd4ebf5);                       // accent bleu glacé
    const Colour PDARK (0xad222832), PEDGE (0x4dffffff);  // panneaux : rgba(34,40,50,.68) / bord blanc .3
    const Colour FXCOL (0xff9db0bf);
    const Colour TCOL (0xff18e0d0), TRIM (0xff0e9d92);   // TRYHARD : turquoise

    constexpr float HY = 140.f;
    const float SHY[3] = { 226.f, 322.f, 414.f };        // ligne de sol (ombres) de chaque rangée
    constexpr float GS = .74f, GPX = 800.f, GPY = 560.f; // réduction de l'arme, ancrée en bas à droite
    constexpr float FPX = 720.f, FPY = 560.f, FSH = .30f; // amortissement du recul du tir
    constexpr float TR = 27.f;                            // rayon des cibles TRYHARD
    const Rectangle<float> RLD { 696.f, 502.f, 68.f, 24.f };        // W-190-74, H-14-24
    const Rectangle<float> SFXBTN { 696.f, 474.f, 68.f, 24.f };     // au-dessus de RELOAD
    const Rectangle<float> THB { 104.f, 20.f, 50.f, 17.f };         // pastille TRYHARD dans le panneau du titre
    const Rectangle<float> THX { 836.f, 12.f, 110.f, 34.f };        // EXIT (mode TRYHARD)
    const Rectangle<float> RP { 210.f, 112.f, 540.f, 316.f };       // panneau de résultats
    const Rectangle<float> THRETRY { RP.getX() + 36.f, RP.getY() + RP.getHeight() - 58.f, 228.f, 40.f };
    const Rectangle<float> THEXIT  { RP.getX() + RP.getWidth() - 36.f - 228.f, RP.getY() + RP.getHeight() - 58.f, 228.f, 40.f };
    constexpr float FONT_H = 0.483f;                                // hauteur JUCE = 0.483 x taille "CSS" (métriques de la police)

    struct KDef { const char* id; const char* label; bool invert; };
    const KDef kKnobs[8] = {
        { "rev_decay", "DECAY", false },   { "dly_feedback", "FEEDBACK", false }, { "cho_depth", "DEPTH", false },
        { "drive_amount", "DRIVE", false },{ "cru_bits", "CRUSH", true },          { "flt_cutoff", "CUTOFF", false },
        { "pha_depth", "DEPTH", false },   { "fla_fb", "FEEDBACK", false } };

    float easeOutBack (float x) { const float c1 = 1.7f, c3 = c1 + 1.f; return 1.f + c3 * std::pow (x - 1.f, 3.f) + c1 * std::pow (x - 1.f, 2.f); }
    float eo (float k) { return 1.f - std::pow (1.f - k, 3.f); }
    juce::String pct (float v) { return juce::String (juce::roundToInt (v * 100.f)) + "%"; }
    juce::String fmtT (double s)
    {
        const int m = (int) (s / 60.0);
        return juce::String (m).paddedLeft ('0', 2) + ":" + juce::String (std::fmod (s, 60.0), 2).paddedLeft ('0', 5);
    }
    Colour A (Colour c, float a) { return c.withAlpha (a); }
    Colour rgba (int r, int g, int b, float a) { return Colour::fromFloatRGBA (r / 255.f, g / 255.f, b / 255.f, a); }
    bool inRect (Point<float> p, const Rectangle<float>& r) { return r.contains (p); }

    uint32_t rd32 (const uint8_t* p) { uint32_t v; std::memcpy (&v, p, 4); return v; }

    template <typename ClipT>
    void loadPack (const char* data, const clipdata::Meta& m, ClipT& clip)
    {
        clip.n = m.n; clip.fps = m.fps; clip.r = { m.x, m.y, m.w, m.h };
        const auto* b = (const uint8_t*) data;
        const int count = (int) rd32 (b);
        size_t off = 4 + 4 * (size_t) count;
        clip.frames.clear();
        for (int i = 0; i < count; ++i)
        {
            const int sz = (int) rd32 (b + 4 + 4 * i);
            clip.frames.emplace_back (b + off, sz);
            off += (size_t) sz;
        }
        clip.cache.assign ((size_t) count, juce::Image());
    }

    juce::Image decodeWebP (const uint8_t* d, int size)
    {
        int w = 0, h = 0;
        uint8_t* rgba = WebPDecodeRGBA (d, (size_t) size, &w, &h);
        if (rgba == nullptr) return {};
        juce::Image img (juce::Image::ARGB, w, h, true);
        {
            juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < h; ++y)
            {
                auto* line = (juce::PixelARGB*) bd.getLinePointer (y);
                const uint8_t* s = rgba + 4 * (size_t) y * (size_t) w;
                for (int x = 0; x < w; ++x, s += 4)
                {
                    line[x].setARGB (s[3], s[0], s[1], s[2]);
                    line[x].premultiply();
                }
            }
        }
        WebPFree (rgba);
        return img;
    }
}

// ------------------------------------------------------------------------------------------------
// Décodage des images de l'arme en arrière-plan : idle en entier, les 6 premières images de chaque animation en permanence,
// puis une fenêtre glissante devant la position courante (les animations lourdes ne tiennent pas toutes en mémoire).
struct Dot50Editor::Prefetch : public juce::Thread
{
    Prefetch (Dot50Editor& e) : juce::Thread ("DOT50 frames"), ed (e) {}
    static constexpr int KEEP = 6, AHEAD = 10;
    void run() override
    {
        for (int i = 0; i < (int) ed.clips[Idle].frames.size() && ! threadShouldExit(); ++i) decode (ed.clips[Idle], i);
        for (int k = Fire; k <= Reload; ++k)
            for (int i = 0; i < KEEP && i < (int) ed.clips[k].frames.size() && ! threadShouldExit(); ++i) decode (ed.clips[k], i);
        int lastClip = -2;
        while (! threadShouldExit())
        {
            const int wc = wantClip.load();
            if (wc >= Fire && wc <= Reload)
            {
                const int idx = wantIdx.load();
                auto& c = ed.clips[wc];
                const int last = juce::jmin ((int) c.frames.size() - 1, idx + AHEAD);
                for (int i = idx; i <= last && ! threadShouldExit(); ++i) decode (c, i);
                const juce::ScopedLock sl (ed.cacheLock);
                for (int i = KEEP; i < idx - 2 && i < (int) c.cache.size(); ++i) c.cache[(size_t) i] = juce::Image();
            }
            else if (lastClip != wc)
            {
                const juce::ScopedLock sl (ed.cacheLock);
                for (int k = Fire; k <= Reload; ++k)
                    for (int i = KEEP; i < (int) ed.clips[k].cache.size(); ++i) ed.clips[k].cache[(size_t) i] = juce::Image();
            }
            lastClip = wc;
            wait (12);
        }
    }
    void decode (Clip& c, int i)
    {
        { const juce::ScopedLock sl (ed.cacheLock); if (c.cache[(size_t) i].isValid()) return; }
        auto im = decodeWebP (c.frames[(size_t) i].first, c.frames[(size_t) i].second);
        const juce::ScopedLock sl (ed.cacheLock);
        c.cache[(size_t) i] = im;
    }
    Dot50Editor& ed;
    std::atomic<int> wantClip { -1 }, wantIdx { 0 };
};

Dot50Editor::Dot50Editor (Dot50Processor& p) : juce::AudioProcessorEditor (&p), proc (p)
{
    typeface = juce::Typeface::createSystemTypefaceFor (BinaryData::cs_regular_ttf, (size_t) BinaryData::cs_regular_ttfSize);

    loadPack (BinaryData::idle_pack,    clipdata::IDLE,    clips[Idle]);
    loadPack (BinaryData::fire_pack,    clipdata::FIRE,    clips[Fire]);
    loadPack (BinaryData::inspect_pack, clipdata::INSPECT, clips[Inspect]);
    loadPack (BinaryData::reload_pack,  clipdata::RELOAD,  clips[Reload]);

    const Row rows[3] = { { 172.f, .72f }, { 252.f, .92f }, { 338.f, 1.1f } };
    struct L { int r; float x, v; };
    const L lay[8] = { { 0, 390, 55 }, { 0, 580, -62 }, { 1, 330, 80 }, { 1, 480, -72 }, { 1, 640, 85 }, { 2, 230, -110 }, { 2, 500, 125 }, { 2, 770, -100 } };
    const char* names[8] = { "REVERB", "DELAY", "CHORUS", "DRIVE", "CRUSH", "FILTER", "PHASER", "FLANGER" };
    for (int i = 0; i < 8; ++i)
    {
        Target t;
        t.name = names[i]; t.paramId = Dot50Processor::kFxIds[i];
        t.ri = lay[i].r; t.row = rows[lay[i].r]; t.x = lay[i].x; t.vx = lay[i].v * 1.45f; t.ph = rng.nextFloat() * TAU;
        t.on = t.von = proc.apvts.getRawParameterValue (t.paramId)->load() > 0.5f;
        targets.push_back (t);
    }

    // halo des cibles actives
    glowImg = juce::Image (juce::Image::ARGB, 128, 128, true);
    {
        Graphics gg (glowImg);
        juce::ColourGradient gr (rgba (235, 248, 255, .75f), 64.f, 64.f, rgba (205, 232, 245, 0.f), 128.f, 64.f, true);
        gr.addColour (22.0 / 64.0, rgba (235, 248, 255, .75f));
        gr.addColour (.705, rgba (205, 232, 245, .28f));
        gg.setGradientFill (gr); gg.fillRect (0, 0, 128, 128);
    }
    buildBackground();

    cBest = proc.getBestTime();
    th.bestKills = proc.getBestKills(); th.bestAcc = proc.getBestAcc();
    // pas encore de record enregistré -> -1
    if (! proc.apvts.state.hasProperty ("tryBestKills")) th.bestKills = -1;

    setOpaque (true);
    setMouseCursor (juce::MouseCursor::NoCursor);
    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (640, 360, 1920, 1080);
    getConstrainer()->setFixedAspectRatio ((double) W / (double) H);
    setSize (960, 540);
    lastMs = juce::Time::getMillisecondCounterHiRes();
    pf = std::make_unique<Prefetch> (*this);
    setAnim (Draw);
    pf->startThread (juce::Thread::Priority::low);
    startTimerHz (60);
}

Dot50Editor::~Dot50Editor() { stopTimer(); if (pf) pf->stopThread (2000); }
void Dot50Editor::resized() {}

juce::Point<float> Dot50Editor::toLogical (const juce::MouseEvent& e) const
{
    const float sc = (float) getWidth() / W;
    return { e.position.x / sc, e.position.y / sc };
}

// ---------------------------------------------------------------- décor : fond gris infini + grille
void Dot50Editor::buildBackground()
{
    const int S = 2;
    bg = juce::Image (juce::Image::RGB, (int) W * S, (int) H * S, false);
    Graphics g (bg);
    g.addTransform (juce::AffineTransform::scale ((float) S));
    g.fillAll (Colour (0xff99a3ac));                                             // ciel vide
    g.setGradientFill (juce::ColourGradient (Colour (0xff7c8491), 0.f, HY, Colour (0xff6c7480), 0.f, H, false));
    g.fillRect (0.f, HY, W, H - HY);                                            // sol
    // grille : vraies cases carrées au sol, symétrique autour du centre
    const float F = 400.f, ZB = 1.f, SP = .25f, VX = 480.f;
    Path grid;
    for (int j = 4; j < 260; ++j) { const float y = HY + F / ((float) j * SP); if (y > H + 1) continue; grid.startNewSubPath (0.f, y); grid.lineTo (W, y); }
    const float px = F * SP / ZB;
    for (int i = -60; i <= 60; ++i) { grid.startNewSubPath (VX, HY); grid.lineTo (VX + (float) i * px, H); }
    g.setGradientFill (juce::ColourGradient (rgba (226, 232, 238, 0.f), 0.f, HY, rgba (226, 232, 238, .66f), 0.f, HY + 190.f, false));
    g.strokePath (grid, juce::PathStrokeType (1.f));
    // brume d'horizon
    g.setGradientFill (juce::ColourGradient (rgba (153, 163, 172, 1.f), 0.f, HY - 2.f, rgba (153, 163, 172, 0.f), 0.f, HY + 46.f, false));
    g.fillRect (0.f, HY - 2.f, W, 48.f);
    // très léger grain
    juce::Image n (juce::Image::ARGB, 192, 192, true);
    {
        juce::Random r (7);
        juce::Image::BitmapData bd (n, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < 192; ++y)
            for (int x = 0; x < 192; ++x)
            {
                const auto v = (juce::uint8) r.nextInt (256), al = (juce::uint8) r.nextInt (9);
                juce::PixelARGB px2 (al, v, v, v); px2.premultiply();
                *((juce::PixelARGB*) bd.getPixelPointer (x, y)) = px2;
            }
    }
    g.setFillType (juce::FillType (n, juce::AffineTransform()));
    g.fillRect (0.f, 0.f, W, H);
}

// ---------------------------------------------------------------- helpers
juce::Font Dot50Editor::fnt (float px, float ls) const
{
    return juce::Font (juce::FontOptions (typeface).withHeight (px * FONT_H).withKerningFactor (px > 0 ? ls / px : 0.f));
}
float Dot50Editor::textWidth (const juce::String& s, float px, float ls) const
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (fnt (px, ls), s, 0.f, 0.f);
    return ga.getBoundingBox (0, -1, true).getWidth();
}

void Dot50Editor::txt (Graphics& g, const juce::String& s, float x, float y, Colour c, float px, int align, float ls, bool boost)
{
    if (boost && px < 12.f) px *= 1.2f;
    g.setFont (fnt (px, ls));
    const float w = textWidth (s, px, ls) + 8.f, h = px * 1.6f;
    float x0 = x; juce::Justification j = juce::Justification::centredLeft;
    if (align == 0) { x0 = x - w * 0.5f; j = juce::Justification::centred; }
    else if (align > 0) { x0 = x - w; j = juce::Justification::centredRight; }
    g.setColour (Colour::fromFloatRGBA (10 / 255.f, 14 / 255.f, 20 / 255.f, .5f));
    g.drawText (s, Rectangle<float> (x0 + 1.2f, y - h * 0.5f + 1.2f, w, h), j, false);
    g.setColour (c);
    g.drawText (s, Rectangle<float> (x0, y - h * 0.5f, w, h), j, false);
}

void Dot50Editor::panel (Graphics& g, float x, float y, float w, float h, Colour accent, Colour tint, Colour edge)
{
    const float c = 7.f;
    Path p;
    p.startNewSubPath (x + c, y); p.lineTo (x + w, y); p.lineTo (x + w, y + h - c); p.lineTo (x + w - c, y + h);
    p.lineTo (x, y + h); p.lineTo (x, y + c); p.closeSubPath();
    g.setColour (tint); g.fillPath (p);
    g.setColour (edge); g.strokePath (p, juce::PathStrokeType (1.f));
    if (! accent.isTransparent()) { g.setColour (accent); g.fillRect (x, y + c, 3.f, h - c); }
}

void Dot50Editor::drawKnob (Graphics& g, float cx, float cy, float r, float v, Colour col, bool active)
{
    const float a0 = PI * .75f, a1 = PI * 2.25f, av = a0 + (a1 - a0) * v, off = PI * .5f;     // JUCE : 0 = midi, sens horaire
    g.setColour (PDARK); g.fillEllipse (cx - r, cy - r, r * 2, r * 2);
    juce::PathStrokeType st (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    Path track; track.addCentredArc (cx, cy, r - 1.5f, r - 1.5f, 0.f, a0 + off, a1 + off, true);
    g.setColour (PEDGE); g.strokePath (track, st);
    Path val; val.addCentredArc (cx, cy, r - 1.5f, r - 1.5f, 0.f, a0 + off, av + off + 0.0001f, true);
    g.setColour (col); g.strokePath (val, st);
    g.setColour (active ? Colour (0xffffffff) : Colour (0xfff2f7fa));
    g.drawLine (cx + std::cos (av) * 2, cy + std::sin (av) * 2, cx + std::cos (av) * (r - 4), cy + std::sin (av) * (r - 4), 2.f);
}

int Dot50Editor::animFrame() const
{
    const auto& c = clips[anim == Draw ? Idle : anim];
    if (c.n <= 1) return 0;
    return juce::jmin (c.n - 1, (int) std::floor (animT * (float) c.fps));
}

void Dot50Editor::setAnim (int a)
{
    // fondu court si on coupe une animation longue (inspection / rechargement)
    if ((anim == Inspect || anim == Reload) && a != Idle && clips[anim].shown.isValid())
        ghost = { clips[anim].shown, clips[anim].r, gunOff(), 0.f, true };
    anim = a; animT = 0;
    if (a == Idle) nextInspect = time + 3.0 + rng.nextFloat() * 2.0;
    if (pf != nullptr) { pf->wantIdx = 0; pf->wantClip = (a == Fire || a == Inspect || a == Reload) ? a : -1; }
}

juce::Image Dot50Editor::frameImage (Clip& c, int idx)
{
    idx = juce::jlimit (0, (int) c.frames.size() - 1, idx);
    const int ci = (int) (&c - clips);
    if (pf != nullptr && ci >= Fire && ci <= Reload) { pf->wantClip = ci; pf->wantIdx = idx; }
    juce::Image im;
    { const juce::ScopedLock sl (cacheLock); im = c.cache[(size_t) idx]; }
    if (im.isValid()) { c.shown = im; return im; }
    if (c.shown.isValid()) return c.shown;               // pas encore prête : on garde l'image précédente (pas de flash)
    im = decodeWebP (c.frames[(size_t) idx].first, c.frames[(size_t) idx].second);   // tout premier affichage uniquement
    c.shown = im;
    return im;
}

// ---------------------------------------------------------------- potards (paramètres du plugin)
juce::RangedAudioParameter* Dot50Editor::knobParam (int i) const
{
    if (i >= 0)  return proc.apvts.getParameter (kKnobs[i].id);
    if (i == -1) return proc.apvts.getParameter ("mix_global");
    return proc.apvts.getParameter ("sfx_volume");
}
float Dot50Editor::getKnob (int i) const
{
    auto* p = knobParam (i); if (p == nullptr) return 0.f;
    const float v = p->getValue();
    return (i >= 0 && kKnobs[i].invert) ? 1.f - v : v;
}
void Dot50Editor::setKnob (int i, float v)
{
    auto* p = knobParam (i); if (p == nullptr) return;
    v = juce::jlimit (0.f, 1.f, v);
    float norm = (i >= 0 && kKnobs[i].invert) ? 1.f - v : v;
    if (i == 4) { const float bits = std::round (p->convertFrom0to1 (norm)); norm = p->convertTo0to1 (bits); }   // CRUSH : bits entiers
    p->setValueNotifyingHost (norm);
}
juce::String Dot50Editor::knobText (int i) const
{
    auto* p = knobParam (i); if (p == nullptr) return {};
    const float real = p->convertFrom0to1 (p->getValue());
    if (i == 0) return juce::String (real, 1) + "S";
    if (i == 4) return juce::String (juce::roundToInt (real)) + " BIT";
    if (i == 5) return real >= 1000.f ? juce::String (real / 1000.f, 1) + "K" : juce::String (juce::roundToInt (real)) + "HZ";
    return pct (real);
}
Rectangle<float> Dot50Editor::knobZone (int i) const
{
    if (i == -1) return { 136.f, H - 62.f, 48.f, 48.f };
    if (i == -2) return { 612.f, H - 62.f, 76.f, 48.f };
    return { 166.f + (float) i * 90.f, 39.f, 86.f, 33.f };
}
int Dot50Editor::knobAt (Point<float> p) const
{
    if (th.on) return -9;
    if (knobZone (-1).contains (p)) return -1;
    if (knobZone (-2).contains (p)) return -2;
    for (int i = 0; i < 8; ++i) if (knobZone (i).contains (p)) return i;
    return -9;
}

// ---------------------------------------------------------------- logique de jeu
float Dot50Editor::tgtY (const Target& t) const { return t.row.y + std::sin ((float) time * 1.3f + t.ph) * 7.f * t.row.s; }
float Dot50Editor::tgtR (const Target& t) const { return 35.f * t.row.s; }
int Dot50Editor::activeCount() const { int n = 0; for (auto& t : targets) n += t.on ? 1 : 0; return n; }

juce::Point<float> Dot50Editor::gunOff() const
{
    float y = sway.y + (anim == Idle ? std::sin ((float) time * 1.4f) * 1.8f : 0.f);
    if (anim == Draw) y += (1.f - eo (juce::jmin (1.f, animT / .55f))) * 330.f;
    return { sway.x, y };
}

juce::Point<float> Dot50Editor::muzzle() const
{
    const auto o = gunOff();
    float x, y;
    if (anim == Fire)
    {
        const int f = juce::jlimit (0, 38, animFrame());
        const float sc = 1.f - FSH * clipdata::FIRE_E[f];
        x = FPX + (clipdata::FIRE_MZ[f][0] - FPX) * sc + o.x; y = FPY + (clipdata::FIRE_MZ[f][1] - FPY) * sc + o.y;
    }
    else { x = clipdata::IDLE_MUZ_X + o.x; y = clipdata::IDLE_MUZ_Y + o.y; }
    return { GPX + (x - GPX) * GS, juce::jmax (20.f, GPY + (y - GPY) * GS) };
}

Dot50Editor::Target* Dot50Editor::pick (float x, float y)
{
    Target* best = nullptr;
    for (auto& t : targets)
    {
        if (! t.alive || time - t.flipT <= .45) continue;
        const float dx = x - t.x, dy = y - tgtY (t), r = tgtR (t) * .9f;
        if (dx * dx + dy * dy <= r * r && (best == nullptr || t.ri > best->ri)) best = &t;
    }
    return best;
}

void Dot50Editor::setFx (int i, bool on)
{
    auto* p = proc.apvts.getParameter (targets[(size_t) i].paramId);
    p->beginChangeGesture(); p->setValueNotifyingHost (on ? 1.f : 0.f); p->endChangeGesture();
    targets[(size_t) i].on = on;
}

void Dot50Editor::shatter (const Target& t)
{
    const float x = t.x, y = tgtY (t), R = tgtR (t);
    for (int i = 0; i < 12; ++i)
    {
        const float a = rng.nextFloat() * TAU, sp = 90.f + rng.nextFloat() * 210.f, d = rng.nextFloat() * R * .7f;
        Particle p;
        p.x = x + std::cos (a) * d; p.y = y + std::sin (a) * d; p.vx = std::cos (a) * sp; p.vy = std::sin (a) * sp - 120.f;
        p.rot = rng.nextFloat() * TAU; p.vr = (rng.nextFloat() - .5f) * 24.f; p.life = 0; p.max = .7f + rng.nextFloat() * .6f;
        p.size = (3.f + rng.nextFloat() * 5.f) * t.row.s;
        p.col = i % 3 == 0 ? Colour (0xfff1f6f9) : (i % 3 == 1 ? FXCOL : Colour (0xff39424d));
        particles.push_back (p);
    }
    rings.push_back ({ x, y, R * .6f, 0.f, .45f, FXCOL });
    rings.push_back ({ x, y, R * .3f, 0.f, .3f, Colour (0xffffffff) });
}

void Dot50Editor::shoot()
{
    if (th.on) { thShoot(); return; }
    if (anim == Reload || anim == Draw) return;
    if (ammo <= 0) { startReload(); return; }
    const float jx = (rng.nextFloat() * 2 - 1) * spread * 9.f, jy = (rng.nextFloat() * 2 - 1) * spread * 9.f;
    const float ax = mouse.x + jx, ay = mouse.y + jy;
    --ammo; setAnim (Fire); flash = 1; spread = 1; shake = juce::jmax (shake, 5.f);
    proc.triggerSfx (frange::Sfx::Shot);
    const auto m = muzzle();
    tracers.push_back ({ m.x, m.y, ax, ay, 0.f, .12f });
    if (auto* hit = pick (ax, ay))
    {
        const int i = (int) (hit - targets.data());
        auto& t = targets[(size_t) i];
        setFx (i, ! t.on);
        t.flipT = time; t.pendUntil = time + .2;
        t.hasHole = true; t.hx = (ax - t.x) / tgtR (t); t.hy = (ay - tgtY (t)) / tgtR (t); t.hole0 = time;
        if (cs == CState::Ready) { cs = CState::Run; cT0 = juce::Time::getMillisecondCounterHiRes(); cT = 0; }
        if (cs == CState::Run && activeCount() == 8)
        {
            cT = (juce::Time::getMillisecondCounterHiRes() - cT0) / 1000.0; cs = CState::Done; cDoneT = time;
            cNewBest = (cBest <= 0.0 || cT < cBest);
            if (cNewBest) { cBest = cT; proc.setBestTime (cBest); }
            winAt = time + 0.25;
        }
        shatter (t);
        proc.triggerSfx (t.on ? frange::Sfx::HitOn : frange::Sfx::HitOff);
        hitMark = 1; hitCol = FXCOL; shake = juce::jmax (shake, 6.f);
        feed.insert (feed.begin(), { t.name, t.on, 0.f }); if (feed.size() > 5) feed.pop_back();
        texts.push_back ({ t.x, tgtY (t) - tgtR (t) - 10.f, 0.f, 1.1f, juce::String (t.name) + (t.on ? " ON" : " OFF"), t.on ? Colour (0xffffffff) : Colour (0xffc4cdd6) });
    }
    else decals.push_back ({ ax, ay, 0.f });
    if (ammo == 0) autoReloadAt = time + .12;
}

void Dot50Editor::startReload()
{
    if (th.on || anim == Reload || anim == Draw || ammo == MAG) return;
    reloading = true; setAnim (Reload); proc.triggerSfx (frange::Sfx::Reload);
}

void Dot50Editor::clearAll()
{
    cs = CState::Ready; cT = 0;
    for (int i = 0; i < 8; ++i)
    {
        auto& t = targets[(size_t) i];
        if (t.on) setFx (i, false);
        t.von = false; t.pendUntil = 0;
        if (! t.alive) { t.alive = true; t.spawnT = time; }
    }
    proc.triggerSfx (frange::Sfx::Click);
}

void Dot50Editor::emitSparks (float dt)
{
    for (auto& t : targets)
    {
        if (! t.alive || ! t.on) continue;
        t.emit += dt;
        const float R = tgtR (t), y = tgtY (t);
        while (t.emit > .24f)
        {
            t.emit -= .24f;
            if (sparks.size() > 45) break;
            if (rng.nextFloat() < .45f)
                sparks.push_back ({ true, t.x + (rng.nextFloat() * 1.1f - .55f) * R, y + R * .2f, -(55.f + rng.nextFloat() * 30.f), 0.f, 1.f + rng.nextFloat() * .5f, t.row.s });
            else
                sparks.push_back ({ false, t.x + (rng.nextFloat() * 2.f - 1.f) * R, y + (rng.nextFloat() * 1.1f - .2f) * R, -(40.f + rng.nextFloat() * 70.f), 0.f, .8f + rng.nextFloat() * .6f, t.row.s });
        }
    }
}

void Dot50Editor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float) juce::jmin (0.05, (now - lastMs) / 1000.0);
    lastMs = now;
    update (dt);
    repaint();
}

void Dot50Editor::update (float dt)
{
    time += dt; hintT += dt;
    if (th.on) thUpdate (dt);

    for (auto& t : targets)
    {
        // l'hôte peut modifier un effet (automation / preset) : la cible suit
        const bool pv = proc.apvts.getRawParameterValue (t.paramId)->load() > 0.5f;
        if (pv != t.on) { t.on = pv; t.von = pv; t.pendUntil = 0; }
        if (t.alive)
        {
            t.x += t.vx * (1.f + .38f * std::sin ((float) time * .9f + t.ph)) * dt;
            const float R = tgtR (t), lo = R + 24.f, hi = W - R - 24.f;
            if (t.x < lo) { t.x = lo; t.vx = std::abs (t.vx); }
            if (t.x > hi) { t.x = hi; t.vx = -std::abs (t.vx); }
        }
        else if (time >= t.respawn) { t.alive = true; t.spawnT = time; }
        if (t.pendUntil > 0 && time >= t.pendUntil) { t.von = t.on; t.pendUntil = 0; }
    }
    // collisions entre cibles d'une même rangée : elles se repoussent et échangent leur vitesse
    for (size_t i = 0; i < targets.size(); ++i)
        for (size_t j = i + 1; j < targets.size(); ++j)
        {
            auto& a = targets[i]; auto& b = targets[j];
            if (a.ri != b.ri || ! a.alive || ! b.alive) continue;
            auto& L = a.x < b.x ? a : b; auto& Rr = a.x < b.x ? b : a;
            const float gap = Rr.x - L.x, minD = tgtR (L) + tgtR (Rr) + 8.f;
            if (gap < minD)
            {
                const float push = (minD - gap) / 2.f; L.x -= push; Rr.x += push;
                if (L.vx > Rr.vx) std::swap (L.vx, Rr.vx);
            }
        }

    const float k = juce::jmin (1.f, dt * 6.f);
    sway.x += (((mouse.x - W / 2) * .05f) - sway.x) * k;
    sway.y += (((mouse.y - H * .45f) * .035f) - sway.y) * k;

    // animation de l'arme
    animT += dt;
    if (ghost.on) { ghost.t += dt; if (ghost.t > .14f) ghost.on = false; }
    if (anim == Draw) { if (animT >= .55f) setAnim (Idle); }
    else if (anim == Idle)
    {
        if (time >= nextInspect && ! reloading && ! (th.on && th.state == 2)) setAnim (Inspect);
    }
    else
    {
        auto& c = clips[anim];
        if (animT * (float) c.fps >= (float) c.n)
        {
            if (anim == Reload) { reloading = false; ammo = MAG; }
            setAnim (Idle);
        }
    }

    flash = juce::jmax (0.f, flash - dt * 10.f); spread = juce::jmax (0.f, spread - dt * 5.f);
    hitMark = juce::jmax (0.f, hitMark - dt * 4.f); shake = juce::jmax (0.f, shake - dt * 30.f);
    if (autoReloadAt > 0 && time >= autoReloadAt) { autoReloadAt = -1; startReload(); }
    if (winAt > 0 && time >= winAt) { proc.triggerSfx (frange::Sfx::Win); winAt = -1; }

    const float lv = proc.getOutLevel();
    const float db = 20.f * std::log10 (lv + 1e-6f), target = juce::jlimit (0.f, 1.f, (db + 48.f) / 48.f);
    outLevelSmooth += (target - outLevelSmooth) * juce::jmin (1.f, dt * 12.f);
}

// ---------------------------------------------------------------- mode TRYHARD (30 s, cibles turquoise fixes qui apparaissent progressivement)
void Dot50Editor::thReset() { th.shots = th.hits = th.spawned = 0; th.ttk = 0; th.list.clear(); th.hasRes = false; }

void Dot50Editor::thEnter()
{
    if (th.on) return;
    for (int i = 0; i < 8; ++i) th.saved[i] = targets[(size_t) i].on;
    for (int i = 0; i < 8; ++i)                                        // on oublie les effets pendant le mode
    {
        auto& t = targets[(size_t) i];
        if (t.on) setFx (i, false);
        t.von = false; t.pendUntil = 0;
    }
    cs = CState::Ready; cT = 0; feed.clear(); texts.clear(); particles.clear(); rings.clear(); decals.clear();
    reloading = false; ammo = MAG; autoReloadAt = -1;
    thReset(); th.on = true; th.state = 1; th.tc = time;
    proc.triggerSfx (frange::Sfx::Click);
}

void Dot50Editor::thExit()
{
    if (! th.on) return;
    th.on = false; th.state = 0; th.list.clear();
    for (int i = 0; i < 8; ++i)
    {
        auto& t = targets[(size_t) i];
        if (t.on != th.saved[i]) setFx (i, th.saved[i]);
        t.von = t.on; t.pendUntil = 0;
    }
    particles.clear(); rings.clear(); texts.clear();
    proc.triggerSfx (frange::Sfx::Click);
}

void Dot50Editor::thRetry() { thReset(); th.state = 1; th.tc = time; proc.triggerSfx (frange::Sfx::Click); }

void Dot50Editor::thSpawn()
{
    for (int k = 0; k < 50; ++k)
    {
        const float x = 90.f + rng.nextFloat() * 780.f, y = 112.f + rng.nextFloat() * 318.f;
        if (x > 500.f && y > 290.f) continue;                    // zone de l'arme
        if (x < 245.f && y < 205.f) continue;                    // panneau de stats
        bool close = false;
        for (auto& t : th.list) if (std::hypot (t.x - x, t.y - y) < TR * 3.f) { close = true; break; }
        if (close) continue;
        th.list.push_back ({ x, y, time }); ++th.spawned; return;
    }
}

void Dot50Editor::thFinish()
{
    th.state = 3; th.list.clear();
    const int k = th.hits; const float acc = th.shots ? (float) th.hits / (float) th.shots * 100.f : 0.f;
    th.rKills = k; th.rShots = th.shots; th.rAcc = acc; th.rKps = (float) k / th.dur;
    th.rTtk = k ? (float) (th.ttk / k * 1000.0) : 0.f; th.rTargets = th.spawned; th.hasRes = true; th.newBest = false;
    if (th.bestKills < 0 || k > th.bestKills) { th.newBest = true; th.bestKills = k; th.bestAcc = acc; proc.setBestTry (k, acc); }
    proc.triggerSfx (frange::Sfx::Win);
}

void Dot50Editor::thUpdate (float)
{
    if (th.state == 1) { if (time - th.tc >= 3.0) { th.state = 2; th.t0 = time; th.next = time; proc.triggerSfx (frange::Sfx::Click); } }
    else if (th.state == 2)
    {
        const double el = time - th.t0;
        if (el >= th.dur) { thFinish(); return; }
        const int maxAct = 4 + (el > 8 ? 1 : 0) + (el > 16 ? 1 : 0);
        if (time >= th.next && (int) th.list.size() < maxAct) { thSpawn(); th.next = time + .25 + rng.nextFloat() * .2; }
    }
}

void Dot50Editor::thShoot()
{
    if (th.state == 1 || th.state == 3) return;
    setAnim (Fire); flash = 1; spread = .5f; shake = juce::jmax (shake, 3.f);
    proc.triggerSfx (frange::Sfx::Shot);
    const auto m = muzzle(); tracers.push_back ({ m.x, m.y, mouse.x, mouse.y, 0.f, .1f });
    ++th.shots;
    int hit = -1;
    for (int i = (int) th.list.size() - 1; i >= 0; --i)
        if (std::hypot (mouse.x - th.list[(size_t) i].x, mouse.y - th.list[(size_t) i].y) <= TR) { hit = i; break; }
    if (hit >= 0)
    {
        const auto t = th.list[(size_t) hit]; th.list.erase (th.list.begin() + hit);
        ++th.hits; th.ttk += time - t.born;
        for (int i = 0; i < 10; ++i)
        {
            const float a = rng.nextFloat() * TAU, sp = 90.f + rng.nextFloat() * 190.f;
            Particle p; p.x = t.x; p.y = t.y; p.vx = std::cos (a) * sp; p.vy = std::sin (a) * sp - 80.f; p.rot = rng.nextFloat() * TAU;
            p.vr = (rng.nextFloat() - .5f) * 20.f; p.life = 0; p.max = .4f + rng.nextFloat() * .4f; p.size = 3.f + rng.nextFloat() * 4.f;
            p.col = i % 2 ? TCOL : Colour (0xffeafffd); particles.push_back (p);
        }
        rings.push_back ({ t.x, t.y, TR * .5f, 0.f, .3f, TCOL });
        hitMark = 1; hitCol = TCOL; proc.triggerSfx (frange::Sfx::Tick);
    }
    else decals.push_back ({ mouse.x, mouse.y, 0.f });
}

bool Dot50Editor::thClick (Point<float> p)
{
    if (th.state == 3) { if (inRect (p, THRETRY)) thRetry(); else if (inRect (p, THEXIT)) thExit(); return true; }
    if (inRect (p, THX)) { thExit(); return true; }
    thShoot(); return true;
}

// ---------------------------------------------------------------- entrées
void Dot50Editor::mouseMove (const juce::MouseEvent& e) { mouse = toLogical (e); hasMouse = true; }
void Dot50Editor::mouseExit (const juce::MouseEvent&) { hasMouse = false; }

void Dot50Editor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    mouse = toLogical (e); hasMouse = true;
    if (th.on) { thClick (mouse); return; }
    if (inRect (mouse, THB)) { thEnter(); return; }
    const int ki = knobAt (mouse);
    if (ki != -9)
    {
        const double n = juce::Time::getMillisecondCounterHiRes();
        auto* p = knobParam (ki);
        if (lastTapKnob == ki && n - lastTapT < 350.0)
        {
            p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture();
            lastTapT = 0; return;
        }
        lastTapKnob = ki; lastTapT = n;
        dragKnob = ki; dragY0 = mouse.y; dragX0 = mouse.x; dragV0 = getKnob (ki);
        p->beginChangeGesture();
        return;
    }
    if (SFXBTN.contains (mouse))
    {
        auto* p = proc.apvts.getParameter ("sfx_on");
        p->beginChangeGesture(); p->setValueNotifyingHost (p->getValue() > .5f ? 0.f : 1.f); p->endChangeGesture();
        return;
    }
    if (RLD.contains (mouse)) { startReload(); return; }
    if (mouse.x > 886.f && mouse.y < 74.f) { clearAll(); return; }
    if (mouse.x >= 196.f && mouse.x <= 690.f && mouse.y >= H - 62.f && mouse.y <= H - 14.f) return;      // panneau du bas
    shoot();
}

void Dot50Editor::mouseDrag (const juce::MouseEvent& e)
{
    mouse = toLogical (e); hasMouse = true;
    if (dragKnob != -9) setKnob (dragKnob, dragV0 + (dragY0 - mouse.y) / 110.f + (mouse.x - dragX0) / 330.f);
}

void Dot50Editor::mouseUp (const juce::MouseEvent&)
{
    if (dragKnob != -9) { if (auto* p = knobParam (dragKnob)) p->endChangeGesture(); dragKnob = -9; }
}

void Dot50Editor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int i = knobAt (toLogical (e));
    if (i == -9) return;
    auto* p = knobParam (i);
    p->beginChangeGesture();
    setKnob (i, getKnob (i) + (w.deltaY > 0 ? 1.f : -1.f) * 0.04f * juce::jmax (0.25f, std::abs (w.deltaY) * 4.f));
    p->endChangeGesture();
}

bool Dot50Editor::keyPressed (const juce::KeyPress& k)
{
    const auto c = juce::CharacterFunctions::toUpperCase (k.getTextCharacter());
    auto toggleSfx = [this] { auto* p = proc.apvts.getParameter ("sfx_on"); p->beginChangeGesture(); p->setValueNotifyingHost (p->getValue() > .5f ? 0.f : 1.f); p->endChangeGesture(); };
    if (c == 'M') { toggleSfx(); return true; }
    if (th.on)
    {
        if (k == juce::KeyPress::escapeKey) { thExit(); return true; }
        if (k == juce::KeyPress::spaceKey) { thShoot(); return true; }
        if ((c == 'R' || k == juce::KeyPress::returnKey) && th.state == 3) { thRetry(); return true; }
        return true;
    }
    if (k == juce::KeyPress::spaceKey) { shoot(); return true; }
    if (c == 'R') { startReload(); return true; }
    if (c == 'C') { clearAll(); return true; }
    return false;
}

// ---------------------------------------------------------------- dessin
void Dot50Editor::paint (Graphics& g)
{
    const float sc = (float) getWidth() / W;
    g.fillAll (Colour (0xff99a3ac));
    g.addTransform (juce::AffineTransform::scale (sc));
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    const float dt = 1.f / 60.f;

    g.saveState();
    if (shake > 0) g.addTransform (juce::AffineTransform::translation ((rng.nextFloat() * 2 - 1) * shake * .35f, (rng.nextFloat() * 2 - 1) * shake * .35f));
    if (bg.isValid()) g.drawImage (bg, Rectangle<float> (-6.f, -6.f, W + 12.f, H + 12.f));
    if (th.on) thDrawTargets (g);
    else for (int r = 0; r < 3; ++r) for (auto& t : targets) if (t.ri == r) drawTarget (g, t);
    drawFx (g, dt);
    g.restoreState();

    drawGun (g);
    if (th.on) thHUD (g, dt); else drawHUD (g, dt);
    drawCrosshair (g);
}

void Dot50Editor::drawTarget (Graphics& g, const Target& t)
{
    if (! t.alive) return;
    const float k = juce::jmin (1.f, (float) (time - t.spawnT) / .35f), sc = juce::jmax (.001f, easeOutBack (k));
    const float x = t.x, y = tgtY (t), R = tgtR (t), s = t.row.s;
    const bool on = t.von;
    const float fk = (float) (time - t.flipT) / .4f, sx = fk < 1.f ? juce::jmax (.05f, std::abs (std::cos (fk * PI))) : 1.f;

    // ombre au sol (plus petite et plus pâle quand la cible monte)
    {
        const float sy = SHY[t.ri], hgt = juce::jmax (0.f, (sy - y - R) / 90.f), hh = juce::jmin (1.f, hgt);
        g.setColour (rgba (32, 40, 52, (.30f - .1f * hh) * sc));
        const float rw = R * (.95f - .12f * hh) * sc, rh = R * .24f * sc;
        g.fillEllipse (x - rw, sy - rh, rw * 2, rh * 2);
    }
    g.saveState();
    g.addTransform (juce::AffineTransform::scale (sc).translated (x, y));
    if (on)
    {
        const float pul = .5f + .5f * std::sin ((float) time * 4.f + t.ph), ar = R * (2.05f + .15f * pul);
        g.setOpacity (.6f + .35f * pul);
        g.drawImage (glowImg, Rectangle<float> (-ar, -ar, ar * 2, ar * 2));
        g.setOpacity (1.f);
    }
    // disque (clair = OFF, graphite = ON) — se retourne quand on le touche
    g.saveState();
    g.addTransform (juce::AffineTransform::scale (sx, 1.f));
    g.setColour (rgba (32, 40, 52, .20f)); g.fillEllipse (4 * s - R, 5 * s - R, R * 2, R * 2);
    {
        const Colour c0 = on ? Colour (0xff566273) : Colour (0xfff6f9fb), c1 = on ? Colour (0xff2a313b) : Colour (0xffcdd8e0);
        juce::ColourGradient gr (c0, -R * .28f, -R * .32f, c1, -R * .28f + R * 1.2f, -R * .32f, true);
        gr.addColour (.08, c0);
        g.setGradientFill (gr); g.fillEllipse (-R, -R, R * 2, R * 2);
    }
    g.setColour (on ? Colour (0xffeef6fa) : Colour (0xff39424d)); g.drawEllipse (-R, -R, R * 2, R * 2, 2.4f * s);
    g.setColour (on ? rgba (238, 246, 250, .8f) : rgba (57, 66, 77, .8f));
    for (float f : { .86f, .52f }) g.drawEllipse (-R * f, -R * f, R * f * 2, R * f * 2, 1.3f * s);
    g.setColour (on ? Colour (0xffffffff) : Colour (0xff8fa3b3)); g.drawEllipse (-R * .69f, -R * .69f, R * .69f * 2, R * .69f * 2, 6.f * s);
    g.setColour (on ? Colour (0xffffffff) : Colour (0xff2a313b)); g.fillEllipse (-R * .2f, -R * .2f, R * .4f, R * .4f);
    if (t.hasHole && time - t.hole0 < 1.1 && fk < .5f)
    {
        const float q = juce::jmax (0.f, 1.f - (float) (time - t.hole0) / 1.1f), hx = t.hx * R, hy = t.hy * R;
        g.setColour (A (Colour (0xff161b22), juce::jmin (1.f, q * 2.f))); g.fillEllipse (hx - 3.2f * s, hy - 3.2f * s, 6.4f * s, 6.4f * s);
        const float rr = (5.f + (1.f - q) * 6.f) * s;
        g.setColour (A (rgba (255, 255, 255, .85f), juce::jmin (1.f, q * 2.f))); g.drawEllipse (hx - rr, hy - rr, rr * 2, rr * 2, 1.4f * s);
    }
    g.restoreState();

    // plaque nom : panneau HUD, coins coupés
    {
        const juce::String nm (t.name);
        const float fs = juce::jmax (9.5f, 10.5f * s);
        const float tw = textWidth (nm, fs, 1.f * s), pw = juce::jmax (56 * s, tw + 24 * s), ph = juce::jmax (18 * s, fs * 1.9f), py = R + 9 * s, cc = 5 * s, px0 = -pw / 2;
        Path p;
        p.startNewSubPath (px0 + cc, py); p.lineTo (px0 + pw, py); p.lineTo (px0 + pw, py + ph - cc); p.lineTo (px0 + pw - cc, py + ph);
        p.lineTo (px0, py + ph); p.lineTo (px0, py + cc); p.closeSubPath();
        g.setColour (on ? rgba (86, 98, 115, .9f) : rgba (34, 40, 50, .78f)); g.fillPath (p);
        g.setColour (on ? rgba (255, 255, 255, .85f) : rgba (255, 255, 255, .28f)); g.strokePath (p, juce::PathStrokeType (1.1f));
        g.setColour (on ? Colour (0xffffffff) : FXCOL); g.fillRect (px0, py + cc, 3 * s, ph - cc);
        txt (g, nm, 1.5f * s, py + ph / 2, on ? Colour (0xffffffff) : Colour (0xffeef3f6), fs, 0, 1.f * s, false);
    }
    if (on)
    {
        // anneau pointillé animé
        const float Rr = R * 1.17f, off = (float) time * 30.f / Rr;
        Path ring; ring.addCentredArc (0.f, 0.f, Rr, Rr, 0.f, off, off + TAU, true);
        Path dashed; const float dl[2] = { 9 * s, 7 * s };
        juce::PathStrokeType (2.6f * s).createDashedStroke (dashed, ring, dl, 2);
        g.setColour (Colour (0xffffffff)); g.strokePath (dashed, juce::PathStrokeType (2.6f * s));
        // pastille "coche"
        const float bx = R * .9f, by = -R * .9f, br = 10.5f * s;
        g.setColour (Colour (0xffffffff)); g.fillEllipse (bx - br, by - br, br * 2, br * 2);
        g.setColour (Colour (0xff2a313b)); g.drawEllipse (bx - br, by - br, br * 2, br * 2, 2.f * s);
        Path tick; tick.startNewSubPath (bx - 4.5f * s, by + .5f * s); tick.lineTo (bx - 1 * s, by + 4 * s); tick.lineTo (bx + 5 * s, by - 3.5f * s);
        g.strokePath (tick, juce::PathStrokeType (2.6f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.restoreState();
}

void Dot50Editor::drawFx (Graphics& g, float dt)
{
    emitSparks (dt);
    for (int i = (int) sparks.size() - 1; i >= 0; --i)
    {
        auto& p = sparks[(size_t) i];
        p.life += dt; if (p.life > p.max) { sparks.erase (sparks.begin() + i); continue; }
        p.y += p.vy * dt;
        const float a = std::sin (juce::jmin (1.f, p.life / p.max) * PI);
        if (p.chev)
        {
            const float w = 9 * p.s;
            Path c; c.startNewSubPath (p.x - w, p.y + w * .5f); c.lineTo (p.x, p.y - w * .5f); c.lineTo (p.x + w, p.y + w * .5f);
            g.setColour (A (Colour (0xffffffff), a * .95f));
            g.strokePath (c, juce::PathStrokeType (2.6f * p.s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else { g.setColour (A (Colour (0xffffffff), a * .95f)); g.fillEllipse (p.x - 2.2f * p.s, p.y - 2.2f * p.s, 4.4f * p.s, 4.4f * p.s); }
    }
    for (int i = (int) decals.size() - 1; i >= 0; --i)
    {
        auto& d = decals[(size_t) i]; d.life += dt;
        if (d.life > 7.f) { decals.erase (decals.begin() + i); continue; }
        const float a = juce::jmax (0.f, 1.f - d.life / 7.f);
        g.setColour (A (Colour (0xff262d37), a)); g.fillEllipse (d.x - 3.2f, d.y - 3.2f, 6.4f, 6.4f);
        g.setColour (A (rgba (255, 255, 255, .55f), a)); g.drawEllipse (d.x - 5.f, d.y - 5.f, 10.f, 10.f, 1.f);
    }
    for (int i = (int) particles.size() - 1; i >= 0; --i)
    {
        auto& p = particles[(size_t) i];
        p.life += dt; if (p.life > p.max) { particles.erase (particles.begin() + i); continue; }
        p.vy += 900.f * dt; p.x += p.vx * dt; p.y += p.vy * dt; p.rot += p.vr * dt;
        const float al = 1.f - std::pow (p.life / p.max, 2.f);
        Path tri; tri.startNewSubPath (0, -p.size); tri.lineTo (p.size * .8f, p.size * .6f); tri.lineTo (-p.size * .7f, p.size * .5f); tri.closeSubPath();
        const auto tf = juce::AffineTransform::rotation (p.rot).translated (p.x, p.y);
        g.setColour (A (p.col, al)); g.fillPath (tri, tf);
        g.setColour (A (Colour (0xffffffff), .4f * al)); g.strokePath (tri, juce::PathStrokeType (1.f), tf);
    }
    for (int i = (int) rings.size() - 1; i >= 0; --i)
    {
        auto& r = rings[(size_t) i]; r.life += dt;
        if (r.life > r.max) { rings.erase (rings.begin() + i); continue; }
        const float k = r.life / r.max, rr = r.r + k * 70.f;
        g.setColour (A (r.col, 1.f - k)); g.drawEllipse (r.x - rr, r.y - rr, rr * 2, rr * 2, 4.f * (1.f - k) + 1.f);
    }
    for (int i = (int) tracers.size() - 1; i >= 0; --i)
    {
        auto& t = tracers[(size_t) i]; t.life += dt;
        if (t.life > t.max) { tracers.erase (tracers.begin() + i); continue; }
        g.setColour (A (Colour (0xffffffff), 1.f - t.life / t.max)); g.drawLine (t.x1, t.y1, t.x2, t.y2, 2.f);
    }
    for (int i = (int) texts.size() - 1; i >= 0; --i)
    {
        auto& t = texts[(size_t) i]; t.life += dt;
        if (t.life > t.max) { texts.erase (texts.begin() + i); continue; }
        const float k = t.life / t.max, a = 1.f - k * k;
        g.saveState(); g.setOpacity (a);
        g.setFont (fnt (14.f, 1.f));
        const float w = textWidth (t.txt, 14.f, 1.f) + 8.f, y = t.y - k * 46.f;
        g.setColour (Colour (0xff000000)); g.drawText (t.txt, Rectangle<float> (t.x - w / 2 + 1.5f, y - 11.f + 1.5f, w, 22.f), juce::Justification::centred, false);
        g.setColour (t.col); g.drawText (t.txt, Rectangle<float> (t.x - w / 2, y - 11.f, w, 22.f), juce::Justification::centred, false);
        g.restoreState();
    }
}

void Dot50Editor::drawGun (Graphics& g)
{
    const int ci = anim == Draw ? Idle : anim;
    auto& c = clips[ci];
    const int i = animFrame();
    const auto o = gunOff();
    g.saveState();
    g.addTransform (juce::AffineTransform::translation (-GPX, -GPY).scaled (GS).translated (GPX, GPY));
    if (ghost.on)
    {
        g.setOpacity (1.f - ghost.t / .14f);
        g.drawImage (ghost.img, Rectangle<float> (ghost.r.getX() + ghost.o.x, ghost.r.getY() + ghost.o.y, ghost.r.getWidth(), ghost.r.getHeight()));
        g.setOpacity (1.f);
    }
    auto img = frameImage (c, i);
    if (img.isValid())
    {
        if (ci == Fire)
        {
            const float sc = 1.f - FSH * clipdata::FIRE_E[juce::jlimit (0, 38, i)];
            g.saveState();
            g.addTransform (juce::AffineTransform::translation (-FPX, -FPY).scaled (sc).translated (FPX + o.x, FPY + o.y));
            g.drawImage (img, c.r);
            g.restoreState();
        }
        else g.drawImage (img, Rectangle<float> (c.r.getX() + o.x, c.r.getY() + o.y, c.r.getWidth(), c.r.getHeight()));
    }
    g.restoreState();

    if (flash > 0.02f && anim == Fire)
    {
        const float f = flash * .9f; const auto m = muzzle();
        g.saveState(); g.addTransform (juce::AffineTransform::translation (m.x, m.y));
        juce::ColourGradient gr (rgba (255, 252, 235, .97f), 0.f, 0.f, rgba (255, 170, 60, 0.f), 64.f * f, 0.f, true);
        gr.addColour (.4, rgba (255, 210, 120, .55f));
        g.setGradientFill (gr); g.fillEllipse (-64 * f, -64 * f, 128 * f, 128 * f);
        Path star;
        for (int k = 0; k < 12; ++k)
        {
            const float a = (float) k / 12.f * TAU + .3f, r = (k % 2 ? 10.f : 34.f) * f;
            if (k == 0) star.startNewSubPath (std::cos (a) * r, std::sin (a) * r); else star.lineTo (std::cos (a) * r, std::sin (a) * r);
        }
        star.closeSubPath();
        g.setColour (rgba (255, 250, 215, f)); g.fillPath (star);
        g.restoreState();
    }
}

void Dot50Editor::drawCrosshair (Graphics& g)
{
    if (! hasMouse) return;
    const float x = mouse.x, y = mouse.y, gap = 2.5f + spread * 4.5f, len = 5.5f;
    auto arms = [&] (Colour c, float w)
    {
        Path p;
        p.startNewSubPath (x - gap - len, y); p.lineTo (x - gap, y); p.startNewSubPath (x + gap, y); p.lineTo (x + gap + len, y);
        p.startNewSubPath (x, y - gap - len); p.lineTo (x, y - gap); p.startNewSubPath (x, y + gap); p.lineTo (x, y + gap + len);
        g.setColour (c); g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
    };
    arms (A (Colour (0xff000000), .95f), 3.f);
    arms (rgba (255, 40, 40, .35f), 2.6f);          // lueur
    arms (Colour (0xffff2a2a), 1.4f);
    g.setColour (Colour (0xff000000)); g.fillEllipse (x - 1.9f, y - 1.9f, 3.8f, 3.8f);
    g.setColour (Colour (0xffff2a2a)); g.fillEllipse (x - 1.1f, y - 1.1f, 2.2f, 2.2f);
    if (hitMark > 0)
    {
        const float d = 6.f + (1.f - hitMark) * 5.f;
        Path p;
        for (int sx : { -1, 1 }) for (int sy : { -1, 1 })
        { p.startNewSubPath (x + sx * d, y + sy * d); p.lineTo (x + sx * d / 2, y + sy * d / 2); }
        g.setColour (A (hitCol, hitMark)); g.strokePath (p, juce::PathStrokeType (2.f));
    }
}

void Dot50Editor::drawHUD (Graphics& g, float dt)
{
    const Colour white (0xffffffff);
    const Colour none (0x00000000);
    // titre
    panel (g, 14, 12, 148, 60, ORG, PDARK, PEDGE);
    txt (g, ".50", 28, 36, white, 24, -1, 4);
    txt (g, "BUYORBEALOSER", 29, 53, ORG, 7, -1, 1.5f);
    {
        const bool hov = inRect (mouse, THB);
        g.setColour (hov ? rgba (24, 224, 208, .38f) : rgba (24, 224, 208, .14f)); g.fillRect (THB);
        g.setColour (hov ? TCOL : rgba (24, 224, 208, .55f)); g.drawRect (THB.reduced (.5f), 1.f);
        txt (g, "TRYHARD", THB.getCentreX(), THB.getCentreY() + .5f, hov ? white : Colour (0xffbff5ef), 6.5f, 0, .5f);
    }

    // rack d'effets : nom + potard
    for (int i = 0; i < 8; ++i)
    {
        auto& t = targets[(size_t) i];
        const float cw = 86, x = 166.f + (float) i * 90.f, y = 12, h = 60; const bool on = t.on;
        panel (g, x, y, cw, h, on ? white : FXCOL, on ? rgba (88, 102, 120, .86f) : PDARK, on ? rgba (255, 255, 255, .85f) : PEDGE);
        const Colour kc = on ? white : FXCOL;
        g.setColour (kc); g.fillRect (x + 9, y + 10, 8.f, 8.f);
        if (on) { g.setColour (rgba (255, 255, 255, .35f)); g.fillRect (x + 6, y + 7, 14.f, 14.f); g.setColour (white); g.fillRect (x + 9, y + 10, 8.f, 8.f); }
        txt (g, t.name, x + 22, y + 14, on ? white : Colour (0xffe9eef1), 8.5f, -1, .5f);
        g.setColour (Colour (0x1fffffff)); g.fillRect (x + 8, y + 27, cw - 16, 1.f);
        const bool act = dragKnob == i;
        drawKnob (g, x + 22, y + 44, 10, getKnob (i), kc, act);
        txt (g, kKnobs[i].label, x + 38, y + 38, Colour (0x99dcdcd7), 5.5f, -1, .5f);
        txt (g, knobText (i), x + 38, y + 50, act ? white : kc, 7.5f, -1, .5f);
    }
    // reset
    panel (g, W - 74, 12, 60, 60, none, PDARK, PEDGE);
    txt (g, "RESET", W - 44, 42, ORG, 8.5f, 0, .5f);

    // kill-feed
    for (int i = (int) feed.size() - 1; i >= 0; --i) { feed[(size_t) i].life += dt; if (feed[(size_t) i].life > 5.f) feed.erase (feed.begin() + i); }
    for (int i = 0; i < (int) feed.size(); ++i)
    {
        auto& e = feed[(size_t) i];
        const float a = e.life > 4.4f ? juce::jmax (0.f, (5.f - e.life) / .6f) : 1.f, y = 82.f + (float) i * 26.f, h = 22.f;
        g.saveState(); g.setOpacity (a);
        const float tsz = 8.5f * 1.2f;
        const float w1 = textWidth ("BUYORBEALOSER", tsz, 1.f), w2 = textWidth (e.n, tsz, 1.f), w3 = textWidth (e.on ? "ON" : "OFF", tsz, 1.f);
        const float w = w1 + w2 + w3 + 66.f, x = W - 14.f - w;
        g.setColour (rgba (34, 40, 50, .72f)); g.fillRect (x, y, w, h);
        g.setColour (e.on ? rgba (255, 255, 255, .9f) : rgba (255, 255, 255, .3f)); g.drawRect (x + .8f, y + .8f, w - 1.6f, h - 1.6f, 1.6f);
        txt (g, "BUYORBEALOSER", x + 9, y + h / 2 - .5f, ORG, 8.5f, -1, 1);
        const float ix = x + 9 + w1 + 16;
        g.setColour (white); g.drawEllipse (ix - 5.5f, y + h / 2 - 5.5f, 11, 11, 1.6f);
        g.drawLine (ix - 8, y + h / 2, ix + 8, y + h / 2, 1.6f); g.drawLine (ix, y + h / 2 - 8, ix, y + h / 2 + 8, 1.6f);
        txt (g, e.n, ix + 16, y + h / 2 - .5f, FXCOL, 8.5f, -1, 1);
        txt (g, e.on ? "ON" : "OFF", x + w - 9, y + h / 2 - .5f, e.on ? white : Colour (0xffaab6c1), 8.5f, 1, 1);
        g.restoreState();
    }

    // bas gauche : FX actifs + MIX global
    const int ac = activeCount();
    panel (g, 14, H - 62, 170, 48, ORG, PDARK, PEDGE);
    txt (g, "FX", 30, H - 52, Colour (0xffc9c9c4), 7.5f, -1, 2);
    txt (g, juce::String (ac).paddedLeft ('0', 2), 30, H - 30, ac ? white : ORG, 26);
    txt (g, "/ 08", 82, H - 27, Colour (0x99dcdcd7), 12);
    txt (g, "MIX", 162, H - 54, Colour (0xffc9c9c4), 6.5f, 0, 1.5f);
    drawKnob (g, 162, H - 35, 11, getKnob (-1), ORG, dragKnob == -1);
    txt (g, knobText (-1), 162, H - 19, dragKnob == -1 ? white : Colour (0xbfdcdcd7), 6.5f, 0, .5f);

    // bas droite : munitions
    panel (g, W - 190, H - 62, 176, 48, ORG, PDARK, PEDGE);
    txt (g, "AMMO", W - 174, H - 52, Colour (0xffc9c9c4), 7.5f, -1, 2);
    const bool low = ammo < 6 && (((int) std::floor (time * 4)) % 2 == 0);
    txt (g, juce::String (ammo).paddedLeft ('0', 2), W - 174, H - 30, low ? Colour (0xffff4a3a) : ORG, 26);
    txt (g, "/ " + juce::String (MAG), W - 122, H - 27, Colour (0x99dcdcd7), 12);
    {
        const bool dim = reloading || ammo == MAG, pulse = ammo < 6 && ! reloading && (((int) std::floor (time * 4)) % 2 == 0);
        const Colour pc = pulse ? Colour (0xffff4a3a) : ORG;
        g.saveState(); g.setOpacity (dim ? .4f : 1.f);
        panel (g, RLD.getX(), RLD.getY(), RLD.getWidth(), RLD.getHeight(), pc, rgba (34, 40, 50, .62f), PEDGE);
        txt (g, "R", RLD.getX() + 13, RLD.getCentreY() - .5f, pc, 10, 0);
        txt (g, "RELOAD", RLD.getX() + 24, RLD.getCentreY() - .5f, Colour (0xffd9d9d3), 7, -1, 1);
        g.restoreState();
    }
    if (reloading && hasMouse) txt (g, "RELOADING", mouse.x, mouse.y + 36, ORG, 9, 0, 2);

    // bouton SFX
    {
        const bool sfxOn = proc.apvts.getRawParameterValue ("sfx_on")->load() > .5f;
        g.setColour (sfxOn ? rgba (212, 235, 245, .22f) : Colour (0x12ffffff)); g.fillRect (SFXBTN);
        g.setColour (sfxOn ? ORG : Colour (0x47ffffff)); g.drawRect (SFXBTN.reduced (.5f), 1.f);
        txt (g, sfxOn ? "SFX ON" : "SFX OFF", SFXBTN.getCentreX(), SFXBTN.getCentreY(), sfxOn ? white : Colour (0xffe9e9e4), 8, 0, 1);
    }

    // panneau du bas : vumètre de sortie + volume des SFX
    panel (g, 196, H - 62, 494, 48, ORG, PDARK, PEDGE);
    txt (g, "OUTPUT", 214, H - 50, Colour (0xffc9c9c4), 7.5f, -1, 2);
    {
        const int n = 34;
        for (int k = 0; k < n; ++k)
        {
            const bool on = outLevelSmooth * (float) n > (float) k;
            g.setColour (on ? (k > n * .88 ? Colour (0xffff4a3a) : (k > n * .72 ? ORG : white)) : Colour (0x1affffff));
            g.fillRect (214.f + (float) k * 11.f, H - 36.f, 8.f, 12.f);
        }
    }
    txt (g, "SFX VOL", 650, H - 54, Colour (0xffc9c9c4), 6.5f, 0, 1.5f);
    drawKnob (g, 650, H - 35, 11, getKnob (-2), ORG, dragKnob == -2);
    txt (g, knobText (-2), 650, H - 19, dragKnob == -2 ? white : Colour (0xbfdcdcd7), 6.5f, 0, .5f);

    // chrono challenge
    if (cs == CState::Run) cT = (juce::Time::getMillisecondCounterHiRes() - cT0) / 1000.0;
    {
        const float x = 14, y = 80, w = 200, h = 66;
        const Colour col = cs == CState::Run ? ORG : white;
        panel (g, x, y, w, h, col, rgba (34, 40, 50, .82f), cs == CState::Done ? rgba (255, 255, 255, .8f) : (cs == CState::Run ? rgba (212, 235, 245, .6f) : rgba (255, 255, 255, .28f)));
        txt (g, "CHALLENGE", x + 14, y + 11, ORG, 7.5f, -1, 2);
        txt (g, cBest > 0 ? "BEST " + fmtT (cBest) : juce::String ("NO BEST YET"), x + w - 10, y + 11, Colour (0xffd9d9d3), 7, 1, 1);
        txt (g, fmtT (cT), x + w / 2 + 2, y + 34, col, 27, 0, 1);
        txt (g, cs == CState::Ready ? juce::String ("SHOOT A TARGET TO START")
              : (cs == CState::Run ? juce::String (8 - ac) + " LEFT" : juce::String ("DONE   /   PRESS C TO RETRY")),
             x + w / 2 + 2, y + h - 8, Colour (0xffe4e4de), 6.5f, 0, 1);
    }
    if (cs == CState::Done && time - cDoneT < 3.5)
    {
        const float el = (float) (time - cDoneT), a = juce::jmin (1.f, (3.5f - el) * 1.5f), k = juce::jmin (1.f, el * 6.f);
        g.saveState(); g.setOpacity (a);
        txt (g, "ALL FX ACTIVE", W / 2, H * .5f - 70.f - (1.f - k) * 10.f, white, 22.f * (.8f + .2f * k), 0, 3);
        txt (g, fmtT (cT), W / 2, H * .5f - 36.f, white, 26, 0, 2);
        if (cNewBest) txt (g, "NEW BEST", W / 2, H * .5f - 2.f, ORG, 12, 0, 3);
        g.restoreState();
    }

    // indice
    if (hintT < 8.f)
    {
        g.saveState(); g.setOpacity (juce::jmin (1.f, 8.f - hintT) * .9f);
        txt (g, "SHOOT A TARGET TO TOGGLE ITS EFFECT   /   DRAG A KNOB TO ADJUST   /   R = RELOAD   C = RESET", W / 2, H - 72, Colour (0xfff2f7fa), 8.5f, 0, 1);
        g.restoreState();
    }
}

void Dot50Editor::thDrawTargets (Graphics& g)
{
    for (auto& t : th.list)
    {
        const float k = juce::jmin (1.f, (float) (time - t.born) / .18f), sc = juce::jmax (.001f, easeOutBack (k));
        g.setColour (rgba (32, 40, 52, .18f));
        const float rw = TR * .9f * sc, rh = TR * .22f * sc;
        g.fillEllipse (t.x - rw, t.y + TR * 1.45f - rh, rw * 2, rh * 2);
        g.setColour (TCOL); g.fillEllipse (t.x - TR * sc, t.y - TR * sc, TR * sc * 2, TR * sc * 2);
        g.setColour (TRIM); g.drawEllipse (t.x - TR * sc, t.y - TR * sc, TR * sc * 2, TR * sc * 2, 2.f);
    }
}

void Dot50Editor::thHUD (Graphics& g, float)
{
    const Colour white (0xffffffff), none (0x00000000);
    const double el = th.state == 2 ? time - th.t0 : (th.state == 3 ? (double) th.dur : 0.0);
    const float left = juce::jmax (0.f, th.dur - (float) el);
    // titre
    panel (g, 14, 12, 148, 60, TCOL, PDARK, PEDGE);
    txt (g, ".50", 28, 36, white, 24, -1, 4); txt (g, "TRYHARD", 29, 57, TCOL, 8, -1, 2.5f);
    // chrono
    {
        const float x = W / 2 - 100, w = 200;
        panel (g, x, 12, w, 60, TCOL, rgba (34, 40, 50, .78f), PEDGE);
        txt (g, th.state == 1 ? "GET READY" : (th.state == 3 ? "TIME UP" : "TIME LEFT"), x + w / 2, 26, Colour (0xffd9e4ea), 7.5f, 0, 2);
        txt (g, juce::String (left, 2).paddedLeft ('0', 5), x + w / 2, 48, left < 5.f && th.state == 2 ? Colour (0xffff7a6a) : white, 26, 0, 1);
        g.setColour (rgba (255, 255, 255, .15f)); g.fillRect (x + 14, 62.f, w - 28, 3.f);
        g.setColour (TCOL); g.fillRect (x + 14, 62.f, (w - 28) * (left / th.dur), 3.f);
    }
    // quitter
    {
        const bool hov = inRect (mouse, THX);
        panel (g, THX.getX(), THX.getY(), THX.getWidth(), THX.getHeight(), none, hov ? rgba (255, 90, 80, .35f) : PDARK, PEDGE);
        txt (g, "EXIT   ESC", THX.getCentreX(), THX.getCentreY(), Colour (0xfff2f7fa), 8.5f, 0, 1.5f);
    }
    // stats en direct
    if (th.state != 3)
    {
        const float acc = th.shots ? (float) th.hits / (float) th.shots * 100.f : 0.f, kps = el > .3 ? (float) th.hits / (float) el : 0.f;
        panel (g, 14, 80, 200, 98, TCOL, rgba (34, 40, 50, .72f), PEDGE);
        const juce::String lab[3] = { "KILLS", "ACCURACY", "KILLS / SEC" };
        const juce::String val[3] = { juce::String (th.hits), juce::String (acc, 1) + "%", juce::String (kps, 2) };
        for (int i = 0; i < 3; ++i)
        {
            txt (g, lab[i], 30, 98.f + (float) i * 28.f, Colour (0xffc9d6dd), 7.5f, -1, 1.5f);
            txt (g, val[i], 200, 98.f + (float) i * 28.f, white, 17, 1, 0);
        }
    }
    // compte à rebours / GO
    if (th.state == 1)
    {
        const float c = 3.f - (float) (time - th.tc), n = std::ceil (c), f = c - std::floor (c);
        g.saveState(); g.setOpacity (juce::jlimit (0.f, 1.f, .35f + .65f * f));
        txt (g, juce::String ((int) n), W / 2, H * .42f, white, 64.f * (.85f + .3f * f), 0, 0);
        g.restoreState();
        txt (g, "SHOOT THE TURQUOISE TARGETS  -  30 SECONDS", W / 2, H * .42f + 58, Colour (0xfff2f7fa), 10, 0, 2);
    }
    if (th.state == 2 && el < .7)
    {
        g.saveState(); g.setOpacity (juce::jlimit (0.f, 1.f, 1.f - (float) el / .7f));
        txt (g, "GO", W / 2, H * .42f, TCOL, 64, 0, 4);
        g.restoreState();
    }
    // résultats
    if (th.state == 3 && th.hasRes)
    {
        g.setColour (rgba (30, 36, 46, .55f)); g.fillRect (0.f, 0.f, W, H);
        panel (g, RP.getX(), RP.getY(), RP.getWidth(), RP.getHeight(), TCOL, rgba (40, 48, 60, .94f), rgba (255, 255, 255, .35f));
        txt (g, "RESULTS", RP.getCentreX(), RP.getY() + 26, white, 16, 0, 5);
        if (th.newBest) txt (g, "NEW BEST", RP.getRight() - 30, RP.getY() + 26, TCOL, 9, 1, 2);
        const juce::String tl[6] = { "ACCURACY", "KILLS / SEC", "TIME TO KILL", "TARGETS", "TOTAL KILLS", "BEST KILLS" };
        const juce::String tv[6] = { juce::String (th.rAcc, 1) + "%", juce::String (th.rKps, 2), juce::String (juce::roundToInt (th.rTtk)) + " ms",
                                     juce::String (th.rTargets), juce::String (th.rKills), juce::String (th.bestKills >= 0 ? th.bestKills : th.rKills) };
        for (int i = 0; i < 6; ++i)
        {
            const float tx = RP.getX() + 30.f + (float) (i % 3) * 166.f, ty = RP.getY() + 52.f + (float) (i / 3) * 88.f;
            g.setColour (rgba (255, 255, 255, .08f)); g.fillRect (tx, ty, 152.f, 76.f);
            g.setColour (i == 4 ? TCOL : rgba (255, 255, 255, .25f)); g.fillRect (tx, ty, 152.f, 2.f);
            txt (g, tl[i], tx + 76, ty + 17, Colour (0xffc9d6dd), 7.5f, 0, 1.5f);
            txt (g, tv[i], tx + 76, ty + 48, i == 4 ? TCOL : white, 22, 0, 0);
        }
        for (int i = 0; i < 2; ++i)
        {
            const auto& b = i == 0 ? THRETRY : THEXIT; const bool hov = inRect (mouse, b);
            g.setColour (i == 0 ? (hov ? rgba (24, 224, 208, .45f) : rgba (24, 224, 208, .25f)) : (hov ? rgba (255, 255, 255, .22f) : rgba (255, 255, 255, .1f)));
            g.fillRect (b);
            g.setColour (i == 0 ? TCOL : rgba (255, 255, 255, .4f)); g.drawRect (b.reduced (.5f), 1.f);
            txt (g, i == 0 ? "RETRY   R" : "EXIT   ESC", b.getCentreX(), b.getCentreY(), white, 9.5f, 0, 2);
        }
    }
}
