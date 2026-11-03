#include "Audio/PluginManager.h"

PluginManager::PluginManager (PropertiesFile& s) : settings (s)
{
    addDefaultFormatsToManager (formatManager);

    if (auto xml = settings.getXmlValue ("knownPlugins"))
        knownPlugins.recreateFromXml (*xml);

    knownPlugins.addChangeListener (this);
}

PluginManager::~PluginManager()
{
    knownPlugins.removeChangeListener (this);
}

File PluginManager::getDeadMansPedalFile() const
{
    return settings.getFile().getSiblingFile ("RecentlyCrashedPluginsList");
}

void PluginManager::changeListenerCallback (ChangeBroadcaster*)
{
    if (auto xml = knownPlugins.createXml())
    {
        settings.setValue ("knownPlugins", xml.get());
        settings.saveIfNeeded();
    }
}

std::unique_ptr<AudioPluginInstance> PluginManager::createInstance (const PluginDescription& desc, double sampleRate,
                                                                    int blockSize, String& error)
{
    auto instance = formatManager.createPluginInstance (desc, sampleRate, blockSize, error);

    if (instance == nullptr && error.isEmpty())
        error = "Could not load the plug-in \"" + desc.name + "\"";

    return instance;
}

std::vector<PluginDescription> PluginManager::fillMenu (PopupMenu& menu, bool instruments, int firstId) const
{
    std::vector<PluginDescription> result;

    for (auto& d : knownPlugins.getTypes())
        if (d.isInstrument == instruments)
            result.push_back (d);

    std::sort (result.begin(), result.end(), [] (const PluginDescription& a, const PluginDescription& b)
    {
        if (a.manufacturerName != b.manufacturerName)
            return a.manufacturerName.compareIgnoreCase (b.manufacturerName) < 0;

        return a.name.compareIgnoreCase (b.name) < 0;
    });

    String currentMaker;
    PopupMenu sub;

    auto flush = [&]
    {
        if (sub.getNumItems() > 0)
            menu.addSubMenu (currentMaker.isNotEmpty() ? currentMaker : String ("(Unknown)"), sub);

        sub = PopupMenu();
    };

    for (size_t i = 0; i < result.size(); ++i)
    {
        auto& d = result[i];

        if (d.manufacturerName != currentMaker)
        {
            flush();
            currentMaker = d.manufacturerName;
        }

        sub.addItem (firstId + (int) i, d.name + "  (" + d.pluginFormatName + ")");
    }

    flush();

    if (result.empty())
        menu.addItem (-1, instruments ? "No instrument plug-ins found - open the Plug-in Manager and scan"
                                      : "No effect plug-ins found - open the Plug-in Manager and scan",
                      false);

    return result;
}

StringArray PluginManager::scanFile (const File& file)
{
    StringArray found;

    for (auto* format : formatManager.getFormats())
    {
        if (! format->fileMightContainThisPluginType (file.getFullPathName()))
            continue;

        OwnedArray<PluginDescription> types;
        knownPlugins.scanAndAddFile (file.getFullPathName(), true, types, *format);

        for (auto* t : types)
            found.add (t->name);
    }

    return found;
}
