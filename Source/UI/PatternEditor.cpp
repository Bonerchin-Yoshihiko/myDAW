#include "UI/PatternEditor.h"
#include "Audio/Channel.h"
#include "UI/Sampler.h"

PatternEditor::PatternEditor (Session& s, PluginWindowManager& w, SampleEditorManager& e, SamplerManager& sampler)
    : session (s), pluginWindows (w), grid (s, w, e, sampler), pianoRoll (s)
{
    title.setText ("PATTERN", dontSendNotification);
    title.setFont (Theme::font (12.0f, true));
    title.setColour (Label::textColourId, Theme::dimText);
    addAndMakeVisible (title);

    nameEditor.setFont (Theme::font (15.0f, true));
    nameEditor.setTooltip ("Pattern name");
    nameEditor.onReturnKey = nameEditor.onFocusLost = [this]
    {
        auto* p = session.getCurrentPattern();
        const auto name = nameEditor.getText().trim();

        if (p != nullptr && name.isNotEmpty() && name != p->name)
        {
            p->name = name;
            session.patternChanged (p->id);
            session.notify (Session::patternListChanged | Session::arrangementChanged);
        }
    };
    addAndMakeVisible (nameEditor);

    colourButton.setTooltip ("Pattern colour");
    colourButton.onClick = [this] { showColourMenu(); };
    addAndMakeVisible (colourButton);

    for (auto* l : { &barsLabel, &signatureLabel, &slashLabel, &stepsLabel })
    {
        l->setFont (Theme::font (12.0f));
        l->setColour (Label::textColourId, Theme::dimText);
        l->setJustificationType (Justification::centredRight);
        addAndMakeVisible (l);
    }

    slashLabel.setJustificationType (Justification::centred);
    slashLabel.setBorderSize ({});

    for (int i : { 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 24, 32 })
        barsBox.addItem (String (i), i);

    for (int i = 1; i <= 16; ++i)
        beatsBox.addItem (String (i), i);

    for (int i : { 2, 4, 8, 16 })
        unitBox.addItem (String (i), i);

    for (int i : { 1, 2, 3, 4, 6, 8 })
        stepsBox.addItem (String (i), i);

    barsBox.setTooltip ("Number of bars in this pattern");
    beatsBox.setTooltip ("Beats per bar (time signature numerator) - any length you like, e.g. 3, 5, 7");
    unitBox.setTooltip ("Beat unit (time signature denominator)");
    stepsBox.setTooltip ("Grid steps per beat (4 = 16th notes when the beat unit is 4, 3 = triplets)");

    for (auto* b : { &barsBox, &beatsBox, &unitBox, &stepsBox })
    {
        b->onChange = [this] { applyLengthChange(); };
        addAndMakeVisible (b);
    }

    addSampleButton.setTooltip ("Import WAV / MP3 / AIFF / FLAC files as new channels");
    addInstrumentButton.setTooltip ("Add a VST3 instrument plug-in as a new channel");
    addSampleButton.onClick = [this]
    {
        auto& settings = session.getPluginManager().getSettings();
        File dir (settings.getValue ("sampleDir", File::getSpecialLocation (File::userMusicDirectory).getFullPathName()));
        chooser = std::make_unique<FileChooser> ("Import samples", dir, SampleData::getWildcard());
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles
                                  | FileBrowserComponent::canSelectMultipleItems,
                              [this] (const FileChooser& fc) { addSamples (fc.getResults()); });
    };
    addInstrumentButton.onClick = [this] { showInstrumentMenu(); };
    samplerButton.setTooltip ("Sampler: cut many samples out of one audio file, or split a song into vocals / drums / bass / guitar ... with AI");
    samplerButton.setColour (TextButton::buttonColourId, Theme::accent.withAlpha (0.3f));
    samplerButton.onClick = [&sampler] { sampler.show(); };
    addAndMakeVisible (samplerButton);
    backButton.onClick = [this] { showStepGrid(); };
    addAndMakeVisible (addSampleButton);
    addAndMakeVisible (addInstrumentButton);
    addChildComponent (backButton);

    pianoRollLabel.setFont (Theme::font (13.0f, true));
    addChildComponent (pianoRollLabel);

    grid.onOpenPianoRoll = [this] (int channelId) { showPianoRoll (channelId); };

    viewport.setScrollBarsShown (true, true);
    viewport.setScrollBarThickness (10);
    viewport.setViewedComponent (&grid, false);
    addAndMakeVisible (viewport);

    session.addListener (this);
    updateHeader();
    startTimerHz (30);
}

PatternEditor::~PatternEditor()
{
    session.removeListener (this);
}

