#pragma once

#include "UI/WindowPlacement.h"

/** Keeps one editor window per plug-in instance. */
class PluginWindowManager
{
public:
    PluginWindowManager();
    ~PluginWindowManager();

    void show (AudioPluginInstance&, const String& title);
    void close (AudioPluginInstance*);   // nullptr closes every window
    void closeAll()                      { close (nullptr); }

private:
    class Window;
    std::vector<std::unique_ptr<Window>> windows;
};

/** Shows the plug-in scanner / list (JUCE's PluginListComponent) in its own window. */
class PluginManagerWindow : public AppWindow
{
public:
    PluginManagerWindow (AudioPluginFormatManager&, KnownPluginList&, const File& deadMansPedal, PropertiesFile*);
    void closeButtonPressed() override;

    std::function<void()> onClose;
};
