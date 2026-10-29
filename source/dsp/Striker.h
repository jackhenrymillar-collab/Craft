#pragma once

#include "ModalBank.h"

#include <array>

namespace foundry
{

// Live (automatable) striker settings.
struct StrikerParams
{
    StrikerType type = StrikerType::Mallet;
    double hardness = 0.5;   // 0 soft .. 1 hard; meaning depends on the type (see PartCatalogue)
    double malletMass = 0.012; // kg, Mallet only (the others have their own mass)
};

// Anything that hits: a point mass on a power-law contact spring with Hunt-Crossley damping,
// F = K * delta^alpha * (1 + lambda * delta'), delta = compression > 0. Contact time therefore
// depends on hardness, mass and speed like the real thing. The force is solved implicitly
// each sample against the modes' response, which keeps very stiff faces stable.
//
//   Mallet  12 g stick, felt .. hard tip (M1)
//   Hammer  350 g head, rubber face .. steel face: heavy, long push, lots of low end
//   Hand    palm (250 g, soft, very lossy, broad) .. fingertip (30 g, small): slaps and taps
//   Drop    a 20 g ball under gravity that keeps bouncing until it settles
class Striker
{
public:
    struct Law
    {
        double K = 0.0, alpha = 1.5, lambda = 0.0;
    };

    struct Model
    {
        Law law;
        double mass = 0.012;      // kg
        double tipRadius = 0.01;  // contact patch radius, m
        double gravity = 0.0;     // m/s^2 toward the surface (Drop)
        double maxTime = 0.08;    // s, retire after this whatever happens
        double speedScale = 1.0;  // how fast this striker lands for a given Force/velocity
        bool bounces = false;     // stays in play after separating (Drop)
    };

    static Model modelFor (const StrikerParams& p) noexcept;
    // The M1 mallet law and tip size (kept for tests and the Mallet model).
    static Law lawFor (double hardness) noexcept;
    static double tipRadiusFor (double hardness) noexcept;

    // Start a strike at a point with the given (treated) shape, landing at speed v0 (m/s).
    void start (const ModalBank& bank, const double* shape, double v0, const Model& m) noexcept;
    [[nodiscard]] bool isActive() const noexcept { return active; }
    void cancel() noexcept { active = false; }

    // One sample: returns the force to apply at the strike point and advances the striker.
    // Must be followed by bank.step(shape(), force).
    double process (const ModalBank& bank, double dt) noexcept;

    [[nodiscard]] const double* shape() const noexcept { return strikeShape.data(); }
    [[nodiscard]] double* shapeBuffer() noexcept { return strikeShape.data(); }
    [[nodiscard]] int contactSamples() const noexcept { return inContactSamples; }
    [[nodiscard]] double peakForce() const noexcept { return maxForce; }
    [[nodiscard]] int bounceCount() const noexcept { return bounces; }

private:
    std::array<double, kMaxModes> strikeShape {};
    Model model;
    double z = 0.0, vel = 0.0, deltaPrev = 0.0, deltaRate = 0.0, G = 0.0, maxForce = 0.0, prevForce = 0.0;
    double windowPeak = 0.0;
    int age = 0, inContactSamples = 0, bounces = 0, restingSamples = 0, windowSamples = 0;
    bool active = false, touched = false, settled = false, calm = false;
};

} // namespace foundry
