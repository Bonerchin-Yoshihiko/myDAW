#include "Session.h"
#include "Audio/SampleEditing.h"

namespace
{
    File fileFromString (const String& path)
    {
        return File::isAbsolutePath (path) ? File (path) : File();
    }

    int nextChannelColourIndex (const Project& p)
    {
        return (int) p.channels.size() * 3 + 5;
    }
}

Session::Session (AudioEngine& e, PluginManager& pm) : engine (e), plugins (pm)
{
    formats.registerBasicFormats();
    newProject();
}

Session::~Session()
{
    engine.setSequence (nullptr, true);
    engine.setChannels ({});

    {
        const ScopedLock sl (engine.getLock());
        engine.getMasterEffects().slots.clear();
    }

    project.channels.clear();
}

String Session::getProjectName() const
{
    return projectFile == File() ? String ("Untitled") : projectFile.getFileNameWithoutExtension();
}

void Session::notify (int changes)
{
    listeners.call ([changes] (Listener& l) { l.sessionChanged (changes); });
}

void Session::markDirty()
{
    if (! dirty)
    {
        dirty = true;
        notify (dirtyChanged);
    }
}

//==============================================================================
void Session::pushChannelsToEngine()
{
    engine.setChannels (project.channels);
}

std::shared_ptr<Sequence> Session::buildSequence (PlayMode mode, bool forExport) const
{
    auto seq = std::make_shared<Sequence>();

    std::map<int, Channel*> lookup;

    for (auto& c : project.channels)
        lookup[c->id] = c.get();

    auto addPattern = [&] (const Pattern& p, double offset)
    {
        const auto stepLength = p.stepLengthInQuarters();
        const auto length = p.lengthInQuarters();

        for (auto& n : p.notes)
        {
            auto it = lookup.find (n.channelId);

            if (it == lookup.end())
                continue;

            const auto on = offset + n.step * stepLength;
            const auto off = offset + jmin (length, (n.step + n.length) * stepLength) - 1.0e-6;

            seq->events.push_back ({ on, it->second, n.pitch, n.velocity, true });
            seq->events.push_back ({ jmax (on, off), it->second, n.pitch, 0.0f, false });
        }
    };

    if (mode == PlayMode::pattern)
    {
        if (auto* p = getCurrentPattern())
        {
            addPattern (*p, 0.0);
            seq->length = p->lengthInQuarters();
            seq->beatsPerBar = p->beatsPerBar;
            seq->beatUnit = p->beatUnit;
        }

        seq->loop = true;
        seq->isSong = false;
    }
    else
    {
        for (auto& clip : project.clips)
        {
            if (isPositiveAndBelow (clip.track, (int) project.tracks.size()) && project.tracks[(size_t) clip.track].muted)
                continue;

            if (auto* p = project.findPattern (clip.patternId))
                addPattern (*p, clip.start);
        }

        const auto songLength = project.getSongLength();
        seq->length = forExport ? songLength : jmax (songLength, (double) project.songBeatsPerBar);
        seq->loop = forExport ? false : project.loopSong;
        seq->isSong = true;
        seq->beatsPerBar = project.songBeatsPerBar;
        seq->beatUnit = 4;
    }

    seq->sort();
    return seq;
}

void Session::rebuildSequence (bool resetPosition)
{
    engine.setSequence (buildSequence (playMode, false), resetPosition);
}

//==============================================================================
void Session::replaceProject (Project&& newProject, std::vector<std::unique_ptr<EffectChain::Slot>> masterSlots)
{
    engine.stop();

    if (onPluginWillBeDeleted)
        onPluginWillBeDeleted (nullptr);

    // 1. stop the audio thread from referencing the old channels
    engine.setSequence (nullptr, true);

    auto oldChannels = std::move (project.channels);
    project = std::move (newProject);
    pushChannelsToEngine();

    for (auto& s : masterSlots)
        engine.preparePlugin (*s->plugin);

    {
        const ScopedLock sl (engine.getLock());
        std::swap (engine.getMasterEffects().slots, masterSlots);
        engine.masterEffectsChanged();
    }

    masterSlots.clear();   // old master effects
    oldChannels.clear();   // old channels (the engine has released them already)

    engine.setBpm (project.bpm);
    engine.setMasterVolume (project.masterVolume);

    if (project.findPattern (currentPatternId) == nullptr)
        currentPatternId = project.patterns.empty() ? 0 : project.patterns.front()->id;

    rebuildSequence (true);
    dirty = false;
    notify (everything);
}

