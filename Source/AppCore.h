#pragma once

#include "Session.h"
#include "UI/PluginWindows.h"
#include "UI/SampleEditor.h"
#include "UI/Sampler.h"

/** Owns the long-lived application objects, in the right construction/destruction order. */
class AppCore
{
public:
    AppCore()
    {
        PropertiesFile::Options options;
        options.applicationName = "Chanco";
        options.filenameSuffix = ".settings";
        options.folderName = "Chanco";
        options.osxLibrarySubFolder = "Application Support";
        takeOverOldSettings (options);
        properties.setStorageParameters (options);

        plugins = std::make_unique<PluginManager> (*properties.getUserSettings());
        session = std::make_unique<Session> (engine, *plugins);
        session->onPluginWillBeDeleted = [this] (AudioPluginInstance* p) { pluginWindows.close (p); };
        sampleEditors = std::make_unique<SampleEditorManager> (*session, pluginWindows);
        sampler = std::make_unique<SamplerManager> (*session);
        sampleEditors->setSampler (sampler.get());
    }

    ~AppCore()
    {
        stopAudio();
        sampler.reset();   // cancels a running AI separation

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

    /** The app was called "myDAW" before: start from its settings (audio device, recent
        projects, scanned plug-ins) the first time. */
    static void takeOverOldSettings (const PropertiesFile::Options& options)
    {
        auto oldOptions = options;
        oldOptions.applicationName = "myDAW";
        oldOptions.folderName = "myDAW";

        const auto file = options.getDefaultFile();
        const auto oldFile = oldOptions.getDefaultFile();

        if (! file.existsAsFile() && oldFile.existsAsFile() && file.getParentDirectory().createDirectory())
            oldFile.copyFileTo (file);
    }

    ApplicationProperties properties;
    AudioDeviceManager deviceManager;
    AudioEngine engine;
    std::unique_ptr<PluginManager> plugins;
    PluginWindowManager pluginWindows;
    std::unique_ptr<Session> session;
    std::unique_ptr<SampleEditorManager> sampleEditors;
    std::unique_ptr<SamplerManager> sampler;
};
