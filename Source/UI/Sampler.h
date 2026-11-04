#pragma once

#include "AI/StemSeparation.h"
#include "UI/SampleEditor.h"

/** The Sampler: open any audio file (or a channel's sound), pick ranges and turn each one
    into a new sample channel. It can also split a song into stems (vocals, drums, bass,
    guitar, piano, other) with AI, so you can sample from a single instrument. */
class SamplerComponent : public Component,
                         public FileDragAndDropTarget,
                         private Timer,
                         private ScrollBar::Listener
{
public:
    explicit SamplerComponent (Session&);
    ~SamplerComponent() override;

    bool loadFile (const File&);
    void loadSample (std::shared_ptr<const SampleData>, const String& name);
    bool hasSource() const noexcept               { return ! sources.empty(); }
    String getSourceName() const                  { return sourceName; }

    void paint (Graphics&) override;
    void resized() override;
    bool keyPressed (const KeyPress&) override;
    void mouseDown (const MouseEvent&) override   { grabKeyboardFocus(); }

    bool isInterestedInFileDrag (const StringArray&) override;
    void filesDropped (const StringArray&, int, int) override;

    struct SeparationSettings
    {
        Stems::ModelType model = Stems::ModelType::sixStems;
        StringArray stems { "vocals", "drums", "bass", "guitar", "piano", "other" };
        bool selectionOnly = false;
        bool addAsChannels = false;
    };

    std::function<void (const String&)> onTitleChanged;

private:
    struct Source
    {
        String id;                                // "original", "vocals", "novocals", ...
        String name;
        std::shared_ptr<const SampleData> data;
        double offsetSeconds = 0.0;               // where it starts on the original's timeline
    };

    void timerCallback() override;
    void scrollBarMoved (ScrollBar*, double newRangeStart) override;

    void setSources (std::vector<Source>, int selectIndex);
    void selectSource (int index);
    void rebuildSourceButtons();
    const Source* getCurrent() const;
    double frameToTime (const Source&, double frame) const;
    double timeToFrame (const Source&, double seconds) const;
    void updateRegions();
    void updateSelectionLabel();
    void updateButtons();
    void setStatus (const String&);

    void openFileChooser();
    void togglePreview();
    void addSlice();
    void showAutoSliceMenu();
    void applySlices (const std::vector<Range<int>>&);
    void showSliceMenu (int index);
    void sampleSelection();
    void sampleAllSlices();
    std::shared_ptr<Channel> createSample (Range<int> frames, const String& suffix);

    void showSeparationOptions();
    void startSeparation (SeparationSettings);
    void startDownload (Stems::ModelType);
    void finishSeparation();
    void cancelJobs();
    void loadModelFromFile();

    Session& session;
    std::vector<Source> sources;
    int current = 0;
    String sourceName;
    std::vector<Range<double>> slices;            // seconds on the original timeline
    int sampleCounter = 0;

    Label title, infoLabel, sourceLabel { {}, "SAMPLE FROM" }, selectionLabel, statusLabel, slicesLabel { {}, "SLICES" },
          jobLabel;
    TextButton openButton { "Open audio file..." }, separateButton { "AI: Separate stems..." }, cancelButton { "Cancel" };
    OwnedArray<TextButton> sourceButtons;
    WaveformView waveform;
    ScrollBar scrollBar { false };
    IconButton playButton { "Play", IconButton::Icon::play };
    ToggleButton loopToggle { "Loop" };
    TextButton selectAllButton { "Select all" }, zoomOutButton { "-" }, zoomInButton { "+" }, fitButton { "Fit" };
    TextButton addSliceButton { "+ Slice from selection" }, autoSliceButton { "Auto-slice..." }, clearSlicesButton { "Clear" };
    TextButton sampleSelectionButton { "Sample selection" }, sampleAllButton { "Sample all slices" };

    std::unique_ptr<StemSeparationJob> separationJob;
    std::unique_ptr<ModelDownloadJob> downloadJob;
    SeparationSettings pendingSettings;
    bool separationQueued = false;
    double jobStartTime = 0.0, separationOffsetSeconds = 0.0;
    double jobProgress = -1.0;

    std::unique_ptr<FileChooser> chooser;
};

//==============================================================================
/** One Sampler window for the whole app. Closing it only hides it, so the loaded file,
    the slices and any AI stems (which take a while to compute) are kept. */
class SamplerManager
{
public:
    explicit SamplerManager (Session&);
    ~SamplerManager();

    void show();
    void openFile (const File&);
    void openSample (std::shared_ptr<const SampleData>, const String& name);
    void close();

private:
    class Window;
    Window& getWindow();
    Session& session;
    std::unique_ptr<Window> window;
};
