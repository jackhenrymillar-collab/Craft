#include "StripSolver.h"

#include "LinearAlgebra.h"
#include "TreatmentSolver.h"
#include "model/Instrument.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace foundry
{
namespace
{
constexpr double kPi = std::numbers::pi;
constexpr double kAirDensity = 1.2;   // kg/m^3
constexpr double kSoundSpeed = 343.0; // m/s
}

StripBasis::StripBasis (PartType t, double len, double w, double m)
    : partType (t), length (len), width (w), mass (m), norm (1.0 / std::sqrt (m * len * w))
{
}

int StripBasis::addTable (std::shared_ptr<const beam::Table> t)
{
    tables.push_back (std::move (t));
    return (int) tables.size() - 1;
}

void StripBasis::sortAndTrim (int maxModes, double maxOmega)
{
    std::stable_sort (modes.begin(), modes.end(), [] (const Mode& a, const Mode& b) { return a.omega < b.omega; });
    std::vector<Mode> kept;
    for (const auto& m : modes)
        if (m.omega > 0.0 && m.omega < maxOmega && (int) kept.size() < std::clamp (maxModes, 1, kMaxExactModes))
            kept.push_back (m);
    if (kept.empty() && ! modes.empty())
        kept.push_back (modes.front()); // always keep the fundamental
    modes = std::move (kept);
}

double StripBasis::massPerAreaAt (Vec2 p) const noexcept
{
    if (massProfile.empty())
        return mass;
    const double xi = std::clamp (p.x / length + 0.5, 0.0, 1.0);
    const int i = std::min ((int) std::lround (xi * (double) (massProfile.size() - 1)), (int) massProfile.size() - 1);
    return mass * massProfile[(size_t) i];
}

void StripBasis::shapes (Vec2 p, double* phi) const noexcept
{
    const int n = size();
    if (! contains (p))
    {
        std::fill (phi, phi + n, 0.0);
        return;
    }
    const double xi = p.x / length + 0.5;
    const double cross = std::sqrt (3.0) * 2.0 * p.y / width;
    for (int k = 0; k < n; ++k)
    {
        const auto& m = modes[(size_t) k];
        phi[k] = norm * m.coupling * tables[(size_t) m.table]->value (xi) * (m.torsion ? cross : 1.0);
    }
}

void StripBasis::shapesAndGradients (Vec2 p, double* phi, double* gx, double* gy) const noexcept
{
    const int n = size();
    if (! contains (p))
    {
        std::fill (phi, phi + n, 0.0);
        std::fill (gx, gx + n, 0.0);
        std::fill (gy, gy + n, 0.0);
        return;
    }
    const double xi = p.x / length + 0.5;
    const double cross = std::sqrt (3.0) * 2.0 * p.y / width;
    const double dCross = std::sqrt (3.0) * 2.0 / width;
    for (int k = 0; k < n; ++k)
    {
        const auto& m = modes[(size_t) k];
        double x, dx;
        tables[(size_t) m.table]->eval (xi, x, dx);
        const double s = norm * m.coupling;
        if (m.torsion)
        {
            phi[k] = s * x * cross;
            gx[k] = s * dx / length * cross;
            gy[k] = s * x * dCross;
        }
        else
        {
            phi[k] = s * x;
            gx[k] = s * dx / length;
            gy[k] = 0.0;
        }
    }
}

double StripBasis::modeCountBelow (double w) const noexcept
{
    // 1-D: one mode per half wavelength along the length.
    return length * wavenumber (w) / kPi;
}

void StripBasis::radiation (std::vector<double>& volume, std::vector<double>& dx, std::vector<double>& dy) const
{
    ModalBasis::radiation (volume, dx, dy);
    for (int k = 0; k < size(); ++k)
    {
        volume[(size_t) k] += modes[(size_t) k].airVolume;
        dx[(size_t) k] += modes[(size_t) k].airDipole;
    }
}

double barThicknessProfile (double xi, double undercut) noexcept
{
    // A marimba-style arch cut under the middle 70 % of the bar, up to 75 % of the thickness.
    const double u = std::clamp (undercut, 0.0, 1.0);
    const double d = std::abs (xi - 0.5) / 0.35;
    if (d >= 1.0 || u <= 0.0)
        return 1.0;
    const double bump = 0.5 * (1.0 + std::cos (kPi * d));
    return 1.0 - 0.75 * u * bump;
}

// ---- Bar ---------------------------------------------------------------------------------

std::shared_ptr<StripBasis> makeBarBasis (const PartSpec& spec, const Material& mat, const AgeGlobals& age,
                                          double ratio, int maxModes, double maxOmega)
{
    const double L = spec.control (0) / ratio;
    const double t = spec.control (1) / ratio;
    const double undercut = spec.control (2);
    const double w = std::clamp (0.14 * spec.control (0), 0.02, 0.08) / ratio;
    const double E = mat.youngsGPa * 1e9 * age.stiffnessScale;
    const double rho = mat.density * age.massScale;
    const auto ends = spec.mount == 1 ? beam::Ends::ClampedFree : beam::Ends::FreeFree;

    auto b = std::make_shared<StripBasis> (PartType::Bar, L, w, rho * t);
    const double kappa = std::sqrt (E * t * t / (12.0 * rho)); // sqrt(EI / rho A)
    b->kOf = [kappa] (double om) { return std::sqrt (std::max (0.0, om) / kappa); };
    b->dentK = 0.6;   // a dent in a solid part stiffens it locally (shallow-shell effect)
    b->dentL = 0.02;

    // Bending: Rayleigh-Ritz over the uniform beam's functions with the undercut thickness
    // profile tau(xi): stiffness ~ tau^3, mass ~ tau. With no undercut this is exactly the
    // Euler-Bernoulli series.
    const int rigid = ends == beam::Ends::FreeFree ? beam::kRigidFreeFree : 0;
    constexpr int nb = 36;
    std::vector<std::shared_ptr<const beam::Table>> base;
    for (int i = 0; i < nb + rigid; ++i)
        base.push_back (beam::table (ends, i));
    const int nf = (int) base.size();

    const int G = beam::Table::kSize;
    std::vector<double> tau ((size_t) G + 1), tau3 ((size_t) G + 1);
    for (int i = 0; i <= G; ++i)
    {
        tau[(size_t) i] = barThicknessProfile ((double) i / G, undercut);
        tau3[(size_t) i] = tau[(size_t) i] * tau[(size_t) i] * tau[(size_t) i];
    }
    std::vector<double> K ((size_t) (nf * nf)), M ((size_t) (nf * nf));
    for (int i = 0; i < nf; ++i)
        for (int j = i; j < nf; ++j)
        {
            K[(size_t) (i * nf + j)] = K[(size_t) (j * nf + i)] = beam::integrate (base[(size_t) i]->ddX, base[(size_t) j]->ddX, &tau3);
            M[(size_t) (i * nf + j)] = M[(size_t) (j * nf + i)] = beam::integrate (base[(size_t) i]->X, base[(size_t) j]->X, &tau);
        }
    std::vector<double> lambda, X;
    if (! linalg::generalisedEigen (K, M, nf, lambda, X))
        return b;

    const double scale = kappa / (L * L);   // omega = sqrt(lambda) * kappa / L^2
    const int keep = (int) (0.7 * nb);      // Ritz is accurate for the lower part of the basis
    int kept = 0;
    for (int k = 0; k < nf && kept < keep; ++k)
    {
        const double om = std::sqrt (std::max (0.0, lambda[(size_t) k])) * scale;
        if (om < 2.0 * kPi * 2.0)
            continue; // rigid-body motion: the bar swinging on its cords
        beam::Table tb;
        tb.X.assign ((size_t) G + 1, 0.0);
        tb.dX.assign ((size_t) G + 1, 0.0);
        tb.ddX.assign ((size_t) G + 1, 0.0);
        for (int i = 0; i < nf; ++i)
        {
            const double c = X[(size_t) (i * nf + k)];
            if (c == 0.0) continue;
            for (int g = 0; g <= G; ++g)
            {
                tb.X[(size_t) g] += c * base[(size_t) i]->X[(size_t) g];
                tb.dX[(size_t) g] += c * base[(size_t) i]->dX[(size_t) g];
                tb.ddX[(size_t) g] += c * base[(size_t) i]->ddX[(size_t) g];
            }
        }
        StripBasis::Mode m;
        m.omega = om;
        m.table = b->addTable (std::make_shared<const beam::Table> (std::move (tb)));
        b->addMode (m);
        ++kept;
    }
    if (undercut > 0.0)
    {
        b->massProfile.resize (65);
        for (int i = 0; i <= 64; ++i)
            b->massProfile[(size_t) i] = barThicknessProfile (i / 64.0, undercut);
    }

    // Torsion: twist angle cos(n pi xi) (free ends) or sin((n - 1/2) pi xi) (clamped end).
    // omega_n = k_n sqrt(G J / (rho I_p)), thin-rectangle J = beta w t^3. The undercut thins
    // the middle, which lowers torsion roughly with the mean thickness.
    {
        const double Gm = E / 2.6;
        const double beta = (1.0 / 3.0) * (1.0 - 0.63 * std::min (1.0, t / w));
        double meanTau = 0.0;
        for (int i = 0; i <= G; ++i) meanTau += tau[(size_t) i];
        meanTau /= G + 1;
        const double tt = t * meanTau;
        const double c = std::sqrt (12.0 * Gm * beta * tt * tt / (rho * (w * w + tt * tt)));
        for (int n = 1; n <= 16; ++n)
        {
            const bool freeEnds = ends == beam::Ends::FreeFree;
            const double kL = freeEnds ? n * kPi : (n - 0.5) * kPi;
            const double om = c * kL / L;
            if (om > maxOmega) break;
            std::shared_ptr<const beam::Table> tb;
            if (freeEnds)
                tb = std::make_shared<const beam::Table> (beam::makeTable ([kL] (double xi, double& tx, double& tdx, double& tddx) {
                    tx = std::cos (kL * xi);
                    tdx = -kL * std::sin (kL * xi);
                    tddx = -kL * kL * tx;
                }));
            else
                tb = beam::table (beam::Ends::FixedFree, n - 1);
            StripBasis::Mode m;
            m.omega = om;
            m.torsion = true;
            m.table = b->addTable (tb);
            b->addMode (m);
        }
    }
    b->sortAndTrim (maxModes, maxOmega);
    return b;
}

// ---- Tube --------------------------------------------------------------------------------

std::shared_ptr<StripBasis> makeTubeBasis (const PartSpec& spec, const Material& mat, const AgeGlobals& age,
                                           double ratio, int maxModes, double maxOmega)
{
    const double L = spec.control (0) / ratio;
    const double D = spec.control (1) / ratio;
    const double air = spec.control (2);
    const bool capped = spec.mount == 1;
    const double ro = 0.5 * D;
    const double wall = std::max (0.0008 / ratio, 0.06 * D);
    const double ri = std::max (0.2 * ro, ro - wall);
    const double E = mat.youngsGPa * 1e9 * age.stiffnessScale;
    const double rho = mat.density * age.massScale;
    const double crossArea = kPi * (ro * ro - ri * ri);

    auto b = std::make_shared<StripBasis> (PartType::Tube, L, D, rho * crossArea / D);
    const double kappa = std::sqrt (E * (ro * ro + ri * ri) / (4.0 * rho)); // sqrt(EI / rho A), hollow circle
    b->kOf = [kappa] (double om) { return std::sqrt (std::max (0.0, om) / kappa); };
    b->dentK = 0.4;
    b->dentL = 0.03;

    // Wall bending: free-free hollow beam (tubular bells and chimes hang free).
    for (int n = 1; n <= 40; ++n)
    {
        const double bl = beam::betaL (beam::Ends::FreeFree, n);
        const double om = kappa * bl * bl / (L * L);
        if (om > maxOmega) break;
        StripBasis::Mode m;
        m.omega = om;
        m.table = b->addTable (beam::table (beam::Ends::FreeFree, n + 1));
        b->addMode (m);
    }

    // Air column: open at both ends, or capped at one (xi = 0). Coordinates are normalised to
    // the air plug mass, so the open end moves 1/sqrt(m) per unit; striking the wall drives the
    // column weakly (Air sets how much), and the open ends radiate strongly.
    if (air > 0.001)
    {
        const double S = kPi * ri * ri;
        const double mAir = kAirDensity * S * L * 0.5;
        const double endMotion = 1.0 / std::sqrt (mAir);
        const double wallNorm = 1.0 / std::sqrt (rho * crossArea * L);
        const double partNorm = 1.0 / std::sqrt (b->massPerArea() * L * D);
        for (int n = 1; n <= 24; ++n)
        {
            const double kL = capped ? (n - 0.5) * kPi : n * kPi;
            const double endCorr = capped ? 0.6 * ri : 1.2 * ri;
            const double om = kSoundSpeed * kL / (L + endCorr);
            if (om > maxOmega) break;
            auto tb = std::make_shared<const beam::Table> (beam::makeTable ([kL, capped] (double xi, double& X, double& dX, double& ddX) {
                // Air displacement: zero at a capped end, maximal at open ends.
                X = capped ? std::sin (kL * xi) : std::cos (kL * xi);
                dX = capped ? kL * std::cos (kL * xi) : -kL * std::sin (kL * xi);
                ddX = -kL * kL * X;
            }));
            StripBasis::Mode m;
            m.omega = om;
            m.table = b->addTable (tb);
            m.coupling = 0.6 * air * wallNorm / partNorm;
            m.lossWeight = 0.0;
            m.extraSigma = om / (2.0 * 35.0); // air-column Q ~ 35 (wall friction + radiation)
            // Open ends: volume flow S * end velocity. Both ends of an open tube move together
            // for odd n (a net monopole), oppositely for even n (a dipole across the length).
            const double vol = air * S * endMotion;
            if (capped)
                m.airVolume = vol;
            else if (n % 2 == 1)
                m.airVolume = 2.0 * vol;
            else
                m.airDipole = vol * L;
            b->addMode (m);
        }
    }
    b->sortAndTrim (maxModes, maxOmega);
    return b;
}

// ---- Spring ------------------------------------------------------------------------------

std::shared_ptr<StripBasis> makeSpringBasis (const PartSpec& spec, const Material& mat, const AgeGlobals& age,
                                             double ratio, int maxModes, double maxOmega)
{
    const double L = spec.control (0) / ratio;
    const double chirp = spec.control (1);
    const double Dc = spec.control (2) / ratio;
    const bool hanging = spec.mount == 1;
    const double E = mat.youngsGPa * 1e9 * age.stiffnessScale;
    const double rho = mat.density * age.massScale;
    const double d = 0.08 * Dc;                              // wire diameter
    const double mu = rho * kPi * kPi * d * Dc / 6.4;        // kg/m along the spring (pitch 1.6 d)

    auto b = std::make_shared<StripBasis> (PartType::Spring, L, Dc, mu / Dc);
    // Low-k wave speed of a stretched spring, scaled from a steel reference (12.6 m/s for a
    // 14 mm coil), and a dispersion length that bends the series toward k^2 (chirp).
    const double steel = std::sqrt (200e9 / 7850.0);
    const double c = 12.6 * std::sqrt (E / rho) / steel * std::sqrt (0.014 / spec.control (2));
    const double ell = 0.33 * Dc;
    const double chi = 4.0 * chirp * chirp;
    auto omegaOf = [c, ell, chi] (double k) { return c * k * std::sqrt (1.0 + chi * k * k * ell * ell); };
    b->kOf = [omegaOf] (double om) {
        // Invert omega(k) by bisection.
        double lo = 0.0, hi = 1.0;
        while (omegaOf (hi) < om && hi < 1e7) hi *= 2.0;
        for (int i = 0; i < 60; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (omegaOf (mid) < om ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi);
    };
    b->dentK = 0.3;
    b->dentL = 0.05;

    const auto ends = hanging ? beam::Ends::FixedFree : beam::Ends::PinnedPinned;
    for (int n = 1; n <= kMaxExactModes; ++n)
    {
        const double k = beam::betaL (ends, n) / L;
        const double om = omegaOf (k);
        if (om > maxOmega) break;
        StripBasis::Mode m;
        m.omega = om;
        m.table = b->addTable (beam::table (ends, n - 1));
        // Coil-on-coil friction and air drag: a spring rings, but not like a bar.
        m.extraSigma = 1.5 + kPi * 0.0015 * om / (2.0 * kPi);
        b->addMode (m);
    }
    b->sortAndTrim (maxModes, maxOmega);
    return b;
}

} // namespace foundry
