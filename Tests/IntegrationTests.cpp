/*  Integration tests for the myDAW engine.

    Usage:  myDAWTests [path/to/InstrumentPlugin.vst3 path/to/EffectPlugin.vst3]

    Without arguments the plug-in hosting tests are skipped. The effect plug-in is
    expected to have a gain-like first parameter (e.g. JUCE's GainPluginDemo).
*/

#include "Session.h"
#include "Audio/Channel.h"
#include "Audio/SampleEditing.h"
#include "Audio/Slicing.h"
#include "AI/StemSeparation.h"

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

    float peakOf (const SampleData& d)
    {
        float peak = 0.0f;

        for (int ch = 0; ch < d.buffer.getNumChannels(); ++ch)
            peak = jmax (peak, d.buffer.getMagnitude (ch, 0, d.getLength()));

        return peak;
    }


    /** Runs the live audio callback once, as the sound card would. */
    float runAudioCallback (AudioEngine& engine, int numSamples = 512)
    {
        AudioBuffer<float> out (2, numSamples);
        out.clear();
        AudioIODeviceCallbackContext context;
        engine.audioDeviceIOCallbackWithContext (nullptr, 0, out.getArrayOfWritePointers(), 2, numSamples, context);
        return jmax (out.getMagnitude (0, 0, numSamples), out.getMagnitude (1, 0, numSamples));
    }

    std::shared_ptr<SampleData> makeRamp (int length)
    {
        auto d = std::make_shared<SampleData>();
        d->buffer.setSize (1, length);

        for (int i = 0; i < length; ++i)
            d->buffer.setSample (0, i, (float) (i + 1) / (float) length);

        return d;
    }

    void testSampleEditing (const File& dir)
    {
        std::cout << "Sample editor: edits, undo, saving" << std::endl;

        // pure edit functions
        auto ramp = makeRamp (100);
        check (approx (SampleEditing::reverse (*ramp, {})->buffer.getSample (0, 0), 1.0, 1e-6), "reverse flips the audio");
        check (approx (SampleEditing::fadeIn (*ramp, {})->buffer.getSample (0, 0), 0.0, 1e-6)
                   && approx (SampleEditing::fadeOut (*ramp, {})->buffer.getSample (0, 99), 0.0, 1e-6), "fades reach silence");
        check (SampleEditing::trimTo (*ramp, { 10, 30 })->getLength() == 20, "trim keeps only the selection");
        check (SampleEditing::remove (*ramp, { 10, 30 })->getLength() == 80
                   && approx (SampleEditing::remove (*ramp, { 10, 30 })->buffer.getSample (0, 10), 0.31, 1e-6),
               "delete removes the selection and closes the gap");
        check (SampleEditing::remove (*ramp, {}) == nullptr, "deleting everything is refused");
        check (approx (peakOf (*SampleEditing::normalize (*ramp, {})), Decibels::decibelsToGain (-0.1), 1e-5), "normalize to -0.1 dB");
        check (approx (SampleEditing::silence (*ramp, { 0, 50 })->buffer.getMagnitude (0, 0, 50), 0.0, 1e-9)
                   && approx (SampleEditing::silence (*ramp, { 0, 50 })->buffer.getSample (0, 50), 0.51, 1e-6),
               "silence only affects the selection");
        check (approx (SampleEditing::applyGain (*ramp, {}, -6.0206f)->buffer.getSample (0, 99), 0.5, 1e-3), "gain -6 dB halves the level");

        AudioBuffer<float> processed (2, 30);   // 20 samples replace the selection, 10 more are a tail
        processed.clear();
        processed.setSample (1, 0, 1.0f);       // makes it stereo
        processed.setSample (0, 25, 0.5f);
        auto replaced = SampleEditing::replaceWithProcessed (*ramp, { 80, 100 }, processed);
        check (replaced->getLength() == 110 && replaced->buffer.getNumChannels() == 2, "effect tail extends the sample and keeps stereo");
        check (approx (replaced->buffer.getSample (0, 105), 0.5, 1e-6) && approx (replaced->buffer.getSample (0, 79), 0.8, 1e-6),
               "processed audio lands in the right place");

        // committing edits through the session
        const auto projectFile = dir.getChildFile ("edits.mydaw");
        const auto sourceFile = writeSine (dir.getChildFile ("edit-source.wav"), 440.0, 1.0, 44100.0, 1);
        int originalLength = 0;

        {
            TestEnvironment env (dir);
            auto& s = *env.session;
            String error;
            auto c = s.addSampleChannel (sourceFile, error);
            originalLength = c->sample->getLength();

            s.commitSampleEdit (*c, SampleEditing::normalize (*c->sample, {}));
            check (approx (peakOf (*c->sample), Decibels::decibelsToGain (-0.1), 1e-3) && c->sampleModified, "edit is applied to the channel");
            s.commitSampleEdit (*c, SampleEditing::trimTo (*c->sample, { 0, 22050 }));
            check (c->sample->getLength() == 22050, "second edit applied");
            check (s.undoSampleEdit (*c) && c->sample->getLength() == originalLength, "undo restores the previous version");
            check (s.redoSampleEdit (*c) && c->sample->getLength() == 22050, "redo re-applies it");

            // the edited sound is what the sequencer plays
            auto* p = s.getCurrentPattern();
            p->notes.push_back ({ c->id, 0, 1, 60, 1.0f });
            s.patternChanged (p->id);
            s.addClip (p->id, 0, 0.0);
            s.exportWav (dir.getChildFile ("edited.wav"));
            const auto audio = readWav (dir.getChildFile ("edited.wav"));
            check (audio.rms (0.1, 0.45) > 0.3 && audio.rms (0.52, 1.0) < 1.0e-4, "playback uses the edited (trimmed, louder) sample");

            check (s.saveProject (projectFile).wasOk(), "project with an edited sample saves");
            check (! c->sampleModified && c->sampleFile.isAChildOf (dir.getChildFile ("edits Samples"))
                       && c->sampleFile.existsAsFile() && sourceFile.existsAsFile(),
                   "edited sample is written next to the project; the original file is untouched");
        }

        {
            TestEnvironment env (dir);
            auto& s = *env.session;
            StringArray warnings;
            s.loadProject (projectFile, warnings);
            auto& c = *s.getProject().channels[0];
            check (warnings.isEmpty() && c.sample != nullptr && c.sample->getLength() == 22050, "reloaded project uses the edited sample");
            check (c.originalSampleFile == sourceFile, "original file is remembered");

            String error;
            check (s.revertSampleToOriginal (c, error) && c.sample->getLength() == originalLength, "revert to original");
            check (s.undoSampleEdit (c) && c.sample->getLength() == 22050, "revert can be undone");

            // live preview of the sample (as heard while editing), independent of the transport
            auto& engine = s.getEngine();
            engine.setMasterVolume (1.0f);
            engine.startSamplePreview (c.sample, {}, nullptr, false);
            const auto previewPeak = runAudioCallback (engine);
            check (previewPeak > 0.9f && engine.getSamplePreviewPosition() > 400.0, "sample preview plays through the audio callback");

            for (int i = 0; i < 60; ++i)
                runAudioCallback (engine);

            check (! engine.isSamplePreviewPlaying() && runAudioCallback (engine) < 1.0e-6f, "preview stops at the end of the sample");

            // an empty effect chain renders the input unchanged plus the tail
            EffectChain empty;
            AudioBuffer<float> out;
            check (s.getEngine().renderThroughChain (empty, c.sample->buffer, c.sample->sampleRate, 100, out).wasOk()
                       && out.getNumSamples() == 22150 && approx (out.getSample (0, 1000), c.sample->buffer.getSample (0, 1000), 1e-6)
                       && approx (out.getSample (1, 1000), c.sample->buffer.getSample (0, 1000), 1e-6),
                   "offline render passes audio through and appends the tail");
        }
    }


    std::shared_ptr<SampleData> makeHits (double sampleRate, double seconds, std::initializer_list<double> hitTimes)
    {
        auto d = std::make_shared<SampleData>();
        d->sampleRate = sampleRate;
        d->buffer.setSize (2, (int) (seconds * sampleRate));
        d->buffer.clear();
        Random random (42);

        for (auto t : hitTimes)
        {
            const auto start = (int) (t * sampleRate);

            for (int i = 0; i < (int) (0.15 * sampleRate) && start + i < d->getLength(); ++i)
            {
                const auto v = (random.nextFloat() * 2.0f - 1.0f) * 0.8f * std::exp (-(float) i / (float) (0.03 * sampleRate));
                d->buffer.setSample (0, start + i, v);
                d->buffer.setSample (1, start + i, v * 0.9f);
            }
        }

        return d;
    }

    void testSlicing (const File& dir)
    {
        std::cout << "Sampler: slicing and sampling" << std::endl;

        auto hits = makeHits (44100.0, 2.0, { 0.0, 0.5, 1.0, 1.5 });
        const auto slices = Slicing::atTransients (*hits, {}, 0.5f);
        bool startsMatch = slices.size() == 4;

        for (size_t i = 0; startsMatch && i < 4; ++i)
            startsMatch = std::abs (slices[i].getStart() - (int) (i * 0.5 * 44100.0)) < 0.012 * 44100.0;

        check (startsMatch, "transient slicing finds the 4 hits (" + String ((int) slices.size()) + " slices)");
        check (slices.front().getStart() == 0 && slices.back().getEnd() == hits->getLength(), "slices cover the whole range");

        const auto parts = Slicing::equalParts ({ 0, 1000 }, 4);
        check (parts.size() == 4 && parts[1] == Range<int> (250, 500), "equal parts");

        const auto beats = Slicing::everyBeats ({ 0, 88200 }, 44100.0, 120.0, 1.0);
        check (beats.size() == 4 && beats[1].getStart() == 22050, "one slice per beat at 120 BPM");

        const auto piece = Slicing::extract (*hits, { 22050, 44100 });
        check (piece->getLength() == 22050 && approx (piece->buffer.getSample (0, 0), 0.0, 1e-9)
                   && approx (piece->sampleRate, 44100.0, 1e-9),
               "extracted slice has the right length and a click-free start");

        // several samples from one file become channels and are saved with the project
        const auto projectFile = dir.getChildFile ("slices.mydaw");

        {
            TestEnvironment env (dir);
            auto& s = *env.session;

            for (size_t i = 0; i < slices.size(); ++i)
                s.addSampleChannelFromData (Slicing::extract (*hits, slices[i]), "Hit #" + String ((int) i + 1));

            check (s.getProject().channels.size() == 4 && s.getProject().channels[2]->name == "Hit #3"
                       && s.getProject().channels[2]->sample->getLength() == slices[2].getLength(),
                   "each slice becomes its own channel");
            check (s.saveProject (projectFile).wasOk(), "project with sliced samples saves");
            check (s.getProject().channels[0]->sampleFile.existsAsFile()
                       && s.getProject().channels[0]->sampleFile.isAChildOf (dir.getChildFile ("slices Samples")),
                   "slices are written as WAV files next to the project");
        }

        {
            TestEnvironment env (dir);
            StringArray warnings;
            env.session->loadProject (projectFile, warnings);
            auto& channels = env.session->getProject().channels;
            check (warnings.isEmpty() && channels.size() == 4 && channels[3]->sample != nullptr
                       && channels[3]->sample->getLength() == slices[3].getLength(),
                   "sliced samples load back");
        }
    }

    void testStemSeparation (const File& dir, const File& modelFile)
    {
        std::cout << "AI stem separation (demucs.cpp)" << std::endl;
        Stems::ModelType type;
        check (Stems::isValidModelFile (modelFile, &type), "model file is recognised");
        const auto& info = Stems::getModelInfo (type);

        // 9 seconds of a little "song" at 48 kHz: hits + a bass tone + a high melody
        auto mix = makeHits (48000.0, 9.0, { 0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0, 5.0, 6.0, 7.0, 8.0 });

        for (int i = 0; i < mix->getLength(); ++i)
        {
            const auto t = i / 48000.0;
            const auto v = (float) (0.2 * std::sin (MathConstants<double>::twoPi * 55.0 * t)
                                    + 0.1 * std::sin (MathConstants<double>::twoPi * 660.0 * t));
            mix->buffer.addSample (0, i, v);
            mix->buffer.addSample (1, i, v);
        }

        std::atomic<int> calls { 0 };
        std::vector<Stems::Stem> stems;
        const auto start = Time::getMillisecondCounterHiRes();
        const auto result = Stems::separate (modelFile, *mix, {}, 2, stems, [&calls] (float, const String&) { ++calls; return true; });
        const auto seconds = (Time::getMillisecondCounterHiRes() - start) * 0.001;
        std::cout << "        (separated 9 s of audio in " << String (seconds, 1) << " s)" << std::endl;

        check (result.wasOk(), "separation runs: " + result.getErrorMessage());
        check ((int) stems.size() == info.stems.size(), "one output per stem (" + String ((int) stems.size()) + ")");

        bool shapesOk = ! stems.empty(), finite = true, notSilent = true;
        const auto expectedLength = (int) std::llround (mix->getLength() * 44100.0 / 48000.0);

        for (auto& st : stems)
        {
            shapesOk = shapesOk && st.audio->getLength() == expectedLength && st.audio->buffer.getNumChannels() == 2
                       && approx (st.audio->sampleRate, 44100.0, 1e-9);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < st.audio->getLength(); i += 97)
                    finite = finite && std::isfinite (st.audio->buffer.getSample (ch, i));

            notSilent = notSilent && st.audio->buffer.getMagnitude (0, 0, st.audio->getLength()) > 0.0f;
        }

        String shapes;

        for (auto& st : stems)
            shapes << st.id << ":" << st.audio->getLength() << "x" << st.audio->buffer.getNumChannels()
                   << (st.audio->buffer.getMagnitude (0, 0, st.audio->getLength()) > 0.0f ? "" : "(silent)") << " ";

        check (shapesOk, "stems are stereo 44.1 kHz with the input's duration (expected " + String (expectedLength) + ", got " + shapes + ")");
        check (finite && notSilent, String ("stems contain valid audio") + (finite ? "" : " [non-finite values]") + (notSilent ? "" : " [silent stem]"));
        check (calls.load() > 5, "progress is reported (" + String (calls.load()) + " updates)");

        if (stems.size() >= 4)
        {
            auto instrumental = Stems::mixWithout (stems, "vocals");
            const auto expected = stems[0].audio->buffer.getSample (0, 1000) + stems[1].audio->buffer.getSample (0, 1000)
                                  + stems[2].audio->buffer.getSample (0, 1000)
                                  + (stems.size() > 4 ? stems[4].audio->buffer.getSample (0, 1000) + stems[5].audio->buffer.getSample (0, 1000) : 0.0f);
            check (approx (instrumental->buffer.getSample (0, 1000), expected, 1e-5), "\"No vocals\" is the sum of the other stems");
        }

        // separating only a selection
        std::vector<Stems::Stem> part;
        const auto partResult = Stems::separate (modelFile, *mix, { 48000, 48000 * 3 }, 1, part, {});
        check (partResult.wasOk() && ! part.empty() && part[0].audio->getLength() == 88200,
               "a selection can be separated on its own " + partResult.getErrorMessage()
                   + (part.empty() ? String() : " (length " + String (part[0].audio->getLength()) + ")"));

        // cancelling stops quickly
        std::vector<Stems::Stem> cancelled;
        const auto cancelStart = Time::getMillisecondCounterHiRes();
        int n = 0;
        const auto r = Stems::separate (modelFile, *mix, {}, 2, cancelled, [&n] (float, const String&) { return ++n < 3; });
        check (r.failed() && r.getErrorMessage() == "Cancelled" && cancelled.empty()
                   && Time::getMillisecondCounterHiRes() - cancelStart < seconds * 1000.0 * 0.8,
               "separation can be cancelled");

        // the background job used by the Sampler window
        StemSeparationJob job (modelFile, mix, { 0, 48000 * 2 });
        job.startThread();

        while (! job.isFinished())
            Thread::sleep (20);

        check (job.getError().isEmpty() && (int) job.getStems().size() == info.stems.size(), "background separation job works");
        ignoreUnused (dir);
    }

    void testSampleEffects (const File& dir, const File& effectFile)
    {
        std::cout << "Sample editor: applying a VST3 effect to a sample" << std::endl;
        TestEnvironment env (dir);
        auto& s = *env.session;
        env.plugins.scanFile (effectFile);
        std::unique_ptr<const PluginDescription> fxDesc (findType (env.plugins.getKnownPlugins(), false));

        if (fxDesc == nullptr)
        {
            check (false, "effect plug-in available");
            return;
        }

        String error;
        auto c = s.addSampleChannel (writeSine (dir.getChildFile ("fx-source.wav"), 440.0, 0.5, 48000.0, 1), error);

        // the sample editor's own effect rack (not the channel's inserts)
        EffectChain rack;
        s.getEngine().addAuxiliaryChain (&rack);
        auto instance = env.plugins.createInstance (*fxDesc, s.getEngine().getSampleRate(), s.getEngine().getBlockSize(), error);
        PluginHelpers::configureStereo (*instance, false);
        s.getEngine().preparePlugin (*instance);
        instance->getParameters()[0]->setValueNotifyingHost (0.5f);
        auto slot = std::make_unique<EffectChain::Slot>();
        slot->plugin = std::move (instance);
        slot->description = *fxDesc;

        {
            const ScopedLock sl (s.getEngine().getLock());
            rack.slots.push_back (std::move (slot));
            s.getEngine().auxiliaryChainChanged();
        }

        const auto before = peakOf (*c->sample);
        const Range<int> firstHalf (0, c->sample->getLength() / 2);
        AudioBuffer<float> section (c->sample->buffer.getNumChannels(), firstHalf.getLength());
        section.copyFrom (0, 0, c->sample->buffer, 0, 0, firstHalf.getLength());

        AudioBuffer<float> processed;
        check (s.getEngine().renderThroughChain (rack, section, c->sample->sampleRate, 0, processed).wasOk(), "effect renders offline");
        s.commitSampleEdit (*c, SampleEditing::replaceWithProcessed (*c->sample, firstHalf, processed));

        const auto& b = c->sample->buffer;
        check (approx (b.getMagnitude (0, 0, firstHalf.getLength()), before * 0.5, before * 0.02),
               "effect is baked into the selected part (gain 0.5)");
        check (approx (b.getMagnitude (0, firstHalf.getEnd(), b.getNumSamples() - firstHalf.getEnd()), before, before * 0.02),
               "the rest of the sample is unchanged");
        check (b.getNumChannels() == 1, "mono stays mono when the effect output is mono-compatible");
        check (approx (s.getEngine().getSampleRate(), 44100.0, 1e-9) && approx (rack.slots[0]->plugin->getSampleRate(), 44100.0, 1e-9),
               "plug-in is prepared for live preview again after rendering at the sample's rate");

        // the live preview goes through the rack (gain 0.5) before anything is applied
        auto& engine = s.getEngine();
        engine.setMasterVolume (1.0f);
        engine.startSamplePreview (c->sample, { firstHalf.getEnd(), c->sample->getLength() }, nullptr, false);
        const auto dry = runAudioCallback (engine);
        engine.startSamplePreview (c->sample, { firstHalf.getEnd(), c->sample->getLength() }, &rack, false);
        runAudioCallback (engine);   // first block may include the plug-in's smoothing
        const auto wet = runAudioCallback (engine);
        check (dry > 0.1f && approx (wet, dry * 0.5f, dry * 0.05f), "preview is heard through the effect rack");

        s.getEngine().removeAuxiliaryChain (&rack);
        check (! engine.isSamplePreviewPlaying(), "removing the rack stops its preview");
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
    testSampleEditing (dir);
    testSlicing (dir);

    {
        std::cout << "AI stem separation: parallel parts" << std::endl;
        String details;
        const auto ok = Stems::selfTestPartMerging (details);
        check (ok, "parts are split, placed and cross-faded exactly (" + details.trim() + ")");
    }

    // AI stem separation needs a model file: MYDAW_TEST_DEMUCS_MODEL=/path/to/ggml-model-htdemucs-6s-f16.bin
    const auto modelPath = SystemStats::getEnvironmentVariable ("MYDAW_TEST_DEMUCS_MODEL", {});

    if (File::isAbsolutePath (modelPath) && File (modelPath).existsAsFile())
        testStemSeparation (dir, File (modelPath));
    else
        std::cout << "(AI stem separation tests skipped: set MYDAW_TEST_DEMUCS_MODEL to a model file)" << std::endl;

    if (argc >= 3)
    {
        testPluginHosting (dir, File (argv[1]), File (argv[2]));
        testSampleEffects (dir, File (argv[2]));
    }
    else
        std::cout << "(plug-in tests skipped: pass an instrument and an effect .vst3 to run them)" << std::endl;

    std::cout << std::endl << (checks - failures) << " / " << checks << " checks passed" << std::endl;
    if (failures == 0)
        dir.deleteRecursively();

    return failures == 0 ? 0 : 1;
}
