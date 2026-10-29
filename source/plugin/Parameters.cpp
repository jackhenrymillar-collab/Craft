#include "Parameters.h"

namespace foundry::params
{
namespace
{
std::unique_ptr<juce::AudioParameterFloat> percent (const char* pid, const juce::String& name, float def,
                                                    const juce::String& group = {})
{
    juce::ignoreUnused (group);
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { pid, 1 }, name, juce::NormalisableRange<float> (0.0f, 1.0f), def,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; })
            .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue() / 100.0f; }));
}
} // namespace

bool affectsModes (const juce::String& p)
{
    return p == id::tune || p.startsWith ("p1_age") || p.startsWith ("p1_deform") || p.startsWith ("p1_twist")
        || p.startsWith ("p1_puncture");
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto pad = std::make_unique<juce::AudioProcessorParameterGroup> ("pad1", "Pad 1", " | ");
    pad->addChild (percent (id::force, "Force", 0.6f));
    pad->addChild (percent (id::hardness, "Hardness", 0.5f));
    pad->addChild (percent (id::spread, "Spread", 0.15f));
    pad->addChild (percent (id::position, "Strike Position", 0.3f));
    pad->addChild (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::tune, 1 }, "Size/Tune", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("st").withStringFromValueFunction (
            [] (float v, int) { return juce::String (v, 1) + " st"; })));
    pad->addChild (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::level, 1 }, "Level", juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f), -3.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (
            [] (float v, int) { return juce::String (v, 1) + " dB"; })));
    pad->addChild (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::pan, 1 }, "Pan", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
            if (std::abs (v) < 0.005f) return juce::String ("C");
            return juce::String (juce::roundToInt (std::abs (v) * 100.0f)) + (v < 0 ? " L" : " R");
        })));

    auto treat = std::make_unique<juce::AudioProcessorParameterGroup> ("treat1", "Pad 1 Treatments", " | ");
    treat->addChild (percent (id::ageAmount, "Age", 0.0f));
    treat->addChild (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::ageClimate, 1 }, "Age Climate", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
            if (v < 0.34f) return juce::String ("Dry");
            if (v > 0.66f) return juce::String ("Damp");
            return juce::String ("Temperate");
        })));
    treat->addChild (percent (id::deformIntensity, "Deform", 0.0f));
    treat->addChild (percent (id::deformSize, "Deform Size", 0.4f));
    treat->addChild (percent (id::deformDensity, "Deform Density", 0.3f));
    treat->addChild (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::twistAmount, 1 }, "Twist", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
            return juce::String (juce::roundToInt (v * 100.0f)) + "%";
        })));
    treat->addChild (percent (id::twistPivot, "Twist Pivot", 0.0f));
    treat->addChild (percent (id::twistFocus, "Twist Focus", 0.3f));
    treat->addChild (percent (id::twistWrinkle, "Twist Wrinkle", 0.0f));
    treat->addChild (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::punctureCount, 1 }, "Puncture", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
            const int n = juce::roundToInt (v * (float) kMaxPunctures);
            return juce::String (n) + (n == 1 ? " hole" : " holes");
        })));
    treat->addChild (percent (id::punctureSize, "Puncture Size", 0.3f));
    treat->addChild (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::puncturePlacement, 1 }, "Puncture Placement", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
            if (v < 0.2f) return juce::String ("Centre");
            if (v > 0.8f) return juce::String ("Rim");
            return juce::String (juce::roundToInt (v * 100.0f)) + "%";
        })));
    treat->addChild (percent (id::punctureScatter, "Puncture Scatter", 0.5f));
    treat->addChild (percent (id::punctureRagged, "Puncture Ragged", 0.3f));

    layout.add (std::move (pad), std::move (treat));
    return layout;
}

} // namespace foundry::params
