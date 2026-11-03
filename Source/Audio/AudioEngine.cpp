#include "Audio/AudioEngine.h"

void Sequence::sort()
{
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b)
    {
        if (a.beat < b.beat) return true;
        if (b.beat < a.beat) return false;

        return ! a.on && b.on; // note-offs before note-ons at the same time
    });
}

//==============================================================================
AudioEngine::AudioEngine()
{
    masterEffects.setPlayHead (this);
}

AudioEngine::~AudioEngine()
{
    const ScopedLock sl (lock);
    channels.clear();
    masterEffects.slots.clear();
}

void AudioEngine::prepare (double newSampleRate, int newBlockSize)
{
    const ScopedLock sl (lock);
    prepareLocked (newSampleRate, newBlockSize);
}

void AudioEngine::prepareLocked (double newSampleRate, int newBlockSize)
{
    sampleRate = newSampleRate > 0 ? newSampleRate : 44100.0;
    blockSize = jlimit (16, 4096, newBlockSize > 0 ? newBlockSize : 512);
    masterBuffer.setSize (2, blockSize);

    for (auto& c : channels)
    {
        c->setPlayHead (this);
        c->prepare (sampleRate, blockSize);
    }

    masterEffects.setPlayHead (this);
    masterEffects.prepare (sampleRate, blockSize);
    masterEffectsChanged();
    prepared = true;
}

void AudioEngine::masterEffectsChanged()
{
    masterBuffer.setSize (masterEffects.getRequiredChannels(), blockSize, false, true, true);
}

void AudioEngine::setChannels (std::vector<std::shared_ptr<Channel>> newChannels)
{
    {
        const ScopedLock sl (lock);
        std::swap (channels, newChannels);
    }
    // the previous list (and any removed channels) is destroyed here, outside the lock
}

void AudioEngine::prepareNewChannel (Channel& c)
{
    c.setPlayHead (this);
    c.prepare (sampleRate, blockSize);
}

void AudioEngine::preparePlugin (AudioPluginInstance& p)
{
    p.setPlayHead (this);
    p.prepareToPlay (sampleRate, blockSize);
}

void AudioEngine::setSequence (std::shared_ptr<const Sequence> newSequence, bool resetPosition)
{
    {
        const ScopedLock sl (lock);
        std::swap (sequence, newSequence);

        if (resetPosition)
        {
            position = 0.0;
            pendingHardRelease = true;
        }
        else
        {
            pendingSoftRelease = true;
        }

        if (sequence != nullptr && sequence->loop && sequence->length > 1.0e-6)
            position = std::fmod (position, sequence->length);

        uiPosition.store (position);
        uiSongMode.store (sequence != nullptr && sequence->isSong);
    }
}

void AudioEngine::play()
{
    const ScopedLock sl (lock);
    playing = true;
    uiPlaying.store (true);
}

void AudioEngine::stop()
{
    const ScopedLock sl (lock);

    if (! playing)
        position = 0.0; // second stop returns to the start

    playing = false;
    pendingHardRelease = true;
    uiPlaying.store (false);
    uiPosition.store (position);
}

void AudioEngine::seek (double beat)
{
    const ScopedLock sl (lock);
    position = jmax (0.0, beat);

    if (sequence != nullptr && sequence->loop && sequence->length > 1.0e-6)
        position = std::fmod (position, sequence->length);

    pendingHardRelease = true;
    uiPosition.store (position);
}

void AudioEngine::setBpm (double newBpm)
{
    bpm.store (jlimit (20.0, 400.0, newBpm));
}

void AudioEngine::previewNote (int channelId, int pitch, float velocity, bool on)
{
    const auto scope = previewFifo.write (1);

    if (scope.blockSize1 > 0)
        previewEvents[(size_t) scope.startIndex1] = { channelId, pitch, velocity, on };
}

//==============================================================================
void AudioEngine::audioDeviceAboutToStart (AudioIODevice* device)
{
    prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}

