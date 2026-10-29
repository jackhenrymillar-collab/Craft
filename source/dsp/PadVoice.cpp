#include "PadVoice.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace foundry
{
namespace
{
constexpr double kPi = std::numbers::pi;

// How loud the clicks of rattles and loose objects are against the part's own sound. The
// wires and grains radiate a little themselves, and their clicks carry the top end the
// contact modes can't.
constexpr double kClickRadiation = 6e-5;
} // namespace

void PadVoice::prepare (double sr) noexcept
{
    hostRate = sr;
    os = 1;
    sampleRate = sr;
    dt = 1.0 / sr;
    fadeLength = std::max (1, (int) std::lround (0.02 * sr));
    panic();
}

void PadVoice::useRateOf (const CompiledInstrument* ci) noexcept
{
    os = ci != nullptr ? std::max (1, ci->oversample) : 1;
    sampleRate = hostRate * os;
    dt = 1.0 / sampleRate;
    fadeLength = std::max (1, (int) std::lround (0.02 * sampleRate));
    // Butterworth 4th order (two biquads) at 0.45 x host rate, run at 2x.
    if (os > 1)
    {
        const double fc = 0.45 * hostRate / sampleRate;
        const double K = std::tan (kPi * fc);
        const double qs[2] = { 0.54119610, 1.30656296 };
        for (int i = 0; i < 2; ++i)
        {
            auto& b = decim[(size_t) i];
            const double norm = 1.0 / (1.0 + K / qs[i] + K * K);
            b.b0 = K * K * norm;
            b.b1 = 2.0 * b.b0;
            b.b2 = b.b0;
            b.a1 = 2.0 * (K * K - 1.0) * norm;
            b.a2 = (1.0 - K / qs[i] + K * K) * norm;
            b.z1 = b.z2 = 0.0;
        }
    }
}

void PadVoice::panic() noexcept
{
    active.reset();
    fading.reset();
    hitter.cancel();
    sweep.active = false;
    fadeRemaining = 0;
    gritEnv = gritState = gritPrev = 0.0;
    flowPrevActive = flowPrevFading = 0.0;
    delayLine.fill (0.0);
    airLowpass = airLowpass1 = 0.0;
    clickState = clickLowpass = 0.0;
    for (auto& b : decim) b.z1 = b.z2 = 0.0;
    tailRemaining = 0;
    asleep = true;
    resetContacts();
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
    const bool sameRate = old != nullptr && std::max (1, old->oversample) == std::max (1, ci->oversample);
    if (old == nullptr || ! sameRate)
    {
        // First instrument, or the oversampling changed: start fresh (the old one can't run
        // alongside at a different rate).
        if (old != nullptr)
            pushRetired (old);
        if (fadeRemaining > 0 && fading.instrument() != nullptr)
            pushRetired (fading.instrument());
        fading.setInstrument (nullptr);
        fadeRemaining = 0;
        active.setInstrument (ci);
        useRateOf (ci);
        active.reset();
        hitter.cancel();
        sweep.active = false;
        flowPrevActive = 0.0;
        delayLine.fill (0.0);
        airLowpass = airLowpass1 = 0.0;
        rattleNow = -1.0;
        resetContacts();
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

    // The striker's and the sweep's shapes belong to the old modes: let them go. The rattles
    // and loose objects keep their own motion (the layout may have changed: re-seat them).
    hitter.cancel();
    sweep.active = false;
    rattleNow = -1.0;
    resetContacts();
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

// ---- Rattles and loose objects ----------------------------------------------------------
// Each one is a point mass at a fixed spot on the part, moving up and down, with a one-sided
// Hunt-Crossley contact against the surface. The contact force is solved implicitly per
// element against the element's own mass and the surface compliance at that spot. Forces
// are measured from the resting state (the part is already pressed down by whatever sits on
// it), so nothing thumps when a pad wakes up.

void PadVoice::updateContactRest (bool snap) noexcept
{
    const auto* ci = active.instrument();
    if (ci == nullptr)
        return;
    const double r = std::clamp (params.rattle, 0.0, 1.0);
    for (size_t j = 0; j < ci->contacts.size(); ++j)
    {
        const auto& c = ci->contacts[j];
        // Rest position of the tether, as a compression (negative = a gap).
        const double pen0 = -(c.gapTight + (c.gapLoose - c.gapTight) * r);
        const double weight = c.side > 0.0 ? c.mass * c.gravity : -c.mass * c.gravity;
        // Static balance: K p^alpha = weight - tetherK (p - pen0).
        auto ext = [&] (double p) { return weight - c.tetherK * (p - pen0); };
        double pStatic = 0.0, fStatic = 0.0;
        if (ext (0.0) <= 0.0)
        {
            // Held clear of the surface.
            pStatic = c.tetherK > 0.0 ? pen0 + weight / c.tetherK : -1e-3;
            fStatic = 0.0;
        }
        else
        {
            double lo = 0.0, hi = 1e-2;
            for (int it = 0; it < 80; ++it)
            {
                const double mid = 0.5 * (lo + hi);
                if (c.K * std::pow (mid, c.alpha) > ext (mid)) hi = mid; else lo = mid;
            }
            pStatic = 0.5 * (lo + hi);
            fStatic = c.K * std::pow (pStatic, c.alpha);
        }
        elemRest[j] = pen0;
        elemStaticF[j] = fStatic;
        elemStaticPen[j] = pStatic;
        if (snap)
        {
            // Element coordinate y (down = +) with the surface at its resting place.
            elemY[j] = c.side * pStatic;
            elemV[j] = 0.0;
            elemPrevF[j] = fStatic;
            elemPen[j] = pStatic;
        }
    }
    rattleNow = params.rattle;
}

void PadVoice::resetContacts() noexcept
{
    elemY.fill (0.0);
    elemV.fill (0.0);
    elemPrevF.fill (0.0);
    elemPen.fill (0.0);
    touching = 0;
    updateContactRest (true);
}

double PadVoice::contactEnergy() const noexcept
{
    const auto* ci = active.instrument();
    if (ci == nullptr)
        return 0.0;
    // Energy above the resting state: kinetic, plus the tether and the contact spring measured
    // from the static balance (gravity and the resting load are linear and cancel there).
    auto phi = [] (const ContactSite& c, double p) {
        return p > 0.0 ? c.K * std::pow (p, c.alpha + 1.0) / (c.alpha + 1.0) : 0.0;
    };
    double e = 0.0;
    for (size_t j = 0; j < ci->contacts.size(); ++j)
    {
        const auto& c = ci->contacts[j];
        const double pS = elemStaticPen[j];
        const double dy = elemY[j] - c.side * pS;
        e += 0.5 * c.mass * elemV[j] * elemV[j] + 0.5 * c.tetherK * dy * dy;
        const double p = elemPen[j];
        e += phi (c, p) - phi (c, pS) - elemStaticF[j] * (p - pS);
    }
    return std::max (0.0, e);
}

double PadVoice::processContacts (double* fm) noexcept
{
    const auto* ci = active.instrument();
    const int m = ci->contactModes;
    const auto nc = ci->contacts.size();
    double click = 0.0;
    int touchingNow = 0;
    for (size_t j = 0; j < nc; ++j)
    {
        const auto& c = ci->contacts[j];
        const double* phi = ci->contactShapes.data() + j * (size_t) m;
        const double side = c.side;

        // Free motion of the element this step (gravity and its tether, explicit).
        const double yRest = side * elemRest[j];
        const double tether = c.tetherK > 0.0 ? (-c.tetherK * (elemY[j] - yRest) - c.tetherDamping * elemV[j]) / c.mass : 0.0;
        const double a = c.gravity + tether;
        const double vFree = elemV[j] + dt * a;
        const double yFree = elemY[j] + dt * vFree;

        // Where the surface would be with no contact force.
        const double wFree = active.predictFreePartial (phi, m);
        const double delta0 = side * (yFree - wFree) + 0.0;
        double F = 0.0;
        if (delta0 > 0.0)
        {
            const double cTot = dt * dt / c.mass + c.compliance;
            F = Striker::solveContact (delta0, cTot, c.K, c.alpha, c.lambda, elemPen[j], dt);
            ++touchingNow;
        }
        // Element: pushed back by the contact.
        elemV[j] = vFree - side * dt * F / c.mass;
        elemY[j] = yFree - side * dt * dt * F / c.mass;
        elemPen[j] = delta0 - (dt * dt / c.mass + c.compliance) * F;

        // Surface: the change from the resting load.
        const double dF = F - elemStaticF[j];
        if (dF != 0.0)
        {
            const double fs = side * dF;
            for (int k = 0; k < m; ++k)
                fm[k] += fs * phi[k];
        }
        click += side * (F - elemPrevF[j]);
        elemPrevF[j] = F;

        if (! std::isfinite (elemY[j]) || std::abs (elemY[j]) > 1.0)
        {
            // Something got flung away: put everything back at rest.
            resetContacts();
            return 0.0;
        }
    }
    touching = touchingNow;
    return click;
}

// ---- Brush and scrape ----------------------------------------------------------------------

void PadVoice::startSweep (Vec2 p, double velocity) noexcept
{
    const auto* ci = active.instrument();
    const auto& b = *ci->basis;
    const double size = b.characteristicSize();
    const double spread = std::clamp (params.spread, 0.0, 1.0);
    // A stroke across the part from the strike point, in a random direction.
    double len = (0.35 + 0.6 * spread) * size;
    const double ang = kPi * nextRandom();
    Vec2 to { p.x + len * std::cos (ang), p.y + len * std::sin (ang) };
    for (int i = 0; i < 8 && ! b.contains (to); ++i)
    {
        len *= 0.7;
        to = { p.x + len * std::cos (ang), p.y + len * std::sin (ang) };
    }
    if (! b.contains (to))
        to = p;

    const double vel = std::clamp (velocity, 0.0, 1.0);
    const double f = std::clamp (params.force, 0.0, 1.0);
    sweep.type = params.striker;
    sweep.from = p;
    sweep.to = to;
    const double seconds = (sweep.type == StrikerType::Brush ? 0.32 : 0.45) * (1.25 - 0.5 * vel);
    sweep.length = std::max (64, (int) (seconds * sampleRate));
    sweep.pos = 0;
    sweep.segment = 0;
    // Downward pressure, newtons.
    sweep.pressure = (0.3 + 2.2 * std::pow (vel, 1.2)) * (0.4 + 0.6 * f);
    sweep.hardness = std::clamp (params.hardness, 0.0, 1.0);
    // Scrape: ridges every 1.5 mm (soft stick) .. 3 mm (ridged rod) at the drag speed.
    const double speed = std::max (0.05, len / seconds);
    sweep.ridgeRate = speed / (0.0015 + 0.0015 * sweep.hardness);
    sweep.ridgePhase = 0.0;
    sweep.noiseLp = sweep.noiseHp = sweep.pulse = 0.0;
    ci->shapeAt (sweep.from, sweepA.data(), scratch.data());
    sweepB = sweepA;
    sweep.active = true;
}

void PadVoice::processSweep (double* fm) noexcept
{
    const auto* ci = active.instrument();
    constexpr int seg = 64;
    if (sweep.pos % seg == 0)
    {
        // Next point on the path: shapes are interpolated between points 64 samples apart.
        sweepA = sweepB;
        const double u = std::min (1.0, (double) (sweep.pos + seg) / sweep.length);
        const Vec2 p { sweep.from.x + (sweep.to.x - sweep.from.x) * u, sweep.from.y + (sweep.to.y - sweep.from.y) * u };
        if (ci->basis->contains (p))
            ci->shapeAt (p, sweepB.data(), scratch.data());
        else
            std::fill (sweepB.begin(), sweepB.begin() + ci->n, 0.0);
    }
    const double frac = (double) (sweep.pos % seg) / seg;

    // Envelope: quick touch-down, steady stroke, lift-off.
    const double t = (double) sweep.pos / sampleRate, T = (double) sweep.length / sampleRate;
    const double env = std::min ({ 1.0, t / 0.015, std::max (0.0, (T - t) / 0.06) });

    // Texture. Brush: many bristles, filtered noise (brighter for stiff wire). Scrape: a
    // ridge every few millimetres plus grit.
    const double noise = nextRandom();
    const double fcN = sweep.type == StrikerType::Brush ? 1500.0 + 7000.0 * sweep.hardness : 3000.0 + 5000.0 * sweep.hardness;
    const double aN = 1.0 - std::exp (-2.0 * kPi * std::min (fcN, 0.45 * sampleRate) / sampleRate);
    sweep.noiseLp += aN * (noise - sweep.noiseLp);
    const double aH = 1.0 - std::exp (-2.0 * kPi * 300.0 / sampleRate);
    sweep.noiseHp += aH * (sweep.noiseLp - sweep.noiseHp);
    const double hiss = (sweep.noiseLp - sweep.noiseHp) * 2.2;

    double texture = hiss;
    if (sweep.type == StrikerType::Scrape)
    {
        sweep.ridgePhase += sweep.ridgeRate * (1.0 + 0.15 * nextRandom()) / sampleRate;
        if (sweep.ridgePhase >= 1.0)
        {
            sweep.ridgePhase -= 1.0;
            sweep.pulse += 1.0 + 0.3 * nextRandom();
        }
        const double ridge = sweep.pulse;
        // Sharper ridges ring shorter.
        sweep.pulse *= std::exp (-(1500.0 + 6000.0 * sweep.hardness) / sampleRate);
        texture = 0.45 * hiss + 1.6 * (ridge - 0.25);
    }
    const double F = ci->sweepGain * sweep.pressure * env * texture;
    const int n = ci->n;
    for (int k = 0; k < n; ++k)
        fm[k] += F * (sweepA[(size_t) k] + frac * (sweepB[(size_t) k] - sweepA[(size_t) k]));

    if (++sweep.pos >= sweep.length)
        sweep.active = false;
}

// ---- Triggering ------------------------------------------------------------------------------

void PadVoice::trigger (double velocity) noexcept
{
    const double spread = std::clamp (params.spread, 0.0, 1.0);
    const double r = std::clamp (params.strikePosition + 0.25 * spread * nextRandom(), 0.0, 0.97);
    const double theta = kPi * spread * nextRandom();
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
    std::array<Vec2, ringPoints> ring {};
    int inside = 0;
    for (int j = 0; j < ringPoints; ++j)
    {
        const double a = 2.0 * kPi * j / ringPoints;
        const Vec2 q { p.x + 0.7 * tipRadius * std::cos (a), p.y + 0.7 * tipRadius * std::sin (a) };
        if (ci->basis->contains (q))
            ring[(size_t) inside++] = q;
    }
    if (inside > 0)
    {
        for (int k = 0; k < n; ++k)
            shape[k] *= 0.25;
        const double ringWeight = 0.75 / inside;
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

    double v0 = params.striker == StrikerType::Drop
                    ? std::sqrt (2.0 * 9.81 * dropHeight (velocity, params.force))
                    : strikeSpeed (velocity, params.force) * model.speedScale;
    if (isSweep (params.striker))
    {
        // A brush or stick is put down gently, then dragged.
        v0 *= 0.12;
        startSweep (p, velocity);
    }
    hitter.start (active, shape, v0, model);
    if (asleep)
    {
        updateAirMic (1.0); // nothing is sounding: jump straight to the mic's place
        updateContactRest (true);
    }
    asleep = false;
}

// ---- Rendering -------------------------------------------------------------------------------

void PadVoice::updateAirMic (double smooth) noexcept
{
    // Air mic geometry, smoothed per block so moving it doesn't zip.
    const double dist = std::clamp (params.micDistance, 0.05, 5.0);
    const double targetDelay = std::min ((double) kMaxDelay - 4.0, dist / kSpeedOfSound * sampleRate);
    // Air absorbs highs, and a mic a little way off hears the part as a whole rather than
    // the click at the contact point: two gentle poles, lower with distance.
    const double fc = 14000.0 / (1.0 + 0.8 * dist);
    const double targetCoeff = 1.0 - std::exp (-2.0 * kPi * std::min (fc, 0.45 * sampleRate) / sampleRate);
    const double targetLevel = std::min (3.0, 0.5 / std::max (dist, 0.1));
    delaySamples += (targetDelay - delaySamples) * smooth;
    airCoeff += (targetCoeff - airCoeff) * smooth;
    airLevel += (targetLevel - airLevel) * smooth;
}

double PadVoice::renderSample() noexcept
{
    const auto* ci = active.instrument();
    const bool hasContacts = ! ci->contacts.empty();
    double click = 0.0;
    if (hasContacts || sweep.active)
    {
        // Several forces this step: project them all onto the modes, then step once. The
        // striker's force is solved first (implicitly, against the free response).
        const int n = ci->n;
        std::fill (modalForce.begin(), modalForce.begin() + n, 0.0);
        if (hitter.isActive())
        {
            const double F = hitter.process (active, dt);
            const double* s = hitter.shape();
            for (int k = 0; k < n; ++k)
                modalForce[(size_t) k] += F * s[k];
        }
        if (sweep.active)
            processSweep (modalForce.data());
        if (hasContacts)
            click = processContacts (modalForce.data());
        active.stepForces (modalForce.data());
    }
    else if (hitter.isActive())
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

    // Clicks of the rattles and loose objects: the change in contact force, high-passed.
    if (hasContacts)
    {
        clickLowpass += 0.05 * (click - clickLowpass);
        const double c = kClickRadiation * ci->contactGain * (click - clickLowpass) * (48000.0 / sampleRate);
        contact += c;
        air += c;
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
        const double clk = 0.2 * (gritState - gritPrev); // differentiated: short, bright clicks
        gritPrev = gritState;
        contact += clk;
        air += clk;
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
    const double angle = (pan + 1.0) * 0.25 * kPi;
    const double gl = params.level * std::cos (angle) * std::numbers::sqrt2;
    const double gr = params.level * std::sin (angle) * std::numbers::sqrt2;

    updateAirMic (std::min (1.0, n / (0.03 * hostRate)));
    if (! ci->contacts.empty() && std::abs (params.rattle - rattleNow) > 1e-6)
    {
        // Glide the Rattle setting (the rest load moves with it) rather than jumping.
        const double target = params.rattle;
        params.rattle = rattleNow < 0.0 ? target : rattleNow + std::clamp (target - rattleNow, -0.05, 0.05);
        updateContactRest (false);
        params.rattle = target;
    }

    double blockPeak = 0.0;
    for (int i = 0; i < n; ++i)
    {
        double y = renderSample();
        if (os > 1)
        {
            y = decim[1].run (decim[0].run (y));
            for (int k = 1; k < os; ++k)
                decim[1].run (decim[0].run (renderSample()));
        }
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
    const bool quiet = ! hitter.isActive() && ! sweep.active && fadeRemaining == 0 && blockPeak < 3e-7
                    && active.energy() * ci->contactGain * ci->contactGain < 1e-14
                    && contactEnergy() < 1e-12;
    if (! quiet)
        tailRemaining = (int) (delaySamples / os) + 64;
    else if ((tailRemaining -= n) <= 0)
    {
        active.reset();
        gritEnv = gritState = gritPrev = 0.0;
        flowPrevActive = 0.0;
        delayLine.fill (0.0);
        airLowpass = airLowpass1 = 0.0;
        clickState = clickLowpass = 0.0;
        for (auto& b : decim) b.z1 = b.z2 = 0.0;
        resetContacts();
        asleep = true;
    }
}

} // namespace foundry
