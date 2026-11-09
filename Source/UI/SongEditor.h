#pragma once

#include "Session.h"
#include "UI/Theme.h"
#include "UI/Touch.h"

/** The song arrangement: tracks with pattern blocks ("clips") that snap together like
    puzzle pieces. Drag patterns in from the pattern list, drag clips around,
    Alt/Ctrl-drag to copy, right-click for more. With a finger: drag empty space to scroll,
    hold a clip or a track name for the menu. */
class SongEditor : public Component,
                   public DragAndDropTarget,
                   private Session::Listener,
                   private Timer,
                   private ScrollBar::Listener
{
public:
    explicit SongEditor (Session&);
    ~SongEditor() override;

    void paint (Graphics&) override;
    void resized() override;
    void mouseDown (const MouseEvent&) override;
    void mouseDrag (const MouseEvent&) override;
    void mouseUp (const MouseEvent&) override;
    void mouseDoubleClick (const MouseEvent&) override;
    void mouseMove (const MouseEvent&) override;
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails&) override;
    bool keyPressed (const KeyPress&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    /** Set on phones, where the pattern editor is not visible next to the song: the clip menu
        then offers to open the clip's pattern. */
    std::function<void()> onEditPattern;

private:
    static constexpr int toolbarHeight = 32;
    static constexpr int rulerHeight = 22;
    static constexpr int headerWidth = 120;
    static constexpr int laneHeight = 48;
    static constexpr int scrollBarSize = 10;

    void sessionChanged (int changes) override;
    void timerCallback() override;
    void scrollBarMoved (ScrollBar*, double newRangeStart) override;

    Rectangle<int> getLanesArea() const;
    Rectangle<int> getRulerArea() const;
    Rectangle<int> getHeadersArea() const;
    double xToBeat (int x) const;
    int beatToX (double beat) const;
    int yToTrack (int y) const;
    int trackToY (int track) const;
    double getSnap() const;
    double snapBeat (double beat) const;
    double placeClip (int clipIdToIgnore, int track, double rawStart, double length) const;
    Clip* clipAt (Point<int>);
    Rectangle<int> getClipBounds (const Clip&, const Pattern&) const;
    void updateScrollBars();
    void setZoom (double newPixelsPerBeat, int anchorX);
    void showClipMenu (int clipId);
    void showTrackMenu (int track);
    void renameTrack (int track);
    void removeTrack (int track);
    void duplicateClip (int clipId);
    void paintClip (Graphics&, const Clip&, const Pattern&, bool selected, bool ghost);
    int getDropPatternId (const SourceDetails&) const;

    Session& session;

    Label title { {}, "SONG" };
    Label snapLabel { {}, "Snap" };
    ComboBox snapBox;
    TextButton zoomOutButton { "-" }, zoomInButton { "+" }, addTrackButton { "+ Track" };
    Label hintLabel;
    ScrollBar hScroll { false }, vScroll { true };

    double pixelsPerBeat = 20.0;
    double scrollBeat = 0.0;
    int scrollY = 0;

    int selectedClipId = 0;
    int draggingClipId = 0;
    double dragGrabOffset = 0.0;
    bool draggingPlayhead = false;

    bool dropActive = false;
    int dropPatternId = 0;
    int dropTrack = 0;
    double dropStart = 0.0;

    double lastPlayheadPos = -1.0;

    // touch: dragging empty space scrolls; holding shows a menu
    LongPress longPress;
    bool touchPanning = false;
    double panStartBeat = 0.0;
    int panStartY = 0;
};
