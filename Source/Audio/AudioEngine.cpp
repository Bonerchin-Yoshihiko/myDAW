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

    for (auto* chain : auxiliaryChains)
    {
        chain->setPlayHead (this);
        chain->prepare (sampleRate, blockSize);
    }

    auxiliaryChainChanged();
    prepared = true;
}

void AudioEngine::masterEffectsChanged()
{
    masterBuffer.setSize (masterEffects.getRequiredChannels(), blockSize, false, true, true);
}

//==============================================================================
void AudioEngine::addAuxiliaryChain (EffectChain* chain)
{
    const ScopedLock sl (lock);

    if (std::find (auxiliaryChains.begin(), auxiliaryChains.end(), chain) == auxiliaryChains.end())
        auxiliaryChains.push_back (chain);

    chain->setPlayHead (this);
}

void AudioEngine::removeAuxiliaryChain (EffectChain* chain)
{
    std::shared_ptr<const SampleData> released;

    {
        const ScopedLock sl (lock);
        auxiliaryChains.erase (std::remove (auxiliaryChains.begin(), auxiliaryChains.end(), chain), auxiliaryChains.end());

        if (samplePreview.chain == chain)
        {
            samplePreview.active = false;
            samplePreview.chain = nullptr;
            std::swap (released, samplePreview.sample);
            uiPreviewActive.store (false);
        }
    }
}

void AudioEngine::auxiliaryChainChanged()
{
    auto numChannels = 2;

    for (auto* chain : auxiliaryChains)
        numChannels = jmax (numChannels, chain->getRequiredChannels());

    samplePreview.buffer.setSize (numChannels, blockSize, false, true, true);
}

void AudioEngine::startSamplePreview (std::shared_ptr<const SampleData> sample, Range<int> range, EffectChain* chain, bool loop)
{
    if (sample == nullptr || sample->getLength() < 2)
        return;

    range = Range<int> (0, sample->getLength()).getIntersectionWith (range);

    if (range.getLength() < 2)
        range = { 0, sample->getLength() };

    {
        const ScopedLock sl (lock);
        std::swap (samplePreview.sample, sample);   // the previous one is released outside the lock
        samplePreview.chain = chain;
        samplePreview.start = range.getStart();
        samplePreview.end = range.getEnd();
        samplePreview.position = range.getStart();
        samplePreview.loop = loop && range.getLength() > 64;
        samplePreview.sourceFinished = false;
        samplePreview.tailRemaining = (int) (sampleRate * 4.0);
        samplePreview.active = true;

        if (chain != nullptr)
            chain->reset();

        auxiliaryChainChanged();
        uiPreviewActive.store (true);
        uiPreviewPosition.store (samplePreview.position);
    }
}

void AudioEngine::stopSamplePreview()
{
    std::shared_ptr<const SampleData> released;

    {
        const ScopedLock sl (lock);
        samplePreview.active = false;
        std::swap (released, samplePreview.sample);
        uiPreviewActive.store (false);
    }
}

void AudioEngine::renderSamplePreview (int numSamples)
{
    auto& p = samplePreview;

    if (! p.active || p.sample == nullptr)
        return;

    auto& buf = p.buffer;

    if (buf.getNumSamples() < numSamples)
        return;

    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        buf.clear (ch, 0, numSamples);

    const auto& src = p.sample->buffer;
    const auto* srcL = src.getReadPointer (0);
    const auto* srcR = src.getReadPointer (src.getNumChannels() > 1 ? 1 : 0);
    const auto increment = p.sample->sampleRate / sampleRate;
    auto* outL = buf.getWritePointer (0);
    auto* outR = buf.getWritePointer (1);

    for (int i = 0; i < numSamples && ! p.sourceFinished; ++i)
    {
        if (p.position >= p.end)
        {
            if (p.loop)
            {
                p.position = p.start + std::fmod (p.position - p.end, (double) (p.end - p.start));
            }
            else
            {
                p.sourceFinished = true;
                break;
            }
        }

        const auto index = (int) p.position;
        const auto next = jmin (index + 1, src.getNumSamples() - 1);
        const auto frac = (float) (p.position - index);
        outL[i] = srcL[index] + frac * (srcL[next] - srcL[index]);
        outR[i] = srcR[index] + frac * (srcR[next] - srcR[index]);
        p.position += increment;
    }

    if (p.chain != nullptr)
        p.chain->process (buf, numSamples);

    if (p.sourceFinished)
    {
        // let reverbs and delays ring out, then stop
        p.tailRemaining -= numSamples;

        if (p.chain == nullptr || p.chain->isEmpty() || p.tailRemaining <= 0
             || jmax (buf.getMagnitude (0, 0, numSamples), buf.getMagnitude (1, 0, numSamples)) < 1.0e-5f)
            p.active = false;
    }

    masterBuffer.addFrom (0, 0, buf, 0, 0, numSamples);
    masterBuffer.addFrom (1, 0, buf, 1, 0, numSamples);

    uiPreviewPosition.store (jmin (p.position, (double) p.end));
    uiPreviewActive.store (p.active);
}

Result AudioEngine::renderThroughChain (EffectChain& chain, const AudioBuffer<float>& input, double renderRate,
                                        int tailSamples, AudioBuffer<float>& output)
{
    if (input.getNumSamples() <= 0 || input.getNumChannels() <= 0)
        return Result::fail ("Nothing to process.");

    const ScopedLock sl (lock); // the live audio callback outputs silence meanwhile

    samplePreview.active = false;
    uiPreviewActive.store (false);

    chain.setNonRealtime (true);
    chain.prepare (renderRate, blockSize);
    chain.reset();

    const auto latency = chain.getLatencySamples();
    const auto inputLength = input.getNumSamples();
    const auto outputLength = inputLength + jmax (0, tailSamples);
    const auto total = outputLength + latency;

    AudioBuffer<float> work (jmax (2, chain.getRequiredChannels()), blockSize);
    output.setSize (2, outputLength);
    output.clear();

    for (int pos = 0; pos < total; pos += blockSize)
    {
        const auto n = jmin (blockSize, total - pos);
        work.clear();

        if (pos < inputLength)
        {
            const auto count = jmin (n, inputLength - pos);

            for (int ch = 0; ch < 2; ++ch)
                work.copyFrom (ch, 0, input, jmin (ch, input.getNumChannels() - 1), pos, count);
        }

        chain.process (work, n);

        // drop the first `latency` samples so the result lines up with the input
        const auto firstOut = pos - latency;
        const auto skip = jmax (0, -firstOut);
        const auto count = jmin (n - skip, outputLength - (firstOut + skip));

        if (count > 0)
            for (int ch = 0; ch < 2; ++ch)
                output.copyFrom (ch, firstOut + skip, work, ch, skip, count);
    }

    chain.setNonRealtime (false);
    chain.prepare (sampleRate, blockSize);
    chain.reset();
    return Result::ok();
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

    renderSamplePreview (numSamples);   // the sample editor's preview skips the master effects

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

    samplePreview.active = false;
    uiPreviewActive.store (false);

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
