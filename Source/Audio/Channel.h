#pragma once

#include "Audio/EffectChain.h"
#include "Audio/SampleData.h"

/** One sound source of the project: either an imported audio sample (played by a small
    built-in sampler) or a VST3/AU instrument plug-in. Each channel has its own insert
    effect chain, volume and pan.

    Thread rules: the public "settings" atomics can be changed from anywhere. Everything
    marked [audio] must be called with the audio engine's lock held. */
class Channel
{
public:
    enum class Type { sample, plugin };

    Channel (int id, Type type);
    ~Channel();

    const int id;
    const Type type;
    String name;
    Colour colour;

    // --- sample channels
    File sampleFile;                          // remembered even if loading failed
    File originalSampleFile;                  // the file first imported ("Revert to original")
    std::shared_ptr<const SampleData> sample;
    bool sampleModified = false;              // edited in the sample editor, not yet written to disk
    std::vector<std::shared_ptr<const SampleData>> undoHistory, redoHistory; // message thread only

    // --- plug-in channels
    std::unique_ptr<AudioPluginInstance> plugin;
    PluginDescription pluginDescription;      // remembered even if loading failed
    MemoryBlock missingPluginState;           // state kept for plug-ins that could not be loaded

    EffectChain effects;

    std::atomic<float> volume { 0.8f };       // 0..1 (linear)
    std::atomic<float> pan { 0.0f };          // -1..1
    std::atomic<bool> muted { false };
    std::atomic<bool> oneShot { true };       // sample plays to the end regardless of note length
    std::atomic<int> rootNote { 60 };         // pitch at which the sample plays unchanged
    std::atomic<float> activity { 0.0f };     // output peak, read by the UI

    bool isSample() const noexcept  { return type == Type::sample; }
    bool isPlugin() const noexcept  { return type == Type::plugin; }
    bool isLoaded() const noexcept  { return isSample() ? sample != nullptr : plugin != nullptr; }

    // [audio]
    void prepare (double sampleRate, int maxBlockSize);
    void releaseResources();
    void updateBufferSize();                  // call after the plug-in/effects changed
    void noteOn (int sampleOffset, int pitch, float velocity);
    void noteOff (int sampleOffset, int pitch);
    void releaseNotes (bool hardStop);
    void render (AudioBuffer<float>& mix, int numSamples);
    void setPlayHead (AudioPlayHead*);
    void setNonRealtime (bool);
    void resetGainSmoothing();                // jump to the current volume/pan (offline render)

private:
    struct Voice
    {
        bool active = false;
        double position = 0.0;
        double increment = 1.0;
        float gain = 1.0f;
        int pitch = 60;
        bool releasing = false;
        float envelope = 1.0f;
        float envelopeStep = 0.0f;
        uint32 age = 0;
    };

    struct PendingEvent
    {
        int offset;
        int pitch;
        float velocity;
        bool on;
    };

    void startVoice (int pitch, float velocity);
    void stopVoices (int pitch);
    void renderVoices (int start, int end);
    void handleEvent (const PendingEvent&);

    static constexpr int maxVoices = 32;
    std::array<Voice, maxVoices> voices;
    uint32 voiceCounter = 0;

    std::vector<PendingEvent> pending;
    MidiBuffer midi;
    std::array<uint8, 128> heldNotes {};
    AudioBuffer<float> buffer;

    double sampleRate = 44100.0;
    int maxBlockSize = 512;
    bool prepared = false;
    float lastGainL = 0.0f, lastGainR = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Channel)
};
