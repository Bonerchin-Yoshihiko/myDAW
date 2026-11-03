#include "UI/PluginWindows.h"
#include "UI/Theme.h"

class PluginWindowManager::Window : public DocumentWindow
{
public:
    Window (AudioPluginInstance& p, const String& title, PluginWindowManager& m)
        : DocumentWindow (title, Theme::background, DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          plugin (p), manager (m)
    {
        AudioProcessorEditor* editor = plugin.hasEditor() ? plugin.createEditorIfNeeded() : nullptr;

        if (editor == nullptr)
            editor = new GenericAudioProcessorEditor (plugin);

        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        setTopLeftPosition (120 + (int) manager.windows.size() * 30, 120 + (int) manager.windows.size() * 30);
        setVisible (true);
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
    : DocumentWindow ("Plug-in Manager", Theme::background, DocumentWindow::closeButton)
{
    auto* content = new PluginListComponent (formats, list, deadMansPedal, props, false);
    content->setSize (760, 520);
    setUsingNativeTitleBar (true);
    setContentOwned (content, true);
    setResizable (true, false);
    centreWithSize (760, 520);
    setVisible (true);
}

void PluginManagerWindow::closeButtonPressed()
{
    if (onClose)
        MessageManager::callAsync ([cb = onClose] { cb(); });
}
