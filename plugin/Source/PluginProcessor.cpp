#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;
using NR  = juce::NormalisableRange<float>;

juce::AudioProcessorValueTreeState::ParameterLayout Dot50Processor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    const char* names[8] = { "Reverb", "Delay", "Chorus", "Drive", "Crush", "Filter", "Phaser", "Flanger" };
    for (int i = 0; i < 8; ++i)
        l.add (std::make_unique<APB> (juce::ParameterID { kFxIds[i], 1 }, names[i], false));

    auto pf = [&] (const char* id, const char* name, NR r, float def, const char* unit = "")
    {
        l.add (std::make_unique<APF> (juce::ParameterID { id, 1 }, name, r, def, juce::AudioParameterFloatAttributes().withLabel (unit)));
    };
    auto pc = [&] (const char* id, const char* name, juce::StringArray items, int def)
    {
        l.add (std::make_unique<APC> (juce::ParameterID { id, 1 }, name, items, def));
    };
    const NR unit01 { 0.f, 1.f, 0.001f };

    // CRUSH
    pf ("cru_bits",   "Crush Bits",    { 4.f, 12.f, 0.01f }, 8.f);
    pf ("cru_rate",   "Crush Rate Reduction", NR (1.f, 48.f, 0.01f, 0.5f), 6.f, "x");
    pf ("cru_smooth", "Crush Smooth",  NR (1500.f, 20000.f, 1.f, 0.5f), 12000.f, " Hz");
    pf ("cru_mix",    "Crush Mix",     unit01, 1.f);
    // FILTER
    { // potard CUTOFF : course logarithmique 120 Hz -> 12 kHz (position 0..1 identique à l\'interface)
        NR r (120.f, 12000.f,
              [] (float lo, float hi, float v) { return lo * std::pow (hi / lo, v); },
              [] (float lo, float hi, float v) { return std::log (v / lo) / std::log (hi / lo); },
              [] (float lo, float hi, float v) { return juce::jlimit (lo, hi, v); });
        pf ("flt_cutoff", "Filter Cutoff", r, 2400.f, " Hz"); }
    pf ("flt_res",    "Filter Resonance", unit01, 0.35f);
    pf ("flt_drive",  "Filter Drive",  unit01, 0.2f);
    pc ("flt_mode",   "Filter Mode",   { "LP 24", "LP 12", "BP", "HP 24" }, 0);
    pf ("flt_env",    "Filter Env Amount", { -1.f, 1.f, 0.001f }, 0.f);
    pf ("flt_mix",    "Filter Mix",    unit01, 1.f);
    // DRIVE
    pf ("drive_amount", "Drive Amount", unit01, 0.45f);
    pf ("drive_tone",   "Drive Tone",   { -1.f, 1.f, 0.001f }, 0.f);
    pf ("drive_bias",   "Drive Warmth", unit01, 0.35f);
    pf ("drive_out",    "Drive Output", { -18.f, 12.f, 0.1f }, 0.f, " dB");
    pf ("drive_mix",    "Drive Mix",    unit01, 1.f);
    // CHORUS
    pf ("cho_rate",  "Chorus Rate",  NR (0.05f, 5.f, 0.01f, 0.5f), 0.6f, " Hz");
    pf ("cho_depth", "Chorus Depth", { 0.1f, 1.f, 0.001f }, 0.5f);
    pf ("cho_width", "Chorus Width", unit01, 0.8f);
    pf ("cho_tone",  "Chorus Tone",  NR (3000.f, 18000.f, 1.f, 0.5f), 11000.f, " Hz");
    pf ("cho_mix",   "Chorus Mix",   unit01, 0.4f);
    // PHASER
    pf ("pha_rate",   "Phaser Rate",   NR (0.05f, 6.f, 0.01f, 0.5f), 0.35f, " Hz");
    pf ("pha_depth",  "Phaser Depth",  { 0.2f, 1.f, 0.001f }, 0.7f);
    pf ("pha_center", "Phaser Center", NR (150.f, 3000.f, 1.f, 0.5f), 700.f, " Hz");
    pf ("pha_fb",     "Phaser Feedback", { -0.9f, 0.9f, 0.001f }, 0.45f);
    pc ("pha_stages", "Phaser Stages", { "4", "6", "8" }, 1);
    pf ("pha_width",  "Phaser Width",  unit01, 0.6f);
    pf ("pha_mix",    "Phaser Mix",    unit01, 0.5f);
    // FLANGER
    pf ("fla_rate",   "Flanger Rate",   NR (0.03f, 5.f, 0.01f, 0.5f), 0.25f, " Hz");
    pf ("fla_depth",  "Flanger Depth",  unit01, 0.6f);
    pf ("fla_manual", "Flanger Manual", NR (0.3f, 6.f, 0.01f, 0.6f), 1.8f, " ms");
    pf ("fla_fb",     "Flanger Feedback", { 0.1f, 0.8f, 0.001f }, 0.5f);
    pf ("fla_width",  "Flanger Width",  unit01, 0.5f);
    pf ("fla_mix",    "Flanger Mix",    unit01, 0.5f);
    // DELAY
    pf ("dly_time",     "Delay Time",     NR (20.f, 1500.f, 0.1f, 0.5f), 350.f, " ms");
    pc ("dly_sync",     "Delay Sync",     { "Free", "1/4", "1/8", "1/8 dotted", "1/16", "1/8 triplet" }, 0);
    pf ("dly_feedback", "Delay Feedback", { 0.1f, 0.8f, 0.001f }, 0.38f);
    pf ("dly_tone",     "Delay Tone",     NR (800.f, 14000.f, 1.f, 0.5f), 4500.f, " Hz");
    pf ("dly_lowcut",   "Delay Low cut",  NR (20.f, 800.f, 1.f, 0.5f), 90.f, " Hz");
    pf ("dly_pingpong", "Delay Ping-pong", unit01, 0.6f);
    pf ("dly_warmth",   "Delay Warmth",   unit01, 0.25f);
    pf ("dly_duck",     "Delay Ducking",  unit01, 0.35f);
    pf ("dly_mod",      "Delay Wow",      unit01, 0.2f);
    pf ("dly_mix",      "Delay Mix",      unit01, 0.25f);
    // REVERB
    pf ("rev_decay",    "Reverb Decay",     { 0.8f, 7.f, 0.01f }, 2.8f, " s");
    pf ("rev_damp",     "Reverb Damping",   unit01, 0.55f);
    pf ("rev_predelay", "Reverb Pre-delay", { 0.f, 200.f, 0.1f }, 12.f, " ms");
    pf ("rev_mod",      "Reverb Motion",    unit01, 0.35f);
    pf ("rev_width",    "Reverb Width",     { 0.f, 1.5f, 0.001f }, 1.f);
    pf ("rev_mix",      "Reverb Mix",       unit01, 0.30f);
    pf ("rev_lowcut",   "Reverb Low cut",   NR (20.f, 600.f, 1.f, 0.5f), 120.f, " Hz");
    pf ("rev_highcut",  "Reverb High cut",  NR (1000.f, 20000.f, 1.f, 0.5f), 9500.f, " Hz");
    pf ("rev_warmth",   "Reverb Warmth",    unit01, 0.f);
    // MIX global (dry/wet de toute la chaîne) + SFX de l'interface
    pf ("mix_global", "Mix", unit01, 1.f);
    // SFX de l'interface (utilisés par l'éditeur)
    l.add (std::make_unique<APB> (juce::ParameterID { "sfx_on", 1 }, "Interface SFX", true));
    pf ("sfx_volume", "SFX Volume", unit01, 0.5f);
    return l;
}

