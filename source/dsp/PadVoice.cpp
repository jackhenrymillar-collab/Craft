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
    hitter.cancel();
    fadeRemaining = 0;
    gritEnv = gritState = gritPrev = 0.0;
    flowPrevActive = flowPrevFading = 0.0;
    delayLine.fill (0.0);
    airLowpass = airLowpass1 = 0.0;
    tailRemaining = 0;
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

double PadVoice::dropHeight (double velocity, double force) noexcept
{
    const double vel = std::clamp (velocity, 0.0, 1.0);
    const double f = std::clamp (force, 0.0, 1.0);
    // 1 cm .. 61 cm
    return 0.01 + 0.6 * (0.25 + 0.75 * f) * std::pow (vel, 1.5);
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
        flowPrevActive = 0.0;
        return;
    }

    // If a previous crossfade is still running, drop it now (rare: two recompiles in 20 ms).
    if (fadeRemaining > 0 && fading.instrument() != nullptr)
        pushRetired (fading.instrument());

    fading = active;
    flowPrevFading = flowPrevActive;
    active.setInstrument (ci);
    active.transferFrom (fading);
    flowPrevActive = active.velocityAt (ci->airShape.data());
    fadeRemaining = fadeLength;
    asleep = false;

    // The striker's shape belongs to the old modes: let it go.
    hitter.cancel();
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
    if (ci == nullptr)
        return;
    triggerAtPoint (velocity, ci->place ({ std::clamp (r, 0.0, 0.97), theta }));
}

void PadVoice::triggerAtPoint (double velocity, Vec2 p) noexcept
{
    const auto* ci = active.instrument();
    if (ci == nullptr || velocity <= 0.0 || ! ci->basis->contains (p))
        return;

    StrikerParams sp { params.striker, params.hardness, params.malletMass };
    const Striker::Model model = Striker::modelFor (sp);

    // The striking face isn't a point: average the mode shapes over its contact patch. A big
    // soft head smooths out the short-wavelength (high) modes; a small hard tip doesn't.
    // Points of the patch that fall off the part (a palm on a thin bar) don't count.
    const double tipRadius = model.tipRadius;
    double* shape = hitter.shapeBuffer();
    ci->shapeAt (p, shape, scratch.data());
    const int n = ci->n;
    constexpr int ringPoints = 6;
    double ringWeight = 0.0;
    std::array<Vec2, ringPoints> ring {};
    int inside = 0;
    for (int j = 0; j < ringPoints; ++j)
    {
        const double a = 2.0 * std::numbers::pi * j / ringPoints;
        const Vec2 q { p.x + 0.7 * tipRadius * std::cos (a), p.y + 0.7 * tipRadius * std::sin (a) };
        if (ci->basis->contains (q))
            ring[(size_t) inside++] = q;
    }
    if (inside > 0)
    {
        for (int k = 0; k < n; ++k)
            shape[k] *= 0.25;
        ringWeight = 0.75 / inside;
        for (int j = 0; j < inside; ++j)
        {
            ci->shapeAt (ring[(size_t) j], scratch2.data(), scratch.data());
            for (int k = 0; k < n; ++k)
                shape[k] += ringWeight * scratch2[(size_t) k];
        }
    }
    // The high band has no spatial detail to average, so apply the patch's smoothing
    // directly (envelope of the disc average 2 J1(kb) / kb).
    for (int k = ci->nExact; k < n; ++k)
    {
        const double x = ci->wavenumber[(size_t) k] * tipRadius / 2.2;
        shape[k] *= 1.0 / std::pow (1.0 + x * x, 0.75);
    }
    // Modes we can't represent at this rate must not be excited.
    for (int k = 0; k < n; ++k)
        if (ci->b1[(size_t) k] == 0.0)
            shape[k] = 0.0;

    const double v0 = params.striker == StrikerType::Drop
                          ? std::sqrt (2.0 * 9.81 * dropHeight (velocity, params.force))
                          : strikeSpeed (velocity, params.force) * model.speedScale;
    hitter.start (active, shape, v0, model);
    asleep = false;
}

