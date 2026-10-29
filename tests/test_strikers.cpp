// Strikers (Mallet, Hammer, Hand, Drop), the air mic, and stability across every part.
#include "test_helpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using Catch::Approx;
using namespace foundry;
using namespace foundry::test;

namespace
{
std::unique_ptr<CompiledInstrument> compilePart (PartType t, double sr = 48000.0)
{
    auto spec = defaultSpec();
    spec.part = PartSpec::defaultFor (t);
    return compileInstrument (spec, sr);
}

// Contact time (ms) of one strike, measured sample by sample.
double contactMs (const CompiledInstrument& ci, StrikerType type, double hardness)
{
    PadVoice v;
    v.prepare (ci.sampleRate);
    v.setInstrument (&ci);
    PadParams p;
    p.striker = type;
    p.hardness = hardness;
    p.spread = 0.0;
    v.setParams (p);
    v.trigger (0.8);
    float l[1] {}, r[1] {};
    int contact = 0;
    for (int i = 0; i < (int) ci.sampleRate && v.striker().isActive(); ++i)
    {
        v.process (l, r, 1);
        contact = v.striker().contactSamples();
    }
    return 1000.0 * contact / ci.sampleRate;
}
} // namespace

TEST_CASE ("A hammer stays on longer than a stick, a palm longer than a fingertip", "[strikers]")
{
    auto bar = compilePart (PartType::Bar);
    const double stick = contactMs (*bar, StrikerType::Mallet, 0.5);
    const double hammer = contactMs (*bar, StrikerType::Hammer, 0.5);
    INFO ("bar contact: stick " << stick << " ms, hammer " << hammer << " ms");
    CHECK (hammer > 1.3 * stick);

    auto box = compilePart (PartType::Box);
    const double palm = contactMs (*box, StrikerType::Hand, 0.0);
    const double finger = contactMs (*box, StrikerType::Hand, 1.0);
    INFO ("box contact: palm " << palm << " ms, fingertip " << finger << " ms");
    CHECK (palm > 2.0 * finger);
}

TEST_CASE ("A dropped ball bounces faster and faster, then settles", "[strikers][drop]")
{
    auto plate = compilePart (PartType::Plate);
    PadVoice v;
    v.prepare (48000.0);
    v.setInstrument (plate.get());
    PadParams p;
    p.striker = StrikerType::Drop;
    p.hardness = 1.0; // steel ball: lively
    p.spread = 0.0;
    v.setParams (p);
    v.trigger (1.0);

    // Record the sample index of each new impact.
    std::vector<int> impacts;
    float l[1] {}, r[1] {};
    int lastCount = 0, i = 0;
    for (; i < 48000 * 8 && v.striker().isActive(); ++i)
    {
        v.process (l, r, 1);
        if (v.striker().bounceCount() != lastCount)
        {
            impacts.push_back (i);
            lastCount = v.striker().bounceCount();
        }
    }
    INFO ("impacts: " << impacts.size() << ", settled after " << i / 48000.0 << " s");
    REQUIRE (impacts.size() >= 4);
    CHECK_FALSE (v.striker().isActive());   // it settles before the time limit
    // Gaps between the first bounces shrink (the ball loses energy each time).
    const int g1 = impacts[2] - impacts[1], g2 = impacts[3] - impacts[2];
    CHECK (g1 > 0);
    CHECK (g2 < g1);
    // Height h -> first flight time 2 v / g ~ 2 sqrt(2 h / g) times the restitution.
    const double v0 = std::sqrt (2.0 * 9.81 * PadVoice::dropHeight (1.0, p.force));
    CHECK ((impacts[1] - impacts[0]) / 48000.0 < 2.0 * v0 / 9.81);
}

TEST_CASE ("The air mic hears the hit later the further away it is", "[strikers][air]")
{
    auto ci = compilePart (PartType::Plate);
    auto onset = [&] (double distance) {
        PadParams p;
        p.spread = 0.0;
        p.airMix = 1.0;
        p.micDistance = distance;
        const auto r = renderHits (*ci, p, { { 0.0, 0.8 } }, 0.05);
        float peak = 0.0f;
        for (float s : r.left) peak = std::max (peak, std::abs (s));
        for (size_t i = 0; i < r.left.size(); ++i)
            if (std::abs (r.left[i]) > 0.02f * peak)
                return (double) i;
        return -1.0;
    };
    const double nearOnset = onset (0.1), farOnset = onset (2.0);
    const double expected = (2.0 - 0.1) / 343.0 * 48000.0;
    INFO ("onset near " << nearOnset << ", far " << farOnset << ", expected gap " << expected);
    CHECK (farOnset - nearOnset == Approx (expected).margin (6.0));
}

