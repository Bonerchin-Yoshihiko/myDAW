#pragma once

#include "Audio/SampleData.h"

/** Destructive edits for the sample editor. Every function returns a new SampleData and
    leaves the source untouched (so the old version can be kept for undo).
    An empty range means "the whole sample". */
namespace SampleEditing
{
    using Ptr = std::shared_ptr<SampleData>;

    Range<int> clampRange (const SampleData&, Range<int>);

    Ptr copy (const SampleData&);
    Ptr trimTo (const SampleData&, Range<int>);       // keep only the range
    Ptr remove (const SampleData&, Range<int>);       // nullptr if it would remove everything
    Ptr silence (const SampleData&, Range<int>);
    Ptr fadeIn (const SampleData&, Range<int>);
    Ptr fadeOut (const SampleData&, Range<int>);
    Ptr normalize (const SampleData&, Range<int>, float targetDb = -0.1f);
    Ptr applyGain (const SampleData&, Range<int>, float decibels);
    Ptr reverse (const SampleData&, Range<int>);

    /** Puts an effect-processed version of `range` back into the sample. `processed` may be
        longer than the range: the extra part (e.g. a reverb tail) is mixed over the audio
        that follows, extending the sample if needed. Mono becomes stereo when needed. */
    Ptr replaceWithProcessed (const SampleData&, Range<int>, const AudioBuffer<float>& processed);

    /** Length of `buffer` once trailing near-silence after `minimumLength` is removed. */
    int findEndOfSound (const AudioBuffer<float>&, int minimumLength, float threshold = 1.0e-4f);

    /** Turns a stereo buffer whose channels are identical into a mono one. */
    void collapseToMonoIfIdentical (AudioBuffer<float>&);

    Result writeWav (const SampleData&, const File&);
}
