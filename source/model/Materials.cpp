#include "Materials.h"

#include <algorithm>
#include <cctype>

namespace foundry
{
namespace
{
// Ageing rules, one per material. Values are the effect at Age = 1, chosen to be
// plausible in direction and exaggerated in size. Rationale per row:
//   metals: oxide/rust layers add mass and act like a lossy coating (high-frequency
//           loss), pitting removes metal unevenly; damp makes rust, dry makes patina.
//   glass/ceramic: micro-cracks ("crazing") split modes and add friction loss + tick.
//   stone: weathering flakes the surface: mass loss, extra loss, grit.
//   ice: sublimates when dry (thinner, lighter), melts/refreezes when damp (lossy).
//   wood: dry -> lighter, stiffer, checks (cracks) that buzz; damp -> swells, rots, dull.
//   rubber: perishes: hardens and cracks.
//   hide: dry -> shrinks tight and brittle; damp -> goes slack and dead.
//   mylar: cold flow: stretches and loses tension, pull-dents go uneven.
//   paper: yellows and turns brittle when dry, soggy and dead when damp.
//   hyperglass: impossible; "crystallises", gaining shimmer from tiny splits.
//                                   stiff           mass           loss          hiLoss        uneven        pits        pitDepth      cracks       crackFric     grit
constexpr AgeingProfile kSteel   { "rust",        {-0.04,-0.10}, { 0.02, 0.08}, { 1.5, 6.0},  {0.3, 1.2},   {0.05,0.12}, {12, 45},  {0.15,0.45}, {0, 0},     {0.0,0.0},    {0.10,0.55} };
constexpr AgeingProfile kBronze  { "patina",      {-0.01,-0.04}, { 0.01, 0.04}, { 0.6, 2.5},  {0.1, 0.5},   {0.03,0.08}, { 4, 20},  {0.05,0.20}, {0, 1},     {0.0,0.04},   {0.02,0.20} };
constexpr AgeingProfile kAlu     { "oxidation",   {-0.01,-0.05}, { 0.00, 0.03}, { 0.8, 3.0},  {0.1, 0.6},   {0.03,0.08}, { 4, 30},  {0.05,0.30}, {0, 0},     {0.0,0.0},    {0.02,0.25} };
constexpr AgeingProfile kBrass   { "tarnish",     {-0.02,-0.06}, { 0.01, 0.05}, { 0.8, 3.0},  {0.1, 0.6},   {0.04,0.10}, { 6, 24},  {0.08,0.25}, {1, 2},     {0.03,0.06},  {0.04,0.25} };
constexpr AgeingProfile kGlass   { "crazing",     {-0.03,-0.05}, { 0.00, 0.00}, { 2.0, 3.0},  {0.2, 0.3},   {0.08,0.10}, { 0, 4},   {0.00,0.10}, {5, 8},     {0.08,0.10},  {0.30,0.40} };
constexpr AgeingProfile kCeramic { "crazing",     {-0.05,-0.08}, { 0.00, 0.03}, { 1.5, 3.0},  {0.2, 0.5},   {0.08,0.12}, { 2, 8},   {0.05,0.15}, {6, 10},    {0.10,0.15},  {0.35,0.45} };
constexpr AgeingProfile kSlate   { "weathering",  {-0.10,-0.20}, {-0.03,-0.06}, { 1.0, 2.5},  {0.4, 1.0},   {0.10,0.18}, {15, 40},  {0.20,0.40}, {2, 5},     {0.08,0.12},  {0.25,0.50} };
constexpr AgeingProfile kIce     { "sublimation", {-0.15,-0.30}, {-0.20,-0.05}, { 0.5, 4.0},  {0.3, 2.0},   {0.10,0.25}, {10, 30},  {0.30,0.30}, {2, 6},     {0.10,0.20},  {0.10,0.40} };
constexpr AgeingProfile kOak     { "checking",    { 0.12,-0.25}, {-0.08, 0.15}, {-0.2, 2.0},  {0.0, 1.0},   {0.08,0.15}, { 0, 25},  {0.00,0.35}, {4, 1},     {0.10,0.06},  {0.20,0.20} };
constexpr AgeingProfile kSpruce  { "checking",    { 0.15,-0.30}, {-0.10, 0.18}, {-0.3, 2.5},  {0.0, 1.2},   {0.08,0.15}, { 0, 25},  {0.00,0.35}, {4, 1},     {0.10,0.06},  {0.20,0.20} };
constexpr AgeingProfile kBamboo  { "splitting",   { 0.10,-0.20}, {-0.06, 0.12}, {-0.1, 2.0},  {0.0, 0.8},   {0.06,0.12}, { 0, 15},  {0.00,0.25}, {5, 2},     {0.12,0.08},  {0.25,0.20} };
constexpr AgeingProfile kRubber  { "perishing",   { 0.80, 0.40}, { 0.00, 0.03}, {-0.4,-0.2},  {0.2, 0.2},   {0.10,0.15}, { 5, 15},  {0.20,0.30}, {6, 8},     {0.20,0.25},  {0.15,0.20} };
constexpr AgeingProfile kHide    { "drying",      { 0.35,-0.45}, {-0.05, 0.10}, { 0.3, 3.0},  {0.2, 1.5},   {0.15,0.30}, { 0, 10},  {0.00,0.20}, {2, 0},     {0.10,0.0},   {0.20,0.10} };
constexpr AgeingProfile kMylar   { "stretch",     {-0.30,-0.40}, { 0.00, 0.01}, { 0.8, 1.5},  {0.5, 0.8},   {0.20,0.25}, { 0, 0},   {0.00,0.00}, {0, 0},     {0.0,0.0},    {0.05,0.08} };
constexpr AgeingProfile kPaper   { "brittling",   { 0.20,-0.60}, {-0.02, 0.25}, { 0.5, 4.0},  {0.5, 2.5},   {0.15,0.30}, { 0, 10},  {0.00,0.40}, {3, 0},     {0.15,0.0},   {0.35,0.10} };
constexpr AgeingProfile kHyper   { "crystallising",{ 0.02, 0.02},{ 0.00, 0.00}, { 2.0, 4.0},  {0.0, 0.0},   {0.04,0.06}, { 0, 0},   {0.00,0.00}, {0, 0},     {0.0,0.0},    {0.00,0.05} };

// name, density, E (GPa), eta, areal density (membranes), impossible, ageing
constexpr std::array<Material, kNumMaterials> kMaterials {{
    { "Steel",      7850, 200,  0.0002,   0,     false, kSteel   },
    { "Bronze",     8800, 100,  0.0003,   0,     false, kBronze  },
    { "Aluminium",  2700,  69,  0.0003,   0,     false, kAlu     },
    { "Brass",      8500, 100,  0.0005,   0,     false, kBrass   },
    { "Glass",      2500,  70,  0.001,    0,     false, kGlass   },
    { "Ceramic",    2400, 100,  0.002,    0,     false, kCeramic },
    { "Slate",      2800,  50,  0.006,    0,     false, kSlate   },
    { "Ice",         917,   9,  0.002,    0,     false, kIce     },
    { "Oak",         750,  12,  0.012,    0,     false, kOak     },
    { "Spruce",      450,  11,  0.008,    0,     false, kSpruce  },
    { "Bamboo",      600,  18,  0.010,    0,     false, kBamboo  },
    { "Rubber",     1200, 0.05, 0.15,     0,     false, kRubber  },
    { "Hide",       1100,   0,  0.02,     0.30,  false, kHide    },   // calf ~0.25 mm
    { "Mylar",      1390,   0,  0.01,     0.26,  false, kMylar   },   // 7.5 mil + coating
    { "Paper",       800,   4,  0.05,     0.20,  false, kPaper   },
    { "Hyperglass",  100, 150,  0.000002, 0,     true,  kHyper   },
}};
} // namespace

const Material& getMaterial (MaterialId id) noexcept
{
    const auto i = std::min (static_cast<std::size_t> (id), kNumMaterials - 1);
    return kMaterials[i];
}

MaterialId materialFromName (std::string_view name, MaterialId fallback) noexcept
{
    for (std::size_t i = 0; i < kNumMaterials; ++i)
    {
        const auto& n = kMaterials[i].name;
        if (n.size() == name.size()
            && std::equal (n.begin(), n.end(), name.begin(), [] (char a, char b)
                           { return std::tolower ((unsigned char) a) == std::tolower ((unsigned char) b); }))
            return static_cast<MaterialId> (i);
    }
    return fallback;
}

} // namespace foundry
