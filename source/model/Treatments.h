#pragma once

#include <cstdint>

namespace foundry
{

// The four "treatments" reshape a part's modes in the compile step. All values
// are normalised 0..1 (Twist amount is -1..1) so they map straight onto knobs.
// Random layouts (dent, hole and crack positions) come from the part's seed, so
// turning a knob changes intensity without reshuffling where things are.

struct AgeParams
{
    double amount = 0.0;    // 0 new .. 1 ancient (exaggerated)
    double climate = 0.5;   // 0 dry .. 1 damp: picks the ageing path (e.g. patina vs rust)
};

struct DeformParams
{
    double intensity = 0.0; // depth of dents/bumps
    double size = 0.4;      // dent radius, small pings .. broad warps
    double density = 0.3;   // how many dents
};

struct TwistParams
{
    double amount = 0.0;    // -1..1, direction and strength of the twist
    double pivot = 0.0;     // 0 centred .. 1 twisted about a point near the rim
    double focus = 0.3;     // 0 spread across the whole part .. 1 concentrated at the pivot
    double wrinkle = 0.0;   // fine wrinkling where the twist is strongest (buzz, uneven ring)
};

struct PunctureParams
{
    double count = 0.0;     // 0 .. 32 holes
    double size = 0.3;      // hole diameter, pin-prick .. large
    double placement = 0.5; // 0 near the centre .. 1 near the rim
    double scatter = 0.5;   // 0 neat ring .. 1 scattered at random
    double ragged = 0.3;    // clean drilled edge .. torn edge (extra loss, rattle)
};

struct Treatments
{
    AgeParams age;
    DeformParams deform;
    TwistParams twist;
    PunctureParams puncture;
    std::uint32_t seed = 0x5eed1234u;

    [[nodiscard]] bool isIdentity() const noexcept;
    void clampToRange() noexcept;
};

constexpr int kMaxPunctures = 32;

} // namespace foundry
