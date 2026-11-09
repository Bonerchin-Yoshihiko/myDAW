#include "UI/StepGrid.h"
#include "UI/AppFiles.h"
#include "UI/ChannelSettings.h"
#include "UI/Sampler.h"

//==============================================================================
class StepGrid::RowHeader : public Component
{
public:
    RowHeader (StepGrid& g, Channel& c)
        : grid (g), channelId (c.id),
          longPress ([this] (Point<int>)
          {
              stopPreview();
              grid.showChannelMenu (channelId, *this);
          })
    {
        mute.setButtonText ("M");
        mute.setClickingTogglesState (true);
        mute.setToggleState (c.muted.load(), dontSendNotification);
        mute.setColour (TextButton::buttonOnColourId, Theme::accentWarm);
        mute.setTooltip ("Mute");
        mute.onClick = [this]
        {
            if (auto* ch = getChannel()) { ch->muted = mute.getToggleState(); grid.session.markDirty(); repaint(); }
        };

        volume.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        volume.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        volume.setRange (0.0, 1.25, 0.001);
        volume.setDoubleClickReturnValue (true, 0.8);
        volume.setValue (c.volume.load(), dontSendNotification);
        volume.setColour (Slider::rotarySliderFillColourId, c.colour);
        volume.setViewportIgnoreDragFlag (true);   // turning the knob with a finger must not scroll
        volume.setPopupDisplayEnabled (true, true, nullptr);
        volume.textFromValueFunction = [] (double v)
        {
            return v <= 0.0001 ? String ("-inf dB") : String (Decibels::gainToDecibels (v), 1) + " dB";
        };
        volume.onValueChange = [this]
        {
            if (auto* ch = getChannel()) { ch->volume = (float) volume.getValue(); grid.session.markDirty(); }
        };

        keys.setButtonText ("Keys");
        keys.setTooltip ("Edit this channel in the piano roll (melodies, pitch, note length)");
        keys.onClick = [this] { if (grid.onOpenPianoRoll) grid.onOpenPianoRoll (channelId); };

        more.setButtonText ("...");
        more.setTooltip ("Channel settings and insert effects");
        more.onClick = [this] { ChannelSettings::show (grid.session, grid.pluginWindows, channelId, more, &grid.sampleEditors); };

        edit.setButtonText ("Edit");
        edit.setTooltip (c.isSample() ? "Edit the sound: waveform, trim, fades, and VST effects baked into the sample"
                                      : "Open the instrument plug-in's window");
        edit.onClick = [this] { grid.editChannel (channelId); };

        for (auto* comp : std::initializer_list<Component*> { &mute, &volume, &edit, &keys, &more })
            addAndMakeVisible (comp);
    }

    Channel* getChannel() const { return grid.session.getProject().findChannel (channelId); }

    void resized() override
    {
        // narrow (phones): no volume knob (it is in the "..." settings)
        const auto narrow = getWidth() < 260;
        volume.setVisible (! narrow);

        auto r = getLocalBounds().reduced (0, 5);
        r.removeFromLeft (18);
        mute.setBounds (r.removeFromLeft (22));
        more.setBounds (r.removeFromRight (24).reduced (0, 1));
        r.removeFromRight (3);
        keys.setBounds (r.removeFromRight (38).reduced (0, 1));
        r.removeFromRight (3);
        edit.setBounds (r.removeFromRight (36).reduced (0, 1));
        r.removeFromRight (3);

        if (! narrow)
        {
            volume.setBounds (r.removeFromRight (26).expanded (0, 3));
            r.removeFromRight (2);
        }

        nameArea = r.withTrimmedLeft (4);
    }

