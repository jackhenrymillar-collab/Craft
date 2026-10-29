#include "PlateSolver.h"

#include "LinearAlgebra.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace foundry
{
namespace
{
constexpr double kPi = std::numbers::pi;
constexpr double kAirDensity = 1.2;
constexpr double kSoundSpeed = 343.0;
}

void PlateBasis::warburton (int i, double& G, double& H, double& J) noexcept
{
    if (i == 0) { G = 0.0; H = 0.0; J = 0.0; return; }
    if (i == 1) { G = 0.0; H = 0.0; J = 12.0 / (kPi * kPi); return; }
    if (i == 2) { G = 1.506; H = 1.248; J = 5.017; return; }
    const double m = i - 0.5;
    G = m;
    H = m * m * (1.0 - 2.0 / (m * kPi));
    J = m * m * (1.0 + 6.0 / (m * kPi));
}

double PlateBasis::bendingSpeedSquared() const noexcept
{
    const auto& p = params;
    const double D = p.youngs * p.thickness * p.thickness * p.thickness / (12.0 * (1.0 - p.poisson * p.poisson));
    return std::sqrt (D / (p.density * p.thickness));
}

PlateBasis::PlateBasis (const Params& p) : params (p)
{
    const double W = p.width, H = p.height, nu = p.poisson;
    const double c2 = bendingSpeedSquared();
    norm = 1.0 / std::sqrt (massPerArea() * W * H);

    struct Cand { int i, j; double w; };
    std::vector<Cand> cands;
    constexpr int maxIndex = 64;
    if (p.freeEdges)
    {
        const double r = W / H;
        double G[maxIndex], Hc[maxIndex], J[maxIndex];
        for (int i = 0; i < maxIndex; ++i)
            warburton (i, G[i], Hc[i], J[i]);
        for (int i = 0; i < maxIndex; ++i)
            for (int j = 0; j < maxIndex; ++j)
            {
                const double lam = std::pow (G[i], 4) + std::pow (G[j], 4) * std::pow (r, 4)
                                 + 2.0 * r * r * (nu * Hc[i] * Hc[j] + (1.0 - nu) * J[i] * J[j]);
                const double w = kPi * kPi * c2 / (W * W) * std::sqrt (std::max (0.0, lam));
                if (w < 2.0 * kPi * 1.0) continue; // rigid-body motion
                if (w > p.maxOmega) continue;
                cands.push_back ({ i, j, w });
            }
    }
    else
    {
        for (int i = 1; i <= maxIndex; ++i)
            for (int j = 1; j <= maxIndex; ++j)
            {
                const double w = c2 * kPi * kPi * ((i / W) * (i / W) + (j / H) * (j / H));
                if (w > p.maxOmega) continue;
                cands.push_back ({ i, j, w });
            }
    }
    std::sort (cands.begin(), cands.end(), [] (const Cand& a, const Cand& b) { return a.w < b.w; });
    const int cap = std::clamp (p.maxModes, 1, kMaxExactModes);
    for (const auto& c : cands)
    {
        if ((int) modes.size() >= cap) break;
        modes.push_back ({ c.i, c.j, c.w });
        if (p.freeEdges)
        {
            tx.push_back (beam::table (beam::Ends::FreeFree, c.i));
            ty.push_back (beam::table (beam::Ends::FreeFree, c.j));
        }
        else
        {
            tx.push_back (beam::table (beam::Ends::PinnedPinned, c.i - 1));
            ty.push_back (beam::table (beam::Ends::PinnedPinned, c.j - 1));
        }
    }
}

void PlateBasis::shapes (Vec2 p, double* phi) const noexcept
{
    const int n = size();
    if (! contains (p))
    {
        std::fill (phi, phi + n, 0.0);
        return;
    }
    const double xi = p.x / params.width + 0.5, eta = p.y / params.height + 0.5;
    for (int k = 0; k < n; ++k)
        phi[k] = norm * tx[(size_t) k]->value (xi) * ty[(size_t) k]->value (eta);
}

void PlateBasis::shapesAndGradients (Vec2 p, double* phi, double* gx, double* gy) const noexcept
{
    const int n = size();
    if (! contains (p))
    {
        std::fill (phi, phi + n, 0.0);
        std::fill (gx, gx + n, 0.0);
        std::fill (gy, gy + n, 0.0);
        return;
    }
    const double xi = p.x / params.width + 0.5, eta = p.y / params.height + 0.5;
    for (int k = 0; k < n; ++k)
    {
        double X, dX, Y, dY;
        tx[(size_t) k]->eval (xi, X, dX);
        ty[(size_t) k]->eval (eta, Y, dY);
        phi[k] = norm * X * Y;
        gx[k] = norm * dX / params.width * Y;
        gy[k] = norm * X * dY / params.height;
    }
}

double PlateBasis::modeCountBelow (double w) const noexcept
{
    // Weyl's law for a thin plate: N(omega) ~ A omega / (4 pi sqrt(D / rho h)).
    return area() * w / (4.0 * kPi * bendingSpeedSquared());
}

double PlateBasis::wavenumber (double w) const noexcept
{
    return std::sqrt (std::max (0.0, w) / bendingSpeedSquared());
}

// ---- Box ---------------------------------------------------------------------------------

BoxBasis::BoxBasis (const Params& p) : params (p)
{
    auto faceParams = p.face;
    faceParams.freeEdges = false;
    faceParams.type = PartType::Box;
    faceParams.maxModes = std::max (1, p.face.maxModes - 1);
    face = std::make_shared<PlateBasis> (faceParams);
    nf = face->size();

    const double V = p.face.width * p.face.height * p.depth;
    const double airK = kAirDensity * kSoundSpeed * kSoundSpeed / V;

    // Net volume per unit coordinate of each face mode (exact for sine modes).
    faceVolume.assign ((size_t) nf, 0.0);
    for (int k = 0; k < nf; ++k)
    {
        const auto& md = face->mode (k);
        auto lobe = [] (int m) { return (m % 2 == 1) ? 2.0 * std::numbers::sqrt2 / (m * kPi) : 0.0; };
        faceVolume[(size_t) k] = std::sqrt (face->area() / face->massPerArea()) * lobe (md.i) * lobe (md.j);
    }

    hasPort = p.portRadius > 1e-4;
    n = nf + (hasPort ? 1 : 0);
    std::vector<double> g ((size_t) n, 0.0);
    for (int k = 0; k < nf; ++k) g[(size_t) k] = faceVolume[(size_t) k];
    double portMass = 0.0;
    if (hasPort)
    {
        const double S = kPi * p.portRadius * p.portRadius;
        const double Leff = p.backThickness + 1.7 * p.portRadius; // flanged both sides
        portMass = kAirDensity * S * Leff;
        // Positive face displacement points into the box (the way it's struck), so pushing the
        // face in and letting air out of the port act against each other on the cavity.
        g[(size_t) nf] = -S / std::sqrt (portMass);
        helmholtz = kSoundSpeed / (2.0 * kPi) * std::sqrt (S / (V * Leff));
    }
    portGain = g;

    std::vector<double> K ((size_t) (n * n), 0.0);
    for (int i = 0; i < n; ++i)
    {
        if (i < nf) K[(size_t) (i * n + i)] = face->omega (i) * face->omega (i);
        for (int j = 0; j < n; ++j)
            K[(size_t) (i * n + j)] += airK * g[(size_t) i] * g[(size_t) j];
    }
    std::vector<double> lambda;
    linalg::symmetricEigen (K, n, lambda, X);

    omegas.resize ((size_t) n);
    portShare.assign ((size_t) n, 0.0);
    portDecay.assign ((size_t) n, 0.0);
    for (int k = 0; k < n; ++k)
    {
        omegas[(size_t) k] = std::sqrt (std::max (lambda[(size_t) k], 1.0));
        if (hasPort)
        {
            const double x = X[(size_t) (nf * n + k)];
            portShare[(size_t) k] = x * x;
            // The port radiates and rubs: a Helmholtz resonance has a Q of about 12.
            portDecay[(size_t) k] = x * x * omegas[(size_t) k] / (2.0 * 12.0);
        }
    }
}

void BoxBasis::shapes (Vec2 p, double* phi) const noexcept
{
    std::array<double, kMaxExactModes> f {};
    face->shapes (p, f.data());
    for (int k = 0; k < n; ++k)
    {
        double s = 0.0;
        for (int i = 0; i < nf; ++i)
            s += f[(size_t) i] * X[(size_t) (i * n + k)];
        phi[k] = s;
    }
}

void BoxBasis::shapesAndGradients (Vec2 p, double* phi, double* gx, double* gy) const noexcept
{
    std::array<double, kMaxExactModes> f {}, fx {}, fy {};
    face->shapesAndGradients (p, f.data(), fx.data(), fy.data());
    for (int k = 0; k < n; ++k)
    {
        double s = 0.0, sx = 0.0, sy = 0.0;
        for (int i = 0; i < nf; ++i)
        {
            const double x = X[(size_t) (i * n + k)];
            s += f[(size_t) i] * x;
            sx += fx[(size_t) i] * x;
            sy += fy[(size_t) i] * x;
        }
        phi[k] = s;
        gx[k] = sx;
        gy[k] = sy;
    }
}

void BoxBasis::radiation (std::vector<double>& volume, std::vector<double>& dx, std::vector<double>& dy) const
{
    std::vector<double> fv, fdx, fdy;
    face->radiation (fv, fdx, fdy);
    volume.assign ((size_t) n, 0.0);
    dx.assign ((size_t) n, 0.0);
    dy.assign ((size_t) n, 0.0);
    for (int k = 0; k < n; ++k)
    {
        for (int i = 0; i < nf; ++i)
        {
            const double x = X[(size_t) (i * n + k)];
            volume[(size_t) k] += fv[(size_t) i] * x;
            dx[(size_t) k] += fdx[(size_t) i] * x;
            dy[(size_t) k] += fdy[(size_t) i] * x;
        }
        if (hasPort)
            volume[(size_t) k] += portGain[(size_t) nf] * X[(size_t) (nf * n + k)];
    }
}

} // namespace foundry
