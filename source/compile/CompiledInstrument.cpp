#include "CompiledInstrument.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <numbers>

namespace foundry
{

double tuneRatio (double semitones) noexcept
{
    return std::pow (2.0, semitones / 12.0);
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
    const auto& spec = ci->spec;
    ci->sampleRate = sampleRate;
    ci->version = version;

    const Material& mat = getMaterial (spec.membrane.material);
    const AgeGlobals age = computeAgeGlobals (mat, spec.treatments.age);

    // Size/Tune scales the whole object: a bigger drum is a lower drum.
    const double ratio = tuneRatio (spec.tuneSemitones);
    MembraneBasis::Params bp;
    bp.radius = spec.membrane.radius / ratio;
    bp.tension = spec.membrane.tension * age.stiffnessScale;
    bp.arealDensity = (mat.arealDensity > 0 ? mat.arealDensity : 0.26) * age.massScale;
    bp.maxFrequency = 0.45 * sampleRate;
    bp.maxModes = spec.quality;
    auto basis = std::make_shared<MembraneBasis> (bp);
    ci->basis = basis;
    ci->radius = bp.radius;
    const int nExact = basis->size();
    ci->nExact = nExact;

    TreatmentContext ctx;
    ctx.basis = basis.get();
    ctx.material = &mat;
    ctx.lossFactor = mat.lossFactor * age.lossScale;
    ctx.highFreqDamping = spec.membrane.highFreqDamping + age.highLossPerKHz;
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

    // ---- Statistical high band -------------------------------------------------------
    std::vector<double> highMic;
    {
        const double wTop = std::min (2.0 * std::numbers::pi * 18000.0, 0.45 * 2.0 * std::numbers::pi * sampleRate);
        const double wLow = basis->omega (nExact - 1) * 1.01;
        const int bands = std::clamp (spec.highBandModes, 0, kMaxHighModes);
        if (bands > 0 && wTop > wLow * 1.05)
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
            const double typical = 1.0 / std::sqrt (basis->massPerArea() * basis->area());
            for (int b = 0; b < bands; ++b)
            {
                const double lo = wLow * std::pow (wTop / wLow, (double) b / bands);
                const double hi = wLow * std::pow (wTop / wLow, (double) (b + 1) / bands);
                const double modesInBand = std::max (1.0, basis->modeCountBelow (hi) - basis->modeCountBelow (lo));
                const double g1 = rnd(), g2 = rnd(), g3 = rnd(), g4 = rnd();
                const double gauss = std::sqrt (-2.0 * std::log (std::max (1e-12, g1))) * std::cos (2.0 * std::numbers::pi * g2);
                const double w = lo * std::pow (hi / lo, g3) * std::exp (meanShift + rmsShift * gauss);
                ci->omega.push_back (w);
                ci->sigma.push_back (std::min (0.9 * w, lossRatio * materialDecayRate (w, ctx.lossFactor, ctx.highFreqDamping)));
                ci->highStrike.push_back ((g4 < 0.5 ? -1.0 : 1.0) * typical * std::sqrt (modesInBand));
                highMic.push_back ((rnd() < 0.5 ? -1.0 : 1.0) * typical);
                ci->wavenumber.push_back (basis->wavenumber (w));
            }
        }
    }
    const int n = (int) ci->omega.size();
    ci->n = n;
    const std::size_t nz = ci->omega.size();

    const double T = 1.0 / sampleRate;
    const double nyquistGuard = 0.47 * 2.0 * std::numbers::pi * sampleRate;
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

    ci->micShape.assign (nz, 0.0);
    std::vector<double> scratch (nz);
    ci->shapeAt (ci->polar (spec.contactMic), ci->micShape.data(), scratch.data());
    for (int k = nExact; k < n; ++k)
        ci->micShape[(size_t) k] = highMic[(size_t) (k - nExact)];
    for (int k = 0; k < n; ++k)
        if (ci->omega[(size_t) k] >= nyquistGuard)
            ci->micShape[(size_t) k] = 0.0;

    ci->solveMilliseconds = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    return ci;
}

} // namespace foundry
