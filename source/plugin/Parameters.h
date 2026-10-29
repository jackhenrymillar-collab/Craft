#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "model/Instrument.h"

namespace foundry::params
{
// Parameter IDs. Pad-scoped IDs are prefixed "p1_" so pads 2-8 slot in at M4.
namespace id
{
inline constexpr const char* force = "p1_force";
inline constexpr const char* hardness = "p1_hardness";
inline constexpr const char* spread = "p1_spread";
inline constexpr const char* position = "p1_position";
inline constexpr const char* tune = "p1_tune";
inline constexpr const char* level = "p1_level";
inline constexpr const char* pan = "p1_pan";
inline constexpr const char* airMix = "p1_airMix";
inline constexpr const char* micDistance = "p1_micDistance";
// The part's three shape controls (meaning depends on the part type, see PartCatalogue).
inline constexpr const char* shapeA = "p1_shapeA";
inline constexpr const char* shapeB = "p1_shapeB";
inline constexpr const char* shapeC = "p1_shapeC";
inline constexpr const char* const shape[3] = { shapeA, shapeB, shapeC };

inline constexpr const char* ageAmount = "p1_ageAmount";
inline constexpr const char* ageClimate = "p1_ageClimate";
inline constexpr const char* deformIntensity = "p1_deformIntensity";
inline constexpr const char* deformSize = "p1_deformSize";
inline constexpr const char* deformDensity = "p1_deformDensity";
inline constexpr const char* twistAmount = "p1_twistAmount";
inline constexpr const char* twistPivot = "p1_twistPivot";
inline constexpr const char* twistFocus = "p1_twistFocus";
inline constexpr const char* twistWrinkle = "p1_twistWrinkle";
inline constexpr const char* punctureCount = "p1_punctureCount";
inline constexpr const char* punctureSize = "p1_punctureSize";
inline constexpr const char* puncturePlacement = "p1_puncturePlacement";
inline constexpr const char* punctureScatter = "p1_punctureScatter";
inline constexpr const char* punctureRagged = "p1_punctureRagged";
} // namespace id

// Parameters whose change needs a recompile (they reshape the modes).
bool affectsModes (const juce::String& paramId);

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// State-tree properties for structural (non-automatable) choices.
inline const juce::Identifier partType { "partType" };
inline const juce::Identifier material { "material" };
inline const juce::Identifier mount { "mount" };
inline const juce::Identifier striker { "striker" };
inline const juce::Identifier headMaterial { "headMaterial" }; // M1 sessions: membrane material

} // namespace foundry::params
