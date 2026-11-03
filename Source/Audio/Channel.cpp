#include "Audio/Channel.h"

Channel::Channel (int channelId, Type t) : id (channelId), type (t)
{
    pending.reserve (1024);
    midi.ensureSize (4096);
}

Channel::~Channel()
{
    effects.slots.clear();
    plugin.reset();
}

void Channel::prepare (double newSampleRate, int newMaxBlockSize)
{
    sampleRate = newSampleRate;
    maxBlockSize = jmax (1, newMaxBlockSize);

    if (plugin != nullptr)
    {
        plugin->releaseResources();
        plugin->prepareToPlay (sampleRate, maxBlockSize);
    }

    effects.prepare (sampleRate, maxBlockSize);

    for (auto& v : voices)
        v.active = false;

    heldNotes.fill (0);
    pending.clear();
    midi.clear();
    updateBufferSize();
    prepared = true;
}

void Channel::releaseResources()
{
    if (plugin != nullptr)
        plugin->releaseResources();

    effects.releaseResources();
    prepared = false;
}

void Channel::updateBufferSize()
{
    auto numChannels = jmax (2, effects.getRequiredChannels());

    if (plugin != nullptr)
        numChannels = jmax (numChannels, PluginHelpers::getRequiredChannels (*plugin));

    buffer.setSize (numChannels, maxBlockSize, false, true, true);
}

void Channel::setPlayHead (AudioPlayHead* playHead)
{
    if (plugin != nullptr)
        plugin->setPlayHead (playHead);

    effects.setPlayHead (playHead);
}

void Channel::setNonRealtime (bool nonRealtime)
{
    if (plugin != nullptr)
        plugin->setNonRealtime (nonRealtime);

    effects.setNonRealtime (nonRealtime);
}

void Channel::resetGainSmoothing()
{
    const auto vol = muted.load() ? 0.0f : volume.load();
    const auto p = jlimit (-1.0f, 1.0f, pan.load());
    lastGainL = vol * jmin (1.0f, 1.0f - p);
    lastGainR = vol * jmin (1.0f, 1.0f + p);
}

//==============================================================================
void Channel::noteOn (int sampleOffset, int pitch, float velocity)
{
    if (pending.size() >= pending.capacity())
        return;

    pending.push_back ({ sampleOffset, pitch, velocity, true });

    // keep the list sorted by time (events normally arrive in order)
    for (auto i = pending.size() - 1; i > 0 && pending[i - 1].offset > pending[i].offset; --i)
        std::swap (pending[i - 1], pending[i]);
}

void Channel::noteOff (int sampleOffset, int pitch)
{
    if (pending.size() >= pending.capacity())
        return;

    pending.push_back ({ sampleOffset, pitch, 0.0f, false });

    for (auto i = pending.size() - 1; i > 0 && pending[i - 1].offset > pending[i].offset; --i)
        std::swap (pending[i - 1], pending[i]);
}

void Channel::releaseNotes (bool hardStop)
{
    pending.clear();

    if (isSample())
    {
        for (auto& v : voices)
        {
            if (! v.active)
                continue;

            if (hardStop)
            {
                // very short fade to avoid clicks
                v.releasing = true;
                v.envelopeStep = v.envelope / (float) jmax (1.0, sampleRate * 0.003);
            }
            else if (! oneShot.load())
            {
                v.releasing = true;
                v.envelopeStep = v.envelope / (float) jmax (1.0, sampleRate * 0.01);
            }
        }
    }
    else
    {
        midi.clear();

        for (int note = 0; note < 128; ++note)
            if (heldNotes[(size_t) note] > 0)
                midi.addEvent (MidiMessage::noteOff (1, note), 0);

        if (hardStop)
            midi.addEvent (MidiMessage::allNotesOff (1), 0);

        heldNotes.fill (0);
    }
}

void Channel::handleEvent (const PendingEvent& e)
{
    if (e.on)
        startVoice (e.pitch, e.velocity);
    else if (! oneShot.load())
        stopVoices (e.pitch);
}

