#pragma once

namespace foundry::bessel
{
// Bessel function of the first kind J_m(x), integer order m >= 0, x >= 0.
// Uses the periodic integral form J_m(x) = (1/2pi) * integral over 0..2pi of
// cos(m t - x sin t) dt, evaluated with the trapezoid rule (exponentially accurate
// for periodic integrands). Portable: libc++ has no std::cyl_bessel_j.
double J (int m, double x) noexcept;

// J'_m(x) = (J_{m-1}(x) - J_{m+1}(x)) / 2, with J'_0 = -J_1.
double dJ (int m, double x) noexcept;

// J_m(x) and J'_m(x) together, sharing one integral pass (faster for tables).
void JandDerivative (int m, double x, double& j, double& dj) noexcept;

// n-th positive zero (n >= 1) of J_m.
double zero (int m, int n) noexcept;
}
