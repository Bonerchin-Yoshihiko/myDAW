#include "UI/SongEditor.h"
#include "Audio/Channel.h"

SongEditor::SongEditor (Session& s) : session (s)
{
    title.setFont (Theme::font (12.0f, true));
    title.setColour (Label::textColourId, Theme::dimText);
    addAndMakeVisible (title);

    snapLabel.setFont (Theme::font (12.0f));
    snapLabel.setColour (Label::textColourId, Theme::dimText);
    snapLabel.setJustificationType (Justification::centredRight);
    addAndMakeVisible (snapLabel);

    snapBox.addItem ("Bar", 1);
    snapBox.addItem ("Beat", 2);
    snapBox.addItem ("1/2 beat", 3);
    snapBox.addItem ("1/4 beat", 4);
    snapBox.addItem ("Off", 5);
    snapBox.setSelectedId (2, dontSendNotification);
    snapBox.setTooltip ("Grid snapping. Clips also stick magnetically to the edges of neighbouring clips.");
    addAndMakeVisible (snapBox);

    zoomOutButton.onClick = [this] { setZoom (pixelsPerBeat / 1.25, getLanesArea().getX()); };
    zoomInButton.onClick  = [this] { setZoom (pixelsPerBeat * 1.25, getLanesArea().getX()); };
    zoomOutButton.setTooltip ("Zoom out (Ctrl + mouse wheel)");
    zoomInButton.setTooltip ("Zoom in (Ctrl + mouse wheel)");
    addTrackButton.onClick = [this] { session.addTrack(); };
    addAndMakeVisible (zoomOutButton);
    addAndMakeVisible (zoomInButton);
    addAndMakeVisible (addTrackButton);

    hintLabel.setText ("Drag patterns here  |  Alt/Ctrl-drag: copy  |  Double-click: place selected pattern  |  Right-click: menu",
                       dontSendNotification);
    hintLabel.setFont (Theme::font (11.5f));
    hintLabel.setColour (Label::textColourId, Theme::dimText);
    hintLabel.setMinimumHorizontalScale (0.6f);
    addAndMakeVisible (hintLabel);

    hScroll.addListener (this);
    vScroll.addListener (this);
    hScroll.setAutoHide (false);
    vScroll.setAutoHide (true);
    addAndMakeVisible (hScroll);
    addAndMakeVisible (vScroll);

    setWantsKeyboardFocus (true);
    session.addListener (this);
    startTimerHz (30);
}

SongEditor::~SongEditor()
{
    session.removeListener (this);
}

//==============================================================================
Rectangle<int> SongEditor::getRulerArea() const
{
    return { headerWidth, toolbarHeight, getWidth() - headerWidth - scrollBarSize, rulerHeight };
}

Rectangle<int> SongEditor::getHeadersArea() const
{
    const auto top = toolbarHeight + rulerHeight;
    return { 0, top, headerWidth, getHeight() - top - scrollBarSize };
}

Rectangle<int> SongEditor::getLanesArea() const
{
    const auto top = toolbarHeight + rulerHeight;
    return { headerWidth, top, getWidth() - headerWidth - scrollBarSize, getHeight() - top - scrollBarSize };
}

double SongEditor::xToBeat (int x) const    { return scrollBeat + (x - headerWidth) / pixelsPerBeat; }
int SongEditor::beatToX (double beat) const { return headerWidth + roundToInt ((beat - scrollBeat) * pixelsPerBeat); }
int SongEditor::yToTrack (int y) const      { return (int) std::floor ((y - getLanesArea().getY() + scrollY) / (double) laneHeight); }
int SongEditor::trackToY (int track) const  { return getLanesArea().getY() + track * laneHeight - scrollY; }

double SongEditor::getSnap() const
{
    switch (snapBox.getSelectedId())
    {
        case 1:  return (double) session.getProject().songBeatsPerBar;
        case 2:  return 1.0;
        case 3:  return 0.5;
        case 4:  return 0.25;
        default: return 0.0;
    }
}

double SongEditor::snapBeat (double beat) const
{
    const auto snap = getSnap();
    return snap > 0.0 ? std::round (beat / snap) * snap : beat;
}