void Session::newProject()
{
    Project p;

    auto pattern = std::make_unique<Pattern>();
    pattern->id = p.createId();
    pattern->name = "Pattern 1";
    pattern->colour = Palette::get (0);
    p.patterns.push_back (std::move (pattern));

    for (int i = 0; i < 8; ++i)
        p.tracks.push_back ({ "Track " + String (i + 1), false });

    currentPatternId = p.patterns.front()->id;
    projectFile = File();
    replaceProject (std::move (p), {});
}

//==============================================================================
var Session::pluginToVar (const PluginDescription& desc, AudioPluginInstance* instance, const MemoryBlock& fallbackState) const
{
    auto* o = new DynamicObject();

    // Prefer the description the plug-in was created from: for VST3 an instance reports the
    // binary inside the bundle, which the VST3 loader does not accept as a location.
    const auto d = (desc.fileOrIdentifier.isNotEmpty() || instance == nullptr) ? desc : instance->getPluginDescription();

    if (auto xml = d.createXml())
        o->setProperty ("description", xml->toString (XmlElement::TextFormat().singleLine().withoutHeader()));

    MemoryBlock state;

    if (instance != nullptr)
        instance->getStateInformation (state);
    else
        state = fallbackState;

    o->setProperty ("state", state.toBase64Encoding());
    return var (o);
}

std::unique_ptr<AudioPluginInstance> Session::pluginFromVar (const var& v, bool isInstrument, PluginDescription& desc,
                                                             MemoryBlock& state, String& error)
{
    const auto xml = parseXML (v.getProperty ("description", "").toString());

    if (xml == nullptr || ! desc.loadFromXml (*xml))
    {
        error = "Invalid plug-in description in file";
        return {};
    }

    state.reset();
    state.fromBase64Encoding (v.getProperty ("state", "").toString());

    auto instance = plugins.createInstance (desc, engine.getSampleRate(), engine.getBlockSize(), error);

    if (instance != nullptr)
    {
        PluginHelpers::configureStereo (*instance, isInstrument);

        if (state.getSize() > 0)
            instance->setStateInformation (state.getData(), (int) state.getSize());
    }

    return instance;
}

var Session::effectsToVar (const EffectChain& chain) const
{
    Array<var> list;

    for (auto& s : chain.slots)
    {
        auto v = pluginToVar (s->description, s->plugin.get(), {});
        v.getDynamicObject()->setProperty ("bypassed", s->bypassed.load());
        list.add (v);
    }

    return list;
}

std::vector<std::unique_ptr<EffectChain::Slot>> Session::effectsFromVar (const var& v, StringArray& warnings)
{
    std::vector<std::unique_ptr<EffectChain::Slot>> result;

    if (auto* list = v.getArray())
    {
        for (auto& item : *list)
        {
            PluginDescription desc;
            MemoryBlock state;
            String error;

            if (auto instance = pluginFromVar (item, false, desc, state, error))
            {
                auto slot = std::make_unique<EffectChain::Slot>();
                slot->plugin = std::move (instance);
                slot->description = desc;
                slot->bypassed = (bool) item.getProperty ("bypassed", false);
                result.push_back (std::move (slot));
            }
            else
            {
                warnings.add ("Effect \"" + desc.name + "\" could not be loaded and was removed: " + error);
            }
        }
    }

    return result;
}

var Session::channelToVar (const Channel& c, const File& baseDir) const
{
    auto* o = new DynamicObject();
    o->setProperty ("id", c.id);
    o->setProperty ("name", c.name);
    o->setProperty ("colour", c.colour.toString());
    o->setProperty ("type", c.isSample() ? "sample" : "plugin");
    o->setProperty ("volume", (double) c.volume.load());
    o->setProperty ("pan", (double) c.pan.load());
    o->setProperty ("muted", c.muted.load());
    o->setProperty ("oneShot", c.oneShot.load());
    o->setProperty ("rootNote", c.rootNote.load());

    if (c.isSample())
    {
        o->setProperty ("file", c.sampleFile.getFullPathName());

        if (baseDir.isDirectory() && c.sampleFile != File())
            o->setProperty ("relativeFile", c.sampleFile.getRelativePathFrom (baseDir).replaceCharacter ('\\', '/'));

        if (c.originalSampleFile != File() && c.originalSampleFile != c.sampleFile)
            o->setProperty ("originalFile", c.originalSampleFile.getFullPathName());
    }
    else
    {
        o->setProperty ("plugin", pluginToVar (c.pluginDescription, c.plugin.get(), c.missingPluginState));
    }

    o->setProperty ("effects", effectsToVar (c.effects));
    return var (o);
}

