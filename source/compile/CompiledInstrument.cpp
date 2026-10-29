#include "CompiledInstrument.h"

#include "MembraneSolver.h"
#include "PlateSolver.h"
#include "ShellSolver.h"
#include "StripSolver.h"
#include "dsp/Calibrate.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <numbers>

namespace foundry
{
namespace
{
constexpr double kPi = std::numbers::pi;

MaterialId resolveMaterial (const PartSpec& p) noexcept
{
    return materialAllowed (p.type, p.material) ? p.material : partInfo (p.type).defaultMaterial;
}

// Losses into whatever holds the part (cords, a frame, hooks), plus viscous air drag: a
// floor under the material and radiation losses so nothing rings for minutes unless it's
// meant to (Hyperglass). sigma = a + pi * b * f.
double mountDecay (PartType t, int mount, double omega) noexcept
{
    const double f = omega / (2.0 * kPi);
    double a = 0.0, b = 0.0;
    switch (t)
    {
        case PartType::Bar:    a = mount == 0 ? 0.3 : 0.8; b = mount == 0 ? 0.0002 : 0.0006; break;
        case PartType::Plate:  a = mount == 0 ? 0.3 : 1.0; b = mount == 0 ? 0.00025 : 0.0006; break;
        case PartType::Shell:  a = 0.05; b = 0.00005; break;
        case PartType::Tube:   a = 0.1;  b = 0.0001; break;
        case PartType::Spring: a = 0.0;  b = 0.0; break;   // has its own coil friction
        case PartType::Box:    a = 0.5;  b = 0.0005; break;
        case PartType::Membrane:
        case PartType::Count:  break;
    }
    return a + kPi * b * f;
}

// Deterministic +-1 from an index and a seed (signs of statistical radiation terms).
double hashSign (std::uint64_t i, std::uint64_t seed) noexcept
{
    std::uint64_t z = (i + 1) * 0x9E3779B97F4A7C15ull ^ seed;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return ((z ^ (z >> 31)) & 1ull) ? 1.0 : -1.0;
}
} // namespace

double tuneRatio (double semitones) noexcept
{
    return std::pow (2.0, semitones / 12.0);
}

std::shared_ptr<const ModalBasis> makePartBasis (const PartSpec& part, double tuneSemitones, double sampleRate,
                                                 int maxModes, const AgeGlobals& age)
{
    const Material& mat = getMaterial (resolveMaterial (part));
    // Size/Tune scales the whole object: a bigger object is a lower one.
    const double ratio = tuneRatio (tuneSemitones);
    const double maxOmega = 0.45 * 2.0 * kPi * sampleRate;
    const double E = mat.youngsGPa * 1e9 * age.stiffnessScale;
    const double rho = mat.density * age.massScale;

    switch (part.type)
    {
        case PartType::Membrane:
        {
            MembraneBasis::Params bp;
            bp.radius = 0.5 * part.control (0) / ratio;
            bp.tension = part.control (1) * age.stiffnessScale;
            bp.arealDensity = (mat.arealDensity > 0 ? mat.arealDensity : 0.26) * age.massScale;
            bp.maxFrequency = 0.45 * sampleRate;
            bp.maxModes = maxModes;
            return std::make_shared<MembraneBasis> (bp);
        }
        case PartType::Bar:    return makeBarBasis (part, mat, age, ratio, maxModes, maxOmega);
        case PartType::Tube:   return makeTubeBasis (part, mat, age, ratio, maxModes, maxOmega);
        case PartType::Spring: return makeSpringBasis (part, mat, age, ratio, maxModes, maxOmega);
        case PartType::Plate:
        {
            PlateBasis::Params pp;
            pp.width = part.control (0) / ratio;
            pp.height = pp.width / part.control (1);
            pp.thickness = part.control (2) / ratio;
            pp.youngs = E;
            pp.density = rho;
            pp.freeEdges = part.mount == 0;
            pp.maxModes = maxModes;
            pp.maxOmega = maxOmega;
            return std::make_shared<PlateBasis> (pp);
        }
        case PartType::Shell:
        {
            ShellBasis::Params sp;
            sp.radius = 0.5 * part.control (0) / ratio;
            sp.profile = part.control (1);
            sp.wall = part.control (2) / ratio;
            sp.youngs = E;
            sp.density = rho;
            sp.resting = part.mount == 0;
            sp.maxModes = maxModes;
            sp.maxOmega = maxOmega;
            return std::make_shared<ShellBasis> (sp);
        }
        case PartType::Box:
        {
            BoxBasis::Params bp;
            bp.face.width = part.control (0) / ratio;
            bp.face.height = 1.6 * bp.face.width;          // cajon proportions
            bp.face.thickness = 0.004 / ratio;             // 4 mm ply face
            bp.face.youngs = E;
            bp.face.density = rho;
            bp.face.maxModes = maxModes;
            bp.face.maxOmega = maxOmega;
            bp.depth = part.control (1) / ratio;
            bp.portRadius = part.control (2) * 0.06 / ratio;
            bp.backThickness = 0.006 / ratio;
            return std::make_shared<BoxBasis> (bp);
        }
        case PartType::Count: break;
    }
    return nullptr;
}

void CompiledInstrument::shapeAt (Vec2 p, double* out, double* scratch) const noexcept
{
    const int ne = nExact;
    if (X.empty())
        basis->shapes (p, out);
    else
    {
        basis->shapes (p, scratch);
        for (int k = 0; k < ne; ++k)
            out[k] = 0.0;
        for (int i = 0; i < ne; ++i)
        {
            const double s = scratch[i];
            if (s == 0.0) continue;
            const double* row = X.data() + (size_t) i * (size_t) ne;
            for (int k = 0; k < ne; ++k)
                out[k] += s * row[k];
        }
    }
    const bool inside = basis->contains (p);
    for (int k = ne; k < n; ++k)
        out[k] = inside ? highStrike[(size_t) k] : 0.0;
}

std::unique_ptr<CompiledInstrument> compileInstrument (const InstrumentSpec& specIn, double sampleRate,
                                                       std::uint64_t version)
{
    const auto t0 = std::chrono::steady_clock::now();
    auto ci = std::make_unique<CompiledInstrument>();
    ci->spec = specIn;
    ci->spec.treatments.clampToRange();
    ci->spec.part.material = resolveMaterial (ci->spec.part);
    ci->spec.part.mount = std::clamp (ci->spec.part.mount, 0, partInfo (ci->spec.part.type).mountCount - 1);
    const auto& spec = ci->spec;
    ci->sampleRate = sampleRate;
    ci->version = version;

    const Material& mat = getMaterial (spec.part.material);
    const AgeGlobals age = computeAgeGlobals (mat, spec.treatments.age);

    auto basis = makePartBasis (spec.part, spec.tuneSemitones, sampleRate, spec.quality, age);
    ci->basis = basis;
    const int nExact = basis->size();
    ci->nExact = nExact;

    // A muffled drum head (pillow, tape): more loss everywhere, most at the top.
    const double muffle = spec.part.type == PartType::Membrane ? spec.part.control (2) : 0.0;

    TreatmentContext ctx;
    ctx.basis = basis.get();
    ctx.material = &mat;
    ctx.lossFactor = mat.lossFactor * age.lossScale * (1.0 + 10.0 * muffle);
    ctx.highFreqDamping = spec.part.highFreqDamping + age.highLossPerKHz + 1.5 * muffle;
    TreatedModes tm = solveTreatments (ctx, spec.treatments);

    ci->X = std::move (tm.X);
    ci->Xinv = std::move (tm.Xinv);
    ci->omega = std::move (tm.omega);
    ci->sigma = std::move (tm.sigma);
    ci->grit = tm.grit;
    ci->features = std::move (tm.features);
    ci->highStrike.assign ((size_t) nExact, 0.0);
    ci->wavenumber.resize ((size_t) nExact);
    for (int k = 0; k < nExact; ++k)
        ci->wavenumber[(size_t) k] = basis->wavenumber (ci->omega[(size_t) k]);

    const double typical = 1.0 / std::sqrt (basis->massPerArea() * basis->area());
    // Edge/corner radiation floor for modes with little net volume, growing toward
    // coincidence (where the structural wavelength reaches the air wavelength).
    auto radiationFloor = [&] (double w) {
        const double ks = std::max (1e-6, basis->wavenumber (w));
        // Capped well below 1: a truncated set of modes radiating at full efficiency makes
        // hard hits unrealistically spiky.
        const double ratio = std::min (0.35, (w / kSpeedOfSound) / ks);
        return 0.12 * typical * basis->area() * ratio * ratio;
    };

    // ---- Statistical high band -------------------------------------------------------
    std::vector<double> highMic, highAir;
    {
        const double wTop = std::min (2.0 * kPi * 18000.0, 0.45 * 2.0 * kPi * sampleRate);
        const double wLow = basis->omega (nExact - 1) * 1.01;
        const int bands = std::clamp (spec.highBandModes, 0, kMaxHighModes);
        // Only parts whose modes really are dense up there need it (2-D parts).
        const double countAbove = basis->modeCountBelow (wTop) - basis->modeCountBelow (wLow);
        if (bands > 0 && wTop > wLow * 1.05 && countAbove > 2.0 * bands)
        {
            // How the treatments moved the upper exact modes, extrapolated statistically.
            double meanShift = 0.0, rmsShift = 0.0, lossRatio = 0.0;
            int count = 0;
            for (int k = nExact / 2; k < nExact; ++k)
            {
                const double sh = std::log (ci->omega[(size_t) k] / basis->omega (k));
                meanShift += sh;
                rmsShift += sh * sh;
                lossRatio += ci->sigma[(size_t) k] / materialDecayRate (ci->omega[(size_t) k], ctx.lossFactor, ctx.highFreqDamping);
                ++count;
            }
            meanShift /= count;
            rmsShift = std::sqrt (std::max (0.0, rmsShift / count - meanShift * meanShift));
            lossRatio /= count;

            std::uint64_t rs = (std::uint64_t) spec.treatments.seed * 0x9E3779B97F4A7C15ull + 0x1234567ull;
            auto rnd = [&rs] {
                rs ^= rs >> 12; rs ^= rs << 25; rs ^= rs >> 27;
                return (double) ((rs * 2685821657736338717ull) >> 11) * (1.0 / 9007199254740992.0);
            };
            for (int b = 0; b < bands; ++b)
            {
                const double lo = wLow * std::pow (wTop / wLow, (double) b / bands);
                const double hi = wLow * std::pow (wTop / wLow, (double) (b + 1) / bands);
                const double modesInBand = std::max (1.0, basis->modeCountBelow (hi) - basis->modeCountBelow (lo));
                const double g1 = rnd(), g2 = rnd(), g3 = rnd(), g4 = rnd();
                const double gauss = std::sqrt (-2.0 * std::log (std::max (1e-12, g1))) * std::cos (2.0 * kPi * g2);
                const double w = lo * std::pow (hi / lo, g3) * std::exp (meanShift + rmsShift * gauss);
                ci->omega.push_back (w);
                ci->sigma.push_back (std::min (0.9 * w, lossRatio * materialDecayRate (w, ctx.lossFactor, ctx.highFreqDamping)));
                ci->highStrike.push_back ((g4 < 0.5 ? -1.0 : 1.0) * typical * std::sqrt (modesInBand));
                highMic.push_back ((rnd() < 0.5 ? -1.0 : 1.0) * typical);
                highAir.push_back ((rnd() < 0.5 ? -1.0 : 1.0) * radiationFloor (w));
                ci->wavenumber.push_back (basis->wavenumber (w));
            }
        }
    }
    const int n = (int) ci->omega.size();
    ci->n = n;
    const std::size_t nz = ci->omega.size();

    const double nyquistGuard = 0.47 * 2.0 * kPi * sampleRate;

    // ---- Pickups ---------------------------------------------------------------------
    ci->micShape.assign (nz, 0.0);
    std::vector<double> scratch (nz);
    ci->shapeAt (ci->place (spec.mic.contact), ci->micShape.data(), scratch.data());
    for (int k = nExact; k < n; ++k)
        ci->micShape[(size_t) k] = highMic[(size_t) (k - nExact)];

    {
        std::vector<double> vol, dx, dy;
        basis->radiation (vol, dx, dy);
        auto treated = [&] (const std::vector<double>& src) {
            if (ci->X.empty()) return src;
            std::vector<double> out ((size_t) nExact, 0.0);
            for (int i = 0; i < nExact; ++i)
                for (int k = 0; k < nExact; ++k)
                    out[(size_t) k] += src[(size_t) i] * ci->X[(size_t) (i * nExact + k)];
            return out;
        };
        vol = treated (vol);
        dx = treated (dx);
        dy = treated (dy);
        ci->airShape.assign (nz, 0.0);
        for (int k = 0; k < nExact; ++k)
        {
            const double w = ci->omega[(size_t) k];
            // Monopole (net volume) + dipole (rocking modes, ~ k_air * moment) + edge floor.
            // The compact-dipole term k*D only holds while the part is small against the
            // wavelength; beyond that radiation saturates, so it is capped at a typical
            // mode's full-area strength.
            const double dip = std::min ((w / kSpeedOfSound) * std::sqrt (dx[(size_t) k] * dx[(size_t) k] + dy[(size_t) k] * dy[(size_t) k]),
                                         0.5 * typical * basis->area());
            const double sign = vol[(size_t) k] != 0.0 ? (vol[(size_t) k] > 0 ? 1.0 : -1.0) : hashSign ((std::uint64_t) k, 77);
            ci->airShape[(size_t) k] = vol[(size_t) k] + sign * dip + hashSign ((std::uint64_t) k, 991) * radiationFloor (w);
        }
        for (int k = nExact; k < n; ++k)
            ci->airShape[(size_t) k] = highAir[(size_t) (k - nExact)];
    }

    // Sound radiation takes energy out of every mode that moves air: sigma_rad =
    // rho w^2 g^2 / (8 pi c) for a source of strength g per unit modal velocity. It is what
    // stops a big thin plate's low modes ringing for minutes. (The membrane's radiation and
    // air loading are folded into its tuned loss factor until drum shells exist, so its M1
    // sound is unchanged.)
    if (spec.part.type != PartType::Membrane)
        for (int k = 0; k < n; ++k)
        {
            const double w = ci->omega[(size_t) k], g = ci->airShape[(size_t) k];
            const double rad = 1.2 * w * w * g * g / (8.0 * kPi * kSpeedOfSound);
            const double mountLoss = getMaterial (spec.part.material).impossible ? 0.0 : mountDecay (spec.part.type, spec.part.mount, w);
            ci->sigma[(size_t) k] = std::min (0.9 * w, ci->sigma[(size_t) k] + std::min (rad, 0.05 * w) + mountLoss);
        }

    // ---- Discretisation ---------------------------------------------------------------
    const double T = 1.0 / sampleRate;
    for (auto* v : { &ci->a11, &ci->a12, &ci->a21, &ci->a22, &ci->b1, &ci->b2 })
        v->assign (nz, 0.0);
    for (int k = 0; k < n; ++k)
    {
        const double w = ci->omega[(size_t) k];
        const double s = ci->sigma[(size_t) k];
        if (w >= nyquistGuard)
        {
            ci->a11[(size_t) k] = ci->a22[(size_t) k] = 0.0; // silent and inert
            continue;
        }
        const double wd = std::sqrt (std::max (w * w - s * s, 1e-9));
        const double e = std::exp (-s * T);
        const double c = std::cos (wd * T), sn = std::sin (wd * T);
        const double A11 = e * (c + s / wd * sn);
        const double A12 = e * sn / wd;
        const double A21 = -e * w * w / wd * sn;
        const double A22 = e * (c - s / wd * sn);
        ci->a11[(size_t) k] = A11;
        ci->a12[(size_t) k] = A12;
        ci->a21[(size_t) k] = A21;
        ci->a22[(size_t) k] = A22;
        ci->b1[(size_t) k] = (-2.0 * s * A12 - (A22 - 1.0)) / (w * w);
        ci->b2[(size_t) k] = A12;
    }

    for (int k = 0; k < n; ++k)
        if (ci->omega[(size_t) k] >= nyquistGuard)
            ci->micShape[(size_t) k] = ci->airShape[(size_t) k] = 0.0;

    calibrateOutputLevels (*ci);

    ci->solveMilliseconds = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    return ci;
}

} // namespace foundry
