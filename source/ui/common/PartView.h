#pragma once

#include "plugin/PluginProcessor.h"
#include "ui/theme/Theme.h"

namespace foundry
{
// Top-down drawing of the pad's part (drum head, bar, plate, bowl/bell, tube, spring or box)
// with everything the treatments did to it (dents, holes, pits, cracks, twist). Click
// anywhere on the part to tap-test it there.
class PartView final : public juce::Component, private juce::Timer
{
public:
    explicit PartView (FoundryProcessor& p);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    bool drawMat = false;    // bench view draws the cutting mat behind the part

    // Also used by the pad tiles for their small line glyphs.
    static void drawGlyph (juce::Graphics& g, PartType type, juce::Rectangle<float> area, juce::Colour c);

private:
    struct Mapping
    {
        juce::Point<float> centre;
        float sx = 1.0f, sy = 1.0f; // px per metre (sy may be stretched for very thin parts)
        juce::Point<float> toScreen (Vec2 p) const { return { centre.x + (float) p.x * sx, centre.y - (float) p.y * sy }; }
        Vec2 toLocal (juce::Point<float> q) const { return { (q.x - centre.x) / sx, (centre.y - q.y) / sy }; }
    };

    void timerCallback() override;
    Mapping mapping() const;
    juce::Path bodyPath (const Mapping& m) const;
    void drawUnder (juce::Graphics& g, const Mapping& m, const juce::Path& body) const;
    void drawSurface (juce::Graphics& g, const Mapping& m, const juce::Path& body) const;
    void drawOver (juce::Graphics& g, const Mapping& m, const juce::Path& body) const;
    void drawFeatures (juce::Graphics& g, const Mapping& m) const;
    void drawContacts (juce::Graphics& g, const Mapping& m, bool underneath) const;
    void drawAttachment (juce::Graphics& g, const Mapping& m) const;
    juce::Rectangle<float> partArea() const;   // where the main part is drawn
    juce::Rectangle<float> attachArea() const; // where a joined part is drawn (empty if none)

    FoundryProcessor& proc;
    InstrumentSnapshot snap;
    std::uint64_t seen = ~0ull;
    juce::Point<float> lastTap;
    float rippleAge = 1.0f;
};
} // namespace foundry
