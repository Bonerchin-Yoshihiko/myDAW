#include "UI/Sampler.h"
#include "Audio/Channel.h"
#include "Audio/SampleEditing.h"
#include "Audio/Slicing.h"
#include "UI/WindowPlacement.h"

namespace
{
    String formatTime (double seconds)
    {
        if (seconds >= 60.0)
            return String ((int) seconds / 60) + ":" + String (std::fmod (seconds, 60.0), 2).paddedLeft ('0', 5);

        return String (seconds, seconds < 10.0 ? 3 : 2) + " s";
    }

    const char* const stemOrder[] = { "vocals", "drums", "bass", "guitar", "piano", "other", "novocals" };

    //==============================================================================
    class StemOptionsPanel : public Component
    {
    public:
        StemOptionsPanel (const SamplerComponent::SeparationSettings& initial, bool hasSelection)
        {
            title.setText ("AI STEM SEPARATION", dontSendNotification);
            title.setFont (Theme::font (13.0f, true));
            title.setColour (Label::textColourId, Theme::dimText);
            addAndMakeVisible (title);

            modelLabel.setText ("Model", dontSendNotification);
            stemsLabel.setText ("Extract", dontSendNotification);

            for (auto* l : { &modelLabel, &stemsLabel })
            {
                l->setFont (Theme::font (12.0f));
                l->setColour (Label::textColourId, Theme::dimText);
                addAndMakeVisible (l);
            }

            modelBox.addItem (Stems::getModelInfo (Stems::ModelType::sixStems).displayName, 1);
            modelBox.addItem (Stems::getModelInfo (Stems::ModelType::fourStems).displayName, 2);
            modelBox.setSelectedId (initial.model == Stems::ModelType::fourStems ? 2 : 1, dontSendNotification);
            modelBox.onChange = [this] { updateState(); };
            addAndMakeVisible (modelBox);

            for (auto* id : stemOrder)
            {
                auto* t = stemToggles.add (new ToggleButton (String (id) == "novocals" ? String ("No vocals (instrumental / karaoke)")
                                                                                       : Stems::getStemDisplayName (id)));
                t->setComponentID (id);
                t->setToggleState (initial.stems.contains (id), dontSendNotification);
                t->onClick = [this] { updateState(); };
                addAndMakeVisible (t);
            }

            selectionOnly.setButtonText ("Only the selected part (much faster)");
            selectionOnly.setToggleState (initial.selectionOnly && hasSelection, dontSendNotification);
            selectionOnly.setEnabled (hasSelection);
            addChannels.setButtonText ("Also add the stems to the project as channels");
            addChannels.setToggleState (initial.addAsChannels, dontSendNotification);
            addAndMakeVisible (selectionOnly);
            addAndMakeVisible (addChannels);

            modelStatus.setFont (Theme::font (12.0f));
            modelStatus.setMinimumHorizontalScale (0.8f);
            addAndMakeVisible (modelStatus);
            note.setFont (Theme::font (11.5f));
            note.setColour (Label::textColourId, Theme::dimText);
            note.setText ("Runs on your computer (no upload). A full song can take several minutes.", dontSendNotification);
            addAndMakeVisible (note);

            loadModelButton.onClick = [this] { if (onLoadModel) onLoadModel(); };
            loadModelButton.setTooltip ("Use a model file you downloaded yourself (ggml-model-htdemucs-...-f16.bin)");
            addAndMakeVisible (loadModelButton);

            startButton.setColour (TextButton::buttonColourId, Theme::accent.withAlpha (0.4f));
            startButton.onClick = [this]
            {
                if (onStart)
                    onStart (getSettings());

                if (auto* box = findParentComponentOfClass<CallOutBox>())
                    box->dismiss();
            };
            addAndMakeVisible (startButton);

            updateState();
            setSize (420, 400);
        }

        SamplerComponent::SeparationSettings getSettings() const
        {
            SamplerComponent::SeparationSettings s;
            s.model = modelBox.getSelectedId() == 2 ? Stems::ModelType::fourStems : Stems::ModelType::sixStems;
            s.stems.clear();

            for (auto* t : stemToggles)
                if (t->getToggleState() && t->isEnabled())
                    s.stems.add (t->getComponentID());

            s.selectionOnly = selectionOnly.getToggleState();
            s.addAsChannels = addChannels.getToggleState();
            return s;
        }