void PatternEditor::paint (Graphics& g)
{
    g.fillAll (Theme::background);
    g.setColour (Theme::panel);
    g.fillRect (getLocalBounds().removeFromTop (headerHeight));
    g.setColour (Theme::outline);
    g.drawHorizontalLine (headerHeight, 0.0f, (float) getWidth());
}

void PatternEditor::paintOverChildren (Graphics& g)
{
    if (dragHighlight)
    {
        g.setColour (Theme::accent.withAlpha (0.12f));
        g.fillRect (viewport.getBounds());
        g.setColour (Theme::accent);
        g.drawRect (viewport.getBounds(), 2);
    }
}

void PatternEditor::resized()
{
    // the pattern settings need ~640 px, the buttons ~310 px (+ ~240 px in piano-roll mode);
    // when they do not fit side by side, the buttons get a second row
    const auto leftWidth = 640, rightWidth = 310 + (showingPianoRoll ? 240 : 0);
    const auto twoRows = getWidth() < leftWidth + rightWidth + 16;
    headerHeight = twoRows ? 76 : 40;

    auto r = getLocalBounds();
    auto headerArea = r.removeFromTop (headerHeight);
    auto header = headerArea.removeFromTop (40).reduced (8, 6);
    auto buttonsRow = twoRows ? headerArea.reduced (8, 2).withTrimmedBottom (4) : header;

    title.setBounds (header.removeFromLeft (64));
    colourButton.setBounds (header.removeFromLeft (22).reduced (0, 3));
    header.removeFromLeft (6);
    nameEditor.setBounds (header.removeFromLeft (jlimit (90, 150, header.getWidth() - 470)));
    header.removeFromLeft (8);

    barsLabel.setBounds (header.removeFromLeft (36));
    barsBox.setBounds (header.removeFromLeft (56));
    signatureLabel.setBounds (header.removeFromLeft (42));
    beatsBox.setBounds (header.removeFromLeft (52));
    slashLabel.setBounds (header.removeFromLeft (12));
    unitBox.setBounds (header.removeFromLeft (52));
    stepsLabel.setBounds (header.removeFromLeft (76));
    stepsBox.setBounds (header.removeFromLeft (50));

    if (! twoRows)
        buttonsRow = header;   // what is left of the single row

    samplerButton.setBounds (buttonsRow.removeFromRight (100));
    buttonsRow.removeFromRight (6);
    addInstrumentButton.setBounds (buttonsRow.removeFromRight (104));
    buttonsRow.removeFromRight (6);
    addSampleButton.setBounds (buttonsRow.removeFromRight (82));
    buttonsRow.removeFromRight (10);

    if (twoRows)
    {
        // piano-roll controls on the left of the second row
        backButton.setBounds (buttonsRow.removeFromLeft (96));
        buttonsRow.removeFromLeft (10);
        pianoRollLabel.setBounds (buttonsRow);
    }
    else
    {
        backButton.setBounds (buttonsRow.removeFromRight (96));
        buttonsRow.removeFromRight (6);
        pianoRollLabel.setBounds (buttonsRow.removeFromRight (jmax (0, buttonsRow.getWidth() - 10)));
    }

    viewport.setBounds (r);
    const auto visibleWidth = viewport.getMaximumVisibleWidth();
    grid.updateLayout (visibleWidth);
    pianoRoll.updateLayout (visibleWidth);
    repaint();
}

void PatternEditor::sessionChanged (int changes)
{
    if ((changes & (Session::selectionChanged | Session::patternContentChanged | Session::patternListChanged
                    | Session::projectReplaced)) != 0)
        updateHeader();

    if ((changes & (Session::channelsChanged | Session::projectReplaced)) != 0 && showingPianoRoll
         && session.getProject().findChannel (pianoRoll.getChannelId()) == nullptr)
        showStepGrid();
    else if (showingPianoRoll)
        updateHeader();
}

void PatternEditor::updateHeader()
{
    auto* p = session.getCurrentPattern();

    if (p == nullptr)
        return;

    if (! nameEditor.hasKeyboardFocus (true))
        nameEditor.setText (p->name, false);

    colourButton.setColour (TextButton::buttonColourId, p->colour);
    barsBox.setText (String (p->bars), dontSendNotification);
    beatsBox.setSelectedId (p->beatsPerBar, dontSendNotification);
    unitBox.setSelectedId (p->beatUnit, dontSendNotification);
    stepsBox.setSelectedId (p->stepsPerBeat, dontSendNotification);

    if (showingPianoRoll)
    {
        if (auto* c = session.getProject().findChannel (pianoRoll.getChannelId()))
        {
            pianoRollLabel.setText ("Piano roll:  " + c->name, dontSendNotification);
            pianoRollLabel.setColour (Label::textColourId, c->colour);
        }
    }
}

