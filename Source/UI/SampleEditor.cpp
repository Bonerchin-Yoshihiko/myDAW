#include "UI/SampleEditor.h"
#include "Audio/Channel.h"
#include "Audio/SampleEditing.h"
#include "UI/EffectRow.h"

namespace
{
    String formatTime (double seconds)
    {
        return String (seconds, seconds < 10.0 ? 3 : 2) + " s";
    }
}

//==============================================================================
WaveformView::WaveformView()
{
    setWantsKeyboardFocus (false);
}

void WaveformView::setSample (std::shared_ptr<const SampleData> s, Colour c)
{
    const auto lengthChanged = sample == nullptr || s == nullptr || s->getLength() != sample->getLength();
    const auto wasShowingEverything = sample != nullptr && visible.getStart() <= 0.5
                                       && visible.getEnd() >= sample->getLength() - 0.5;
    sample = std::move (s);
    colour = c;
    cacheValid = false;

    if (sample == nullptr)
        return;

    const auto length = sample->getLength();
    selection = Range<int> (0, length).getIntersectionWith (selection);
    cursor = jlimit (0, length, cursor);

    if (lengthChanged && (wasShowingEverything || visible.getEnd() > length || visible.isEmpty()))
    {
        visible = { 0.0, (double) length };

        if (onViewChanged)
            onViewChanged();
    }

    repaint();
}

void WaveformView::setSelection (Range<int> r)
{
    if (sample == nullptr)
        return;

    selection = Range<int> (0, sample->getLength()).getIntersectionWith (r);

    if (! selection.isEmpty())
        cursor = selection.getStart();

    repaint();

    if (onSelectionChanged)
        onSelectionChanged();
}

void WaveformView::setPlayPosition (double frame)
{
    if (std::abs (frame - playPosition) > 0.5)
    {
        playPosition = frame;
        repaint();
    }
}

void WaveformView::setVisibleRange (Range<double> r)
{
    if (sample == nullptr)
        return;

    const auto length = (double) sample->getLength();
    const auto width = jlimit (jmin (32.0, length), length, r.getLength());
    const auto start = jlimit (0.0, length - width, r.getStart());
    visible = { start, start + width };
    cacheValid = false;
    repaint();

    if (onViewChanged)
        onViewChanged();
}

void WaveformView::zoom (double factor, int anchorX)
{
    const auto anchor = xToFrame ((float) anchorX);
    const auto newLength = visible.getLength() / factor;
    const auto proportion = getWidth() > 0 ? (double) anchorX / getWidth() : 0.5;
    setVisibleRange ({ anchor - newLength * proportion, anchor - newLength * proportion + newLength });
}

void WaveformView::zoomToFit()
{
    if (sample != nullptr)
        setVisibleRange ({ 0.0, (double) sample->getLength() });
}

double WaveformView::xToFrame (float x) const
{
    return visible.getStart() + (double) x / jmax (1, getWidth()) * visible.getLength();
}

float WaveformView::frameToX (double frame) const
{
    return (float) ((frame - visible.getStart()) / jmax (1.0e-9, visible.getLength()) * getWidth());
}

int WaveformView::clampFrame (double frame) const
{
    return sample == nullptr ? 0 : jlimit (0, sample->getLength(), roundToInt (frame));
}

void WaveformView::rebuildCache()
{
    cacheValid = true;
    const auto w = getWidth();

    for (int ch = 0; ch < 2; ++ch)
    {
        mins[ch].assign ((size_t) jmax (0, w), 0.0f);
        maxs[ch].assign ((size_t) jmax (0, w), 0.0f);
    }

    if (sample == nullptr || w <= 0)
        return;

    const auto& b = sample->buffer;
    const auto length = b.getNumSamples();
    const auto framesPerPixel = visible.getLength() / w;

    for (int ch = 0; ch < jmin (2, b.getNumChannels()); ++ch)
    {
        const auto* data = b.getReadPointer (ch);

        for (int x = 0; x < w; ++x)
        {
            const auto from = visible.getStart() + x * framesPerPixel;
            auto a = jlimit (0, length - 1, (int) std::floor (from));
            auto e = jlimit (a + 1, length, (int) std::ceil (from + framesPerPixel));
            auto lo = data[a], hi = data[a];

            for (int i = a; i < e; ++i)
            {
                lo = jmin (lo, data[i]);
                hi = jmax (hi, data[i]);
            }

            mins[ch][(size_t) x] = lo;
            maxs[ch][(size_t) x] = hi;
        }
    }
}

