#pragma once

#include "Session.h"
#include "UI/Theme.h"
#include "UI/Touch.h"

/** Piano roll for one channel of the current pattern.
    Click to add a note, drag to move, drag the right edge to change the length,
    right-click (or double-click) to delete, Ctrl/Alt + mouse-wheel over a note for velocity.
    With a finger: tap to add, drag a note to move it, drag empty space or the keyboard to
    scroll, double-tap to delete, hold a note for velocity / delete. */
class PianoRoll : public Component,
                  private Session::Listener
{
public:
    explicit PianoRoll (Session&);
    ~PianoRoll() override;

    static constexpr int keyWidth = 58;
    static constexpr int noteHeight = Touch::isMobile() ? 20 : 14;   // finger-sized rows on phones

    void setChannel (int channelId);
    int getChannelId() const noexcept { return channelId; }
    int getYForPitch (int pitch) const { return (127 - pitch) * noteHeight; }
    int getSuggestedScrollPitch() const;

    void updateLayout (int visibleWidth);
    void setPlayPosition (double stepPosition);

    void paint (Graphics&) override;
    void mouseMove (const MouseEvent&) override;
    void mouseDown (const MouseEvent&) override;
    void mouseDrag (const MouseEvent&) override;
    void mouseUp (const MouseEvent&) override;
    void mouseDoubleClick (const MouseEvent&) override;
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails&) override;

private:
    enum class DragMode { none, move, resize, keyboard };

    void sessionChanged (int changes) override;
    int findNoteAt (Point<int>, bool& onRightEdge) const;
    Rectangle<int> getNoteBounds (const Note&) const;
    int xToStep (int x) const;
    int yToPitch (int y) const;
    void startPreview (int pitch);
    void stopPreview();
    void showNoteMenu (int noteIndex);
    void addNoteAt (Point<int>);

    Session& session;
    int channelId = 0;
    int cellWidth = 20;
    int lastVisibleWidth = 800;
    double playStep = -1.0;
    int lastLength = 1;

    DragMode dragMode = DragMode::none;
    int dragNoteIndex = -1;
    Note dragOriginal;
    int dragStartStep = 0, dragStartPitch = 0;
    int previewPitch = -1;

    // a finger on empty space adds a note only when it is lifted without moving
    // (moving it scrolls the view)
    bool touchPending = false, touchInput = false;
    LongPress longPress;
};
