#pragma once

#include "UI/Theme.h"

/** One row of an effect rack: name, Edit (plug-in window), On/bypass and remove. */
class EffectRow : public Component
{
public:
    EffectRow (const String& name, bool bypassed)
    {
        label.setText (name, dontSendNotification);
        label.setFont (Theme::font (13.0f));
        bypass.setButtonText ("On");
        bypass.setToggleState (! bypassed, dontSendNotification);
        bypass.setClickingTogglesState (true);
        bypass.setTooltip ("Enable / bypass");
        remove.setTooltip ("Remove this effect");
        edit.setTooltip ("Open the plug-in window");

        for (auto* c : std::initializer_list<Component*> { &label, &edit, &bypass, &remove })
            addAndMakeVisible (c);
    }

    void paint (Graphics& g) override
    {
        g.setColour (Theme::panelLight);
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4, 3);
        remove.setBounds (r.removeFromRight (24));
        r.removeFromRight (4);
        bypass.setBounds (r.removeFromRight (36));
        r.removeFromRight (4);
        edit.setBounds (r.removeFromRight (44));
        label.setBounds (r);
    }

    Label label;
    TextButton edit { "Edit" }, bypass, remove { "X" };
};