std::shared_ptr<Channel> Session::channelFromVar (const var& v, const File& baseDir, int newId, StringArray& warnings)
{
    const auto type = v.getProperty ("type", "sample").toString() == "plugin" ? Channel::Type::plugin
                                                                               : Channel::Type::sample;
    auto c = std::make_shared<Channel> (newId, type);
    c->name = v.getProperty ("name", "Channel").toString();
    c->colour = Colour::fromString (v.getProperty ("colour", Palette::get (newId).toString()).toString());
    c->volume = (float) (double) v.getProperty ("volume", 0.8);
    c->pan = (float) (double) v.getProperty ("pan", 0.0);
    c->muted = (bool) v.getProperty ("muted", false);
    c->oneShot = (bool) v.getProperty ("oneShot", true);
    c->rootNote = jlimit (0, 127, (int) v.getProperty ("rootNote", 60));

    if (c->isSample())
    {
        const auto absolute = fileFromString (v.getProperty ("file", "").toString());
        const auto relative = v.getProperty ("relativeFile", "").toString();

        Array<File> candidates;

        if (relative.isNotEmpty() && baseDir.isDirectory())
            candidates.add (baseDir.getChildFile (relative));

        candidates.add (absolute);
        c->sampleFile = absolute;

        for (auto& f : candidates)
        {
            if (f.existsAsFile())
            {
                String error;

                if (auto data = SampleData::load (formats, f, error))
                {
                    c->sample = data;
                    c->sampleFile = f;
                    break;
                }

                warnings.add (error);
            }
        }

        if (c->sample == nullptr)
            warnings.add ("Sample not found for \"" + c->name + "\": " + absolute.getFullPathName());

        const auto original = fileFromString (v.getProperty ("originalFile", "").toString());
        c->originalSampleFile = original != File() ? original : c->sampleFile;
    }
    else
    {
        String error;
        MemoryBlock state;
        auto pluginVar = v.getProperty ("plugin", var());
        c->plugin = pluginFromVar (pluginVar, true, c->pluginDescription, state, error);

        if (c->plugin == nullptr)
        {
            c->missingPluginState = state;
            warnings.add ("Plug-in for \"" + c->name + "\" could not be loaded: " + error);
        }
    }

    c->effects.slots = effectsFromVar (v.getProperty ("effects", var()), warnings);
    engine.prepareNewChannel (*c);
    return c;
}

//==============================================================================
Result Session::saveProject (const File& file)
{
    {
        const auto samplesFolder = file.getParentDirectory().getChildFile (file.getFileNameWithoutExtension() + " Samples");
        const auto written = writeEditedSamples (samplesFolder);

        if (written.failed())
            return written;
    }

    auto* o = new DynamicObject();
    var root (o);
    const auto baseDir = file.getParentDirectory();

    o->setProperty ("format", "myDAW");
    o->setProperty ("version", 1);
    o->setProperty ("bpm", project.bpm);
    o->setProperty ("songBeatsPerBar", project.songBeatsPerBar);
    o->setProperty ("masterVolume", (double) project.masterVolume);
    o->setProperty ("loopSong", project.loopSong);
    o->setProperty ("currentPattern", currentPatternId);

    Array<var> channels, patterns, tracks, clips;

    for (auto& c : project.channels)
        channels.add (channelToVar (*c, baseDir));

    for (auto& p : project.patterns)
        patterns.add (p->toVar());

    for (auto& t : project.tracks)
    {
        auto* to = new DynamicObject();
        to->setProperty ("name", t.name);
        to->setProperty ("muted", t.muted);
        tracks.add (var (to));
    }

    for (auto& c : project.clips)
    {
        auto* co = new DynamicObject();
        co->setProperty ("id", c.id);
        co->setProperty ("pattern", c.patternId);
        co->setProperty ("track", c.track);
        co->setProperty ("start", c.start);
        clips.add (var (co));
    }

    o->setProperty ("channels", channels);
    o->setProperty ("patterns", patterns);
    o->setProperty ("tracks", tracks);
    o->setProperty ("clips", clips);
    o->setProperty ("masterEffects", effectsToVar (engine.getMasterEffects()));

    TemporaryFile temp (file);

    if (! temp.getFile().replaceWithText (JSON::toString (root, false)) || ! temp.overwriteTargetFileWithTemporary())
        return Result::fail ("Could not write " + file.getFullPathName());

    projectFile = file;
    dirty = false;
    notify (dirtyChanged);
    return Result::ok();
}