void WaveformView::paint (Graphics& g)
{
    g.fillAll (Theme::panel);

    if (sample == nullptr)
    {
        g.setColour (Theme::dimText);
        g.setFont (Theme::font (14.0f));
        g.drawText ("The sample could not be loaded.", getLocalBounds(), Justification::centred);
        return;
    }

    if (! cacheValid || (int) mins[0].size() != getWidth())
        rebuildCache();

    const auto numChannels = jmin (2, sample->buffer.getNumChannels());
    auto area = getLocalBounds().withTrimmedTop (rulerHeight);

    // selection
    if (! selection.isEmpty())
    {
        const auto x1 = frameToX (selection.getStart()), x2 = frameToX (selection.getEnd());
        g.setColour (Theme::accent.withAlpha (0.18f));
        g.fillRect (Rectangle<float> (x1, (float) area.getY(), jmax (1.0f, x2 - x1), (float) area.getHeight()));
        g.setColour (Theme::accent.withAlpha (0.8f));
        g.drawVerticalLine (roundToInt (x1), (float) area.getY(), (float) area.getBottom());
        g.drawVerticalLine (roundToInt (x2), (float) area.getY(), (float) area.getBottom());
    }

    // waveform lanes
    const auto laneHeight = area.getHeight() / numChannels;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto lane = area.removeFromTop (laneHeight).reduced (0, 4);
        const auto mid = (float) lane.getCentreY();
        const auto half = lane.getHeight() * 0.5f;

        g.setColour (Theme::gridLine);
        g.drawHorizontalLine (roundToInt (mid), 0.0f, (float) getWidth());
        g.setColour (Theme::gridLine.withAlpha (0.5f));
        g.drawHorizontalLine (roundToInt (mid - half * 0.5f), 0.0f, (float) getWidth());
        g.drawHorizontalLine (roundToInt (mid + half * 0.5f), 0.0f, (float) getWidth());

        g.setColour (colour);

        for (int x = 0; x < (int) mins[ch].size(); ++x)
        {
            const auto top = mid - jlimit (-1.0f, 1.0f, maxs[ch][(size_t) x]) * half;
            const auto bottom = mid - jlimit (-1.0f, 1.0f, mins[ch][(size_t) x]) * half;
            g.drawVerticalLine (x, top, jmax (top + 1.0f, bottom));
        }

        if (numChannels == 2)
        {
            g.setColour (Theme::dimText);
            g.setFont (Theme::font (11.0f));
            g.drawText (ch == 0 ? "L" : "R", 4, lane.getY(), 20, 16, Justification::centredLeft);
        }
    }

    // ruler
    auto ruler = getLocalBounds().removeFromTop (rulerHeight);
    g.setColour (Theme::panelLight);
    g.fillRect (ruler);

    const auto secondsVisible = visible.getLength() / sample->sampleRate;
    const double steps[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0 };
    double step = 60.0;

    for (auto s : steps)
    {
        if (secondsVisible / s <= jmax (2.0, getWidth() / 90.0))
        {
            step = s;
            break;
        }
    }

    g.setFont (Theme::font (10.5f));

    for (auto t = std::ceil (visible.getStart() / sample->sampleRate / step) * step;
         t * sample->sampleRate <= visible.getEnd(); t += step)
    {
        const auto x = roundToInt (frameToX (t * sample->sampleRate));
        g.setColour (Theme::barLine);
        g.drawVerticalLine (x, (float) ruler.getY() + 10.0f, (float) ruler.getBottom());
        g.setColour (Theme::dimText);
        g.drawText (String (t, step < 0.01 ? 3 : (step < 0.1 ? 2 : (step < 1.0 ? 1 : 0))) + "s",
                    x + 3, ruler.getY(), 60, ruler.getHeight(), Justification::centredLeft);
    }

    // cursor and playhead
    if (selection.isEmpty())
    {
        g.setColour (Theme::text.withAlpha (0.7f));
        g.drawVerticalLine (roundToInt (frameToX (cursor)), (float) rulerHeight, (float) getHeight());
    }

    if (playPosition >= 0.0)
    {
        g.setColour (Theme::playhead);
        const auto x = frameToX (playPosition);
        g.drawLine (x, 0.0f, x, (float) getHeight(), 1.5f);
    }

    g.setColour (Theme::outline);
    g.drawRect (getLocalBounds());
}

