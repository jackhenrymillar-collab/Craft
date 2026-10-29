#include "PadVoice.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace foundry
{

void PadVoice::prepare (double sr) noexcept
{
    sampleRate = sr;
    dt = 1.0 / sr;
    fadeLength = std::max (1, (int) std::lround (0.02 * sr));
    panic();
}

void PadVoice::panic() noexcept
{
    active.reset();
    fading.reset();
    hammer.cancel();
    fadeRemaining = 0;
    gritEnv = gritState = gritPrev = 0.0;
    asleep = true;
}

void PadVoice::detach (const CompiledInstrument*& activeOut, const CompiledInstrument*& fadingOut) noexcept
{
    activeOut = active.instrument();
    fadingOut = fadeRemaining > 0 ? fading.instrument() : nullptr;
    active.setInstrument (nullptr);
    fading.setInstrument (nullptr);
    panic();
}

double PadVoice::strikeSpeed (double velocity, double force) noexcept
{
    const double vel = std::clamp (velocity, 0.0, 1.0);
    const double f = std::clamp (force, 0.0, 1.0);
    // 0.25 .. ~6 m/s for a full-force, full-velocity hit.
    return (0.2 + 1.3 * f) * (0.25 + 3.75 * std::pow (vel, 1.5));
}

void PadVoice::setInstrument (const CompiledInstrument* ci) noexcept
{
    if (ci == nullptr || ci == active.instrument())
        return;

    const CompiledInstrument* old = active.instrument();
    if (old == nullptr)
    {
        active.setInstrument (ci);
        active.reset();
        return;
    }

    // If a previous crossfade is still running, drop it now (rare: two recompiles in 20 ms).
    if (fadeRemaining > 0 && fading.instrument() != nullptr)
        pushRetired (fading.instrument());

    fading = active;
    active.setInstrument (ci);
    active.transferFrom (fading);
    fadeRemaining = fadeLength;
    asleep = false;

    if (hammer.isActive())
    {
        // Keep the mallet in contact, but against the new mode shapes.
        hammer.cancel();
    }
}

void PadVoice::pushRetired (const CompiledInstrument* ci) noexcept
{
    if (ci != nullptr && retiredCount < (int) retired.size())
        retired[(size_t) retiredCount++] = ci;
}

const CompiledInstrument* PadVoice::takeRetired() noexcept
{
    if (retiredCount == 0)
        return nullptr;
    return retired[(size_t) --retiredCount];
}

double PadVoice::nextRandom() noexcept
{
    // xorshift64*, uniform in [-1, 1)
    rng ^= rng >> 12;
    rng ^= rng << 25;
    rng ^= rng >> 27;
    const auto x = rng * 2685821657736338717ull;
    return (double) (x >> 11) * (2.0 / 9007199254740992.0) - 1.0;
}

void PadVoice::trigger (double velocity) noexcept
{
    const double spread = std::clamp (params.spread, 0.0, 1.0);
    const double r = std::clamp (params.strikePosition + 0.25 * spread * nextRandom(), 0.0, 0.97);
    const double theta = std::numbers::pi * spread * nextRandom();
    triggerAt (velocity, r, theta);
}

void PadVoice::triggerAt (double velocity, double r, double theta) noexcept
{
    const auto* ci = active.instrument();
    if (ci == nullptr || velocity <= 0.0)
        return;
    const Vec2 p = MembraneBasis::polarToLocal (std::clamp (r, 0.0, 0.97), theta, ci->radius);

    // The tip isn't a point: average the mode shapes over its contact patch. A big soft
    // felt head smooths out the short-wavelength (high) modes; a small hard tip doesn't.
    const double tipRadius = HertzMallet::tipRadiusFor (params.hardness);
    double* shape = hammer.shapeBuffer();
    ci->shapeAt (p, shape, scratch.data());
    const int n = ci->n;
    for (int k = 0; k < n; ++k)
        shape[k] *= 0.25;
    constexpr int ringPoints = 6;
    for (int j = 0; j < ringPoints; ++j)
    {
        const double a = 2.0 * std::numbers::pi * j / ringPoints;
        const Vec2 q { p.x + 0.7 * tipRadius * std::cos (a), p.y + 0.7 * tipRadius * std::sin (a) };
        ci->shapeAt (q, scratch2.data(), scratch.data());
        for (int k = 0; k < n; ++k)
            shape[k] += (0.75 / ringPoints) * scratch2[(size_t) k];
    }
    // The high band has no spatial detail to average, so apply the patch's smoothing
    // directly (envelope of the disc average 2 J1(kb) / kb).
    for (int k = ci->nExact; k < n; ++k)
    {
        const double x = ci->wavenumber[(size_t) k] * tipRadius / 2.2;
        shape[k] *= 1.0 / std::pow (1.0 + x * x, 0.75);
    }
    // Modes we can't represent at this rate must not be excited.
    for (int k = 0; k < ci->n; ++k)
        if (ci->b1[(size_t) k] == 0.0)
            hammer.shapeBuffer()[k] = 0.0;
    MalletParams mp { params.hardness, params.malletMass };
    hammer.start (active, hammer.shapeBuffer(), strikeSpeed (velocity, params.force), mp);
    asleep = false;
}

double PadVoice::renderSample() noexcept
{
    const auto* ci = active.instrument();
    if (hammer.isActive())
    {
        const double F = hammer.process (active, dt);
        active.step (hammer.shape(), F);
    }
    else
        active.stepFree();

    double y = active.velocityAt (ci->micShape.data());

    if (fadeRemaining > 0)
    {
        fading.stepFree();
        const double old = fading.velocityAt (fading.instrument()->micShape.data());
        const double g = (double) fadeRemaining / fadeLength;
        y = (1.0 - g) * y + g * old;
        if (--fadeRemaining == 0)
        {
            pushRetired (fading.instrument());
            fading.setInstrument (nullptr);
        }
    }

    // Grit: crackle from loose flakes / torn edges, driven by how hard the surface moves.
    if (ci->grit > 0.0)
    {
        const double mag = std::abs (y);
        gritEnv += (mag - gritEnv) * (mag > gritEnv ? 0.02 : 0.0015);
        const double rate = 6000.0 * ci->grit * std::min (1.0, gritEnv * 4.0); // events per second
        if (0.5 * (nextRandom() + 1.0) < rate * dt)
            gritState += gritEnv * ci->grit * 0.9 * nextRandom();
        gritState *= 0.82;
        y += gritState - gritPrev;   // differentiated: short, bright clicks
        gritPrev = gritState;
    }
    return y * kMicGain;
}

void PadVoice::process (float* left, float* right, int n) noexcept
{
    const auto* ci = active.instrument();
    if (ci == nullptr || asleep)
        return;

    const double pan = std::clamp (params.pan, -1.0, 1.0);
    const double angle = (pan + 1.0) * 0.25 * std::numbers::pi;
    const double gl = params.level * std::cos (angle) * std::numbers::sqrt2;
    const double gr = params.level * std::sin (angle) * std::numbers::sqrt2;

    for (int i = 0; i < n; ++i)
    {
        double y = renderSample();
        if (! std::isfinite (y) || std::abs (y) > 1e3)
        {
            // Recover cleanly: silence this pad and flag it for the UI.
            panic();
            blowUps.fetch_add (1, std::memory_order_relaxed);
            y = 0.0;
        }
        left[i] += (float) (gl * y);
        right[i] += (float) (gr * y);
    }

    // Flush tiny states so an idle pad costs nothing and never goes denormal.
    if (! hammer.isActive() && fadeRemaining == 0 && active.energy() < 1e-18)
    {
        active.reset();
        gritEnv = gritState = gritPrev = 0.0;
        asleep = true;
    }
}

} // namespace foundry
