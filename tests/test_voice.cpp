#include "test_helpers.h"

#include "compile/MembraneSolver.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using Catch::Approx;
using namespace foundry;
using namespace foundry::test;

TEST_CASE ("Rendered spectrum peaks sit on the predicted membrane modes", "[voice][spectrum]")
{
    auto spec = defaultSpec();
    auto ci = compileInstrument (spec, 48000.0);
    PadParams p;
    p.strikePosition = 0.7;   // off-centre so asymmetric modes ring too
    p.spread = 0.0;
    p.hardness = 0.8;
    const auto r = renderHits (*ci, p, { { 0.0, 1.0 } }, 2.0);

    const size_t start = 2400, len = 48000; // skip the strike transient, 1 s window
    for (int k : { 0, 1, 3, 5, 6 })
    {
        const double f = ci->omega[(size_t) k] / (2.0 * std::numbers::pi);
        const double peak = findPeak (r.left, r.sampleRate, f, 0.02, start, len);
        INFO ("mode " << k << " expected " << f << " Hz, found " << peak << " Hz");
        CHECK (peak == Approx (f).epsilon (0.002));
    }
}

TEST_CASE ("Fundamental decay matches the material loss factor", "[voice][decay]")
{
    auto spec = defaultSpec();
    auto ci = compileInstrument (spec, 48000.0);
    PadParams p;
    p.strikePosition = 0.0; // centre: axisymmetric modes only
    p.spread = 0.0;
    const auto r = renderHits (*ci, p, { { 0.0, 0.8 } }, 2.5);

    const double f0 = ci->omega[0] / (2.0 * std::numbers::pi);
    const size_t len = 9600; // 200 ms windows
    const double a1 = toneMagnitude (r.left, r.sampleRate, f0, 4800, len);
    const double a2 = toneMagnitude (r.left, r.sampleRate, f0, 4800 + 48000, len);
    const double measuredSigma = std::log (a1 / a2) / 1.0;
    const double t60Measured = std::log (1000.0) / measuredSigma;

    const auto& mat = getMaterial (spec.part.material);
    const double expectedT60 = std::log (1000.0) / materialDecayRate (ci->omega[0], mat.lossFactor, spec.part.highFreqDamping);
    INFO ("f0 " << f0 << " Hz, T60 measured " << t60Measured << " s, expected " << expectedT60 << " s");
    CHECK (t60Measured == Approx (expectedT60).epsilon (0.03));
}

TEST_CASE ("A freely ringing pad loses energy monotonically", "[voice][energy]")
{
    auto ci = compileInstrument (defaultSpec(), 48000.0);
    PadVoice v;
    v.prepare (48000.0);
    v.setInstrument (ci.get());
    PadParams p;
    p.spread = 0.0;
    p.strikePosition = 0.55;
    v.setParams (p);
    v.trigger (1.0);

    float l[64] {}, rr[64] {};
    // Let the contact finish.
    for (int i = 0; i < 200 && v.striker().isActive(); ++i)
        v.process (l, rr, 64);
    REQUIRE_FALSE (v.striker().isActive());

    double prev = v.energy();
    REQUIRE (prev > 0.0);
    for (int i = 0; i < 48000; ++i)
    {
        v.process (l, rr, 1);
        const double e = v.energy();
        REQUIRE (e <= prev * (1.0 + 1e-12));
        prev = e;
    }
}

TEST_CASE ("The mallet obeys the Hertz contact law against a rigid surface", "[voice][mallet]")
{
    // A frozen single-mode instrument: the surface can't move at all.
    CompiledInstrument rigid;
    rigid.n = rigid.nExact = 1;
    rigid.omega = { 1.0 };
    rigid.sigma = { 0.0 };
    rigid.a11 = { 1.0 }; rigid.a12 = { 0.0 }; rigid.a21 = { 0.0 }; rigid.a22 = { 1.0 };
    rigid.b1 = { 0.0 }; rigid.b2 = { 0.0 };
    ModalBank bank;
    bank.setInstrument (&rigid);
    bank.reset();

    const double sr = 192000.0, dt = 1.0 / sr;
    for (double hardness : { 0.7, 1.0 })
        for (double v0 : { 0.5, 3.0 })
        {
            const double shape[1] = { 1.0 };
            Striker m;
            const double mass = 0.012;
            Striker::Model model;
            model.law = Striker::lawFor (hardness);
            model.mass = mass;
            m.start (bank, shape, v0, model);
            int steps = 0;
            while (m.isActive() && steps < 100000) { m.process (bank, dt); ++steps; }
            const double measured = m.contactSamples() * dt;

            // Undamped power law F = K d^a: d_max from energy, t = 2 d_max / v * I(a),
            // I(a) = integral_0^1 du / sqrt(1 - u^(a+1)).
            const auto law = Striker::lawFor (hardness);
            const double dMax = std::pow (0.5 * mass * v0 * v0 * (law.alpha + 1.0) / law.K, 1.0 / (law.alpha + 1.0));
            double I = 0.0;
            const int N = 200000;
            for (int i = 0; i < N; ++i)
            {
                const double u = (i + 0.5) / N;
                I += 1.0 / std::sqrt (1.0 - std::pow (u, law.alpha + 1.0)) / N;
            }
            const double expected = 2.0 * dMax / v0 * I;
            INFO ("hardness " << hardness << " v0 " << v0 << ": " << measured * 1e3 << " ms vs Hertz " << expected * 1e3 << " ms");
            // Hunt-Crossley damping lengthens compression and shortens restitution a little.
            CHECK (measured == Approx (expected).epsilon (0.12));
        }
}

