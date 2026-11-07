#pragma once

#include "Session.h"
#include "UI/Theme.h"

class TransportBar : public Component,
                     private Session::Listener,
                     private Timer
{
public:
    explicit TransportBar (Session&);
    ~TransportBar() override;

    std::function<void (Component&)> onFileMenu, onMasterEffects, onPluginManager, onAudioSettings;
    std::function<void()> onExportWav;

    void paint (Graphics&) override;
    void resized() override;

private:
    void sessionChanged (int changes) override;
    void timerCallback() override;
    void updateFromSession();
    String getPositionText() const;

    Session& session;

    TextButton fileButton { "File" };
    IconButton playButton { "Play", IconButton::Icon::play };
    IconButton stopButton { "Stop", IconButton::Icon::stop };
    IconButton loopButton { "Loop song", IconButton::Icon::loop };
    TextButton patternModeButton { "PATTERN" }, songModeButton { "SONG" };
    Label bpmLabel { {}, "BPM" };
    Slider bpmSlider;
    Label positionLabel;
    Label masterLabel { {}, "Master" };
    Slider masterSlider;
    LevelMeter meter;
    TextButton masterFxButton { "Master FX" }, pluginsButton { "Plug-ins" }, audioButton { "Audio" }, exportButton { "Export WAV" };
    bool showLogo = true;
};
