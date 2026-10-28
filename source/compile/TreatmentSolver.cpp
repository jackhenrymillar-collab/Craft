#include "TreatmentSolver.h"

#include "LinearAlgebra.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace foundry
{
namespace
{
constexpr double kPi = std::numbers::pi;

// Small deterministic RNG (splitmix64) so layouts are stable per seed.
struct Rng
{
    std::uint64_t s;
    explicit Rng (std::uint64_t seed) : s (seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) {}
    std::uint64_t next()
    {
        std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double uniform() { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }
    double range (double lo, double hi) { return lo + (hi - lo) * uniform(); }
};

double lerp (double a, double b, double t) { return a + (b - a) * t; }

Vec2 randomPointInDisc (Rng& rng, double radius)
{
    const double r = radius * std::sqrt (rng.uniform());
    const double t = rng.range (0.0, 2.0 * kPi);
    return { r * std::cos (t), r * std::sin (t) };
}

// Smooth random scalar field as a sum of plane waves (zero mean, unit-ish RMS).
struct WaveField
{
    struct Wave { double kx, ky, phase; };
    std::vector<Wave> waves;

    WaveField (Rng& rng, int count, double minWavelength, double maxWavelength)
    {
        for (int i = 0; i < count; ++i)
        {
            const double lambda = rng.range (minWavelength, maxWavelength);
            const double ang = rng.range (0.0, kPi);
            const double k = 2.0 * kPi / lambda;
            waves.push_back ({ k * std::cos (ang), k * std::sin (ang), rng.range (0.0, 2.0 * kPi) });
        }
    }

    double operator() (Vec2 p) const
    {
        double s = 0.0;
        for (const auto& w : waves)
            s += std::sin (w.kx * p.x + w.ky * p.y + w.phase);
        return waves.empty() ? 0.0 : s * std::sqrt (2.0 / (double) waves.size());
    }
};

// One sample of the local material change, accumulated into the modal matrices.
struct LocalChange
{
    double fIso = 0.0;    // fraction of local stiffness/tension added
    Sym2 aniso;           // anisotropic stress (fraction of tension)
    double fMass = 0.0;   // fraction of local mass added
    double loss = 0.0;    // loss factor of the local strain energy
};

class Assembler
{
public:
    Assembler (const ModalBasis& b) : basis (b), n (b.size())
    {
        dK.assign ((size_t) (n * n), 0.0);
        dM.assign ((size_t) (n * n), 0.0);
        D.assign ((size_t) (n * n), 0.0);
        phi.resize ((size_t) n);
        gx.resize ((size_t) n);
        gy.resize ((size_t) n);
    }

    void add (Vec2 p, double weight, const LocalChange& c)
    {
        if (weight <= 0.0 || ! basis.contains (p))
            return;
        basis.shapesAndGradients (p, phi.data(), gx.data(), gy.data());

        if (c.fIso != 0.0 || c.aniso.xx != 0.0 || c.aniso.xy != 0.0 || c.aniso.yy != 0.0)
            basis.addStiffness (phi.data(), gx.data(), gy.data(), weight, c.fIso, c.aniso, dK.data());
        if (c.loss > 0.0)
            basis.addStiffness (phi.data(), gx.data(), gy.data(), weight * c.loss, 1.0, {}, D.data());
        if (c.fMass != 0.0)
        {
            const double w = weight * c.fMass * basis.massPerArea();
            for (int i = 0; i < n; ++i)
            {
                const double a = w * phi[(size_t) i];
                double* row = dM.data() + (size_t) i * (size_t) n;
                for (int j = i; j < n; ++j)
                    row[j] += a * phi[(size_t) j];
            }
        }
        touched = true;
    }

    // Spread a circular feature over a small polar stencil. `profile(u)` gives the
    // change at fractional distance u (0 centre .. 1 edge of the stencil).
    template <typename Profile>
    void addDisc (Vec2 c, double radius, Profile profile)
    {
        constexpr int rings = 4, spokes = 10;
        const double dr = radius / rings;
        add (c, kPi * (0.5 * dr) * (0.5 * dr), profile (0.0));
        for (int i = 1; i <= rings; ++i)
        {
            const double r = (i - 0.5) * dr + 0.5 * dr;
            const double ringArea = kPi * ((r + 0.5 * dr) * (r + 0.5 * dr) - (r - 0.5 * dr) * (r - 0.5 * dr));
            const int m = spokes * i;
            for (int j = 0; j < m; ++j)
            {
                const double t = (j + 0.5 * (i & 1)) * 2.0 * kPi / m;
                add ({ c.x + r * std::cos (t), c.y + r * std::sin (t) }, ringArea / m, profile (r / radius));
            }
        }
    }

    void symmetrise()
    {
        for (auto* A : { &dK, &dM, &D })
            for (int i = 0; i < n; ++i)
                for (int j = i + 1; j < n; ++j)
                    (*A)[(size_t) (j * n + i)] = (*A)[(size_t) (i * n + j)];
    }

    const ModalBasis& basis;
    int n;
    std::vector<double> dK, dM, D;
    bool touched = false;

private:
    std::vector<double> phi, gx, gy;
};

} // namespace

double materialDecayRate (double omega, double lossFactor, double h) noexcept
{
    const double f = omega / (2.0 * kPi);
    return kPi * lossFactor * f * (1.0 + std::max (0.0, h) * f / 1000.0);
}

AgeGlobals computeAgeGlobals (const Material& material, const AgeParams& age) noexcept
{
    AgeGlobals g;
    const double A = std::clamp (age.amount, 0.0, 1.0);
    const double c = std::clamp (age.climate, 0.0, 1.0);
    const auto& p = material.ageing;
    g.stiffnessScale = std::max (0.2, 1.0 + A * p.stiffness.at (c));
    g.massScale = std::max (0.3, 1.0 + A * p.mass.at (c));
    g.lossScale = std::max (0.1, 1.0 + A * p.loss.at (c));
    g.highLossPerKHz = std::max (0.0, A * p.highLoss.at (c));
    return g;
}

TreatedModes solveTreatments (const TreatmentContext& ctx, const Treatments& tIn)
{
    Treatments t = tIn;
    t.clampToRange();

    const ModalBasis& basis = *ctx.basis;
    const int n = basis.size();
    const double a = basis.characteristicSize();

    TreatedModes out;
    out.n = n;
    Assembler as (basis);

    const auto& prof = ctx.material->ageing;
    const double A = t.age.amount, clim = t.age.climate;

    // ---- Age: uneven stiffness, pits, cracks, grit ---------------------------------
    {
        Rng rng (t.seed ^ 0xA6E0001u);
        const double uneven = A * prof.unevenness.at (clim);
        WaveField field (rng, 6, 0.5 * a, 1.6 * a);

        const int pits = (int) std::lround (A * prof.pits.at (clim));
        const double pitDepth = A * prof.pitDepth.at (clim);
        Rng pitRng (t.seed ^ 0xB175u);
        for (int i = 0; i < pits; ++i)
        {
            const Vec2 c = randomPointInDisc (pitRng, 0.95 * a);
            const double r = a * pitRng.range (0.012, 0.045);
            const double depth = pitDepth * pitRng.range (0.5, 1.0);
            as.addDisc (c, r, [&] (double u) {
                const double g = 1.0 - u * u;
                LocalChange lc;
                lc.fMass = -0.9 * depth * g;
                lc.fIso = -1.5 * depth * g;  // thinning hits stiffness harder than mass
                lc.loss = 0.02 * depth * g;
                return lc;
            });
            out.features.push_back ({ TreatmentFeature::Kind::Pit, c, r, depth });
        }

        const int cracks = (int) std::lround (A * prof.cracks.at (clim));
        const double friction = A * prof.crackFriction.at (clim);
        Rng crackRng (t.seed ^ 0xC4AC4u);
        for (int i = 0; i < cracks; ++i)
        {
            // Cracks tend to start at the rim and run inwards.
            const double ang = crackRng.range (0.0, 2.0 * kPi);
            const double len = a * crackRng.range (0.15, 0.6);
            const double bend = crackRng.range (-0.6, 0.6);
            Vec2 p { 0.97 * a * std::cos (ang), 0.97 * a * std::sin (ang) };
            double dir = ang + kPi + crackRng.range (-0.4, 0.4);
            const int steps = 8;
            const double step = len / steps;
            for (int s = 0; s < steps; ++s)
            {
                as.addDisc (p, 0.03 * a, [&] (double u) {
                    LocalChange lc;
                    lc.fIso = -0.7 * A * (1.0 - u);
                    lc.loss = friction * (1.0 - u * u);
                    return lc;
                });
                out.features.push_back ({ TreatmentFeature::Kind::Crack, p, step, A });
                dir += bend / steps;
                p.x += step * std::cos (dir);
                p.y += step * std::sin (dir);
            }
        }

        out.grit += A * prof.grit.at (clim);

        // Uneven stiffness over the whole part (applied below with the twist field).
        if (uneven > 0.0 || t.twist.amount != 0.0)
        {
            const double tw = t.twist.amount;
            Rng twRng (t.seed ^ 0x7715Fu);
            const double pivotAngle = twRng.range (0.0, 2.0 * kPi);
            const Vec2 pivot { t.twist.pivot * 0.65 * a * std::cos (pivotAngle), t.twist.pivot * 0.65 * a * std::sin (pivotAngle) };
            const double r0 = a * lerp (0.9, 0.08, t.twist.focus);
            WaveField wrinkle (twRng, 5, 0.08 * a, 0.2 * a);
            const double wr = t.twist.wrinkle * std::abs (tw);
            if (tw != 0.0)
                out.features.push_back ({ TreatmentFeature::Kind::TwistPivot, pivot, r0, tw });

            std::vector<QuadPoint> quad;
            basis.quadrature (quad, 44);
            for (const auto& q : quad)
            {
                LocalChange lc;
                lc.fIso = uneven * field (q.p);
                if (tw != 0.0)
                {
                    const double dx = q.p.x - pivot.x, dy = q.p.y - pivot.y;
                    const double rho2 = dx * dx + dy * dy;
                    const double g = r0 * r0 / (rho2 + r0 * r0);
                    const double phiAng = std::atan2 (dy, dx);
                    // Torsion -> shear stress about the pivot, plus second-order stretching
                    // (|twist|^2) that tightens the part where the twist is strongest.
                    const double tau = 0.55 * tw * g;
                    lc.aniso.xx = -tau * std::sin (2.0 * phiAng);
                    lc.aniso.xy = tau * std::cos (2.0 * phiAng);
                    lc.aniso.yy = tau * std::sin (2.0 * phiAng);
                    lc.fIso += 0.8 * tw * tw * g;
                    if (wr > 0.0)
                    {
                        // Wrinkles: fine ridges along the tension direction, uneven and lossy.
                        const double w = 0.35 * wr * g * wrinkle (q.p);
                        lc.aniso.xx += w * std::cos (2.0 * phiAng + 0.785);
                        lc.aniso.yy -= w * std::cos (2.0 * phiAng + 0.785);
                        lc.aniso.xy += w * std::sin (2.0 * phiAng + 0.785);
                        lc.loss += 0.06 * wr * g;
                    }
                }
                as.add (q.p, q.weight, lc);
            }
            out.grit += 0.35 * wr;
        }
    }

    // ---- Deform: dents ---------------------------------------------------------------
    if (t.deform.intensity > 0.0)
    {
        Rng rng (t.seed ^ 0xDE70u);
        constexpr int maxDents = 48;
        const int count = 1 + (int) std::lround (t.deform.density * t.deform.density * (maxDents - 1));
        const double baseR = a * lerp (0.03, 0.4, t.deform.size);
        for (int i = 0; i < maxDents; ++i)
        {
            // Always draw the same random numbers so positions don't reshuffle with density.
            const Vec2 c = randomPointInDisc (rng, 0.92 * a);
            const double r = baseR * rng.range (0.6, 1.4);
            const double depth = t.deform.intensity * rng.range (0.5, 1.0);
            const double sign = rng.uniform() < 0.75 ? 1.0 : -1.0; // mostly dents, a few bumps
            if (i >= count)
                continue;
            const double kStiff = basis.dentStiffness(), kLoss = basis.dentLoss();
            as.addDisc (c, r, [&] (double u) {
                const double g = std::exp (-3.0 * u * u);
                LocalChange lc;
                lc.fIso = std::max (-0.9, kStiff * depth * g);
                lc.fMass = 0.08 * sign * depth * g;
                lc.loss = kLoss * depth * g;
                return lc;
            });
            out.features.push_back ({ TreatmentFeature::Kind::Dent, c, r, sign * depth });
        }
    }

    // ---- Puncture: holes ------------------------------------------------------------
    if (t.puncture.count > 0.0)
    {
        Rng rng (t.seed ^ 0x9D1Cu);
        const int count = (int) std::lround (t.puncture.count * kMaxPunctures);
        const double ringR = a * lerp (0.05, 0.85, t.puncture.placement);
        const double holeR = a * lerp (0.006, 0.1, std::pow (t.puncture.size, 1.5));
        const double ringPhase = rng.range (0.0, 2.0 * kPi);
        for (int i = 0; i < kMaxPunctures; ++i)
        {
            const double ang = ringPhase + 2.0 * kPi * i / std::max (1, count);
            const Vec2 ring { ringR * std::cos (ang), ringR * std::sin (ang) };
            const Vec2 rnd = randomPointInDisc (rng, 0.9 * a);
            const double sizeJitter = rng.range (0.7, 1.3);
            if (i >= count)
                continue;
            const double s = t.puncture.scatter;
            Vec2 c { lerp (ring.x, rnd.x, s), lerp (ring.y, rnd.y, s) };
            const double r = holeR * lerp (1.0, sizeJitter, s);
            const double ragged = t.puncture.ragged;
            // Inside the hole the material is gone; around it, a torn rim adds loss.
            as.addDisc (c, 1.7 * r, [&] (double u) {
                LocalChange lc;
                if (u < 1.0 / 1.7)
                {
                    lc.fMass = -0.95;
                    lc.fIso = -0.95;
                }
                else
                {
                    lc.loss = 0.03 + 0.25 * ragged;
                    lc.fIso = -0.3 * ragged;
                }
                return lc;
            });
            out.features.push_back ({ TreatmentFeature::Kind::Hole, c, r, ragged });
        }
        out.grit += 0.25 * t.puncture.ragged * std::min (1.0, count / 8.0);
    }

    out.grit = std::clamp (out.grit, 0.0, 1.0);

    // ---- Solve -------------------------------------------------------------------
    std::vector<double> lambda;
    if (as.touched)
    {
        as.symmetrise();
        std::vector<double> K = as.dK, M = as.dM;
        for (int i = 0; i < n; ++i)
        {
            K[(size_t) (i * n + i)] += basis.omega (i) * basis.omega (i);
            M[(size_t) (i * n + i)] += 1.0;
        }
        out.solvedOk = linalg::generalisedEigen (K, M, n, lambda, out.X);
        if (! out.solvedOk)
            out.X.clear();
        else
        {
            out.Xinv.assign ((size_t) (n * n), 0.0);
            for (int k = 0; k < n; ++k)
                for (int i = 0; i < n; ++i)
                {
                    const double xik = out.X[(size_t) (i * n + k)];
                    if (xik == 0.0) continue;
                    const double* mrow = M.data() + (size_t) i * (size_t) n;
                    double* dst = out.Xinv.data() + (size_t) k * (size_t) n;
                    for (int j = 0; j < n; ++j)
                        dst[j] += xik * mrow[j];
                }
        }
    }

    out.omega.resize ((size_t) n);
    out.sigma.resize ((size_t) n);
    const bool transformed = ! out.X.empty();
    for (int k = 0; k < n; ++k)
    {
        const double base = basis.omega (k);
        const double w = transformed ? std::sqrt (std::max (lambda[(size_t) k], 0.01 * basis.omega (0) * basis.omega (0)))
                                     : base;
        double sigma = materialDecayRate (w, ctx.lossFactor, ctx.highFreqDamping);
        if (transformed)
        {
            // Extra loss from local features: strain-energy fraction method.
            double e = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double xi = out.X[(size_t) (i * n + k)];
                if (xi == 0.0) continue;
                const double* row = as.D.data() + (size_t) i * (size_t) n;
                double s = 0.0;
                for (int j = 0; j < n; ++j)
                    s += row[j] * out.X[(size_t) (j * n + k)];
                e += xi * s;
            }
            sigma += std::max (0.0, e) / (2.0 * w);
        }
        out.omega[(size_t) k] = w;
        out.sigma[(size_t) k] = std::min (sigma, 0.9 * w);
    }
    return out;
}

} // namespace foundry
