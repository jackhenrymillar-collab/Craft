#include "HertzMallet.h"

#include <algorithm>
#include <cmath>

namespace foundry
{

HertzMallet::Law HertzMallet::lawFor (double hardness) noexcept
{
    const double h = std::clamp (hardness, 0.0, 1.0);
    Law l;
    // Felt is a soft, strongly non-linear spring; a hard tip approaches Hertz (alpha = 1.5).
    l.alpha = 2.6 - 1.1 * h;
    // Force at 1 mm compression, log-spaced from 3 N (soft felt) to 4 kN (hard tip).
    const double f1mm = 3.0 * std::pow (1300.0, h);
    l.K = f1mm / std::pow (1e-3, l.alpha);
    l.lambda = 0.45 - 0.4 * h; // s/m, felt hysteresis
    return l;
}

double HertzMallet::tipRadiusFor (double hardness) noexcept
{
    const double h = std::clamp (hardness, 0.0, 1.0);
    return 0.02 * std::pow (0.15, h); // 20 mm felt .. 3 mm tip
}

void HertzMallet::start (const ModalBank& bank, const double* shapeIn, double v0, const MalletParams& p) noexcept
{
    const int n = bank.instrument()->n;
    std::copy (shapeIn, shapeIn + n, strikeShape.begin());
    law = lawFor (p.hardness);
    mass = std::max (1e-3, p.mass);
    // Land exactly on the (possibly still ringing) surface: retriggering adds energy.
    z = bank.displacementAt (strikeShape.data());
    vel = std::max (0.0, v0);
    deltaPrev = 0.0;
    deltaRate = 0.0;
    G = bank.compliance (strikeShape.data());
    maxForce = 0.0;
    age = 0;
    inContactSamples = 0;
    active = true;
    touched = false;
}

double HertzMallet::process (const ModalBank& bank, double dt) noexcept
{
    if (! active)
        return 0.0;

    const double wFree = bank.predictFree (strikeShape.data());
    const double zFree = z + dt * vel;
    const double delta0 = zFree - wFree;
    const double c = dt * dt / (2.0 * mass) + G;
    const double damping = std::clamp (1.0 + law.lambda * deltaRate, 0.0, 4.0);
    const double Kh = law.K * damping;

    double F = 0.0;
    if (delta0 > 0.0 && Kh > 0.0)
    {
        // Solve g(F) = F - Kh * (delta0 - c F)^alpha = 0 on [0, delta0 / c]; g is increasing.
        double lo = 0.0, hi = delta0 / c;
        F = std::min (hi, Kh * std::pow (delta0, law.alpha));
        for (int it = 0; it < 30; ++it)
        {
            const double d = std::max (0.0, delta0 - c * F);
            const double p = Kh * std::pow (d, law.alpha);
            const double g = F - p;
            if (g > 0.0) hi = F; else lo = F;
            const double dg = 1.0 + Kh * law.alpha * std::pow (d, law.alpha - 1.0) * c;
            double Fn = F - g / dg;
            if (! (Fn > lo && Fn < hi))
                Fn = 0.5 * (lo + hi);
            if (std::abs (Fn - F) <= 1e-12 * (1.0 + F))
            {
                F = Fn;
                break;
            }
            F = Fn;
        }
    }

    z = zFree - dt * dt / (2.0 * mass) * F;
    vel -= dt * F / mass;
    const double wNew = wFree + G * F;
    const double delta = z - wNew;
    deltaRate = (delta - deltaPrev) / dt;
    deltaPrev = delta;
    maxForce = std::max (maxForce, F);
    if (F > 0.0)
    {
        touched = true;
        ++inContactSamples;
    }

    // Lift the mallet once it has bounced clear (or after 80 ms whatever happens).
    ++age;
    const double surfaceVel = bank.velocityAt (strikeShape.data());
    if ((touched && F == 0.0 && delta < -2e-4 && vel < surfaceVel) || age * dt > 0.08)
        active = false;
    return F;
}

} // namespace foundry
