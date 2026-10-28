#include "test_helpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>

using Catch::Approx;
using namespace foundry;
using namespace foundry::test;

namespace
{
double meanSigmaRatio (const CompiledInstrument& a, const CompiledInstrument& b, int count)
{
    double s = 0.0;
    for (int k = 0; k < count; ++k)
        s += b.sigma[(size_t) k] / a.sigma[(size_t) k];
    return s / count;
}

// Largest relative split of the (1,1) cos/sin pair, which is degenerate on a perfect head.
double pairSplit (const CompiledInstrument& ci)
{
    return (ci.omega[2] - ci.omega[1]) / ci.omega[1];
}
}

TEST_CASE ("No treatment leaves the modes untouched", "[treatments]")
{
    auto ci = compileInstrument (defaultSpec(), 48000.0);
    CHECK (ci->X.empty());
    for (int k = 0; k < ci->nExact; ++k)
        CHECK (ci->omega[(size_t) k] == ci->basis->omega (k));
    CHECK (ci->grit == 0.0);
}

TEST_CASE ("Treatment transforms are consistent (X^-1 X = I)", "[treatments]")
{
    auto spec = defaultSpec();
    spec.treatments.deform.intensity = 0.7;
    spec.treatments.twist.amount = 0.6;
    spec.treatments.puncture.count = 0.3;
    auto ci = compileInstrument (spec, 48000.0);
    REQUIRE_FALSE (ci->X.empty());
    const int n = ci->nExact;
    for (int a = 0; a < n; a += 7)
        for (int b = 0; b < n; b += 5)
        {
            double s = 0.0;
            for (int i = 0; i < n; ++i)
                s += ci->Xinv[(size_t) (a * n + i)] * ci->X[(size_t) (i * n + b)];
            CHECK (s == Approx (a == b ? 1.0 : 0.0).margin (1e-8));
        }
    for (int k = 1; k < n; ++k)
        CHECK (ci->omega[(size_t) k] >= ci->omega[(size_t) k - 1]);
    CHECK (ci->n > ci->nExact); // the high band is there too
}

TEST_CASE ("Deform splits paired modes and grows with intensity", "[treatments][deform]")
{
    auto spec = defaultSpec();
    spec.treatments.deform.density = 0.4;
    spec.treatments.deform.intensity = 0.3;
    auto light = compileInstrument (spec, 48000.0);
    spec.treatments.deform.intensity = 1.0;
    auto heavy = compileInstrument (spec, 48000.0);
    INFO ("split light " << pairSplit (*light) << " heavy " << pairSplit (*heavy));
    CHECK (pairSplit (*light) > 1e-3);
    CHECK (pairSplit (*heavy) > pairSplit (*light));
    // Dents in a head are slack, dead spots: pitch drops, decay gets faster.
    CHECK (heavy->omega[0] < light->omega[0]);
    CHECK (meanSigmaRatio (*light, *heavy, 20) > 1.0);

    // Changing intensity must not move the dents.
    REQUIRE (light->features.size() == heavy->features.size());
    for (size_t i = 0; i < light->features.size(); ++i)
    {
        CHECK (light->features[i].p.x == heavy->features[i].p.x);
        CHECK (light->features[i].p.y == heavy->features[i].p.y);
    }
}

TEST_CASE ("Deform size and density change the result", "[treatments][deform]")
{
    auto spec = defaultSpec();
    spec.treatments.deform.intensity = 0.8;
    spec.treatments.deform.density = 0.0;
    auto one = compileInstrument (spec, 48000.0);
    spec.treatments.deform.density = 1.0;
    auto many = compileInstrument (spec, 48000.0);
    CHECK (many->features.size() > one->features.size());
    spec.treatments.deform.size = 1.0;
    auto big = compileInstrument (spec, 48000.0);
    CHECK (std::abs (big->omega[0] - many->omega[0]) > 1e-3 * many->omega[0]);
}

TEST_CASE ("Twist tightens the part and splits modes either way round", "[treatments][twist]")
{
    auto spec = defaultSpec();
    auto flat = compileInstrument (spec, 48000.0);
    spec.treatments.twist.amount = 0.8;
    auto cw = compileInstrument (spec, 48000.0);
    spec.treatments.twist.amount = -0.8;
    auto ccw = compileInstrument (spec, 48000.0);
    CHECK (cw->omega[0] > flat->omega[0] * 1.01);
    CHECK (ccw->omega[0] > flat->omega[0] * 1.01);

    spec.treatments.twist.amount = 0.8;
    spec.treatments.twist.pivot = 0.8;
    auto offCentre = compileInstrument (spec, 48000.0);
    CHECK (pairSplit (*offCentre) > 1e-3);

    spec.treatments.twist.wrinkle = 1.0;
    auto wrinkled = compileInstrument (spec, 48000.0);
    CHECK (wrinkled->grit > 0.0);
    CHECK (meanSigmaRatio (*offCentre, *wrinkled, 30) > 1.05);
}

