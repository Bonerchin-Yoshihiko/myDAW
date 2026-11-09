#include "UI/MainComponent.h"
#include "UI/Touch.h"
#include "UI/WindowPlacement.h"

#include <iostream>

class MainWindow : public AppWindow
{
public:
    explicit MainWindow (AppCore& c)
        : AppWindow ("myDAW", Touch::isMobile() ? 0 : DocumentWindow::allButtons), core (c)
    {
        content = new MainComponent (core);
        setContentOwned (content, ! Touch::isMobile());

        if (Touch::isMobile())
        {
            placeOnScreen (0, 0);   // full screen
        }
        else
        {
            setResizable (true, false);
            setResizeLimits (860, 560, 10000, 10000);

            // Fit the window (title bar included) on the screen: on a small or scaled display
            // (e.g. 150 % on a 1920x1080 laptop) the preferred size would not fit.
            if (! WindowPlacement::restoreState (*this, core.properties.getUserSettings()->getValue ("mainWindowBounds")))
                WindowPlacement::fitOnScreen (*this, 1360, 860);
        }

        content->onTitleChanged = [this] { setName (content->getWindowTitle()); };
        setName (content->getWindowTitle());
        setVisible (true);
        WindowPlacement::keepOnScreenAfterShowing (*this);
        content->grabKeyboardFocus();
    }

    ~MainWindow() override
    {
        if (rememberBounds)
            core.properties.getUserSettings()->setValue ("mainWindowBounds", WindowPlacement::getStateString (*this));
    }

    void closeButtonPressed() override
    {
        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    AppCore& core;
    MainComponent* content = nullptr;
    bool rememberBounds = ! Touch::isMobile();
};

//==============================================================================
class MyDAWApplication : public JUCEApplication
{
public:
    const String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override    { return true; }

    void initialise (const String&) override
    {
        LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const auto args = getCommandLineParameterArray();

        // Command-line rendering:  myDAW --render song.mydaw out.wav [--samplerate 48000]
        if (const auto index = args.indexOf ("--render"); index >= 0)
        {
            renderFromCommandLine (args, index);
            return;
        }

        core = std::make_unique<AppCore>();
        core->startAudio();
        mainWindow = std::make_unique<MainWindow> (*core);

        for (auto& a : args)
        {
            if (File::isAbsolutePath (a) && File (a).hasFileExtension (Session::projectExtension) && File (a).existsAsFile())
            {
                mainWindow->content->openFile (File (a));
                break;
            }
        }

        // phones: carry on where the user left off (iOS may have ended the app in the background)
        if (Touch::isMobile())
            mainWindow->content->reopenLastProject();

        // Developer helper: lay the window out at a given size, e.g. "--window-size 750x369" for
        // the area an iPhone 15 leaves for the app in landscape (to check the phone layout).
        if (const auto index = args.indexOf ("--window-size"); index >= 0 && index + 1 < args.size())
        {
            const auto w = args[index + 1].upToFirstOccurrenceOf ("x", false, true).getIntValue();
            const auto h = args[index + 1].fromFirstOccurrenceOf ("x", false, true).getIntValue();

            if (w > 0 && h > 0)
            {
                mainWindow->rememberBounds = false;
                mainWindow->setResizeLimits (w, h, 10000, 10000);
                mainWindow->setSize (w, h);
            }
        }

        // Developer helper: save a picture of the window and quit.
        if (const auto index = args.indexOf ("--screenshot"); index >= 0 && index + 1 < args.size())
        {
            const auto target = File::getCurrentWorkingDirectory().getChildFile (args[index + 1]);
            Timer::callAfterDelay (2000, [this, target]
            {
                if (mainWindow != nullptr)
                {
                    auto image = mainWindow->content->createComponentSnapshot (mainWindow->content->getLocalBounds(), true, 1.0f);
                    target.deleteFile();
                    FileOutputStream out (target);
                    PNGImageFormat().writeImageToStream (image, out);
                }

                quit();
            });
        }
    }

    void shutdown() override
    {
        mainWindow = nullptr;

        if (core != nullptr)
            core->saveAudioSettings();

        core = nullptr;
        LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            mainWindow->content->askToSaveChanges ([] { JUCEApplication::quit(); });
        else
            quit();
    }

    void anotherInstanceStarted (const String&) override {}

    // iOS: the app may be ended while it is in the background, without being asked to quit
    void suspended() override
    {
        if (core != nullptr)
            core->saveAudioSettings();

        if (mainWindow != nullptr)
            mainWindow->content->autoSave();
    }

private:
    void renderFromCommandLine (const StringArray& args, int index)
    {
        auto fail = [this] (const String& message)
        {
            std::cerr << message << std::endl;
            setApplicationReturnValue (1);
            quit();
        };

        if (index + 2 >= args.size())
            return fail ("Usage: myDAW --render <project.mydaw> <output.wav> [--samplerate 48000]");

        const auto cwd = File::getCurrentWorkingDirectory();
        const auto projectFile = cwd.getChildFile (args[index + 1]);
        const auto outputFile = cwd.getChildFile (args[index + 2]);
        const auto srIndex = args.indexOf ("--samplerate");
        const auto sampleRate = srIndex >= 0 && srIndex + 1 < args.size() ? args[srIndex + 1].getDoubleValue() : 44100.0;

        AppCore renderCore;
        renderCore.engine.prepare (sampleRate > 0 ? sampleRate : 44100.0, 512);

        StringArray warnings;
        auto result = renderCore.session->loadProject (projectFile, warnings);

        for (auto& w : warnings)
            std::cerr << "warning: " << w << std::endl;

        if (result.failed())
            return fail (result.getErrorMessage());

        result = renderCore.session->exportWav (outputFile, [] (double) {});

        if (result.failed())
            return fail (result.getErrorMessage());

        std::cout << "Rendered " << outputFile.getFullPathName() << std::endl;
        setApplicationReturnValue (0);
        quit();
    }

    DawLookAndFeel lookAndFeel;
    std::unique_ptr<AppCore> core;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (MyDAWApplication)
