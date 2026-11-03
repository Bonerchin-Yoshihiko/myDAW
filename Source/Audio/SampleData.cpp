#include "Audio/SampleData.h"

std::shared_ptr<SampleData> SampleData::load (AudioFormatManager& formats, const File& file, String& error)
{
    if (! file.existsAsFile())
    {
        error = "File not found: " + file.getFullPathName();
        return {};
    }

    std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
    {
        error = "Unsupported or damaged audio file: " + file.getFileName();
        return {};
    }

    constexpr double maxSeconds = 20.0 * 60.0;
    const auto maxLength = (int64) (maxSeconds * reader->sampleRate);

    if (reader->lengthInSamples <= 0)
    {
        error = "The audio file is empty: " + file.getFileName();
        return {};
    }

    if (reader->lengthInSamples > maxLength)
    {
        error = "The audio file is too long (max 20 minutes): " + file.getFileName();
        return {};
    }

    auto data = std::make_shared<SampleData>();
    const auto numChannels = (int) jlimit (1u, 2u, reader->numChannels);
    const auto length = (int) reader->lengthInSamples;

    data->buffer.setSize (numChannels, length);
    data->sampleRate = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
    data->file = file;

    if (! reader->read (&data->buffer, 0, length, 0, true, numChannels > 1))
    {
        error = "Could not read the audio file: " + file.getFileName();
        return {};
    }

    return data;
}

String SampleData::getWildcard()
{
    return "*.wav;*.mp3;*.aif;*.aiff;*.flac;*.ogg";
}

bool SampleData::isSupportedFile (const File& f)
{
    return f.hasFileExtension ("wav;mp3;aif;aiff;flac;ogg");
}