        void updateState()
        {
            const auto settings = getSettings();
            const auto& info = Stems::getModelInfo (settings.model);

            for (auto* t : stemToggles)
            {
                const auto id = t->getComponentID();
                t->setEnabled (id == "novocals" || info.stems.contains (id));
            }

            if (Stems::isModelInstalled (settings.model))
            {
                modelStatus.setText ("Model installed.", dontSendNotification);
                modelStatus.setColour (Label::textColourId, Colour (0xff66bb6a));
            }
            else
            {
                modelStatus.setText ("The model (" + String (info.approxSizeMB) + " MB) will be downloaded once.", dontSendNotification);
                modelStatus.setColour (Label::textColourId, Theme::accentWarm);
            }

            startButton.setEnabled (! getSettings().stems.isEmpty());
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (12, 8);
            title.setBounds (r.removeFromTop (24));
            auto modelRow = r.removeFromTop (28);
            modelLabel.setBounds (modelRow.removeFromLeft (60));
            modelBox.setBounds (modelRow);
            r.removeFromTop (6);
            stemsLabel.setBounds (r.removeFromTop (20));

            for (int i = 0; i < stemToggles.size(); ++i)
            {
                if (i == 6)
                {
                    stemToggles[i]->setBounds (r.removeFromTop (26));
                    continue;
                }

                if (i % 2 == 0)
                {
                    auto row = r.removeFromTop (26);
                    stemToggles[i]->setBounds (row.removeFromLeft (row.getWidth() / 2));

                    if (i + 1 < 6)
                        stemToggles[i + 1]->setBounds (row);
                }
            }

            r.removeFromTop (6);
            selectionOnly.setBounds (r.removeFromTop (26));
            addChannels.setBounds (r.removeFromTop (26));
            r.removeFromTop (4);
            auto statusRow = r.removeFromTop (26);
            loadModelButton.setBounds (statusRow.removeFromRight (130).reduced (0, 2));
            modelStatus.setBounds (statusRow);
            note.setBounds (r.removeFromTop (22));
            r.removeFromTop (4);
            startButton.setBounds (r.removeFromTop (34).withSizeKeepingCentre (160, 32));
        }

        std::function<void (SamplerComponent::SeparationSettings)> onStart;
        std::function<void()> onLoadModel;

    private:
        Label title, modelLabel, stemsLabel, modelStatus, note;
        ComboBox modelBox;
        OwnedArray<ToggleButton> stemToggles;
        ToggleButton selectionOnly, addChannels;
        TextButton loadModelButton { "Load model file..." }, startButton { "Separate" };
    };
}

//==============================================================================
SamplerComponent::SamplerComponent (Session& s) : session (s)
{
    title.setFont (Theme::font (16.0f, true));
    title.setText ("SAMPLER", dontSendNotification);
    title.setColour (Label::textColourId, Theme::accent);
    infoLabel.setFont (Theme::font (12.0f));
    infoLabel.setColour (Label::textColourId, Theme::dimText);
    selectionLabel.setFont (Font (FontOptions (Font::getDefaultMonospacedFontName(), 12.5f, Font::plain)));
    statusLabel.setFont (Theme::font (12.0f));
    statusLabel.setColour (Label::textColourId, Theme::dimText);
    jobLabel.setFont (Theme::font (12.0f));

    for (auto* l : { &sourceLabel, &slicesLabel })
    {
        l->setFont (Theme::font (12.0f, true));
        l->setColour (Label::textColourId, Theme::dimText);
    }

    for (auto* c : std::initializer_list<Component*> { &title, &infoLabel, &sourceLabel, &selectionLabel, &statusLabel,
                                                       &slicesLabel, &openButton, &separateButton, &waveform, &scrollBar,
                                                       &playButton, &loopToggle, &selectAllButton, &zoomOutButton,
                                                       &zoomInButton, &fitButton, &addSliceButton, &autoSliceButton,
                                                       &clearSlicesButton, &sampleSelectionButton, &sampleAllButton })
        addAndMakeVisible (c);

    addChildComponent (jobLabel);
    addChildComponent (cancelButton);

    openButton.onClick = [this] { openFileChooser(); };
    separateButton.setColour (TextButton::buttonColourId, Theme::accent.withAlpha (0.3f));
    separateButton.setTooltip ("Split the sound into vocals, drums, bass, guitar, piano and other with AI");
    separateButton.onClick = [this] { showSeparationOptions(); };
    cancelButton.onClick = [this] { cancelJobs(); };

    playButton.setTooltip ("Play the selection, or from the cursor (Space)");
    playButton.onClick = [this] { togglePreview(); };
    selectAllButton.onClick = [this] { if (auto* src = getCurrent()) waveform.setSelection ({ 0, src->data->getLength() }); };
    zoomInButton.onClick = [this] { waveform.zoom (1.5, waveform.getWidth() / 2); };
    zoomOutButton.onClick = [this] { waveform.zoom (1.0 / 1.5, waveform.getWidth() / 2); };
    fitButton.onClick = [this] { waveform.zoomToFit(); };

    addSliceButton.setTooltip ("Mark the selection as a slice (shown with a number)");
    autoSliceButton.setTooltip ("Cut the selection (or everything) automatically: at hits, into equal parts, or on the beat");
    clearSlicesButton.setTooltip ("Remove all slice markers");
    sampleSelectionButton.setTooltip ("Make a new sample channel from the selection (or the whole sound)");
    sampleAllButton.setTooltip ("Make one sample channel per slice");
    sampleSelectionButton.setColour (TextButton::buttonColourId, Theme::accent.withAlpha (0.35f));
    sampleAllButton.setColour (TextButton::buttonColourId, Theme::accent.withAlpha (0.35f));

    addSliceButton.onClick = [this] { addSlice(); };
    autoSliceButton.onClick = [this] { showAutoSliceMenu(); };
    clearSlicesButton.onClick = [this] { slices.clear(); updateRegions(); updateButtons(); };
    sampleSelectionButton.onClick = [this] { sampleSelection(); };
    sampleAllButton.onClick = [this] { sampleAllSlices(); };

    waveform.onSelectionChanged = [this] { updateSelectionLabel(); updateButtons(); };
    waveform.onRegionMenu = [this] (int index) { showSliceMenu (index); };
    waveform.onViewChanged = [this]
    {
        if (auto* src = getCurrent())
        {
            scrollBar.setRangeLimits (0.0, src->data->getLength(), dontSendNotification);
            const auto v = waveform.getVisibleRange();
            scrollBar.setCurrentRange (v.getStart(), v.getLength(), dontSendNotification);
        }
    };
    scrollBar.setAutoHide (false);
    scrollBar.addListener (this);

    setWantsKeyboardFocus (true);
    setStatus ("Open an audio file (or drop one here). Drag on the waveform to select a part, then \"Sample selection\".");
    updateButtons();
    setSize (1100, 680);
    startTimerHz (30);
}

