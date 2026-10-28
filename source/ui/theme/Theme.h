#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace foundry::theme
{
// Workbench theme tokens (brief section 4.1). Change these only with the user's OK.
namespace colour
{
inline const juce::Colour bg { 0xff1b1c1a };
inline const juce::Colour panel { 0xff20211e };
inline const juce::Colour control { 0xff2a2b27 };
inline const juce::Colour divider { 0xff34352f };
inline const juce::Colour outline { 0xff3a3b35 };
inline const juce::Colour text { 0xffecebe6 };
inline const juce::Colour textMuted { 0xffa6a59d };
inline const juce::Colour accent { 0xffff7a1a };
inline const juce::Colour mat { 0xff23503d };
inline const juce::Colour ok { 0xff6fbf73 };
inline const juce::Colour modeBar { 0xffd4b06a };
} // namespace colour

constexpr int baseWidth = 1280;
constexpr int baseHeight = 800;
constexpr float radiusSmall = 4.0f;
constexpr float radiusLarge = 8.0f;
constexpr int minHitTarget = 44;

juce::Font sans (float height, bool semibold = false);
juce::Font mono (float height, bool medium = false);

// Small-caps style section label: mono, upper case, letter-spaced ~0.12 em.
void drawSectionLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                       juce::Colour colour = colour::textMuted, float height = 11.0f,
                       juce::Justification just = juce::Justification::centredLeft);

// Cutting mat: 20 px fine and 100 px major grid lines, white at 4% / 10%.
void drawCuttingMat (juce::Graphics& g, juce::Rectangle<float> area, juce::Point<float> origin = {});
} // namespace foundry::theme
