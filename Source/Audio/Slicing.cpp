#include "Audio/Slicing.h"

namespace Slicing
{
    namespace
    {
        std::vector<Range<int>> fromBoundaries (std::vector<int> cuts, Range<int> range)
        {
            cuts.push_back (range.getStart());
            cuts.push_back (range.getEnd());
            std::sort (cuts.begin(), cuts.end());
            cuts.erase (std::unique (cuts.begin(), cuts.end()), cuts.end());

            std::vector<Range<int>> result;

            for (size_t i = 0; i + 1 < cuts.size(); ++i)
                if (cuts[i + 1] - cuts[i] > 0)
                    result.push_back ({ cuts[i], cuts[i + 1] });

            return result;
        }
    }

    std::vector<Range<int>> atTransients (const SampleData& data, Range<int> range, float sensitivity)
    {
        range = Range<int> (0, data.getLength()).getIntersectionWith (range);

        if (range.isEmpty())
            range = { 0, data.getLength() };

        sensitivity = jlimit (0.0f, 1.0f, sensitivity);

        // energy envelope in ~5 ms hops
        const auto hop = jmax (16, (int) (data.sampleRate * 0.005));
        const auto numFrames = range.getLength() / hop;

        if (numFrames < 4)
            return { range };

        std::vector<float> energy ((size_t) numFrames, 0.0f);
        float peakEnergy = 0.0f;

        for (int f = 0; f < numFrames; ++f)
        {
            float sum = 0.0f;

            for (int ch = 0; ch < data.buffer.getNumChannels(); ++ch)
            {
                const auto* d = data.buffer.getReadPointer (ch, range.getStart() + f * hop);

                for (int i = 0; i < hop; ++i)
                    sum += d[i] * d[i];
            }

            energy[(size_t) f] = std::sqrt (sum / (float) (hop * data.buffer.getNumChannels()));
            peakEnergy = jmax (peakEnergy, energy[(size_t) f]);
        }

        if (peakEnergy <= 0.0f)
            return { range };

        // an onset = the level jumps well above the recent level and is loud enough overall
        const auto ratio = jmap (sensitivity, 4.0f, 1.6f);
        const auto floorLevel = peakEnergy * jmap (sensitivity, 0.25f, 0.03f);
        const auto minGapFrames = jmax (1, (int) (jmap (sensitivity, 0.20f, 0.06f) / 0.005f));

        std::vector<int> cuts;
        int lastOnset = -minGapFrames;
        float recent = energy[0];

        for (int f = 1; f < numFrames; ++f)
        {
            const auto e = energy[(size_t) f];

            if (e > floorLevel && e > recent * ratio && f - lastOnset >= minGapFrames)
            {
                // start the slice a little before the hit so the attack is not cut
                const auto position = range.getStart() + jmax (0, f - 1) * hop;

                if (position > range.getStart() + hop)
                    cuts.push_back (position);

                lastOnset = f;
            }

            recent = recent * 0.7f + e * 0.3f;
        }

        return fromBoundaries (cuts, range);
    }

    std::vector<Range<int>> equalParts (Range<int> range, int n)
    {
        n = jlimit (1, jmax (1, range.getLength()), n);
        std::vector<int> cuts;

        for (int i = 1; i < n; ++i)
            cuts.push_back (range.getStart() + (int) ((int64) range.getLength() * i / n));

        return fromBoundaries (cuts, range);
    }

    std::vector<Range<int>> everyBeats (Range<int> range, double sampleRate, double bpm, double beats)
    {
        const auto step = beats * 60.0 / jmax (1.0, bpm) * sampleRate;
        std::vector<int> cuts;

        if (step >= 1.0)
            for (auto pos = range.getStart() + step; pos < range.getEnd() - 1.0; pos += step)
                cuts.push_back (roundToInt (pos));

        return fromBoundaries (cuts, range);
    }

    std::shared_ptr<SampleData> extract (const SampleData& data, Range<int> range)
    {
        range = Range<int> (0, data.getLength()).getIntersectionWith (range);

        if (range.isEmpty())
            range = { 0, data.getLength() };

        auto result = std::make_shared<SampleData>();
        result->sampleRate = data.sampleRate;
        result->buffer.setSize (data.buffer.getNumChannels(), range.getLength());

        for (int ch = 0; ch < data.buffer.getNumChannels(); ++ch)
            result->buffer.copyFrom (ch, 0, data.buffer, ch, range.getStart(), range.getLength());

        // a few milliseconds of fade at both ends avoid clicks at the cut points
        const auto fade = jmin (range.getLength() / 4, (int) (data.sampleRate * 0.002));

        if (fade > 1)
        {
            for (int ch = 0; ch < result->buffer.getNumChannels(); ++ch)
            {
                result->buffer.applyGainRamp (ch, 0, fade, 0.0f, 1.0f);
                result->buffer.applyGainRamp (ch, range.getLength() - fade, fade, 1.0f, 0.0f);
            }
        }

        return result;
    }
}
