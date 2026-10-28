#include "compile/Bessel.h"
#include "compile/MembraneSolver.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using namespace foundry;

TEST_CASE ("Bessel J matches reference values", "[bessel]")
{
    // Reference values from Abramowitz & Stegun / scipy.special.jv.
    CHECK (bessel::J (0, 1.0) == Approx (0.7651976865579666).margin (1e-13));
    CHECK (bessel::J (1, 2.5) == Approx (0.4970941024642741).margin (1e-13));
    CHECK (bessel::J (5, 10.0) == Approx (-0.2340615281867936).margin (1e-13));
    CHECK (bessel::J (2, 0.0) == Approx (0.0).margin (1e-15));
    CHECK (bessel::J (0, 0.0) == Approx (1.0).margin (1e-15));
    CHECK (bessel::dJ (0, 3.0) == Approx (-bessel::J (1, 3.0)).margin (1e-15));
}

TEST_CASE ("Bessel zeros match published tables", "[bessel]")
{
    struct Z { int m, n; double z; };
    const Z table[] = {
        { 0, 1, 2.404825557695773 }, { 0, 2, 5.520078110286311 }, { 0, 3, 8.653727912911012 },
        { 1, 1, 3.831705970207512 }, { 1, 2, 7.015586669815619 },
        { 2, 1, 5.135622301840683 }, { 3, 1, 6.380161895923984 },
        { 4, 1, 7.588342434503805 }, { 10, 1, 14.47550068655454 },
    };
    for (const auto& z : table)
    {
        INFO ("m=" << z.m << " n=" << z.n);
        CHECK (besselZeroCached (z.m, z.n) == Approx (z.z).margin (1e-10));
        CHECK (bessel::zero (z.m, z.n) == Approx (z.z).margin (1e-10));
    }
}

TEST_CASE ("Combined J and J' agree with the separate forms", "[bessel]")
{
    for (int m : { 0, 1, 2, 7, 20 })
        for (double x : { 0.0, 0.3, 2.0, 11.5, 40.0 })
        {
            double j, dj;
            bessel::JandDerivative (m, x, j, dj);
            CHECK (j == Approx (bessel::J (m, x)).margin (1e-13));
            CHECK (dj == Approx (bessel::dJ (m, x)).margin (1e-13));
        }
}