SamplerComponent::~SamplerComponent()
{
    session.getEngine().stopSamplePreview();
    separationJob.reset();
    downloadJob.reset();
}

//==============================================================================
bool SamplerComponent::loadFile (const File& file)
{
    String error;
    auto data = SampleData::load (session.getFormatManager(), file, error);

    if (data == nullptr)
    {
        setStatus (error);
        AlertWindow::showAsync (MessageBoxOptions().withIconType (MessageBoxIconType::WarningIcon)
                                                   .withTitle ("Sampler").withMessage (error).withButton ("OK"),
                                nullptr);
        return false;
    }

    loadSample (data, file.getFileNameWithoutExtension());
    session.getPluginManager().getSettings().setValue ("samplerDir", file.getParentDirectory().getFullPathName());
    return true;
}

void SamplerComponent::loadSample (std::shared_ptr<const SampleData> data, const String& name)
{
    if (separationJob != nullptr)
        separationJob->cancel();   // its result belongs to the previous sound and will be discarded

    session.getEngine().stopSamplePreview();
    sourceName = name;
    slices.clear();
    sampleCounter = 0;
    waveform.setSelection ({});

    std::vector<Source> list;
    list.push_back ({ "original", "Original", std::move (data), 0.0 });
    setSources (std::move (list), 0);
    waveform.zoomToFit();

    if (onTitleChanged)
        onTitleChanged ("Sampler - " + sourceName);

    setStatus ("Drag on the waveform to select a part, then \"Sample selection\". Or try \"AI: Separate stems...\".");
}

void SamplerComponent::setSources (std::vector<Source> list, int selectIndex)
{
    const auto* previous = getCurrent();
    const auto previousSelection = waveform.getSelection();
    Range<double> selectionTime;
    Range<double> viewTime;

    if (previous != nullptr)
    {
        selectionTime = { frameToTime (*previous, previousSelection.getStart()), frameToTime (*previous, previousSelection.getEnd()) };
        const auto v = waveform.getVisibleRange();
        viewTime = { frameToTime (*previous, v.getStart()), frameToTime (*previous, v.getEnd()) };
    }

    sources = std::move (list);
    current = jlimit (0, jmax (0, (int) sources.size() - 1), selectIndex);
    rebuildSourceButtons();

    if (auto* src = getCurrent())
    {
        waveform.setSample (src->data, current == 0 ? Theme::accent : Palette::get (current + 1));

        if (previous != nullptr && ! previousSelection.isEmpty())
            waveform.setSelection ({ roundToInt (timeToFrame (*src, selectionTime.getStart())),
                                     roundToInt (timeToFrame (*src, selectionTime.getEnd())) });

        if (previous != nullptr && viewTime.getLength() > 0.0)
            waveform.setVisibleRange ({ timeToFrame (*src, viewTime.getStart()), timeToFrame (*src, viewTime.getEnd()) });

        const auto& d = *src->data;
        infoLabel.setText (sourceName + (current > 0 ? " - " + src->name : String()) + "   "
                               + formatTime (d.getLength() / d.sampleRate) + "   "
                               + String (roundToInt (d.sampleRate)) + " Hz   " + (d.buffer.getNumChannels() > 1 ? "stereo" : "mono")
                               + (src->offsetSeconds > 0.0 ? "   (starts at " + formatTime (src->offsetSeconds) + " of the original)" : String()),
                           dontSendNotification);
    }

    updateRegions();
    updateSelectionLabel();
    updateButtons();
    resized();
}

void SamplerComponent::selectSource (int index)
{
    if (! isPositiveAndBelow (index, (int) sources.size()) || index == current)
        return;

    const auto wasPlaying = session.getEngine().isSamplePreviewPlaying();
    session.getEngine().stopSamplePreview();
    auto list = sources;
    setSources (std::move (list), index);

    if (wasPlaying)
        togglePreview();   // keep listening, now to the other stem
}

