#include "Audio/EffectChain.h"

void PluginHelpers::configureStereo (AudioPluginInstance& p, bool isInstrument)
{
    auto layout = p.getBusesLayout();

    if (! layout.outputBuses.isEmpty())
        layout.outputBuses.getReference (0) = AudioChannelSet::stereo();

    if (! isInstrument && ! layout.inputBuses.isEmpty())
        layout.inputBuses.getReference (0) = AudioChannelSet::stereo();

    if (p.checkBusesLayoutSupported (layout))
        p.setBusesLayout (layout);
}

int PluginHelpers::getRequiredChannels (const AudioPluginInstance& p)
{
    return jmax (2, p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
}

bool PluginHelpers::process (AudioPluginInstance& p, AudioBuffer<float>& buffer, int numSamples, MidiBuffer& midi)
{
    const ScopedLock sl (p.getCallbackLock());

    if (p.isSuspended())
        return false;

    const auto numChannels = jmin (buffer.getNumChannels(), getRequiredChannels (p));
    AudioBuffer<float> view (buffer.getArrayOfWritePointers(), numChannels, numSamples);
    p.processBlock (view, midi);

    // mono output -> duplicate to the right channel
    if (p.getMainBusNumOutputChannels() == 1 && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    return true;
}

//==============================================================================
void EffectChain::prepare (double sampleRate, int maxBlockSize)
{
    midi.ensureSize (2048);

    for (auto& s : slots)
    {
        s->plugin->releaseResources();
        s->plugin->prepareToPlay (sampleRate, maxBlockSize);
    }
}

void EffectChain::releaseResources()
{
    for (auto& s : slots)
        s->plugin->releaseResources();
}

void EffectChain::process (AudioBuffer<float>& buffer, int numSamples)
{
    for (auto& s : slots)
    {
        if (s->bypassed.load())
            continue;

        // keep extra channels (e.g. side-chain inputs) silent
        for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, 0, numSamples);

        midi.clear();
        PluginHelpers::process (*s->plugin, buffer, numSamples, midi);
    }
}

int EffectChain::getRequiredChannels() const
{
    int n = 2;

    for (auto& s : slots)
        n = jmax (n, PluginHelpers::getRequiredChannels (*s->plugin));

    return n;
}

void EffectChain::setNonRealtime (bool nonRealtime) noexcept
{
    for (auto& s : slots)
        s->plugin->setNonRealtime (nonRealtime);
}

void EffectChain::setPlayHead (AudioPlayHead* playHead)
{
    for (auto& s : slots)
        s->plugin->setPlayHead (playHead);
}
