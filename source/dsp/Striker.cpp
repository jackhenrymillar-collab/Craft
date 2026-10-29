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

double Striker::solveContact (double delta0, double c, double K, double alpha, double lambda, double deltaPrev,
                              double dt) noexcept
{
    if (! (delta0 > 0.0) || ! (K > 0.0) || ! (c > 0.0))
        return 0.0;
    // Solve g(F) = F - K d^alpha D(d) = 0 on [0, delta0 / c], with d = delta0 - c F the
    // compression at the end of the step and D(d) = 1 + lambda (d - deltaPrev) / dt the
    // Hunt-Crossley damping, both taken implicitly. g is increasing in F (d falls, and
    // D rises with d), so bisection keeps Newton honest. An explicit damping term (using the
    // last step's rate) can pump energy into a contact/no-contact chatter.
    auto damp = [&] (double d) { return std::clamp (1.0 + lambda * (d - deltaPrev) / dt, 0.0, 6.0); };
    double lo = 0.0, hi = delta0 / c;
    double F = std::min (hi, K * std::pow (delta0, alpha) * damp (delta0));
    for (int it = 0; it < 40; ++it)
    {
        const double d = std::max (0.0, delta0 - c * F);
        const double Dd = damp (d);
        const double p = K * std::pow (d, alpha) * Dd;
        const double g = F - p;
        if (g > 0.0) hi = F; else lo = F;
        const bool dampActive = Dd > 0.0 && Dd < 6.0;
        const double dg = 1.0 + c * (K * alpha * std::pow (d, alpha - 1.0) * Dd
                                     + (dampActive ? K * std::pow (d, alpha) * lambda / dt : 0.0));
        double Fn = F - g / dg;
        if (! (Fn > lo && Fn < hi))
            Fn = 0.5 * (lo + hi);
        if (std::abs (Fn - F) <= 1e-12 * (1.0 + F))
            return Fn;
        F = Fn;
    }
    return F;
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
        case StrikerType::Brush:
            // The touch-down of a brush: many fine bristles, a soft, spread-out landing.
            m.law = power (1.8, 4.0 * std::pow (15.0, h), 1.2 - 0.6 * h);
            m.mass = 0.006;
            m.tipRadius = 0.03 - 0.015 * h;
            break;
        case StrikerType::Scrape:
            // The tip of the stick or rod settling onto the surface before it's dragged.
            m.law = power (1.5, 150.0 * std::pow (20.0, h), 0.5 - 0.4 * h);
            m.mass = 0.008;
            m.tipRadius = 0.004;
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
    G = bank.compliance (strikeShape.data());
    maxForce = 0.0;
    prevForce = 0.0;
    age = 0;
    inContactSamples = 0;
    bounces = 0;
    restingSamples = 0;
    separatedSamples = 0;
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
    const double F = solveContact (delta0, c, model.law.K, model.law.alpha, model.law.lambda, deltaPrev, dt);

    z = zFree - dt * dt / (2.0 * mass) * F;
    vel += dt * (model.gravity - F / mass);
    const double wNew = wFree + G * F;
    const double delta = z - wNew;
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
    else
    {
        // Lifted clear after the bounce off the surface (or held too long). The separation
        // has to last 0.3 ms: near-Nyquist ringing of the top modes can open a momentary gap
        // that isn't a real lift-off (and would make the contact time depend on the rate).
        if (touched && F == 0.0 && delta < -2e-4 && vel < surfaceVel)
            ++separatedSamples;
        else
            separatedSamples = 0;
        if (separatedSamples * dt >= 3e-4 || t > model.maxTime)
            active = false;
    }
    return F;
}

} // namespace foundry