void WaveformView::mouseDown (const MouseEvent& e)
{
    if (sample == nullptr)
        return;

    const auto frame = clampFrame (xToFrame ((float) e.x));

    if (e.mods.isShiftDown())
    {
        const auto other = selection.isEmpty() ? cursor
                                               : (std::abs (frame - selection.getStart()) > std::abs (frame - selection.getEnd())
                                                      ? selection.getStart() : selection.getEnd());
        dragAnchor = other;
        setSelection (Range<int>::between (other, frame));
        return;
    }

    dragAnchor = frame;
    cursor = frame;
    selection = {};
    repaint();

    if (onSelectionChanged)
        onSelectionChanged();
}

void WaveformView::mouseDrag (const MouseEvent& e)
{
    if (sample == nullptr || e.mouseWasClicked())
        return;

    const auto frame = clampFrame (xToFrame ((float) jlimit (0, getWidth(), e.x)));
    selection = Range<int>::between (dragAnchor, frame);
    repaint();

    if (onSelectionChanged)
        onSelectionChanged();
}

void WaveformView::mouseUp (const MouseEvent&)
{
    // a tiny drag is just a click
    if (! selection.isEmpty() && frameToX (selection.getEnd()) - frameToX (selection.getStart()) < 2.0f)
    {
        selection = {};
        repaint();

        if (onSelectionChanged)
            onSelectionChanged();
    }
}

void WaveformView::mouseDoubleClick (const MouseEvent&)
{
    if (sample != nullptr)
        setSelection ({ 0, sample->getLength() });
}

void WaveformView::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    if (sample == nullptr)
        return;

    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        zoom (wheel.deltaY > 0 ? 1.25 : 0.8, e.x);
        return;
    }

    const auto delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
    setVisibleRange (visible.movedToStartAt (visible.getStart() - delta * visible.getLength() * 0.5));
}