Result Session::loadProject (const File& file, StringArray& warnings)
{
    const auto root = JSON::parse (file.loadFileAsString());

    if (! root.isObject() || root.getProperty ("format", "").toString() != "myDAW")
        return Result::fail (file.getFileName() + " is not a myDAW project file.");

    const auto baseDir = file.getParentDirectory();
    Project p;
    p.bpm = jlimit (20.0, 400.0, (double) root.getProperty ("bpm", 120.0));
    p.songBeatsPerBar = jlimit (1, 32, (int) root.getProperty ("songBeatsPerBar", 4));
    p.masterVolume = (float) (double) root.getProperty ("masterVolume", 0.8);
    p.loopSong = (bool) root.getProperty ("loopSong", true);

    int maxId = 0;

    if (auto* list = root.getProperty ("channels", var()).getArray())
    {
        for (auto& v : *list)
        {
            const auto id = (int) v.getProperty ("id", 0);

            if (id <= 0 || p.findChannel (id) != nullptr)
                continue;

            p.channels.push_back (channelFromVar (v, baseDir, id, warnings));
            maxId = jmax (maxId, id);
        }
    }

    if (auto* list = root.getProperty ("patterns", var()).getArray())
    {
        for (auto& v : *list)
        {
            auto pattern = std::make_unique<Pattern>();
            pattern->fromVar (v);

            if (pattern->id <= 0 || p.findPattern (pattern->id) != nullptr)
                continue;

            maxId = jmax (maxId, pattern->id);
            p.patterns.push_back (std::move (pattern));
        }
    }

    if (auto* list = root.getProperty ("tracks", var()).getArray())
        for (auto& v : *list)
            p.tracks.push_back ({ v.getProperty ("name", "Track").toString(), (bool) v.getProperty ("muted", false) });

    while (p.tracks.size() < 4)
        p.tracks.push_back ({ "Track " + String ((int) p.tracks.size() + 1), false });

    if (auto* list = root.getProperty ("clips", var()).getArray())
    {
        for (auto& v : *list)
        {
            Clip c;
            c.id = (int) v.getProperty ("id", 0);
            c.patternId = (int) v.getProperty ("pattern", 0);
            c.track = jlimit (0, (int) p.tracks.size() - 1, (int) v.getProperty ("track", 0));
            c.start = jmax (0.0, (double) v.getProperty ("start", 0.0));

            if (p.findPattern (c.patternId) != nullptr)
            {
                p.clips.push_back (c);
                maxId = jmax (maxId, c.id);
            }
        }
    }

    if (p.patterns.empty())
    {
        auto pattern = std::make_unique<Pattern>();
        pattern->id = ++maxId;
        pattern->name = "Pattern 1";
        pattern->colour = Palette::get (0);
        p.patterns.push_back (std::move (pattern));
    }

    p.nextId = maxId + 1;

    for (auto& c : p.clips)
        if (c.id <= 0)
            c.id = p.createId();

    auto masterSlots = effectsFromVar (root.getProperty ("masterEffects", var()), warnings);

    currentPatternId = (int) root.getProperty ("currentPattern", 0);
    projectFile = file;
    replaceProject (std::move (p), std::move (masterSlots));
    return Result::ok();
}

//==============================================================================
std::shared_ptr<Channel> Session::addSampleChannel (const File& file, String& error)
{
    auto data = SampleData::load (formats, file, error);

    if (data == nullptr)
        return {};

    auto c = std::make_shared<Channel> (project.createId(), Channel::Type::sample);
    c->name = file.getFileNameWithoutExtension();
    c->colour = Palette::get (nextChannelColourIndex (project));
    c->sampleFile = file;
    c->originalSampleFile = file;
    c->sample = data;
    engine.prepareNewChannel (*c);

    project.channels.push_back (c);
    pushChannelsToEngine();
    markDirty();
    notify (channelsChanged);
    return c;
}

