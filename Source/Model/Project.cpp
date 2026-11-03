#include "Model/Project.h"
#include "Audio/Channel.h"

//==============================================================================
void Pattern::trimNotesToLength()
{
    const auto total = totalSteps();

    notes.erase (std::remove_if (notes.begin(), notes.end(),
                                 [total] (const Note& n) { return n.step < 0 || n.step >= total; }),
                 notes.end());

    for (auto& n : notes)
        n.length = jlimit (1, total - n.step, n.length);
}

void Pattern::changeStepsPerBeat (int newStepsPerBeat)
{
    newStepsPerBeat = jlimit (1, 16, newStepsPerBeat);

    if (newStepsPerBeat == stepsPerBeat)
        return;

    const auto ratio = (double) newStepsPerBeat / (double) stepsPerBeat;

    for (auto& n : notes)
    {
        n.step   = roundToInt (n.step * ratio);
        n.length = jmax (1, roundToInt (n.length * ratio));
    }

    stepsPerBeat = newStepsPerBeat;

    // merge notes that now collide on the same step/pitch
    std::vector<Note> unique;

    for (auto& n : notes)
    {
        auto dup = std::find_if (unique.begin(), unique.end(), [&n] (const Note& u)
                                 { return u.channelId == n.channelId && u.step == n.step && u.pitch == n.pitch; });

        if (dup == unique.end())
            unique.push_back (n);
    }

    notes = std::move (unique);
    trimNotesToLength();
}

bool Pattern::hasNoteAt (int channelId, int step) const
{
    return std::any_of (notes.begin(), notes.end(),
                        [=] (const Note& n) { return n.channelId == channelId && n.step == step; });
}

void Pattern::removeNotesAt (int channelId, int step)
{
    notes.erase (std::remove_if (notes.begin(), notes.end(),
                                 [=] (const Note& n) { return n.channelId == channelId && n.step == step; }),
                 notes.end());
}

var Pattern::toVar() const
{
    auto* o = new DynamicObject();
    o->setProperty ("id", id);
    o->setProperty ("name", name);
    o->setProperty ("colour", colour.toString());
    o->setProperty ("bars", bars);
    o->setProperty ("beatsPerBar", beatsPerBar);
    o->setProperty ("beatUnit", beatUnit);
    o->setProperty ("stepsPerBeat", stepsPerBeat);

    Array<var> noteList;

    for (auto& n : notes)
    {
        Array<var> fields { n.channelId, n.step, n.length, n.pitch, (double) n.velocity };
        noteList.add (var (fields));
    }

    o->setProperty ("notes", noteList);
    return var (o);
}

void Pattern::fromVar (const var& v)
{
    id           = (int) v.getProperty ("id", 0);
    name         = v.getProperty ("name", "Pattern").toString();
    colour       = Colour::fromString (v.getProperty ("colour", "ff42a5f5").toString());
    bars         = jlimit (1, 256, (int) v.getProperty ("bars", 1));
    beatsPerBar  = jlimit (1, 64, (int) v.getProperty ("beatsPerBar", 4));
    beatUnit     = jlimit (1, 32, (int) v.getProperty ("beatUnit", 4));
    stepsPerBeat = jlimit (1, 16, (int) v.getProperty ("stepsPerBeat", 4));

    notes.clear();

    if (auto* list = v.getProperty ("notes", var()).getArray())
    {
        for (auto& item : *list)
        {
            if (auto* f = item.getArray(); f != nullptr && f->size() >= 5)
            {
                Note n;
                n.channelId = (int) (*f)[0];
                n.step      = (int) (*f)[1];
                n.length    = jmax (1, (int) (*f)[2]);
                n.pitch     = jlimit (0, 127, (int) (*f)[3]);
                n.velocity  = jlimit (0.0f, 1.0f, (float) (double) (*f)[4]);
                notes.push_back (n);
            }
        }
    }

    trimNotesToLength();
}

String Pattern::getLengthDescription() const
{
    return String (bars) + (bars == 1 ? " bar  " : " bars  ")
         + String (beatsPerBar) + "/" + String (beatUnit);
}

//==============================================================================
Pattern* Project::findPattern (int id) const
{
    for (auto& p : patterns)
        if (p->id == id)
            return p.get();

    return nullptr;
}

Channel* Project::findChannel (int id) const
{
    for (auto& c : channels)
        if (c->id == id)
            return c.get();

    return nullptr;
}

Clip* Project::findClip (int id)
{
    for (auto& c : clips)
        if (c.id == id)
            return &c;

    return nullptr;
}

int Project::indexOfChannel (int id) const
{
    for (size_t i = 0; i < channels.size(); ++i)
        if (channels[i]->id == id)
            return (int) i;

    return -1;
}

double Project::getSongLength() const
{
    double end = 0.0;

    for (auto& c : clips)
        if (auto* p = findPattern (c.patternId))
            end = jmax (end, c.start + p->lengthInQuarters());

    const auto bar = (double) jmax (1, songBeatsPerBar);
    return std::ceil (end / bar - 1.0e-9) * bar;
}

//==============================================================================
Colour Palette::get (int index)
{
    static const uint32 colours[] = { 0xffef5350, 0xffffa726, 0xffd4e157, 0xff66bb6a, 0xff26c6da,
                                      0xff42a5f5, 0xff7e57c2, 0xffec407a, 0xff8d6e63, 0xff26a69a };
    const auto n = (int) (sizeof (colours) / sizeof (colours[0]));
    return Colour (colours[((index % n) + n) % n]);
}
