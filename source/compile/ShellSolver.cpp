#include "ShellSolver.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace foundry
{
namespace
{
constexpr double kPi = std::numbers::pi;
constexpr int kMaxM = 24, kMaxN = 3;

double ring (int m) noexcept
{
    const double mm = m;
    return mm * (mm * mm - 1.0) / std::sqrt (mm * mm + 1.0);
}

// Church-bell partials (m = 2..9, n = 0..3) relative to the hum (2, 0). The well-known
// ones: prime (2,1) 2.0, tierce (3,1) 2.38, quint (3,2) 3.0, nominal (4,1) 4.0,
// superquint (4,2) 5.0, octave nominal (6,1) 8.0. The rest are smoothed guesses.
constexpr double kBell[8][4] = {
    { 1.0,  2.0,  3.5,  5.6 },  // m = 2
    { 2.2,  2.38, 3.0,  5.2 },  // m = 3
    { 3.3,  4.0,  5.0,  8.4 },  // m = 4
    { 5.0,  5.34, 6.0,  9.6 },  // m = 5
    { 7.2,  8.0,  9.0,  12.0 }, // m = 6
    { 9.8,  10.7, 12.0, 15.0 }, // m = 7
    { 12.8, 13.3, 15.0, 18.5 }, // m = 8
    { 16.0, 16.6, 18.6, 22.5 }, // m = 9
};
} // namespace

double ShellBasis::ratio (int m, int n, double profile) noexcept
{
    m = std::max (2, m);
    n = std::clamp (n, 0, kMaxN);
    const double bowl = ring (m) / ring (2) * (1.0 + 1.1 * n * n / (m - 0.5));
    const double bell = m <= 9 ? kBell[m - 2][n] : kBell[7][n] * ring (m) / ring (9);
    const double p = std::clamp (profile, 0.0, 1.0);
    return std::exp ((1.0 - p) * std::log (bowl) + p * std::log (bell));
}

double ShellBasis::massPerArea() const noexcept
{
    // Seen from above, the wall area is larger than the disc: about 1.5x for a bowl, 3x for a bell.
    return params.density * params.wall * (1.5 + 1.5 * params.profile);
}

ShellBasis::ShellBasis (const Params& p) : params (p)
{
    const double a = p.radius;
    // f(2,0): ring-bending scale h / a^2 * sqrt(E / rho), calibrated so a 20 cm bronze bowl with a
    // 3 mm wall sits near 220 Hz; bells (thicker, taller) a little higher for the same size.
    const double f0 = 0.218 * (p.wall / (a * a)) * std::sqrt (p.youngs / p.density) * (1.0 + 0.5 * p.profile);
    const double w0 = 2.0 * kPi * f0;

    // Radial profiles rho^2 cos(n pi (1 - rho)), made orthonormal (weight rho d rho) by
    // Gram-Schmidt so modes of the same m stay mass-orthogonal.
    radial.resize (kMaxN + 1);
    auto weight = [] (int i) { return ((i == 0 || i == kTable) ? 1.0 : (i % 2 ? 4.0 : 2.0)) * i / (3.0 * kTable * kTable); };
    auto dot = [&] (const std::vector<double>& x, const std::vector<double>& y) {
        double s = 0.0;
        for (int i = 0; i <= kTable; ++i) s += weight (i) * x[(size_t) i] * y[(size_t) i];
        return s;
    };
    for (int n = 0; n <= kMaxN; ++n)
    {
        auto& t = radial[(size_t) n];
        t.resize (kTable + 1);
        for (int i = 0; i <= kTable; ++i)
        {
            const double rho = (double) i / kTable;
            t[(size_t) i] = rho * rho * std::cos (n * kPi * (1.0 - rho));
        }
        for (int j = 0; j < n; ++j)
        {
            const double c = dot (t, radial[(size_t) j]);
            for (int i = 0; i <= kTable; ++i) t[(size_t) i] -= c * radial[(size_t) j][(size_t) i];
        }
        const double g = 1.0 / std::sqrt (dot (t, t));
        for (auto& v : t) v *= g;
        // Keep the rim moving "up" for every profile.
        if (t.back() < 0.0)
            for (auto& v : t) v = -v;
    }

    struct Cand { int m, n; double w; };
    std::vector<Cand> cands;
    for (int m = 2; m <= kMaxM; ++m)
        for (int n = 0; n <= kMaxN; ++n)
        {
            const double w = w0 * ratio (m, n, p.profile);
            if (w < p.maxOmega) cands.push_back ({ m, n, w });
        }
    std::sort (cands.begin(), cands.end(), [] (const Cand& x, const Cand& y) { return x.w < y.w; });

    const int cap = std::clamp (p.maxModes, 2, kMaxExactModes);
    const double sigma = massPerArea();
    for (const auto& c : cands)
    {
        if ((int) modes.size() + 2 > cap) break;
        // Modal mass: sigma * a^2 * pi (angular cos^2 / sin^2) * integral R^2 rho d rho (= 1).
        const double norm = 1.0 / std::sqrt (sigma * a * a * kPi);
        const double split = 1.0 + 0.0012 * c.m; // casting asymmetry: the sin twin a touch higher
        modes.push_back ({ c.m, c.n, false, c.w, norm, c.n });
        modes.push_back ({ c.m, c.n, true, c.w * split, norm, c.n });
    }
}

double ShellBasis::radialValue (int table, double rho) const noexcept
{
    const auto& t = radial[(size_t) table];
    const double x = std::clamp (rho, 0.0, 1.0) * kTable;
    const int i = std::min ((int) x, kTable - 1);
    const double u = x - i;
    return t[(size_t) i] + u * (t[(size_t) i + 1] - t[(size_t) i]);
}

void ShellBasis::shapes (Vec2 p, double* phi) const noexcept
{
    const int n = size();
    const double a = params.radius;
    const double r = std::sqrt (p.x * p.x + p.y * p.y);
    if (r >= a)
    {
        std::fill (phi, phi + n, 0.0);
        return;
    }
    const double theta = std::atan2 (p.y, p.x);
    const double rho = r / a;
    for (int k = 0; k < n; ++k)
    {
        const auto& md = modes[(size_t) k];
        const double ang = md.isSine ? std::sin (md.m * theta) : std::cos (md.m * theta);
        phi[k] = md.norm * radialValue (md.table, rho) * ang;
    }
}

double ShellBasis::modeCountBelow (double w) const noexcept
{
    const double D = params.youngs * params.wall * params.wall * params.wall / (12.0 * 0.91);
    const double c2 = std::sqrt (D / (params.density * params.wall));
    const double S = kPi * params.radius * params.radius * (1.5 + 1.5 * params.profile);
    return S * w / (4.0 * kPi * c2);
}

double ShellBasis::wavenumber (double w) const noexcept
{
    const double D = params.youngs * params.wall * params.wall * params.wall / (12.0 * 0.91);
    const double c2 = std::sqrt (D / (params.density * params.wall));
    return std::sqrt (std::max (0.0, w) / c2);
}

double ShellBasis::extraDecay (int k) const noexcept
{
    if (! params.resting)
        return 0.0;
    // A cushion under the base touches the pole region: it drains the low, broad modes most.
    const auto& md = modes[(size_t) k];
    return 0.6 / ((md.m - 1.0) * (1.0 + md.n));
}

} // namespace foundry
