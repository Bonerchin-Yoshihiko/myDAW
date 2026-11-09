#include "UI/MainComponent.h"
#include "UI/AppFiles.h"
#include "UI/ChannelSettings.h"
#include "UI/Touch.h"

MainComponent::MainComponent (AppCore& c)
    : core (c), session (*c.session), transport (session), patternList (session),
      songEditor (session), patternEditor (session, c.pluginWindows, *c.sampleEditors, *c.sampler)
{
    addAndMakeVisible (transport);
    addAndMakeVisible (patternList);
    addAndMakeVisible (songEditor);
    addAndMakeVisible (splitter);
    addAndMakeVisible (patternEditor);

    layout.setItemLayout (0, 140, -1.0, -0.42);   // song
    layout.setItemLayout (1, 6, 6, 6);            // splitter
    layout.setItemLayout (2, 200, -1.0, -0.58);   // pattern editor

    transport.onFileMenu = [this] (Component& target) { showFileMenu (target); };
    transport.onMasterEffects = [this] (Component& target) { ChannelSettings::show (session, core.pluginWindows, 0, target); };
    transport.onPluginManager = [this] (Component&) { showPluginManager(); };
    transport.onAudioSettings = [this] (Component&) { showAudioSettings(); };
    transport.onExportWav = [this] { exportWav(); };

    auto errorHandler = [] (const String& message) { showMessage ("myDAW", message); };
    patternList.onError = errorHandler;
    patternEditor.onError = errorHandler;

    for (auto* b : { &songViewButton, &patternViewButton })
    {
        b->setClickingTogglesState (false);
        b->setColour (TextButton::buttonOnColourId, Theme::accent.withAlpha (0.85f));
        addChildComponent (b);
    }

    songViewButton.onClick = [this] { showView (true); };
    patternViewButton.onClick = [this] { showView (false); };

    session.addListener (this);
    setWantsKeyboardFocus (true);
    setSize (1360, 860);
}

MainComponent::~MainComponent()
{
    session.removeListener (this);
}

void MainComponent::paint (Graphics& g)
{
    g.fillAll (Theme::background);

    if (smallScreen)
    {
        g.setColour (Theme::panel);
        g.fillRect (viewButtonsArea);
        g.setColour (Theme::outline);
        g.drawVerticalLine (viewButtonsArea.getRight() - 1, (float) viewButtonsArea.getY(), (float) viewButtonsArea.getBottom());
    }
}

void MainComponent::resized()
{
    auto r = getLocalBounds();

    // Phones (in landscape) are too small for the song and the pattern editor at once: show
    // one of them, picked with the buttons above the pattern list.
    smallScreen = WindowPlacement::isSmallScreen (getWidth(), getHeight());
    transport.setSmallScreen (smallScreen);
    patternList.setSmallScreen (smallScreen);

    if (smallScreen)
        songEditor.onEditPattern = [this] { showView (false); };
    else
        songEditor.onEditPattern = nullptr;

    transport.setBounds (r.removeFromTop (smallScreen ? 44 : 48));

    if (smallScreen)
    {
        auto left = r.removeFromLeft (jlimit (150, 180, r.getWidth() / 5));
        viewButtonsArea = left.removeFromTop (40);
        auto buttons = viewButtonsArea.reduced (6, 5).withTrimmedRight (1);
        songViewButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2));
        patternViewButton.setBounds (buttons);
        patternList.setBounds (left);

        songViewButton.setVisible (true);
        patternViewButton.setVisible (true);
        songViewButton.setToggleState (showingSong, dontSendNotification);
        patternViewButton.setToggleState (! showingSong, dontSendNotification);
        splitter.setVisible (false);
        songEditor.setVisible (showingSong);
        patternEditor.setVisible (! showingSong);
        songEditor.setBounds (r);
        patternEditor.setBounds (r);
        return;
    }

    songViewButton.setVisible (false);
    patternViewButton.setVisible (false);
    splitter.setVisible (true);
    songEditor.setVisible (true);
    patternEditor.setVisible (true);

    patternList.setBounds (r.removeFromLeft (jlimit (170, 230, getWidth() / 6)));

    Component* comps[] = { &songEditor, &splitter, &patternEditor };
    layout.layOutComponents (comps, 3, r.getX(), r.getY(), r.getWidth(), r.getHeight(), true, true);
}

