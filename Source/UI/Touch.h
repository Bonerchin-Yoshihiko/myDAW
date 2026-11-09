#pragma once

#include "Common.h"

/** Helpers for touch screens (iPhone / iPad, and touch screens on desktops): a finger has no
    right button, no mouse wheel, no modifier keys and no hover. */
namespace Touch
{
    /** True when the app runs on a phone or tablet. */
    constexpr bool isMobile()
    {
       #if JUCE_IOS || JUCE_ANDROID
        return true;
       #else
        return false;
       #endif
    }

    inline bool isTouch (const MouseEvent& e)  { return e.source.isTouch(); }

    /** How far (in pixels) a finger may wander before a press counts as a drag. */
    constexpr int dragThreshold = 8;

    /** Menu options for a context menu at the pointer. With a mouse the menu opens at the
        pointer; with a finger it opens next to it, because lifting the finger over a menu
        item would pick that item. */
    PopupMenu::Options menuAtPointer (PopupMenu::Options = PopupMenu::Options());
}

/** Turns a finger held still for a moment into a "right click", so that the context menus a
    mouse opens with the right button can be reached on a touch screen. The owner feeds it its
    mouse events; after it has fired, the owner should ignore the rest of that gesture
    (see hasFired()). Mouse and pen input is ignored. */
class LongPress : private Timer
{
public:
    /** `position` is where the finger went down, relative to the component that received the
        mouseDown passed to start(). */
    explicit LongPress (std::function<void (Point<int> position)> onLongPress);
    ~LongPress() override;

    void start (const MouseEvent&);     // call from mouseDown
    void drag (const MouseEvent&);      // call from mouseDrag: moving the finger cancels it
    void cancel();                      // call from mouseUp (or whenever the press is consumed)

    /** True from the moment the long press fired until the next start(). */
    bool hasFired() const noexcept      { return fired; }

    static constexpr int delayMs = 450;

private:
    void timerCallback() override;

    std::function<void (Point<int>)> callback;
    Point<int> position;
    bool fired = false;
};
