// Low end: the kick shell (air inside, front head, port), tension-modulation pitch drop and
// the kick beater.
#include "test_helpers.h"

#include "compile/MembraneSolver.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using Catch::Approx;
using namespace foundry;
using namespace foundry::test;

namespace
{
constexpr double kTwoPi = 2.0 * std::numbers::pi;

DrumShellBasis::Params kickParams()
{
    DrumShellBasis::Params p;
    p.batter.radius = 0.28;
    p.batter.tension = 700.0;
    p.batter.arealDensity = 0.26;
    p.batter.maxModes = 64;
    p.depth = 0.4;
    return p;
}

InstrumentSpec kickSpec()
{
    auto s = defaultSpec();
    s.part = PartSpec::defaultFor (PartType::Membrane);
    s.part.mount = 1;
    s.part.shape[0] = 0.86; // 22"
    s.part.shape[1] = 0.1;  // slack
    return s;
}

PadParams beater (double hardness, double pitchDrop)
{
    PadParams p;
    p.striker = StrikerType::Beater;
    p.hardness = hardness;
    p.spread = 0.0;
    p.strikePosition = 0.1;
    p.pitchDrop = pitchDrop;
    p.airMix = 0.5;
    return p;
}
} // namespace

TEST_CASE ("A membrane's analytic radiation matches quadrature", "[kick]")
{
    MembraneBasis::Params p;
    p.maxModes = 24;
    MembraneBasis b (p);
    std::vector<double> v, dx, dy, qv, qdx, qdy;
    b.radiation (v, dx, dy);
    b.ModalBasis::radiation (qv, qdx, qdy);
    double scale = 0.0;
    for (double x : qv) scale = std::max (scale, std::abs (x));
    for (int k = 0; k < b.size(); ++k)
    {
        CHECK (v[(size_t) k] == Approx (qv[(size_t) k]).margin (0.02 * scale));
        CHECK (dx[(size_t) k] == Approx (qdx[(size_t) k]).margin (0.02 * scale * p.radius));
        CHECK (dy[(size_t) k] == Approx (qdy[(size_t) k]).margin (0.02 * scale * p.radius));
    }
}

TEST_CASE ("The air in a closed shell stiffens a single head", "[kick]")
{
    auto p = kickParams();
    p.frontModes = 0;
    p.portRadius = 0.0;
    DrumShellBasis b (p);
    // The (0,1) mode is pushed well up; the (1,1) pair (no net volume) doesn't move.
    const double bare = b.bareFundamentalHz();
    double lowestBreathing = 1e9;
    std::vector<double> v, dx, dy;
    b.radiation (v, dx, dy);
    for (int k = 0; k < b.size(); ++k)
        if (std::abs (v[(size_t) k]) > 1e-3)
            lowestBreathing = std::min (lowestBreathing, b.omega (k) / kTwoPi);
    CHECK (lowestBreathing > 1.3 * bare);
    // A shallower shell has less air, so a stiffer spring.
    p.depth = 0.2;
    DrumShellBasis shallow (p);
    double lowestShallow = 1e9;
    shallow.radiation (v, dx, dy);
    for (int k = 0; k < shallow.size(); ++k)
        if (std::abs (v[(size_t) k]) > 1e-3)
            lowestShallow = std::min (lowestShallow, shallow.omega (k) / kTwoPi);
    CHECK (lowestShallow > lowestBreathing);
}

TEST_CASE ("Two heads beat through the air: one mode below the bare head, one far above", "[kick]")
{
    auto p = kickParams();
    p.portRadius = 0.0;
    DrumShellBasis b (p);
    const double bare = b.bareFundamentalHz();
    // Lowest mode: the heads move together (the air shifts, not squeezed), weighed down by the
    // outside air, so it sits below either bare head.
    CHECK (b.omega (0) / kTwoPi < bare);
    CHECK (b.omega (0) / kTwoPi > 0.4 * bare);
    // Some mode with lots of net breathing sits well above.
    std::vector<double> v, dx, dy;
    b.radiation (v, dx, dy);
    int loudest = 0;
    for (int k = 1; k < b.size(); ++k)
        if (std::abs (v[(size_t) k]) > std::abs (v[(size_t) loudest])) loudest = k;
    CHECK (b.omega (loudest) / kTwoPi > 1.5 * bare);
    for (int k = 0; k < b.size(); ++k)
        CHECK (b.extraDecay (k) >= 0.0);
}

TEST_CASE ("A port adds a low air mode that loses energy through the hole", "[kick]")
{
    auto p = kickParams();
    p.portRadius = 0.06;
    DrumShellBasis b (p);
    REQUIRE (b.helmholtzHz() > 20.0);
    int port = -1;
    for (int k = 0; k < b.size(); ++k)
        if (b.portParticipation (k) > 0.5) { port = k; break; }
    REQUIRE (port >= 0);
    // Compliant heads soften the air spring, so it sits below the rigid-wall Helmholtz value.
    CHECK (b.omega (port) / kTwoPi < b.helmholtzHz());
    CHECK (b.extraDecay (port) > 0.0);
    CHECK (b.materialLossWeight (port) < 0.5);
}