void SamplerComponent::rebuildSourceButtons()
{
    sourceButtons.clear();

    for (int i = 0; i < (int) sources.size(); ++i)
    {
        auto* b = sourceButtons.add (new TextButton (sources[(size_t) i].name));
        b->setClickingTogglesState (false);
        b->setToggleState (i == current, dontSendNotification);
        b->setColour (TextButton::buttonOnColourId, i == 0 ? Theme::accent : Palette::get (i + 1));
        b->onClick = [this, i] { selectSource (i); };
        addAndMakeVisible (b);
    }
}

const SamplerComponent::Source* SamplerComponent::getCurrent() const
{
    return isPositiveAndBelow (current, (int) sources.size()) ? &sources[(size_t) current] : nullptr;
}

double SamplerComponent::frameToTime (const Source& s, double frame) const
{
    return frame / s.data->sampleRate + s.offsetSeconds;
}

double SamplerComponent::timeToFrame (const Source& s, double seconds) const
{
    return jlimit (0.0, (double) s.data->getLength(), (seconds - s.offsetSeconds) * s.data->sampleRate);
}

void SamplerComponent::updateRegions()
{
    std::vector<Range<int>> regions;

    if (auto* src = getCurrent())
        for (auto& sl : slices)
            regions.push_back ({ roundToInt (timeToFrame (*src, sl.getStart())), roundToInt (timeToFrame (*src, sl.getEnd())) });

    waveform.setRegions (std::move (regions));
}

void SamplerComponent::updateSelectionLabel()
{
    auto* src = getCurrent();

    if (src == nullptr)
    {
        selectionLabel.setText ({}, dontSendNotification);
        return;
    }

    const auto sel = waveform.getSelection();
    const auto sr = src->data->sampleRate;

    if (sel.isEmpty())
        selectionLabel.setText ("Cursor " + formatTime (frameToTime (*src, waveform.getCursor())) + "   (no selection)",
                                dontSendNotification);
    else
        selectionLabel.setText ("Selection " + formatTime (frameToTime (*src, sel.getStart())) + " - "
                                    + formatTime (frameToTime (*src, sel.getEnd())) + "   (" + formatTime (sel.getLength() / sr) + ")",
                                dontSendNotification);
}

void SamplerComponent::updateButtons()
{
    const auto has = hasSource();
    const auto hasSelection = ! waveform.getSelection().isEmpty();
    const auto busy = separationJob != nullptr || downloadJob != nullptr;

    for (auto* c : std::initializer_list<Component*> { &playButton, &selectAllButton, &zoomInButton, &zoomOutButton,
                                                       &fitButton, &autoSliceButton, &sampleSelectionButton })
        c->setEnabled (has);

    separateButton.setEnabled (has && ! busy);
    addSliceButton.setEnabled (has && hasSelection);
    clearSlicesButton.setEnabled (! slices.empty());
    sampleAllButton.setEnabled (has && ! slices.empty());
    sampleAllButton.setButtonText (slices.empty() ? String ("Sample all slices") : "Sample all " + String ((int) slices.size()) + " slices");
    sampleSelectionButton.setButtonText (hasSelection ? "Sample selection" : "Sample whole sound");

    jobLabel.setVisible (busy);
    cancelButton.setVisible (busy);
}

void SamplerComponent::setStatus (const String& text)
{
    statusLabel.setText (text, dontSendNotification);
}

//==============================================================================
void SamplerComponent::paint (Graphics& g)
{
    g.fillAll (Theme::background);
    g.setColour (Theme::panel);
    g.fillRect (getLocalBounds().removeFromTop (44));
    g.setColour (Theme::outline);
    g.drawHorizontalLine (44, 0.0f, (float) getWidth());

    if (! hasSource())
    {
        auto r = waveform.getBounds();
        g.setColour (Theme::panel);
        g.fillRect (r);
        g.setColour (Theme::outline);
        Path border;
        border.addRoundedRectangle (r.toFloat().reduced (8.0f), 8.0f);
        const float dashes[] = { 6.0f, 5.0f };
        PathStrokeType (1.5f).createDashedStroke (border, border, dashes, 2);
        g.fillPath (border);
        g.setColour (Theme::dimText);
        g.setFont (Theme::font (16.0f));
        g.drawFittedText ("Drop an audio file here (WAV, MP3, AIFF, FLAC, OGG)\nor click \"Open audio file...\"",
                          r, Justification::centred, 3);
    }

    // AI job progress bar
    if (jobLabel.isVisible())
    {
        auto bar = jobLabel.getBounds().withWidth (jmin (360, jobLabel.getWidth())).removeFromBottom (6);
        g.setColour (Theme::panelLighter);
        g.fillRoundedRectangle (bar.toFloat(), 3.0f);

        if (jobProgress >= 0.0)
        {
            g.setColour (Theme::accent);
            g.fillRoundedRectangle (bar.toFloat().withWidth ((float) (bar.getWidth() * jlimit (0.0, 1.0, jobProgress))), 3.0f);
        }
    }
}

