#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Effects.h"
#include "Sfx.h"

// DOT50 — BUYORBEALOSER
// 8 cibles = 8 effets. Chaîne : CRUSH -> FILTER -> DRIVE -> CHORUS -> PHASER -> FLANGER -> DELAY -> REVERB
// Chaque effet s'active/se coupe par fondu (aucun clic) ; seul DRIVE introduit de la latence (52 échantillons, constante, annoncée à l'hôte).
class Dot50Processor : public juce::AudioProcessor
{
public:
    Dot50Processor();
    ~Dot50Processor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "DOT50"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 14.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // interface -> audio
    void triggerSfx (frange::Sfx::Type t);
    float getOutLevel() const { return outLevel.load (std::memory_order_relaxed); }
    int getBestKills() const;             // meilleur score du mode TRYHARD
    float getBestAcc() const;
    void setBestTry (int kills, float acc);
    double getBestTime() const;           // meilleur temps du challenge (secondes, 0 = aucun), sauvegardé dans le projet
    void setBestTime (double seconds);

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    // ordre des cibles = ordre de l'interface (fx_*) ; true = effet actif
    static constexpr const char* kFxIds[8] = { "fx_reverb", "fx_delay", "fx_chorus", "fx_drive",
                                               "fx_crush", "fx_filter", "fx_phaser", "fx_flanger" };

private:
    float get (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    bool on (const char* id) const { return get (id) > 0.5f; }

    frange::Crush crush;
    frange::Filter filter;
    frange::Drive drive;
    frange::Chorus chorus;
    frange::Phaser phaser;
    frange::Flanger flanger;
    frange::Delay delay;
    frange::Reverb reverb;
    frange::Sfx sfx;
    std::atomic<float> outLevel { 0.f };
    double sr = 48000.0, hostBpm = 120.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Dot50Processor)
};