//==============================================================================
SampleEditor::SampleEditor (Session& s, PluginWindowManager& w, int id)
    : session (s), pluginWindows (w), channelId (id)
{
    identity = session.getProject().findChannel (channelId);

    title.setFont (Theme::font (16.0f, true));
    infoLabel.setFont (Theme::font (12.0f));
    infoLabel.setColour (Label::textColourId, Theme::dimText);
    selectionLabel.setFont (Font (FontOptions (Font::getDefaultMonospacedFontName(), 12.5f, Font::plain)));
    selectionLabel.setColour (Label::textColourId, Theme::text);
    statusLabel.setFont (Theme::font (12.0f));
    statusLabel.setColour (Label::textColourId, Theme::dimText);
    effectsHint.setFont (Theme::font (11.5f));
    effectsHint.setColour (Label::textColourId, Theme::dimText);
    effectsHint.setText ("Preview plays through these effects. \"Apply\" bakes them into the selection (or the whole sample).",
                         dontSendNotification);

    for (auto* l : { &editLabel, &effectsLabel, &tailLabel })
    {
        l->setFont (Theme::font (12.0f, true));
        l->setColour (Label::textColourId, Theme::dimText);
    }

    for (auto* c : std::initializer_list<Component*> { &title, &infoLabel, &selectionLabel, &statusLabel, &editLabel,
                                                       &effectsLabel, &effectsHint, &tailLabel, &undoButton, &redoButton,
                                                       &revertButton, &exportButton, &playButton, &loopToggle,
                                                       &hearEffectsToggle, &selectAllButton, &zoomOutButton, &zoomInButton,
                                                       &fitButton, &waveform, &scrollBar, &addEffectButton,
                                                       &applyEffectsButton, &tailBox })
        addAndMakeVisible (c);

    undoButton.setTooltip ("Undo (Ctrl+Z)");
    redoButton.setTooltip ("Redo (Ctrl+Shift+Z)");
    revertButton.setTooltip ("Reload the file you originally imported (can be undone)");
    exportButton.setTooltip ("Save the edited sound as a WAV file");
    playButton.setTooltip ("Play the selection, or from the cursor (Space)");
    hearEffectsToggle.setTooltip ("Play the preview through the effect rack below");
    hearEffectsToggle.setToggleState (true, dontSendNotification);

    undoButton.onClick = [this] { if (auto* c = getChannel()) { session.getEngine().stopSamplePreview(); session.undoSampleEdit (*c); } };
    redoButton.onClick = [this] { if (auto* c = getChannel()) { session.getEngine().stopSamplePreview(); session.redoSampleEdit (*c); } };
    revertButton.onClick = [this]
    {
        if (auto* c = getChannel())
        {
            session.getEngine().stopSamplePreview();
            String error;

            if (session.revertSampleToOriginal (*c, error))
                setStatus ("Reverted to " + c->sampleFile.getFileName() + " (Undo to go back).");
            else
                setStatus (error);
        }
    };
    exportButton.onClick = [this] { exportWav(); };
    playButton.onClick = [this] { togglePreview(); };
    selectAllButton.onClick = [this] { if (auto* c = getChannel(); c != nullptr && c->sample != nullptr) waveform.setSelection ({ 0, c->sample->getLength() }); };
    zoomInButton.onClick = [this] { waveform.zoom (1.5, waveform.getWidth() / 2); };
    zoomOutButton.onClick = [this] { waveform.zoom (1.0 / 1.5, waveform.getWidth() / 2); };
    fitButton.onClick = [this] { waveform.zoomToFit(); };

    struct EditDef { const char* name; const char* tip; EditFunction fn; bool needsSelection; };
    const EditDef edits[] = {
        { "Trim", "Keep only the selection", [] (const SampleData& d, Range<int> r) { return SampleEditing::trimTo (d, r); }, true },
        { "Delete", "Remove the selection (Del)", [] (const SampleData& d, Range<int> r) { return SampleEditing::remove (d, r); }, true },
        { "Silence", "Replace the selection with silence", [] (const SampleData& d, Range<int> r) { return SampleEditing::silence (d, r); }, true },
        { "Fade in", "Fade in over the selection (or the whole sample)", [] (const SampleData& d, Range<int> r) { return SampleEditing::fadeIn (d, r); }, false },
        { "Fade out", "Fade out over the selection (or the whole sample)", [] (const SampleData& d, Range<int> r) { return SampleEditing::fadeOut (d, r); }, false },
        { "Normalize", "Raise the level so the peak reaches -0.1 dB", [] (const SampleData& d, Range<int> r) { return SampleEditing::normalize (d, r); }, false },
        { "-3 dB", "Make quieter", [] (const SampleData& d, Range<int> r) { return SampleEditing::applyGain (d, r, -3.0f); }, false },
        { "+3 dB", "Make louder", [] (const SampleData& d, Range<int> r) { return SampleEditing::applyGain (d, r, 3.0f); }, false },
        { "Reverse", "Play backwards", [] (const SampleData& d, Range<int> r) { return SampleEditing::reverse (d, r); }, false },
    };

    for (auto& def : edits)
    {
        auto* b = editButtons.add (new TextButton (def.name));
        b->setTooltip (def.tip);
        b->onClick = [this, name = String (def.name), fn = def.fn, needs = def.needsSelection] { applyEdit (name, fn, needs); };
        addAndMakeVisible (b);
    }

    tailBox.addItem ("Auto (up to 5 s)", 1);
    tailBox.addItem ("Off", 2);
    tailBox.setSelectedId (1, dontSendNotification);
    tailBox.setTooltip ("Let reverbs and delays ring out after the selection. Auto trims the silence afterwards.");

    addEffectButton.onClick = [this] { showAddEffectMenu(); };
    applyEffectsButton.onClick = [this] { applyEffects(); };
    applyEffectsButton.setColour (TextButton::buttonColourId, Theme::accent.withAlpha (0.35f));

    waveform.onSelectionChanged = [this] { updateSelectionLabel(); updateButtons(); };
    waveform.onViewChanged = [this]
    {
        if (auto* c = getChannel(); c != nullptr && c->sample != nullptr)
        {
            scrollBar.setRangeLimits (0.0, c->sample->getLength(), dontSendNotification);
            const auto v = waveform.getVisibleRange();
            scrollBar.setCurrentRange (v.getStart(), v.getLength(), dontSendNotification);
        }
    };
    scrollBar.setAutoHide (false);
    scrollBar.addListener (this);

    session.getEngine().addAuxiliaryChain (&rack);
    session.addListener (this);
    setWantsKeyboardFocus (true);

    refreshFromChannel (true);
    rebuildEffectRows();
    setStatus ("Drag to select  |  Shift+click extends  |  Double-click selects all  |  Ctrl+wheel zooms  |  Space plays");
    setSize (1000, 640);
    startTimerHz (30);
}

SampleEditor::~SampleEditor()
{
    session.removeListener (this);
    session.getEngine().stopSamplePreview();

    for (auto& slot : rack.slots)
        pluginWindows.close (slot->plugin.get());

    session.getEngine().removeAuxiliaryChain (&rack);

    {
        const ScopedLock sl (session.getEngine().getLock());
        rack.slots.clear();
    }
}

Channel* SampleEditor::getChannel() const
{
    auto* c = session.getProject().findChannel (channelId);
    return c == identity ? c : nullptr;
}

