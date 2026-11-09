#pragma once

#include "AppCore.h"
#include "UI/PatternEditor.h"
#include "UI/PatternList.h"
#include "UI/SongEditor.h"
#include "UI/TransportBar.h"
#include "UI/WindowPlacement.h"

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

    /** Phones: saves unsaved changes without asking (iOS may end the app while it is in the
        background). An untitled project is saved as "Autosave" in the app's Documents folder. */
    void autoSave();

    /** Opens the most recently used project, if it still exists. */
    void reopenLastProject();

    String getWindowTitle() const;
    std::function<void()> onTitleChanged;

    static void showMessage (const String& title, const String& message, bool warning = true);

private:
    void sessionChanged (int changes) override;
    void showFileMenu (Component&);
    void showAudioSettings();
    void showView (bool song);
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

    // small screens (phones): one editor at a time, picked with these
    TextButton songViewButton { "Song" }, patternViewButton { "Pattern" };
    bool smallScreen = false, showingSong = true;
    Rectangle<int> viewButtonsArea;

    std::unique_ptr<PluginManagerWindow> pluginManagerWindow;
    std::unique_ptr<AppWindow> audioSettingsWindow;
    std::unique_ptr<FileChooser> chooser;
    TooltipWindow tooltipWindow { this, 700 };
};