double SongEditor::placeClip (int clipIdToIgnore, int track, double rawStart, double length) const
{
    // magnetic edges: stick to the end/start of neighbouring clips on the same track
    const auto threshold = 10.0 / pixelsPerBeat;
    auto best = rawStart;
    auto bestDistance = threshold;
    auto& project = session.getProject();

    auto consider = [&] (double candidate)
    {
        const auto d = std::abs (candidate - rawStart);

        if (d < bestDistance)
        {
            bestDistance = d;
            best = candidate;
        }
    };

    consider (0.0);

    for (auto& c : project.clips)
    {
        if (c.id == clipIdToIgnore || c.track != track)
            continue;

        if (auto* p = project.findPattern (c.patternId))
        {
            consider (c.start + p->lengthInQuarters()); // our start to their end
            consider (c.start - length);                // our end to their start
            consider (c.start);                         // align starts
        }
    }

    if (bestDistance < threshold)
        return jmax (0.0, best);

    return jmax (0.0, snapBeat (rawStart));
}

Rectangle<int> SongEditor::getClipBounds (const Clip& c, const Pattern& p) const
{
    const auto x1 = beatToX (c.start);
    const auto x2 = beatToX (c.start + p.lengthInQuarters());
    return { x1, trackToY (c.track) + 3, jmax (4, x2 - x1), laneHeight - 6 };
}

Clip* SongEditor::clipAt (Point<int> pos)
{
    auto& project = session.getProject();

    for (int i = (int) project.clips.size(); --i >= 0;)
    {
        auto& c = project.clips[(size_t) i];

        if (auto* p = project.findPattern (c.patternId))
            if (getClipBounds (c, *p).contains (pos))
                return &c;
    }

    return nullptr;
}

//==============================================================================
void SongEditor::resized()
{
    auto bar = getLocalBounds().removeFromTop (toolbarHeight).reduced (8, 5);
    title.setBounds (bar.removeFromLeft (60));
    addTrackButton.setBounds (bar.removeFromLeft (70));
    bar.removeFromLeft (14);
    snapLabel.setBounds (bar.removeFromLeft (40));
    snapBox.setBounds (bar.removeFromLeft (96));
    bar.removeFromLeft (10);
    zoomOutButton.setBounds (bar.removeFromLeft (24));
    bar.removeFromLeft (2);
    zoomInButton.setBounds (bar.removeFromLeft (24));
    bar.removeFromLeft (14);
    hintLabel.setBounds (bar);
    hintLabel.setVisible (bar.getWidth() >= 320);   // too squashed to read on narrow windows

    const auto lanes = getLanesArea();
    hScroll.setBounds (lanes.getX(), lanes.getBottom(), lanes.getWidth(), scrollBarSize);
    vScroll.setBounds (lanes.getRight(), lanes.getY(), scrollBarSize, lanes.getHeight());
    updateScrollBars();
}

void SongEditor::updateScrollBars()
{
    const auto lanes = getLanesArea();
    const auto visibleBeats = lanes.getWidth() / pixelsPerBeat;
    const auto total = jmax (session.getProject().getSongLength() + 64.0, scrollBeat + visibleBeats, 256.0);
    hScroll.setRangeLimits (0.0, total, dontSendNotification);
    hScroll.setCurrentRange (scrollBeat, visibleBeats, dontSendNotification);

    const auto contentHeight = (int) session.getProject().tracks.size() * laneHeight + laneHeight;
    scrollY = jlimit (0, jmax (0, contentHeight - lanes.getHeight()), scrollY);
    vScroll.setRangeLimits (0.0, jmax ((double) contentHeight, (double) lanes.getHeight()), dontSendNotification);
    vScroll.setCurrentRange (scrollY, lanes.getHeight(), dontSendNotification);
}

void SongEditor::scrollBarMoved (ScrollBar* bar, double newRangeStart)
{
    if (bar == &hScroll) scrollBeat = jmax (0.0, newRangeStart);
    else                 scrollY = jmax (0, (int) newRangeStart);

    repaint();
}

void SongEditor::setZoom (double newPixelsPerBeat, int anchorX)
{
    const auto anchorBeat = xToBeat (anchorX);
    pixelsPerBeat = jlimit (3.0, 160.0, newPixelsPerBeat);
    scrollBeat = jmax (0.0, anchorBeat - (anchorX - headerWidth) / pixelsPerBeat);
    updateScrollBars();
    repaint();
}

