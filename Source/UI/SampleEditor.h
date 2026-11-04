#pragma once

#include "Session.h"
#include "UI/PluginWindows.h"
#include "UI/Theme.h"

/** Draws a sample's waveform and lets the user select a range, zoom and scroll. */
class WaveformView : public Component
{
public:
    WaveformView();

    void setSample (std::shared_ptr<const SampleData>, Colour);
    Range<int> getSelection() const noexcept   { return selection; }
    void setSelection (Range<int>);
    int getCursor() const noexcept             { return cursor; }
    void setPlayPosition (double frame);       // < 0 hides the playhead

    Range<double> getVisibleRange() const noexcept { return visible; }
    void setVisibleRange (Range<double>);
    void zoom (double factor, int anchorX);
    void zoomToFit();

    std::function<void()> onSelectionChanged, onViewChanged;

    void paint (Graphics&) override;
    void resized() override                    { cacheValid = false; }
    void mouseDown (const MouseEvent&) override;
    void mouseDrag (const MouseEvent&) override;
    void mouseUp (const MouseEvent&) override;
    void mouseDoubleClick (const MouseEvent&) override;
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails&) override;

    static constexpr int rulerHeight = 20;

private:
    double xToFrame (float x) const;
    float frameToX (double frame) const;
    int clampFrame (double frame) const;
    void rebuildCache();

    std::shared_ptr<const SampleData> sample;
    Colour colour { Theme::accent };
    Range<double> visible;
    Range<int> selection;
    int cursor = 0;
    double playPosition = -1.0;
    int dragAnchor = 0;

    std::vector<float> mins[2], maxs[2];
    bool cacheValid = false;
};

//==============================================================================
/** Destructive editor for one sample channel: trim, fades, normalize, reverse ... and a
    VST effect rack whose result can be previewed and then baked into the sample. */
class SampleEditor : public Component,
                     private Session::Listener,
                     private ScrollBar::Listener,
                     private Timer
{
public:
    SampleEditor (Session&, PluginWindowManager&, int channelId);
    ~SampleEditor() override;

    std::function<void()> onCloseRequested;

    void paint (Graphics&) override;
    void resized() override;
    bool keyPressed (const KeyPress&) override;
    void mouseDown (const MouseEvent&) override  { grabKeyboardFocus(); }

    Channel* getChannel() const;

private:
    void sessionChanged (int changes) override;
    void timerCallback() override;
    void scrollBarMoved (ScrollBar*, double newRangeStart) override;

    void refreshFromChannel (bool resetView);
    void updateButtons();
    void updateSelectionLabel();
    void setStatus (const String&);

    using EditFunction = std::function<std::shared_ptr<SampleData> (const SampleData&, Range<int>)>;
    void applyEdit (const String& name, EditFunction, bool needsSelection);
    void togglePreview();
    void startPreview();

    void showAddEffectMenu();
    void removeEffect (int index);
    void rebuildEffectRows();
    void applyEffects();
    void exportWav();

    Session& session;
    PluginWindowManager& pluginWindows;
    const int channelId;
    const Channel* identity = nullptr;

    Label title, infoLabel, selectionLabel, statusLabel, editLabel { {}, "EDIT" }, effectsLabel { {}, "VST EFFECTS" },
          effectsHint, tailLabel { {}, "Tail" };
    TextButton undoButton { "Undo" }, redoButton { "Redo" }, revertButton { "Revert to original" }, exportButton { "Export WAV..." };
    IconButton playButton { "Play", IconButton::Icon::play };
    ToggleButton loopToggle { "Loop" }, hearEffectsToggle { "Hear effects" };
    TextButton selectAllButton { "Select all" }, zoomOutButton { "-" }, zoomInButton { "+" }, fitButton { "Fit" };
    OwnedArray<TextButton> editButtons;
    WaveformView waveform;
    ScrollBar scrollBar { false };

    EffectChain rack;
    OwnedArray<Component> effectRows;
    TextButton addEffectButton { "+ Add effect" }, applyEffectsButton { "Apply effects to selection" };
    ComboBox tailBox;

    std::unique_ptr<FileChooser> chooser;
};

//==============================================================================
/** Keeps one sample-editor window per channel. */
class SampleEditorManager
{
public:
    SampleEditorManager (Session&, PluginWindowManager&);
    ~SampleEditorManager();

    void open (int channelId);
    void close (int channelId);
    void closeAll();

private:
    class Window;
    Session& session;
    PluginWindowManager& pluginWindows;
    std::vector<std::unique_ptr<Window>> windows;
};
