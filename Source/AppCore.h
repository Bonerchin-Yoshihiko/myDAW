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

        // The app used to be called "myDAW": carry its settings over (scanned plug-ins, audio
        // device, window position, recent projects) the first time Chanco starts.
        {
            auto legacy = options;
            legacy.applicationName = "myDAW";
            legacy.folderName = "myDAW";
            const auto newFile = options.getDefaultFile();
            const auto oldFile = legacy.getDefaultFile();

            if (! newFile.existsAsFile() && oldFile.existsAsFile() && newFile.getParentDirectory().createDirectory())
                oldFile.copyFileTo (newFile);
        }

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

    ApplicationProperties properties;
    AudioDeviceManager deviceManager;
    AudioEngine engine;
    std::unique_ptr<PluginManager> plugins;
    PluginWindowManager pluginWindows;
    std::unique_ptr<Session> session;
    std::unique_ptr<SampleEditorManager> sampleEditors;
    std::unique_ptr<SamplerManager> sampler;
};
