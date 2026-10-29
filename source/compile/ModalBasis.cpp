#include "ModalBasis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace foundry
{
namespace
{
constexpr double kPi = std::numbers::pi;
}

bool ModalBasis::contains (Vec2 p) const noexcept
{
    const Vec2 h = halfExtents();
    if (isRound())
        return p.x * p.x + p.y * p.y < h.x * h.x;
    return std::abs (p.x) < h.x && std::abs (p.y) < h.y;
}

double ModalBasis::area() const noexcept
{
    const Vec2 h = halfExtents();
    return isRound() ? kPi * h.x * h.x : 4.0 * h.x * h.y;
}

double ModalBasis::characteristicSize() const noexcept
{
    return std::sqrt (area() / kPi);
}

double ModalBasis::minHalfExtent() const noexcept
{
    const Vec2 h = halfExtents();
    return isRound() ? h.x : std::min (h.x, h.y);
}

void ModalBasis::shapesAndGradients (Vec2 p, double* phi, double* gx, double* gy) const noexcept
{
    const int n = size();
    const double h = 1e-5 * std::max (1e-3, characteristicSize());
    shapes (p, phi);
    shapes ({ p.x + h, p.y }, gx);
    shapes ({ p.x, p.y + h }, gy);
    for (int k = 0; k < n; ++k)
    {
        gx[k] = (gx[k] - phi[k]) / h;
        gy[k] = (gy[k] - phi[k]) / h;
    }
}

void ModalBasis::quadrature (std::vector<QuadPoint>& out, int resolution) const
{
    const Vec2 h = halfExtents();
    if (isRound())
    {
        // Polar midpoint rule: nr rings, angular count growing with radius.
        const double a = h.x;
        const int nr = std::max (4, resolution / 2);
        const double dr = a / nr;
        for (int i = 0; i < nr; ++i)
        {
            const double r = (i + 0.5) * dr;
            const int nt = std::max (6, (int) std::ceil (2.0 * kPi * r / dr));
            const double dt = 2.0 * kPi / nt;
            for (int j = 0; j < nt; ++j)
            {
                const double t = (j + 0.5 * (i & 1)) * dt;
                out.push_back ({ { r * std::cos (t), r * std::sin (t) }, r * dr * dt });
            }
        }
        return;
    }
    // Midpoint grid with about resolution^2 * pi/4 cells, split by aspect ratio.
    const double total = std::max (16.0, 0.8 * resolution * resolution);
    const double aspect = h.x / h.y;
    const int nx = std::max (3, (int) std::lround (std::sqrt (total * aspect)));
    const int ny = std::max (3, (int) std::lround (total / nx));
    const double dx = 2.0 * h.x / nx, dy = 2.0 * h.y / ny;
    for (int i = 0; i < nx; ++i)
        for (int j = 0; j < ny; ++j)
            out.push_back ({ { -h.x + (i + 0.5) * dx, -h.y + (j + 0.5) * dy }, dx * dy });
}

Vec2 ModalBasis::place (double r, double theta) const noexcept
{
    const Vec2 h = halfExtents();
    const double hy = isRound() ? h.x : h.y;
    return { r * h.x * std::cos (theta), r * hy * std::sin (theta) };
}

Vec2 ModalBasis::interiorPoint (double u1, double u2, double margin) const noexcept
{
    const Vec2 h = halfExtents();
    if (isRound())
    {
        const double r = margin * h.x * std::sqrt (u1);
        const double t = 2.0 * kPi * u2;
        return { r * std::cos (t), r * std::sin (t) };
    }
    return { margin * h.x * (2.0 * u1 - 1.0), margin * h.y * (2.0 * u2 - 1.0) };
}

void ModalBasis::edgePoint (double u, Vec2& p, double& inward) const noexcept
{
    const Vec2 h = halfExtents();
    if (isRound())
    {
        const double ang = 2.0 * kPi * u;
        p = { 0.97 * h.x * std::cos (ang), 0.97 * h.x * std::sin (ang) };
        inward = ang + kPi;
        return;
    }
    // Walk the perimeter: bottom, right, top, left.
    const double per = 4.0 * (h.x + h.y);
    double s = std::fmod (std::max (0.0, u), 1.0) * per;
    const double ix = 0.97 * h.x, iy = 0.97 * h.y;
    if (s < 2.0 * h.x) { p = { -ix + s * 0.97, -iy }; inward = 0.5 * kPi; return; }
    s -= 2.0 * h.x;
    if (s < 2.0 * h.y) { p = { ix, -iy + s * 0.97 }; inward = kPi; return; }
    s -= 2.0 * h.y;
    if (s < 2.0 * h.x) { p = { ix - s * 0.97, iy }; inward = -0.5 * kPi; return; }
    s -= 2.0 * h.x;
    p = { -ix, iy - s * 0.97 };
    inward = 0.0;
}

double ModalBasis::referenceTension() const noexcept
{
    // A membrane of the same size and mass with the same fundamental: T = sigma (omega0 / k0)^2,
    // with k0 = 2.405 / equivalent radius.
    const double k0 = 2.405 / std::max (1e-4, characteristicSize());
    const double w0 = size() > 0 ? omega (0) : 0.0;
    return massPerArea() * (w0 / k0) * (w0 / k0);
}

void ModalBasis::addStiffness (const double* phi, const double* gx, const double* gy,
                               double weight, double fIso, Sym2 S, double* K) const noexcept
{
    const int n = size();
    const double mw = massPerArea() * weight * fIso;
    const bool stress = S.xx != 0.0 || S.xy != 0.0 || S.yy != 0.0;
    const double T = referenceTension() * weight;
    std::array<double, kMaxExactModes> a {};
    for (int i = 0; i < n; ++i)
        a[(size_t) i] = omega (i) * phi[i];
    for (int i = 0; i < n; ++i)
    {
        double* row = K + (size_t) i * (size_t) n;
        const double ai = mw * a[(size_t) i];
        if (stress)
        {
            const double sx = T * (S.xx * gx[i] + S.xy * gy[i]);
            const double sy = T * (S.xy * gx[i] + S.yy * gy[i]);
            for (int j = i; j < n; ++j)
                row[j] += ai * a[(size_t) j] + sx * gx[j] + sy * gy[j];
        }
        else if (ai != 0.0)
            for (int j = i; j < n; ++j)
                row[j] += ai * a[(size_t) j];
    }
}

double ModalBasis::normalisation (int) const noexcept
{
    return 1.0 / std::sqrt (std::max (1e-12, massPerArea() * area()));
}

void ModalBasis::radiation (std::vector<double>& volume, std::vector<double>& dx, std::vector<double>& dy) const
{
    const int n = size();
    volume.assign ((size_t) n, 0.0);
    dx.assign ((size_t) n, 0.0);
    dy.assign ((size_t) n, 0.0);
    std::vector<QuadPoint> quad;
    quadrature (quad, 48);
    std::vector<double> phi ((size_t) n);
    for (const auto& q : quad)
    {
        shapes (q.p, phi.data());
        for (int k = 0; k < n; ++k)
        {
            const double a = q.weight * phi[(size_t) k];
            volume[(size_t) k] += a;
            dx[(size_t) k] += a * q.p.x;
            dy[(size_t) k] += a * q.p.y;
        }
    }
}

} // namespace foundry
