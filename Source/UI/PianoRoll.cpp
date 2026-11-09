#include "UI/PianoRoll.h"
#include "Audio/Channel.h"

namespace
{
    bool isBlackKey (int pitch)
    {
        const auto n = pitch % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }
}

PianoRoll::PianoRoll (Session& s)
    : session (s),
      longPress ([this] (Point<int>)
      {
          const auto index = dragNoteIndex;
          stopPreview();
          dragMode = DragMode::none;
          dragNoteIndex = -1;
          setViewportIgnoreDragFlag (false);

          if (index >= 0)
              showNoteMenu (index);
      })
{
    session.addListener (this);
}

PianoRoll::~PianoRoll()
{
    stopPreview();
    session.removeListener (this);
}

void PianoRoll::setChannel (int newChannelId)
{
    channelId = newChannelId;
    lastLength = 1;
    updateLayout (lastVisibleWidth);
    repaint();
}

int PianoRoll::getSuggestedScrollPitch() const
{
    auto* p = session.getCurrentPattern();
    int sum = 0, count = 0;

    if (p != nullptr)
        for (auto& n : p->notes)
            if (n.channelId == channelId) { sum += n.pitch; ++count; }

    if (count > 0)
        return sum / count;

    if (auto* c = session.getProject().findChannel (channelId))
        return c->rootNote.load();

    return 60;
}

void PianoRoll::sessionChanged (int changes)
{
    if ((changes & (Session::patternContentChanged | Session::selectionChanged | Session::channelsChanged
                    | Session::projectReplaced)) != 0)
    {
        updateLayout (lastVisibleWidth);
        repaint();
    }
}

void PianoRoll::updateLayout (int visibleWidth)
{
    lastVisibleWidth = visibleWidth;
    const auto* p = session.getCurrentPattern();
    const auto steps = p != nullptr ? p->totalSteps() : 16;
    cellWidth = jlimit (10, 60, (visibleWidth - keyWidth - 4) / jmax (1, steps));
    setSize (jmax (visibleWidth, keyWidth + steps * cellWidth + 4), 128 * noteHeight);
}

void PianoRoll::setPlayPosition (double stepPosition)
{
    if (std::abs (stepPosition - playStep) > 1.0e-6)
    {
        playStep = stepPosition;
        repaint();
    }
}

int PianoRoll::xToStep (int x) const   { return (x - keyWidth) / cellWidth; }
int PianoRoll::yToPitch (int y) const  { return jlimit (0, 127, 127 - y / noteHeight); }

Rectangle<int> PianoRoll::getNoteBounds (const Note& n) const
{
    return { keyWidth + n.step * cellWidth, getYForPitch (n.pitch), n.length * cellWidth, noteHeight };
}

int PianoRoll::findNoteAt (Point<int> pos, bool& onRightEdge) const
{
    onRightEdge = false;
    auto* p = session.getCurrentPattern();

    if (p == nullptr)
        return -1;

    for (int i = (int) p->notes.size(); --i >= 0;)
    {
        auto& n = p->notes[(size_t) i];

        if (n.channelId != channelId)
            continue;

        auto r = getNoteBounds (n);

        if (r.contains (pos))
        {
            // a finger needs a wider grip on the note's end
            const auto grip = touchInput ? 14 : 7;
            onRightEdge = pos.x >= r.getRight() - jmin (grip, r.getWidth() / 3);
            return i;
        }
    }

    return -1;
}

