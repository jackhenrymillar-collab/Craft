#include "Striker.h"

#include <algorithm>
#include <cmath>

namespace foundry
{

Striker::Law Striker::lawFor (double hardness) noexcept
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

double Striker::tipRadiusFor (double hardness) noexcept
{
    const double h = std::clamp (hardness, 0.0, 1.0);
    return 0.02 * std::pow (0.15, h); // 20 mm felt .. 3 mm tip
}

Striker::Model Striker::modelFor (const StrikerParams& p) noexcept
{
    const double h = std::clamp (p.hardness, 0.0, 1.0);
    Model m;
    auto power = [] (double alpha, double f1mm, double lambda) {
        return Law { f1mm / std::pow (1e-3, alpha), alpha, lambda };
    };
    switch (p.type)
    {
        case StrikerType::Mallet:
            m.law = lawFor (h);
            m.mass = std::max (1e-3, p.malletMass);
            m.tipRadius = tipRadiusFor (h);
            break;
        case StrikerType::Hammer:
            // Rubber-faced (dead-blow) .. bare steel face: Hertz contact, 2 kN .. 500 kN at 1 mm.
            m.law = power (1.5, 2000.0 * std::pow (250.0, h), 0.6 - 0.55 * h);
            m.mass = 0.35;
            m.tipRadius = 0.012 * (1.0 - 0.5 * h);
            m.maxTime = 0.15;
            break;
        case StrikerType::Hand:
            // Flesh is soft and very lossy, so a hand barely bounces; a flat palm is heavy and
            // broad (dull thump), a fingertip light and small (a tap).
            m.law = power (2.0, 15.0 * std::pow (20.0, h), 1.5 - 0.7 * h);
            m.mass = 0.25 * std::pow (0.12, h);
            m.tipRadius = 0.045 * std::pow (0.155, h);
            m.maxTime = 0.15;
            break;
        case StrikerType::Drop:
            // A 20 g ball: rubber (50 N/mm, lossy) .. steel (100 kN/mm, lively).
            m.law = power (1.5, 50.0 * std::pow (2000.0, h), 0.3 * (1.0 - h) + 0.01);
            m.mass = 0.02;
            m.tipRadius = 0.005;
            m.gravity = 9.81;
            m.maxTime = 6.0;
            m.bounces = true;
            break;
        case StrikerType::Count: break;
    }
    // Heavy strikers swing slower for the same Force: momentum grows only as mass^0.55, so a
    // hammer is about 15 dB louder than a stick rather than 26 dB (and still sounds heavy:
    // the long, soft push is what you hear).
    if (! m.bounces)
        m.speedScale = std::min (1.0, std::pow (0.012 / m.mass, 0.45));
    return m;
}

void Striker::start (const ModalBank& bank, const double* shapeIn, double v0, const Model& m) noexcept
{
    const int n = bank.instrument()->n;
    std::copy (shapeIn, shapeIn + n, strikeShape.begin());
    model = m;
    model.mass = std::max (1e-3, model.mass);
    // Land exactly on the (possibly still ringing) surface: retriggering adds energy.
    z = bank.displacementAt (strikeShape.data());
    vel = std::max (0.0, v0);
    deltaPrev = 0.0;
    deltaRate = 0.0;
    G = bank.compliance (strikeShape.data());
    maxForce = 0.0;
    prevForce = 0.0;
    age = 0;
    inContactSamples = 0;
    bounces = 0;
    restingSamples = 0;
    active = true;
    touched = false;
    settled = false;
    calm = false;
    windowPeak = 0.0;
    windowSamples = 0;
}

double Striker::process (const ModalBank& bank, double dt) noexcept
{
    if (! active)
        return 0.0;

    const double mass = model.mass;
    const double wFree = bank.predictFree (strikeShape.data());
    const double zFree = z + dt * vel + 0.5 * model.gravity * dt * dt;
    const double delta0 = zFree - wFree;
    const double c = dt * dt / (2.0 * mass) + G;
    const double damping = std::clamp (1.0 + model.law.lambda * deltaRate, 0.0, 4.0);
    const double Kh = model.law.K * damping;
    const double alpha = model.law.alpha;

    double F = 0.0;
    if (delta0 > 0.0 && Kh > 0.0)
    {
        // Solve g(F) = F - Kh * (delta0 - c F)^alpha = 0 on [0, delta0 / c]; g is increasing.
        double lo = 0.0, hi = delta0 / c;
        F = std::min (hi, Kh * std::pow (delta0, alpha));
        for (int it = 0; it < 30; ++it)
        {
            const double d = std::max (0.0, delta0 - c * F);
            const double p = Kh * std::pow (d, alpha);
            const double g = F - p;
            if (g > 0.0) hi = F; else lo = F;
            const double dg = 1.0 + Kh * alpha * std::pow (d, alpha - 1.0) * c;
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
    vel += dt * (model.gravity - F / mass);
    const double wNew = wFree + G * F;
    const double delta = z - wNew;
    deltaRate = (delta - deltaPrev) / dt;
    deltaPrev = delta;
    maxForce = std::max (maxForce, F);
    const double surfaceVel = bank.velocityAt (strikeShape.data());

    if (F > 0.0)
    {
        if (prevForce == 0.0)
        {
            // A new impact. A ball that lands this slowly is about to settle.
            ++bounces;
            if (model.bounces && touched && vel - surfaceVel < 0.03)
                settled = true;
        }
        touched = true;
        ++inContactSamples;
        ++restingSamples;
    }
    else
        restingSamples = 0;
    prevForce = F;

    ++age;
    const double t = age * dt;
    if (model.bounces)
    {
        // Retire once it has settled (between two tiny bounces), if it ends up resting in
        // contact, or at the time limit.
        // Also when the ball has stopped moving much (chattering on a ringing surface).
        windowPeak = std::max (windowPeak, std::abs (vel));
        if (++windowSamples * dt >= 0.05)
        {
            calm = windowPeak < 0.05;
            windowPeak = 0.0;
            windowSamples = 0;
        }
        if ((settled && F == 0.0) || (calm && t > 0.1) || restingSamples * dt > 0.25 || t > model.maxTime)
            active = false;
    }
    else if ((touched && F == 0.0 && delta < -2e-4 && vel < surfaceVel) || t > model.maxTime)
        // Lifted clear after the bounce off the surface (or held too long).
        active = false;
    return F;
}

} // namespace foundry