void SampleEditor::setStatus (const String& text)
{
    statusLabel.setText (text, dontSendNotification);
}

//==============================================================================
void SampleEditor::paint (Graphics& g)
{
    g.fillAll (Theme::background);
    g.setColour (Theme::panel);
    g.fillRect (getLocalBounds().removeFromTop (44));
    g.setColour (Theme::outline);
    g.drawHorizontalLine (44, 0.0f, (float) getWidth());

    // effects panel background
    auto fx = effectsLabel.getBounds().getUnion (applyEffectsButton.getBounds()).withLeft (0).withRight (getWidth()).expanded (0, 6);
    g.setColour (Theme::panel);
    g.fillRect (fx);
}

void SampleEditor::resized()
{
    auto r = getLocalBounds();

    auto header = r.removeFromTop (44).reduced (10, 7);
    exportButton.setBounds (header.removeFromRight (104));
    header.removeFromRight (6);
    revertButton.setBounds (header.removeFromRight (130));
    header.removeFromRight (6);
    redoButton.setBounds (header.removeFromRight (56));
    header.removeFromRight (4);
    undoButton.setBounds (header.removeFromRight (56));
    header.removeFromRight (10);
    title.setBounds (header.removeFromLeft (jmin (300, header.getWidth() / 2)));
    infoLabel.setBounds (header);

    r.removeFromTop (4);
    statusLabel.setBounds (r.removeFromBottom (24).reduced (10, 0));

    // effect rack at the bottom
    const auto rackHeight = 30 + 22 + effectRows.size() * 32 + 40;
    auto fx = r.removeFromBottom (rackHeight).reduced (10, 6);
    auto fxTitle = fx.removeFromTop (24);
    effectsLabel.setBounds (fxTitle.removeFromLeft (100));
    effectsHint.setBounds (fxTitle);
    fx.removeFromTop (4);

    for (auto* row : effectRows)
    {
        row->setBounds (fx.removeFromTop (30).withWidth (jmin (fx.getWidth(), 520)));
        fx.removeFromTop (2);
    }

    auto fxButtons = fx.removeFromTop (34).reduced (0, 3);
    addEffectButton.setBounds (fxButtons.removeFromLeft (120));
    fxButtons.removeFromLeft (16);
    tailLabel.setBounds (fxButtons.removeFromLeft (36));
    tailBox.setBounds (fxButtons.removeFromLeft (150));
    fxButtons.removeFromLeft (16);
    applyEffectsButton.setBounds (fxButtons.removeFromLeft (220));

    // edit buttons
    auto editRow = r.removeFromBottom (40).reduced (10, 5);
    editLabel.setBounds (editRow.removeFromLeft (44));
    const auto buttonWidth = jmin (90, editRow.getWidth() / jmax (1, editButtons.size()));

    for (auto* b : editButtons)
        b->setBounds (editRow.removeFromLeft (buttonWidth).reduced (2, 0));

    // transport row
    auto transportRow = r.removeFromBottom (40).reduced (10, 5);
    playButton.setBounds (transportRow.removeFromLeft (40));
    transportRow.removeFromLeft (8);
    loopToggle.setBounds (transportRow.removeFromLeft (64));
    hearEffectsToggle.setBounds (transportRow.removeFromLeft (110));
    fitButton.setBounds (transportRow.removeFromRight (44));
    transportRow.removeFromRight (4);
    zoomInButton.setBounds (transportRow.removeFromRight (28));
    transportRow.removeFromRight (2);
    zoomOutButton.setBounds (transportRow.removeFromRight (28));
    transportRow.removeFromRight (8);
    selectAllButton.setBounds (transportRow.removeFromRight (80));
    transportRow.removeFromRight (8);
    selectionLabel.setBounds (transportRow);

    auto wave = r.reduced (10, 0);
    scrollBar.setBounds (wave.removeFromBottom (10));
    waveform.setBounds (wave);
}

//==============================================================================
void SampleEditor::sessionChanged (int changes)
{
    if ((changes & Session::projectReplaced) != 0 || getChannel() == nullptr)
    {
        if (onCloseRequested)
            onCloseRequested();

        return;
    }

    if ((changes & (Session::sampleChanged | Session::channelsChanged)) != 0)
        refreshFromChannel (false);
}

