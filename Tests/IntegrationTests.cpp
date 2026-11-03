/*  Integration tests for the myDAW engine.

    Usage:  myDAWTests [path/to/InstrumentPlugin.vst3 path/to/EffectPlugin.vst3]

    Without arguments the plug-in hosting tests are skipped. The effect plug-in is
    expected to have a gain-like first parameter (e.g. JUCE's GainPluginDemo).
*/

#include "Session.h"
#include "Audio/Channel.h"

#include <iostream>

namespace
{
    int failures = 0, checks = 0;

    void check (bool condition, const String& what)
    {
        ++checks;

        if (condition)
        {
            std::cout << "  ok    " << what << std::endl;
        }
        else
        {
            ++failures;
            std::cout << "  FAIL  " << what << std::endl;
        }
    }

    File writeSine (const File& file, double frequency, double seconds, double sampleRate, int numChannels)
    {
        AudioBuffer<float> buffer (numChannels, (int) (seconds * sampleRate));

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto fade = jmin (1.0, i / 64.0, (buffer.getNumSamples() - i) / 64.0);
            const auto v = (float) (0.5 * fade * std::sin (MathConstants<double>::twoPi * frequency * i / sampleRate));

            for (int ch = 0; ch < numChannels; ++ch)
                buffer.setSample (ch, i, v);
        }

