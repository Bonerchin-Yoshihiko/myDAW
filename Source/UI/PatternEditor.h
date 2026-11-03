#pragma once

#include "UI/PianoRoll.h"
#include "UI/StepGrid.h"

/** Bottom half of the window: edits the currently selected pattern, either as a step
    grid (all channels) or as a piano roll (one channel). */
class PatternEditor : public Component,
                      public FileDragAndDropTarget,
                      private Session::Listener,
                      private Timer
{
public:
    PatternEditor (Session&, PluginWindowManager&);
    ~PatternEditor() override;

    void paint (Graphics&) override;
    void paintOverChildren (Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const StringArray&) override;
    void fileDragEnter (const StringArray&, int, int) override { dragHighlight = true; repaint(); }
    void fileDragExit (const StringArray&) override              { dragHighlight = false; repaint(); }
    void filesDropped (const StringArray&, int, int) override;

    void showPianoRoll (int channelId);
    void showStepGrid();

    std::function<void (const String&)> onError;

private:
    void sessionChanged (int changes) override;
    void timerCallback() override;
    void updateHeader();
    void applyLengthChange();
    void addSamples (const Array<File>&);
    void showInstrumentMenu();
    void showColourMenu();
    double getPatternPlayStep() const;

    Session& session;
    PluginWindowManager& pluginWindows;

    Label title;
    TextEditor nameEditor;
    TextButton colourButton;
    Label barsLabel { {}, "Bars" }, signatureLabel { {}, "Time" }, slashLabel { {}, "/" }, stepsLabel { {}, "Steps/beat" };
    ComboBox barsBox, beatsBox, unitBox, stepsBox;
    TextButton addSampleButton { "+ Sample" }, addInstrumentButton { "+ Instrument" }, backButton { "< Step grid" };
    Label pianoRollLabel;

    Viewport viewport;
    StepGrid grid;
    PianoRoll pianoRoll;
    bool showingPianoRoll = false;
    bool dragHighlight = false;

    std::unique_ptr<FileChooser> chooser;
};