void AudioEngine::audioDeviceStopped()
{
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                    float* const* outputChannelData, int numOutputChannels,
                                                    int numSamples, const AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            FloatVectorOperations::clear (outputChannelData[ch], numSamples);

    const ScopedTryLock sl (lock);

    if (! sl.isLocked() || ! prepared)
        return;

    int done = 0;

    while (done < numSamples)
    {
        const auto n = jmin (blockSize, numSamples - done);
        renderChunk (n);

        for (int ch = 0; ch < numOutputChannels; ++ch)
        {
            if (outputChannelData[ch] == nullptr)
                continue;

            if (numOutputChannels == 1)
            {
                FloatVectorOperations::copy (outputChannelData[ch] + done, masterBuffer.getReadPointer (0), n);
                FloatVectorOperations::add (outputChannelData[ch] + done, masterBuffer.getReadPointer (1), n);
                FloatVectorOperations::multiply (outputChannelData[ch] + done, 0.5f, n);
            }
            else if (ch < 2)
            {
                FloatVectorOperations::copy (outputChannelData[ch] + done, masterBuffer.getReadPointer (ch), n);
            }
        }

        done += n;
    }
}

void AudioEngine::releaseAllNotes (bool hardStop)
{
    for (auto& c : channels)
        c->releaseNotes (hardStop);
}

void AudioEngine::handlePreviewEvents()
{
    const auto scope = previewFifo.read (previewFifo.getNumReady());

    auto handle = [this] (int index)
    {
        const auto& e = previewEvents[(size_t) index];

        for (auto& c : channels)
        {
            if (c->id == e.channelId)
            {
                if (e.on) c->noteOn (0, e.pitch, e.velocity);
                else      c->noteOff (0, e.pitch);
            }
        }
    };

    for (int i = 0; i < scope.blockSize1; ++i) handle (scope.startIndex1 + i);
    for (int i = 0; i < scope.blockSize2; ++i) handle (scope.startIndex2 + i);
}

void AudioEngine::renderChunk (int numSamples)
{
    for (int ch = 0; ch < masterBuffer.getNumChannels(); ++ch)
        masterBuffer.clear (ch, 0, numSamples);

    if (pendingHardRelease || pendingSoftRelease)
    {
        releaseAllNotes (pendingHardRelease);
        pendingHardRelease = pendingSoftRelease = false;
    }

    handlePreviewEvents();

    blockStartBeat = position;

    if (playing)
        scheduleEvents (numSamples);

    for (auto& c : channels)
        c->render (masterBuffer, numSamples);

    masterEffects.process (masterBuffer, numSamples);

    const auto gain = masterVolume.load();

    for (int ch = 0; ch < 2; ++ch)
    {
        masterBuffer.applyGain (ch, 0, numSamples, gain);
        const auto peak = masterBuffer.getMagnitude (ch, 0, numSamples);
        masterPeak[(size_t) ch].store (jmax (peak, masterPeak[(size_t) ch].load()));
    }

    if (playing)
        samplesPlayed += numSamples;

    uiPosition.store (position);
    uiPlaying.store (playing);
}

void AudioEngine::scheduleEvents (int numSamples)
{
    const auto beatsPerSample = bpm.load() / 60.0 / sampleRate;

    if (sequence == nullptr)
    {
        position += numSamples * beatsPerSample;
        return;
    }

    const auto length = sequence->length;
    const auto hasEnd = length > 1.0e-6;
    double sampleOffset = 0.0;

    if (hasEnd && sequence->loop && position >= length)
        position = std::fmod (position, length);

    while (sampleOffset < numSamples)
    {
        const auto segmentEnd = position + (numSamples - sampleOffset) * beatsPerSample;

        if (hasEnd && segmentEnd >= length)
        {
            dispatch (position, length, sampleOffset, numSamples);
            sampleOffset += (length - position) / beatsPerSample;

            if (sequence->loop)
            {
                position = 0.0;
            }
            else
            {
                position = 0.0;
                playing = false;
                releaseAllNotes (false);
                break;
            }
        }
        else
        {
            dispatch (position, segmentEnd, sampleOffset, numSamples);
            position = segmentEnd;
            break;
        }
    }
}

