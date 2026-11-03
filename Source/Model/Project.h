#pragma once

#include "Common.h"

class Channel;

/** A single note inside a pattern. Positions are measured in pattern steps. */
struct Note
{
    int channelId = 0;
    int step = 0;          // start step inside the pattern
    int length = 1;        // length in steps
    int pitch = 60;        // MIDI note number
    float velocity = 0.8f; // 0..1
};

/** A pattern (a "block" of one or more bars) that can be placed on the song timeline.
    The bar length is free: bars x (beatsPerBar / beatUnit), e.g. 7/8, 5/4, 3/4 ...
    The song timeline is measured in quarter notes ("beats"). */
struct Pattern
{
    int id = 0;
    String name;
    Colour colour;
    int bars = 1;
    int beatsPerBar = 4;   // time-signature numerator
    int beatUnit = 4;      // time-signature denominator (2, 4, 8, 16)
    int stepsPerBeat = 4;  // grid resolution per beat unit
    std::vector<Note> notes;

    int totalSteps() const noexcept        { return jmax (1, bars * beatsPerBar * stepsPerBeat); }
    int stepsPerBar() const noexcept       { return beatsPerBar * stepsPerBeat; }
    double stepLengthInQuarters() const noexcept { return (4.0 / beatUnit) / stepsPerBeat; }
    double lengthInQuarters() const noexcept     { return totalSteps() * stepLengthInQuarters(); }

    /** Removes notes outside the pattern and shortens the ones that hang over the end. */
    void trimNotesToLength();

    /** Rescales note positions when the step resolution changes. */
    void changeStepsPerBeat (int newStepsPerBeat);

    bool hasNoteAt (int channelId, int step) const;
    void removeNotesAt (int channelId, int step);

    var toVar() const;
    void fromVar (const var&);   // reads everything including id
    String getLengthDescription() const;
};

struct Clip
{
    int id = 0;
    int patternId = 0;
    int track = 0;
    double start = 0.0; // in quarter notes
};

struct Track
{
    String name;
    bool muted = false;
};

/** Everything that is stored in a project file. Only touched from the message thread,
    except for the channel objects, whose list is mirrored into the audio engine. */
struct Project
{
    double bpm = 120.0;
    int songBeatsPerBar = 4;   // grid of the song timeline (in quarter notes)
    float masterVolume = 0.8f;
    bool loopSong = true;

    std::vector<std::shared_ptr<Channel>> channels;
    std::vector<std::unique_ptr<Pattern>> patterns;
    std::vector<Track> tracks;
    std::vector<Clip> clips;

    int nextId = 1;
    int createId() noexcept { return nextId++; }

    Pattern* findPattern (int id) const;
    Channel* findChannel (int id) const;
    Clip* findClip (int id);
    int indexOfChannel (int id) const;

    /** End of the last clip, rounded up to a whole bar of the song grid. */
    double getSongLength() const;
};

namespace Palette
{
    Colour get (int index);
}