        file.deleteFile();
        std::unique_ptr<OutputStream> stream (file.createOutputStream());
        WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, AudioFormatWriterOptions{}.withSampleRate (sampleRate)
                                                                             .withNumChannels (numChannels)
                                                                             .withBitsPerSample (16));
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        return file;
    }

    struct Audio
    {
        AudioBuffer<float> buffer;
        double sampleRate = 0;

        float rms (double from, double to) const
        {
            const auto a = jlimit (0, buffer.getNumSamples(), (int) (from * sampleRate));
            const auto b = jlimit (0, buffer.getNumSamples(), (int) (to * sampleRate));
            return b > a ? buffer.getRMSLevel (0, a, b - a) : 0.0f;
        }

        double frequency (double from, double to) const
        {
            const auto a = (int) (from * sampleRate), b = (int) (to * sampleRate);
            int crossings = 0;

            for (int i = a + 1; i < b; ++i)
                if ((buffer.getSample (0, i - 1) < 0.0f) != (buffer.getSample (0, i) < 0.0f))
                    ++crossings;

            return crossings / (2.0 * (to - from));
        }

        double seconds() const { return buffer.getNumSamples() / sampleRate; }
    };

    Audio readWav (const File& file)
    {
        AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (file));
        Audio a;

        if (reader != nullptr)
        {
            a.sampleRate = reader->sampleRate;
            a.buffer.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
            reader->read (&a.buffer, 0, (int) reader->lengthInSamples, 0, true, true);
        }

        return a;
    }

    bool approx (double a, double b, double tolerance) { return std::abs (a - b) <= tolerance; }

    struct TestEnvironment
    {
        TestEnvironment (const File& dir)
            : settings (dir.getChildFile ("test.settings"), PropertiesFile::Options()),
              plugins (settings)
        {
            engine.prepare (44100.0, 512);
            session = std::make_unique<Session> (engine, plugins);
        }

        ~TestEnvironment() { session.reset(); }

        PropertiesFile settings;
        AudioEngine engine;
        PluginManager plugins;
        std::unique_ptr<Session> session;
    };

    //==============================================================================
    void testSampler (const File& dir)
    {
        std::cout << "Sampler / sequencer" << std::endl;
        TestEnvironment env (dir);
        auto& s = *env.session;

        // 48 kHz file into a 44.1 kHz engine: exercises the resampling
        String error;
        auto channel = s.addSampleChannel (writeSine (dir.getChildFile ("a440.wav"), 440.0, 0.3, 48000.0, 1), error);
        check (channel != nullptr && channel->isLoaded(), "WAV sample loads into a new channel");

        if (channel == nullptr)
            return;

        auto* p = s.getCurrentPattern();
        p->notes.push_back ({ channel->id, 0, 1, 60, 1.0f });   // root note -> 440 Hz at 0.0 s
        p->notes.push_back ({ channel->id, 8, 1, 72, 1.0f });   // octave up -> 880 Hz at 1.0 s
        s.patternChanged (p->id);
        s.addClip (p->id, 0, 0.0);

        check (approx (s.getProject().getSongLength(), 4.0, 1e-9), "song length is one 4/4 bar");

        const auto out = dir.getChildFile ("sampler.wav");
        check (s.exportWav (out).wasOk(), "song exports to WAV");

        const auto audio = readWav (out);
        check (approx (audio.seconds(), 4.0, 0.01), "export length = 2 s song + 2 s tail (got " + String (audio.seconds(), 3) + ")");
        check (approx (audio.frequency (0.05, 0.25), 440.0, 10.0), "first note plays at 440 Hz (got " + String (audio.frequency (0.05, 0.25), 1) + ")");
        // the octave-up note plays twice as fast, so it lasts ~0.14 s
        check (approx (audio.frequency (1.02, 1.12), 880.0, 15.0), "pitched note plays at 880 Hz (got " + String (audio.frequency (1.02, 1.12), 1) + ")");
        check (audio.rms (0.4, 0.95) < 1.0e-4, "silence between the notes");
        check (audio.rms (0.0, 0.02) > 0.01, "first note starts right at 0 s");

        // muting the channel silences it
        channel->muted = true;
        check (s.exportWav (out).wasOk() && readWav (out).rms (0.0, 2.0) < 1.0e-5, "muted channel is silent");
        channel->muted = false;

        // a file that is not audio is rejected cleanly
        auto bogus = dir.getChildFile ("bogus.wav");
        bogus.replaceWithText ("not audio");
        check (s.addSampleChannel (bogus, error) == nullptr && error.isNotEmpty(), "invalid audio file is rejected with an error");
    }

    void testFreeBarLengthsAndArrangement (const File& dir)
    {
        std::cout << "Free bar lengths / arrangement" << std::endl;
        TestEnvironment env (dir);
        auto& s = *env.session;

        String error;
        auto channel = s.addSampleChannel (writeSine (dir.getChildFile ("click.wav"), 1000.0, 0.02, 44100.0, 1), error);
        auto* p = s.getCurrentPattern();
        p->beatsPerBar = 7;
        p->beatUnit = 8;
        p->stepsPerBeat = 2;
        p->notes.push_back ({ channel->id, 0, 1, 60, 1.0f });
        s.patternChanged (p->id);

        check (p->totalSteps() == 14, "7/8 bar with 1/16 steps has 14 steps");
        check (approx (p->lengthInQuarters(), 3.5, 1e-9), "7/8 bar is 3.5 quarter notes long");

        s.addClip (p->id, 0, 0.0);
        s.addClip (p->id, 0, 3.5);                 // puzzle piece placed right after the first
        check (approx (s.getProject().getSongLength(), 8.0, 1e-9), "song length rounds up to whole song bars");

        const auto out = dir.getChildFile ("odd.wav");
        check (s.exportWav (out).wasOk(), "odd-meter song exports");
        const auto audio = readWav (out);
        // second clip starts at 3.5 quarters = 1.75 s at 120 BPM
        check (audio.rms (1.75, 1.77) > 0.05 && audio.rms (1.70, 1.749) < 1.0e-4, "second 7/8 clip starts exactly at 1.75 s");

        // track mute removes its clips from playback
        s.getProject().tracks[0].muted = true;
        s.arrangementEdited();
        check (s.exportWav (out).wasOk() && readWav (out).rms (0.0, 4.0) < 1.0e-5, "muted track is silent");
        s.getProject().tracks[0].muted = false;

        // changing the step resolution keeps note positions
        p->notes.push_back ({ channel->id, 4, 2, 60, 1.0f });
        p->changeStepsPerBeat (4);
        check (p->hasNoteAt (channel->id, 8) && p->totalSteps() == 28, "changing steps/beat rescales notes");

        // deleting a pattern removes its clips; deleting a channel removes its notes
        auto* second = s.createPattern();
        second->notes.push_back ({ channel->id, 0, 1, 60, 1.0f });
        s.addClip (second->id, 1, 0.0);
        s.deletePattern (second->id);
        check (s.getProject().clips.size() == 2, "deleting a pattern removes its clips");

        s.removeChannel (channel->id);
        check (s.getProject().channels.empty() && s.getProject().patterns.front()->notes.empty(),
               "deleting a channel removes its notes");
    }

    void testFiles (const File& dir)
    {
        std::cout << "Project and pattern files" << std::endl;
        const auto projectFile = dir.getChildFile ("roundtrip.mydaw");
        const auto patternFile = dir.getChildFile ("groove.mdpat");
        float firstRms = 0.0f;
        int channelId = 0;

        {
            TestEnvironment env (dir);
            auto& s = *env.session;
            String error;
            auto c = s.addSampleChannel (writeSine (dir.getChildFile ("tone.wav"), 330.0, 0.2, 44100.0, 2), error);
            channelId = c->id;
            c->volume = 0.5f;
            c->pan = -0.5f;
            auto* p = s.getCurrentPattern();
            p->name = "Groove";
            p->bars = 2;
            p->notes.push_back ({ c->id, 0, 1, 60, 0.9f });
            p->notes.push_back ({ c->id, 20, 3, 64, 0.5f });
            s.patternChanged (p->id);
            s.addClip (p->id, 2, 4.0);
            s.setBpm (97.0);

            check (s.saveProject (projectFile).wasOk() && ! s.isDirty(), "project saves");
            check (s.exportPattern (p->id, patternFile).wasOk(), "pattern exports to a file");
            s.exportWav (dir.getChildFile ("before.wav"));
            firstRms = readWav (dir.getChildFile ("before.wav")).rms (0.0, 10.0);
        }

        {
            TestEnvironment env (dir);
            auto& s = *env.session;
            StringArray warnings;
            const auto loaded = s.loadProject (projectFile, warnings);
            check (loaded.wasOk() && warnings.isEmpty(), "project loads without warnings " + warnings.joinIntoString ("; "));

            auto& p = s.getProject();
            check (approx (p.bpm, 97.0, 1e-9), "tempo restored");
            check (p.channels.size() == 1 && p.channels[0]->id == channelId && p.channels[0]->isLoaded(), "channel restored");
            check (approx (p.channels[0]->volume.load(), 0.5, 1e-6) && approx (p.channels[0]->pan.load(), -0.5, 1e-6), "volume and pan restored");
            check (p.patterns.size() == 1 && p.patterns[0]->notes.size() == 2 && p.patterns[0]->bars == 2, "pattern restored");
            check (p.clips.size() == 1 && p.clips[0].track == 2 && approx (p.clips[0].start, 4.0, 1e-9), "clip restored");

            s.exportWav (dir.getChildFile ("after.wav"));
            check (approx (readWav (dir.getChildFile ("after.wav")).rms (0.0, 10.0), firstRms, 1.0e-4), "reloaded project renders identically");

            // import the saved pattern into a fresh project: the channel is created automatically
            s.newProject();
            check (s.importPattern (patternFile, warnings).wasOk(), "pattern file imports");
            check (s.getProject().channels.size() == 1 && s.getProject().channels[0]->isLoaded(), "pattern import creates the missing channel");
            auto* imported = s.getCurrentPattern();
            check (imported != nullptr && imported->name == "Groove" && imported->notes.size() == 2
                       && imported->notes[0].channelId == s.getProject().channels[0]->id,
                   "imported pattern is selected and its notes point at the new channel");

            // importing again reuses the existing channel instead of duplicating it
            check (s.importPattern (patternFile, warnings).wasOk() && s.getProject().channels.size() == 1
                       && s.getProject().patterns.size() == 3,
                   "re-import reuses the channel");
        }
    }

    const PluginDescription* findType (KnownPluginList& list, bool instrument)
    {
        for (auto& t : list.getTypes())
            if (t.isInstrument == instrument)
                return new PluginDescription (t);

        return nullptr;
    }

    void testPluginHosting (const File& dir, const File& synthFile, const File& effectFile)
    {
        std::cout << "VST3 plug-in hosting" << std::endl;
        const auto projectFile = dir.getChildFile ("plugins.mydaw");

        {
            TestEnvironment env (dir);
            auto& s = *env.session;

            check (! env.plugins.scanFile (synthFile).isEmpty(), "instrument plug-in is found by the scanner");
            check (! env.plugins.scanFile (effectFile).isEmpty(), "effect plug-in is found by the scanner");

            std::unique_ptr<const PluginDescription> synthDesc (findType (env.plugins.getKnownPlugins(), true));
            std::unique_ptr<const PluginDescription> fxDesc (findType (env.plugins.getKnownPlugins(), false));

            if (synthDesc == nullptr || fxDesc == nullptr)
            {
                check (false, "plug-in descriptions available");
                return;
            }

            String error;
            auto channel = s.addPluginChannel (*synthDesc, error);
            check (channel != nullptr && channel->plugin != nullptr, "instrument plug-in instantiates (" + error + ")");

            if (channel == nullptr)
                return;

            auto* p = s.getCurrentPattern();
            p->notes.push_back ({ channel->id, 0, 8, 69, 1.0f });  // A4, half a bar
            s.patternChanged (p->id);
            s.addClip (p->id, 0, 0.0);

            const auto out = dir.getChildFile ("plugin.wav");
            check (s.exportWav (out).wasOk(), "song with an instrument plug-in exports");
            auto audio = readWav (out);
            const auto dryRms = audio.rms (0.1, 0.4);
            check (dryRms > 0.01, "instrument produces sound (rms " + String (dryRms, 4) + ")");
            check (approx (audio.frequency (0.1, 0.4), 440.0, 12.0), "instrument plays the right pitch (got " + String (audio.frequency (0.1, 0.4), 1) + " Hz)");

            check (s.addEffect (channel.get(), *fxDesc, error) && channel->effects.slots.size() == 1, "effect plug-in inserts on the channel");
            auto* gain = channel->effects.slots[0]->plugin->getParameters()[0];
            gain->setValueNotifyingHost (0.0f);
            s.exportWav (out);
            check (readWav (out).rms (0.1, 0.4) < dryRms * 0.05, "effect processes the channel (gain 0 silences it)");

            channel->effects.slots[0]->bypassed = true;
            s.exportWav (out);
            check (approx (readWav (out).rms (0.1, 0.4), dryRms, dryRms * 0.05), "bypassed effect passes audio through");
            channel->effects.slots[0]->bypassed = false;

            gain->setValueNotifyingHost (0.25f);
            check (s.addEffect (nullptr, *fxDesc, error) && s.getEngine().getMasterEffects().slots.size() == 1, "effect inserts on the master");
            check (s.saveProject (projectFile).wasOk(), "project with plug-ins saves");
        }

        {
            TestEnvironment env (dir);
            auto& s = *env.session;
            StringArray warnings;
            const auto loaded = s.loadProject (projectFile, warnings);
            check (loaded.wasOk() && warnings.isEmpty(),
                   "project with plug-ins loads " + loaded.getErrorMessage() + warnings.joinIntoString ("; "));

            auto& channels = s.getProject().channels;
            check (channels.size() == 1 && channels[0]->plugin != nullptr && channels[0]->effects.slots.size() == 1,
                   "instrument and effect are re-created");
            check (s.getEngine().getMasterEffects().slots.size() == 1, "master effect is re-created");

            if (channels.size() == 1 && channels[0]->effects.slots.size() == 1)
            {
                const auto value = channels[0]->effects.slots[0]->plugin->getParameters()[0]->getValue();
                check (approx (value, 0.25, 0.01), "plug-in state (parameter value) is restored (got " + String (value, 3) + ")");
            }

            const auto out = dir.getChildFile ("plugin2.wav");
            check (s.exportWav (out).wasOk() && readWav (out).rms (0.1, 0.4) > 0.001, "reloaded plug-in project renders sound");

            // removing a channel with plug-ins must not crash
            s.removeChannel (channels[0]->id);
            check (s.getProject().channels.empty(), "plug-in channel removed");
        }
    }
}

int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI juce;

    auto dir = File::getSpecialLocation (File::tempDirectory).getChildFile ("myDAWTests");
    dir.deleteRecursively();
    dir.createDirectory();

    testSampler (dir);
    testFreeBarLengthsAndArrangement (dir);
    testFiles (dir);

    if (argc >= 3)
        testPluginHosting (dir, File (argv[1]), File (argv[2]));
    else
        std::cout << "(plug-in tests skipped: pass an instrument and an effect .vst3 to run them)" << std::endl;

    std::cout << std::endl << (checks - failures) << " / " << checks << " checks passed" << std::endl;
    if (failures == 0)
        dir.deleteRecursively();

    return failures == 0 ? 0 : 1;
}
