#include "UI/TransportBar.h"
#include "UI/Touch.h"

TransportBar::TransportBar (Session& s) : session (s)
{
    for (auto* c : std::initializer_list<Component*> { &fileButton, &playButton, &stopButton, &loopButton,
                                                       &patternModeButton, &songModeButton, &bpmLabel, &bpmSlider,
                                                       &positionLabel, &masterLabel, &masterSlider, &meter,
                                                       &masterFxButton, &pluginsButton, &audioButton, &exportButton })
        addAndMakeVisible (c);

    fileButton.onClick = [this] { if (onFileMenu) onFileMenu (fileButton); };
    playButton.onClick = [this] { session.togglePlay(); };
    stopButton.onClick = [this] { session.stop(); };
    loopButton.setClickingTogglesState (false);
    loopButton.onClick = [this] { session.setLoopSong (! session.getProject().loopSong); };
    loopButton.setTooltip ("Loop the song");
    playButton.setTooltip ("Play / Pause (Space)");
    stopButton.setTooltip ("Stop (press twice to return to the start)");

    patternModeButton.setTooltip ("Loop the pattern that is open in the editor");
    songModeButton.setTooltip ("Play the song arrangement");
    patternModeButton.setClickingTogglesState (false);
    songModeButton.setClickingTogglesState (false);
    patternModeButton.onClick = [this] { session.setPlayMode (Session::PlayMode::pattern); };
    songModeButton.onClick = [this] { session.setPlayMode (Session::PlayMode::song); };

    bpmLabel.setFont (Theme::font (12.0f));
    bpmLabel.setColour (Label::textColourId, Theme::dimText);
    bpmSlider.setSliderStyle (Slider::LinearBar);
    bpmSlider.setRange (20.0, 400.0, 0.1);
    bpmSlider.setTextBoxIsEditable (true);
    bpmSlider.setColour (Slider::trackColourId, Colours::transparentBlack);
    bpmSlider.setColour (Slider::backgroundColourId, Theme::panelLight);
    bpmSlider.setColour (Slider::textBoxTextColourId, Theme::text);
    bpmSlider.setMouseCursor (MouseCursor::UpDownResizeCursor);
    bpmSlider.setNumDecimalPlacesToDisplay (1);
    bpmSlider.onValueChange = [this] { session.setBpm (bpmSlider.getValue()); };
    bpmSlider.setTooltip ("Tempo - drag or double-click to type");

    positionLabel.setFont (Font (FontOptions (Font::getDefaultMonospacedFontName(), 17.0f, Font::bold)));
    positionLabel.setJustificationType (Justification::centred);
    positionLabel.setColour (Label::backgroundColourId, Theme::panelLight);
    positionLabel.setColour (Label::textColourId, Theme::accent);

    masterLabel.setFont (Theme::font (12.0f));
    masterLabel.setColour (Label::textColourId, Theme::dimText);
    masterSlider.setSliderStyle (Slider::LinearHorizontal);
    masterSlider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    masterSlider.setRange (0.0, 1.25, 0.001);
    masterSlider.setDoubleClickReturnValue (true, 0.8);
    masterSlider.onValueChange = [this] { session.setMasterVolume ((float) masterSlider.getValue()); };

    masterFxButton.onClick = [this] { if (onMasterEffects) onMasterEffects (masterFxButton); };
    pluginsButton.onClick  = [this] { if (onPluginManager) onPluginManager (pluginsButton); };
    audioButton.onClick    = [this] { if (onAudioSettings) onAudioSettings (audioButton); };
    exportButton.onClick   = [this] { if (onExportWav) onExportWav(); };
    masterFxButton.setTooltip ("Effect plug-ins on the master output");
    pluginsButton.setTooltip (Touch::isMobile() ? "Find AUv3 plug-ins" : "Scan for VST3 plug-ins");
    audioButton.setTooltip ("Audio device settings");
    exportButton.setTooltip ("Render the song to a WAV file");

    session.addListener (this);
    updateFromSession();
    startTimerHz (30);
}

TransportBar::~TransportBar()
{
    session.removeListener (this);
}

void TransportBar::paint (Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    if (showLogo)
    {
        g.setColour (Theme::accent);
        g.setFont (Theme::font (19.0f, true));
        g.drawText ("myDAW", 12, 0, 80, getHeight(), Justification::centredLeft);
    }
}

void TransportBar::setSmallScreen (bool shouldBeSmall)
{
    if (smallScreen != shouldBeSmall)
    {
        smallScreen = shouldBeSmall;
        fileButton.setButtonText (smallScreen ? "Menu" : "File");
        resized();
        repaint();
    }
}