double PadVoice::renderSample() noexcept
{
    const auto* ci = active.instrument();
    if (hitter.isActive())
    {
        const double F = hitter.process (active, dt);
        active.step (hitter.shape(), F);
    }
    else
        active.stepFree();

    double contact = ci->contactGain * active.velocityAt (ci->micShape.data());
    const double flow = active.velocityAt (ci->airShape.data());
    double air = ci->airGain * (flow - flowPrevActive) * sampleRate;
    flowPrevActive = flow;

    if (fadeRemaining > 0)
    {
        const auto* old = fading.instrument();
        fading.stepFree();
        const double oc = old->contactGain * fading.velocityAt (old->micShape.data());
        const double of = fading.velocityAt (old->airShape.data());
        const double oa = old->airGain * (of - flowPrevFading) * sampleRate;
        flowPrevFading = of;
        const double g = (double) fadeRemaining / fadeLength;
        contact = (1.0 - g) * contact + g * oc;
        air = (1.0 - g) * air + g * oa;
        if (--fadeRemaining == 0)
        {
            pushRetired (old);
            fading.setInstrument (nullptr);
        }
    }

    // Grit: crackle from loose flakes / torn edges, driven by how hard the surface moves.
    if (ci->grit > 0.0)
    {
        const double mag = std::abs (contact) * 5.0;
        gritEnv += (mag - gritEnv) * (mag > gritEnv ? 0.02 : 0.0015);
        const double rate = 6000.0 * ci->grit * std::min (1.0, gritEnv * 4.0); // events per second
        if (0.5 * (nextRandom() + 1.0) < rate * dt)
            gritState += gritEnv * ci->grit * 0.9 * nextRandom();
        gritState *= 0.82;
        const double click = 0.2 * (gritState - gritPrev); // differentiated: short, bright clicks
        gritPrev = gritState;
        contact += click;
        air += click;
    }

    // Air mic: distance delay, 1/r level and a gentle high cut that grows with distance.
    delayLine[(size_t) delayWrite] = air;
    double readPos = delayWrite - delaySamples;
    if (readPos < 0.0) readPos += kMaxDelay;
    const int i0 = (int) readPos;
    const double frac = readPos - i0;
    const double d0 = delayLine[(size_t) i0], d1 = delayLine[(size_t) ((i0 + 1) % kMaxDelay)];
    // Reading "forward" from i0 to i0+1 moves toward newer samples.
    const double delayed = d0 + frac * (d1 - d0);
    delayWrite = (delayWrite + 1) % kMaxDelay;
    airLowpass1 += airCoeff * (delayed - airLowpass1);
    airLowpass += airCoeff * (airLowpass1 - airLowpass);

    const double mix = std::clamp (params.airMix, 0.0, 1.0);
    return (1.0 - mix) * contact + mix * airLevel * airLowpass;
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

    // Air mic geometry, smoothed per block so moving it doesn't zip.
    const double dist = std::clamp (params.micDistance, 0.05, 5.0);
    const double targetDelay = std::min ((double) kMaxDelay - 4.0, dist / kSpeedOfSound * sampleRate);
    // Air absorbs highs, and a mic a little way off hears the part as a whole rather than
    // the click at the contact point: two gentle poles, lower with distance.
    const double fc = 14000.0 / (1.0 + 0.8 * dist);
    const double targetCoeff = 1.0 - std::exp (-2.0 * std::numbers::pi * std::min (fc, 0.45 * sampleRate) / sampleRate);
    const double targetLevel = std::min (3.0, 0.5 / std::max (dist, 0.1));
    const double smooth = asleep ? 1.0 : std::min (1.0, n / (0.03 * sampleRate));
    delaySamples += (targetDelay - delaySamples) * smooth;
    airCoeff += (targetCoeff - airCoeff) * smooth;
    airLevel += (targetLevel - airLevel) * smooth;

    double blockPeak = 0.0;
    for (int i = 0; i < n; ++i)
    {
        double y = renderSample();
        if (! std::isfinite (y) || std::abs (y) > 1e3)
        {
            // Recover cleanly: silence this pad and flag it for the UI.
            panic();
            blowUps.fetch_add (1, std::memory_order_relaxed);
            return;
        }
        blockPeak = std::max (blockPeak, std::abs (y));
        left[i] += (float) (gl * y);
        right[i] += (float) (gr * y);
    }

    // Flush tiny states so an idle pad costs nothing and never goes denormal. Wait for the
    // air mic's delay line to empty first.
    const bool quiet = ! hitter.isActive() && fadeRemaining == 0 && blockPeak < 3e-7
                    && active.energy() * ci->contactGain * ci->contactGain < 1e-14;
    if (! quiet)
        tailRemaining = (int) delaySamples + 64;
    else if ((tailRemaining -= n) <= 0)
    {
        active.reset();
        gritEnv = gritState = gritPrev = 0.0;
        flowPrevActive = 0.0;
        delayLine.fill (0.0);
        airLowpass = airLowpass1 = 0.0;
        asleep = true;
    }
}

} // namespace foundry
