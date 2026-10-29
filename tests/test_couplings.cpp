// M3: joined parts, rattles, loose objects, brush / scrape and 2x oversampling.
#include "test_helpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <cmath>

using namespace foundry;

namespace
{
double lowestOmega (const CompiledInstrument& ci)
{
    double w = 1e30;
    for (int k = 0; k < ci.nExact; ++k)
        w = std::min (w, ci.omega[(size_t) k]);
    return w;
}

// Closest exact mode to omega, as a ratio (1 = exact match).
double nearestRatio (const CompiledInstrument& ci, double omega)
{
    double best = 1e30;
    for (int k = 0; k < ci.nExact; ++k)
    {
        const double r = ci.omega[(size_t) k] / omega;
        if (std::abs (std::log (r)) < std::abs (std::log (best)))
            best = r;
    }
    return best;
}

double rms (const std::vector<float>& x, double sr, double from, double to)
{
    const auto a = (size_t) (from * sr), b = std::min (x.size(), (size_t) (to * sr));
    double s = 0.0;
    for (size_t i = a; i < b; ++i)
        s += (double) x[i] * x[i];
    return b > a ? std::sqrt (s / (double) (b - a)) : 0.0;
}

// Ratio of the first difference's energy to the signal's: rises with brightness.
double brightness (const std::vector<float>& x, double sr, double from, double to)
{
    const auto a = (size_t) (from * sr) + 1, b = std::min (x.size(), (size_t) (to * sr));
    double s = 0.0, d = 0.0;
    for (size_t i = a; i < b; ++i)
    {
        s += (double) x[i] * x[i];
        const double dx = (double) x[i] - x[i - 1];
        d += dx * dx;
    }
    return s > 0.0 ? d / s : 0.0;
}

bool allFinite (const test::Render& r)
{
    for (size_t i = 0; i < r.left.size(); ++i)
        if (! std::isfinite (r.left[i]) || ! std::isfinite (r.right[i]))
            return false;
    return true;
}

float peakOf (const test::Render& r)
{
    float p = 0.0f;
    for (size_t i = 0; i < r.left.size(); ++i)
        p = std::max ({ p, std::abs (r.left[i]), std::abs (r.right[i]) });
    return p;
}

InstrumentSpec barWith (PartType attach, JointType joint, double stiffness)
{
    auto s = test::defaultSpec();
    s.part = PartSpec::defaultFor (PartType::Bar);
    s.attach.enabled = true;
    s.attach.part = PartSpec::defaultFor (attach);
    s.attach.joint = joint;
    s.attach.stiffness = stiffness;
    return s;
}
} // namespace

TEST_CASE ("A soft rubber joint keeps both parts' own fundamentals", "[joints]")
{
    auto bar = test::defaultSpec();
    bar.part = PartSpec::defaultFor (PartType::Bar);
    auto plate = test::defaultSpec();
    plate.part = PartSpec::defaultFor (PartType::Plate);
    const auto ciBar = compileInstrument (bar, 48000.0);
    const auto ciPlate = compileInstrument (plate, 48000.0);
    const auto joined = compileInstrument (barWith (PartType::Plate, JointType::Rubber, 0.0), 48000.0);

    REQUIRE (joined->hasAttachment());
    CHECK (joined->nExact > ciBar->nExact);
    CHECK (std::abs (nearestRatio (*joined, lowestOmega (*ciBar)) - 1.0) < 0.03);
    CHECK (std::abs (nearestRatio (*joined, lowestOmega (*ciPlate)) - 1.0) < 0.03);
    for (int k = 0; k < joined->n; ++k)
    {
        CHECK (std::isfinite (joined->omega[(size_t) k]));
        CHECK (joined->omega[(size_t) k] > 0.0);
        CHECK (joined->sigma[(size_t) k] > 0.0);
    }
}

TEST_CASE ("A stiffer joint pulls the modes further from the separate parts", "[joints]")
{
    auto bar = test::defaultSpec();
    bar.part = PartSpec::defaultFor (PartType::Bar);
    auto plate = test::defaultSpec();
    plate.part = PartSpec::defaultFor (PartType::Plate);
    const auto ciBar = compileInstrument (bar, 48000.0);
    const auto ciPlate = compileInstrument (plate, 48000.0);
    // How far the joined pair's lowest modes sit from the nearest mode of either part on its
    // own, summed in log frequency.
    auto shift = [&] (JointType j, double stiffness) {
        const auto ci = compileInstrument (barWith (PartType::Plate, j, stiffness), 48000.0);
        double s = 0.0;
        for (int k = 0; k < std::min (12, ci->nExact); ++k)
        {
            const double w = ci->omega[(size_t) k];
            s += std::min (std::abs (std::log (nearestRatio (*ciBar, w))), std::abs (std::log (nearestRatio (*ciPlate, w))));
        }
        return s;
    };
    const double rubberSoft = shift (JointType::Rubber, 0.0);
    const double rubberHard = shift (JointType::Rubber, 1.0);
    const double bolt = shift (JointType::Bolt, 0.5);
    INFO ("rubber soft " << rubberSoft << ", rubber hard " << rubberHard << ", bolt " << bolt);
    CHECK (rubberSoft < 0.05);
    CHECK (rubberHard > 1.5 * rubberSoft);
    CHECK (bolt > 2.0 * rubberSoft);
    CHECK (bolt > 0.05);
}