void TransportBar::resized()
{
    // On narrow windows (small or scaled screens) drop what is also reachable elsewhere:
    // Export / Audio / Plug-ins are in the File menu (and on phones Master FX too).
    const auto width = getWidth();
    const auto full = width >= 1270 && ! smallScreen;
    const auto medium = width >= 1040 && ! smallScreen;
    const auto compact = width >= 900 || (smallScreen && width >= 700);

    showLogo = medium;
    exportButton.setVisible (full);
    audioButton.setVisible (full);
    pluginsButton.setVisible (full);
    masterFxButton.setVisible (! smallScreen);
    masterLabel.setVisible (medium);
    bpmLabel.setVisible (compact);
    meter.setVisible (compact);

    auto r = getLocalBounds().reduced (6, 7);
    r.removeFromLeft (showLogo ? 84 : 4);

    fileButton.setBounds (r.removeFromLeft (56));
    r.removeFromLeft (compact ? 14 : 8);
    playButton.setBounds (r.removeFromLeft (38));
    r.removeFromLeft (4);
    stopButton.setBounds (r.removeFromLeft (38));
    r.removeFromLeft (4);
    loopButton.setBounds (r.removeFromLeft (38));
    r.removeFromLeft (compact ? 10 : 6);
    patternModeButton.setBounds (r.removeFromLeft (78));
    songModeButton.setBounds (r.removeFromLeft (62));
    r.removeFromLeft (compact ? 12 : 6);

    if (compact)
        bpmLabel.setBounds (r.removeFromLeft (34));

    bpmSlider.setBounds (r.removeFromLeft (64));
    r.removeFromLeft (compact ? 10 : 6);
    positionLabel.setBounds (r.removeFromLeft (compact ? 120 : 100));

    if (full)
    {
        exportButton.setBounds (r.removeFromRight (90));
        r.removeFromRight (4);
        audioButton.setBounds (r.removeFromRight (56));
        r.removeFromRight (4);
        pluginsButton.setBounds (r.removeFromRight (70));
        r.removeFromRight (4);
    }

    if (! smallScreen)
    {
        masterFxButton.setBounds (r.removeFromRight (80));
        r.removeFromRight (compact ? 10 : 6);
    }

    if (compact)
    {
        meter.setBounds (r.removeFromRight (medium ? 90 : 56).reduced (0, 4));
        r.removeFromRight (4);
    }

    const auto sliderWidth = jlimit (40, 120, r.getWidth() - (medium ? 52 : 4));
    masterSlider.setBounds (r.removeFromRight (sliderWidth));

    if (medium)
        masterLabel.setBounds (r.removeFromRight (48));
}

void TransportBar::sessionChanged (int changes)
{
    if ((changes & (Session::transportChanged | Session::projectReplaced)) != 0)
        updateFromSession();
}

void TransportBar::updateFromSession()
{
    auto& p = session.getProject();
    bpmSlider.setValue (p.bpm, dontSendNotification);
    masterSlider.setValue (p.masterVolume, dontSendNotification);
    loopButton.setToggleState (p.loopSong, dontSendNotification);

    const auto songMode = session.getPlayMode() == Session::PlayMode::song;
    patternModeButton.setToggleState (! songMode, dontSendNotification);
    songModeButton.setToggleState (songMode, dontSendNotification);
    playButton.setToggleState (session.getEngine().isPlaying(), dontSendNotification);
}

String TransportBar::getPositionText() const
{
    const auto beat = session.getEngine().getPositionInBeats();
    int beatsPerBar = session.getProject().songBeatsPerBar;
    double beatLength = 1.0;

    if (session.getPlayMode() == Session::PlayMode::pattern)
    {
        if (auto* p = session.getCurrentPattern())
        {
            beatsPerBar = p->beatsPerBar;
            beatLength = 4.0 / p->beatUnit;
        }
    }

    const auto beats = beat / beatLength;
    const auto bar = (int) std::floor (beats / beatsPerBar);
    const auto beatInBar = (int) std::floor (beats - bar * beatsPerBar);
    const auto ticks = (int) ((beats - std::floor (beats)) * 100.0);

    return String (bar + 1).paddedLeft ('0', 3) + "." + String (beatInBar + 1) + "." + String (ticks).paddedLeft ('0', 2);
}

void TransportBar::timerCallback()
{
    positionLabel.setText (getPositionText(), dontSendNotification);
    playButton.setToggleState (session.getEngine().isPlaying(), dontSendNotification);

    auto& e = session.getEngine();
    meter.setLevels (e.takeMasterPeak (0), e.takeMasterPeak (1));
}
