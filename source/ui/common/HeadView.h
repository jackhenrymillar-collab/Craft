#pragma once

#include "plugin/PluginProcessor.h"
#include "ui/theme/Theme.h"

namespace foundry
{
// Top-down drawing of the membrane with everything the treatments did to it (dents, holes,
// pits, cracks, twist). Click anywhere on the head to tap-test it there.
class HeadView final : public juce::Component, private juce::Timer
{
public:
    explicit HeadView (FoundryProcessor& p);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    bool drawMat = false;    // bench view draws the cutting mat behind the drum
    bool showLabels = false; // bench view shows tags

private:
    void timerCallback() override;
    juce::Rectangle<float> headBounds() const;

    FoundryProcessor& proc;
    InstrumentSnapshot snap;
    std::uint64_t seen = ~0ull;
    juce::Point<float> lastTap;
    float rippleAge = 1.0f;
};
} // namespace foundry
