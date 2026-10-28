#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace foundry
{

// All material data lives in Materials.cpp so it's easy to tune by ear.
enum class MaterialId
{
    Steel, Bronze, Aluminium, Brass, Glass, Ceramic, Slate, Ice,
    Oak, Spruce, Bamboo, Rubber, Hide, Mylar, Paper, Hyperglass,
    Count
};

constexpr std::size_t kNumMaterials = static_cast<std::size_t> (MaterialId::Count);

// A pair of coefficients: one for a dry environment, one for a damp one.
// The Age "Climate" control blends between them.
struct DryDamp
{
    double dry = 0.0;
    double damp = 0.0;

    [[nodiscard]] double at (double climate) const noexcept { return dry + (damp - dry) * climate; }
};

// How a material changes as it ages. Every value is the effect at Age = 1
// (deliberately exaggerated: "decades in a skip"). Effects scale with Age.
struct AgeingProfile
{
    std::string_view process;   // human-readable: "rust", "stretch", "checking"...
    DryDamp stiffness;          // fractional change of modulus / tension (+ = stiffer)
    DryDamp mass;               // fractional change of mass per area (+ = heavier)
    DryDamp loss;               // fractional change of the loss factor (+1 = doubled)
    DryDamp highLoss;           // extra high-frequency loss, per kHz
    DryDamp unevenness;         // amplitude of a smooth random stiffness field (splits paired modes)
    DryDamp pits;               // surface pitting: number of pits at Age = 1 (0..60)
    DryDamp pitDepth;           // fraction of local thickness lost in a pit
    DryDamp cracks;             // number of cracks at Age = 1
    DryDamp crackFriction;      // loss factor of rubbing crack faces
    DryDamp grit;               // crackle from loose flakes/fibres (0..1)
};

struct Material
{
    std::string_view name;
    double density;       // kg/m^3 (for membranes: unused, see areal density)
    double youngsGPa;     // GPa (membranes use tension instead)
    double lossFactor;    // eta: decay rate sigma = pi * eta * f
    double arealDensity;  // kg/m^2, membranes only (0 otherwise)
    bool impossible;
    AgeingProfile ageing;
};

const Material& getMaterial (MaterialId id) noexcept;
MaterialId materialFromName (std::string_view name, MaterialId fallback) noexcept;

} // namespace foundry