TEST_CASE ("Puncture adds loss and detunes; more holes do more", "[treatments][puncture]")
{
    auto spec = defaultSpec();
    auto whole = compileInstrument (spec, 48000.0);
    spec.treatments.puncture.count = 0.25; // 8 holes
    auto few = compileInstrument (spec, 48000.0);
    spec.treatments.puncture.count = 1.0;  // 32 holes
    auto many = compileInstrument (spec, 48000.0);
    CHECK (std::count_if (many->features.begin(), many->features.end(),
                          [] (const auto& f) { return f.kind == TreatmentFeature::Kind::Hole; }) == kMaxPunctures);
    CHECK (meanSigmaRatio (*whole, *few, 30) > 1.02);
    CHECK (meanSigmaRatio (*whole, *many, 30) > meanSigmaRatio (*whole, *few, 30));

    double detuneFew = 0, detuneMany = 0;
    for (int k = 0; k < 30; ++k)
    {
        detuneFew += std::abs (std::log (few->omega[(size_t) k] / whole->omega[(size_t) k]));
        detuneMany += std::abs (std::log (many->omega[(size_t) k] / whole->omega[(size_t) k]));
    }
    CHECK (detuneFew > 1e-3);
    CHECK (detuneMany > detuneFew);

    spec.treatments.puncture.ragged = 1.0;
    auto torn = compileInstrument (spec, 48000.0);
    CHECK (meanSigmaRatio (*many, *torn, 30) > 1.0);
    CHECK (torn->grit > many->grit);
}

TEST_CASE ("Age follows each material's rules", "[treatments][age]")
{
    SECTION ("an old Mylar head stretches: lower, deader, uneven")
    {
        auto spec = defaultSpec();
        auto fresh = compileInstrument (spec, 48000.0);
        spec.treatments.age.amount = 1.0;
        auto old = compileInstrument (spec, 48000.0);
        CHECK (old->omega[0] < 0.9 * fresh->omega[0]);
        CHECK (old->sigma[0] / old->omega[0] > fresh->sigma[0] / fresh->omega[0]);
        CHECK (pairSplit (*old) > 1e-3);
    }
    SECTION ("calf hide: dry tightens it, damp makes it slack")
    {
        auto spec = defaultSpec();
        spec.membrane.material = MaterialId::Hide;
        auto fresh = compileInstrument (spec, 48000.0);
        spec.treatments.age.amount = 1.0;
        spec.treatments.age.climate = 0.0;
        auto dry = compileInstrument (spec, 48000.0);
        spec.treatments.age.climate = 1.0;
        auto damp = compileInstrument (spec, 48000.0);
        CHECK (dry->omega[0] > fresh->omega[0]);
        CHECK (damp->omega[0] < fresh->omega[0]);
        CHECK (damp->sigma[0] / damp->omega[0] > dry->sigma[0] / dry->omega[0]);
    }
    SECTION ("ageing never makes things explode")
    {
        for (int m = 0; m < (int) kNumMaterials; ++m)
        {
            auto spec = defaultSpec();
            spec.membrane.material = (MaterialId) m;
            spec.treatments.age.amount = 1.0;
            spec.treatments.age.climate = 1.0;
            auto ci = compileInstrument (spec, 48000.0);
            INFO (getMaterial ((MaterialId) m).name);
            for (int k = 0; k < ci->n; ++k)
            {
                CHECK (std::isfinite (ci->omega[(size_t) k]));
                CHECK (ci->sigma[(size_t) k] >= 0.0);
            }
        }
    }
}

TEST_CASE ("Ageing material data is complete and sane", "[materials]")
{
    for (int m = 0; m < (int) kNumMaterials; ++m)
    {
        const auto& mat = getMaterial ((MaterialId) m);
        INFO (mat.name);
        CHECK_FALSE (mat.ageing.process.empty());
        CHECK (mat.lossFactor > 0.0);
        CHECK (materialFromName (mat.name, MaterialId::Steel) == (MaterialId) m);
    }
}
