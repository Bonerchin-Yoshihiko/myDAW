#pragma once

#include "Common.h"

/** A list of insert effect plug-ins. Structural changes (add/remove) must be made while
    holding the audio engine's lock; the bypass flag can be changed at any time. */
class EffectChain
{
public:
    struct Slot
    {
        std::unique_ptr<AudioPluginInstance> plugin;
        PluginDescription description;   // as scanned (points at the .vst3 bundle)
        std::atomic<bool> bypassed { false };
    };

    std::vector<std::unique_ptr<Slot>> slots;

    void prepare (double sampleRate, int maxBlockSize);
    void releaseResources();

    /** Processes `numSamples` of `buffer` in place. The buffer must have at least
        getRequiredChannels() channels; channel 0/1 carry the stereo signal. */
    void process (AudioBuffer<float>& buffer, int numSamples);

    int getRequiredChannels() const;
    int getLatencySamples() const;           // sum of the active plug-ins' latencies
    void reset();                            // clears delay lines / reverb tails
    void setNonRealtime (bool) noexcept;
    void setPlayHead (AudioPlayHead*);
    bool isEmpty() const noexcept { return slots.empty(); }

private:
    MidiBuffer midi;
};

namespace PluginHelpers
{
    /** Tries to give a plug-in a stereo main bus. */
    void configureStereo (AudioPluginInstance&, bool isInstrument);

    int getRequiredChannels (const AudioPluginInstance&);

    /** Runs processBlock with the plug-in's callback lock held. Returns false if the
        plug-in is suspended (the buffer is left untouched in that case). */
    bool process (AudioPluginInstance&, AudioBuffer<float>& buffer, int numSamples, MidiBuffer& midi);
}
