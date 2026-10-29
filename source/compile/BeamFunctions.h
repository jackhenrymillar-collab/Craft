#pragma once

#include <cmath>
#include <memory>
#include <vector>

namespace foundry::beam
{

// Boundary conditions of a 1-D beam (or string) on xi in [0, 1].
enum class Ends
{
    FreeFree,      // xylophone bar on cords, free plate edges
    ClampedFree,   // kalimba tine, ruler off a desk (clamped at xi = 0)
    PinnedPinned,  // simply supported: sin(n pi xi); also a fixed-fixed string
    FixedFree,     // a string or spring fixed at xi = 0, free at 1: sin((n - 1/2) pi xi)
};

// beta_n * L of the n-th elastic bending mode (n >= 1). For PinnedPinned / FixedFree this
// is the string wavenumber k_n * L.
double betaL (Ends ends, int n) noexcept;

// A mode function sampled on a uniform grid, normalised so the integral of X^2 over
// [0, 1] is 1. Derivatives are with respect to xi.
struct Table
{
    static constexpr int kSize = 1024;
    std::vector<double> X, dX, ddX; // kSize + 1 samples each

    void eval (double xi, double& x, double& dx) const noexcept;
    [[nodiscard]] double value (double xi) const noexcept;
};

// Cached mode functions (thread-safe). For FreeFree, index 0 is the rigid translation and
// index 1 the rigid rotation; elastic mode n is index n + 1. For the other ends, index n - 1
// is elastic mode n.
std::shared_ptr<const Table> table (Ends ends, int index);
// Number of rigid-body functions at the start of the FreeFree series.
constexpr int kRigidFreeFree = 2;

// Builds a table from a function returning (X, X', X'') at xi, then normalises it.
template <typename F>
Table makeTable (F f)
{
    Table t;
    t.X.resize (Table::kSize + 1);
    t.dX.resize (Table::kSize + 1);
    t.ddX.resize (Table::kSize + 1);
    for (int i = 0; i <= Table::kSize; ++i)
    {
        const double xi = (double) i / Table::kSize;
        f (xi, t.X[(size_t) i], t.dX[(size_t) i], t.ddX[(size_t) i]);
    }
    // Simpson's rule for the norm.
    double s = 0.0;
    for (int i = 0; i <= Table::kSize; ++i)
    {
        const double w = (i == 0 || i == Table::kSize) ? 1.0 : (i % 2 ? 4.0 : 2.0);
        s += w * t.X[(size_t) i] * t.X[(size_t) i];
    }
    s /= 3.0 * Table::kSize;
    const double g = s > 0.0 ? 1.0 / std::sqrt (s) : 0.0;
    for (int i = 0; i <= Table::kSize; ++i)
    {
        t.X[(size_t) i] *= g;
        t.dX[(size_t) i] *= g;
        t.ddX[(size_t) i] *= g;
    }
    return t;
}

// Integral over [0, 1] of a(xi) * b(xi) * w(xi) by Simpson's rule on the table grid.
double integrate (const std::vector<double>& a, const std::vector<double>& b, const std::vector<double>* w = nullptr) noexcept;

} // namespace foundry::beam