void SampleEditor::refreshFromChannel (bool resetView)
{
    auto* c = getChannel();

    if (c == nullptr)
        return;

    title.setText (c->name, dontSendNotification);
    title.setColour (Label::textColourId, c->colour);

    if (auto* w = findParentComponentOfClass<DocumentWindow>())
        w->setName ("Sample Editor - " + c->name);

    if (c->sample != nullptr)
    {
        const auto& d = *c->sample;
        infoLabel.setText (formatTime (d.getLength() / d.sampleRate) + "   " + String (roundToInt (d.sampleRate)) + " Hz   "
                               + (d.buffer.getNumChannels() > 1 ? "stereo" : "mono")
                               + (c->sampleModified ? "   (edited - saved with the project)" : "   " + c->sampleFile.getFileName()),
                           dontSendNotification);
    }
    else
    {
        infoLabel.setText ("Missing: " + c->sampleFile.getFullPathName(), dontSendNotification);
    }

    waveform.setSample (c->sample, c->colour);

    if (resetView)
        waveform.zoomToFit();
    else if (waveform.onViewChanged)
        waveform.onViewChanged();

    updateSelectionLabel();
    updateButtons();
}

void SampleEditor::updateButtons()
{
    auto* c = getChannel();
    const auto hasSample = c != nullptr && c->sample != nullptr;
    const auto hasSelection = ! waveform.getSelection().isEmpty();

    undoButton.setEnabled (c != nullptr && ! c->undoHistory.empty());
    redoButton.setEnabled (c != nullptr && ! c->redoHistory.empty());
    revertButton.setEnabled (c != nullptr && (c->originalSampleFile.existsAsFile() || c->sampleFile.existsAsFile()));
    exportButton.setEnabled (hasSample);
    playButton.setEnabled (hasSample);

    for (int i = 0; i < editButtons.size(); ++i)
        editButtons[i]->setEnabled (hasSample && (i > 2 || hasSelection));

    applyEffectsButton.setEnabled (hasSample && ! rack.isEmpty());
    applyEffectsButton.setButtonText (hasSelection ? "Apply effects to selection" : "Apply effects to whole sample");
}

void SampleEditor::updateSelectionLabel()
{
    auto* c = getChannel();

    if (c == nullptr || c->sample == nullptr)
    {
        selectionLabel.setText ({}, dontSendNotification);
        return;
    }

    const auto sr = c->sample->sampleRate;
    const auto sel = waveform.getSelection();

    if (sel.isEmpty())
        selectionLabel.setText ("Cursor " + formatTime (waveform.getCursor() / sr) + "   (no selection: edits apply to the whole sample)",
                                dontSendNotification);
    else
        selectionLabel.setText ("Selection " + formatTime (sel.getStart() / sr) + " - " + formatTime (sel.getEnd() / sr)
                                    + "   (" + formatTime (sel.getLength() / sr) + ")",
                                dontSendNotification);
}

void SampleEditor::scrollBarMoved (ScrollBar*, double newRangeStart)
{
    waveform.setVisibleRange (waveform.getVisibleRange().movedToStartAt (newRangeStart));
}

void SampleEditor::timerCallback()
{
    auto& engine = session.getEngine();
    const auto playing = engine.isSamplePreviewPlaying();
    playButton.setToggleState (playing, dontSendNotification);
    waveform.setPlayPosition (playing ? engine.getSamplePreviewPosition() : -1.0);
}

bool SampleEditor::keyPressed (const KeyPress& key)
{
    const auto cmd = ModifierKeys::commandModifier;

    if (key == KeyPress::spaceKey)                                             { togglePreview(); return true; }
    if (key == KeyPress ('z', cmd, 0))                                         { undoButton.triggerClick(); return true; }
    if (key == KeyPress ('z', cmd | ModifierKeys::shiftModifier, 0) || key == KeyPress ('y', cmd, 0)) { redoButton.triggerClick(); return true; }
    if (key == KeyPress ('a', cmd, 0))                                         { selectAllButton.triggerClick(); return true; }
    if (key == KeyPress::deleteKey || key == KeyPress::backspaceKey)           { editButtons[1]->triggerClick(); return true; }

    return false;
}

//==============================================================================
void SampleEditor::applyEdit (const String& name, EditFunction fn, bool needsSelection)
{
    auto* c = getChannel();

    if (c == nullptr || c->sample == nullptr)
        return;

    const auto sel = waveform.getSelection();

    if (needsSelection && sel.isEmpty())
    {
        setStatus ("Select a part of the waveform first (drag with the mouse).");
        return;
    }

    auto result = fn (*c->sample, sel);

    if (result == nullptr)
    {
        setStatus (name + ": not possible (a sample cannot be empty).");
        return;
    }

    session.getEngine().stopSamplePreview();
    const auto lengthChanged = result->getLength() != c->sample->getLength();
    session.commitSampleEdit (*c, std::move (result));

    if (name == "Trim")
    {
        waveform.setSelection ({});
        waveform.zoomToFit();
    }
    else if (lengthChanged)
    {
        waveform.setSelection ({});
    }

    setStatus (name + " applied.  (Undo with Ctrl+Z)");
}

