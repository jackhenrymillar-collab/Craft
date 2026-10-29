#pragma once

#include "PluginProcessor.h"
#include "ui/bench/BenchView.h"
#include "ui/kit/KitView.h"
#include "ui/theme/FoundryLookAndFeel.h"

namespace foundry
{
class TopBar final : public juce::Component, private juce::Timer
{
public:
    explicit TopBar (FoundryProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

    juce::TextButton kitButton { "Kit" }, benchButton { "Bench" };

private:
    void timerCallback() override;
    FoundryProcessor& proc;
    juce::ComboBox kitSelector;
    float meter = 0.0f;
    InstrumentSnapshot snap;
    std::uint64_t seen = ~0ull;
    juce::Rectangle<int> meterArea, statusArea, transportArea;
};

class FoundryEditor final : public juce::AudioProcessorEditor
{
public:
    explicit FoundryEditor (FoundryProcessor&);
    ~FoundryEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void showBench (bool bench);

private:
    FoundryLookAndFeel lnf;
    FoundryProcessor& proc;
    juce::Component content; // laid out at 1280 x 800, scaled to the window
    TopBar topBar;
    KitView kit;
    BenchView bench;
    juce::TooltipWindow tooltips { this, 600 };
};
} // namespace foundry
