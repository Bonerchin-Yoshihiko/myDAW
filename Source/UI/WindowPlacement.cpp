#include "UI/WindowPlacement.h"

namespace
{
    /** The native frame (title bar + borders) around the window's content. It is not known on
        every platform before the window is shown, so assume a generous typical size. */
    BorderSize<int> getFrame (const ResizableWindow& w)
    {
        if (! w.isUsingNativeTitleBar())
            return {};

        BorderSize<int> frame (36, 8, 8, 8);

        if (auto* peer = w.getPeer())
            if (auto f = peer->getFrameSizeIfPresent())
                frame = BorderSize<int> (jmax (frame.getTop(), f->getTop()), jmax (frame.getLeft(), f->getLeft()),
                                         jmax (frame.getBottom(), f->getBottom()), jmax (frame.getRight(), f->getRight()));

        return frame;
    }

    const Displays::Display* findDisplay (const Component* near)
    {
        auto& displays = Desktop::getInstance().getDisplays();
        const Displays::Display* display = nullptr;

        if (near != nullptr && near->isShowing())
            display = displays.getDisplayForRect (near->getScreenBounds());

        if (display == nullptr)
            display = displays.getDisplayForPoint (Desktop::getMousePosition());

        if (display == nullptr)
            display = displays.getPrimaryDisplay();

        return display;
    }

    void limitMinimumSize (ResizableWindow& w, Rectangle<int> usable)
    {
        if (auto* c = w.getConstrainer())
        {
            const auto minW = jmin (c->getMinimumWidth(), usable.getWidth());
            const auto minH = jmin (c->getMinimumHeight(), usable.getHeight());

            if (minW != c->getMinimumWidth() || minH != c->getMinimumHeight())
                w.setResizeLimits (minW, minH, c->getMaximumWidth(), c->getMaximumHeight());
        }
    }
}

Rectangle<int> WindowPlacement::getUsableArea (const Displays::Display& display, BorderSize<int> frame)
{
    // keep a small gap to the screen edges and the task bar / dock
    auto area = display.userArea.reduced (jmin (16, display.userArea.getWidth() / 50),
                                          jmin (12, display.userArea.getHeight() / 50));
    return frame.subtractedFrom (area);
}

void WindowPlacement::fitOnScreen (ResizableWindow& w, int preferredWidth, int preferredHeight, const Component* near)
{
    const auto* display = findDisplay (near);

    if (display == nullptr)
    {
        w.centreWithSize (preferredWidth, preferredHeight);
        return;
    }

    const auto usable = getUsableArea (*display, getFrame (w));
    limitMinimumSize (w, usable);

    const auto width = jmin (preferredWidth, usable.getWidth());
    const auto height = jmin (preferredHeight, usable.getHeight());
    w.setBounds (usable.withSizeKeepingCentre (width, height));
}

void WindowPlacement::keepOnScreen (ResizableWindow& w)
{
    const auto* display = Desktop::getInstance().getDisplays().getDisplayForRect (w.getBounds());

    if (display == nullptr)
        display = findDisplay (nullptr);

    if (display == nullptr)
        return;

    const auto usable = getUsableArea (*display, getFrame (w));
    limitMinimumSize (w, usable);

    auto b = w.getBounds();
    b.setSize (jmin (b.getWidth(), usable.getWidth()), jmin (b.getHeight(), usable.getHeight()));
    w.setBounds (b.constrainedWithin (usable));
}

void WindowPlacement::keepOnScreenAfterShowing (ResizableWindow& w)
{
    for (auto delay : { 150, 600 })
        Timer::callAfterDelay (delay, [safe = Component::SafePointer<ResizableWindow> (&w)]
        {
            if (safe != nullptr && safe->isShowing() && ! safe->isFullScreen() && ! safe->isMinimised())
                keepOnScreen (*safe);
        });
}

String WindowPlacement::getStateString (ResizableWindow& w)
{
    // JUCE's state is "x y w h", prefixed with "fs " when maximised; it always holds the
    // normal (not maximised) bounds, which is what we want to restore
    return w.getWindowStateAsString().replace ("fs ", "").upToFirstOccurrenceOf (" frame", false, false).trim();
}

bool WindowPlacement::restoreState (ResizableWindow& w, const String& state)
{
    if (state.isEmpty())
        return false;

    const auto bounds = Rectangle<int>::fromString (state);

    if (bounds.getWidth() < 200 || bounds.getHeight() < 150)
        return false;

    // only accept positions where the whole window, title bar included, is on a current screen
    // (with a little tolerance: maximised windows tuck their borders behind the screen edges)
    const auto frame = getFrame (w);
    const auto withTitleBar = bounds.withTop (bounds.getY() - jmin (frame.getTop(), 30));

    for (auto& display : Desktop::getInstance().getDisplays().displays)
    {
        if (display.userArea.expanded (10).contains (withTitleBar))
        {
            limitMinimumSize (w, getUsableArea (display, frame));
            w.setBounds (bounds);
            return true;
        }
    }

    return false;
}