void PatternEditor::applyLengthChange()
{
    auto* p = session.getCurrentPattern();

    if (p == nullptr)
        return;

    const auto bars = barsBox.getText().getIntValue();

    p->bars = jlimit (1, 256, bars > 0 ? bars : p->bars);
    p->beatsPerBar = jmax (1, beatsBox.getSelectedId());
    p->beatUnit = jmax (1, unitBox.getSelectedId());
    p->changeStepsPerBeat (jmax (1, stepsBox.getSelectedId()));

    session.patternChanged (p->id);
    session.notify (Session::patternListChanged | Session::arrangementChanged);
    resized();
}

void PatternEditor::showPianoRoll (int channelId)
{
    showingPianoRoll = true;
    resized();   // the header may need a second row for the piano-roll controls
    pianoRoll.setChannel (channelId);
    viewport.setViewedComponent (&pianoRoll, false);
    pianoRoll.updateLayout (viewport.getMaximumVisibleWidth());

    const auto pitch = pianoRoll.getSuggestedScrollPitch();
    viewport.setViewPosition (0, jmax (0, pianoRoll.getYForPitch (pitch) - viewport.getHeight() / 2));

    backButton.setVisible (true);
    pianoRollLabel.setVisible (true);
    updateHeader();
}

void PatternEditor::showStepGrid()
{
    showingPianoRoll = false;
    resized();
    viewport.setViewedComponent (&grid, false);
    grid.updateLayout (viewport.getMaximumVisibleWidth());
    backButton.setVisible (false);
    pianoRollLabel.setVisible (false);
}

double PatternEditor::getPatternPlayStep() const
{
    auto& engine = session.getEngine();
    auto* p = session.getCurrentPattern();

    if (p == nullptr || ! engine.isPlaying())
        return -1.0;

    const auto pos = engine.getPositionInBeats();
    const auto stepLength = p->stepLengthInQuarters();

    if (! engine.isSongSequence())
        return pos / stepLength;

    for (auto& c : session.getProject().clips)
        if (c.patternId == p->id && pos >= c.start && pos < c.start + p->lengthInQuarters())
            return (pos - c.start) / stepLength;

    return -1.0;
}

void PatternEditor::timerCallback()
{
    const auto step = getPatternPlayStep();

    if (showingPianoRoll)
        pianoRoll.setPlayPosition (step);
    else
        grid.setPlayPosition (step);
}

bool PatternEditor::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files)
        if (SampleData::isSupportedFile (File (f)))
            return true;

    return false;
}

void PatternEditor::filesDropped (const StringArray& files, int, int)
{
    dragHighlight = false;
    repaint();

    Array<File> list;

    for (auto& f : files)
        list.add (File (f));

    addSamples (list);
}

void PatternEditor::addSamples (const Array<File>& files)
{
    StringArray errors;

    for (auto& f : files)
    {
        if (! SampleData::isSupportedFile (f))
            continue;

        String error;

        if (session.addSampleChannel (f, error) == nullptr)
            errors.add (error);
        else
            session.getPluginManager().getSettings().setValue ("sampleDir", f.getParentDirectory().getFullPathName());
    }

    if (! errors.isEmpty() && onError)
        onError (errors.joinIntoString ("\n"));
}

void PatternEditor::showInstrumentMenu()
{
    PopupMenu m;
    auto descriptions = std::make_shared<std::vector<PluginDescription>> (session.getPluginManager().fillMenu (m, true, 1));

    m.showMenuAsync (PopupMenu::Options().withTargetComponent (addInstrumentButton),
                     [safe = SafePointer<PatternEditor> (this), descriptions] (int result)
    {
        if (safe == nullptr || result <= 0 || result > (int) descriptions->size())
            return;

        String error;

        if (auto channel = safe->session.addPluginChannel ((*descriptions)[(size_t) result - 1], error))
        {
            if (channel->plugin != nullptr)
                safe->pluginWindows.show (*channel->plugin, channel->name);
        }
        else if (safe->onError)
        {
            safe->onError (error);
        }
    });
}

void PatternEditor::showColourMenu()
{
    PopupMenu m;

    for (int i = 0; i < 10; ++i)
        m.addColouredItem (i + 1, "Colour " + String (i + 1), Palette::get (i), true, false);

    m.showMenuAsync (PopupMenu::Options().withTargetComponent (colourButton),
                     [safe = SafePointer<PatternEditor> (this)] (int result)
    {
        if (safe == nullptr || result <= 0)
            return;

        if (auto* p = safe->session.getCurrentPattern())
        {
            p->colour = Palette::get (result - 1);
            safe->session.markDirty();
            safe->session.notify (Session::patternListChanged | Session::arrangementChanged | Session::selectionChanged);
        }
    });
}
