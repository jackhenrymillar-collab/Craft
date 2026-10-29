#include "PartCatalogue.h"

#include <algorithm>
#include <cctype>

namespace foundry
{
namespace
{
//                                 name         unit    lo       hi      default  log    display
constexpr std::array<PartInfo, kNumPartTypes> kParts {{
    { "Membrane", {{ { "Diameter",  "cm",  0.20,   0.66,   0.356,  true,  100.0 },
                     { "Tension",   "N/m", 500.0,  6000.0, 2000.0, true,  1.0 },
                     { "Muffle",    "%",   0.0,    1.0,    0.0,    false, 100.0 } }},
      { "Tensioned", "" }, 1, MaterialId::Mylar, true },
    { "Bar",      {{ { "Length",    "cm",  0.12,   1.0,    0.40,   true,  100.0 },
                     { "Thickness", "mm",  0.004,  0.04,   0.02,   true,  1000.0 },
                     { "Undercut",  "%",   0.0,    1.0,    0.5,    false, 100.0 } }},
      { "On cords", "Clamped" }, 2, MaterialId::Oak, false },
    { "Plate",    {{ { "Width",     "cm",  0.15,   1.2,    0.5,    true,  100.0 },
                     { "Aspect",    "",    1.0,    3.0,    1.4,    false, 1.0 },
                     { "Thickness", "mm",  0.0005, 0.008,  0.0015, true,  1000.0 } }},
      { "Hung", "Framed" }, 2, MaterialId::Steel, false },
    { "Shell",    {{ { "Diameter",  "cm",  0.08,   0.6,    0.2,    true,  100.0 },
                     { "Profile",   "%",   0.0,    1.0,    0.0,    false, 100.0 },
                     { "Wall",      "mm",  0.001,  0.015,  0.003,  true,  1000.0 } }},
      { "Resting", "Hung" }, 2, MaterialId::Bronze, false },
    { "Tube",     {{ { "Length",    "cm",  0.15,   2.0,    0.6,    true,  100.0 },
                     { "Diameter",  "mm",  0.012,  0.08,   0.032,  true,  1000.0 },
                     { "Air",       "%",   0.0,    1.0,    0.3,    false, 100.0 } }},
      { "Open", "Capped" }, 2, MaterialId::Aluminium, false },
    { "Spring",   {{ { "Length",    "cm",  0.05,   1.0,    0.3,    true,  100.0 },
                     { "Chirp",     "%",   0.0,    1.0,    0.6,    false, 100.0 },
                     { "Coil",      "mm",  0.006,  0.04,   0.014,  true,  1000.0 } }},
      { "Stretched", "Hanging" }, 2, MaterialId::Steel, false },
    { "Box",      {{ { "Width",     "cm",  0.2,    0.6,    0.3,    false, 100.0 },
                     { "Depth",     "cm",  0.15,   0.6,    0.3,    false, 100.0 },
                     { "Port",      "%",   0.0,    1.0,    0.4,    false, 100.0 } }},
      { "Glued face", "" }, 1, MaterialId::Oak, false },
}};

constexpr std::array<StrikerInfo, kNumStrikerTypes> kStrikers {{
    { "Mallet", "Felt", "Hard tip" },
    { "Hammer", "Rubber face", "Steel face" },
    { "Hand", "Palm", "Fingertip" },
    { "Drop", "Rubber ball", "Steel ball" },
}};

bool equalsIgnoreCase (std::string_view a, std::string_view b)
{
    return a.size() == b.size()
        && std::equal (a.begin(), a.end(), b.begin(), [] (char x, char y) {
               return std::tolower ((unsigned char) x) == std::tolower ((unsigned char) y);
           });
}
} // namespace

const PartInfo& partInfo (PartType t) noexcept
{
    const int i = std::clamp ((int) t, 0, kNumPartTypes - 1);
    return kParts[(size_t) i];
}

const StrikerInfo& strikerInfo (StrikerType t) noexcept
{
    const int i = std::clamp ((int) t, 0, kNumStrikerTypes - 1);
    return kStrikers[(size_t) i];
}

PartType partFromName (std::string_view name, PartType fallback) noexcept
{
    for (int i = 0; i < kNumPartTypes; ++i)
        if (equalsIgnoreCase (kParts[(size_t) i].name, name))
            return (PartType) i;
    if (equalsIgnoreCase (name, "Bowl") || equalsIgnoreCase (name, "Bell"))
        return PartType::Shell;
    return fallback;
}

StrikerType strikerFromName (std::string_view name, StrikerType fallback) noexcept
{
    for (int i = 0; i < kNumStrikerTypes; ++i)
        if (equalsIgnoreCase (kStrikers[(size_t) i].name, name))
            return (StrikerType) i;
    return fallback;
}

bool materialAllowed (PartType t, MaterialId m) noexcept
{
    const auto& mat = getMaterial (m);
    if (partInfo (t).membrane)
        return mat.arealDensity > 0.0;
    return mat.youngsGPa > 0.0;
}

} // namespace foundry
