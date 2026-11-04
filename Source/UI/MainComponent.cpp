#include "UI/MainComponent.h"
#include "UI/ChannelSettings.h"

MainComponent::MainComponent (AppCore& c)
    : core (c), session (*c.session), transport (session), patternList (session),
      songEditor (session), patternEditor (session, c.pluginWindows, *c.sampleEditors)
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
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    transport.setBounds (r.removeFromTop (48));
    patternList.setBounds (r.removeFromLeft (230));

    Component* comps[] = { &songEditor, &splitter, &patternEditor };
    layout.layOutComponents (comps, 3, r.getX(), r.getY(), r.getWidth(), r.getHeight(), true, true);
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

void MainComponent::newProject()
{
    askToSaveChanges ([this] { session.newProject(); });
}

void MainComponent::openProject()
{
    askToSaveChanges ([this]
    {
        chooser = std::make_unique<FileChooser> ("Open project", getDefaultDirectory ("projectDir"),
                                                 String ("*") + Session::projectExtension);
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [this] (const FileChooser& fc)
        {
            if (fc.getResult() != File())
                openFile (fc.getResult());
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

    chooser = std::make_unique<FileChooser> ("Save project", initial, String ("*") + Session::projectExtension);
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles
                              | FileBrowserComponent::warnAboutOverwriting,
                          [this, done] (const FileChooser& fc)
    {
        auto file = fc.getResult();

        if (file == File())
        {
            if (done) done (false);
            return;
        }

        file = file.withFileExtension (Session::projectExtension);
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
    chooser = std::make_unique<FileChooser> ("Export song as WAV", getDefaultDirectory ("exportDir").getChildFile (name + ".wav"), "*.wav");
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles
                              | FileBrowserComponent::warnAboutOverwriting,
                          [this] (const FileChooser& fc)
    {
        auto file = fc.getResult();

        if (file == File())
            return;

        file = file.withFileExtension (".wav");
        core.properties.getUserSettings()->setValue ("exportDir", file.getParentDirectory().getFullPathName());

        MouseCursor::showWaitCursor();
        const auto result = session.exportWav (file);
        MouseCursor::hideWaitCursor();

        if (result.failed())
            showMessage ("Export WAV", result.getErrorMessage());
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

    PopupMenu m;
    m.addItem ("New project          Ctrl+N", [this] { newProject(); });
    m.addItem ("Open...                  Ctrl+O", [this] { openProject(); });
    m.addSubMenu ("Open recent", recentMenu, recent.getNumFiles() > 0);
    m.addSeparator();
    m.addItem ("Save                       Ctrl+S", [this] { saveProject(); });
    m.addItem ("Save as...", [this] { saveProjectAs(); });
    m.addSeparator();
    m.addItem ("Export song as WAV...   Ctrl+E", [this] { exportWav(); });
    m.addSeparator();
    m.addItem ("Audio settings...", [this] { showAudioSettings(); });
    m.addItem ("Plug-in manager...", [this] { showPluginManager(); });
    m.addSeparator();
    m.addItem ("Quit", [] { JUCEApplication::getInstance()->systemRequestedQuit(); });
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (target));
}

void MainComponent::showAudioSettings()
{
    auto* selector = new AudioDeviceSelectorComponent (core.deviceManager, 0, 0, 2, 2, false, false, true, false);
    selector->setSize (520, 420);

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
