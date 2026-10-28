#pragma once

#include "ModalBasis.h"
#include "model/Materials.h"
#include "model/Treatments.h"

#include <vector>

namespace foundry
{

// Global (part-wide) effects of ageing, applied before the base modes are solved.
struct AgeGlobals
{
    double stiffnessScale = 1.0;  // multiplies tension / modulus
    double massScale = 1.0;       // multiplies mass per area
    double lossScale = 1.0;       // multiplies the material loss factor
    double highLossPerKHz = 0.0;  // added to the part's high-frequency damping
};

AgeGlobals computeAgeGlobals (const Material& material, const AgeParams& age) noexcept;

// A drawable/inspectable record of what the treatments did (for the UI and tests).
struct TreatmentFeature
{
    enum class Kind { Dent, Hole, Pit, Crack, TwistPivot };
    Kind kind;
    Vec2 p;
    double radius;
    double strength;
};

struct TreatedModes
{
    int n = 0;
    std::vector<double> omega;   // rad/s, ascending
    std::vector<double> sigma;   // decay rate 1/s (amplitude ~ exp(-sigma t))
    // Column k = new mode k as a combination of the basis modes: X[i * n + k].
    // Empty when the treatments are the identity (X = I).
    std::vector<double> X;
    std::vector<double> Xinv;    // X^-1 = X^T M (row k maps basis coords to new mode k)
    double grit = 0.0;           // 0..1 crackle from loose flakes, torn edges, wrinkles
    std::vector<TreatmentFeature> features;
    bool solvedOk = true;
};

struct TreatmentContext
{
    const ModalBasis* basis = nullptr;
    const Material* material = nullptr;
    double lossFactor = 0.01;        // material loss after ageing scale
    double highFreqDamping = 0.5;    // per kHz, after ageing
    double maxOmega = 1e9;           // modes above this are muted (Nyquist guard)
};

TreatedModes solveTreatments (const TreatmentContext& ctx, const Treatments& t);

// Material loss model shared by all parts: sigma = pi * eta * f * (1 + h * f / 1 kHz).
double materialDecayRate (double omega, double lossFactor, double highFreqDampingPerKHz) noexcept;

} // namespace foundry
