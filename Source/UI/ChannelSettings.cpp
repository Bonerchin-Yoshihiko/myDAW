#include "UI/ChannelSettings.h"
#include "UI/AppFiles.h"
#include "UI/EffectRow.h"
#include "UI/WindowPlacement.h"


void ChannelSettings::show (Session& session, PluginWindowManager& windows, int channelId, Component& target,
                            SampleEditorManager* sampleEditors)
{
    auto content = std::make_unique<ChannelSettings> (session, windows, channelId, sampleEditors);
    auto* top = target.getTopLevelComponent();
    CallOutBox::launchAsynchronously (WindowPlacement::limitToScreen (std::move (content), *top),
                                      top->getLocalArea (&target, target.getLocalBounds()), top);
}

ChannelSettings::ChannelSettings (Session& s, PluginWindowManager& w, int id, SampleEditorManager* e)
    : session (s), windows (w), sampleEditors (e), channelId (id)
{
    auto* c = getChannel();

    title.setFont (Theme::font (13.0f, true));
    title.setColour (Label::textColourId, Theme::dimText);
    addAndMakeVisible (title);

    if (c != nullptr)
    {
        title.setText (c->isSample() ? "SAMPLE CHANNEL" : "INSTRUMENT PLUG-IN", dontSendNotification);

        nameEditor.setText (c->name, false);
        nameEditor.setFont (Theme::font (15.0f));
        nameEditor.onReturnKey = nameEditor.onFocusLost = [this]
        {
            if (auto* ch = getChannel(); ch != nullptr && nameEditor.getText().trim().isNotEmpty()
                                         && ch->name != nameEditor.getText().trim())
            {
                ch->name = nameEditor.getText().trim();
                session.channelSettingsChanged();
            }
        };
        addAndMakeVisible (nameEditor);

        for (auto* l : { &volumeLabel, &panLabel, &rootLabel })
        {
            l->setFont (Theme::font (12.0f));
            l->setColour (Label::textColourId, Theme::dimText);
        }

        volumeSlider.setSliderStyle (Slider::LinearHorizontal);
        volumeSlider.setTextBoxStyle (Slider::TextBoxRight, false, 64, 20);
        volumeSlider.setRange (0.0, 1.25, 0.001);
        volumeSlider.setDoubleClickReturnValue (true, 0.8);
        volumeSlider.textFromValueFunction = [] (double v)
        {
            return v <= 0.0001 ? String ("-inf dB") : String (Decibels::gainToDecibels (v), 1) + " dB";
        };
        volumeSlider.setValue (c->volume.load(), dontSendNotification);
        volumeSlider.updateText();
        volumeSlider.onValueChange = [this]
        {
            if (auto* ch = getChannel()) { ch->volume = (float) volumeSlider.getValue(); session.markDirty(); }
        };

        panSlider.setSliderStyle (Slider::LinearHorizontal);
        panSlider.setTextBoxStyle (Slider::TextBoxRight, false, 64, 20);
        panSlider.setRange (-1.0, 1.0, 0.01);
        panSlider.setDoubleClickReturnValue (true, 0.0);
        panSlider.textFromValueFunction = [] (double v)
        {
            if (std::abs (v) < 0.005) return String ("C");
            return String (roundToInt (std::abs (v) * 100.0)) + (v < 0 ? " L" : " R");
        };
        panSlider.setValue (c->pan.load(), dontSendNotification);
        panSlider.updateText();
        panSlider.onValueChange = [this]
        {
            if (auto* ch = getChannel()) { ch->pan = (float) panSlider.getValue(); session.markDirty(); }
        };

        addAndMakeVisible (volumeLabel);
        addAndMakeVisible (volumeSlider);
        addAndMakeVisible (panLabel);
        addAndMakeVisible (panSlider);

        if (c->isSample())
        {
            rootSlider.setSliderStyle (Slider::LinearHorizontal);
            rootSlider.setTextBoxStyle (Slider::TextBoxRight, false, 64, 20);
            rootSlider.setRange (0, 127, 1);
            rootSlider.setDoubleClickReturnValue (true, 60);
            rootSlider.textFromValueFunction = [] (double v) { return MidiMessage::getMidiNoteName ((int) v, true, true, 4); };
            rootSlider.setValue (c->rootNote.load(), dontSendNotification);
            rootSlider.updateText();
            rootSlider.onValueChange = [this]
            {
                if (auto* ch = getChannel()) { ch->rootNote = (int) rootSlider.getValue(); session.markDirty(); }
            };
            rootSlider.setTooltip ("The piano-roll note at which the sample plays at its original pitch");

            oneShotToggle.setToggleState (c->oneShot.load(), dontSendNotification);
            oneShotToggle.onClick = [this]
            {
                if (auto* ch = getChannel()) { ch->oneShot = oneShotToggle.getToggleState(); session.markDirty(); }
            };

            fileLabel.setFont (Theme::font (11.5f));
            fileLabel.setColour (Label::textColourId, Theme::dimText);
            fileLabel.setText (c->sampleFile.getFullPathName() + (c->sample == nullptr ? "  (missing)" : ""), dontSendNotification);
            fileLabel.setMinimumHorizontalScale (0.5f);

            replaceButton.onClick = [this]
            {
                auto* ch = getChannel();

                if (ch == nullptr)
                    return;

                AppFiles::OpenRequest request;
                request.title = "Replace sample";
                request.initialFolder = ch->sampleFile;
                request.wildcard = SampleData::getWildcard();
                request.copyInto = AppFiles::getDocumentsFolder().getChildFile ("Imported Audio");

                // (a SafePointer made outside the inner lambda: MSVC misreads `this` in nested init-captures)
                SafePointer<ChannelSettings> safe (this);

                AppFiles::chooseFilesToOpen (chooser, request, [safe] (const Array<File>& files)
                {
                    auto* chan = safe != nullptr ? safe->getChannel() : nullptr;

                    if (chan == nullptr || files.isEmpty())
                        return;

                    String error;

                    if (! safe->session.replaceSample (*chan, files.getFirst(), error))
                        AlertWindow::showAsync (MessageBoxOptions().withIconType (MessageBoxIconType::WarningIcon)
                                                                   .withTitle ("Replace sample").withMessage (error).withButton ("OK"),
                                                nullptr);
                });
            };

            addAndMakeVisible (rootLabel);
            addAndMakeVisible (rootSlider);
            addAndMakeVisible (oneShotToggle);
            addAndMakeVisible (fileLabel);
            addAndMakeVisible (replaceButton);

            editSampleButton.setTooltip ("Waveform editor: trim, fades, normalize, reverse, and VST effects baked into the sound");
            editSampleButton.setEnabled (sampleEditors != nullptr && c->sample != nullptr);
            editSampleButton.onClick = [this]
            {
                if (sampleEditors != nullptr)
                {
                    sampleEditors->open (channelId);

                    if (auto* box = findParentComponentOfClass<CallOutBox>())
                        box->dismiss();
                }
            };
            addAndMakeVisible (editSampleButton);
        }
        else
        {
            editPluginButton.setEnabled (c->plugin != nullptr);
            editPluginButton.onClick = [this]
            {
                if (auto* ch = getChannel(); ch != nullptr && ch->plugin != nullptr)
                    windows.show (*ch->plugin, ch->name);
            };
            addAndMakeVisible (editPluginButton);
        }
    }
    else
    {
        title.setText ("MASTER OUTPUT", dontSendNotification);
    }

    effectsLabel.setFont (Theme::font (12.0f, true));
    effectsLabel.setColour (Label::textColourId, Theme::dimText);
    addAndMakeVisible (effectsLabel);

    addEffectButton.onClick = [this] { showAddEffectMenu(); };
    addAndMakeVisible (addEffectButton);

    session.addListener (this);
    rebuildEffectRows();
}