void MainComponent::showView (bool song)
{
    showingSong = song;
    resized();
    repaint();
}

bool MainComponent::keyPressed (const KeyPress& key)
{
    const auto cmd = ModifierKeys::commandModifier;

    if (key == KeyPress::spaceKey)                                    { session.togglePlay(); return true; }
    if (key == KeyPress ('s', cmd, 0))                                { saveProject(); return true; }
    if (key == KeyPress ('s', cmd | ModifierKeys::shiftModifier, 0))  { saveProjectAs(); return true; }
    if (key == KeyPress ('o', cmd, 0))                                { openProject(); return true; }
    if (key == KeyPress ('n', cmd, 0))                                { newProject(); return true; }
    if (key == KeyPress ('e', cmd, 0))                                { exportWav(); return true; }
    if (key == KeyPress (KeyPress::F5Key))                            { session.setPlayMode (Session::PlayMode::pattern); return true; }
    if (key == KeyPress (KeyPress::F6Key))                            { session.setPlayMode (Session::PlayMode::song); return true; }

    return false;
}

void MainComponent::sessionChanged (int changes)
{
    if ((changes & (Session::dirtyChanged | Session::projectReplaced)) != 0 && onTitleChanged)
        onTitleChanged();
}

String MainComponent::getWindowTitle() const
{
    return session.getProjectName() + (session.isDirty() ? " *" : "") + " - myDAW";
}

void MainComponent::showMessage (const String& title, const String& message, bool warning)
{
    AlertWindow::showAsync (MessageBoxOptions().withIconType (warning ? MessageBoxIconType::WarningIcon
                                                                      : MessageBoxIconType::InfoIcon)
                                               .withTitle (title)
                                               .withMessage (message)
                                               .withButton ("OK"),
                            nullptr);
}

File MainComponent::getDefaultDirectory (const String& key) const
{
    const auto path = core.properties.getUserSettings()->getValue (key);

    if (File::isAbsolutePath (path) && File (path).isDirectory())
        return File (path);

    return File::getSpecialLocation (File::userDocumentsDirectory);
}

void MainComponent::rememberRecentFile (const File& f)
{
    auto* settings = core.properties.getUserSettings();
    RecentlyOpenedFilesList recent;
    recent.restoreFromString (settings->getValue ("recentProjects"));
    recent.addFile (f);
    recent.setMaxNumberOfItems (10);
    settings->setValue ("recentProjects", recent.toString());
    settings->setValue ("projectDir", f.getParentDirectory().getFullPathName());
    settings->saveIfNeeded();
}

//==============================================================================
void MainComponent::askToSaveChanges (std::function<void()> proceed)
{
    if (! session.isDirty())
    {
        proceed();
        return;
    }

    AlertWindow::showAsync (MessageBoxOptions().withIconType (MessageBoxIconType::QuestionIcon)
                                               .withTitle ("Unsaved changes")
                                               .withMessage ("Save the changes to \"" + session.getProjectName() + "\"?")
                                               .withButton ("Save")
                                               .withButton ("Don't save")
                                               .withButton ("Cancel"),
                            [safe = SafePointer<MainComponent> (this), proceed] (int result)
    {
        if (safe == nullptr)
            return;

        if (result == 1)
            safe->saveProject ([proceed] (bool saved) { if (saved) proceed(); });
        else if (result == 2)
            proceed();
    });
}

void MainComponent::autoSave()
{
    if (! session.isDirty())
        return;

    auto file = session.getProjectFile();

    if (file == File())
        file = AppFiles::getDocumentsFolder().getChildFile (String ("Autosave") + Session::projectExtension);

    if (session.saveProject (file).wasOk())
        rememberRecentFile (file);
}

void MainComponent::reopenLastProject()
{
    RecentlyOpenedFilesList recent;
    recent.restoreFromString (core.properties.getUserSettings()->getValue ("recentProjects"));
    recent.removeNonExistentFiles();

    if (recent.getNumFiles() > 0)
    {
        StringArray warnings;

        if (session.loadProject (recent.getFile (0), warnings).failed())
            session.newProject();
    }
}

