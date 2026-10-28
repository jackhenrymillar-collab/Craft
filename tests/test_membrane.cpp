#include "compile/Bessel.h"
#include "compile/MembraneSolver.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using Catch::Approx;
using namespace foundry;

namespace
{
MembraneBasis makeBasis (int modes = 40)
{
    MembraneBasis::Params p;
    p.radius = 0.178;
    p.tension = 1100.0;
    p.arealDensity = 0.26;
    p.maxModes = modes;
    return MembraneBasis (p);
}
}

TEST_CASE ("Membrane mode frequencies follow Bessel-zero ratios", "[membrane]")
{
    const auto b = makeBasis();
    const double c = std::sqrt (1100.0 / 0.26);
    const double f01 = 2.404825557695773 * c / (2.0 * std::numbers::pi * 0.178);
    CHECK (b.omega (0) / (2.0 * std::numbers::pi) == Approx (f01).epsilon (1e-12));

    // The classic inharmonic series of an ideal membrane.
    const double ratios[] = { 1.0, 1.5933, 1.5933, 2.1355, 2.1355, 2.2954, 2.6531, 2.6531 };
    for (int k = 0; k < 8; ++k)
    {
        INFO ("mode " << k << " m=" << b.mode (k).m << " n=" << b.mode (k).n);
        CHECK (b.omega (k) / b.omega (0) == Approx (ratios[k]).epsilon (2e-4));
    }
    // Ascending and degenerate pairs adjacent.
    for (int k = 1; k < b.size(); ++k)
        CHECK (b.omega (k) >= b.omega (k - 1));
}

TEST_CASE ("Membrane modes are mass-orthonormal and stiffness-diagonal", "[membrane]")
{
    const auto b = makeBasis (24);
    const int n = b.size();
    std::vector<QuadPoint> quad;
    b.quadrature (quad, 160);

    std::vector<double> M ((size_t) (n * n), 0.0), K ((size_t) (n * n), 0.0);
    std::vector<double> phi ((size_t) n), gx ((size_t) n), gy ((size_t) n);
    for (const auto& q : quad)
    {
        b.shapesAndGradients (q.p, phi.data(), gx.data(), gy.data());
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                M[(size_t) (i * n + j)] += q.weight * b.massPerArea() * phi[(size_t) i] * phi[(size_t) j];
        b.addStiffness (phi.data(), gx.data(), gy.data(), q.weight, 1.0, {}, K.data());
    }
    for (int i = 0; i < n; ++i)
        for (int j = i; j < n; ++j)
        {
            INFO ("i=" << i << " j=" << j);
            CHECK (M[(size_t) (i * n + j)] == Approx (i == j ? 1.0 : 0.0).margin (5e-3));
            const double w2 = b.omega (i) * b.omega (i);
            CHECK (K[(size_t) (i * n + j)] / (b.omega (0) * b.omega (0)) == Approx (i == j ? w2 / (b.omega (0) * b.omega (0)) : 0.0).margin (2e-2 * (1 + i)));
        }
}

TEST_CASE ("Only axisymmetric modes move at the centre", "[membrane]")
{
    const auto b = makeBasis();
    std::vector<double> phi ((size_t) b.size());
    b.shapes ({ 0.0, 0.0 }, phi.data());
    for (int k = 0; k < b.size(); ++k)
    {
        if (b.mode (k).m == 0)
            CHECK (std::abs (phi[(size_t) k]) > 0.1);
        else
            CHECK (phi[(size_t) k] == Approx (0.0).margin (1e-12));
    }
    // And nothing moves at the rim.
    b.shapes ({ 0.178 * 0.99999, 0.0 }, phi.data());
    for (int k = 0; k < b.size(); ++k)
        CHECK (std::abs (phi[(size_t) k]) < 1e-2);
}
