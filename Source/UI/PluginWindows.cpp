#include "UI/PluginWindows.h"
#include "UI/Theme.h"
#include "UI/Touch.h"
#include "UI/WindowPlacement.h"

class PluginWindowManager::Window : public AppWindow
{
public:
    Window (AudioPluginInstance& p, const String& title, PluginWindowManager& m)
        : AppWindow (title, DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          plugin (p), manager (m)
    {
        AudioProcessorEditor* editor = plugin.hasEditor() ? plugin.createEditorIfNeeded() : nullptr;

        if (editor == nullptr)
            editor = new GenericAudioProcessorEditor (plugin);

        // phones: a fixed-size editor that is bigger than the screen can be scrolled
        const auto editorResizes = editor->isResizable();
        setScrollableContent (editor, editorResizes ? 0 : editor->getWidth(), editorResizes ? 0 : editor->getHeight());
        setResizable (editorResizes, false);
        placeOnScreen (editor->getWidth(), editor->getHeight());

        if (! Touch::isMobile())
        {
            // on screen, slightly offset from other plug-in windows
            const auto offset = ((int) manager.windows.size() % 6) * 28;
            setTopLeftPosition (getX() + offset, getY() + offset);
            WindowPlacement::keepOnScreen (*this);
        }

        setVisible (true);
        WindowPlacement::keepOnScreenAfterShowing (*this);
    }

    ~Window() override
    {
        clearContentComponent(); // the editor must go before the plug-in
    }

    void closeButtonPressed() override
    {
        auto* p = &plugin;
        auto* m = &manager;
        MessageManager::callAsync ([m, p] { m->close (p); });
    }

    AudioPluginInstance& plugin;
    PluginWindowManager& manager;
};

PluginWindowManager::PluginWindowManager() = default;

PluginWindowManager::~PluginWindowManager()
{
    windows.clear();
}

void PluginWindowManager::show (AudioPluginInstance& plugin, const String& title)
{
    for (auto& w : windows)
    {
        if (&w->plugin == &plugin)
        {
            w->setVisible (true);
            w->toFront (true);
            return;
        }
    }

    windows.push_back (std::make_unique<Window> (plugin, title, *this));
}

void PluginWindowManager::close (AudioPluginInstance* plugin)
{
    windows.erase (std::remove_if (windows.begin(), windows.end(), [plugin] (const std::unique_ptr<Window>& w)
                                   { return plugin == nullptr || &w->plugin == plugin; }),
                   windows.end());
}

//==============================================================================
PluginManagerWindow::PluginManagerWindow (AudioPluginFormatManager& formats, KnownPluginList& list,
                                          const File& deadMansPedal, PropertiesFile* props)
    : AppWindow (Touch::isMobile() ? "Plug-ins (AUv3)" : "Plug-in Manager", DocumentWindow::closeButton)
{
    auto* content = new PluginListComponent (formats, list, deadMansPedal, props, false);
    content->setSize (760, 520);
    setScrollableContent (content, 0, 0);
    setResizable (true, false);
    placeOnScreen (760, 520);
    setVisible (true);
    WindowPlacement::keepOnScreenAfterShowing (*this);
}

void PluginManagerWindow::closeButtonPressed()
{
    if (onClose)
        MessageManager::callAsync ([cb = onClose] { cb(); });
}