void AudioEngine::dispatch (double fromBeat, double toBeat, double sampleOffset, int numSamples)
{
    const auto& events = sequence->events;
    const auto beatsPerSample = bpm.load() / 60.0 / sampleRate;

    auto it = std::lower_bound (events.begin(), events.end(), fromBeat,
                                [] (const Sequence::Event& e, double b) { return e.beat < b; });

    for (; it != events.end() && it->beat < toBeat; ++it)
    {
        const auto offset = jlimit (0, numSamples - 1, (int) (sampleOffset + (it->beat - fromBeat) / beatsPerSample));

        if (it->on) it->channel->noteOn (offset, it->pitch, it->velocity);
        else        it->channel->noteOff (offset, it->pitch);
    }
}

Optional<AudioPlayHead::PositionInfo> AudioEngine::getPosition() const
{
    PositionInfo info;
    const auto tempo = bpm.load();
    info.setBpm (tempo);

    int num = 4, den = 4;

    if (sequence != nullptr)
    {
        num = sequence->beatsPerBar;
        den = sequence->beatUnit;
    }

    info.setTimeSignature (TimeSignature { num, den });
    info.setIsPlaying (playing);
    info.setIsLooping (sequence != nullptr && sequence->loop);
    info.setPpqPosition (blockStartBeat);

    const auto barLength = num * 4.0 / den;
    info.setPpqPositionOfLastBarStart (std::floor (blockStartBeat / barLength) * barLength);
    info.setTimeInSeconds (blockStartBeat * 60.0 / tempo);
    info.setTimeInSamples ((int64) (blockStartBeat * 60.0 / tempo * sampleRate));
    return info;
}

//==============================================================================
Result AudioEngine::renderToFile (const File& file, std::shared_ptr<const Sequence> exportSequence,
                                  double tailSeconds, std::function<void (double)> progress)
{
    if (exportSequence == nullptr || exportSequence->length <= 0.0)
        return Result::fail ("The song is empty. Place some patterns on the song timeline first.");

    file.deleteFile();
    std::unique_ptr<OutputStream> stream (file.createOutputStream());

    if (stream == nullptr)
        return Result::fail ("Cannot write to " + file.getFullPathName());

    WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, AudioFormatWriterOptions{}.withSampleRate (sampleRate)
                                                                         .withNumChannels (2)
                                                                         .withBitsPerSample (24));
    if (writer == nullptr)
        return Result::fail ("Could not create the WAV writer.");

    const ScopedLock sl (lock); // the live audio callback outputs silence meanwhile

    auto savedSequence = sequence;
    const auto savedPosition = position;

    for (auto& c : channels)
    {
        c->setNonRealtime (true);
        c->resetGainSmoothing();
    }

    masterEffects.setNonRealtime (true);

    sequence = exportSequence;
    position = 0.0;
    playing = true;
    releaseAllNotes (true);
    pendingHardRelease = pendingSoftRelease = false;

    const auto songSamples = (int64) (exportSequence->length * 60.0 / bpm.load() * sampleRate);
    const auto totalSamples = songSamples + (int64) (tailSeconds * sampleRate);
    int64 written = 0;

    while (written < totalSamples)
    {
        const auto n = (int) jmin ((int64) blockSize, totalSamples - written);

        if (written >= songSamples)
            playing = false;

        renderChunk (n);
        const AudioBuffer<float> stereo (masterBuffer.getArrayOfWritePointers(), 2, n);
        writer->writeFromAudioSampleBuffer (stereo, 0, n);
        written += n;

        if (progress)
            progress ((double) written / (double) totalSamples);
    }

    writer.reset();

    for (auto& c : channels)
        c->setNonRealtime (false);

    masterEffects.setNonRealtime (false);

    sequence = savedSequence;
    position = savedPosition;
    playing = false;
    releaseAllNotes (true);
    uiPlaying.store (false);
    uiPosition.store (position);
    uiSongMode.store (sequence != nullptr && sequence->isSong);

    return Result::ok();
}
