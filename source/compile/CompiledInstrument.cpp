#include "CompiledInstrument.h"

#include "MembraneSolver.h"
#include "PlateSolver.h"
#include "ShellSolver.h"
#include "StripSolver.h"
#include "LinearAlgebra.h"
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
        for (int i = 0; i < nBasis; ++i)
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

void CompiledInstrument::attachShapeAt (Vec2 p, double* out, double* scratch) const noexcept
{
    for (int k = 0; k < n; ++k)
        out[k] = 0.0;
    if (attachBasis == nullptr)
        return;
    attachBasis->shapes (p, scratch);
    const int nb = attachBasis->size();
    for (int i = 0; i < nb; ++i)
    {
        const double s = scratch[i];
        if (s == 0.0) continue;
        const double* row = attachX.data() + (size_t) i * (size_t) nExact;
        for (int k = 0; k < nExact; ++k)
            out[k] += s * row[k];
    }
}

namespace
{
// One part's modes after treatments, with its pickups, radiation and losses.
struct PartModes
{
    std::shared_ptr<const ModalBasis> basis;
    std::vector<double> X, Xinv;           // treated = X^T basis (empty = identity)
    std::vector<double> omega, sigma;      // exact modes
    std::vector<double> air;               // radiation per unit modal velocity
    double typical = 0.0;                  // 1 / sqrt(total mass)
    double lossFactor = 0.0, highFreqDamping = 0.0;
};

// Treated shapes of the exact modes at p (no high band).
void treatedShapes (const PartModes& pm, Vec2 p, std::vector<double>& out)
{
    const int nb = pm.basis->size();
    std::vector<double> raw ((size_t) nb);
    pm.basis->shapes (p, raw.data());
    out.assign ((size_t) nb, 0.0);
    if (pm.X.empty())
    {
        out = raw;
        return;
    }
    for (int i = 0; i < nb; ++i)
        for (int k = 0; k < nb; ++k)
            out[(size_t) k] += raw[(size_t) i] * pm.X[(size_t) (i * nb + k)];
}

double radiationFloorFor (const ModalBasis& b, double typical, double w) noexcept
{
    // Edge/corner radiation for modes with little net volume, growing toward coincidence
    // (where the structural wavelength reaches the air wavelength). Capped well below 1: a
    // truncated set of modes radiating at full efficiency makes hard hits unrealistically spiky.
    const double ks = std::max (1e-6, b.wavenumber (w));
    const double ratio = std::min (0.35, (w / kSpeedOfSound) / ks);
    return 0.12 * typical * b.area() * ratio * ratio;
}

// Radiation per unit modal velocity of each exact mode: monopole (net volume) + dipole
// (rocking modes, ~ k_air * moment, capped once the part is no longer small against the
// wavelength) + edge floor.
std::vector<double> airShapes (const PartModes& pm)
{
    const auto& b = *pm.basis;
    const int nb = b.size();
    std::vector<double> vol, dx, dy;
    b.radiation (vol, dx, dy);
    auto treated = [&] (const std::vector<double>& src) {
        if (pm.X.empty()) return src;
        std::vector<double> out ((size_t) nb, 0.0);
        for (int i = 0; i < nb; ++i)
            for (int k = 0; k < nb; ++k)
                out[(size_t) k] += src[(size_t) i] * pm.X[(size_t) (i * nb + k)];
        return out;
    };
    vol = treated (vol);
    dx = treated (dx);
    dy = treated (dy);
    std::vector<double> air ((size_t) nb, 0.0);
    for (int k = 0; k < nb; ++k)
    {
        const double w = pm.omega[(size_t) k];
        const double dip = std::min ((w / kSpeedOfSound) * std::sqrt (dx[(size_t) k] * dx[(size_t) k] + dy[(size_t) k] * dy[(size_t) k]),
                                     0.5 * pm.typical * b.area());
        const double sign = vol[(size_t) k] != 0.0 ? (vol[(size_t) k] > 0 ? 1.0 : -1.0) : hashSign ((std::uint64_t) k, 77);
        air[(size_t) k] = vol[(size_t) k] + sign * dip + hashSign ((std::uint64_t) k, 991) * radiationFloorFor (b, pm.typical, w);
    }
    return air;
}

// Sound radiation takes energy out of every mode that moves air: sigma_rad =
// rho w^2 g^2 / (8 pi c) for a source of strength g per unit modal velocity. It is what stops
// a big thin plate's low modes ringing for minutes. Plus losses into the mount. (The
// membrane's radiation and air loading are folded into its tuned loss factor, so its M1
// sound is unchanged.)
double radiationAndMountLoss (const PartSpec& part, double w, double g) noexcept
{
    if (part.type == PartType::Membrane)
        return 0.0;
    const double rad = 1.2 * w * w * g * g / (8.0 * kPi * kSpeedOfSound);
    const double mountLoss = getMaterial (part.material).impossible ? 0.0 : mountDecay (part.type, part.mount, w);
    return std::min (rad, 0.05 * w) + mountLoss;
}

PartModes solvePart (const PartSpec& part, const Treatments& treatments, double tuneSemitones, double sampleRate,
                     int maxModes, TreatedModes* treatedOut)
{
    PartModes pm;
    const Material& mat = getMaterial (part.material);
    const AgeGlobals age = computeAgeGlobals (mat, treatments.age);
    pm.basis = makePartBasis (part, tuneSemitones, sampleRate, maxModes, age);
    const auto& b = *pm.basis;

    // A muffled drum head (pillow, tape): more loss everywhere, most at the top.
    const double muffle = part.type == PartType::Membrane ? part.control (2) : 0.0;
    TreatmentContext ctx;
    ctx.basis = pm.basis.get();
    ctx.material = &mat;
    ctx.lossFactor = mat.lossFactor * age.lossScale * (1.0 + 10.0 * muffle);
    ctx.highFreqDamping = part.highFreqDamping + age.highLossPerKHz + 1.5 * muffle;
    pm.lossFactor = ctx.lossFactor;
    pm.highFreqDamping = ctx.highFreqDamping;
    TreatedModes tm = solveTreatments (ctx, treatments);
    pm.X = std::move (tm.X);
    pm.Xinv = std::move (tm.Xinv);
    pm.omega = std::move (tm.omega);
    pm.sigma = std::move (tm.sigma);
    pm.typical = 1.0 / std::sqrt (b.massPerArea() * b.area());
    pm.air = airShapes (pm);
    for (int k = 0; k < b.size(); ++k)
    {
        const double w = pm.omega[(size_t) k];
        pm.sigma[(size_t) k] = std::min (0.9 * w, pm.sigma[(size_t) k] + radiationAndMountLoss (part, w, pm.air[(size_t) k]));
    }
    if (treatedOut != nullptr)
    {
        treatedOut->grit = tm.grit;
        treatedOut->features = std::move (tm.features);
    }
    return pm;
}

// Where a joined part is held: an end for long parts, near the rim for round ones.
Vec2 attachAnchor (const ModalBasis& b) noexcept
{
    switch (b.type())
    {
        case PartType::Bar:
        case PartType::Tube:
        case PartType::Spring:   return b.place (0.92, 0.0);
        case PartType::Membrane: return b.place (0.7, 0.0);
        case PartType::Plate:
        case PartType::Box:      return b.place (0.85, 0.785);
        case PartType::Shell:    return b.place (0.85, 0.0);
        case PartType::Count:    break;
    }
    return b.place (0.8, 0.0);
}

// Joint spring (N/m) and damper (N s/m).
void jointConstants (const AttachSpec& a, double& k, double& c, double massA, double massB) noexcept
{
    const double u = std::clamp (a.stiffness, 0.0, 1.0);
    const double mEff = 1.0 / (1.0 / std::max (massA, 1e-4) + 1.0 / std::max (massB, 1e-4));
    if (a.joint == JointType::Bolt)
    {
        k = 2e5 * std::pow (100.0, u);            // 2e5 .. 2e7 N/m
        c = 0.002 * 2.0 * std::sqrt (k * mEff);   // nearly lossless
    }
    else
    {
        k = 3e2 * std::pow (300.0, u);            // 300 .. 9e4 N/m
        c = 0.3 * 2.0 * std::sqrt (k * mEff);     // rubber: heavily damped
    }
}

std::uint64_t mix (std::uint64_t z) noexcept
{
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// Lay out the rattles and loose objects on the main part.
std::vector<ContactSite> layoutContacts (const InstrumentSpec& spec, const ModalBasis& b)
{
    std::vector<ContactSite> out;
    auto law = [] (ContactSite& c, double f1mm, double alpha, double lambda) {
        c.alpha = alpha;
        c.K = f1mm / std::pow (1e-3, alpha);
        c.lambda = lambda;
    };
    const Vec2 he = b.halfExtents();

    if (spec.rattle.type == RattleType::Snare)
    {
        // Ten groups of wires across the middle of the underside, each a light mass on the
        // wire's tension spring. Rattle 0: pressed 0.25 mm into the part (crisp, short);
        // Rattle 1: hanging 0.35 mm clear (loose, long buzz).
        constexpr int wires = 10;
        for (int j = 0; j < wires; ++j)
        {
            const double u = -0.72 + 1.44 * j / (wires - 1);
            Vec2 p = b.place (std::abs (u), u >= 0.0 ? 0.0 : kPi);
            p.y += ((j % 2) ? 1.0 : -1.0) * 0.06 * he.y;
            if (! b.contains (p)) continue;
            ContactSite c;
            c.kind = ContactSite::Kind::Wire;
            c.p = p;
            c.side = -1.0;
            c.mass = 6e-4;
            const double fw = 140.0 + 25.0 * (j % 3);  // slightly different wire tensions
            c.tetherK = c.mass * std::pow (2.0 * kPi * fw, 2.0);
            c.tetherDamping = 2.0 * 0.04 * std::sqrt (c.tetherK * c.mass);
            law (c, 1000.0, 1.5, 1.0);
            c.gapTight = -2.5e-4;
            c.gapLoose = 3.5e-4;
            out.push_back (c);
        }
    }
    else if (spec.rattle.type == RattleType::Sizzle)
    {
        // Six rivets sitting loosely in holes near the edge.
        for (int j = 0; j < 6; ++j)
        {
            ContactSite c;
            c.kind = ContactSite::Kind::Rivet;
            c.p = b.place (0.82, 2.0 * kPi * j / 6.0 + 0.3);
            if (! b.contains (c.p)) continue;
            c.side = 1.0;
            c.mass = 1.5e-3;
            c.gravity = 9.81;
            c.tetherK = 25.0;
            c.tetherDamping = 0.004;
            law (c, 3000.0, 1.5, 0.05);
            c.gapTight = 0.0;
            c.gapLoose = 8e-4;
            out.push_back (c);
        }
    }

    const auto& L = spec.loose;
    if (L.type != LooseType::None && L.amount > 0.0)
    {
        int maxCount = 0;
        ContactSite proto;
        proto.kind = ContactSite::Kind::Grain;
        proto.side = 1.0;
        proto.gravity = 9.81;
        switch (L.type)
        {
            case LooseType::Rice:  maxCount = 16; proto.mass = 1e-4; law (proto, 150.0, 1.5, 1.5); break;   // clumps of a few grains
            case LooseType::Beads: maxCount = 12; proto.mass = 5e-4; law (proto, 4000.0, 1.5, 0.05); break;  // glass: lively
            case LooseType::Sand:  maxCount = 20; proto.mass = 4e-5; law (proto, 15.0, 1.5, 6.0); break;     // soft, dead
            case LooseType::Chain:
                maxCount = 10; proto.mass = 2e-3; law (proto, 1500.0, 1.5, 0.2);
                proto.kind = ContactSite::Kind::Link;    // links are held together
                proto.tetherK = 30.0;
                proto.tetherDamping = 0.02;
                break;
            case LooseType::None:
            case LooseType::Count: break;
        }
        const int count = std::clamp ((int) std::lround (std::clamp (L.amount, 0.0, 1.0) * maxCount), 1, maxCount);
        std::uint64_t seed = mix ((std::uint64_t) spec.treatments.seed * 7919ull + (std::uint64_t) L.type);
        auto rnd = [&seed] { seed = mix (seed + 0x9E3779B97F4A7C15ull); return (double) (seed >> 11) * (1.0 / 9007199254740992.0); };
        for (int j = 0; j < count && (int) out.size() < kMaxContacts; ++j)
        {
            ContactSite c = proto;
            if (L.type == LooseType::Chain)
            {
                const double u = -0.7 + 1.4 * j / std::max (1, count - 1);
                c.p = b.place (std::abs (u), u >= 0.0 ? 0.5 : 0.5 + kPi);
            }
            else
                c.p = b.interiorPoint (rnd(), rnd(), 0.12);
            if (! b.contains (c.p)) continue;
            out.push_back (c);
        }
    }
    if ((int) out.size() > kMaxContacts)
        out.resize ((size_t) kMaxContacts);
    return out;
}
} // namespace

std::unique_ptr<CompiledInstrument> compileInstrument (const InstrumentSpec& specIn, double hostRate,
                                                       std::uint64_t version)
{
    const auto t0 = std::chrono::steady_clock::now();
    auto ci = std::make_unique<CompiledInstrument>();
    ci->spec = specIn;
    ci->spec.treatments.clampToRange();
    ci->spec.part.material = resolveMaterial (ci->spec.part);
    ci->spec.part.mount = std::clamp (ci->spec.part.mount, 0, partInfo (ci->spec.part.type).mountCount - 1);
    ci->spec.attach.part.material = resolveMaterial (ci->spec.attach.part);
    ci->spec.attach.part.mount = std::clamp (ci->spec.attach.part.mount, 0, partInfo (ci->spec.attach.part.type).mountCount - 1);
    const auto& spec = ci->spec;
    ci->oversample = spec.oversample ? 2 : 1;
    const double sampleRate = hostRate * ci->oversample;
    ci->sampleRate = sampleRate;
    ci->version = version;

    // ---- Main part ---------------------------------------------------------------------
    TreatedModes extras;
    PartModes A = solvePart (spec.part, spec.treatments, spec.tuneSemitones, sampleRate, spec.quality, &extras);
    const auto& basis = *A.basis;
    ci->basis = A.basis;
    ci->nBasis = basis.size();
    ci->grit = extras.grit;
    ci->features = std::move (extras.features);
    const int nA = basis.size();

    // ---- Statistical high band ---------------------------------------------------------
    std::vector<double> hOmega, hSigma, hStrike, hMic, hAir, hWave;
    {
        const double wTop = std::min (2.0 * kPi * 18000.0, 0.45 * 2.0 * kPi * hostRate);
        const double wLow = basis.omega (nA - 1) * 1.01;
        const int bands = std::clamp (spec.highBandModes, 0, kMaxHighModes);
        // Only parts whose modes really are dense up there need it (2-D parts).
        const double countAbove = basis.modeCountBelow (wTop) - basis.modeCountBelow (wLow);
        if (bands > 0 && wTop > wLow * 1.05 && countAbove > 2.0 * bands)
        {
            // How the treatments moved the upper exact modes, extrapolated statistically.
            // (Uses the material-only decay of the exact modes, before radiation and mounts.)
            double meanShift = 0.0, rmsShift = 0.0, lossRatio = 0.0;
            int count = 0;
            for (int k = nA / 2; k < nA; ++k)
            {
                const double w = A.omega[(size_t) k];
                const double sh = std::log (w / basis.omega (k));
                meanShift += sh;
                rmsShift += sh * sh;
                const double extra = radiationAndMountLoss (spec.part, w, A.air[(size_t) k]);
                lossRatio += std::max (0.0, A.sigma[(size_t) k] - extra) / materialDecayRate (w, A.lossFactor, A.highFreqDamping);
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
                const double modesInBand = std::max (1.0, basis.modeCountBelow (hi) - basis.modeCountBelow (lo));
                const double g1 = rnd(), g2 = rnd(), g3 = rnd(), g4 = rnd();
                const double gauss = std::sqrt (-2.0 * std::log (std::max (1e-12, g1))) * std::cos (2.0 * kPi * g2);
                const double w = lo * std::pow (hi / lo, g3) * std::exp (meanShift + rmsShift * gauss);
                hOmega.push_back (w);
                hSigma.push_back (std::min (0.9 * w, lossRatio * materialDecayRate (w, A.lossFactor, A.highFreqDamping)));
                hStrike.push_back ((g4 < 0.5 ? -1.0 : 1.0) * A.typical * std::sqrt (modesInBand));
                hMic.push_back ((rnd() < 0.5 ? -1.0 : 1.0) * A.typical);
                hAir.push_back ((rnd() < 0.5 ? -1.0 : 1.0) * radiationFloorFor (basis, A.typical, w));
                hWave.push_back (basis.wavenumber (w));
            }
            for (size_t b = 0; b < hOmega.size(); ++b)
                hSigma[b] = std::min (0.9 * hOmega[b], hSigma[b] + radiationAndMountLoss (spec.part, hOmega[b], hAir[b]));
        }
    }

    // Contact mic on the main part (exact modes).
    std::vector<double> micA;
    treatedShapes (A, ci->place (spec.mic.contact), micA);

    // ---- Joined part --------------------------------------------------------------------
    // The joint is a spring (and damper) between a point on each part: K = diag(w^2) + k g g^T
    // with g = [phi_A(a); -phi_B(b)], re-solved into the modes of the combined object. Energy
    // passes between the parts through it: beating, swells and a body under the strike.
    std::vector<double> omegaC = A.omega, sigmaC = A.sigma, micC = micA, airC = A.air;
    std::vector<double> XC = A.X;
    int nC = nA;
    if (spec.attach.enabled)
    {
        PartModes B = solvePart (spec.attach.part, Treatments {}, spec.tuneSemitones, sampleRate, kMaxAttachModes, nullptr);
        const int nB = std::min (B.basis->size(), kMaxAttachModes);
        nC = nA + nB;
        ci->attachBasis = B.basis;
        ci->jointMain = basis.place (std::clamp (spec.attach.place, 0.0, 0.95), 0.6);
        ci->jointAttach = attachAnchor (*B.basis);

        std::vector<double> gA, gB;
        treatedShapes (A, ci->jointMain, gA);
        treatedShapes (B, ci->jointAttach, gB);
        double sA = 0.0, sB = 0.0;
        for (double x : gA) sA += x * x;
        for (int k = 0; k < nB; ++k) sB += gB[(size_t) k] * gB[(size_t) k];
        double kJ = 0.0, cJ = 0.0;
        // Driving-point masses 1 / sum(phi^2) set the joint's damping scale.
        jointConstants (spec.attach, kJ, cJ, 1.0 / std::max (sA, 1e-12), 1.0 / std::max (sB, 1e-12));

        std::vector<double> g ((size_t) nC);
        for (int k = 0; k < nA; ++k) g[(size_t) k] = gA[(size_t) k];
        for (int k = 0; k < nB; ++k) g[(size_t) (nA + k)] = -gB[(size_t) k];
        std::vector<double> K ((size_t) nC * (size_t) nC, 0.0), w0 ((size_t) nC), s0 ((size_t) nC);
        for (int k = 0; k < nA; ++k) { w0[(size_t) k] = A.omega[(size_t) k]; s0[(size_t) k] = A.sigma[(size_t) k]; }
        for (int k = 0; k < nB; ++k) { w0[(size_t) (nA + k)] = B.omega[(size_t) k]; s0[(size_t) (nA + k)] = B.sigma[(size_t) k]; }
        for (int i = 0; i < nC; ++i)
        {
            K[(size_t) (i * nC + i)] = w0[(size_t) i] * w0[(size_t) i];
            for (int j = 0; j < nC; ++j)
                K[(size_t) (i * nC + j)] += kJ * g[(size_t) i] * g[(size_t) j];
        }
        std::vector<double> lam, V;
        linalg::symmetricEigen (K, nC, lam, V);

        omegaC.assign ((size_t) nC, 0.0);
        sigmaC.assign ((size_t) nC, 0.0);
        micC.assign ((size_t) nC, 0.0);
        airC.assign ((size_t) nC, 0.0);
        for (int k = 0; k < nC; ++k)
        {
            const double w = std::sqrt (std::max (lam[(size_t) k], 1.0));
            // Proportional part of the damping keeps each old mode's share; the joint's own
            // damper acts on the stretch of the joint in this mode.
            double s = 0.0, stretch = 0.0, mic = 0.0, air = 0.0;
            for (int i = 0; i < nC; ++i)
            {
                const double vik = V[(size_t) (i * nC + k)];
                s += vik * vik * s0[(size_t) i];
                stretch += g[(size_t) i] * vik;
                if (i < nA) mic += micA[(size_t) i] * vik;
                air += (i < nA ? A.air[(size_t) i] : B.air[(size_t) (i - nA)]) * vik;
            }
            s += 0.5 * cJ * stretch * stretch;
            omegaC[(size_t) k] = w;
            sigmaC[(size_t) k] = std::min (0.9 * w, s);
            micC[(size_t) k] = mic;
            airC[(size_t) k] = air;
        }
        // Final shapes: main part = X_A * V_top, joined part = V_bottom.
        XC.assign ((size_t) nA * (size_t) nC, 0.0);
        for (int j = 0; j < nA; ++j)
            for (int i = 0; i < nA; ++i)
            {
                const double xji = A.X.empty() ? (i == j ? 1.0 : 0.0) : A.X[(size_t) (j * nA + i)];
                if (xji == 0.0) continue;
                for (int k = 0; k < nC; ++k)
                    XC[(size_t) (j * nC + k)] += xji * V[(size_t) (i * nC + k)];
            }
        ci->attachX.assign ((size_t) B.basis->size() * (size_t) nC, 0.0);
        for (int j = 0; j < nB; ++j)
            for (int k = 0; k < nC; ++k)
                ci->attachX[(size_t) (j * nC + k)] = V[(size_t) ((nA + j) * nC + k)];
    }
    else
        ci->Xinv = std::move (A.Xinv);

    // ---- Assemble: exact (combined) modes, then the high band ----------------------------
    ci->nExact = nC;
    ci->X = std::move (XC);
    ci->omega = omegaC;
    ci->sigma = sigmaC;
    ci->micShape = micC;
    ci->airShape = airC;
    ci->highStrike.assign ((size_t) nC, 0.0);
    ci->wavenumber.resize ((size_t) nC);
    for (int k = 0; k < nC; ++k)
        ci->wavenumber[(size_t) k] = basis.wavenumber (ci->omega[(size_t) k]);
    for (size_t b = 0; b < hOmega.size(); ++b)
    {
        ci->omega.push_back (hOmega[b]);
        ci->sigma.push_back (hSigma[b]);
        ci->highStrike.push_back (hStrike[b]);
        ci->micShape.push_back (hMic[b]);
        ci->airShape.push_back (hAir[b]);
        ci->wavenumber.push_back (hWave[b]);
    }
    const int n = (int) ci->omega.size();
    ci->n = n;
    const std::size_t nz = ci->omega.size();

    // ---- Discretisation ---------------------------------------------------------------
    // Exact discretisation of each damped mode with a zero-order-hold force.
    const double nyquistGuard = 0.47 * 2.0 * kPi * sampleRate;
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

    // ---- Rattles and loose objects ------------------------------------------------------
    ci->contacts = layoutContacts (spec, basis);
    ci->contactModes = n; // solved against every mode, so the clicks' energy is accounted for
    if (! ci->contacts.empty())
    {
        // Shapes over every mode, the high band included: that's where a snare's buzz and the
        // rattle of rice live. The high band stands in for many modes, so each site gets its
        // own random signs there (they add up incoherently across sites).
        const int m = ci->contactModes;
        ci->contactShapes.assign (ci->contacts.size() * (size_t) n, 0.0);
        std::vector<double> sh (nz), scratch (std::max<std::size_t> (nz, (size_t) kMaxModes));
        for (size_t j = 0; j < ci->contacts.size(); ++j)
        {
            auto& c = ci->contacts[j];
            ci->shapeAt (c.p, sh.data(), scratch.data());
            double* row = ci->contactShapes.data() + j * (size_t) n;
            double comp = 0.0;
            for (int k = 0; k < n; ++k)
            {
                double v = sh[(size_t) k];
                if (k >= ci->nExact)
                    v *= hashSign ((std::uint64_t) j * 1009ull + (std::uint64_t) k, 0x5eedull);
                // Not the top octave: a force that changes every sample can feed modes that
                // turn half a cycle per step (and a buzz up there isn't heard as such).
                if (ci->b1[(size_t) k] == 0.0 || ci->omega[(size_t) k] > 0.5 * kPi * sampleRate)
                    v = 0.0;
                row[k] = v;
                if (k < m)
                    comp += v * v * ci->b1[(size_t) k];
            }
            c.compliance = comp;
        }
    }

    calibrateOutputLevels (*ci);

    ci->solveMilliseconds = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    return ci;
}

} // namespace foundry