TEST_CASE ("Joined parts lose energy monotonically once the striker lets go", "[joints][energy]")
{
    const auto ci = compileInstrument (barWith (PartType::Box, JointType::Bolt, 0.5), 48000.0);
    PadVoice v;
    v.prepare (48000.0);
    v.setInstrument (ci.get());
    v.trigger (0.9);
    float l[64] {}, r[64] {};
    double prev = -1.0;
    bool grew = false;
    for (int i = 0; i < 1500; ++i)
    {
        v.process (l, r, 64);
        if (v.striker().isActive())
        {
            prev = -1.0;
            continue;
        }
        const double e = v.energy();
        if (prev >= 0.0 && e > prev * (1.0 + 1e-9) + 1e-30)
            grew = true;
        prev = e;
    }
    CHECK_FALSE (grew);
}

TEST_CASE ("Snare wires make the drum brighter and shorter", "[contacts][snare]")
{
    auto plain = test::defaultSpec();
    auto snare = plain;
    snare.rattle.type = RattleType::Snare;
    const auto ciPlain = compileInstrument (plain, 48000.0);
    const auto ciSnare = compileInstrument (snare, 48000.0);
    REQUIRE (! ciSnare->contacts.empty());
    PadParams p;
    p.spread = 0.0;
    const auto a = test::renderHits (*ciPlain, p, { { 0.0, 0.8 } }, 1.0);
    const auto b = test::renderHits (*ciSnare, p, { { 0.0, 0.8 } }, 1.0);
    REQUIRE (allFinite (b));
    const double brightA = brightness (a.left, 48000.0, 0.02, 0.3);
    const double brightB = brightness (b.left, 48000.0, 0.02, 0.3);
    CHECK (brightB > 1.5 * brightA);
    // Wires take energy out of the head: the tail is quieter relative to the attack.
    const double tailA = rms (a.left, 48000.0, 0.6, 0.9) / rms (a.left, 48000.0, 0.0, 0.1);
    const double tailB = rms (b.left, 48000.0, 0.6, 0.9) / rms (b.left, 48000.0, 0.0, 0.1);
    CHECK (tailB < tailA);
}

TEST_CASE ("Rattles and loose objects stay bounded under the hardest hits", "[contacts][stability]")
{
    const int which = GENERATE (0, 1, 2, 3, 4, 5);
    auto spec = test::defaultSpec();
    spec.part = PartSpec::defaultFor (which % 2 == 0 ? PartType::Membrane : PartType::Plate);
    switch (which)
    {
        case 0: spec.rattle.type = RattleType::Snare; break;
        case 1: spec.rattle.type = RattleType::Sizzle; break;
        case 2: spec.loose.type = LooseType::Rice; spec.loose.amount = 1.0; break;
        case 3: spec.loose.type = LooseType::Beads; spec.loose.amount = 1.0; break;
        case 4: spec.loose.type = LooseType::Sand; spec.loose.amount = 1.0; break;
        default: spec.loose.type = LooseType::Chain; spec.loose.amount = 1.0; break;
    }
    const auto ci = compileInstrument (spec, 48000.0);
    REQUIRE (! ci->contacts.empty());
    PadParams p;
    p.striker = StrikerType::Hammer;
    p.hardness = 1.0;
    p.force = 1.0;
    p.rattle = 1.0;
    std::vector<std::pair<double, double>> hits;
    for (int i = 0; i < 12; ++i)
        hits.push_back ({ 0.04 * i, 1.0 });
    const auto r = test::renderHits (*ci, p, hits, 2.5);
    CHECK (allFinite (r));
    CHECK (peakOf (r) < 8.0f);
    // It dies away after the last hit.
    CHECK (rms (r.left, 48000.0, 2.2, 2.5) < 0.5 * rms (r.left, 48000.0, 0.4, 0.7));
}

TEST_CASE ("A tight or loose snare dies away completely", "[contacts][snare]")
{
    // Ten preloaded wires all touching the head are the hardest case for the contact solver:
    // solved independently they fed each other into a steady 775 Hz buzz.
    const double rattle = GENERATE (0.0, 0.1, 0.5, 1.0);
    const bool oversample = GENERATE (false, true);
    auto spec = test::defaultSpec();
    spec.rattle.type = RattleType::Snare;
    spec.oversample = oversample;
    const auto ci = compileInstrument (spec, 48000.0);
    PadParams p;
    p.rattle = rattle;
    p.hardness = 0.8;
    const auto r = test::renderHits (*ci, p, { { 0.05, 0.9 } }, 3.0);
    INFO ("rattle " << rattle << " oversample " << oversample);
    REQUIRE (allFinite (r));
    const double early = rms (r.left, 48000.0, 0.05, 0.15);
    const double late = rms (r.left, 48000.0, 2.5, 3.0);
    CHECK (late < 1e-4 * early);
}