void PianoRoll::paint (Graphics& g)
{
    auto* p = session.getCurrentPattern();
    auto* channel = session.getProject().findChannel (channelId);

    g.fillAll (Theme::background);

    if (p == nullptr || channel == nullptr)
        return;

    const auto steps = p->totalSteps();
    const auto gridRight = keyWidth + steps * cellWidth;
    const auto clip = g.getClipBounds();
    const auto root = channel->rootNote.load();

    // rows
    for (int pitch = 0; pitch < 128; ++pitch)
    {
        const auto y = getYForPitch (pitch);

        if (y > clip.getBottom() || y + noteHeight < clip.getY())
            continue;

        auto rowColour = isBlackKey (pitch) ? Theme::panel : Theme::panelLight;

        if (channel->isSample() && pitch == root)
            rowColour = rowColour.interpolatedWith (channel->colour, 0.18f);

        g.setColour (rowColour);
        g.fillRect (keyWidth, y, gridRight - keyWidth, noteHeight);

        if (pitch % 12 == 0)
        {
            g.setColour (Theme::barLine.withAlpha (0.6f));
            g.drawHorizontalLine (y + noteHeight - 1, (float) keyWidth, (float) gridRight);
        }
        else
        {
            g.setColour (Theme::gridLine.withAlpha (0.5f));
            g.drawHorizontalLine (y + noteHeight - 1, (float) keyWidth, (float) gridRight);
        }

        // keyboard
        auto key = Rectangle<int> (0, y, keyWidth, noteHeight);
        const auto black = isBlackKey (pitch);
        g.setColour (pitch == previewPitch ? Theme::accent : (black ? Colour (0xff2a2d33) : Colour (0xffd9dde2)));
        g.fillRect (key.withTrimmedRight (black ? 18 : 0).reduced (0, black ? 1 : 0));

        if (black)
        {
            g.setColour (Colour (0xffd9dde2));
            g.fillRect (key.removeFromRight (18));
        }

        g.setColour (Colour (0xff9aa0a8));
        g.drawHorizontalLine (y + noteHeight - 1, 0.0f, (float) keyWidth);

        if (pitch % 12 == 0 || (channel->isSample() && pitch == root))
        {
            g.setColour (pitch == root && channel->isSample() ? channel->colour.darker (0.4f) : Colour (0xff50555e));
            g.setFont (Theme::font (10.5f, pitch % 12 == 0));
            g.drawText (MidiMessage::getMidiNoteName (pitch, true, true, 4), key.withTrimmedLeft (4).withTrimmedRight (4),
                        Justification::centredRight);
        }
    }

    g.setColour (Theme::outline);
    g.drawVerticalLine (keyWidth - 1, 0.0f, (float) getHeight());

    // vertical grid
    for (int step = 0; step <= steps; ++step)
    {
        const auto x = keyWidth + step * cellWidth;

        if (step % p->stepsPerBar() == 0)       g.setColour (Theme::barLine);
        else if (step % p->stepsPerBeat == 0)   g.setColour (Theme::gridLine.brighter (0.25f));
        else                                    g.setColour (Theme::gridLine.withAlpha (0.6f));

        g.drawVerticalLine (x, (float) clip.getY(), (float) clip.getBottom());
    }

    // notes
    for (auto& n : p->notes)
    {
        if (n.channelId != channelId)
            continue;

        auto r = getNoteBounds (n).toFloat().reduced (0.5f, 1.0f);
        g.setColour (channel->colour.withMultipliedBrightness (0.5f + 0.5f * n.velocity));
        g.fillRoundedRectangle (r, 2.5f);
        g.setColour (channel->colour.brighter (0.6f));
        g.drawRoundedRectangle (r, 2.5f, 1.0f);

        if (r.getWidth() > 26)
        {
            g.setColour (Colours::black.withAlpha (0.7f));
            g.setFont (Theme::font (10.0f));
            g.drawText (MidiMessage::getMidiNoteName (n.pitch, true, true, 4), r.reduced (3.0f, 0.0f),
                        Justification::centredLeft, false);
        }
    }

    if (playStep >= 0.0)
    {
        const auto x = (float) keyWidth + (float) (playStep * cellWidth);
        g.setColour (Theme::playhead);
        g.drawLine (x, (float) clip.getY(), x, (float) clip.getBottom(), 1.5f);
    }
}

void PianoRoll::startPreview (int pitch)
{
    if (previewPitch == pitch)
        return;

    stopPreview();
    previewPitch = pitch;
    session.previewNote (channelId, pitch, true);
    repaint (0, getYForPitch (pitch), keyWidth, noteHeight);
}