ChannelSettings::~ChannelSettings()
{
    session.removeListener (this);
}

Channel* ChannelSettings::getChannel() const
{
    return channelId == 0 ? nullptr : session.getProject().findChannel (channelId);
}

EffectChain* ChannelSettings::getChain() const
{
    if (channelId == 0)
        return &session.getEngine().getMasterEffects();

    if (auto* c = getChannel())
        return &c->effects;

    return nullptr;
}

void ChannelSettings::sessionChanged (int changes)
{
    if ((changes & (Session::channelsChanged | Session::projectReplaced)) == 0)
        return;

    if (channelId != 0 && getChannel() == nullptr)
    {
        if (auto* box = findParentComponentOfClass<CallOutBox>())
            box->dismiss();

        return;
    }

    if (auto* c = getChannel(); c != nullptr && c->isSample())
        fileLabel.setText (c->sampleFile.getFullPathName() + (c->sample == nullptr ? "  (missing)" : ""), dontSendNotification);

    rebuildEffectRows();
}

void ChannelSettings::rebuildEffectRows()
{
    effectRows.clear();
    auto* chain = getChain();

    if (chain != nullptr)
    {
        for (size_t i = 0; i < chain->slots.size(); ++i)
        {
            auto& slot = *chain->slots[i];
            auto* row = new EffectRow (slot.plugin->getName(), slot.bypassed.load());
            auto* plugin = slot.plugin.get();
            const auto index = (int) i;

            row->edit.onClick = [this, plugin] { windows.show (*plugin, plugin->getName()); };
            row->bypass.onClick = [this, row, index]
            {
                if (auto* ch = getChain(); ch != nullptr && isPositiveAndBelow (index, (int) ch->slots.size()))
                {
                    ch->slots[(size_t) index]->bypassed = ! row->bypass.getToggleState();
                    session.markDirty();
                }
            };
            row->remove.onClick = [safe = SafePointer<ChannelSettings> (this), index]
            {
                MessageManager::callAsync ([safe, index]
                {
                    if (safe != nullptr)
                        safe->session.removeEffect (safe->getChannel(), index);
                });
            };

            addAndMakeVisible (row);
            effectRows.add (row);
        }
    }

    updateSize();
}

