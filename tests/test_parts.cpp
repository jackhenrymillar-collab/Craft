// Physics of the M2 parts: mode ratios against the textbook series, orthonormality,
// the Helmholtz resonance, the spring's dispersion and the air column.
#include "test_helpers.h"

#include "compile/PlateSolver.h"
#include "compile/ShellSolver.h"
#include "compile/StripSolver.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using Catch::Approx;
using namespace foundry;

namespace
{
constexpr double kPi = std::numbers::pi;

PartSpec part (PartType t)
{
    return PartSpec::defaultFor (t);
}

std::shared_ptr<const ModalBasis> basisFor (const PartSpec& p, int modes = 96)
{
    return makePartBasis (p, 0.0, 48000.0, modes);
}

// Bending (non-torsion) mode frequencies of a strip basis, ascending.
std::vector<double> bendingOmegas (const ModalBasis& b)
{
    const auto& s = static_cast<const StripBasis&> (b);
    std::vector<double> w;
    for (int k = 0; k < s.size(); ++k)
        if (! s.mode (k).torsion && s.mode (k).lossWeight > 0.0)
            w.push_back (s.omega (k));
    return w;
}

// Mass matrix of the first `count` modes by brute-force quadrature.
std::vector<double> massMatrix (const ModalBasis& b, int count, int resolution)
{
    std::vector<QuadPoint> quad;
    b.quadrature (quad, resolution);
    std::vector<double> phi ((size_t) b.size()), M ((size_t) (count * count), 0.0);
    for (const auto& q : quad)
    {
        b.shapes (q.p, phi.data());
        const double w = q.weight * b.massPerAreaAt (q.p);
        for (int i = 0; i < count; ++i)
            for (int j = 0; j < count; ++j)
                M[(size_t) (i * count + j)] += w * phi[(size_t) i] * phi[(size_t) j];
    }
    return M;
}
} // namespace

TEST_CASE ("A uniform free bar follows the Euler-Bernoulli series", "[parts][bar]")
{
    auto p = part (PartType::Bar);
    p.shape[2] = 0.0; // no undercut
    const auto w = bendingOmegas (*basisFor (p));
    REQUIRE (w.size() >= 5);
    const double expected[] = { 1.0, 2.7565, 5.4039, 8.9330, 13.3443 };
    for (size_t k = 0; k < 5; ++k)
        CHECK (w[k] / w[0] == Approx (expected[k]).epsilon (0.002));

    // f1 = (4.730^2 / 2 pi L^2) sqrt(E t^2 / 12 rho), oak, 40 cm x 20 mm.
    const auto& oak = getMaterial (MaterialId::Oak);
    const double f1 = 4.730041 * 4.730041 / (2.0 * kPi * 0.4 * 0.4) * std::sqrt (oak.youngsGPa * 1e9 * 0.02 * 0.02 / (12.0 * oak.density));
    CHECK (w[0] / (2.0 * kPi) == Approx (f1).epsilon (0.002));
}

TEST_CASE ("A clamped bar follows the cantilever series", "[parts][bar]")
{
    auto p = part (PartType::Bar);
    p.shape[2] = 0.0;
    p.mount = 1;
    const auto w = bendingOmegas (*basisFor (p));
    REQUIRE (w.size() >= 4);
    const double expected[] = { 1.0, 6.2669, 17.5475, 34.3861 };
    for (size_t k = 0; k < 4; ++k)
        CHECK (w[k] / w[0] == Approx (expected[k]).epsilon (0.003));
}

TEST_CASE ("Undercutting a bar lowers it and stretches its overtones", "[parts][bar]")
{
    auto flat = part (PartType::Bar);
    flat.shape[2] = 0.0;
    auto cut = flat;
    cut.shape[2] = 1.0;
    const auto a = bendingOmegas (*basisFor (flat)), b = bendingOmegas (*basisFor (cut));
    INFO ("undercut ratio f2/f1 = " << b[1] / b[0]);
    CHECK (b[0] < 0.75 * a[0]);          // thinner middle: much lower fundamental
    CHECK (b[1] / b[0] == Approx (4.0).epsilon (0.03)); // full undercut: the marimba's 1 : 4
}

TEST_CASE ("Simply supported plate modes match the exact formula", "[parts][plate]")
{
    auto p = part (PartType::Plate);
    p.mount = 1; // framed
    const auto b = basisFor (p);
    const auto& pb = static_cast<const PlateBasis&> (*b);
    const double W = pb.getParams().width, H = pb.getParams().height;
    for (int k = 0; k < 20; ++k)
    {
        const auto& m = pb.mode (k);
        const double expected = pb.bendingSpeedSquared() * kPi * kPi * ((m.i / W) * (m.i / W) + (m.j / H) * (m.j / H));
        CHECK (pb.omega (k) == Approx (expected).epsilon (1e-9));
    }
    // (1,1) of a 1.5 mm steel sheet 50 x 35.7 cm: f = (pi / 2) sqrt(D / rho h) (1/W^2 + 1/H^2)
    const auto& steel = getMaterial (MaterialId::Steel);
    const double D = steel.youngsGPa * 1e9 * std::pow (0.0015, 3) / (12.0 * (1.0 - 0.09));
    const double f11 = 0.5 * kPi * std::sqrt (D / (steel.density * 0.0015)) * (1.0 / (W * W) + 1.0 / (H * H));
    CHECK (pb.omega (0) / (2.0 * kPi) == Approx (f11).epsilon (1e-6));
}