Dot50Processor::Dot50Processor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "FRANGE", createLayout())
{
    // sons échantillonnés (tir, rechargement) intégrés au plugin
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    auto load = [&] (frange::Sfx::Type t, const void* d, size_t sz, float gain)
    {
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (std::make_unique<juce::MemoryInputStream> (d, sz, false)));
        if (rd == nullptr || rd->lengthInSamples <= 0) return;
        const int n = (int) rd->lengthInSamples;
        juce::AudioBuffer<float> b ((int) juce::jmax (2u, rd->numChannels), n);
        rd->read (&b, 0, n, 0, true, true);
        if (rd->numChannels == 1) b.copyFrom (1, 0, b, 0, 0, n);
        std::vector<float> l (b.getReadPointer (0), b.getReadPointer (0) + n), r (b.getReadPointer (1), b.getReadPointer (1) + n);
        sfx.setSample (t, std::move (l), std::move (r), rd->sampleRate, gain);
    };
    load (frange::Sfx::Shot,   BinaryData::shot_wav,   (size_t) BinaryData::shot_wavSize,   0.15f);
    load (frange::Sfx::Reload, BinaryData::reload_wav, (size_t) BinaryData::reload_wavSize, 0.45f);
}

bool Dot50Processor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void Dot50Processor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    crush.prepare (sr); filter.prepare (sr); drive.prepare (sr); chorus.prepare (sr);
    phaser.prepare (sr); flanger.prepare (sr); delay.prepare (sr); reverb.prepare (sr);
    // latence constante (dry toujours aligné) : l'hôte compense, pas de décalage quand on active un effet
    sfx.prepare (sr);
    setLatencySamples (drive.latencySamples());
}

