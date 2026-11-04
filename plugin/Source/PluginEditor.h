#pragma once
#include "PluginProcessor.h"
#include <vector>
#include <utility>
#include <memory>
#include <atomic>

// Interface ".50" (Desert Eagle, fond gris + grille, 8 cibles = 8 effets, mode TRYHARD) — port de la preview web.
// Dessinée en 960x540 logiques, mise à l'échelle.
class Dot50Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Dot50Editor (Dot50Processor&);
    ~Dot50Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    // ---------- données ----------
    static constexpr float W = 960.f, H = 540.f;
    struct Row { float y, s; };
    struct Target
    {
        const char* name; const char* paramId;
        int ri; float x, vx, ph; bool alive = true, on = false, von = false;
        double spawnT = 0, respawn = 0, flipT = -9, pendUntil = 0; float emit = 0;
        bool hasHole = false; float hx = 0, hy = 0; double hole0 = -9;
        Row row;
    };
    struct Spark { bool chev; float x, y, vy, life, max, s; };
    struct Particle { float x, y, vx, vy, rot, vr, life, max, size; juce::Colour col; };
    struct Ring { float x, y, r, life, max; juce::Colour col; };
    struct FloatText { float x, y, life, max; juce::String txt; juce::Colour col; };
    struct Tracer { float x1, y1, x2, y2, life, max; };
    struct Decal { float x, y, life; };
    struct Feed { const char* n; bool on; float life; };
    struct Clip
    {
        int n = 0; double fps = 60; juce::Rectangle<float> r;
        std::vector<std::pair<const uint8_t*, int>> frames;
        std::vector<juce::Image> cache;
        juce::Image shown;          // dernière image valide affichée (repli, jamais de trou)
    };
    enum { Idle = 0, Fire, Inspect, Reload, Draw };       // Draw : animation procédurale (sortie de l'arme)

    // TRYHARD
    struct ThTarget { float x, y; double born; };
    struct Tryhard
    {
        bool on = false; int state = 0;       // 0 off, 1 compte à rebours, 2 en cours, 3 résultats
        bool saved[8] = {};
        double tc = 0, t0 = 0, next = 0; float dur = 30.f;
        int shots = 0, hits = 0, spawned = 0; double ttk = 0;
        std::vector<ThTarget> list;
        bool hasRes = false, newBest = false;
        int rKills = 0, rShots = 0, rTargets = 0; float rAcc = 0, rKps = 0, rTtk = 0;
        int bestKills = 0; float bestAcc = 0;
    };

    // ---------- logique ----------
    void timerCallback() override;
    void update (float dt);
    void shoot();
    void startReload();
    void clearAll();
    void setFx (int i, bool on);
    void shatter (const Target&);
    void emitSparks (float dt);
    float tgtY (const Target& t) const;
    float tgtR (const Target& t) const;
    int activeCount() const;
    Target* pick (float x, float y);
    juce::Point<float> muzzle() const;
    juce::Point<float> gunOff() const;
    void setAnim (int a);
    int animFrame() const;

    // TRYHARD
    void thEnter(); void thExit(); void thRetry(); void thReset(); void thSpawn(); void thFinish();
    void thUpdate (float dt); void thShoot(); bool thClick (juce::Point<float> p);
    void thDrawTargets (juce::Graphics&); void thHUD (juce::Graphics&, float dt);

    // ---------- dessin ----------
    void buildBackground();
    void drawTarget (juce::Graphics&, const Target&);
    void drawFx (juce::Graphics&, float dt);
    void drawGun (juce::Graphics&);
    void drawHUD (juce::Graphics&, float dt);
    void drawCrosshair (juce::Graphics&);
    void drawKnob (juce::Graphics&, float cx, float cy, float r, float v, juce::Colour c, bool active);
    void panel (juce::Graphics&, float x, float y, float w, float h, juce::Colour accent, juce::Colour tint, juce::Colour edge);
    void txt (juce::Graphics&, const juce::String&, float x, float y, juce::Colour c, float px, int align = -1, float ls = 0.f, bool boost = true);
    juce::Font fnt (float px, float ls = 0.f) const;
    float textWidth (const juce::String&, float px, float ls) const;
    juce::Image frameImage (Clip&, int idx);
    struct Prefetch;
    std::unique_ptr<Prefetch> pf;
    juce::CriticalSection cacheLock;

    // ---------- potards ----------
    juce::RangedAudioParameter* knobParam (int i) const;       // i = -1 : MIX global, -2 : SFX volume
    float getKnob (int i) const;                                // 0..1 (position visuelle)
    void  setKnob (int i, float v);
    juce::String knobText (int i) const;
    juce::Rectangle<float> knobZone (int i) const;
    int knobAt (juce::Point<float> p) const;

    juce::Point<float> toLogical (const juce::MouseEvent&) const;

    Dot50Processor& proc;
    juce::Typeface::Ptr typeface;
    juce::Image bg;
    Clip clips[4];                       // 0 idle, 1 fire, 2 inspect, 3 reload

    std::vector<Target> targets;
    std::vector<Spark> sparks; std::vector<Particle> particles; std::vector<Ring> rings;
    std::vector<FloatText> texts; std::vector<Tracer> tracers; std::vector<Decal> decals; std::vector<Feed> feed;

    juce::Random rng;
    juce::Point<float> mouse { W * 0.5f, H * 0.42f };
    bool hasMouse = false;
    int ammo = 20; static constexpr int MAG = 20;
    bool reloading = false;
    float flash = 0, spread = 0, shake = 0, hitMark = 0, hintT = 0;
    juce::Colour hitCol { 0xffffffff };
    double time = 0, lastMs = 0, nextInspect = 0, autoReloadAt = -1;
    int anim = Draw; float animT = 0;
    juce::Point<float> sway;

    // fondu court quand on coupe une animation longue
    struct Ghost { juce::Image img; juce::Rectangle<float> r; juce::Point<float> o; float t = 0; bool on = false; } ghost;

    // challenge
    enum class CState { Ready, Run, Done };
    CState cs = CState::Ready; double cT0 = 0, cT = 0, cBest = 0, cDoneT = -99, winAt = -1; bool cNewBest = false;

    Tryhard th;

    // potards : glisser / molette / double-clic
    int dragKnob = -9; float dragY0 = 0, dragX0 = 0, dragV0 = 0;
    double lastTapT = 0; int lastTapKnob = -9;
    float outLevelSmooth = 0;

    juce::Image glowImg;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Dot50Editor)
};