TEST_CASE ("A free square plate's first mode is the twisting mode near Leissa's value", "[parts][plate]")
{
    PlateBasis::Params pp;
    pp.width = pp.height = 0.4;
    pp.thickness = 0.002;
    pp.freeEdges = true;
    PlateBasis b (pp);
    REQUIRE (b.mode (0).i == 1);
    REQUIRE (b.mode (0).j == 1);
    // lambda = omega a^2 sqrt(rho h / D) = 13.47 for nu = 0.3 (Leissa 1969); Warburton's
    // approximation is within a few per cent.
    const double lambda = b.omega (0) * pp.width * pp.width / b.bendingSpeedSquared();
    INFO ("lambda = " << lambda);
    CHECK (lambda == Approx (13.47).epsilon (0.07));
}

TEST_CASE ("Part modes are mass-orthonormal", "[parts][orthonormal]")
{
    for (auto t : { PartType::Bar, PartType::Plate, PartType::Shell, PartType::Tube, PartType::Spring })
    {
        auto p = part (t);
        if (t == PartType::Tube)
            p.shape[2] = 0.0; // air-column modes are normalised to the air, not the wall
        const auto b = basisFor (p, 24);
        const int count = std::min (12, b->size());
        const auto M = massMatrix (*b, count, 160);
        for (int i = 0; i < count; ++i)
            for (int j = 0; j < count; ++j)
            {
                INFO (partInfo (t).name << " M[" << i << "][" << j << "]");
                CHECK (M[(size_t) (i * count + j)] == Approx (i == j ? 1.0 : 0.0).margin (0.03));
            }
    }
}

TEST_CASE ("Bowl and bell partials follow their patterns", "[parts][shell]")
{
    // Bowl: inextensional ring bending, f(m) ~ m (m^2 - 1) / sqrt(m^2 + 1).
    CHECK (ShellBasis::ratio (3, 0, 0.0) == Approx (2.828).epsilon (0.002));
    CHECK (ShellBasis::ratio (4, 0, 0.0) == Approx (5.423).epsilon (0.002));
    // Bell: prime, tierce, quint and nominal relative to the hum.
    CHECK (ShellBasis::ratio (2, 1, 1.0) == Approx (2.0));
    CHECK (ShellBasis::ratio (3, 1, 1.0) == Approx (2.38));
    CHECK (ShellBasis::ratio (3, 2, 1.0) == Approx (3.0));
    CHECK (ShellBasis::ratio (4, 1, 1.0) == Approx (4.0));

    // Every pair is split very slightly, so struck bowls beat.
    const auto b = basisFor (part (PartType::Shell));
    const auto& sb = static_cast<const ShellBasis&> (*b);
    REQUIRE (sb.mode (0).m == 2);
    REQUIRE (sb.mode (1).isSine);
    CHECK (sb.omega (1) / sb.omega (0) == Approx (1.0024).epsilon (1e-6));
}

TEST_CASE ("A box with stiff walls rings at its Helmholtz frequency", "[parts][box]")
{
    BoxBasis::Params bp;
    bp.face.width = 0.3;
    bp.face.height = 0.48;
    bp.face.thickness = 0.06; // near-rigid face
    bp.face.youngs = 12e9;
    bp.face.density = 750;
    bp.face.maxModes = 24;
    bp.depth = 0.3;
    bp.portRadius = 0.04;
    BoxBasis b (bp);
    // f_H = c / 2 pi sqrt(S / (V L_eff)), L_eff = t + 1.7 r.
    const double S = kPi * 0.04 * 0.04, V = 0.3 * 0.48 * 0.3, L = 0.006 + 1.7 * 0.04;
    const double fH = 343.0 / (2.0 * kPi) * std::sqrt (S / (V * L));
    CHECK (b.helmholtzHz() == Approx (fH).epsilon (1e-9));
    CHECK (b.omega (0) / (2.0 * kPi) == Approx (fH).epsilon (0.02));
    CHECK (b.portParticipation (0) > 0.95);

    // A real (4 mm) face couples to it: the air mode shares energy with the face.
    bp.face.thickness = 0.004;
    BoxBasis soft (bp);
    double maxShare = 0.0;
    int sharing = 0;
    for (int k = 0; k < soft.size(); ++k)
    {
        maxShare = std::max (maxShare, soft.portParticipation (k));
        if (soft.portParticipation (k) > 0.05) ++sharing;
    }
    CHECK (maxShare < 0.95);
    CHECK (sharing >= 2);
}

