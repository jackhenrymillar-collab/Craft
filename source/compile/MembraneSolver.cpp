#include "MembraneSolver.h"

#include "Bessel.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <numbers>

namespace foundry
{
namespace
{
constexpr int kZeroM = 48;
constexpr double kZeroMax = 64.0; // enough for kMaxExactModes even at the bottom of the tune range

struct ZeroTable
{
    std::vector<double> z[kZeroM];

    ZeroTable()
    {
        // One scan per order m for the sign changes of J_m below kZeroMax.
        for (int m = 0; m < kZeroM; ++m)
        {
            double x = (m == 0 ? 0.5 : (double) m);
            double fPrev = bessel::J (m, x);
            const double step = 0.1; // zeros are ~pi apart
            while (x < kZeroMax)
            {
                const double xn = x + step;
                const double f = bessel::J (m, xn);
                if ((fPrev < 0.0) != (f < 0.0))
                {
                    double lo = x, hi = xn, flo = fPrev;
                    for (int it = 0; it < 52; ++it)
                    {
                        const double mid = 0.5 * (lo + hi);
                        const double fm = bessel::J (m, mid);
                        if ((fm < 0.0) == (flo < 0.0)) { lo = mid; flo = fm; }
                        else hi = mid;
                    }
                    z[m].push_back (0.5 * (lo + hi));
                }
                x = xn;
                fPrev = f;
            }
        }
    }
};

const ZeroTable& zeroTable()
{
    static const ZeroTable table;
    return table;
}
} // namespace

double besselZeroCached (int m, int n) noexcept
{
    if (m >= 0 && m < kZeroM && n >= 1 && n <= (int) zeroTable().z[m].size())
        return zeroTable().z[m][(size_t) (n - 1)];
    return bessel::zero (m, n);
}

std::shared_ptr<const MembraneBasis::Radial> MembraneBasis::radialTable (int m, int n)
{
    static std::mutex lock;
    static std::map<std::pair<int, int>, std::shared_ptr<const Radial>> cache;
    {
        std::lock_guard g (lock);
        if (auto it = cache.find ({ m, n }); it != cache.end())
            return it->second;
    }
    const double j = besselZeroCached (m, n);
    auto rad = std::make_shared<Radial>();
    rad->R.resize (kTable + 1);
    rad->dR.resize (kTable + 1);
    rad->Rr.resize (kTable + 1);
    for (int i = 0; i <= kTable; ++i)
    {
        const double rho = (double) i / kTable;
        double Jv, dJv;
        bessel::JandDerivative (m, j * rho, Jv, dJv);
        rad->R[(size_t) i] = Jv;
        rad->dR[(size_t) i] = j * dJv;
        rad->Rr[(size_t) i] = i == 0 ? (m == 1 ? 0.5 * j : 0.0) : rad->R[(size_t) i] / rho;
    }
    std::lock_guard g (lock);
    return cache.emplace (std::make_pair (m, n), std::move (rad)).first->second;
}

Vec2 MembraneBasis::polarToLocal (double rFraction, double theta, double radius) noexcept
{
    return { rFraction * radius * std::cos (theta), rFraction * radius * std::sin (theta) };
}

MembraneBasis::MembraneBasis (const Params& p) : params (p)
{
    const double c = std::sqrt (params.tension / params.arealDensity);
    const double a = params.radius;
    const double jMax = params.maxFrequency * 2.0 * std::numbers::pi * a / c;

    struct Candidate { int m, n; double j; };
    std::vector<Candidate> cands;
    const auto& table = zeroTable();
    for (int m = 0; m < kZeroM; ++m)
        for (int n = 1; n <= (int) table.z[m].size(); ++n)
        {
            const double j = table.z[m][(size_t) (n - 1)];
            if (j > jMax) break;
            cands.push_back ({ m, n, j });
        }
    std::sort (cands.begin(), cands.end(), [] (const Candidate& x, const Candidate& y) { return x.j < y.j; });

    const int cap = std::clamp (params.maxModes, 1, kMaxExactModes);
    for (const auto& cd : cands)
    {
        const int needed = cd.m == 0 ? 1 : 2;
        if ((int) modes.size() + needed > cap)
            break;

        const double jn1 = bessel::J (cd.m + 1, cd.j);
        const double angular = cd.m == 0 ? std::numbers::pi : 0.5 * std::numbers::pi;
        const double modalMass = params.arealDensity * a * a * angular * jn1 * jn1;
        // m = 0: sigma * 2pi * a^2/2 * J1^2 = sigma * pi a^2 J1^2. m > 0: sigma * pi * a^2/2 * J_{m+1}^2.
        const double norm = 1.0 / std::sqrt (modalMass);

        radial.push_back (radialTable (cd.m, cd.n));
        const int ri = (int) radial.size() - 1;

        const double w = cd.j * c / a;
        modes.push_back ({ cd.m, cd.n, false, cd.j, w, norm });
        radialIndex.push_back (ri);
        if (cd.m > 0)
        {
            modes.push_back ({ cd.m, cd.n, true, cd.j, w, norm });
            radialIndex.push_back (ri);
        }
    }
}

double MembraneBasis::modeCountBelow (double w) const noexcept
{
    // Weyl's law for a 2-D membrane: N(omega) ~ A omega^2 / (4 pi c^2).
    const double c2 = params.tension / params.arealDensity;
    return area() * w * w / (4.0 * std::numbers::pi * c2);
}

double MembraneBasis::wavenumber (double w) const noexcept
{
    return w / std::sqrt (params.tension / params.arealDensity);
}

bool MembraneBasis::contains (Vec2 p) const noexcept
{
    return p.x * p.x + p.y * p.y < params.radius * params.radius;
}

double MembraneBasis::area() const noexcept
{
    return std::numbers::pi * params.radius * params.radius;
}

void MembraneBasis::evalRadial (int k, double rho, double& R, double& dR, double& Rr) const noexcept
{
    const auto& t = *radial[(size_t) radialIndex[(size_t) k]];
    const double x = std::clamp (rho, 0.0, 1.0) * kTable;
    const int i = std::min ((int) x, kTable - 1);
    const double u = x - i;
    const double h = 1.0 / kTable;

    // Cubic Hermite for R using the exact derivative; linear for dR and R/rho.
    const double u2 = u * u, u3 = u2 * u;
    const double h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
    R = h00 * t.R[(size_t) i] + h10 * h * t.dR[(size_t) i] + h01 * t.R[(size_t) i + 1] + h11 * h * t.dR[(size_t) i + 1];
    dR = t.dR[(size_t) i] + u * (t.dR[(size_t) i + 1] - t.dR[(size_t) i]);
    Rr = t.Rr[(size_t) i] + u * (t.Rr[(size_t) i + 1] - t.Rr[(size_t) i]);
}

void MembraneBasis::shapes (Vec2 p, double* phi) const noexcept
{
    const double a = params.radius;
    const double r = std::sqrt (p.x * p.x + p.y * p.y);
    const int n = size();
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
        double R, dR, Rr;
        evalRadial (k, rho, R, dR, Rr);
        const double ang = md.m == 0 ? 1.0 : (md.isSine ? std::sin (md.m * theta) : std::cos (md.m * theta));
        phi[k] = md.norm * R * ang;
    }
}

