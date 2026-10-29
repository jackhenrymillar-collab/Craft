#pragma once

#include "Materials.h"
#include "PartCatalogue.h"
#include "Treatments.h"

#include <array>

namespace foundry
{

// Instrument description for one pad: a main resonating part, optionally a second part joined
// to it, things that rattle against it or rest on it, where it's struck and how it's picked up.
// The compile step reads this plain struct so it can run on any thread. (It becomes a full
// graph of parts and joints with the Bench view in M5.)

struct PartSpec
{
    PartType type = PartType::Membrane;
    MaterialId material = MaterialId::Mylar;
    // The part's three shape controls, normalised 0..1 (see PartCatalogue for meaning/range).
    std::array<double, 3> shape { 0.5, 0.5, 0.5 };
    int mount = 0;                // index into PartInfo::mounts
    double highFreqDamping = 0.2; // extra loss per kHz (1 + h * f/1kHz)
    // Kick shell (Membrane mount 1): shell depth in metres, and the front head's port as
    // 0 (sealed) .. 1 (a hole nearly half the head's radius).
    double shellDepth = 0.4;
    double shellPort = 0.3;

    [[nodiscard]] double control (int i) const noexcept
    {
        return partInfo (type).controls[(size_t) i].value (shape[(size_t) i]);
    }

    // A part of the given type with its default material, shape and mount.
    static PartSpec defaultFor (PartType t) noexcept
    {
        PartSpec p;
        p.type = t;
        const auto& info = partInfo (t);
        p.material = info.defaultMaterial;
        for (size_t i = 0; i < 3; ++i)
            p.shape[i] = info.controls[i].defaultNormalised();
        return p;
    }
};

struct PointPolar
{
    double r = 0.0;            // 0 centre .. 1 edge (fraction of the part's half-size)
    double theta = 0.0;        // radians
};

struct MicSpec
{
    PointPolar contact { 0.82, 1.1 };  // contact mic position on the part
    // The air mic's mix and distance are live voice controls (PadParams), not compiled.
};

// A second part joined to the main one (a bar bolted to a box, a plate hung from a spring).
// The joint couples the two parts' modes in the compile step.
struct AttachSpec
{
    bool enabled = false;
    PartSpec part = PartSpec::defaultFor (PartType::Box);
    JointType joint = JointType::Bolt;
    double stiffness = 0.5;        // 0..1 within the joint type's range
    double place = 0.6;            // where on the main part: 0 centre .. 1 edge
};

// Snare wires or sizzle rivets against the main part. How loose they are is a live control
// (PadParams::rattle); the layout is compiled.
struct RattleSpec
{
    RattleType type = RattleType::None;
};

// Loose objects resting on the main part.
struct LooseSpec
{
    LooseType type = LooseType::None;
    double amount = 0.5;           // 0..1: how many
};

struct InstrumentSpec
{
    PartSpec part = PartSpec::defaultFor (PartType::Membrane);
    AttachSpec attach;
    RattleSpec rattle;
    LooseSpec loose;
    bool oversample = false;       // run the pad at 2x the host rate (cleaner buzz and rattles)
    PointPolar strike { 0.3, 0.0 };
    double strikeSpread = 0.05;    // random radial/angle jitter per hit (fraction of size)
    MicSpec mic;
    double tuneSemitones = 0.0;    // Size/Tune: scales the whole object
    int quality = 96;              // max number of exactly-solved modes
    int highBandModes = 96;        // statistical modes covering the dense top end
    Treatments treatments;
};

} // namespace foundry
