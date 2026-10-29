#pragma once

#include "ModalBasis.h"
#include "TreatmentSolver.h"
#include "model/Instrument.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace foundry
{

// Immutable result of compiling an InstrumentSpec for one sample rate. Built off the
// audio thread, then handed over by pointer; the audio thread only reads it.
struct CompiledInstrument
{
    double sampleRate = 48000.0;
    int n = 0;                                   // number of modes (exact + high band)
    int nExact = 0;                              // modes [0, nExact) are solved exactly
    std::shared_ptr<const ModalBasis> basis;     // untreated modes (shape tables)
    std::vector<double> X;                       // nExact x nExact: treated = X^T basis; empty = identity
    std::vector<double> Xinv;                    // X^-1, for carrying state across recompiles

    // Above the exact modes, a 2-D part's modes get too dense to solve one by one, so each
    // high band is represented by one mode standing in for `count` real ones: it gets
    // sqrt(count) of a typical mode's coupling at the strike point (so the contact sees the
    // right local stiffness and the band gets the right energy) and a typical coupling at
    // the mics (so the band sums incoherently, like many modes would).
    std::vector<double> highStrike;              // size n, zero for exact modes
    std::vector<double> wavenumber;              // rad/m per mode (contact-patch filtering)

    std::vector<double> omega, sigma;            // treated modes
    // Exact discretisation of q'' + 2 sigma q' + omega^2 q = f with zero-order-hold force:
    // [q, v]' = A [q, v] + B f.
    std::vector<double> a11, a12, a21, a22, b1, b2;

    // Pickups. Contact mic: surface velocity at its point. Air mic: radiated pressure is the
    // time derivative of sum(airShape_k * v_k) (volume flow plus dipole and edge terms).
    std::vector<double> micShape;
    std::vector<double> airShape;
    // Output gains that bring a reference hit on any object to about the same level.
    double contactGain = 0.2, airGain = 1.0;

    double grit = 0.0;
    std::vector<TreatmentFeature> features;
    InstrumentSpec spec;                         // the spec it was built from (material resolved)

    double solveMilliseconds = 0.0;
    std::uint64_t version = 0;

    // Treated mode shapes at p (part-local metres). Real-time safe: no allocation.
    // `scratch` must hold at least n doubles.
    void shapeAt (Vec2 p, double* out, double* scratch) const noexcept;

    // Placement (0 centre .. 1 edge, angle) on this part.
    [[nodiscard]] Vec2 place (PointPolar pp) const noexcept { return basis->place (pp.r, pp.theta); }
    [[nodiscard]] PartType partType() const noexcept { return basis->type(); }
};

std::unique_ptr<CompiledInstrument> compileInstrument (const InstrumentSpec& spec, double sampleRate,
                                                       std::uint64_t version = 0);

// The untreated modes of a part (exposed for tests and the UI).
std::shared_ptr<const ModalBasis> makePartBasis (const PartSpec& part, double tuneSemitones, double sampleRate,
                                                 int maxModes, const AgeGlobals& age = {});

// Frequency ratio for the Size/Tune control.
double tuneRatio (double semitones) noexcept;

// Speed of sound used by the air mic and air columns.
constexpr double kSpeedOfSound = 343.0;

} // namespace foundry
