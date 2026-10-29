#pragma once

#include "Materials.h"

#include <array>
#include <cmath>
#include <string_view>

namespace foundry
{

// What a pad's resonator is. Each type has its own modal solver (compile/) and its own
// three shape controls and mounts (below).
enum class PartType
{
    Membrane, Bar, Plate, Shell, Tube, Spring, Box,
    Count
};
constexpr int kNumPartTypes = static_cast<int> (PartType::Count);

// What plays it. Mallet .. Drop are impacts; Brush and Scrape are sweeps across the surface.
enum class StrikerType
{
    Mallet, Hammer, Hand, Drop, Brush, Scrape, Beater,
    Count
};
constexpr int kNumStrikerTypes = static_cast<int> (StrikerType::Count);

// How a second part is joined to the main one.
enum class JointType
{
    Bolt,    // stiff: the two parts move as one at the joint
    Rubber,  // soft, lossy mount
    Count
};
constexpr int kNumJointTypes = static_cast<int> (JointType::Count);

// Things that buzz against the main part (one-sided contacts with a gap).
enum class RattleType
{
    None,
    Snare,   // snare wires pressed against the underside
    Sizzle,  // loose rivets resting in holes near the edge (sizzle cymbal)
    Count
};
constexpr int kNumRattleTypes = static_cast<int> (RattleType::Count);

// Loose objects resting on the main part, bouncing under gravity.
enum class LooseType
{
    None, Rice, Beads, Sand, Chain,
    Count
};
constexpr int kNumLooseTypes = static_cast<int> (LooseType::Count);

// One of a part's three shape controls. The plugin stores it normalised (0..1); `value`
// maps that to the physical quantity the solver uses.
struct ShapeControl
{
    std::string_view name;
    std::string_view unit;   // display unit ("cm", "mm", "N/m", "%", "")
    double lo, hi, def;      // physical range and default
    bool logScale;
    double display;          // physical -> display multiplier (m -> cm is 100)

    [[nodiscard]] double value (double u) const noexcept
    {
        u = u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u);
        return logScale ? lo * std::pow (hi / lo, u) : lo + (hi - lo) * u;
    }
    [[nodiscard]] double normalised (double v) const noexcept
    {
        const double u = logScale ? std::log (v / lo) / std::log (hi / lo) : (v - lo) / (hi - lo);
        return u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u);
    }
    [[nodiscard]] double defaultNormalised() const noexcept { return normalised (def); }
};

struct PartInfo
{
    std::string_view name;
    std::array<ShapeControl, 3> controls;
    std::array<std::string_view, 2> mounts;
    int mountCount;
    MaterialId defaultMaterial;
    bool membrane;           // takes membrane materials (Mylar, Hide, Paper) rather than solids
};

struct StrikerInfo
{
    std::string_view name;
    std::string_view soft, hard;   // what the Hardness extremes mean
};

// Name and one-line description (joints, rattles, loose objects).
struct NamedInfo
{
    std::string_view name;
    std::string_view description;
};

const PartInfo& partInfo (PartType t) noexcept;
const StrikerInfo& strikerInfo (StrikerType t) noexcept;
const NamedInfo& jointInfo (JointType t) noexcept;
const NamedInfo& rattleInfo (RattleType t) noexcept;
const NamedInfo& looseInfo (LooseType t) noexcept;
PartType partFromName (std::string_view name, PartType fallback) noexcept;
StrikerType strikerFromName (std::string_view name, StrikerType fallback) noexcept;
JointType jointFromName (std::string_view name, JointType fallback) noexcept;
RattleType rattleFromName (std::string_view name, RattleType fallback) noexcept;
LooseType looseFromName (std::string_view name, LooseType fallback) noexcept;
// True for strikers that sweep across the surface instead of hitting it.
constexpr bool isSweep (StrikerType t) noexcept { return t == StrikerType::Brush || t == StrikerType::Scrape; }

// Membranes take Mylar / Hide / Paper; everything else takes the solids (Paper included).
bool materialAllowed (PartType t, MaterialId m) noexcept;

} // namespace foundry
