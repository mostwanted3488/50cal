// Outil de contrôle (CI) : instancie le plugin sans interface graphique, rend l'éditeur dans des PNG et fait un test audio.
#include "PluginProcessor.h"
#include <cstdio>

static void savePng (const juce::Image& img, const char* name)
{
    juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile (name);
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    Dot50Processor proc;
    proc.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (960, 540);
    auto run = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };
    auto snap = [&] (const char* name) { savePng (ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f), name); std::printf ("saved %s\n", name); };

    run (2500);                         // animation de sortie de l'arme -> repos
    snap ("snap_1_idle.png");

    auto setP = [&] (const char* id, float v) { proc.apvts.getParameter (id)->setValueNotifyingHost (v); };
    for (auto id : { "fx_reverb", "fx_filter", "fx_phaser", "fx_crush" }) setP (id, 1.f);   // cibles actives (l'hôte pilote l'interface)
    setP ("rev_decay", 0.8f); setP ("mix_global", 0.64f); setP ("cru_bits", 0.25f);
    run (900);
    snap ("snap_2_active.png");

    // test audio : sinus + SFX, sorties finies et non nulles
    juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
    bool finite = true; float pk = 0.f;
    proc.triggerSfx (frange::Sfx::Shot);
    for (int b = 0; b < 200; ++b)
    {
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) buf.setSample (c, i, 0.3f * std::sin (0.05f * (float) (b * 512 + i)));
        proc.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) { const float v = buf.getSample (c, i); if (! std::isfinite (v)) finite = false; pk = std::max (pk, std::abs (v)); }
    }
    std::printf ("audio finite=%d peak=%.3f latency=%d\n", (int) finite, pk, proc.getLatencySamples());
    ed.reset();
    return finite ? 0 : 1;
}
