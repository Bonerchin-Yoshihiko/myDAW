#include "UI/Touch.h"

PopupMenu::Options Touch::menuAtPointer (PopupMenu::Options options)
{
    const auto source = Desktop::getInstance().getMainMouseSource();

    if (! source.isTouch())
        return options.withMousePosition();

    // keep the menu out from under the finger
    const auto pos = source.getScreenPosition().roundToInt();
    return options.withTargetScreenArea (Rectangle<int> (pos, pos).expanded (28, 20));
}

//==============================================================================
LongPress::LongPress (std::function<void (Point<int>)> onLongPress) : callback (std::move (onLongPress)) {}

LongPress::~LongPress()
{
    stopTimer();
}

void LongPress::start (const MouseEvent& e)
{
    fired = false;
    stopTimer();

    if (Touch::isTouch (e) && ! e.mods.isPopupMenu())
    {
        position = e.getPosition();
        startTimer (delayMs);
    }
}

void LongPress::drag (const MouseEvent& e)
{
    if (isTimerRunning() && e.getDistanceFromDragStart() > Touch::dragThreshold)
        stopTimer();
}

void LongPress::cancel()
{
    stopTimer();
}

void LongPress::timerCallback()
{
    stopTimer();
    fired = true;

    if (callback)
        callback (position);
}