void PianoRoll::stopPreview()
{
    if (previewPitch >= 0)
    {
        session.previewNote (channelId, previewPitch, false);
        repaint (0, getYForPitch (previewPitch), keyWidth, noteHeight);
    }

    previewPitch = -1;
}

void PianoRoll::mouseMove (const MouseEvent& e)
{
    bool edge = false;
    const auto index = findNoteAt (e.getPosition(), edge);
    setMouseCursor (index >= 0 ? (edge ? MouseCursor::LeftRightResizeCursor : MouseCursor::DraggingHandCursor)
                               : MouseCursor::NormalCursor);
}

void PianoRoll::mouseDown (const MouseEvent& e)
{
    auto* p = session.getCurrentPattern();
    touchPending = false;
    setViewportIgnoreDragFlag (false);

    if (p == nullptr || session.getProject().findChannel (channelId) == nullptr)
        return;

    const auto touch = Touch::isTouch (e);
    touchInput = touch;

    if (e.x < keyWidth)
    {
        // with a finger the keyboard only plays the key; dragging it scrolls
        dragMode = touch ? DragMode::none : DragMode::keyboard;
        startPreview (yToPitch (e.y));
        return;
    }

    bool edge = false;
    auto index = findNoteAt (e.getPosition(), edge);

    if (e.mods.isPopupMenu())
    {
        if (index >= 0)
        {
            p->notes.erase (p->notes.begin() + index);
            session.patternChanged (p->id);
        }

        return;
    }

    const auto step = xToStep (e.x);

    if (index < 0)
    {
        if (touch)
        {
            touchPending = true;   // a tap adds a note (see mouseUp), a drag scrolls
            return;
        }

        if (! isPositiveAndBelow (step, p->totalSteps()))
            return;

        addNoteAt (e.getPosition());
        index = (int) p->notes.size() - 1;
        edge = false;
    }

    if (touch)
    {
        setViewportIgnoreDragFlag (true);   // the finger moves the note, not the view
        longPress.start (e);
    }

    dragNoteIndex = index;
    dragOriginal = p->notes[(size_t) index];
    dragStartStep = step;
    dragStartPitch = yToPitch (e.y);
    dragMode = edge ? DragMode::resize : DragMode::move;

    if (dragMode == DragMode::move)
        startPreview (dragOriginal.pitch);
}

void PianoRoll::addNoteAt (Point<int> pos)
{
    auto* p = session.getCurrentPattern();
    const auto step = xToStep (pos.x);

    if (p == nullptr || ! isPositiveAndBelow (step, p->totalSteps()))
        return;

    Note n;
    n.channelId = channelId;
    n.step = step;
    n.pitch = yToPitch (pos.y);
    n.length = jmin (lastLength, p->totalSteps() - step);
    n.velocity = 0.8f;
    p->notes.push_back (n);
    session.patternChanged (p->id);
}

void PianoRoll::mouseDrag (const MouseEvent& e)
{
    auto* p = session.getCurrentPattern();
    longPress.drag (e);

    if (Touch::isTouch (e) && e.getDistanceFromDragStart() > Touch::dragThreshold)
    {
        touchPending = false;

        if (dragMode == DragMode::none)
            stopPreview();   // scrolling
    }

    if (dragMode == DragMode::keyboard)
    {
        startPreview (yToPitch (e.y));
        return;
    }

    if (dragMode == DragMode::none || p == nullptr || ! isPositiveAndBelow (dragNoteIndex, (int) p->notes.size()))
        return;

    auto& n = p->notes[(size_t) dragNoteIndex];
    const auto total = p->totalSteps();
    const auto oldNote = n;

    if (dragMode == DragMode::move)
    {
        const auto deltaStep = xToStep (e.x) - dragStartStep;
        const auto deltaPitch = yToPitch (e.y) - dragStartPitch;
        n.step = jlimit (0, total - 1, dragOriginal.step + deltaStep);
        n.pitch = jlimit (0, 127, dragOriginal.pitch + deltaPitch);
        n.length = jmin (dragOriginal.length, total - n.step);

        if (n.pitch != oldNote.pitch)
            startPreview (n.pitch);
    }
    else if (dragMode == DragMode::resize)
    {
        const auto endStep = jlimit (n.step + 1, total, (int) std::ceil ((e.x - keyWidth) / (double) cellWidth));
        n.length = endStep - n.step;
        lastLength = n.length;
    }

    if (n.step != oldNote.step || n.pitch != oldNote.pitch || n.length != oldNote.length)
        session.patternChanged (p->id);
}