TEST_CASE ("Contact and air mics come out at similar levels on every part", "[strikers][levels]")
{
    for (int t = 0; t < kNumPartTypes; ++t)
    {
        auto ci = compilePart ((PartType) t);
        auto rms = [&] (double mix) {
            PadParams p;
            p.spread = 0.0;
            p.airMix = mix;
            p.micDistance = 0.5;
            const auto r = renderHits (*ci, p, { { 0.0, 0.6 } }, 0.1);
            double s = 0.0;
            for (float x : r.left) s += (double) x * x;
            return std::sqrt (s / (double) r.left.size());
        };
        const double contact = rms (0.0), air = rms (1.0);
        INFO (partInfo ((PartType) t).name << ": contact " << contact << ", air " << air);
        CHECK (contact > 0.004);
        CHECK (contact < 0.2);
        CHECK (air > 0.1 * contact);
        CHECK (air < 10.0 * contact);
    }
}

TEST_CASE ("Every striker on every part stays finite and bounded", "[strikers][stability]")
{
    for (double sr : { 44100.0, 96000.0 })
        for (int t = 0; t < kNumPartTypes; ++t)
        {
            auto ci = compilePart ((PartType) t, sr);
            for (int s = 0; s < kNumStrikerTypes; ++s)
                for (double hardness : { 0.0, 1.0 })
                {
                    PadVoice v;
                    v.prepare (sr);
                    v.setInstrument (ci.get());
                    PadParams p;
                    p.striker = (StrikerType) s;
                    p.hardness = hardness;
                    p.force = 1.0;
                    p.spread = 1.0;
                    p.airMix = 0.5;
                    v.setParams (p);
                    float l[64] {}, r[64] {};
                    double peak = 0.0;
                    for (int block = 0; block < 600; ++block)
                    {
                        if (block % 8 == 0)
                            v.trigger (1.0);
                        std::fill (l, l + 64, 0.0f);
                        v.process (l, r, 64);
                        for (float x : l)
                        {
                            REQUIRE (std::isfinite (x));
                            peak = std::max (peak, (double) std::abs (x));
                        }
                    }
                    INFO (partInfo ((PartType) t).name << " / " << strikerInfo ((StrikerType) s).name << " h " << hardness << " sr " << sr << " peak " << peak);
                    CHECK_FALSE (v.hadBlowUp());
                    CHECK (peak < 50.0);
                }
        }
}

TEST_CASE ("Every part loses energy monotonically once the striker lets go", "[strikers][energy]")
{
    for (int t = 0; t < kNumPartTypes; ++t)
    {
        auto ci = compilePart ((PartType) t);
        PadVoice v;
        v.prepare (48000.0);
        v.setInstrument (ci.get());
        PadParams p;
        p.spread = 0.0;
        v.setParams (p);
        v.trigger (1.0);
        float l[1] {}, r[1] {};
        for (int i = 0; i < 48000 && v.striker().isActive(); ++i)
            v.process (l, r, 1);
        INFO (partInfo ((PartType) t).name);
        REQUIRE_FALSE (v.striker().isActive());
        double prev = v.energy();
        REQUIRE (prev > 0.0);
        for (int i = 0; i < 24000; ++i)
        {
            v.process (l, r, 1);
            const double e = v.energy();
            REQUIRE (e <= prev * (1.0 + 1e-12));
            prev = e;
        }
    }
}

TEST_CASE ("Switching to a different kind of part while ringing crossfades cleanly", "[strikers][swap]")
{
    auto drum = compilePart (PartType::Membrane);
    auto bell = compilePart (PartType::Shell);
    PadVoice v;
    v.prepare (48000.0);
    v.setInstrument (drum.get());
    PadParams p;
    p.spread = 0.0;
    v.setParams (p);
    v.trigger (0.8);
    std::vector<float> l (24000, 0.0f), r (24000, 0.0f);
    v.process (l.data(), r.data(), 4800);
    v.setInstrument (bell.get());
    CHECK (v.energy() == 0.0); // different modes: the new part starts at rest
    v.process (l.data() + 4800, r.data() + 4800, 19200);
    CHECK (v.takeRetired() == drum.get());
    double maxStepBefore = 0.0, maxStepAround = 0.0;
    for (size_t i = 2401; i < 4800; ++i)
        maxStepBefore = std::max (maxStepBefore, (double) std::abs (l[i] - l[i - 1]));
    for (size_t i = 4800; i < 6000; ++i)
        maxStepAround = std::max (maxStepAround, (double) std::abs (l[i] - l[i - 1]));
    CHECK (maxStepAround < 1.5 * maxStepBefore);
}
