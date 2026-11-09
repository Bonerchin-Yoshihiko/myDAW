#pragma once

#include "Session.h"
#include "UI/PluginWindows.h"
#include "UI/SampleEditor.h"
#include "UI/Theme.h"
#include "UI/Touch.h"

/** The "channel rack": one row per channel, one cell per step. Click cells to toggle
    notes, drag to paint, Ctrl/Alt + mouse-wheel over a note to change its velocity.
    With a finger: tap to toggle, swipe sideways to paint (up/down scrolls), hold a note
    for its velocity. */
class StepGrid : public Component,
                 private Session::Listener,
                 private Timer
{
public:
    StepGrid (Session&, PluginWindowManager&, SampleEditorManager&, SamplerManager&);
    ~StepGrid() override;

    static constexpr int rowHeight = 34;

    std::function<void (int channelId)> onOpenPianoRoll;

    /** Recomputes the cell size for the given visible width and resizes the component. */
    void updateLayout (int visibleWidth);
    void setPlayPosition (double stepPosition); // < 0 = not playing this pattern

    void paint (Graphics&) override;
    void mouseDown (const MouseEvent&) override;
    void mouseDrag (const MouseEvent&) override;
    void mouseUp (const MouseEvent&) override;
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails&) override;

    class RowHeader;

private:
    void sessionChanged (int changes) override;
    void timerCallback() override;
    void rebuildRows();
    bool getCellAt (Point<int>, int& row, int& step) const;
    void applyPaint (int row, int step);
    void showChannelMenu (int channelId, Component& target);
    void showNoteMenu (int row, int step);
    void renameChannel (int channelId);
    void editChannel (int channelId);
    void startPainting (int row, int step, bool adds);

    Session& session;
    PluginWindowManager& pluginWindows;
    SampleEditorManager& sampleEditors;
    SamplerManager& sampler;
    OwnedArray<RowHeader> rows;
    std::vector<int> rowChannelIds;

    int headerWidth = 276;       // narrower on phones
    int cellWidth = 24;
    int lastVisibleWidth = 800;
    double playStep = -1.0;

    bool painting = false;
    bool paintAdds = true;
    int paintRow = -1;
    int lastPaintStep = -1;

    // a finger on a cell: nothing happens until it is lifted (tap = toggle), moved sideways
    // (paint), moved up/down (scrolls the view) or held (note menu)
    bool touchPending = false;
    int touchRow = -1, touchStep = -1;
    LongPress longPress;
};