void PianoRoll::mouseUp (const MouseEvent& e)
{
    auto* p = session.getCurrentPattern();
    longPress.cancel();

    if (touchPending && e.getNumberOfClicks() < 2)
        addNoteAt (e.getMouseDownPosition());

    touchPending = false;

    if (dragMode == DragMode::move && p != nullptr && isPositiveAndBelow (dragNoteIndex, (int) p->notes.size()))
        lastLength = p->notes[(size_t) dragNoteIndex].length;

    stopPreview();
    dragMode = DragMode::none;
    dragNoteIndex = -1;
    setViewportIgnoreDragFlag (false);
}

void PianoRoll::showNoteMenu (int noteIndex)
{
    auto* p = session.getCurrentPattern();

    if (p == nullptr || ! isPositiveAndBelow (noteIndex, (int) p->notes.size()))
        return;

    const auto patternId = p->id;
    const auto note = p->notes[(size_t) noteIndex];

    // find the note again when the menu item is picked (the list may have changed)
    auto withNote = [this, patternId, note] (std::function<void (Pattern&, size_t)> action)
    {
        if (auto* pat = session.getProject().findPattern (patternId))
        {
            for (size_t i = 0; i < pat->notes.size(); ++i)
            {
                auto& n = pat->notes[i];

                if (n.channelId == note.channelId && n.step == note.step && n.pitch == note.pitch)
                {
                    action (*pat, i);
                    session.patternChanged (patternId);
                    return;
                }
            }
        }
    };

    PopupMenu m;
    m.addSectionHeader ("Velocity (strength)");

    for (auto percent : { 100, 80, 60, 40, 20 })
    {
        const auto v = (float) percent / 100.0f;
        m.addItem (String (percent) + " %", true, std::abs (note.velocity - v) < 0.026f,
                   [withNote, v] { withNote ([v] (Pattern& pat, size_t i) { pat.notes[i].velocity = v; }); });
    }

    m.addSeparator();
    m.addItem ("Delete note", [withNote] { withNote ([] (Pattern& pat, size_t i) { pat.notes.erase (pat.notes.begin() + (long) i); }); });
    m.showMenuAsync (Touch::menuAtPointer (PopupMenu::Options().withTargetComponent (this)));
}

void PianoRoll::mouseDoubleClick (const MouseEvent& e)
{
    auto* p = session.getCurrentPattern();
    bool edge = false;
    const auto index = findNoteAt (e.getPosition(), edge);

    if (p != nullptr && index >= 0 && e.x >= keyWidth)
    {
        p->notes.erase (p->notes.begin() + index);
        dragNoteIndex = -1;
        dragMode = DragMode::none;
        session.patternChanged (p->id);
    }
}

void PianoRoll::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    auto* p = session.getCurrentPattern();
    bool edge = false;
    const auto index = findNoteAt (e.getPosition(), edge);

    if (p != nullptr && index >= 0 && (e.mods.isCtrlDown() || e.mods.isAltDown()))
    {
        auto& n = p->notes[(size_t) index];
        n.velocity = jlimit (0.05f, 1.0f, n.velocity + (wheel.deltaY > 0 ? 0.05f : -0.05f));
        session.patternChanged (p->id);
        return;
    }

    Component::mouseWheelMove (e, wheel);
}