    void paint (Graphics& g) override
    {
        auto* c = getChannel();

        if (c == nullptr)
            return;

        auto r = getLocalBounds();
        g.setColour (Theme::panelLight);
        g.fillRect (r.reduced (0, 1));

        g.setColour (c->colour);
        g.fillRect (r.removeFromLeft (4).reduced (0, 1));

        // activity LED
        const auto level = jlimit (0.0f, 1.0f, displayedActivity * 2.0f);
        auto led = Rectangle<float> (8.0f, (float) getHeight() * 0.5f - 3.5f, 7.0f, 7.0f);
        g.setColour (Theme::panelLighter.interpolatedWith (Colour (0xff66ff88), level));
        g.fillEllipse (led);

        auto nameBox = nameArea.toFloat().reduced (0.0f, 1.0f);
        g.setColour (pressed ? Theme::panelLighter.brighter (0.2f) : Theme::panelLighter);
        g.fillRoundedRectangle (nameBox, 3.0f);

        g.setColour (c->isLoaded() ? Theme::text : Theme::recordRed);
        g.setFont (Theme::font (13.0f));
        g.drawFittedText (c->name, nameArea.reduced (6, 0), Justification::centredLeft, 1, 0.7f);

        if (c->isPlugin())
        {
            g.setColour (Theme::accent.withAlpha (0.7f));
            g.fillEllipse (nameBox.getRight() - 9.0f, nameBox.getY() + 4.0f, 4.0f, 4.0f);
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        if (! nameArea.contains (e.getPosition()))
            return;

        if (e.mods.isPopupMenu())
        {
            grid.showChannelMenu (channelId, *this);
            return;
        }

        if (e.getNumberOfClicks() > 1)
        {
            grid.renameChannel (channelId);
            return;
        }

        longPress.start (e);

        if (auto* c = getChannel())
        {
            previewPitch = c->rootNote.load();
            grid.session.previewNote (channelId, previewPitch, true);
            pressed = true;
            repaint();
        }
    }

    void mouseDrag (const MouseEvent& e) override
    {
        longPress.drag (e);

        if (Touch::isTouch (e) && e.getDistanceFromDragStart() > Touch::dragThreshold)
            stopPreview();   // the finger is scrolling the grid
    }

    void mouseUp (const MouseEvent&) override
    {
        longPress.cancel();
        stopPreview();
    }

    void stopPreview()
    {
        if (previewPitch >= 0)
            grid.session.previewNote (channelId, previewPitch, false);

        previewPitch = -1;
        pressed = false;
        repaint();
    }

    void updateActivity()
    {
        if (auto* c = getChannel())
        {
            const auto a = c->activity.exchange (0.0f);
            const auto shown = jmax (a, displayedActivity * 0.75f);

            if (std::abs (shown - displayedActivity) > 0.01f)
            {
                displayedActivity = shown;
                repaint (0, 0, 18, getHeight());
            }
        }
    }

    StepGrid& grid;
    const int channelId;
    LongPress longPress;
    TextButton mute, keys, more, edit;
    Slider volume;
    Rectangle<int> nameArea;
    int previewPitch = -1;
    bool pressed = false;
    float displayedActivity = 0.0f;
};

//==============================================================================
StepGrid::StepGrid (Session& s, PluginWindowManager& w, SampleEditorManager& e, SamplerManager& sm)
    : session (s), pluginWindows (w), sampleEditors (e), sampler (sm),
      longPress ([this] (Point<int>)
      {
          if (touchPending)
          {
              touchPending = false;
              showNoteMenu (touchRow, touchStep);
          }
      })
{
    session.addListener (this);
    rebuildRows();
    startTimerHz (30);
}

StepGrid::~StepGrid()
{
    session.removeListener (this);
}

void StepGrid::sessionChanged (int changes)
{
    if ((changes & (Session::channelsChanged | Session::projectReplaced)) != 0)
        rebuildRows();

    if ((changes & (Session::patternContentChanged | Session::selectionChanged | Session::projectReplaced
                    | Session::channelsChanged)) != 0)
    {
        updateLayout (lastVisibleWidth);
        repaint();
    }
}

void StepGrid::timerCallback()
{
    for (auto* r : rows)
        r->updateActivity();
}

void StepGrid::rebuildRows()
{
    std::vector<int> ids;

    for (auto& c : session.getProject().channels)
        ids.push_back (c->id);

    if (ids == rowChannelIds)
    {
        for (auto* r : rows)
        {
            if (auto* c = r->getChannel())
            {
                r->mute.setToggleState (c->muted.load(), dontSendNotification);
                r->volume.setValue (c->volume.load(), dontSendNotification);
                r->volume.setColour (Slider::rotarySliderFillColourId, c->colour);
            }

            r->repaint();
        }

        return;
    }

    rows.clear();
    rowChannelIds = ids;

    for (auto& c : session.getProject().channels)
    {
        auto* r = rows.add (new RowHeader (*this, *c));
        addAndMakeVisible (r);
    }

    updateLayout (lastVisibleWidth);
}

void StepGrid::updateLayout (int visibleWidth)
{
    lastVisibleWidth = visibleWidth;
    const auto* p = session.getCurrentPattern();
    const auto steps = p != nullptr ? p->totalSteps() : 16;

    // phones: a narrower channel column leaves more room for the steps
    headerWidth = visibleWidth < 760 ? 214 : 276;

    cellWidth = jlimit (14, 44, (visibleWidth - headerWidth - 12) / jmax (1, steps));

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * rowHeight, headerWidth, rowHeight);