void Channel::startVoice (int pitch, float velocity)
{
    if (sample == nullptr)
        return;

    Voice* target = nullptr;

    for (auto& v : voices)
    {
        if (! v.active)
        {
            target = &v;
            break;
        }
    }

    if (target == nullptr) // steal the oldest voice
    {
        target = &voices[0];

        for (auto& v : voices)
            if (v.age < target->age)
                target = &v;
    }

    const auto semitones = (double) (pitch - rootNote.load());

    target->active = true;
    target->position = 0.0;
    target->increment = (sample->sampleRate / sampleRate) * std::pow (2.0, semitones / 12.0);
    target->gain = velocity;
    target->pitch = pitch;
    target->releasing = false;
    target->envelope = 1.0f;
    target->envelopeStep = 0.0f;
    target->age = ++voiceCounter;
}

void Channel::stopVoices (int pitch)
{
    for (auto& v : voices)
    {
        if (v.active && ! v.releasing && v.pitch == pitch)
        {
            v.releasing = true;
            v.envelopeStep = 1.0f / (float) jmax (1.0, sampleRate * 0.01);
        }
    }
}

void Channel::renderVoices (int start, int end)
{
    if (sample == nullptr || start >= end)
        return;

    const auto& src = sample->buffer;
    const auto length = src.getNumSamples();
    const auto* srcL = src.getReadPointer (0);
    const auto* srcR = src.getReadPointer (src.getNumChannels() > 1 ? 1 : 0);
    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getWritePointer (1);

    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        for (int i = start; i < end; ++i)
        {
            const auto index = (int) v.position;

            if (index >= length - 1)
            {
                v.active = false;
                break;
            }

            const auto frac = (float) (v.position - index);
            const auto l = srcL[index] + frac * (srcL[index + 1] - srcL[index]);
            const auto r = srcR[index] + frac * (srcR[index + 1] - srcR[index]);

            auto g = v.gain;

            if (v.releasing)
            {
                v.envelope -= v.envelopeStep;

                if (v.envelope <= 0.0f)
                {
                    v.active = false;
                    break;
                }

                g *= v.envelope;
            }

            outL[i] += l * g;
            outR[i] += r * g;
            v.position += v.increment;
        }
    }
}

void Channel::render (AudioBuffer<float>& mix, int numSamples)
{
    if (! prepared || numSamples <= 0)
        return;

    jassert (numSamples <= maxBlockSize);
    numSamples = jmin (numSamples, maxBlockSize);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    if (isSample())
    {
        int position = 0;
        size_t next = 0;

        while (position < numSamples)
        {
            while (next < pending.size() && pending[next].offset <= position)
                handleEvent (pending[next++]);

            const auto end = next < pending.size() ? jmin (numSamples, pending[next].offset) : numSamples;
            renderVoices (position, end);
            position = end;
        }

        while (next < pending.size())
            handleEvent (pending[next++]);
    }
    else if (plugin != nullptr)
    {
        for (auto& e : pending)
        {
            const auto offset = jlimit (0, numSamples - 1, e.offset);

            if (e.on)
            {
                midi.addEvent (MidiMessage::noteOn (1, e.pitch, (uint8) jlimit (1, 127, roundToInt (e.velocity * 127.0f))), offset);
                heldNotes[(size_t) e.pitch] = (uint8) jmin (255, heldNotes[(size_t) e.pitch] + 1);
            }
            else
            {
                midi.addEvent (MidiMessage::noteOff (1, e.pitch), offset);

                if (heldNotes[(size_t) e.pitch] > 0)
                    --heldNotes[(size_t) e.pitch];
            }
        }

        PluginHelpers::process (*plugin, buffer, numSamples, midi);
        midi.clear();
    }

    pending.clear();

    effects.process (buffer, numSamples);

    // volume / pan / mute with a per-block gain ramp
    const auto vol = muted.load() ? 0.0f : volume.load();
    const auto p = jlimit (-1.0f, 1.0f, pan.load());
    const auto gainL = vol * jmin (1.0f, 1.0f - p);
    const auto gainR = vol * jmin (1.0f, 1.0f + p);

    mix.addFromWithRamp (0, 0, buffer.getReadPointer (0), numSamples, lastGainL, gainL);
    mix.addFromWithRamp (1, 0, buffer.getReadPointer (1), numSamples, lastGainR, gainR);

    const auto peak = jmax (buffer.getMagnitude (0, 0, numSamples) * gainL,
                            buffer.getMagnitude (1, 0, numSamples) * gainR);
    activity.store (jmax (peak, activity.load()));

    lastGainL = gainL;
    lastGainR = gainR;
}
