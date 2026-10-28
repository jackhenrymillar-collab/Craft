#include "FoundryLookAndFeel.h"

namespace foundry
{
using namespace theme;

FoundryLookAndFeel::FoundryLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colour::bg);
    setColour (juce::Label::textColourId, colour::text);
    setColour (juce::TextButton::buttonColourId, colour::control);
    setColour (juce::TextButton::buttonOnColourId, colour::accent);
    setColour (juce::TextButton::textColourOffId, colour::text);
    setColour (juce::TextButton::textColourOnId, colour::bg);
    setColour (juce::Slider::rotarySliderFillColourId, colour::accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, colour::outline);
    setColour (juce::Slider::thumbColourId, colour::text);
    setColour (juce::ComboBox::backgroundColourId, colour::control);
    setColour (juce::ComboBox::outlineColourId, colour::outline);
    setColour (juce::ComboBox::textColourId, colour::text);
    setColour (juce::ComboBox::arrowColourId, colour::textMuted);
    setColour (juce::PopupMenu::backgroundColourId, colour::panel);
    setColour (juce::PopupMenu::textColourId, colour::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colour::accent);
    setColour (juce::TooltipWindow::backgroundColourId, colour::control);
    setColour (juce::TooltipWindow::textColourId, colour::text);
    setColour (juce::TooltipWindow::outlineColourId, colour::outline);
}

juce::Typeface::Ptr FoundryLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    // Anything that asks for the default sans gets IBM Plex Sans.
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return sans (f.getHeight(), f.isBold()).getTypefacePtr();
    if (f.getTypefaceName() == juce::Font::getDefaultMonospacedFontName())
        return mono (f.getHeight()).getTypefacePtr();
    return LookAndFeel_V4::getTypefaceForFont (f);
}

void FoundryLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                           float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float track = juce::jmax (2.5f, radius * 0.11f);
    const float arcR = radius - track * 0.5f;

    // Knob body: a machined puck.
    const float bodyR = arcR - track * 1.6f;
    g.setGradientFill (juce::ColourGradient (colour::control.brighter (0.15f), c.x, c.y - bodyR,
                                             colour::control.darker (0.25f), c.x, c.y + bodyR, false));
    g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2, bodyR * 2);
    g.setColour (colour::outline);
    g.drawEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2, bodyR * 2, 1.0f);

    juce::Path bg;
    bg.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (colour::divider);
    g.strokePath (bg, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar sliders (pan, twist) fill from the centre.
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? (start + end) * 0.5f : start;
    const float to = start + pos * (end - start);
    if (std::abs (to - from) > 0.001f)
    {
        juce::Path v;
        v.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (from, to), juce::jmax (from, to), true);
        g.setColour (s.isEnabled() ? colour::accent : colour::textMuted);
        g.strokePath (v, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const auto tip = c.getPointOnCircumference (bodyR * 0.72f, to);
    const auto inner = c.getPointOnCircumference (bodyR * 0.25f, to);
    g.setColour (colour::text);
    g.drawLine ({ inner, tip }, 2.0f);
}

void FoundryLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                               bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto fill = on ? colour::accent : colour::control;
    if (highlighted && ! on) fill = fill.brighter (0.08f);
    if (down) fill = fill.darker (0.1f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, radiusSmall);
    g.setColour (on ? colour::accent : colour::outline);
    g.drawRoundedRectangle (r, radiusSmall, 1.0f);
}

void FoundryLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.getToggleState() ? colour::bg : (b.isEnabled() ? colour::text : colour::textMuted));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 2), juce::Justification::centred, 1);
}

juce::Font FoundryLookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return sans (juce::jmin (15.0f, (float) h * 0.42f), true);
}

juce::Font FoundryLookAndFeel::getLabelFont (juce::Label& l)
{
    return sans (l.getFont().getHeight());
}
} // namespace foundry
