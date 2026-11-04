#pragma once

#include "Session.h"
#include "UI/PluginWindows.h"
#include "UI/SampleEditor.h"

/** Owns the long-lived application objects, in the right construction/destruction order. */
class AppCore
{
public:
    AppCore()
    {
        PropertiesFile::Options options;
        options.applicationName = "myDAW";
        options.filenameSuffix = ".settings";
        options.folderName = "myDAW";
        options.osxLibrarySubFolder = "Application Support";
        properties.setStorageParameters (options);

        plugins = std::make_unique<PluginManager> (*properties.getUserSettings());
        session = std::make_unique<Session> (engine, *plugins);
        session->onPluginWillBeDeleted = [this] (AudioPluginInstance* p) { pluginWindows.close (p); };
        sampleEditors = std::make_unique<SampleEditorManager> (*session, pluginWindows);
    }

    ~AppCore()
    {
        stopAudio();

        if (sampleEditors != nullptr)
            sampleEditors->closeAll();

        sampleEditors.reset();
        pluginWindows.closeAll();
        session.reset();
        plugins.reset();
    }

    void startAudio()
    {
        auto saved = properties.getUserSettings()->getXmlValue ("audioDevice");
        const auto error = deviceManager.initialise (0, 2, saved.get(), true);

        if (error.isNotEmpty())
            Logger::writeToLog ("Audio device error: " + error);

        deviceManager.addAudioCallback (&engine);
    }

    void stopAudio()
    {
        deviceManager.removeAudioCallback (&engine);
        deviceManager.closeAudioDevice();
    }

    void saveAudioSettings()
    {
        if (auto xml = deviceManager.createStateXml())
            properties.getUserSettings()->setValue ("audioDevice", xml.get());

        properties.saveIfNeeded();
    }

    ApplicationProperties properties;
    AudioDeviceManager deviceManager;
    AudioEngine engine;
    std::unique_ptr<PluginManager> plugins;
    PluginWindowManager pluginWindows;
    std::unique_ptr<Session> session;
    std::unique_ptr<SampleEditorManager> sampleEditors;
};