void SongEditor::sessionChanged (int changes)
{
    if ((changes & Session::projectReplaced) != 0)
    {
        selectedClipId = 0;
        scrollBeat = 0.0;
        scrollY = 0;
    }

    if ((changes & (Session::arrangementChanged | Session::patternContentChanged | Session::patternListChanged
                    | Session::projectReplaced | Session::transportChanged | Session::selectionChanged)) != 0)
    {
        updateScrollBars();
        repaint();
    }
}

void SongEditor::timerCallback()
{
    auto& engine = session.getEngine();
    const auto pos = engine.isSongSequence() ? engine.getPositionInBeats() : -1.0;

    if (std::abs (pos - lastPlayheadPos) > 1.0e-9)
    {
        // follow the playhead
        if (engine.isPlaying() && pos >= 0.0 && ! draggingPlayhead && draggingClipId == 0)
        {
            const auto lanes = getLanesArea();
            const auto visibleBeats = lanes.getWidth() / pixelsPerBeat;

            if (pos > scrollBeat + visibleBeats * 0.92 || pos < scrollBeat)
            {
                scrollBeat = jmax (0.0, pos - visibleBeats * 0.1);
                updateScrollBars();
                repaint();
            }
        }

        const auto oldX = beatToX (lastPlayheadPos), newX = beatToX (pos);
        lastPlayheadPos = pos;
        repaint (oldX - 7, toolbarHeight, 15, getHeight());
        repaint (newX - 7, toolbarHeight, 15, getHeight());
    }
}

//==============================================================================
void SongEditor::paintClip (Graphics& g, const Clip& c, const Pattern& p, bool selected, bool ghost)
{
    auto r = getClipBounds (c, p).toFloat();
    auto colour = p.colour;

    g.setColour (colour.withAlpha (ghost ? 0.35f : 0.85f).darker (0.35f));
    g.fillRoundedRectangle (r, 4.0f);

    // header strip with the name
    auto head = r.removeFromTop (14.0f);
    g.setColour (colour.withAlpha (ghost ? 0.5f : 1.0f));
    g.fillRoundedRectangle (head.withHeight (14.0f), 4.0f);
    g.fillRect (head.withTrimmedTop (8.0f));

    g.setColour (Colours::black.withAlpha (0.8f));
    g.setFont (Theme::font (11.0f, true));
    g.drawText (p.name, head.reduced (4.0f, 0.0f).toNearestInt(), Justification::centredLeft, true);

    // mini preview of the notes
    auto& project = session.getProject();
    std::vector<int> usedChannels;

    for (auto& n : p.notes)
        if (std::find (usedChannels.begin(), usedChannels.end(), n.channelId) == usedChannels.end())
            usedChannels.push_back (n.channelId);

    std::sort (usedChannels.begin(), usedChannels.end(), [&project] (int a, int b)
               { return project.indexOfChannel (a) < project.indexOfChannel (b); });

    auto body = r.reduced (2.0f, 2.0f);
    const auto stepW = body.getWidth() / (float) p.totalSteps();
    int minPitch = 127, maxPitch = 0;

    for (auto& n : p.notes)
    {
        minPitch = jmin (minPitch, n.pitch);
        maxPitch = jmax (maxPitch, n.pitch);
    }

    const auto useChannelRows = usedChannels.size() > 1;
    const auto rowsCount = useChannelRows ? (int) usedChannels.size() : jmax (1, maxPitch - minPitch + 1);
    const auto rowH = jmin (6.0f, body.getHeight() / (float) rowsCount);

    g.setColour (Colours::white.withAlpha (ghost ? 0.3f : 0.75f));

    for (auto& n : p.notes)
    {
        int row = 0;

        if (useChannelRows)
            row = (int) std::distance (usedChannels.begin(), std::find (usedChannels.begin(), usedChannels.end(), n.channelId));
        else
            row = maxPitch - n.pitch;

        const auto y = body.getY() + (body.getHeight() - rowH * rowsCount) * 0.5f + row * rowH;
        g.fillRect (body.getX() + n.step * stepW, y, jmax (1.5f, n.length * stepW - 1.0f), jmax (1.0f, rowH - 1.0f));
    }

    auto outline = getClipBounds (c, p).toFloat();

    if (selected)
    {
        g.setColour (Colours::white);
        g.drawRoundedRectangle (outline.reduced (0.5f), 4.0f, 1.8f);
    }
    else
    {
        g.setColour (colour.brighter (0.4f).withAlpha (ghost ? 0.6f : 0.9f));
        g.drawRoundedRectangle (outline.reduced (0.5f), 4.0f, 1.0f);
    }
}