std::shared_ptr<Channel> Session::addPluginChannel (const PluginDescription& desc, String& error)
{
    auto instance = plugins.createInstance (desc, engine.getSampleRate(), engine.getBlockSize(), error);

    if (instance == nullptr)
        return {};

    PluginHelpers::configureStereo (*instance, true);

    auto c = std::make_shared<Channel> (project.createId(), Channel::Type::plugin);
    c->name = desc.name;
    c->colour = Palette::get (nextChannelColourIndex (project));
    c->pluginDescription = desc;
    c->plugin = std::move (instance);
    c->oneShot = false;
    engine.prepareNewChannel (*c);

    project.channels.push_back (c);
    pushChannelsToEngine();
    markDirty();
    notify (channelsChanged);
    return c;
}

void Session::removeChannel (int channelId)
{
    const auto index = project.indexOfChannel (channelId);

    if (index < 0)
        return;

    auto keepAlive = project.channels[(size_t) index];

    if (onPluginWillBeDeleted)
    {
        if (keepAlive->plugin != nullptr)
            onPluginWillBeDeleted (keepAlive->plugin.get());

        for (auto& s : keepAlive->effects.slots)
            onPluginWillBeDeleted (s->plugin.get());
    }

    project.channels.erase (project.channels.begin() + index);

    for (auto& p : project.patterns)
        p->notes.erase (std::remove_if (p->notes.begin(), p->notes.end(),
                                        [channelId] (const Note& n) { return n.channelId == channelId; }),
                        p->notes.end());

    rebuildSequence (false);   // the sequence no longer points at the channel
    pushChannelsToEngine();    // neither does the engine
    keepAlive.reset();         // destroyed here, on the message thread

    markDirty();
    notify (channelsChanged | patternContentChanged | arrangementChanged);
}

void Session::moveChannel (int channelId, int delta)
{
    const auto index = project.indexOfChannel (channelId);
    const auto target = index + delta;

    if (index < 0 || ! isPositiveAndBelow (target, (int) project.channels.size()))
        return;

    std::swap (project.channels[(size_t) index], project.channels[(size_t) target]);
    pushChannelsToEngine();
    markDirty();
    notify (channelsChanged);
}

bool Session::replaceSample (Channel& c, const File& file, String& error)
{
    auto data = SampleData::load (formats, file, error);

    if (data == nullptr)
        return false;

    std::shared_ptr<const SampleData> old = data;

    {
        const ScopedLock sl (engine.getLock());
        c.releaseNotes (true);
        std::swap (c.sample, old);
        c.sampleFile = file;
    }

    old.reset();
    c.originalSampleFile = file;
    c.sampleModified = false;
    c.undoHistory.clear();
    c.redoHistory.clear();
    markDirty();
    notify (channelsChanged | sampleChanged);
    return true;
}

//==============================================================================
void Session::swapSample (Channel& c, std::shared_ptr<const SampleData> data)
{
    {
        const ScopedLock sl (engine.getLock());
        std::swap (c.sample, data);   // voices stop by themselves if the new sample is shorter
    }

    data.reset(); // previous version is either in the history or released here, off the audio thread
}

void Session::commitSampleEdit (Channel& c, std::shared_ptr<const SampleData> newData)
{
    if (newData == nullptr || ! c.isSample())
        return;

    constexpr size_t maxUndoSteps = 30;

    if (c.sample != nullptr)
        c.undoHistory.push_back (c.sample);

    if (c.undoHistory.size() > maxUndoSteps)
        c.undoHistory.erase (c.undoHistory.begin());

    c.redoHistory.clear();
    swapSample (c, std::move (newData));
    c.sampleModified = true;
    markDirty();
    notify (sampleChanged);
}

bool Session::undoSampleEdit (Channel& c)
{
    if (c.undoHistory.empty())
        return false;

    auto previous = c.undoHistory.back();
    c.undoHistory.pop_back();

    if (c.sample != nullptr)
        c.redoHistory.push_back (c.sample);

    swapSample (c, std::move (previous));
    c.sampleModified = true;
    markDirty();
    notify (sampleChanged);
    return true;
}