void MainComponent::newProject()
{
    askToSaveChanges ([this] { session.newProject(); });
}

void MainComponent::openProject()
{
    askToSaveChanges ([this]
    {
        AppFiles::OpenRequest request;
        request.title = "Open project";
        request.initialFolder = getDefaultDirectory ("projectDir");
        request.wildcard = String ("*") + Session::projectExtension;
        request.appFileType = true;
        request.copyInto = AppFiles::getDocumentsFolder();

        AppFiles::chooseFilesToOpen (chooser, request, [this] (const Array<File>& files)
        {
            if (! files.isEmpty())
                openFile (files.getFirst());
        });
    });
}

void MainComponent::openFile (const File& file)
{
    StringArray warnings;
    const auto result = session.loadProject (file, warnings);

    if (result.failed())
    {
        showMessage ("Open project", result.getErrorMessage());
        return;
    }

    rememberRecentFile (file);

    if (! warnings.isEmpty())
        showMessage ("Project opened with problems", warnings.joinIntoString ("\n"));
}

void MainComponent::saveProject (std::function<void (bool)> done)
{
    if (session.getProjectFile() == File())
    {
        saveProjectAs (std::move (done));
        return;
    }

    const auto result = session.saveProject (session.getProjectFile());

    if (result.failed())
        showMessage ("Save project", result.getErrorMessage());
    else
        rememberRecentFile (session.getProjectFile());

    if (done)
        done (result.wasOk());
}

void MainComponent::saveProjectAs (std::function<void (bool)> done)
{
    const auto initial = session.getProjectFile() != File()
                             ? session.getProjectFile()
                             : getDefaultDirectory ("projectDir").getChildFile ("My Song" + String (Session::projectExtension));

    AppFiles::SaveRequest request;
    request.title = "Save project";
    request.suggestedFile = initial;
    request.extension = Session::projectExtension;
    request.parent = this;

    AppFiles::chooseFileToSave (chooser, request, [this, done] (const File& file)
    {
        if (file == File())
        {
            if (done) done (false);
            return;
        }

        const auto result = session.saveProject (file);

        if (result.failed())
            showMessage ("Save project", result.getErrorMessage());
        else
            rememberRecentFile (file);

        if (done)
            done (result.wasOk());
    });
}

void MainComponent::exportWav()
{
    if (session.getProject().clips.empty())
    {
        showMessage ("Export WAV", "The song is empty. Drag some patterns onto the song timeline first.");
        return;
    }

    const auto name = session.getProjectFile() != File() ? session.getProjectName() : String ("My Song");

    AppFiles::SaveRequest request;
    request.title = "Export song as WAV";
    request.suggestedFile = getDefaultDirectory ("exportDir").getChildFile (name + ".wav");
    request.extension = ".wav";
    request.mobileSubfolder = "Exports";
    request.parent = this;

    AppFiles::chooseFileToSave (chooser, request, [this] (const File& file)
    {
        if (file == File())
            return;

        core.properties.getUserSettings()->setValue ("exportDir", file.getParentDirectory().getFullPathName());

        MouseCursor::showWaitCursor();
        const auto result = session.exportWav (file);
        MouseCursor::hideWaitCursor();

        if (result.failed())
            showMessage ("Export WAV", result.getErrorMessage());
        else if (Touch::isMobile())
            AppFiles::offerToShare (file, this);
        else
            showMessage ("Export WAV", "Exported to\n" + file.getFullPathName(), false);
    });
}

