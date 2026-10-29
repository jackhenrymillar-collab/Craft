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
    return p == id::tune || p.startsWith ("p1_shape") || p.startsWith ("p1_age") || p.startsWith ("p1_deform") || p.startsWith ("p1_twist")
        || p.startsWith ("p1_puncture") || p == id::looseAmount || p == id::jointStiffness || p == id::attachSize
        || p == id::attachPlace || p == id::shellDepth || p == id::shellPort;
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

    pad->addChild (percent (id::airMix, "Air Mic Mix", 0.5f));
    pad->addChild (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::micDistance, 1 }, "Air Mic Distance",
        juce::NormalisableRange<float> (0.1f, 3.0f, 0.0f, 0.5f), 0.5f,
        juce::AudioParameterFloatAttributes().withLabel ("m").withStringFromValueFunction (
            [] (float v, int) { return v < 1.0f ? juce::String (juce::roundToInt (v * 100.0f)) + " cm" : juce::String (v, 2) + " m"; })));

    // Shape controls: stored 0..1; the editor shows them in the part's own units.
    auto shape = std::make_unique<juce::AudioProcessorParameterGroup> ("shape1", "Pad 1 Shape", " | ");
    const char* shapeNames[] = { "Shape A", "Shape B", "Shape C" };
    for (int i = 0; i < 3; ++i)
    {
        const auto& def = partInfo (PartType::Membrane).controls[(size_t) i];
        shape->addChild (percent (id::shape[i], shapeNames[i], (float) def.defaultNormalised()));
    }

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

    auto addOns = std::make_unique<juce::AudioProcessorParameterGroup> ("addons1", "Pad 1 Add-ons", " | ");
    addOns->addChild (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::rattle, 1 }, "Rattle", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
            if (v < 0.15f) return juce::String ("Tight");
            if (v > 0.85f) return juce::String ("Loose");
            return juce::String (juce::roundToInt (v * 100.0f)) + "%";
        })));
    addOns->addChild (percent (id::looseAmount, "Loose Amount", 0.5f));
    addOns->addChild (percent (id::jointStiffness, "Joint Stiffness", 0.5f));
    addOns->addChild (percent (id::attachSize, "Joined Part Size", (float) partInfo (PartType::Box).controls[0].defaultNormalised()));
    addOns->addChild (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::attachPlace, 1 }, "Joint Place", juce::NormalisableRange<float> (0.0f, 1.0f), 0.6f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
            if (v < 0.1f) return juce::String ("Centre");
            if (v > 0.9f) return juce::String ("Edge");
            return juce::String (juce::roundToInt (v * 100.0f)) + "%";
        })));

    auto low = std::make_unique<juce::AudioProcessorParameterGroup> ("low1", "Pad 1 Low End", " | ");
    low->addChild (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::shellDepth, 1 }, "Shell Depth", juce::NormalisableRange<float> (0.15f, 0.6f), 0.4f,
        juce::AudioParameterFloatAttributes().withLabel ("cm").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " cm"; })));
    low->addChild (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::shellPort, 1 }, "Port", juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
            if (v < 0.01f) return juce::String ("Sealed");
            return juce::String (juce::roundToInt (v * 100.0f)) + "%";
        })));
    low->addChild (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::pitchDrop, 1 }, "Pitch Drop", juce::NormalisableRange<float> (0.0f, 1.0f), 0.25f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
            if (v < 0.005f) return juce::String ("Off");
            if (std::abs (v - 0.25f) < 0.02f) return juce::String ("Natural");
            return juce::String (juce::roundToInt (v * 100.0f)) + "%";
        })));

    layout.add (std::move (pad), std::move (shape), std::move (treat), std::move (addOns), std::move (low));
    return layout;
}

} // namespace foundry::params