TEST_CASE ("A kick is mostly low end and stays stable", "[kick]")
{
    auto ci = compileInstrument (kickSpec(), 48000.0);
    REQUIRE (ci->pitchCoupling > 0.0);
    auto params = beater (0.3, 0.0);
    params.airMix = 0.0; // the contact mic on the batter, as a kick is usually miked close
    auto r = renderHits (*ci, params, { { 0.0, 1.0 }, { 0.25, 1.0 } }, 1.5);
    float peak = 0.0f;
    for (float x : r.left) { REQUIRE (std::isfinite (x)); peak = std::max (peak, std::abs (x)); }
    CHECK (peak < 1.0f);
    CHECK (peak > 0.01f);
    // Compare the low band with the mids over the first half second.
    const size_t len = 24000;
    double low = 0.0, mid = 0.0;
    for (double f = 35.0; f < 100.0; f += 5.0) low = std::max (low, toneMagnitude (r.left, 48000.0, f, 0, len));
    for (double f = 300.0; f < 2000.0; f += 25.0) mid = std::max (mid, toneMagnitude (r.left, 48000.0, f, 0, len));
    CHECK (low > 2.0 * mid);
}

TEST_CASE ("Pitch drop: a hard hit starts sharp and falls, without adding energy", "[kick]")
{
    auto ci = compileInstrument (kickSpec(), 48000.0);
    auto ratioAfter = [&] (double velocity, double pitchDrop, double seconds) {
        PadVoice v;
        v.prepare (48000.0);
        v.setInstrument (ci.get());
        v.setParams (beater (0.3, pitchDrop));
        v.trigger (velocity);
        std::vector<float> l (64), r (64);
        for (int i = 0; i < (int) (seconds * 48000.0 / 64); ++i)
            v.process (l.data(), r.data(), 64);
        return v.pitchRatio();
    };
    const double early = ratioAfter (1.0, 0.6, 0.05);
    const double late = ratioAfter (1.0, 0.6, 0.6);
    CHECK (early > 1.05);
    CHECK (late < early);
    CHECK (late >= 1.0);
    CHECK (ratioAfter (0.3, 0.6, 0.05) < early);   // harder hits glide further
    CHECK (ratioAfter (1.0, 0.0, 0.05) == 1.0);    // off is off
    CHECK (ratioAfter (1.0, 1.0, 0.05) <= 2.0);    // never past an octave

    // The glide only re-times the modes: the energy matches the same hit without it.
    PadVoice a, b;
    for (auto* v : { &a, &b })
    {
        v->prepare (48000.0);
        v->setInstrument (ci.get());
    }
    a.setParams (beater (0.3, 0.0));
    b.setParams (beater (0.3, 0.9));
    a.trigger (1.0);
    b.trigger (1.0);
    std::vector<float> l (64), r (64);
    double prev = 1e9;
    for (int i = 0; i < 750; ++i)
    {
        a.process (l.data(), r.data(), 64);
        b.process (l.data(), r.data(), 64);
        if (! b.striker().isActive())
        {
            CHECK (b.energy() <= prev * (1.0 + 1e-9));
            prev = b.energy();
        }
        CHECK (b.energy() <= 1.05 * a.energy());
        CHECK (b.energy() >= 0.85 * a.energy());
    }
    // The pitch really moves: early on, the ringing sits above the unglided fundamental.
    auto plain = renderHits (*ci, beater (0.3, 0.0), { { 0.0, 1.0 } }, 0.5);
    auto glide = renderHits (*ci, beater (0.3, 0.8), { { 0.0, 1.0 } }, 0.5);
    const double f0 = findPeak (plain.left, 48000.0, 55.0, 0.25, 2400, 9600);
    const double f1 = findPeak (glide.left, 48000.0, 1.15 * f0, 0.3, 2400, 9600);
    CHECK (f1 > 1.04 * f0);
}

TEST_CASE ("A felt beater sounds darker than a plastic one", "[kick]")
{
    auto ci = compileInstrument (kickSpec(), 48000.0);
    auto brightness = [&] (double hardness) {
        auto p = beater (hardness, 0.0);
        p.airMix = 0.0;
        auto r = renderHits (*ci, p, { { 0.0, 0.8 } }, 0.3);
        double low = 0.0, high = 0.0;
        for (double f = 40.0; f < 70.0; f += 2.0) low = std::max (low, toneMagnitude (r.left, 48000.0, f, 0, 9600));
        for (double f = 1000.0; f < 4000.0; f += 50.0) high += toneMagnitude (r.left, 48000.0, f, 0, 9600);
        REQUIRE (low > 0.0);
        return high / low;
    };
    CHECK (brightness (1.0) > 2.0 * brightness (0.0));
}
