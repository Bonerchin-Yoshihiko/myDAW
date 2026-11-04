#pragma once

#include "Audio/AudioEngine.h"
#include "Audio/PluginManager.h"
#include "Model/Project.h"

/** The open project plus every operation that changes it. All methods must be called on
    the message thread. UI components register as listeners to hear about changes. */
class Session
{
public:
    enum Change
    {
        channelsChanged       = 1 << 0,
        patternListChanged    = 1 << 1,
        patternContentChanged = 1 << 2,
        arrangementChanged    = 1 << 3,
        transportChanged      = 1 << 4,
        selectionChanged      = 1 << 5,
        projectReplaced       = 1 << 6,
        dirtyChanged          = 1 << 7,
        sampleChanged         = 1 << 8,
        everything            = 0xffff
    };

    enum class PlayMode { pattern, song };

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void sessionChanged (int changes) = 0;
    };

    Session (AudioEngine&, PluginManager&);
    ~Session();

    Project& getProject() noexcept                 { return project; }
    AudioEngine& getEngine() noexcept              { return engine; }
    PluginManager& getPluginManager() noexcept     { return plugins; }
    AudioFormatManager& getFormatManager() noexcept { return formats; }

    /** Called just before a plug-in instance is deleted (nullptr = all of them), so that
        editor windows can be closed first. */
    std::function<void (AudioPluginInstance*)> onPluginWillBeDeleted;

    // ---- project files
    void newProject();
    Result loadProject (const File&, StringArray& warnings);
    Result saveProject (const File&);
    const File& getProjectFile() const noexcept    { return projectFile; }
    bool isDirty() const noexcept                  { return dirty; }
    String getProjectName() const;

    // ---- channels
    std::shared_ptr<Channel> addSampleChannel (const File&, String& error);
    std::shared_ptr<Channel> addPluginChannel (const PluginDescription&, String& error);
    void removeChannel (int channelId);
    void moveChannel (int channelId, int delta);
    bool replaceSample (Channel&, const File&, String& error);
    bool addEffect (Channel* channelOrNullForMaster, const PluginDescription&, String& error);
    void removeEffect (Channel* channelOrNullForMaster, int index);
    void channelSettingsChanged();
    void previewNote (int channelId, int pitch, bool on);

    // ---- sample editing (destructive edits of a sample channel's audio, undoable)
    void commitSampleEdit (Channel&, std::shared_ptr<const SampleData> newData);
    bool undoSampleEdit (Channel&);
    bool redoSampleEdit (Channel&);
    bool revertSampleToOriginal (Channel&, String& error);
    /** Writes edited samples that are not on disk yet into `folder` and points the channels at them. */
    Result writeEditedSamples (const File& folder);
    File getEditedSamplesFolder() const;

    // ---- patterns
    Pattern* getCurrentPattern() const             { return project.findPattern (currentPatternId); }
    int getCurrentPatternId() const noexcept       { return currentPatternId; }
    void selectPattern (int patternId);
    Pattern* createPattern();
    Pattern* duplicatePattern (int patternId);
    void deletePattern (int patternId);
    void patternChanged (int patternId);           // notes or properties edited
    Result exportPattern (int patternId, const File&);
    Result importPattern (const File&, StringArray& warnings);
    String makeUniquePatternName (const String& base) const;

    // ---- arrangement
    int addClip (int patternId, int track, double start);
    void removeClip (int clipId);
    void addTrack();
    void arrangementEdited();

    // ---- transport
    void setPlayMode (PlayMode);
    PlayMode getPlayMode() const noexcept          { return playMode; }
    void play();
    void stop();
    void togglePlay();
    void seek (double beat);
    void setBpm (double);
    void setLoopSong (bool);
    void setMasterVolume (float);

    Result exportWav (const File&, std::function<void (double)> progress = {});

    // ---- notifications
    void addListener (Listener* l)                 { listeners.add (l); }
    void removeListener (Listener* l)              { listeners.remove (l); }
    void notify (int changes);
    void markDirty();

    static constexpr const char* projectExtension = ".mydaw";
    static constexpr const char* patternExtension = ".mdpat";

private:
    std::shared_ptr<Sequence> buildSequence (PlayMode, bool forExport) const;
    void rebuildSequence (bool resetPosition);
    void pushChannelsToEngine();
    void swapSample (Channel&, std::shared_ptr<const SampleData>);
    void replaceProject (Project&&, std::vector<std::unique_ptr<EffectChain::Slot>> masterSlots);

    var channelToVar (const Channel&, const File& baseDir) const;
    std::shared_ptr<Channel> channelFromVar (const var&, const File& baseDir, int newId, StringArray& warnings);
    var effectsToVar (const EffectChain&) const;
    std::vector<std::unique_ptr<EffectChain::Slot>> effectsFromVar (const var&, StringArray& warnings);
    var pluginToVar (const PluginDescription&, AudioPluginInstance*, const MemoryBlock& fallbackState) const;
    std::unique_ptr<AudioPluginInstance> pluginFromVar (const var&, bool isInstrument, PluginDescription& descOut,
                                                        MemoryBlock& stateOut, String& error);

    AudioEngine& engine;
    PluginManager& plugins;
    AudioFormatManager formats;

    Project project;
    File projectFile;
    bool dirty = false;
    int currentPatternId = 0;
    PlayMode playMode = PlayMode::pattern;
    ListenerList<Listener> listeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Session)
};
