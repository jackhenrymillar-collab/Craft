#pragma once

#include "compile/CompiledInstrument.h"

#include <array>

namespace foundry
{

// Running state of one instrument's modes. Structure-of-arrays, fixed capacity,
// no allocation: safe to use on the audio thread.
class ModalBank
{
public:
    void reset() noexcept;
    void setInstrument (const CompiledInstrument* ci) noexcept { inst = ci; }
    [[nodiscard]] const CompiledInstrument* instrument() const noexcept { return inst; }

    // Displacement at a point (given its shape vector) after one free step, i.e. with no force.
    [[nodiscard]] double predictFree (const double* shape) const noexcept;
    // Extra displacement per newton of force applied at that point for one step.
    [[nodiscard]] double compliance (const double* shape) const noexcept;

    // Advance one sample with force F (newtons) applied at a point with the given shape.
    void step (const double* shape, double force) noexcept;
    void stepFree() noexcept;

    [[nodiscard]] double displacementAt (const double* shape) const noexcept;
    [[nodiscard]] double velocityAt (const double* shape) const noexcept;

    // Total mechanical energy (J) of the mass-normalised modes.
    [[nodiscard]] double energy() const noexcept;
    [[nodiscard]] bool isFinite() const noexcept;
    [[nodiscard]] bool isSilent (double threshold) const noexcept { return energy() < threshold; }

    // Carry the ringing state across a recompile so edits don't click or reset.
    void transferFrom (const ModalBank& other) noexcept;

    std::array<double, kMaxModes> q {}, v {};

private:
    const CompiledInstrument* inst = nullptr;
};

} // namespace foundry