    const auto w = headerWidth + steps * cellWidth + 12;
    const auto h = rows.size() * rowHeight + 70;
    setSize (jmax (w, visibleWidth), h);
}

void StepGrid::setPlayPosition (double stepPosition)
{
    if (std::abs (stepPosition - playStep) > 1.0e-6)
    {
        const auto oldStep = (int) std::floor (playStep);
        const auto newStep = (int) std::floor (stepPosition);
        playStep = stepPosition;

        if (oldStep != newStep)
            repaint (headerWidth, 0, getWidth() - headerWidth, getHeight());
    }
}

bool StepGrid::getCellAt (Point<int> pos, int& row, int& step) const
{
    const auto* p = session.getCurrentPattern();

    if (p == nullptr || pos.x < headerWidth)
        return false;

    row = pos.y / rowHeight;
    step = (pos.x - headerWidth) / cellWidth;
    return isPositiveAndBelow (row, (int) rowChannelIds.size()) && isPositiveAndBelow (step, p->totalSteps());
}

void StepGrid::paint (Graphics& g)
{
    g.fillAll (Theme::background);

    auto* p = session.getCurrentPattern();

    if (p == nullptr)
        return;

    const auto steps = p->totalSteps();
    const auto gridRight = headerWidth + steps * cellWidth;
    const auto currentStep = playStep >= 0.0 ? (int) std::floor (playStep) : -1;
    auto& project = session.getProject();

    for (int row = 0; row < (int) rowChannelIds.size(); ++row)
    {
        auto* channel = project.findChannel (rowChannelIds[(size_t) row]);

        if (channel == nullptr)
            continue;

        const auto y = row * rowHeight;

        for (int step = 0; step < steps; ++step)
        {
            const auto beatIndex = step / p->stepsPerBeat;
            const auto x = headerWidth + step * cellWidth;
            auto cell = Rectangle<int> (x, y, cellWidth, rowHeight).reduced (1, 3).toFloat();

            auto bg = (beatIndex % 2 == 0) ? Theme::panelLight : Theme::panel.brighter (0.06f);

            if (step == currentStep)
                bg = bg.brighter (0.25f);

            g.setColour (bg);
            g.fillRoundedRectangle (cell, 2.5f);
        }

        // notes
        const auto root = channel->rootNote.load();

        for (auto& n : p->notes)
        {
            if (n.channelId != channel->id || n.step >= steps)
                continue;

            const auto x = headerWidth + n.step * cellWidth;

            if (n.length > 1)
            {
                const auto tailW = jmin (n.length, steps - n.step) * cellWidth;
                auto tail = Rectangle<int> (x, y, tailW, rowHeight).reduced (2, 12).toFloat();
                g.setColour (channel->colour.withAlpha (0.35f));
                g.fillRoundedRectangle (tail, 2.0f);
            }

            auto cell = Rectangle<int> (x, y, cellWidth, rowHeight).reduced (2, 4).toFloat();
            g.setColour (channel->colour.withMultipliedBrightness (0.45f + 0.55f * n.velocity));
            g.fillRoundedRectangle (cell, 3.0f);

            // velocity bar
            g.setColour (Colours::white.withAlpha (0.35f));
            g.fillRect (cell.withTrimmedTop (cell.getHeight() * (1.0f - n.velocity)).withWidth (2.0f).translated (1.5f, 0.0f));

            if (n.pitch != root)
            {
                g.setColour (Colours::black.withAlpha (0.6f));
                g.fillEllipse (cell.getRight() - 6.0f, cell.getY() + 2.0f, 4.0f, 4.0f);
            }
        }
    }

    // bar lines
    for (int step = 0; step <= steps; step += p->stepsPerBar())
    {
        const auto x = headerWidth + step * cellWidth;
        g.setColour (Theme::barLine);
        g.fillRect (x - 1, 0, 2, (int) rowChannelIds.size() * rowHeight);
    }

    // add-channel hint row
    auto hint = Rectangle<int> (6, (int) rowChannelIds.size() * rowHeight + 8, jmax (gridRight, 600) - 6, 46);
    g.setColour (Theme::outline);
    Path border;
    border.addRoundedRectangle (hint.toFloat(), 5.0f);
    const float dashes[] = { 5.0f, 4.0f };
    PathStrokeType (1.0f).createDashedStroke (border, border, dashes, 2);
    g.fillPath (border);
    g.setColour (Theme::dimText);
    g.setFont (Theme::font (13.0f));
    if (Touch::isMobile())
        g.drawText (rowChannelIds.empty() ? "Use \"+ Sample\" / \"+ Instrument\" above to add sounds"
                                          : "Tap a step to switch it on or off, swipe sideways to fill, hold a note for its velocity",
                    hint.reduced (8, 0), Justification::centred);
    else
        g.drawText (rowChannelIds.empty() ? "Drop WAV / MP3 files here, or use \"+ Sample\" / \"+ Instrument\" above to add sounds"
                                          : "Drop more WAV / MP3 files here to add channels",
                    hint, Justification::centred);
}