bool Session::redoSampleEdit (Channel& c)
{
    if (c.redoHistory.empty())
        return false;

    auto next = c.redoHistory.back();
    c.redoHistory.pop_back();

    if (c.sample != nullptr)
        c.undoHistory.push_back (c.sample);

    swapSample (c, std::move (next));
    c.sampleModified = true;
    markDirty();
    notify (sampleChanged);
    return true;
}

bool Session::revertSampleToOriginal (Channel& c, String& error)
{
    const auto original = c.originalSampleFile != File() ? c.originalSampleFile : c.sampleFile;
    auto data = SampleData::load (formats, original, error);

    if (data == nullptr)
        return false;

    commitSampleEdit (c, data);   // undoable like any other edit
    c.sampleFile = original;
    c.sampleModified = false;
    notify (channelsChanged);
    return true;
}

File Session::getEditedSamplesFolder() const
{
    if (projectFile != File())
        return projectFile.getParentDirectory().getChildFile (projectFile.getFileNameWithoutExtension() + " Samples");

    return File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("myDAW").getChildFile ("Edited Samples");
}

Result Session::writeEditedSamples (const File& folder)
{
    for (auto& c : project.channels)
    {
        if (! c->isSample() || ! c->sampleModified || c->sample == nullptr)
            continue;

        // re-use the file we wrote last time for this channel, otherwise pick a fresh name
        auto target = c->sampleFile.isAChildOf (folder) && c->sampleFile != c->originalSampleFile
                          ? c->sampleFile
                          : folder.getNonexistentChildFile (File::createLegalFileName (c->name), ".wav", false);

        const auto result = SampleEditing::writeWav (*c->sample, target);

        if (result.failed())
            return result;

        c->sampleFile = target;
        c->sampleModified = false;
    }

    return Result::ok();
}

bool Session::addEffect (Channel* channel, const PluginDescription& desc, String& error)
{
    auto instance = plugins.createInstance (desc, engine.getSampleRate(), engine.getBlockSize(), error);

    if (instance == nullptr)
        return false;

    PluginHelpers::configureStereo (*instance, false);
    engine.preparePlugin (*instance);

    auto slot = std::make_unique<EffectChain::Slot>();
    slot->plugin = std::move (instance);
    slot->description = desc;

    {
        const ScopedLock sl (engine.getLock());

        if (channel != nullptr)
        {
            channel->effects.slots.push_back (std::move (slot));
            channel->updateBufferSize();
        }
        else
        {
            engine.getMasterEffects().slots.push_back (std::move (slot));
            engine.masterEffectsChanged();
        }
    }

    markDirty();
    notify (channelsChanged);
    return true;
}

void Session::removeEffect (Channel* channel, int index)
{
    auto& chain = channel != nullptr ? channel->effects : engine.getMasterEffects();

    if (! isPositiveAndBelow (index, (int) chain.slots.size()))
        return;

    if (onPluginWillBeDeleted)
        onPluginWillBeDeleted (chain.slots[(size_t) index]->plugin.get());

    std::unique_ptr<EffectChain::Slot> removed;

    {
        const ScopedLock sl (engine.getLock());
        removed = std::move (chain.slots[(size_t) index]);
        chain.slots.erase (chain.slots.begin() + index);

        if (channel != nullptr)
            channel->updateBufferSize();
        else
            engine.masterEffectsChanged();
    }

    removed.reset();
    markDirty();
    notify (channelsChanged);
}

void Session::channelSettingsChanged()
{
    markDirty();
    notify (channelsChanged);
}

void Session::previewNote (int channelId, int pitch, bool on)
{
    engine.previewNote (channelId, pitch, 0.8f, on);
}

//==============================================================================
void Session::selectPattern (int patternId)
{
    if (project.findPattern (patternId) == nullptr || patternId == currentPatternId)
        return;

    currentPatternId = patternId;

    if (playMode == PlayMode::pattern)
        rebuildSequence (false);

    notify (selectionChanged);
}

String Session::makeUniquePatternName (const String& base) const
{
    auto exists = [this] (const String& n)
    {
        return std::any_of (project.patterns.begin(), project.patterns.end(),
                            [&n] (const std::unique_ptr<Pattern>& p) { return p->name == n; });
    };

    if (! exists (base))
        return base;

    auto stem = base.trimCharactersAtEnd ("0123456789").trimEnd();

    if (stem.isEmpty())
        stem = "Pattern";

    for (int i = 2;; ++i)
        if (! exists (stem + " " + String (i)))
            return stem + " " + String (i);
}

