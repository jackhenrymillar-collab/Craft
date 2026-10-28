#pragma once

#include "ModalBank.h"

#include <array>

namespace foundry
{

// Live (automatable) mallet settings.
struct MalletParams
{
    double hardness = 0.5;  // 0 felt .. 1 hard
    double mass = 0.012;    // kg
};

// A mallet as a point mass on a Hertzian (power-law) contact spring with Hunt-Crossley
// damping: F = K * delta^alpha * (1 + lambda * delta'), delta = compression > 0.
// Contact time therefore depends on hardness, mass and velocity like a real stick.
// The contact force is solved implicitly each sample against the modes' response,
// which keeps it stable for very stiff tips.
class HertzMallet
{
public:
    struct Law
    {
        double K = 0.0, alpha = 1.5, lambda = 0.0;
    };
    static Law lawFor (double hardness) noexcept;
    // Radius of the contact patch: big soft felt .. small hard tip (m).
    static double tipRadiusFor (double hardness) noexcept;

    // Start a strike at a point with the given (treated) shape, landing at speed v0 (m/s).
    void start (const ModalBank& bank, const double* shape, double v0, const MalletParams& p) noexcept;
    [[nodiscard]] bool isActive() const noexcept { return active; }
    void cancel() noexcept { active = false; }

    // One sample: returns the force to apply at the strike point and advances the mallet.
    // Must be followed by bank.step(shape(), force).
    double process (const ModalBank& bank, double dt) noexcept;

    [[nodiscard]] const double* shape() const noexcept { return strikeShape.data(); }
    [[nodiscard]] double* shapeBuffer() noexcept { return strikeShape.data(); }
    [[nodiscard]] int contactSamples() const noexcept { return inContactSamples; }
    [[nodiscard]] double peakForce() const noexcept { return maxForce; }

private:
    std::array<double, kMaxModes> strikeShape {};
    Law law;
    double mass = 0.03, z = 0.0, vel = 0.0, deltaPrev = 0.0, deltaRate = 0.0, G = 0.0, maxForce = 0.0;
    int age = 0, inContactSamples = 0;
    bool active = false, touched = false;
};

} // namespace foundry