void StepGrid::applyPaint (int row, int step)
{
    auto* p = session.getCurrentPattern();
    auto* channel = session.getProject().findChannel (rowChannelIds[(size_t) row]);

    if (p == nullptr || channel == nullptr)
        return;

    const auto has = p->hasNoteAt (channel->id, step);

    if (paintAdds && ! has)
    {
        Note n;
        n.channelId = channel->id;
        n.step = step;
        n.length = 1;
        n.pitch = channel->rootNote.load();
        n.velocity = 0.8f;
        p->notes.push_back (n);
        session.patternChanged (p->id);

        if (! session.getEngine().isPlaying())
        {
            session.previewNote (channel->id, n.pitch, true);
            session.previewNote (channel->id, n.pitch, false);
        }
    }
    else if (! paintAdds && has)
    {
        p->removeNotesAt (channel->id, step);
        session.patternChanged (p->id);
    }
}

void StepGrid::startPainting (int row, int step, bool adds)
{
    painting = true;
    paintRow = row;
    lastPaintStep = step;
    paintAdds = adds;
    applyPaint (row, step);
}

void StepGrid::mouseDown (const MouseEvent& e)
{
    int row, step;
    touchPending = false;

    if (! getCellAt (e.getPosition(), row, step))
        return;

    auto* p = session.getCurrentPattern();
    const auto has = p->hasNoteAt (rowChannelIds[(size_t) row], step);

    if (Touch::isTouch (e))
    {
        touchPending = true;
        touchRow = row;
        touchStep = step;

        if (has)
            longPress.start (e);

        return;
    }

    startPainting (row, step, ! e.mods.isPopupMenu() && ! has);
}

void StepGrid::mouseDrag (const MouseEvent& e)
{
    longPress.drag (e);

    if (touchPending)
    {
        if (e.getDistanceFromDragStart() <= Touch::dragThreshold / 2)
            return;

        touchPending = false;

        // up / down: let the view scroll. Sideways: paint along the row.
        if (std::abs (e.getDistanceFromDragStartY()) >= std::abs (e.getDistanceFromDragStartX()))
            return;

        auto* p = session.getCurrentPattern();

        if (p == nullptr || ! isPositiveAndBelow (touchRow, (int) rowChannelIds.size()))
            return;

        setViewportIgnoreDragFlag (true);
        startPainting (touchRow, touchStep, ! p->hasNoteAt (rowChannelIds[(size_t) touchRow], touchStep));
    }

    if (! painting)
        return;

    int row, step;

    if (getCellAt ({ e.x, paintRow * rowHeight + 1 }, row, step) && step != lastPaintStep)
    {
        const auto from = jmin (step, lastPaintStep), to = jmax (step, lastPaintStep);

        for (int s = from; s <= to; ++s)
            if (s != lastPaintStep)
                applyPaint (row, s);

        lastPaintStep = step;
    }
}

void StepGrid::mouseUp (const MouseEvent&)
{
    longPress.cancel();

    if (touchPending && ! longPress.hasFired())
    {
        // a tap
        if (auto* p = session.getCurrentPattern(); p != nullptr && isPositiveAndBelow (touchRow, (int) rowChannelIds.size()))
            startPainting (touchRow, touchStep, ! p->hasNoteAt (rowChannelIds[(size_t) touchRow], touchStep));
    }

    touchPending = false;
    painting = false;
    paintRow = -1;
    setViewportIgnoreDragFlag (false);
}

