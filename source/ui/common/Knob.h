#pragma once

#include "ui/theme/Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace foundry
{
// Rotary knob bound to a parameter: mono caps label on top, value readout below.
class Knob final : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& label,
          const juce::String& tooltip = {})
        : name (label)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        slider.setTooltip (tooltip);
        slider.setTitle (label);
        addAndMakeVisible (slider);
        param = state.getParameter (paramId);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
        slider.onValueChange = [this] { repaint(); };
        slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        theme::drawSectionLabel (g, name, r.removeFromTop (14.0f), theme::colour::textMuted, 10.0f,
                                 juce::Justification::centred);
        g.setFont (theme::mono (11.0f));
        g.setColour (theme::colour::text);
        g.drawText (valueText ? valueText ((float) param->getValue()) : param->getCurrentValueAsText(),
                    r.removeFromBottom (14.0f), juce::Justification::centred, false);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (14);
        r.removeFromBottom (14);
        slider.setBounds (r);
    }

    // For knobs whose meaning changes (the part's shape controls): new label, tooltip and
    // a readout computed from the normalised value.
    void setLabel (const juce::String& label, const juce::String& tooltip = {})
    {
        name = label;
        slider.setTitle (label);
        slider.setTooltip (tooltip);
        repaint();
    }
    std::function<juce::String (float normalised)> valueText;

    juce::Slider slider;

private:
    juce::String name;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};
} // namespace foundry
