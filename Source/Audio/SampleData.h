#pragma once

#include "Common.h"

/** An audio file loaded completely into memory (immutable once loaded). */
struct SampleData
{
    AudioBuffer<float> buffer;   // 1 (mono) or 2 (stereo) channels
    double sampleRate = 44100.0;
    File file;

    int getLength() const noexcept { return buffer.getNumSamples(); }

    static std::shared_ptr<SampleData> load (AudioFormatManager&, const File&, String& error);

    /** File patterns that the import dialogs and drag & drop accept. */
    static String getWildcard();
    static bool isSupportedFile (const File&);
};