void StepGrid::showNoteMenu (int row, int step)
{
    auto* p = session.getCurrentPattern();

    if (p == nullptr || ! isPositiveAndBelow (row, (int) rowChannelIds.size()))
        return;

    const auto channelId = rowChannelIds[(size_t) row];
    float velocity = -1.0f;

    for (auto& n : p->notes)
        if (n.channelId == channelId && n.step == step)
            velocity = n.velocity;

    if (velocity < 0.0f)
        return;

    const auto patternId = p->id;
    auto setVelocity = [this, patternId, channelId, step] (float v)
    {
        if (auto* pat = session.getProject().findPattern (patternId))
        {
            for (auto& n : pat->notes)
                if (n.channelId == channelId && n.step == step)
                    n.velocity = v;

            session.patternChanged (patternId);
        }
    };

    PopupMenu m;
    m.addSectionHeader ("Velocity (strength)");

    for (auto percent : { 100, 80, 60, 40, 20 })
    {
        const auto v = (float) percent / 100.0f;
        m.addItem (String (percent) + " %", true, std::abs (velocity - v) < 0.026f, [setVelocity, v] { setVelocity (v); });
    }

    m.addSeparator();
    m.addItem ("Delete note", [this, patternId, channelId, step]
    {
        if (auto* pat = session.getProject().findPattern (patternId))
        {
            pat->removeNotesAt (channelId, step);
            session.patternChanged (patternId);
        }
    });

    m.showMenuAsync (Touch::menuAtPointer (PopupMenu::Options().withTargetComponent (this)));
}

void StepGrid::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    int row, step;
    auto* p = session.getCurrentPattern();

    if (p != nullptr && (e.mods.isCtrlDown() || e.mods.isAltDown()) && getCellAt (e.getPosition(), row, step)
         && p->hasNoteAt (rowChannelIds[(size_t) row], step))
    {
        for (auto& n : p->notes)
            if (n.channelId == rowChannelIds[(size_t) row] && n.step == step)
                n.velocity = jlimit (0.05f, 1.0f, n.velocity + (wheel.deltaY > 0 ? 0.05f : -0.05f));

        session.patternChanged (p->id);
        return;
    }

    Component::mouseWheelMove (e, wheel);
}

void StepGrid::renameChannel (int channelId)
{
    auto* c = session.getProject().findChannel (channelId);

    if (c == nullptr)
        return;

    AppFiles::askForText ("Rename channel", {}, c->name, "OK", this, [this, channelId] (const String& name)
    {
        if (auto* ch = session.getProject().findChannel (channelId))
        {
            ch->name = name;
            session.channelSettingsChanged();
        }
    });
}

void StepGrid::editChannel (int channelId)
{
    auto* c = session.getProject().findChannel (channelId);

    if (c == nullptr)
        return;

    if (c->isSample())
        sampleEditors.open (channelId);
    else if (c->plugin != nullptr)
        pluginWindows.show (*c->plugin, c->name);
}

void StepGrid::showChannelMenu (int channelId, Component& target)
{
    auto* c = session.getProject().findChannel (channelId);

    if (c == nullptr)
        return;

    PopupMenu m;
    m.addItem ("Settings & effects...", [this, channelId, t = SafePointer<Component> (&target)]
    {
        if (t != nullptr)
            ChannelSettings::show (session, pluginWindows, channelId, *t, &sampleEditors);
    });

    if (c->isSample())
    {
        m.addItem ("Edit sample (waveform, VST effects)...", c->sample != nullptr, false,
                   [this, channelId] { sampleEditors.open (channelId); });
        m.addItem ("Open in Sampler (cut into samples, AI stems)...", c->sample != nullptr, false, [this, channelId]
        {
            if (auto* ch = session.getProject().findChannel (channelId); ch != nullptr && ch->sample != nullptr)
                sampler.openSample (ch->sample, ch->name);
        });
    }

    if (c->isPlugin())
        m.addItem ("Open plug-in window", c->plugin != nullptr, false, [this, channelId]
        {
            if (auto* ch = session.getProject().findChannel (channelId); ch != nullptr && ch->plugin != nullptr)
                pluginWindows.show (*ch->plugin, ch->name);
        });

    m.addItem ("Piano roll", [this, channelId] { if (onOpenPianoRoll) onOpenPianoRoll (channelId); });
    m.addItem ("Rename...", [this, channelId] { renameChannel (channelId); });
    m.addSeparator();
    m.addItem ("Move up", [this, channelId] { session.moveChannel (channelId, -1); });
    m.addItem ("Move down", [this, channelId] { session.moveChannel (channelId, 1); });
    m.addSeparator();
    m.addItem ("Clear notes in this pattern", [this, channelId]
    {
        if (auto* p = session.getCurrentPattern())
        {
            p->notes.erase (std::remove_if (p->notes.begin(), p->notes.end(),
                                            [channelId] (const Note& n) { return n.channelId == channelId; }),
                            p->notes.end());
            session.patternChanged (p->id);
        }
    });
    m.addItem ("Delete channel", [safe = SafePointer<StepGrid> (this), channelId]
    {
        MessageManager::callAsync ([safe, channelId]
        {
            if (safe != nullptr)
                safe->session.removeChannel (channelId);
        });
    });

    m.showMenuAsync (Touch::menuAtPointer (PopupMenu::Options().withTargetComponent (target)));
}