void ChannelSettings::updateSize()
{
    int h = 34;                 // title

    if (auto* c = getChannel())
    {
        h += 34 + 30 + 30;      // name, volume, pan
        h += c->isSample() ? 30 + 28 + 22 + 32 : 36;
    }

    h += 30 + effectRows.size() * 32 + 40;
    setSize (360, h);
    resized();
}

void ChannelSettings::paint (Graphics& g)
{
    g.fillAll (Theme::panel.withAlpha (0.0f));
}

void ChannelSettings::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    title.setBounds (r.removeFromTop (28));

    if (auto* c = getChannel())
    {
        nameEditor.setBounds (r.removeFromTop (28));
        r.removeFromTop (6);

        auto row = [&r] (Label& l, Component& comp)
        {
            auto line = r.removeFromTop (30);
            l.setBounds (line.removeFromLeft (76));
            comp.setBounds (line.reduced (0, 4));
        };

        row (volumeLabel, volumeSlider);
        row (panLabel, panSlider);

        if (c->isSample())
        {
            row (rootLabel, rootSlider);
            oneShotToggle.setBounds (r.removeFromTop (28));
            fileLabel.setBounds (r.removeFromTop (22));
            auto buttons = r.removeFromTop (32).reduced (0, 3);
            replaceButton.setBounds (buttons.removeFromLeft (140));
            buttons.removeFromLeft (8);
            editSampleButton.setBounds (buttons.removeFromLeft (120));
        }
        else
        {
            editPluginButton.setBounds (r.removeFromTop (36).reduced (0, 4).withWidth (170));
        }
    }

    effectsLabel.setBounds (r.removeFromTop (30));

    for (auto* row : effectRows)
    {
        row->setBounds (r.removeFromTop (30));
        r.removeFromTop (2);
    }

    addEffectButton.setBounds (r.removeFromTop (36).reduced (0, 5).withWidth (130));
}

void ChannelSettings::showAddEffectMenu()
{
    PopupMenu m;
    auto descriptions = std::make_shared<std::vector<PluginDescription>> (session.getPluginManager().fillMenu (m, false, 1));

    m.showMenuAsync (PopupMenu::Options().withTargetComponent (addEffectButton),
                     [safe = SafePointer<ChannelSettings> (this), descriptions] (int result)
    {
        if (safe == nullptr || result <= 0 || result > (int) descriptions->size())
            return;

        String error;

        if (! safe->session.addEffect (safe->getChannel(), (*descriptions)[(size_t) result - 1], error))
            AlertWindow::showAsync (MessageBoxOptions().withIconType (MessageBoxIconType::WarningIcon)
                                                       .withTitle ("Add effect").withMessage (error).withButton ("OK"),
                                    nullptr);
    });
}
