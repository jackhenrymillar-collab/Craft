#pragma once

#include "Theme.h"

namespace foundry
{
class FoundryLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    FoundryLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                           juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
};
} // namespace foundry