void SamplerComponent::resized()
{
    auto r = getLocalBounds();

    auto header = r.removeFromTop (44).reduced (10, 7);
    openButton.setBounds (header.removeFromRight (140));
    title.setBounds (header.removeFromLeft (100));
    infoLabel.setBounds (header);

    auto sourceRow = r.removeFromTop (42).reduced (10, 6);
    separateButton.setBounds (sourceRow.removeFromRight (180));
    sourceRow.removeFromRight (10);

    // drop the label when the stem buttons need the room (narrow windows)
    const auto numButtons = jmax (1, sourceButtons.size());
    sourceLabel.setVisible (sourceRow.getWidth() - 96 >= numButtons * 64);

    if (sourceLabel.isVisible())
        sourceLabel.setBounds (sourceRow.removeFromLeft (96));

    const auto buttonWidth = jmin (96, (sourceRow.getWidth() - (numButtons - 1) * 4) / numButtons);

    for (auto* b : sourceButtons)
    {
        b->setBounds (sourceRow.removeFromLeft (buttonWidth));
        sourceRow.removeFromLeft (4);
    }

    if (jobLabel.isVisible())
    {
        auto jobRow = r.removeFromTop (34).reduced (10, 3);
        cancelButton.setBounds (jobRow.removeFromRight (80).reduced (0, 2));
        jobRow.removeFromRight (10);
        jobLabel.setBounds (jobRow);
    }

    statusLabel.setBounds (r.removeFromBottom (24).reduced (10, 0));

    auto slicesRow = r.removeFromBottom (42).reduced (10, 6);
    sampleAllButton.setBounds (slicesRow.removeFromRight (170));
    slicesRow.removeFromRight (6);
    sampleSelectionButton.setBounds (slicesRow.removeFromRight (150));
    slicesLabel.setVisible (slicesRow.getWidth() >= 440);

    if (slicesLabel.isVisible())
        slicesLabel.setBounds (slicesRow.removeFromLeft (52));

    addSliceButton.setBounds (slicesRow.removeFromLeft (jmin (160, slicesRow.getWidth() / 2 - 90)));
    slicesRow.removeFromLeft (6);
    autoSliceButton.setBounds (slicesRow.removeFromLeft (100));
    slicesRow.removeFromLeft (6);
    clearSlicesButton.setBounds (slicesRow.removeFromLeft (60));

    auto transportRow = r.removeFromBottom (40).reduced (10, 5);
    playButton.setBounds (transportRow.removeFromLeft (40));
    transportRow.removeFromLeft (8);
    loopToggle.setBounds (transportRow.removeFromLeft (64));
    fitButton.setBounds (transportRow.removeFromRight (44));
    transportRow.removeFromRight (4);
    zoomInButton.setBounds (transportRow.removeFromRight (28));
    transportRow.removeFromRight (2);
    zoomOutButton.setBounds (transportRow.removeFromRight (28));
    transportRow.removeFromRight (8);
    selectAllButton.setBounds (transportRow.removeFromRight (80));
    transportRow.removeFromRight (8);
    selectionLabel.setBounds (transportRow);

    auto wave = r.reduced (10, 2);
    scrollBar.setBounds (wave.removeFromBottom (10));
    waveform.setBounds (wave);
    waveform.setVisible (hasSource());
    scrollBar.setVisible (hasSource());
}

bool SamplerComponent::keyPressed (const KeyPress& key)
{
    if (key == KeyPress::spaceKey)                       { togglePreview(); return true; }
    if (key == KeyPress ('a', ModifierKeys::commandModifier, 0)) { selectAllButton.triggerClick(); return true; }
    if (key == KeyPress::returnKey)                      { sampleSelection(); return true; }

    return false;
}

bool SamplerComponent::isInterestedInFileDrag (const StringArray& files)
{
    return ! files.isEmpty() && SampleData::isSupportedFile (File (files[0]));
}

void SamplerComponent::filesDropped (const StringArray& files, int, int)
{
    if (! files.isEmpty())
        loadFile (File (files[0]));
}

void SamplerComponent::scrollBarMoved (ScrollBar*, double newRangeStart)
{
    waveform.setVisibleRange (waveform.getVisibleRange().movedToStartAt (newRangeStart));
}

//==============================================================================
void SamplerComponent::openFileChooser()
{
    const auto dirPath = session.getPluginManager().getSettings().getValue ("samplerDir");
    const auto dir = File::isAbsolutePath (dirPath) ? File (dirPath) : File::getSpecialLocation (File::userMusicDirectory);

    chooser = std::make_unique<FileChooser> ("Open audio file for sampling", dir, SampleData::getWildcard());
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                          [safe = SafePointer<SamplerComponent> (this)] (const FileChooser& fc)
    {
        if (safe != nullptr && fc.getResult() != File())
            safe->loadFile (fc.getResult());
    });
}

void SamplerComponent::togglePreview()
{
    auto& engine = session.getEngine();

    if (engine.isSamplePreviewPlaying())
    {
        engine.stopSamplePreview();
        return;
    }

    if (auto* src = getCurrent())
    {
        auto range = waveform.getSelection();

        if (range.isEmpty())
            range = { waveform.getCursor() < src->data->getLength() - 2 ? waveform.getCursor() : 0, src->data->getLength() };

        engine.startSamplePreview (src->data, range, nullptr, loopToggle.getToggleState());
    }
}