void SongEditor::paint (Graphics& g)
{
    auto& project = session.getProject();
    const auto lanes = getLanesArea();
    const auto ruler = getRulerArea();
    const auto headers = getHeadersArea();
    const auto barLength = (double) project.songBeatsPerBar;

    g.fillAll (Theme::background);

    // toolbar
    g.setColour (Theme::panel);
    g.fillRect (0, 0, getWidth(), toolbarHeight);

    // lanes
    {
        Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (lanes);

        for (int t = 0; t < (int) project.tracks.size(); ++t)
        {
            const auto y = trackToY (t);
            g.setColour (t % 2 == 0 ? Theme::panel.brighter (0.03f) : Theme::panel);
            g.fillRect (lanes.getX(), y, lanes.getWidth(), laneHeight);

            if (project.tracks[(size_t) t].muted)
            {
                g.setColour (Colours::black.withAlpha (0.25f));
                g.fillRect (lanes.getX(), y, lanes.getWidth(), laneHeight);
            }

            g.setColour (Theme::gridLine);
            g.drawHorizontalLine (y + laneHeight - 1, (float) lanes.getX(), (float) lanes.getRight());
        }

        const auto firstBeat = (int) std::floor (scrollBeat);
        const auto lastBeat = (int) std::ceil (xToBeat (lanes.getRight()));
        const auto bottom = (float) jmin (lanes.getBottom(), trackToY ((int) project.tracks.size()));
        const auto beatStep = pixelsPerBeat < 6.0 ? (int) barLength : 1;

        for (int b = firstBeat; b <= lastBeat; ++b)
        {
            const auto isBar = b % jmax (1, project.songBeatsPerBar) == 0;

            if (! isBar && b % beatStep != 0)
                continue;

            g.setColour (isBar ? Theme::barLine.withAlpha (0.7f) : Theme::gridLine.withAlpha (0.6f));
            g.drawVerticalLine (beatToX (b), (float) lanes.getY(), bottom);
        }

        // clips
        for (auto& c : project.clips)
            if (auto* p = project.findPattern (c.patternId))
                paintClip (g, c, *p, c.id == selectedClipId, false);

        // drop preview
        if (dropActive)
        {
            if (auto* p = project.findPattern (dropPatternId))
            {
                Clip ghost;
                ghost.patternId = dropPatternId;
                ghost.track = dropTrack;
                ghost.start = dropStart;
                paintClip (g, ghost, *p, false, true);
            }
        }

        // song end marker
        const auto songEnd = project.getSongLength();

        if (songEnd > 0.0)
        {
            const auto x = (float) beatToX (songEnd);
            g.setColour (Theme::accentWarm.withAlpha (0.6f));
            const float dashes[] = { 4.0f, 4.0f };
            g.drawDashedLine ({ x, (float) lanes.getY(), x, (float) lanes.getBottom() }, dashes, 2, 1.5f);
        }
    }

    // ruler
    g.setColour (Theme::panelLight);
    g.fillRect (ruler);

    {
        Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (ruler);
        g.setFont (Theme::font (11.0f));

        const auto firstBar = (int) std::floor (scrollBeat / barLength);
        const auto lastBar = (int) std::ceil (xToBeat (ruler.getRight()) / barLength);
        const auto labelEvery = pixelsPerBeat * barLength < 28.0 ? (pixelsPerBeat * barLength < 14.0 ? 8 : 4) : 1;

        for (int bar = firstBar; bar <= lastBar; ++bar)
        {
            const auto x = beatToX (bar * barLength);
            g.setColour (Theme::barLine);
            g.drawVerticalLine (x, (float) ruler.getY() + (bar % labelEvery == 0 ? 2.0f : 12.0f), (float) ruler.getBottom());

            if (bar % labelEvery == 0)
            {
                g.setColour (Theme::dimText);
                g.drawText (String (bar + 1), x + 3, ruler.getY(), 40, ruler.getHeight(), Justification::centredLeft);
            }
        }
    }

    g.setColour (Theme::outline);
    g.drawHorizontalLine (ruler.getBottom() - 1, (float) ruler.getX(), (float) ruler.getRight());

    // track headers
    {
        Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (headers);

        for (int t = 0; t < (int) project.tracks.size(); ++t)
        {
            auto& track = project.tracks[(size_t) t];
            auto r = Rectangle<int> (0, trackToY (t), headerWidth, laneHeight);

            g.setColour (Theme::panelLight);
            g.fillRect (r.reduced (0, 1));
            g.setColour (Theme::gridLine);
            g.drawHorizontalLine (r.getBottom() - 1, 0.0f, (float) headerWidth);

            auto muteR = Rectangle<int> (headerWidth - 28, r.getCentreY() - 10, 22, 20);
            g.setColour (track.muted ? Theme::accentWarm : Theme::panelLighter);
            g.fillRoundedRectangle (muteR.toFloat(), 3.0f);
            g.setColour (track.muted ? Colours::black : Theme::dimText);
            g.setFont (Theme::font (11.0f, true));
            g.drawText ("M", muteR, Justification::centred);

            g.setColour (track.muted ? Theme::dimText : Theme::text);
            g.setFont (Theme::font (13.0f));
            g.drawFittedText (track.name, r.withTrimmedLeft (10).withTrimmedRight (34), Justification::centredLeft, 1, 0.7f);
        }
    }

    g.setColour (Theme::outline);
    g.drawVerticalLine (headerWidth - 1, (float) toolbarHeight, (float) getHeight());
    g.drawHorizontalLine (toolbarHeight, 0.0f, (float) getWidth());

    g.setColour (Theme::panel);
    g.fillRect (0, toolbarHeight, headerWidth - 1, rulerHeight);

    // playhead
    auto& engine = session.getEngine();

    if (engine.isSongSequence())
    {
        const auto x = (float) beatToX (engine.getPositionInBeats());

        if (x >= (float) lanes.getX())
        {
            g.setColour (Theme::playhead);
            g.drawLine (x, (float) ruler.getY(), x, (float) lanes.getBottom(), 1.5f);

            Path tri;
            tri.addTriangle (x - 5.0f, (float) ruler.getY(), x + 5.0f, (float) ruler.getY(), x, (float) ruler.getY() + 7.0f);
            g.fillPath (tri);
        }
    }

    g.setColour (Theme::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

//==============================================================================
void SongEditor::mouseDown (const MouseEvent& e)
{
    grabKeyboardFocus();
    auto& project = session.getProject();

    if (getRulerArea().contains (e.getPosition()))
    {
        draggingPlayhead = true;
        session.setPlayMode (Session::PlayMode::song);
        session.seek (jmax (0.0, snapBeat (xToBeat (e.x))));
        return;
    }

    if (getHeadersArea().contains (e.getPosition()))
    {
        const auto track = yToTrack (e.y);

        if (! isPositiveAndBelow (track, (int) project.tracks.size()))
            return;

        if (e.mods.isPopupMenu())
        {
            showTrackMenu (track);
        }
        else if (e.x >= headerWidth - 30)
        {
            project.tracks[(size_t) track].muted = ! project.tracks[(size_t) track].muted;
            session.arrangementEdited();
        }
        else if (e.getNumberOfClicks() > 1)
        {
            renameTrack (track);
        }

        return;
    }

    if (! getLanesArea().contains (e.getPosition()))
        return;

    if (auto* clip = clipAt (e.getPosition()))
    {
        selectedClipId = clip->id;
        session.selectPattern (clip->patternId);

        if (e.mods.isPopupMenu())
        {
            repaint();
            showClipMenu (clip->id);
            return;
        }

        if (e.mods.isAltDown() || e.mods.isCommandDown())
        {
            // copy and drag the copy
            const auto copy = *clip;
            const auto newId = session.addClip (copy.patternId, copy.track, copy.start);
            selectedClipId = newId;
        }

        if (auto* c = project.findClip (selectedClipId))
        {
            draggingClipId = c->id;
            dragGrabOffset = xToBeat (e.x) - c->start;
        }

        repaint();
        return;
    }

    selectedClipId = 0;
    repaint();
}

void SongEditor::mouseDrag (const MouseEvent& e)
{
    auto& project = session.getProject();

    if (draggingPlayhead)
    {
        session.seek (jmax (0.0, snapBeat (xToBeat (e.x))));
        return;
    }

    if (draggingClipId == 0)
        return;

    auto* clip = project.findClip (draggingClipId);
    auto* p = clip != nullptr ? project.findPattern (clip->patternId) : nullptr;

    if (clip == nullptr || p == nullptr)
        return;

    const auto track = jlimit (0, (int) project.tracks.size() - 1, yToTrack (e.y));
    const auto start = placeClip (clip->id, track, xToBeat (e.x) - dragGrabOffset, p->lengthInQuarters());

    if (track != clip->track || std::abs (start - clip->start) > 1.0e-9)
    {
        clip->track = track;
        clip->start = start;
        session.arrangementEdited();
    }

    setMouseCursor (MouseCursor::DraggingHandCursor);
}

void SongEditor::mouseUp (const MouseEvent&)
{
    draggingPlayhead = false;
    draggingClipId = 0;
    setMouseCursor (MouseCursor::NormalCursor);
}

void SongEditor::mouseMove (const MouseEvent& e)
{
    setMouseCursor (clipAt (e.getPosition()) != nullptr ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
}

void SongEditor::mouseDoubleClick (const MouseEvent& e)
{
    if (! getLanesArea().contains (e.getPosition()) || clipAt (e.getPosition()) != nullptr)
        return;

    auto& project = session.getProject();
    const auto track = yToTrack (e.y);
    auto* p = session.getCurrentPattern();

    if (p == nullptr || ! isPositiveAndBelow (track, (int) project.tracks.size()))
        return;

    const auto start = placeClip (0, track, std::floor (xToBeat (e.x) / jmax (0.25, getSnap())) * jmax (0.25, getSnap()),
                                  p->lengthInQuarters());
    selectedClipId = session.addClip (p->id, track, start);
}

void SongEditor::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        setZoom (pixelsPerBeat * (wheel.deltaY > 0 ? 1.15 : 1.0 / 1.15), jmax (headerWidth, e.x));
        return;
    }

    const auto lanes = getLanesArea();
    const auto contentHeight = (int) session.getProject().tracks.size() * laneHeight + laneHeight;
    const auto horizontal = e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY)
                            || contentHeight <= lanes.getHeight();

    if (horizontal)
    {
        const auto delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        scrollBeat = jmax (0.0, scrollBeat - delta * 200.0 / pixelsPerBeat);
    }
    else
    {
        scrollY = jmax (0, scrollY - roundToInt (wheel.deltaY * 200.0f));
    }

    updateScrollBars();
    repaint();
}