//==============================================================================
void MainComponent::showFileMenu (Component& target)
{
    PopupMenu recentMenu;
    RecentlyOpenedFilesList recent;
    recent.restoreFromString (core.properties.getUserSettings()->getValue ("recentProjects"));
    recent.removeNonExistentFiles();

    for (int i = 0; i < recent.getNumFiles(); ++i)
    {
        const auto f = recent.getFile (i);
        recentMenu.addItem (f.getFileName(), [this, f] { askToSaveChanges ([this, f] { openFile (f); }); });
    }

    // keyboard shortcuts are only shown where there is a keyboard
    auto item = [] (const String& text, const String& shortcut, std::function<void()> action)
    {
        PopupMenu::Item i (text);
        i.action = std::move (action);

        if (! Touch::isMobile())
            i.shortcutKeyDescription = shortcut;

        return i;
    };

    PopupMenu m;
    m.addItem (item ("New project", "Ctrl+N", [this] { newProject(); }));
    m.addItem (item ("Open...", "Ctrl+O", [this] { openProject(); }));
    m.addSubMenu ("Open recent", recentMenu, recent.getNumFiles() > 0);
    m.addSeparator();
    m.addItem (item ("Save", "Ctrl+S", [this] { saveProject(); }));
    m.addItem ("Save as...", [this] { saveProjectAs(); });
    m.addSeparator();
    m.addItem (item ("Export song as WAV...", "Ctrl+E", [this] { exportWav(); }));
    m.addSeparator();
    m.addItem ("Sampler / AI stem separation...", [this] { core.sampler->show(); });

    if (smallScreen)   // the transport bar has no room for this button
        m.addItem ("Master effects...", [this, t = SafePointer<Component> (&target)]
        {
            if (t != nullptr)
                ChannelSettings::show (session, core.pluginWindows, 0, *t);
        });

    m.addSeparator();
    m.addItem ("Audio settings...", [this] { showAudioSettings(); });
    m.addItem (Touch::isMobile() ? "Plug-in manager (AUv3)..." : "Plug-in manager...", [this] { showPluginManager(); });

    if (! Touch::isMobile())   // iOS apps do not quit themselves
    {
        m.addSeparator();
        m.addItem ("Quit", [] { JUCEApplication::getInstance()->systemRequestedQuit(); });
    }

    m.showMenuAsync (PopupMenu::Options().withTargetComponent (target));
}

void MainComponent::showAudioSettings()
{
    auto* selector = new AudioDeviceSelectorComponent (core.deviceManager, 0, 0, 2, 2, false, false, true, false);
    selector->setSize (520, 420);

    if (Touch::isMobile())
    {
        // no dialog windows on phones: a full-screen window that saves the settings when closed
        struct SettingsWindow : public AppWindow
        {
            SettingsWindow (MainComponent& o) : AppWindow ("Audio settings", DocumentWindow::closeButton), owner (o) {}

            void closeButtonPressed() override
            {
                owner.core.saveAudioSettings();
                MessageManager::callAsync ([safe = SafePointer<MainComponent> (&owner)]
                {
                    if (safe != nullptr)
                        safe->audioSettingsWindow.reset();
                });
            }

            MainComponent& owner;
        };

        audioSettingsWindow = std::make_unique<SettingsWindow> (*this);
        audioSettingsWindow->setScrollableContent (selector, 420, 360);
        audioSettingsWindow->placeOnScreen (520, 420);
        audioSettingsWindow->setVisible (true);
        return;
    }

    DialogWindow::LaunchOptions options;
    options.content.setOwned (selector);
    options.dialogTitle = "Audio settings";
    options.dialogBackgroundColour = Theme::background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.componentToCentreAround = this;

    if (auto* dialog = options.launchAsync())
    {
        struct SaveOnClose : public ComponentListener
        {
            explicit SaveOnClose (AppCore& c) : core (c) {}
            void componentBeingDeleted (Component& comp) override
            {
                core.saveAudioSettings();
                comp.removeComponentListener (this);
                delete this;
            }
            AppCore& core;
        };

        dialog->addComponentListener (new SaveOnClose (core));
    }
}

void MainComponent::showPluginManager()
{
    if (pluginManagerWindow != nullptr)
    {
        pluginManagerWindow->toFront (true);
        return;
    }

    auto& pm = *core.plugins;
    pluginManagerWindow = std::make_unique<PluginManagerWindow> (pm.getFormatManager(), pm.getKnownPlugins(),
                                                                 pm.getDeadMansPedalFile(), &pm.getSettings());
    pluginManagerWindow->onClose = [this] { pluginManagerWindow.reset(); };
}
