#pragma once

#include "Audio/Channel.h"

/** A compiled, immutable list of note events, built on the message thread from the
    patterns/arrangement and handed to the audio thread. */
struct Sequence
{
    struct Event
    {
        double beat;       // position in quarter notes
        Channel* channel;
        int pitch;
        float velocity;
        bool on;
    };

    std::vector<Event> events;   // sorted by beat
    double length = 0.0;         // loop / end point in quarter notes
    bool loop = true;
    bool isSong = false;
    int beatsPerBar = 4;
    int beatUnit = 4;

    void sort();
};

/** Real-time audio engine: transport, sequencing, mixing and plug-in hosting. */
class AudioEngine : public AudioIODeviceCallback,
                    public AudioPlayHead
{
public:
    AudioEngine();
    ~AudioEngine() override;

    /** Held by the audio thread while rendering. Take it on the message thread before
        making structural changes to channels or effect chains. */
    CriticalSection& getLock() noexcept { return lock; }

    double getSampleRate() const noexcept { return sampleRate; }
    int getBlockSize() const noexcept     { return blockSize; }

    // ---- message thread
    void prepare (double sampleRate, int blockSize);
    void setChannels (std::vector<std::shared_ptr<Channel>>);
    void setSequence (std::shared_ptr<const Sequence>, bool resetPosition);
    void prepareNewChannel (Channel&);           // call before adding to the project
    void preparePlugin (AudioPluginInstance&);   // call before inserting an effect

    void play();
    void stop();
    void seek (double beat);
    bool isPlaying() const noexcept              { return uiPlaying.load(); }
    double getPositionInBeats() const noexcept   { return uiPosition.load(); }
    bool isSongSequence() const noexcept         { return uiSongMode.load(); }

    void setBpm (double);
    double getBpm() const noexcept               { return bpm.load(); }
    void setMasterVolume (float v) noexcept      { masterVolume.store (v); }
    float getMasterVolume() const noexcept       { return masterVolume.load(); }
    float takeMasterPeak (int channel) noexcept  { return masterPeak[(size_t) channel].exchange (0.0f); }

    EffectChain& getMasterEffects() noexcept     { return masterEffects; }
    void masterEffectsChanged();                 // call with the lock held

    /** Plays a note on a channel immediately (used for auditioning). */
    void previewNote (int channelId, int pitch, float velocity, bool on);

    // ---- sample editor support (message thread)
    /** Registers an effect chain that is not part of the project (the sample editor's rack),
        so it stays prepared when the audio device changes. */
    void addAuxiliaryChain (EffectChain*);
    void removeAuxiliaryChain (EffectChain*);      // also stops a preview that uses it
    void auxiliaryChainChanged();                  // call with the lock held after adding/removing slots

    /** Plays (part of) a sample through an optional effect chain, independent of the transport. */
    void startSamplePreview (std::shared_ptr<const SampleData>, Range<int> range, EffectChain*, bool loop);
    void stopSamplePreview();
    bool isSamplePreviewPlaying() const noexcept   { return uiPreviewActive.load(); }
    double getSamplePreviewPosition() const noexcept { return uiPreviewPosition.load(); } // source frames

    /** Runs audio through an effect chain offline at the given sample rate, appending
        `tailSamples` of silence so reverbs/delays can ring out. Plug-in latency is compensated.
        The output has 2 channels and input length + tailSamples frames. */
    Result renderThroughChain (EffectChain&, const AudioBuffer<float>& input, double sampleRate,
                               int tailSamples, AudioBuffer<float>& output);

    /** Renders a sequence offline into a WAV file. Blocks the audio thread meanwhile. */
    Result renderToFile (const File&, std::shared_ptr<const Sequence>, double tailSeconds,
                         std::function<void (double)> progress = {});

    // ---- AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (AudioIODevice*) override;
    void audioDeviceStopped() override;

    // ---- AudioPlayHead (called by plug-ins on the audio thread)
    Optional<PositionInfo> getPosition() const override;

private:
    void prepareLocked (double sampleRate, int blockSize);
    void renderChunk (int numSamples);
    void scheduleEvents (int numSamples);
    void dispatch (double fromBeat, double toBeat, double sampleOffset, int numSamples);
    void handlePreviewEvents();
    void releaseAllNotes (bool hardStop);
    void renderSamplePreview (int numSamples);

    CriticalSection lock;
    std::vector<std::shared_ptr<Channel>> channels;
    std::shared_ptr<const Sequence> sequence;
    EffectChain masterEffects;
    AudioBuffer<float> masterBuffer;

    double sampleRate = 44100.0;
    int blockSize = 512;
    bool prepared = false;

    bool playing = false;
    double position = 0.0;        // quarter notes
    double blockStartBeat = 0.0;
    int64 samplesPlayed = 0;
    bool pendingHardRelease = false, pendingSoftRelease = false;

    std::atomic<double> bpm { 120.0 };
    std::atomic<float> masterVolume { 0.8f };
    std::array<std::atomic<float>, 2> masterPeak {};
    std::atomic<double> uiPosition { 0.0 };
    std::atomic<bool> uiPlaying { false }, uiSongMode { false };

    struct SamplePreview
    {
        std::shared_ptr<const SampleData> sample;
        EffectChain* chain = nullptr;
        int start = 0, end = 0;
        double position = 0.0;
        bool loop = false, active = false, sourceFinished = false;
        int tailRemaining = 0;
        AudioBuffer<float> buffer;
    };

    SamplePreview samplePreview;
    std::vector<EffectChain*> auxiliaryChains;
    std::atomic<bool> uiPreviewActive { false };
    std::atomic<double> uiPreviewPosition { 0.0 };

    struct PreviewEvent { int channelId, pitch; float velocity; bool on; };
    AbstractFifo previewFifo { 256 };
    std::array<PreviewEvent, 256> previewEvents {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