void MembraneBasis::shapesAndGradients (Vec2 p, double* phi, double* gx, double* gy) const noexcept
{
    const double a = params.radius;
    const double r = std::sqrt (p.x * p.x + p.y * p.y);
    const int n = size();
    if (r >= a)
    {
        std::fill (phi, phi + n, 0.0);
        std::fill (gx, gx + n, 0.0);
        std::fill (gy, gy + n, 0.0);
        return;
    }
    const double theta = std::atan2 (p.y, p.x);
    const double ct = std::cos (theta), st = std::sin (theta);
    const double rho = r / a;
    for (int k = 0; k < n; ++k)
    {
        const auto& md = modes[(size_t) k];
        double R, dR, Rr;
        evalRadial (k, rho, R, dR, Rr);
        double ang = 1.0, dAng = 0.0;
        if (md.m > 0)
        {
            const double cm = std::cos (md.m * theta), sm = std::sin (md.m * theta);
            ang = md.isSine ? sm : cm;
            dAng = md.isSine ? md.m * cm : -md.m * sm;
        }
        phi[k] = md.norm * R * ang;
        const double gr = md.norm * dR * ang / a;     // d phi / dr
        const double gt = md.norm * Rr * dAng / a;    // (1/r) d phi / d theta
        gx[k] = gr * ct - gt * st;
        gy[k] = gr * st + gt * ct;
    }
}

void MembraneBasis::quadrature (std::vector<QuadPoint>& out, int resolution) const
{
    // Polar midpoint rule: nr rings, angular count growing with radius.
    const double a = params.radius;
    const int nr = std::max (4, resolution / 2);
    const double dr = a / nr;
    for (int i = 0; i < nr; ++i)
    {
        const double r = (i + 0.5) * dr;
        const int nt = std::max (6, (int) std::ceil (2.0 * std::numbers::pi * r / dr));
        const double dt = 2.0 * std::numbers::pi / nt;
        for (int j = 0; j < nt; ++j)
        {
            const double t = (j + 0.5 * (i & 1)) * dt;
            out.push_back ({ { r * std::cos (t), r * std::sin (t) }, r * dr * dt });
        }
    }
}

void MembraneBasis::addStiffness (const double* phi, const double* gx, const double* gy,
                                  double weight, double fIso, Sym2 S, double* K) const noexcept
{
    // Membrane strain energy: 1/2 * integral of grad(w)^T (T (1 + fIso) I + T S) grad(w).
    // Mass-normalised modes already include 1/sigma, so the kernel is T * (...).
    (void) phi;
    const int n = size();
    const double T = params.tension * weight;
    for (int i = 0; i < n; ++i)
    {
        const double ax = T * ((fIso + S.xx) * gx[i] + S.xy * gy[i]);
        const double ay = T * (S.xy * gx[i] + (fIso + S.yy) * gy[i]);
        double* row = K + (size_t) i * (size_t) n;
        for (int j = i; j < n; ++j)
            row[j] += ax * gx[j] + ay * gy[j];
    }
}

} // namespace foundry
