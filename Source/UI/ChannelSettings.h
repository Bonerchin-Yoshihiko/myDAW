#pragma once

#include "Session.h"
#include "UI/PluginWindows.h"
#include "UI/SampleEditor.h"
#include "UI/Theme.h"

/** Pop-up panel with a channel's settings and its effect chain.
    channelId == 0 shows the master effect chain instead. */
class ChannelSettings : public Component,
                        private Session::Listener
{
public:
    ChannelSettings (Session&, PluginWindowManager&, int channelId, SampleEditorManager* = nullptr);
    ~ChannelSettings() override;

    void paint (Graphics&) override;
    void resized() override;

    static void show (Session&, PluginWindowManager&, int channelId, Component& target,
                      SampleEditorManager* sampleEditors = nullptr);

private:
    void sessionChanged (int changes) override;
    Channel* getChannel() const;
    EffectChain* getChain() const;
    void rebuildEffectRows();
    void showAddEffectMenu();
    void updateSize();

    Session& session;
    PluginWindowManager& windows;
    SampleEditorManager* sampleEditors;
    const int channelId;

    Label title;
    TextEditor nameEditor;
    Label volumeLabel { {}, "Volume" }, panLabel { {}, "Pan" }, rootLabel { {}, "Root note" }, fileLabel;
    Slider volumeSlider, panSlider, rootSlider;
    ToggleButton oneShotToggle { "One-shot (ignore note length)" };
    TextButton replaceButton { "Replace sample..." }, editSampleButton { "Edit sample..." }, editPluginButton { "Open plug-in window" };
    Label effectsLabel { {}, "INSERT EFFECTS" };
    TextButton addEffectButton { "+ Add effect" };
    OwnedArray<Component> effectRows;
    std::unique_ptr<FileChooser> chooser;
};