TEST_CASE ("Loose objects come to rest on the part", "[contacts][loose]")
{
    const LooseType type = GENERATE (LooseType::Rice, LooseType::Beads, LooseType::Chain);
    // On the drum head (about 1 s of ring): on a hung plate that rings for 20 s the part
    // itself keeps kicking them.
    auto spec = test::defaultSpec();
    spec.loose.type = type;
    const auto ci = compileInstrument (spec, 48000.0);
    PadVoice v;
    v.prepare (48000.0);
    v.setInstrument (ci.get());
    v.trigger (0.9);
    float l[256] {}, r[256] {};
    double early = 0.0;
    for (int i = 0; i < (int) (4.0 * 48000.0 / 256); ++i)
    {
        v.process (l, r, 256);
        if (i == 20)
            early = v.contactEnergy();
    }
    INFO ("type " << (int) type << " early " << early << " late " << v.contactEnergy());
    CHECK (v.contactEnergy() < 1e-3 * early + 1e-12);
    for (size_t j = 0; j < ci->contacts.size(); ++j)
        CHECK (std::abs (v.contactPosition ((int) j)) < 0.01);
}

TEST_CASE ("Brush and scrape play on every part, finite and bounded", "[sweeps]")
{
    const int t = GENERATE (Catch::Generators::range (0, kNumPartTypes));
    auto spec = test::defaultSpec();
    spec.part = PartSpec::defaultFor ((PartType) t);
    const auto ci = compileInstrument (spec, 48000.0);
    for (auto type : { StrikerType::Brush, StrikerType::Scrape })
    {
        for (double h : { 0.0, 1.0 })
        {
            PadParams p;
            p.striker = type;
            p.hardness = h;
            p.force = 1.0;
            const auto r = test::renderHits (*ci, p, { { 0.0, 1.0 } }, 1.2);
            INFO ("part " << t << " striker " << (int) type << " hardness " << h);
            CHECK (allFinite (r));
            CHECK (peakOf (r) < 1.0f);
            // Still sounding in the middle of the stroke, not just at touch-down.
            CHECK (rms (r.left, 48000.0, 0.1, 0.2) > 1e-4);
        }
    }
}

TEST_CASE ("A sweep stops after its stroke", "[sweeps]")
{
    const auto ci = compileInstrument (test::defaultSpec(), 48000.0);
    PadVoice v;
    v.prepare (48000.0);
    v.setInstrument (ci.get());
    PadParams p;
    p.striker = StrikerType::Brush;
    v.setParams (p);
    v.trigger (1.0);
    float l[256] {}, r[256] {};
    v.process (l, r, 256);
    CHECK (v.isSweeping());
    for (int i = 0; i < 400; ++i)
        v.process (l, r, 256);
    CHECK_FALSE (v.isSweeping());
}

TEST_CASE ("2x oversampling keeps the same level and pitch", "[oversample]")
{
    auto spec = test::defaultSpec();
    spec.part = PartSpec::defaultFor (PartType::Plate);
    const auto plain = compileInstrument (spec, 48000.0);
    spec.oversample = true;
    const auto os = compileInstrument (spec, 48000.0);
    REQUIRE (os->oversample == 2);
    CHECK (os->sampleRate == 96000.0);

    PadParams p;
    p.spread = 0.0;
    // render through the voice at the host rate
    auto render = [&] (const CompiledInstrument& ci) {
        PadVoice v;
        v.prepare (48000.0);
        v.setInstrument (&ci);
        v.setParams (p);
        v.trigger (0.8);
        std::vector<float> l (48000), r (48000);
        for (int i = 0; i < 48000; i += 256)
            v.process (l.data() + i, r.data() + i, std::min (256, 48000 - i));
        return l;
    };
    const auto a = render (*plain);
    const auto b = render (*os);
    const double ra = rms (a, 48000.0, 0.0, 0.5), rb = rms (b, 48000.0, 0.0, 0.5);
    CHECK (std::abs (20.0 * std::log10 (rb / ra)) < 1.0);
    const double f0 = lowestOmega (*plain) / (2.0 * std::numbers::pi);
    const double fa = test::findPeak (a, 48000.0, f0, 0.05, 2400, 32768);
    const double fb = test::findPeak (b, 48000.0, f0, 0.05, 2400, 32768);
    CHECK (std::abs (fb / fa - 1.0) < 0.003);
}