void SamplerComponent::timerCallback()
{
    auto& engine = session.getEngine();
    const auto playing = engine.isSamplePreviewPlaying();
    playButton.setToggleState (playing, dontSendNotification);
    waveform.setPlayPosition (playing ? engine.getSamplePreviewPosition() : -1.0);

    if (downloadJob != nullptr)
    {
        jobProgress = downloadJob->getProgress();
        jobLabel.setText ("Downloading the AI model ("
                              + String (Stems::getModelInfo (downloadJob->getType()).approxSizeMB) + " MB)...  "
                              + String (roundToInt (jobProgress * 100.0)) + "%",
                          dontSendNotification);
        repaint (jobLabel.getBounds().expanded (2));

        if (downloadJob->isFinished())
        {
            const auto error = downloadJob->getError();
            downloadJob.reset();
            updateButtons();
            resized();

            if (error.isNotEmpty())
            {
                separationQueued = false;
                setStatus (error.upToFirstOccurrenceOf ("\n", false, false));

                if (error != "Cancelled")
                    AlertWindow::showAsync (MessageBoxOptions().withIconType (MessageBoxIconType::WarningIcon)
                                                               .withTitle ("AI model download").withMessage (error).withButton ("OK"),
                                            nullptr);
            }
            else if (separationQueued)
            {
                separationQueued = false;
                startSeparation (pendingSettings);
            }
        }
    }

    if (separationJob != nullptr)
    {
        jobProgress = separationJob->getProgress();
        const auto elapsed = Time::getMillisecondCounterHiRes() * 0.001 - jobStartTime;
        String eta;

        if (jobProgress > 0.08 && jobProgress < 0.99)
        {
            const auto remaining = elapsed / jobProgress * (1.0 - jobProgress);
            eta = "   about " + (remaining >= 60.0 ? String (roundToInt (remaining / 60.0)) + " min" : String (roundToInt (remaining)) + " s") + " left";
        }

        jobLabel.setText ((separationJob->wasCancelled() ? String ("Cancelling...") : separationJob->getStatus()) + "  "
                              + String (roundToInt (jobProgress * 100.0)) + "%" + eta,
                          dontSendNotification);
        repaint (jobLabel.getBounds().expanded (2));

        if (separationJob->isFinished())
            finishSeparation();
    }
}

//==============================================================================
void SamplerComponent::addSlice()
{
    auto* src = getCurrent();
    const auto sel = waveform.getSelection();

    if (src == nullptr || sel.isEmpty())
        return;

    slices.push_back ({ frameToTime (*src, sel.getStart()), frameToTime (*src, sel.getEnd()) });
    std::sort (slices.begin(), slices.end(), [] (const Range<double>& a, const Range<double>& b) { return a.getStart() < b.getStart(); });
    updateRegions();
    updateButtons();
    setStatus ("Slice added (" + String ((int) slices.size()) + " in total).");
}

void SamplerComponent::showAutoSliceMenu()
{
    auto* src = getCurrent();

    if (src == nullptr)
        return;

    const auto bpm = session.getProject().bpm;
    PopupMenu hits, parts, beats;
    hits.addItem ("Few (only strong hits)", [this] { if (auto* s = getCurrent()) applySlices (Slicing::atTransients (*s->data, waveform.getSelection(), 0.2f)); });
    hits.addItem ("Normal", [this] { if (auto* s = getCurrent()) applySlices (Slicing::atTransients (*s->data, waveform.getSelection(), 0.5f)); });
    hits.addItem ("Many (every small hit)", [this] { if (auto* s = getCurrent()) applySlices (Slicing::atTransients (*s->data, waveform.getSelection(), 0.85f)); });

    for (int n : { 2, 4, 8, 16, 32 })
        parts.addItem (String (n) + " equal parts", [this, n]
        {
            if (auto* s = getCurrent())
                applySlices (Slicing::equalParts (SampleEditing::clampRange (*s->data, waveform.getSelection()), n));
        });

    struct BeatOption { const char* name; double beats; };

    for (auto option : { BeatOption { "Every 1/4 beat (16th notes)", 0.25 }, BeatOption { "Every 1/2 beat (8th notes)", 0.5 },
                         BeatOption { "Every beat", 1.0 }, BeatOption { "Every bar (4 beats)", 4.0 } })
        beats.addItem (option.name, [this, numBeats = option.beats]
        {
            if (auto* s = getCurrent())
                applySlices (Slicing::everyBeats (SampleEditing::clampRange (*s->data, waveform.getSelection()),
                                                  s->data->sampleRate, session.getProject().bpm, numBeats));
        });

    PopupMenu m;
    m.addSectionHeader (waveform.getSelection().isEmpty() ? "Slice the whole sound" : "Slice the selection");
    m.addSubMenu ("At hits (drums, plucks...)", hits);
    m.addSubMenu ("Into equal parts", parts);
    m.addSubMenu ("On the beat (" + String (bpm, 1) + " BPM, starting at the selection)", beats);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (autoSliceButton));
}