bool SongEditor::keyPressed (const KeyPress& key)
{
    if ((key == KeyPress::deleteKey || key == KeyPress::backspaceKey) && selectedClipId != 0)
    {
        session.removeClip (selectedClipId);
        selectedClipId = 0;
        return true;
    }

    if (key == KeyPress ('d', ModifierKeys::commandModifier, 0) && selectedClipId != 0)
    {
        duplicateClip (selectedClipId);
        return true;
    }

    return false;
}

//==============================================================================
void SongEditor::duplicateClip (int clipId)
{
    auto& project = session.getProject();
    auto* c = project.findClip (clipId);
    auto* p = c != nullptr ? project.findPattern (c->patternId) : nullptr;

    if (p != nullptr)
        selectedClipId = session.addClip (c->patternId, c->track, c->start + p->lengthInQuarters());
}

void SongEditor::showClipMenu (int clipId)
{
    PopupMenu m;
    m.addItem ("Duplicate after (Ctrl+D)", [this, clipId] { duplicateClip (clipId); });
    m.addItem ("Make unique (copy the pattern)", [this, clipId]
    {
        auto* c = session.getProject().findClip (clipId);

        if (c == nullptr)
            return;

        if (auto* copy = session.duplicatePattern (c->patternId))
        {
            if (auto* clip = session.getProject().findClip (clipId))
                clip->patternId = copy->id;

            session.arrangementEdited();
        }
    });
    m.addItem ("Play from here", [this, clipId]
    {
        if (auto* c = session.getProject().findClip (clipId))
        {
            session.setPlayMode (Session::PlayMode::song);
            session.seek (c->start);
            session.play();
        }
    });
    m.addSeparator();
    m.addItem ("Delete (Del)", [this, clipId] { session.removeClip (clipId); selectedClipId = 0; });
    m.showMenuAsync (PopupMenu::Options().withMousePosition());
}

