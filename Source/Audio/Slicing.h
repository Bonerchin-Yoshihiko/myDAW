#pragma once

#include "Audio/SampleData.h"

/** Ways of chopping a range of a sample into slices (all positions in frames). */
namespace Slicing
{
    /** Splits at detected hits/onsets. sensitivity: 0 (few slices) .. 1 (many slices). */
    std::vector<Range<int>> atTransients (const SampleData&, Range<int>, float sensitivity = 0.5f);

    /** n slices of equal length. */
    std::vector<Range<int>> equalParts (Range<int>, int n);

    /** One slice every `beats` quarter notes at the given tempo, starting at the range start. */
    std::vector<Range<int>> everyBeats (Range<int>, double sampleRate, double bpm, double beats);

    /** Copies a range out of a sample as a new, independent sample. */
    std::shared_ptr<SampleData> extract (const SampleData&, Range<int>);
}
