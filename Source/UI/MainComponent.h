#pragma once

#include "AppCore.h"
#include "UI/PatternEditor.h"
#include "UI/PatternList.h"
#include "UI/SongEditor.h"
#include "UI/TransportBar.h"

class MainComponent : public Component,
                      public DragAndDropContainer,
                      private Session::Listener
{
public:
    explicit MainComponent (AppCore&);
    ~MainComponent() override;

    void paint (Graphics&) override;
    void resized() override;
    bool keyPressed (const KeyPress&) override;

    void newProject();
    void openProject();
    void openFile (const File&);
    void saveProject (std::function<void (bool)> done = {});
    void saveProjectAs (std::function<void (bool)> done = {});
    void exportWav();

    /** Asks to save unsaved changes, then calls `proceed` unless the user cancelled. */
    void askToSaveChanges (std::function<void()> proceed);

    String getWindowTitle() const;
    std::function<void()> onTitleChanged;

    static void showMessage (const String& title, const String& message, bool warning = true);

private:
    void sessionChanged (int changes) override;
    void showFileMenu (Component&);
    void showAudioSettings();
    void showPluginManager();
    void rememberRecentFile (const File&);
    File getDefaultDirectory (const String& key) const;

    AppCore& core;
    Session& session;

    TransportBar transport;
    PatternList patternList;
    SongEditor songEditor;
    PatternEditor patternEditor;

    StretchableLayoutManager layout;
    StretchableLayoutResizerBar splitter { &layout, 1, false };

    std::unique_ptr<PluginManagerWindow> pluginManagerWindow;
    std::unique_ptr<FileChooser> chooser;
    TooltipWindow tooltipWindow { this, 700 };
};