void SampleEditor::togglePreview()
{
    if (session.getEngine().isSamplePreviewPlaying())
        session.getEngine().stopSamplePreview();
    else
        startPreview();
}

void SampleEditor::startPreview()
{
    auto* c = getChannel();

    if (c == nullptr || c->sample == nullptr)
        return;

    auto range = waveform.getSelection();

    if (range.isEmpty())
        range = { waveform.getCursor() < c->sample->getLength() - 2 ? waveform.getCursor() : 0, c->sample->getLength() };

    session.getEngine().startSamplePreview (c->sample, range, hearEffectsToggle.getToggleState() ? &rack : nullptr,
                                            loopToggle.getToggleState());
}

//==============================================================================
void SampleEditor::rebuildEffectRows()
{
    effectRows.clear();

    for (size_t i = 0; i < rack.slots.size(); ++i)
    {
        auto& slot = *rack.slots[i];
        auto* row = new EffectRow (slot.plugin->getName(), slot.bypassed.load());
        auto* plugin = slot.plugin.get();
        const auto index = (int) i;

        row->edit.onClick = [this, plugin] { pluginWindows.show (*plugin, plugin->getName() + " (sample editor)"); };
        row->bypass.onClick = [this, row, index]
        {
            if (isPositiveAndBelow (index, (int) rack.slots.size()))
                rack.slots[(size_t) index]->bypassed = ! row->bypass.getToggleState();
        };
        row->remove.onClick = [safe = SafePointer<SampleEditor> (this), index]
        {
            MessageManager::callAsync ([safe, index] { if (safe != nullptr) safe->removeEffect (index); });
        };

        addAndMakeVisible (row);
        effectRows.add (row);
    }

    updateButtons();
    resized();
    repaint();
}

void SampleEditor::showAddEffectMenu()
{
    PopupMenu m;
    auto descriptions = std::make_shared<std::vector<PluginDescription>> (session.getPluginManager().fillMenu (m, false, 1));

    m.showMenuAsync (PopupMenu::Options().withTargetComponent (addEffectButton),
                     [safe = SafePointer<SampleEditor> (this), descriptions] (int result)
    {
        if (safe == nullptr || result <= 0 || result > (int) descriptions->size())
            return;

        auto& self = *safe;
        auto& engine = self.session.getEngine();
        const auto& desc = (*descriptions)[(size_t) result - 1];
        String error;
        auto instance = self.session.getPluginManager().createInstance (desc, engine.getSampleRate(), engine.getBlockSize(), error);

        if (instance == nullptr)
        {
            self.setStatus (error);
            return;
        }

        PluginHelpers::configureStereo (*instance, false);
        engine.preparePlugin (*instance);

        auto slot = std::make_unique<EffectChain::Slot>();
        slot->plugin = std::move (instance);
        slot->description = desc;
        auto* plugin = slot->plugin.get();

        {
            const ScopedLock sl (engine.getLock());
            self.rack.slots.push_back (std::move (slot));
            engine.auxiliaryChainChanged();
        }

        self.rebuildEffectRows();
        self.pluginWindows.show (*plugin, plugin->getName() + " (sample editor)");
        self.setStatus ("Added " + plugin->getName() + ". Press Play to hear it, then \"Apply\" to bake it into the sound.");
    });
}

void SampleEditor::removeEffect (int index)
{
    if (! isPositiveAndBelow (index, (int) rack.slots.size()))
        return;

    pluginWindows.close (rack.slots[(size_t) index]->plugin.get());
    std::unique_ptr<EffectChain::Slot> removed;

    {
        const ScopedLock sl (session.getEngine().getLock());
        removed = std::move (rack.slots[(size_t) index]);
        rack.slots.erase (rack.slots.begin() + index);
        session.getEngine().auxiliaryChainChanged();
    }

    removed.reset();
    rebuildEffectRows();
}

