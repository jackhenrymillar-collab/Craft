#pragma once

#include "plugin/PluginProcessor.h"
#include "ui/common/HeadView.h"
#include "ui/common/Knob.h"

namespace foundry
{
// Pad tile in the 4x2 grid. Only pad 1 is live in M1; the rest arrive in M4.
class PadTile final : public juce::Component
{
public:
    PadTile (int number, juce::String name, juce::String recipe, bool live);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    std::function<void (float)> onHit;
    std::function<void()> onOpenBench;
    bool selected = false;
    void setRecipe (juce::String r) { recipe = std::move (r); repaint(); }
    void flash() { flashAlpha = 1.0f; repaint(); }
    void decay() { if (flashAlpha > 0.0f) { flashAlpha = juce::jmax (0.0f, flashAlpha - 0.12f); repaint(); } }

private:
    int number;
    juce::String name, recipe;
    bool live;
    float flashAlpha = 0.0f;
};

class KitView final : public juce::Component, private juce::Timer
{
public:
    explicit KitView (FoundryProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onOpenBench;

private:
    void timerCallback() override;
    void refreshMaterialChips();

    FoundryProcessor& proc;
    juce::OwnedArray<PadTile> pads;
    HeadView head;

    juce::OwnedArray<juce::TextButton> materialChips;
    juce::TextButton openBench { "Open on bench" };

    struct Group
    {
        juce::String title, blurb;
        juce::OwnedArray<Knob> knobs;
        juce::Rectangle<int> area;
    };
    std::array<Group, 4> treatments;
    juce::OwnedArray<Knob> macros;
    juce::Rectangle<int> padArea, recipeArea, treatArea, macroArea;
};
} // namespace foundry
