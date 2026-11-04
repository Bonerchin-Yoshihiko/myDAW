#pragma once

#include "Session.h"
#include "UI/PluginWindows.h"
#include "UI/SampleEditor.h"
#include "UI/Theme.h"

/** The "channel rack": one row per channel, one cell per step. Click cells to toggle
    notes, drag to paint, Ctrl/Alt + mouse-wheel over a note to change its velocity. */
class StepGrid : public Component,
                 private Session::Listener,
                 private Timer
{
public:
    StepGrid (Session&, PluginWindowManager&, SampleEditorManager&);
    ~StepGrid() override;

    static constexpr int headerWidth = 276;
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
    void renameChannel (int channelId);
    void editChannel (int channelId);

    Session& session;
    PluginWindowManager& pluginWindows;
    SampleEditorManager& sampleEditors;
    OwnedArray<RowHeader> rows;
    std::vector<int> rowChannelIds;

    int cellWidth = 24;
    int lastVisibleWidth = 800;
    double playStep = -1.0;

    bool painting = false;
    bool paintAdds = true;
    int paintRow = -1;
    int lastPaintStep = -1;
};