Pattern* Session::createPattern()
{
    auto p = std::make_unique<Pattern>();
    p->id = project.createId();
    p->name = makeUniquePatternName ("Pattern " + String ((int) project.patterns.size() + 1));
    p->colour = Palette::get ((int) project.patterns.size());

    if (auto* current = getCurrentPattern())
    {
        p->bars = current->bars;
        p->beatsPerBar = current->beatsPerBar;
        p->beatUnit = current->beatUnit;
        p->stepsPerBeat = current->stepsPerBeat;
    }

    auto* raw = p.get();
    project.patterns.push_back (std::move (p));
    markDirty();
    notify (patternListChanged);
    selectPattern (raw->id);
    return raw;
}

Pattern* Session::duplicatePattern (int patternId)
{
    auto* source = project.findPattern (patternId);

    if (source == nullptr)
        return nullptr;

    auto p = std::make_unique<Pattern> (*source);
    p->id = project.createId();
    p->name = makeUniquePatternName (source->name);

    auto* raw = p.get();
    auto it = std::find_if (project.patterns.begin(), project.patterns.end(),
                            [patternId] (const std::unique_ptr<Pattern>& x) { return x->id == patternId; });
    project.patterns.insert (it + 1, std::move (p));
    markDirty();
    notify (patternListChanged);
    selectPattern (raw->id);
    return raw;
}

void Session::deletePattern (int patternId)
{
    auto it = std::find_if (project.patterns.begin(), project.patterns.end(),
                            [patternId] (const std::unique_ptr<Pattern>& x) { return x->id == patternId; });

    if (it == project.patterns.end())
        return;

    const auto index = (int) std::distance (project.patterns.begin(), it);
    project.patterns.erase (it);

    project.clips.erase (std::remove_if (project.clips.begin(), project.clips.end(),
                                         [patternId] (const Clip& c) { return c.patternId == patternId; }),
                         project.clips.end());

    if (project.patterns.empty())
    {
        auto p = std::make_unique<Pattern>();
        p->id = project.createId();
        p->name = "Pattern 1";
        p->colour = Palette::get (0);
        project.patterns.push_back (std::move (p));
    }

    if (currentPatternId == patternId)
        currentPatternId = project.patterns[(size_t) jlimit (0, (int) project.patterns.size() - 1, index)]->id;

    rebuildSequence (false);
    markDirty();
    notify (patternListChanged | arrangementChanged | selectionChanged);
}

void Session::patternChanged (int patternId)
{
    if (auto* p = project.findPattern (patternId))
        p->trimNotesToLength();

    rebuildSequence (false);
    markDirty();
    notify (patternContentChanged);
}

Result Session::exportPattern (int patternId, const File& file)
{
    auto* p = project.findPattern (patternId);

    if (p == nullptr)
        return Result::fail ("No pattern selected.");

    if (const auto written = writeEditedSamples (getEditedSamplesFolder()); written.failed())
        return written;

    auto* o = new DynamicObject();
    var root (o);
    o->setProperty ("format", "myDAW-pattern");
    o->setProperty ("version", 1);
    o->setProperty ("pattern", p->toVar());

    Array<var> channels;
    std::set<int> used;

    for (auto& n : p->notes)
        used.insert (n.channelId);

    for (auto& c : project.channels)
        if (used.count (c->id) > 0)
            channels.add (channelToVar (*c, file.getParentDirectory()));

    o->setProperty ("channels", channels);

    if (! file.replaceWithText (JSON::toString (root, false)))
        return Result::fail ("Could not write " + file.getFullPathName());

    return Result::ok();
}

