#pragma once

#include "plugin/PluginProcessor.h"
#include "ui/common/Knob.h"
#include "ui/common/PartView.h"

#include <optional>

namespace foundry
{
// Pad tile in the 4x2 grid. Only pad 1 is live so far; the rest arrive in M4.
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
    void setRecipe (juce::String r, PartType t) { recipe = std::move (r); glyph = t; repaint(); }
    void flash() { flashAlpha = 1.0f; repaint(); }
    void decay() { if (flashAlpha > 0.0f) { flashAlpha = juce::jmax (0.0f, flashAlpha - 0.12f); repaint(); } }

private:
    int number;
    juce::String name, recipe;
    bool live;
    PartType glyph = PartType::Membrane;
    float flashAlpha = 0.0f;
};

// Square material swatch drawn in the bench style; accent ring when selected.
class MaterialSwatch final : public juce::Button
{
public:
    explicit MaterialSwatch (MaterialId m);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    const MaterialId material;
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
    void refreshRecipe();   // chips, swatches, shape-knob labels and the pad text

    FoundryProcessor& proc;
    juce::OwnedArray<PadTile> pads;
    PartView part;

    juce::OwnedArray<juce::TextButton> objectChips, strikerChips, mountChips;
    juce::OwnedArray<MaterialSwatch> swatches;
    juce::TextButton openBench { "Open on bench" };
    juce::OwnedArray<Knob> shapeKnobs;
    juce::OwnedArray<Knob> shellKnobs; // kick shell: depth and port

    // Add-ons column: a joined part, rattles, loose objects.
    juce::ComboBox attachBox, attachMaterialBox;
    juce::OwnedArray<juce::TextButton> jointChips, rattleChips, looseChips;
    juce::TextButton oversampleButton { "2x" };
    juce::OwnedArray<Knob> joinKnobs, rattleKnobs;

    // What the recipe panel last showed, so state changes from anywhere (undo, presets,
    // the host) are picked up by the timer.
    struct Shown
    {
        PartType part; MaterialId material; StrikerType striker; int mount;
        std::optional<PartType> attach; MaterialId attachMaterial; JointType joint;
        RattleType rattle; LooseType loose; bool oversample;
        bool operator== (const Shown&) const = default;
    };
    Shown current() const;
    std::optional<Shown> shown;

    struct Group
    {
        juce::String title, blurb;
        juce::OwnedArray<Knob> knobs;
        juce::Rectangle<int> area;
    };
    std::array<Group, 4> treatments;
    juce::OwnedArray<Knob> macros;
    juce::Rectangle<int> padArea, addOnArea, recipeArea, treatArea, macroArea;
    juce::Rectangle<int> joinTitle, rattleTitle, looseTitle;
    juce::Rectangle<int> objectRow, materialRow, strikerRow, mountRow;
};
} // namespace foundry
