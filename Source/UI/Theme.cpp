#include "UI/Theme.h"

Font Theme::font (float height, bool bold)
{
    return Font (FontOptions (height, bold ? Font::bold : Font::plain));
}

DawLookAndFeel::DawLookAndFeel()
{
    auto scheme = LookAndFeel_V4::getDarkColourScheme();
    scheme.setUIColour (ColourScheme::windowBackground, Theme::background);
    scheme.setUIColour (ColourScheme::widgetBackground, Theme::panelLight);
    scheme.setUIColour (ColourScheme::menuBackground, Theme::panel);
    scheme.setUIColour (ColourScheme::outline, Theme::outline);
    scheme.setUIColour (ColourScheme::defaultText, Theme::text);
    scheme.setUIColour (ColourScheme::defaultFill, Theme::accent);
    scheme.setUIColour (ColourScheme::highlightedText, Colours::black);
    scheme.setUIColour (ColourScheme::highlightedFill, Theme::accent);
    scheme.setUIColour (ColourScheme::menuText, Theme::text);
    setColourScheme (scheme);

    setColour (TextButton::buttonColourId, Theme::panelLight);
    setColour (TextButton::buttonOnColourId, Theme::accent.withAlpha (0.85f));
    setColour (TextButton::textColourOffId, Theme::text);
    setColour (TextButton::textColourOnId, Colours::black);
    setColour (ComboBox::backgroundColourId, Theme::panelLight);
    setColour (ComboBox::outlineColourId, Theme::outline);
    setColour (TextEditor::backgroundColourId, Theme::panelLight);
    setColour (TextEditor::outlineColourId, Theme::outline);
    setColour (TextEditor::focusedOutlineColourId, Theme::accent);
    setColour (ListBox::backgroundColourId, Theme::panel);
    setColour (Slider::thumbColourId, Theme::accent);
    setColour (Slider::trackColourId, Theme::accent.withAlpha (0.6f));
    setColour (Slider::backgroundColourId, Theme::panelLighter);
    setColour (Slider::rotarySliderFillColourId, Theme::accent);
    setColour (Slider::rotarySliderOutlineColourId, Theme::panelLighter);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Theme::panelLight);
    setColour (Label::textColourId, Theme::text);
    setColour (ScrollBar::thumbColourId, Theme::panelLighter);
    setColour (ToggleButton::tickColourId, Theme::accent);
    setColour (PopupMenu::highlightedBackgroundColourId, Theme::accent.withAlpha (0.8f));
    setColour (PopupMenu::highlightedTextColourId, Colours::black);
    setColour (DocumentWindow::backgroundColourId, Theme::background);
    setColour (ResizableWindow::backgroundColourId, Theme::background);
}

void DawLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour& backgroundColour,
                                           bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = backgroundColour;

    if (down)      c = c.brighter (0.25f);
    else if (over) c = c.brighter (0.1f);

    g.setColour (c);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (Theme::outline.brighter (b.getToggleState() ? 0.4f : 0.0f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

Font DawLookAndFeel::getTextButtonFont (TextButton&, int buttonHeight)
{
    return Theme::font (jmin (14.0f, (float) buttonHeight * 0.55f));
}

void DawLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int width, int height, float pos,
                                       float startAngle, float endAngle, Slider& s)
{
    auto bounds = Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    auto radius = jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    auto centre = bounds.getCentre();
    auto angle = startAngle + pos * (endAngle - startAngle);
    auto lineW = jmax (2.0f, radius * 0.22f);
    auto arcR = radius - lineW * 0.5f;

    Path bg;
    bg.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (s.findColour (Slider::rotarySliderOutlineColourId));
    g.strokePath (bg, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));

    const auto isBipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const auto from = isBipolar ? (startAngle + endAngle) * 0.5f : startAngle;

    Path fg;
    fg.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, jmin (from, angle), jmax (from, angle), true);
    g.setColour (s.findColour (Slider::rotarySliderFillColourId));
    g.strokePath (fg, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));

    Point<float> tip (centre.x + (arcR - lineW) * std::sin (angle), centre.y - (arcR - lineW) * std::cos (angle));
    g.setColour (Theme::text);
    g.drawLine ({ centre, tip }, 1.5f);
}

//==============================================================================
IconButton::IconButton (const String& name, Icon i) : Button (name), icon (i) {}

void IconButton::paintButton (Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const auto on = getToggleState();

    g.setColour (on ? Theme::accent.withAlpha (0.25f) : (down ? Theme::panelLighter : (over ? Theme::panelLight.brighter (0.1f) : Theme::panelLight)));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? Theme::accent : Theme::outline);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    auto ic = r.withSizeKeepingCentre (r.getHeight() * 0.5f, r.getHeight() * 0.5f);
    g.setColour (on ? Theme::accent : Theme::text);

    if (icon == Icon::play)
    {
        Path p;
        p.addTriangle (ic.getX() + ic.getWidth() * 0.1f, ic.getY(), ic.getX() + ic.getWidth() * 0.1f, ic.getBottom(),
                       ic.getRight(), ic.getCentreY());
        g.fillPath (p);
    }
    else if (icon == Icon::stop)
    {
        g.fillRect (ic.reduced (ic.getWidth() * 0.08f));
    }
    else
    {
        Path p;
        p.addRoundedRectangle (ic.reduced (ic.getWidth() * 0.05f, ic.getHeight() * 0.15f), 3.0f);
        g.strokePath (p, PathStrokeType (1.8f));
        Path arrow;
        const auto ax = ic.getCentreX() + ic.getWidth() * 0.15f;
        const auto ay = ic.getY() + ic.getHeight() * 0.15f;
        arrow.addTriangle (ax, ay - 4.0f, ax, ay + 4.0f, ax + 5.0f, ay);
        g.fillPath (arrow);
    }
}

//==============================================================================
void LevelMeter::setLevels (float left, float right)
{
    const float decay = 0.80f;
    const float newLevels[2] = { left, right };
    bool changed = false;

    for (int i = 0; i < 2; ++i)
    {
        const auto v = jmax (newLevels[i], levels[i] * decay);

        if (std::abs (v - levels[i]) > 0.001f)
        {
            levels[i] = v;
            changed = true;
        }
    }

    if (changed)
        repaint();
}

void LevelMeter::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Theme::panelLight);
    g.fillRoundedRectangle (r, 3.0f);

    auto rows = r.reduced (3.0f);
    const auto h = (rows.getHeight() - 2.0f) / 2.0f;

    for (int i = 0; i < 2; ++i)
    {
        auto bar = rows.removeFromTop (h);
        rows.removeFromTop (2.0f);

        const auto db = Decibels::gainToDecibels (levels[i], -60.0f);
        const auto proportion = jlimit (0.0f, 1.0f, (db + 60.0f) / 63.0f);

        g.setColour (Theme::panelLighter);
        g.fillRect (bar);

        auto fill = bar.withWidth (bar.getWidth() * proportion);
        g.setGradientFill (ColourGradient (Colour (0xff66bb6a), bar.getX(), 0.0f,
                                           Colour (0xffef5350), bar.getRight(), 0.0f, false));
        g.fillRect (fill);
    }
}
