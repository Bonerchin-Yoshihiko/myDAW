#include "Audio/SampleEditing.h"

namespace SampleEditing
{
    namespace
    {
        Ptr makeFrom (const SampleData& source, AudioBuffer<float>&& buffer)
        {
            auto result = std::make_shared<SampleData>();
            result->buffer = std::move (buffer);
            result->sampleRate = source.sampleRate;
            result->file = source.file;
            return result;
        }

        template <typename Fn>
        Ptr modifyRange (const SampleData& source, Range<int> range, Fn&& fn)
        {
            auto result = copy (source);
            range = clampRange (source, range);

            for (int ch = 0; ch < result->buffer.getNumChannels(); ++ch)
                fn (result->buffer.getWritePointer (ch) + range.getStart(), range.getLength());

            return result;
        }
    }

    Range<int> clampRange (const SampleData& s, Range<int> r)
    {
        const auto all = Range<int> (0, s.getLength());
        r = all.getIntersectionWith (r);
        return r.isEmpty() ? all : r;
    }

    Ptr copy (const SampleData& source)
    {
        AudioBuffer<float> b (source.buffer);
        return makeFrom (source, std::move (b));
    }

    Ptr trimTo (const SampleData& source, Range<int> range)
    {
        range = clampRange (source, range);
        AudioBuffer<float> b (source.buffer.getNumChannels(), range.getLength());

        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            b.copyFrom (ch, 0, source.buffer, ch, range.getStart(), range.getLength());

        return makeFrom (source, std::move (b));
    }

    Ptr remove (const SampleData& source, Range<int> range)
    {
        range = clampRange (source, range);
        const auto newLength = source.getLength() - range.getLength();

        if (newLength <= 0)
            return {};

        AudioBuffer<float> b (source.buffer.getNumChannels(), newLength);

        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            b.copyFrom (ch, 0, source.buffer, ch, 0, range.getStart());
            b.copyFrom (ch, range.getStart(), source.buffer, ch, range.getEnd(), source.getLength() - range.getEnd());
        }

        return makeFrom (source, std::move (b));
    }

    Ptr silence (const SampleData& source, Range<int> range)
    {
        return modifyRange (source, range, [] (float* d, int n) { FloatVectorOperations::clear (d, n); });
    }

    Ptr fadeIn (const SampleData& source, Range<int> range)
    {
        return modifyRange (source, range, [] (float* d, int n)
        {
            for (int i = 0; i < n; ++i)
                d[i] *= (float) i / (float) jmax (1, n - 1);
        });
    }

    Ptr fadeOut (const SampleData& source, Range<int> range)
    {
        return modifyRange (source, range, [] (float* d, int n)
        {
            for (int i = 0; i < n; ++i)
                d[i] *= 1.0f - (float) i / (float) jmax (1, n - 1);
        });
    }

    Ptr normalize (const SampleData& source, Range<int> range, float targetDb)
    {
        const auto r = clampRange (source, range);
        float peak = 0.0f;

        for (int ch = 0; ch < source.buffer.getNumChannels(); ++ch)
            peak = jmax (peak, source.buffer.getMagnitude (ch, r.getStart(), r.getLength()));

        if (peak <= 0.0f)
            return copy (source);

        const auto gain = Decibels::decibelsToGain (targetDb) / peak;
        return modifyRange (source, r, [gain] (float* d, int n) { FloatVectorOperations::multiply (d, gain, n); });
    }

    Ptr applyGain (const SampleData& source, Range<int> range, float decibels)
    {
        const auto gain = Decibels::decibelsToGain (decibels, -200.0f);
        return modifyRange (source, range, [gain] (float* d, int n) { FloatVectorOperations::multiply (d, gain, n); });
    }

    Ptr reverse (const SampleData& source, Range<int> range)
    {
        return modifyRange (source, range, [] (float* d, int n) { std::reverse (d, d + n); });
    }

    Ptr replaceWithProcessed (const SampleData& source, Range<int> range, const AudioBuffer<float>& processed)
    {
        range = clampRange (source, range);
        const auto numChannels = jmax (source.buffer.getNumChannels(), jmin (2, processed.getNumChannels()));
        const auto processedLength = processed.getNumSamples();
        const auto newLength = jmax (source.getLength(), range.getStart() + processedLength);

        AudioBuffer<float> b (numChannels, newLength);
        b.clear();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto srcCh = jmin (ch, source.buffer.getNumChannels() - 1);
            b.copyFrom (ch, 0, source.buffer, srcCh, 0, source.getLength());

            const auto procCh = jmin (ch, processed.getNumChannels() - 1);
            const auto bodyLength = jmin (range.getLength(), processedLength);

            // the processed range replaces the original ...
            b.copyFrom (ch, range.getStart(), processed, procCh, 0, bodyLength);

            // ... and its tail rings on over whatever follows
            if (processedLength > bodyLength)
                b.addFrom (ch, range.getStart() + bodyLength, processed, procCh, bodyLength, processedLength - bodyLength);
        }

        collapseToMonoIfIdentical (b);
        return makeFrom (source, std::move (b));
    }

    int findEndOfSound (const AudioBuffer<float>& buffer, int minimumLength, float threshold)
    {
        auto end = buffer.getNumSamples();

        while (end > minimumLength)
        {
            bool loud = false;

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                loud = loud || std::abs (buffer.getSample (ch, end - 1)) > threshold;

            if (loud)
                break;

            --end;
        }

        return end;
    }

    void collapseToMonoIfIdentical (AudioBuffer<float>& b)
    {
        if (b.getNumChannels() != 2)
            return;

        const auto* l = b.getReadPointer (0);
        const auto* r = b.getReadPointer (1);

        for (int i = 0; i < b.getNumSamples(); ++i)
            if (std::abs (l[i] - r[i]) > 1.0e-6f)
                return;

        b.setSize (1, b.getNumSamples(), true);
    }

    Result writeWav (const SampleData& data, const File& file)
    {
        if (! file.getParentDirectory().createDirectory())
            return Result::fail ("Cannot create the folder " + file.getParentDirectory().getFullPathName());

        TemporaryFile temp (file);
        std::unique_ptr<OutputStream> stream (temp.getFile().createOutputStream());

        if (stream == nullptr)
            return Result::fail ("Cannot write " + file.getFullPathName());

        WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, AudioFormatWriterOptions{}
                                                       .withSampleRate (data.sampleRate)
                                                       .withNumChannels (data.buffer.getNumChannels())
                                                       .withBitsPerSample (32)
                                                       .withSampleFormat (AudioFormatWriterOptions::SampleFormat::floatingPoint));
        if (writer == nullptr)
            return Result::fail ("Could not create a WAV writer for " + file.getFullPathName());

        if (! writer->writeFromAudioSampleBuffer (data.buffer, 0, data.getLength()))
            return Result::fail ("Could not write " + file.getFullPathName());

        writer.reset();

        if (! temp.overwriteTargetFileWithTemporary())
            return Result::fail ("Could not replace " + file.getFullPathName());

        return Result::ok();
    }
}