void SampleEditor::applyEffects()
{
    auto* c = getChannel();

    if (c == nullptr || c->sample == nullptr)
        return;

    const auto active = std::count_if (rack.slots.begin(), rack.slots.end(),
                                       [] (const std::unique_ptr<EffectChain::Slot>& s) { return ! s->bypassed.load(); });

    if (active == 0)
    {
        setStatus ("Add an effect (and switch it on) first.");
        return;
    }

    auto& engine = session.getEngine();
    engine.stopSamplePreview();

    const auto& data = *c->sample;
    const auto range = SampleEditing::clampRange (data, waveform.getSelection());
    const auto autoTail = tailBox.getSelectedId() == 1;
    const auto tailSamples = autoTail ? (int) (data.sampleRate * 5.0) : 0;

    AudioBuffer<float> section (data.buffer.getNumChannels(), range.getLength());

    for (int ch = 0; ch < section.getNumChannels(); ++ch)
        section.copyFrom (ch, 0, data.buffer, ch, range.getStart(), range.getLength());

    AudioBuffer<float> processed;
    MouseCursor::showWaitCursor();
    const auto result = engine.renderThroughChain (rack, section, data.sampleRate, tailSamples, processed);
    MouseCursor::hideWaitCursor();

    if (result.failed())
    {
        setStatus (result.getErrorMessage());
        return;
    }

    if (autoTail)
        processed.setSize (2, SampleEditing::findEndOfSound (processed, range.getLength()), true);

    session.commitSampleEdit (*c, SampleEditing::replaceWithProcessed (data, range, processed));

    // the effects are now part of the sound: bypass them so the preview doesn't apply them twice
    for (auto& s : rack.slots)
        s->bypassed = true;

    rebuildEffectRows();
    setStatus ("Applied " + String ((int) active) + (active == 1 ? " effect" : " effects")
               + ". They are now switched off so they are not heard twice. (Undo with Ctrl+Z)");
}

void SampleEditor::exportWav()
{
    auto* c = getChannel();

    if (c == nullptr || c->sample == nullptr)
        return;

    auto initial = c->sampleFile.getSiblingFile (File::createLegalFileName (c->name) + " (edited).wav");
    chooser = std::make_unique<FileChooser> ("Export sample", initial, "*.wav");
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles
                              | FileBrowserComponent::warnAboutOverwriting,
                          [safe = SafePointer<SampleEditor> (this)] (const FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == File())
            return;

        auto* ch = safe->getChannel();

        if (ch == nullptr || ch->sample == nullptr)
            return;

        const auto file = fc.getResult().withFileExtension (".wav");
        const auto r = SampleEditing::writeWav (*ch->sample, file);
        safe->setStatus (r.wasOk() ? "Exported " + file.getFullPathName() : r.getErrorMessage());
    });
}

//==============================================================================
class SampleEditorManager::Window : public DocumentWindow
{
public:
    Window (SampleEditorManager& m, int id)
        : DocumentWindow ("Sample Editor", Theme::background, DocumentWindow::allButtons),
          manager (m), channelId (id)
    {
        auto* editor = new SampleEditor (m.session, m.pluginWindows, id);
        editor->onCloseRequested = [this] { requestClose(); };
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (true, false);
        setResizeLimits (720, 480, 4000, 3000);
        centreWithSize (getWidth(), getHeight());

        if (auto* c = m.session.getProject().findChannel (id))
            setName ("Sample Editor - " + c->name);

        setVisible (true);
        editor->grabKeyboardFocus();
    }

    void closeButtonPressed() override { requestClose(); }

    void requestClose()
    {
        MessageManager::callAsync ([safe = SafePointer<Window> (this)]
        {
            if (safe != nullptr)
                safe->manager.close (safe->channelId);
        });
    }

    SampleEditorManager& manager;
    const int channelId;
};

SampleEditorManager::SampleEditorManager (Session& s, PluginWindowManager& w) : session (s), pluginWindows (w) {}

SampleEditorManager::~SampleEditorManager()
{
    closeAll();
}

void SampleEditorManager::open (int channelId)
{
    auto* c = session.getProject().findChannel (channelId);

    if (c == nullptr || ! c->isSample())
        return;

    for (auto& w : windows)
    {
        if (w->channelId == channelId)
        {
            w->setVisible (true);
            w->toFront (true);
            return;
        }
    }

    windows.push_back (std::make_unique<Window> (*this, channelId));
}

void SampleEditorManager::close (int channelId)
{
    windows.erase (std::remove_if (windows.begin(), windows.end(),
                                   [channelId] (const std::unique_ptr<Window>& w) { return w->channelId == channelId; }),
                   windows.end());
}

void SampleEditorManager::closeAll()
{
    windows.clear();
}