TEST_CASE ("The spring disperses: harmonic when Chirp is 0, stretching when it's up", "[parts][spring]")
{
    auto p = part (PartType::Spring);
    p.shape[1] = 0.0;
    const auto harmonic = basisFor (p);
    for (int k = 1; k < 10; ++k)
        CHECK (harmonic->omega (k) / harmonic->omega (0) == Approx (k + 1.0).epsilon (1e-6));

    p.shape[1] = 1.0;
    const auto chirpy = basisFor (p);
    double prevGap = 0.0;
    for (int k = 1; k < 60; ++k)
    {
        const double gap = chirpy->omega (k) - chirpy->omega (k - 1);
        CHECK (gap > prevGap); // spacing keeps growing: highs arrive first, then the "pew"
        prevGap = gap;
    }
    CHECK (chirpy->omega (59) / chirpy->omega (0) > 3.0 * 60.0);
}

TEST_CASE ("An open tube's air column sits at c / 2(L + end corrections)", "[parts][tube]")
{
    auto p = part (PartType::Tube);
    p.shape[2] = 1.0; // plenty of air
    const auto b = basisFor (p);
    const auto& sb = static_cast<const StripBasis&> (*b);
    std::vector<double> air;
    for (int k = 0; k < sb.size(); ++k)
        if (sb.mode (k).lossWeight == 0.0) air.push_back (sb.omega (k) / (2.0 * kPi));
    REQUIRE (air.size() >= 3);
    const double L = p.control (0), ro = 0.5 * p.control (1);
    const double ri = ro - std::max (0.0008, 0.06 * p.control (1));
    const double f1 = 343.0 / (2.0 * (L + 1.2 * ri));
    CHECK (air[0] == Approx (f1).epsilon (1e-9));
    CHECK (air[1] == Approx (2.0 * f1).epsilon (1e-9));

    // Capped at one end: odd harmonics of c / 4(L + 0.6 r).
    p.mount = 1;
    const auto c = basisFor (p);
    const auto& cb = static_cast<const StripBasis&> (*c);
    std::vector<double> cair;
    for (int k = 0; k < cb.size(); ++k)
        if (cb.mode (k).lossWeight == 0.0) cair.push_back (cb.omega (k) / (2.0 * kPi));
    CHECK (cair[1] / cair[0] == Approx (3.0).epsilon (1e-9));
}

TEST_CASE ("Size/Tune scales every part's modes by the same ratio", "[parts][tune]")
{
    for (int t = 0; t < kNumPartTypes; ++t)
    {
        auto spec = test::defaultSpec();
        spec.part = part ((PartType) t);
        auto a = compileInstrument (spec, 96000.0);
        spec.tuneSemitones = 5.0;
        auto b = compileInstrument (spec, 96000.0);
        const double ratio = std::pow (2.0, 5.0 / 12.0);
        INFO (partInfo ((PartType) t).name);
        for (int k = 0; k < std::min (8, a->nExact); ++k)
            CHECK (b->omega[(size_t) k] / a->omega[(size_t) k] == Approx (ratio).epsilon (0.01));
    }
}

TEST_CASE ("Every part, material and mount compiles to finite, decaying modes", "[parts][materials]")
{
    for (int t = 0; t < kNumPartTypes; ++t)
        for (int m = 0; m < (int) kNumMaterials; ++m)
        {
            auto spec = test::defaultSpec();
            spec.part = part ((PartType) t);
            spec.part.material = (MaterialId) m;
            if (! materialAllowed (spec.part.type, spec.part.material))
                continue;
            for (int mount = 0; mount < partInfo (spec.part.type).mountCount; ++mount)
            {
                spec.part.mount = mount;
                auto ci = compileInstrument (spec, 48000.0);
                INFO (partInfo ((PartType) t).name << " / " << getMaterial ((MaterialId) m).name << " mount " << mount);
                REQUIRE (ci->n > 0);
                for (int k = 0; k < ci->n; ++k)
                {
                    REQUIRE (std::isfinite (ci->omega[(size_t) k]));
                    REQUIRE (ci->sigma[(size_t) k] > 0.0);
                }
                CHECK (std::isfinite (ci->contactGain));
                CHECK (std::isfinite (ci->airGain));
            }
        }
}

TEST_CASE ("Treatments reshape every kind of part", "[parts][treatments]")
{
    for (int t = 0; t < kNumPartTypes; ++t)
    {
        auto spec = test::defaultSpec();
        spec.part = part ((PartType) t);
        auto plain = compileInstrument (spec, 48000.0);
        spec.treatments.age.amount = 0.7;
        spec.treatments.deform.intensity = 0.6;
        spec.treatments.twist.amount = 0.5;
        spec.treatments.puncture.count = 0.2;
        auto treated = compileInstrument (spec, 48000.0);
        INFO (partInfo ((PartType) t).name);
        REQUIRE_FALSE (treated->X.empty());
        CHECK_FALSE (treated->features.empty());
        // Features land on the part.
        for (const auto& f : treated->features)
            CHECK (treated->basis->contains (f.p));
        double moved = 0.0;
        for (int k = 0; k < std::min (plain->nExact, treated->nExact); ++k)
            moved = std::max (moved, std::abs (std::log (treated->omega[(size_t) k] / plain->omega[(size_t) k])));
        CHECK (moved > 0.002);
    }
}
