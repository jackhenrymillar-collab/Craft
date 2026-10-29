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
// Something that buzzes or bounces against the main part: a snare wire, a rivet, a grain of
// rice. A point mass on a one-sided Hunt-Crossley contact with the surface, optionally held
// by a spring to a rest position. Coordinates: positive = down (into the part from above).
struct ContactSite
{
    enum class Kind { Wire, Rivet, Grain };
    Kind kind = Kind::Grain;
    Vec2 p;                  // on the main part, part-local metres
    double side = 1.0;       // +1 rests on top (presses down), -1 underneath (presses up)
    double mass = 1e-4;      // kg
    double K = 0.0, alpha = 1.5, lambda = 0.0;  // contact law F = K d^alpha (1 + lambda d')
    double gravity = 0.0;    // m/s^2, downward
    double tetherK = 0.0, tetherDamping = 0.0;  // spring to the rest position (0 = free)
    double gapTight = 0.0, gapLoose = 0.0;      // rest gap (m, negative = pressed in) at Rattle 0 / 1
    double compliance = 0.0; // surface compliance at the site over the contact modes, m/N per step
};

struct CompiledInstrument
{
    double sampleRate = 48000.0;                 // the rate the modes run at (host rate x oversample)
    int oversample = 1;                          // 1 or 2
    int n = 0;                                   // number of modes (exact + high band)
    int nExact = 0;                              // modes [0, nExact) are solved exactly
    int nBasis = 0;                              // basis modes of the main part (rows of X)
    std::shared_ptr<const ModalBasis> basis;     // untreated modes of the main part (shape tables)
    std::vector<double> X;                       // nBasis x nExact: final = X^T basis; empty = identity
    std::vector<double> Xinv;                    // X^-1, for carrying state across recompiles (no joint only)

    // A second part joined to the main one. Its shapes map to the final modes through
    // attachX (attachBasis->size() x nExact). The joint sits at jointMain / jointAttach.
    std::shared_ptr<const ModalBasis> attachBasis;
    std::vector<double> attachX;
    Vec2 jointMain, jointAttach;

    // Rattles and loose objects: sites plus their shapes over the lowest contactModes modes.
    std::vector<ContactSite> contacts;
    int contactModes = 0;
    std::vector<double> contactShapes;           // contacts.size() x contactModes

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
    // Brush and scrape force per newton of nominal pressure, so a sweep sits at about the same
    // level under a hit on every part (a fixed force would throw a light head around and barely
    // move a heavy bar). Set by the calibration.
    double sweepGain = 1.0;

    double grit = 0.0;
    std::vector<TreatmentFeature> features;
    InstrumentSpec spec;                         // the spec it was built from (material resolved)

    double solveMilliseconds = 0.0;
    std::uint64_t version = 0;

    // Final mode shapes at p on the main part (part-local metres). Real-time safe: no
    // allocation. `scratch` must hold at least n doubles.
    void shapeAt (Vec2 p, double* out, double* scratch) const noexcept;
    // The same on the joined part (zero if there is none).
    void attachShapeAt (Vec2 p, double* out, double* scratch) const noexcept;
    [[nodiscard]] bool hasAttachment() const noexcept { return attachBasis != nullptr; }

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