void Dot50Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || n == 0) return;

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm()) hostBpm = *bpm;

    const float gm = get ("mix_global");
    { frange::Crush::Params p; p.bits = get ("cru_bits"); p.rate = get ("cru_rate"); p.smoothHz = get ("cru_smooth");
      p.mix = get ("cru_mix") * gm; p.active = on ("fx_crush"); crush.setParams (p); }
    { frange::Filter::Params p; p.cutoffHz = get ("flt_cutoff"); p.resonance = get ("flt_res"); p.drive = get ("flt_drive");
      p.mode = (int) get ("flt_mode"); p.envAmount = get ("flt_env"); p.mix = get ("flt_mix") * gm; p.active = on ("fx_filter"); filter.setParams (p); }
    { frange::Drive::Params p; p.drive = get ("drive_amount"); p.tone = get ("drive_tone"); p.bias = get ("drive_bias");
      p.outputDb = get ("drive_out"); p.mix = get ("drive_mix") * gm; p.active = on ("fx_drive"); drive.setParams (p); }
    { frange::Chorus::Params p; p.rate = get ("cho_rate"); p.depth = get ("cho_depth"); p.width = get ("cho_width");
      p.toneHz = get ("cho_tone"); p.mix = get ("cho_mix") * gm; p.active = on ("fx_chorus"); chorus.setParams (p); }
    { frange::Phaser::Params p; p.rate = get ("pha_rate"); p.depth = get ("pha_depth"); p.centerHz = get ("pha_center");
      p.feedback = get ("pha_fb"); p.stages = 4 + 2 * (int) get ("pha_stages"); p.width = get ("pha_width"); p.mix = get ("pha_mix") * gm;
      p.active = on ("fx_phaser"); phaser.setParams (p); }
    { frange::Flanger::Params p; p.rate = get ("fla_rate"); p.depth = get ("fla_depth"); p.manualMs = get ("fla_manual");
      p.feedback = get ("fla_fb"); p.width = get ("fla_width"); p.mix = get ("fla_mix") * gm; p.active = on ("fx_flanger"); flanger.setParams (p); }
    { frange::Delay::Params p; p.timeMs = get ("dly_time"); p.syncDiv = (int) get ("dly_sync"); p.bpm = hostBpm;
      p.feedback = get ("dly_feedback"); p.toneHz = get ("dly_tone"); p.lowCutHz = get ("dly_lowcut"); p.pingpong = get ("dly_pingpong");
      p.warmth = get ("dly_warmth"); p.duck = get ("dly_duck"); p.mod = get ("dly_mod"); p.mix = get ("dly_mix") * gm;
      p.active = on ("fx_delay"); delay.setParams (p); }
    { frange::Reverb::Params p; p.decaySec = get ("rev_decay"); p.size = 0.3f + 0.65f * juce::jlimit (0.f, 1.f, (p.decaySec - 0.8f) / 6.2f); p.damp = get ("rev_damp");
      p.predelayMs = get ("rev_predelay"); p.mod = get ("rev_mod"); p.width = get ("rev_width"); p.mix = get ("rev_mix") * gm;
      p.lowCutHz = get ("rev_lowcut"); p.highCutHz = get ("rev_highcut"); p.warmth = get ("rev_warmth");
      p.active = on ("fx_reverb"); reverb.setParams (p); }

    float* l = buffer.getWritePointer (0);
    float* r = buffer.getWritePointer (1);
    crush.process (l, r, n);
    filter.process (l, r, n);
    drive.process (l, r, n);
    chorus.process (l, r, n);
    phaser.process (l, r, n);
    flanger.process (l, r, n);
    delay.process (l, r, n);
    reverb.process (l, r, n);

    // SFX de l'interface (après les effets) + vumètre de sortie
    if (on ("sfx_on")) sfx.process (l, r, n, get ("sfx_volume") * 2.f);
    float pk = 0.f;
    for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::abs (l[i]), std::abs (r[i])));
    const float prev = outLevel.load (std::memory_order_relaxed);
    outLevel.store (std::max (pk, prev * std::pow (0.9f, (float) n / 512.f)), std::memory_order_relaxed);
}

void Dot50Processor::triggerSfx (frange::Sfx::Type t) { if (on ("sfx_on")) sfx.trigger (t); }
int Dot50Processor::getBestKills() const { return (int) apvts.state.getProperty ("tryBestKills", 0); }
float Dot50Processor::getBestAcc() const { return (float) (double) apvts.state.getProperty ("tryBestAcc", 0.0); }
void Dot50Processor::setBestTry (int kills, float acc) { apvts.state.setProperty ("tryBestKills", kills, nullptr); apvts.state.setProperty ("tryBestAcc", (double) acc, nullptr); }
double Dot50Processor::getBestTime() const { return (double) apvts.state.getProperty ("bestTime", 0.0); }
void Dot50Processor::setBestTime (double s) { apvts.state.setProperty ("bestTime", s, nullptr); }

juce::AudioProcessorEditor* Dot50Processor::createEditor() { return new Dot50Editor (*this); }

void Dot50Processor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void Dot50Processor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Dot50Processor(); }
