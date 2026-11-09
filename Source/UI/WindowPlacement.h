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

//==============================================================================
/** Shows a component that needs a certain minimum size in a scrollable view, for screens
    that are smaller than that (phones). */
class ScrollingContent : public Viewport
{
public:
    enum class Mode
    {
        fillView,        // the content fills the view, but is never smaller than the minimum size
        followContent    // the view takes the content's size, up to the maximum size
    };

    ScrollingContent (std::unique_ptr<Component> content, Mode, Point<int> sizeLimit);
    ~ScrollingContent() override;

    Component& getContent() noexcept   { return *content; }
    void resized() override;

private:
    void updateSize();

    struct SizeWatcher : public juce::ComponentListener
    {
        explicit SizeWatcher (ScrollingContent& o) : owner (o) {}
        void componentMovedOrResized (Component&, bool, bool wasResized) override  { if (wasResized) owner.updateSize(); }
        ScrollingContent& owner;
    };

    std::unique_ptr<Component> content;
    const Mode mode;
    const Point<int> limit;
    SizeWatcher sizeWatcher { *this };
};

//==============================================================================
/** The app's top-level windows. On desktops: a DocumentWindow with a native title bar.
    On phones and tablets, where there is no window manager: the window fills the screen, keeps
    its title bar and content out of the notch, rounded corners and home indicator (the safe
    area), cannot be dragged around and has a big close button. */
class AppWindow : public DocumentWindow
{
public:
    /** `desktopButtons`: DocumentWindow::closeButton etc. (mobile windows only get a close button,
        or none if desktopButtons is 0). */
    AppWindow (const String& title, int desktopButtons);

    /** Desktop: sizes the window to its content (or the preferred size) and fits it on the screen.
        Mobile: fills the screen. */
    void placeOnScreen (int preferredWidth, int preferredHeight, const Component* near = nullptr);

    /** Takes ownership of the content. On a screen smaller than the minimum size the content is
        shown in a scrollable view. */
    void setScrollableContent (Component* content, int minimumWidth, int minimumHeight);

    /** The content passed to setScrollableContent(). */
    Component* getMainContent() const;

    BorderSize<int> getBorderThickness() const override;
};

namespace WindowPlacement
{
    /** Phones: wraps the content of a pop-up (call-out box) in a scrollable view when the screen
        is not tall enough for it. Desktop: returns the content unchanged. */
    std::unique_ptr<Component> limitToScreen (std::unique_ptr<Component> content, const Component& parent);

    /** True when the screen is too small for the normal (desktop) layout. */
    bool isSmallScreen (int width, int height);
}