Result Session::importPattern (const File& file, StringArray& warnings)
{
    const auto root = JSON::parse (file.loadFileAsString());

    if (! root.isObject() || root.getProperty ("format", "").toString() != "myDAW-pattern")
        return Result::fail (file.getFileName() + " is not a myDAW pattern file.");

    std::map<int, int> channelMap;
    bool addedChannels = false;

    if (auto* list = root.getProperty ("channels", var()).getArray())
    {
        for (auto& v : *list)
        {
            const auto oldId = (int) v.getProperty ("id", 0);
            const auto isPlugin = v.getProperty ("type", "").toString() == "plugin";
            Channel* match = nullptr;

            for (auto& c : project.channels)
            {
                if (! isPlugin && c->isSample()
                     && c->sampleFile == fileFromString (v.getProperty ("file", "").toString()))
                {
                    match = c.get();
                }
                else if (isPlugin && c->isPlugin())
                {
                    PluginDescription d;

                    if (auto xml = parseXML (v.getProperty ("plugin", var()).getProperty ("description", "").toString()))
                        if (d.loadFromXml (*xml) && d.createIdentifierString() == c->pluginDescription.createIdentifierString())
                            match = c.get();
                }

                if (match != nullptr)
                    break;
            }

            if (match != nullptr)
            {
                channelMap[oldId] = match->id;
            }
            else
            {
                auto c = channelFromVar (v, file.getParentDirectory(), project.createId(), warnings);
                channelMap[oldId] = c->id;
                project.channels.push_back (c);
                addedChannels = true;
            }
        }
    }

    auto p = std::make_unique<Pattern>();
    p->fromVar (root.getProperty ("pattern", var()));
    p->id = project.createId();
    p->name = makeUniquePatternName (p->name.isNotEmpty() ? p->name : file.getFileNameWithoutExtension());

    std::vector<Note> remapped;

    for (auto n : p->notes)
    {
        auto it = channelMap.find (n.channelId);

        if (it != channelMap.end())
        {
            n.channelId = it->second;
            remapped.push_back (n);
        }
    }

    p->notes = std::move (remapped);
    const auto newId = p->id;
    project.patterns.push_back (std::move (p));

    if (addedChannels)
        pushChannelsToEngine();

    markDirty();
    notify (patternListChanged | channelsChanged);
    selectPattern (newId);
    return Result::ok();
}

//==============================================================================
int Session::addClip (int patternId, int track, double start)
{
    if (project.findPattern (patternId) == nullptr || ! isPositiveAndBelow (track, (int) project.tracks.size()))
        return 0;

    Clip c;
    c.id = project.createId();
    c.patternId = patternId;
    c.track = track;
    c.start = jmax (0.0, start);
    project.clips.push_back (c);
    arrangementEdited();
    return c.id;
}

void Session::removeClip (int clipId)
{
    project.clips.erase (std::remove_if (project.clips.begin(), project.clips.end(),
                                         [clipId] (const Clip& c) { return c.id == clipId; }),
                         project.clips.end());
    arrangementEdited();
}

void Session::addTrack()
{
    project.tracks.push_back ({ "Track " + String ((int) project.tracks.size() + 1), false });
    arrangementEdited();
}

void Session::arrangementEdited()
{
    if (playMode == PlayMode::song)
        rebuildSequence (false);

    markDirty();
    notify (arrangementChanged);
}

//==============================================================================
void Session::setPlayMode (PlayMode mode)
{
    if (mode == playMode)
        return;

    playMode = mode;
    rebuildSequence (true);
    notify (transportChanged);
}

void Session::play()
{
    engine.play();
    notify (transportChanged);
}

void Session::stop()
{
    engine.stop();
    notify (transportChanged);
}

void Session::togglePlay()
{
    if (engine.isPlaying())
        stop();
    else
        play();
}

void Session::seek (double beat)
{
    engine.seek (beat);
    notify (transportChanged);
}

void Session::setBpm (double newBpm)
{
    project.bpm = jlimit (20.0, 400.0, newBpm);
    engine.setBpm (project.bpm);
    markDirty();
    notify (transportChanged);
}

void Session::setLoopSong (bool shouldLoop)
{
    project.loopSong = shouldLoop;

    if (playMode == PlayMode::song)
        rebuildSequence (false);

    markDirty();
    notify (transportChanged);
}

void Session::setMasterVolume (float v)
{
    project.masterVolume = jlimit (0.0f, 1.5f, v);
    engine.setMasterVolume (project.masterVolume);
    markDirty();
}

Result Session::exportWav (const File& file, std::function<void (double)> progress)
{
    stop();
    auto result = engine.renderToFile (file, buildSequence (PlayMode::song, true), 2.0, std::move (progress));
    notify (transportChanged);
    return result;
}
