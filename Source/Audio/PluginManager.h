#pragma once

#include "Common.h"

/** Knows which plug-in formats are available, keeps the list of scanned plug-ins
    (persisted in the app settings) and creates plug-in instances. */
class PluginManager : private ChangeListener
{
public:
    explicit PluginManager (PropertiesFile& settings);
    ~PluginManager() override;

    AudioPluginFormatManager& getFormatManager() noexcept { return formatManager; }
    KnownPluginList& getKnownPlugins() noexcept           { return knownPlugins; }
    PropertiesFile& getSettings() noexcept                { return settings; }
    File getDeadMansPedalFile() const;

    std::unique_ptr<AudioPluginInstance> createInstance (const PluginDescription&, double sampleRate,
                                                         int blockSize, String& error);

    /** Fills a menu with the known instruments or effects. Item IDs start at `firstId`;
        the returned list maps (itemId - firstId) to a description. */
    std::vector<PluginDescription> fillMenu (PopupMenu&, bool instruments, int firstId) const;

    /** Scans a single plug-in file (e.g. a .vst3 bundle) and adds what it finds to the list. */
    StringArray scanFile (const File&);

private:
    void changeListenerCallback (ChangeBroadcaster*) override;

    PropertiesFile& settings;
    AudioPluginFormatManager formatManager;
    KnownPluginList knownPlugins;
};
