#pragma once

#include "Common.h"

namespace Theme
{
    const Colour background   { 0xff1b1e23 };
    const Colour panel        { 0xff23272e };
    const Colour panelLight   { 0xff2c313a };
    const Colour panelLighter { 0xff363c47 };
    const Colour outline      { 0xff3b414c };
    const Colour gridLine     { 0xff313640 };
    const Colour barLine      { 0xff4a5160 };
    const Colour text         { 0xffdde2e8 };
    const Colour dimText      { 0xff8b939e };
    const Colour accent       { 0xff4fc3f7 };
    const Colour accentWarm   { 0xffffb74d };
    const Colour playhead     { 0xffff7043 };
    const Colour recordRed    { 0xffef5350 };

    Font font (float height, bool bold = false);
}

class DawLookAndFeel : public LookAndFeel_V4
{
public:
    DawLookAndFeel();

    void drawButtonBackground (Graphics&, Button&, const Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;
    Font getTextButtonFont (TextButton&, int buttonHeight) override;
    void drawRotarySlider (Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, Slider&) override;
};

/** Small button that paints a transport icon (play / stop / loop). */
class IconButton : public Button
{
public:
    enum class Icon { play, stop, loop };

    IconButton (const String& name, Icon);
    void paintButton (Graphics&, bool isMouseOver, bool isButtonDown) override;

private:
    Icon icon;
};

/** Simple stereo peak meter fed by a polling timer. */
class LevelMeter : public Component
{
public:
    void setLevels (float left, float right);
    void paint (Graphics&) override;

private:
    float levels[2] {};
};
