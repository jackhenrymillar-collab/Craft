#pragma once

#include "plugin/PluginProcessor.h"
#include "ui/common/HeadView.h"

namespace foundry
{
// Placeholder for the M5 bench: the cutting mat with this pad's drum on it (tap-testable).
class BenchView final : public juce::Component
{
public:
    explicit BenchView (FoundryProcessor& p) : head (p)
    {
        head.drawMat = true;
        addAndMakeVisible (head);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        g.setColour (colour::panel);
        g.fillRect (bin);
        g.fillRect (inspector);
        drawSectionLabel (g, "Parts bin", bin.toFloat().reduced (14.0f, 10.0f).removeFromTop (16.0f));
        drawSectionLabel (g, "Inspector", inspector.toFloat().reduced (14.0f, 10.0f).removeFromTop (16.0f));
        g.setFont (sans (13.0f));
        g.setColour (colour::textMuted);
        g.drawFittedText ("Resonators, joints, loose objects and exciters arrive with the full bench (M5).",
                          bin.reduced (14, 36).removeFromTop (80), juce::Justification::topLeft, 4);
        g.drawFittedText ("Click anywhere on the head to tap-test it there.",
                          inspector.reduced (14, 36).removeFromTop (60), juce::Justification::topLeft, 3);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        bin = r.removeFromLeft (220);
        inspector = r.removeFromRight (280);
        head.setBounds (r);
    }

private:
    HeadView head;
    juce::Rectangle<int> bin, inspector;
};
} // namespace foundry