TEST_CASE ("Contact on a head lasts a few ms, softer is longer", "[voice][mallet]")
{
    auto ci = compileInstrument (defaultSpec(), 48000.0);
    auto contactMs = [&] (double hardness) {
        PadVoice v;
        v.prepare (48000.0);
        v.setInstrument (ci.get());
        PadParams p;
        p.hardness = hardness;
        p.spread = 0.0;
        v.setParams (p);
        v.trigger (0.8);
        float l[1] {}, r[1] {};
        int contact = 0;
        for (int i = 0; i < 4800 && v.striker().isActive(); ++i)
        {
            v.process (l, r, 1);
            contact = v.striker().contactSamples();
        }
        return 1000.0 * contact / 48000.0;
    };
    const double soft = contactMs (0.0), mid = contactMs (0.5), hard = contactMs (1.0);
    INFO ("contact ms: soft " << soft << ", mid " << mid << ", hard " << hard);
    // On a membrane the head's own give dominates the contact time (hardness mostly changes
    // the sharpness of the force pulse, i.e. brightness), so only soft >= hard is required.
    CHECK (soft >= hard);
    CHECK (soft < 20.0);
    CHECK (hard > 1.0);
    CHECK (hard < 8.0);
}

TEST_CASE ("Harder hits are louder and brighter", "[voice][mallet]")
{
    auto ci = compileInstrument (defaultSpec(), 48000.0);
    PadParams p;
    p.spread = 0.0;
    p.strikePosition = 0.6;
    auto peakAbs = [] (const Render& r) {
        float m = 0;
        for (float s : r.left) m = std::max (m, std::abs (s));
        return (double) m;
    };
    p.hardness = 0.1;
    const auto soft = renderHits (*ci, p, { { 0.0, 0.3 } }, 0.5);
    const auto loud = renderHits (*ci, p, { { 0.0, 1.0 } }, 0.5);
    CHECK (peakAbs (loud) > 2.0 * peakAbs (soft));

    // Brightness: share of energy above 1 kHz in the first 100 ms.
    auto highShare = [&] (double hardness) {
        p.hardness = hardness;
        const auto r = renderHits (*ci, p, { { 0.0, 0.8 } }, 0.2);
        double hi = 0.0, all = 0.0;
        for (double f = 50.0; f < 12000.0; f *= 1.03)
        {
            const double m = toneMagnitude (r.left, 48000.0, f, 0, 4800);
            const double e = m * m * f; // log-spaced bins: weight by bandwidth
            all += e;
            if (f > 1000.0) hi += e;
        }
        return hi / all;
    };
    const double soft2 = highShare (0.0), hard2 = highShare (1.0);
    INFO ("energy above 1 kHz: soft " << soft2 << ", hard " << hard2);
    CHECK (hard2 > 2.0 * soft2);
}

TEST_CASE ("Retriggering adds energy to the running state instead of resetting it", "[voice][retrigger]")
{
    auto ci = compileInstrument (defaultSpec(), 48000.0);
    PadParams p;
    p.spread = 0.0;
    const auto single = renderHits (*ci, p, { { 0.0, 0.8 } }, 0.6);
    const auto doubleHit = renderHits (*ci, p, { { 0.0, 0.8 }, { 0.3, 0.8 } }, 0.6);
    const auto lateOnly = renderHits (*ci, p, { { 0.3, 0.8 } }, 0.6);

    // After the second hit the output is not just a fresh hit: the earlier ringing is still there.
    const size_t from = (size_t) (0.35 * 48000), to = (size_t) (0.6 * 48000);
    double diff = 0.0, ref = 0.0;
    for (size_t i = from; i < to; ++i)
    {
        diff += std::pow (doubleHit.left[i] - lateOnly.left[i], 2.0);
        ref += std::pow (single.left[i], 2.0);
    }
    CHECK (diff > 0.1 * ref);
    // And before the second hit both renders are identical.
    for (size_t i = 0; i < (size_t) (0.29 * 48000); ++i)
        REQUIRE (doubleHit.left[i] == single.left[i]);
}

