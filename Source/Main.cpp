#include "UI/MainComponent.h"

#include <iostream>

class MainWindow : public DocumentWindow
{
public:
    explicit MainWindow (AppCore& core)
        : DocumentWindow ("myDAW", Theme::background, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        content = new MainComponent (core);
        setContentOwned (content, true);
        setResizable (true, true);
        setResizeLimits (980, 620, 10000, 10000);
        centreWithSize (getWidth(), getHeight());

        content->onTitleChanged = [this] { setName (content->getWindowTitle()); };
        setName (content->getWindowTitle());
        setVisible (true);
        content->grabKeyboardFocus();
    }

    void closeButtonPressed() override
    {
        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    MainComponent* content = nullptr;
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