void SongEditor::showTrackMenu (int track)
{
    PopupMenu m;
    m.addItem ("Rename...", [this, track] { renameTrack (track); });
    m.addItem ("Mute", true, session.getProject().tracks[(size_t) track].muted, [this, track]
    {
        auto& tracks = session.getProject().tracks;

        if (isPositiveAndBelow (track, (int) tracks.size()))
        {
            tracks[(size_t) track].muted = ! tracks[(size_t) track].muted;
            session.arrangementEdited();
        }
    });
    m.addSeparator();
    m.addItem ("Remove track", session.getProject().tracks.size() > 1, false, [this, track] { removeTrack (track); });
    m.showMenuAsync (PopupMenu::Options().withMousePosition());
}

void SongEditor::renameTrack (int track)
{
    auto& tracks = session.getProject().tracks;

    if (! isPositiveAndBelow (track, (int) tracks.size()))
        return;

    auto* w = new AlertWindow ("Rename track", {}, MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", tracks[(size_t) track].name);
    w->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    w->enterModalState (true, ModalCallbackFunction::create ([this, track, w] (int result)
    {
        auto& t = session.getProject().tracks;
        const auto name = w->getTextEditorContents ("name").trim();

        if (result == 1 && name.isNotEmpty() && isPositiveAndBelow (track, (int) t.size()))
        {
            t[(size_t) track].name = name;
            session.arrangementEdited();
        }
    }), true);
}

void SongEditor::removeTrack (int track)
{
    auto& project = session.getProject();

    if (project.tracks.size() <= 1 || ! isPositiveAndBelow (track, (int) project.tracks.size()))
        return;

    project.tracks.erase (project.tracks.begin() + track);
    project.clips.erase (std::remove_if (project.clips.begin(), project.clips.end(),
                                         [track] (const Clip& c) { return c.track == track; }),
                         project.clips.end());

    for (auto& c : project.clips)
        if (c.track > track)
            --c.track;

    session.arrangementEdited();
}

//==============================================================================
int SongEditor::getDropPatternId (const SourceDetails& details) const
{
    const auto text = details.description.toString();
    return text.startsWith ("pattern:") ? text.fromFirstOccurrenceOf (":", false, false).getIntValue() : 0;
}

bool SongEditor::isInterestedInDragSource (const SourceDetails& details)
{
    return getDropPatternId (details) != 0;
}

void SongEditor::itemDragEnter (const SourceDetails& details)
{
    dropActive = true;
    itemDragMove (details);
}

void SongEditor::itemDragMove (const SourceDetails& details)
{
    auto& project = session.getProject();
    dropPatternId = getDropPatternId (details);
    auto* p = project.findPattern (dropPatternId);

    if (p == nullptr || ! getLanesArea().contains (details.localPosition))
    {
        dropActive = false;
        repaint();
        return;
    }

    dropActive = true;
    dropTrack = jlimit (0, (int) project.tracks.size() - 1, yToTrack (details.localPosition.y));
    dropStart = placeClip (0, dropTrack, xToBeat (details.localPosition.x) - 0.5, p->lengthInQuarters());
    repaint();
}

void SongEditor::itemDragExit (const SourceDetails&)
{
    dropActive = false;
    repaint();
}

void SongEditor::itemDropped (const SourceDetails& details)
{
    itemDragMove (details);

    if (dropActive)
        selectedClipId = session.addClip (dropPatternId, dropTrack, dropStart);

    dropActive = false;
    repaint();
}