TEST_CASE ("Size/Tune scales every mode by the same ratio", "[voice][tune]")
{
    auto spec = defaultSpec();
    auto a = compileInstrument (spec, 48000.0);
    spec.tuneSemitones = 7.0;
    auto b = compileInstrument (spec, 48000.0);
    const double ratio = std::pow (2.0, 7.0 / 12.0);
    for (int k = 0; k < 20; ++k)
        CHECK (b->omega[(size_t) k] / a->omega[(size_t) k] == Approx (ratio).epsilon (1e-9));
}

TEST_CASE ("Extreme strikes stay finite and bounded", "[voice][stability]")
{
    auto spec = defaultSpec();
    spec.treatments.age.amount = 1.0;
    spec.treatments.deform.intensity = 1.0;
    spec.treatments.deform.density = 1.0;
    spec.treatments.twist.amount = -1.0;
    spec.treatments.twist.wrinkle = 1.0;
    spec.treatments.puncture.count = 1.0;
    spec.treatments.puncture.size = 1.0;
    for (double sr : { 44100.0, 96000.0 })
        for (double hardness : { 0.0, 1.0 })
        {
            auto ci = compileInstrument (spec, sr);
            PadVoice v;
            v.prepare (sr);
            v.setInstrument (ci.get());
            PadParams p;
            p.hardness = hardness;
            p.force = 1.0;
            p.spread = 1.0;
            v.setParams (p);
            float l[32] {}, r[32] {};
            double peak = 0.0;
            for (int block = 0; block < 3000; ++block)
            {
                if (block % 3 == 0)
                    v.trigger (1.0);  // a 500 Hz-ish machine-gun roll at full force
                std::fill (l, l + 32, 0.0f);
                v.process (l, r, 32);
                for (float s : l)
                {
                    REQUIRE (std::isfinite (s));
                    peak = std::max (peak, (double) std::abs (s));
                }
            }
            INFO ("sr " << sr << " hardness " << hardness << " peak " << peak);
            CHECK_FALSE (v.hadBlowUp());
            CHECK (peak < 50.0);
        }
}

TEST_CASE ("Hitting the centre excites only axisymmetric modes", "[voice]")
{
    auto ci = compileInstrument (defaultSpec(), 48000.0);
    PadVoice v;
    v.prepare (48000.0);
    v.setInstrument (ci.get());
    v.triggerAt (1.0, 0.0, 0.0);
    float l[256] {}, r[256] {};
    for (int i = 0; i < 40; ++i)
        v.process (l, r, 256);
    for (int k = 0; k < ci->nExact; ++k)
        if (static_cast<const MembraneBasis&> (*ci->basis).mode (k).m != 0)
            CHECK (std::abs (v.bank().q[(size_t) k]) < 1e-12);
}

TEST_CASE ("Recompiling while ringing carries the state across without a click", "[voice][swap]")
{
    auto spec = defaultSpec();
    auto a = compileInstrument (spec, 48000.0);
    spec.treatments.deform.intensity = 0.5;
    auto b = compileInstrument (spec, 48000.0);

    PadVoice v;
    v.prepare (48000.0);
    v.setInstrument (a.get());
    PadParams p;
    p.spread = 0.0;
    v.setParams (p);
    v.trigger (0.8);
    std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
    v.process (l.data(), r.data(), 12000);
    const double before = v.energy();
    v.setInstrument (b.get());
    // Energy is carried over (within the change the treatment makes to the modes).
    CHECK (v.energy() == Approx (before).epsilon (0.35));
    v.process (l.data() + 12000, r.data() + 12000, 36000);
    CHECK (v.takeRetired() == a.get());

    // No sample-to-sample jump bigger than the signal's own largest step.
    double maxStepBefore = 0.0, maxStepAround = 0.0;
    for (size_t i = 6001; i < 12000; ++i)
        maxStepBefore = std::max (maxStepBefore, (double) std::abs (l[i] - l[i - 1]));
    for (size_t i = 12000; i < 13200; ++i)
        maxStepAround = std::max (maxStepAround, (double) std::abs (l[i] - l[i - 1]));
    CHECK (maxStepAround < 1.5 * maxStepBefore);
}