void SamplerComponent::applySlices (const std::vector<Range<int>>& frames)
{
    auto* src = getCurrent();

    if (src == nullptr)
        return;

    slices.clear();

    for (auto& r : frames)
        slices.push_back ({ frameToTime (*src, r.getStart()), frameToTime (*src, r.getEnd()) });

    updateRegions();
    updateButtons();
    setStatus (String ((int) slices.size()) + " slices. Click a number above the waveform to select one (right-click for options), "
               "or \"Sample all\" to make a channel from each.");
}

void SamplerComponent::showSliceMenu (int index)
{
    if (! isPositiveAndBelow (index, (int) slices.size()))
        return;

    PopupMenu m;
    m.addSectionHeader ("Slice " + String (index + 1));
    m.addItem ("Play", [this] { togglePreview(); });
    m.addItem ("Sample this slice", [this] { sampleSelection(); });
    m.addItem ("Remove slice", [this, index]
    {
        if (isPositiveAndBelow (index, (int) slices.size()))
        {
            slices.erase (slices.begin() + index);
            updateRegions();
            updateButtons();
        }
    });
    m.showMenuAsync (PopupMenu::Options().withMousePosition());
}

std::shared_ptr<Channel> SamplerComponent::createSample (Range<int> frames, const String& suffix)
{
    auto* src = getCurrent();

    if (src == nullptr)
        return {};

    const auto name = sourceName + (src->id != "original" ? " " + src->name : String()) + suffix;
    return session.addSampleChannelFromData (Slicing::extract (*src->data, frames), name);
}

void SamplerComponent::sampleSelection()
{
    auto* src = getCurrent();

    if (src == nullptr)
        return;

    const auto sel = SampleEditing::clampRange (*src->data, waveform.getSelection());
    auto channel = createSample (sel, " #" + String (++sampleCounter));

    if (channel == nullptr)
        return;

    // remember what was sampled as a slice marker
    const Range<double> t (frameToTime (*src, sel.getStart()), frameToTime (*src, sel.getEnd()));
    const auto exists = std::any_of (slices.begin(), slices.end(), [&t] (const Range<double>& r)
                                     { return std::abs (r.getStart() - t.getStart()) < 0.001 && std::abs (r.getEnd() - t.getEnd()) < 0.001; });

    if (! exists && sel.getLength() < src->data->getLength())
    {
        slices.push_back (t);
        std::sort (slices.begin(), slices.end(), [] (const Range<double>& a, const Range<double>& b) { return a.getStart() < b.getStart(); });
        updateRegions();
    }

    updateButtons();
    setStatus ("Added the channel \"" + channel->name + "\" (" + formatTime (sel.getLength() / src->data->sampleRate)
               + ") to the pattern editor. Select another part to make more.");
}

void SamplerComponent::sampleAllSlices()
{
    auto* src = getCurrent();

    if (src == nullptr || slices.empty())
        return;

    int created = 0;

    for (size_t i = 0; i < slices.size(); ++i)
    {
        const Range<int> frames (roundToInt (timeToFrame (*src, slices[i].getStart())), roundToInt (timeToFrame (*src, slices[i].getEnd())));

        if (frames.getLength() < 16)
            continue;

        if (createSample (frames, " slice " + String ((int) i + 1)) != nullptr)
            ++created;
    }

    setStatus ("Added " + String (created) + " sample channels to the pattern editor.");
}

//==============================================================================
void SamplerComponent::showSeparationOptions()
{
    if (! hasSource())
        return;

    auto panel = std::make_unique<StemOptionsPanel> (pendingSettings, ! waveform.getSelection().isEmpty());
    panel->onStart = [safe = SafePointer<SamplerComponent> (this)] (SeparationSettings s)
    {
        if (safe != nullptr)
            safe->startSeparation (s);
    };
    panel->onLoadModel = [safe = SafePointer<SamplerComponent> (this)]
    {
        if (safe != nullptr)
            safe->loadModelFromFile();
    };

    auto* top = getTopLevelComponent();
    CallOutBox::launchAsynchronously (std::move (panel), top->getLocalArea (&separateButton, separateButton.getLocalBounds()), top);
}

void SamplerComponent::startSeparation (SeparationSettings settings)
{
    if (separationJob != nullptr || sources.empty())
        return;

    pendingSettings = settings;

    if (! Stems::isModelInstalled (settings.model))
    {
        separationQueued = true;
        startDownload (settings.model);
        return;
    }

    // always separate the original sound, either completely or just the selected part
    const auto& original = sources.front();
    Range<int> range (0, original.data->getLength());

    if (settings.selectionOnly)
    {
        if (auto* src = getCurrent(); src != nullptr && ! waveform.getSelection().isEmpty())
        {
            const auto sel = waveform.getSelection();
            range = Range<int> (roundToInt (timeToFrame (original, frameToTime (*src, sel.getStart()))),
                                roundToInt (timeToFrame (original, frameToTime (*src, sel.getEnd()))));
        }
    }

    separationOffsetSeconds = range.getStart() / original.data->sampleRate;
    separationJob = std::make_unique<StemSeparationJob> (Stems::getModelFile (settings.model), original.data, range);
    separationJob->startThread (Thread::Priority::low);
    jobStartTime = Time::getMillisecondCounterHiRes() * 0.001;
    jobProgress = 0.0;
    setStatus ("Separating " + formatTime (range.getLength() / original.data->sampleRate)
               + " of audio. You can keep working; this window can be closed meanwhile.");
    updateButtons();
    resized();
}

