#pragma once

#include "Common.h"

/** Keeps windows (and their title bars, with the close / minimise buttons) on the screen,
    whatever the screen size or display scaling (e.g. 150 % on a 1920x1080 laptop leaves
    only 1280x720 for applications). */
namespace WindowPlacement
{
    /** Sizes the window to at most the preferred size so that it fits completely on the screen
        it belongs to (the one under `near` if given, otherwise the one under the mouse), and
        centres it there. Also lowers the window's minimum size if the screen is smaller. */
    void fitOnScreen (ResizableWindow&, int preferredWidth, int preferredHeight, const Component* near = nullptr);

    /** Moves (and if needed shrinks) the window so that it is completely on its screen,
        without re-centring it. */
    void keepOnScreen (ResizableWindow&);

    /** Same, a moment after the window has appeared: some window managers apply their own
        placement when a window is first shown. */
    void keepOnScreenAfterShowing (ResizableWindow&);

    /** Restores bounds saved with getStateString(), but only if they still fit on one of the
        current screens. Returns false (and changes nothing) otherwise. */
    bool restoreState (ResizableWindow&, const String& state);
    String getStateString (ResizableWindow&);

    /** The area a window's content may use on the given screen, leaving room for the frame. */
    Rectangle<int> getUsableArea (const Displays::Display&, BorderSize<int> frame);
}