void SamplerComponent::startDownload (Stems::ModelType type)
{
    if (downloadJob != nullptr)
        return;

    downloadJob = std::make_unique<ModelDownloadJob> (type);
    downloadJob->startThread();
    jobProgress = 0.0;
    setStatus ("Downloading the AI model. This happens only once.");
    updateButtons();
    resized();
}

void SamplerComponent::cancelJobs()
{
    if (downloadJob != nullptr)
        downloadJob->signalThreadShouldExit();

    if (separationJob != nullptr)
        separationJob->cancel();

    separationQueued = false;
    setStatus ("Cancelling...");
}

void SamplerComponent::finishSeparation()
{
    auto job = std::move (separationJob);
    updateButtons();
    resized();

    const auto elapsed = Time::getMillisecondCounterHiRes() * 0.001 - jobStartTime;

    if (job->wasCancelled() || job->getError() == "Cancelled")
    {
        setStatus ("Separation cancelled.");
        return;
    }

    if (job->getError().isNotEmpty())
    {
        setStatus (job->getError());
        AlertWindow::showAsync (MessageBoxOptions().withIconType (MessageBoxIconType::WarningIcon)
                                                   .withTitle ("AI stem separation").withMessage (job->getError()).withButton ("OK"),
                                nullptr);
        return;
    }

    auto& stems = job->getStems();
    std::vector<Source> list;
    list.push_back (sources.front());   // keep the original
    int added = 0;

    for (auto* id : stemOrder)
    {
        if (! pendingSettings.stems.contains (id))
            continue;

        std::shared_ptr<SampleData> data;

        if (String (id) == "novocals")
        {
            data = Stems::mixWithout (stems, "vocals");
        }
        else
        {
            for (auto& s : stems)
                if (s.id == id)
                    data = s.audio;
        }

        if (data == nullptr)
            continue;

        const auto name = Stems::getStemDisplayName (id);
        list.push_back ({ id, name, data, separationOffsetSeconds });

        if (pendingSettings.addAsChannels && session.addSampleChannelFromData (data, sourceName + " - " + name) != nullptr)
            ++added;
    }

    const auto numStems = (int) list.size() - 1;
    setSources (std::move (list), numStems > 0 ? 1 : 0);
    setStatus ("Separated into " + String (numStems) + " stems in " + formatTime (elapsed)
               + (added > 0 ? " and added " + String (added) + " channels" : String())
               + ". Click a stem above to listen, slice and sample from it.");
}

void SamplerComponent::loadModelFromFile()
{
    chooser = std::make_unique<FileChooser> ("Choose a Demucs model file (ggml-model-htdemucs-...-f16.bin)",
                                             File::getSpecialLocation (File::userHomeDirectory).getChildFile ("Downloads"), "*.bin");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                          [safe = SafePointer<SamplerComponent> (this)] (const FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == File())
            return;

        Stems::ModelType type;
        const auto result = Stems::installModelFile (fc.getResult(), &type);
        safe->setStatus (result.wasOk() ? "Model installed: " + Stems::getModelInfo (type).displayName : result.getErrorMessage());
    });
}

//==============================================================================
class SamplerManager::Window : public DocumentWindow
{
public:
    explicit Window (Session& session)
        : DocumentWindow ("Sampler", Theme::background, DocumentWindow::allButtons)
    {
        auto* content = new SamplerComponent (session);
        component = content;
        content->onTitleChanged = [this] (const String& t) { setName (t); };
        setUsingNativeTitleBar (true);
        setContentOwned (content, true);
        setResizable (true, false);
        setResizeLimits (760, 460, 4000, 3000);
        WindowPlacement::fitOnScreen (*this, getWidth(), getHeight());
    }

    void closeButtonPressed() override
    {
        // keep everything (and running AI jobs); just hide
        setVisible (false);
    }

    SamplerComponent* component = nullptr;
};

SamplerManager::SamplerManager (Session& s) : session (s) {}

SamplerManager::~SamplerManager()
{
    close();
}

SamplerManager::Window& SamplerManager::getWindow()
{
    if (window == nullptr)
        window = std::make_unique<Window> (session);

    return *window;
}

void SamplerManager::show()
{
    auto& w = getWindow();
    const auto firstTime = ! w.isVisible();
    w.setVisible (true);

    if (firstTime)
        WindowPlacement::keepOnScreenAfterShowing (w);

    w.toFront (true);
    w.component->grabKeyboardFocus();
}

void SamplerManager::openFile (const File& file)
{
    show();
    window->component->loadFile (file);
}

void SamplerManager::openSample (std::shared_ptr<const SampleData> data, const String& name)
{
    show();
    window->component->loadSample (std::move (data), name);
}

void SamplerManager::close()
{
    window.reset();
}
