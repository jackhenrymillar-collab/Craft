#include "ModalBank.h"

#include <cmath>

namespace foundry
{

void ModalBank::reset() noexcept
{
    q.fill (0.0);
    v.fill (0.0);
}

double ModalBank::predictFree (const double* shape) const noexcept
{
    const int n = inst->n;
    const double* a11 = inst->a11.data();
    const double* a12 = inst->a12.data();
    double s = 0.0;
    for (int k = 0; k < n; ++k)
        s += shape[k] * (a11[k] * q[(size_t) k] + a12[k] * v[(size_t) k]);
    return s;
}

double ModalBank::compliance (const double* shape) const noexcept
{
    const int n = inst->n;
    const double* b1 = inst->b1.data();
    double s = 0.0;
    for (int k = 0; k < n; ++k)
        s += shape[k] * shape[k] * b1[k];
    return s;
}

void ModalBank::step (const double* shape, double force) noexcept
{
    const int n = inst->n;
    const double *a11 = inst->a11.data(), *a12 = inst->a12.data(), *a21 = inst->a21.data(),
                 *a22 = inst->a22.data(), *b1 = inst->b1.data(), *b2 = inst->b2.data();
    double* qq = q.data();
    double* vv = v.data();
    for (int k = 0; k < n; ++k)
    {
        const double f = force * shape[k];
        const double qn = a11[k] * qq[k] + a12[k] * vv[k] + b1[k] * f;
        const double vn = a21[k] * qq[k] + a22[k] * vv[k] + b2[k] * f;
        qq[k] = qn;
        vv[k] = vn;
    }
}

void ModalBank::stepFree() noexcept
{
    const int n = inst->n;
    const double *a11 = inst->a11.data(), *a12 = inst->a12.data(), *a21 = inst->a21.data(), *a22 = inst->a22.data();
    double* qq = q.data();
    double* vv = v.data();
    for (int k = 0; k < n; ++k)
    {
        const double qn = a11[k] * qq[k] + a12[k] * vv[k];
        const double vn = a21[k] * qq[k] + a22[k] * vv[k];
        qq[k] = qn;
        vv[k] = vn;
    }
}

double ModalBank::displacementAt (const double* shape) const noexcept
{
    double s = 0.0;
    for (int k = 0; k < inst->n; ++k)
        s += shape[k] * q[(size_t) k];
    return s;
}

double ModalBank::velocityAt (const double* shape) const noexcept
{
    double s = 0.0;
    for (int k = 0; k < inst->n; ++k)
        s += shape[k] * v[(size_t) k];
    return s;
}

double ModalBank::energy() const noexcept
{
    if (inst == nullptr)
        return 0.0;
    double e = 0.0;
    for (int k = 0; k < inst->n; ++k)
    {
        const double w = inst->omega[(size_t) k];
        e += 0.5 * (v[(size_t) k] * v[(size_t) k] + w * w * q[(size_t) k] * q[(size_t) k]);
    }
    return e;
}

bool ModalBank::isFinite() const noexcept
{
    if (inst == nullptr)
        return true;
    for (int k = 0; k < inst->n; ++k)
        if (! std::isfinite (q[(size_t) k]) || ! std::isfinite (v[(size_t) k]))
            return false;
    return true;
}

void ModalBank::transferFrom (const ModalBank& other) noexcept
{
    reset();
    const auto* src = other.inst;
    const auto* dst = inst;
    if (src == nullptr || dst == nullptr)
        return;

    // Exact part: old treated modes -> untreated basis coordinates -> new treated modes.
    std::array<double, kMaxExactModes> uq {}, uv {};
    const int ns = src->nExact;
    if (src->X.empty())
    {
        for (int i = 0; i < ns; ++i) { uq[(size_t) i] = other.q[(size_t) i]; uv[(size_t) i] = other.v[(size_t) i]; }
    }
    else
    {
        for (int i = 0; i < ns; ++i)
        {
            const double* row = src->X.data() + (size_t) i * (size_t) ns;
            double sq = 0.0, sv = 0.0;
            for (int k = 0; k < ns; ++k) { sq += row[k] * other.q[(size_t) k]; sv += row[k] * other.v[(size_t) k]; }
            uq[(size_t) i] = sq;
            uv[(size_t) i] = sv;
        }
    }

    // Basis modes keep their order across recompiles, so map by index, rescaling so the
    // physical displacement field (in normalised radius) is unchanged.
    const int nd = dst->nExact;
    const int nm = ns < nd ? ns : nd;
    for (int i = 0; i < nm; ++i)
    {
        const double ratio = src->basis->mode (i).norm / dst->basis->mode (i).norm;
        uq[(size_t) i] *= ratio;
        uv[(size_t) i] *= ratio;
    }
    for (int i = nm; i < kMaxExactModes; ++i) { uq[(size_t) i] = 0.0; uv[(size_t) i] = 0.0; }

    if (dst->X.empty())
    {
        for (int i = 0; i < nd; ++i) { q[(size_t) i] = uq[(size_t) i]; v[(size_t) i] = uv[(size_t) i]; }
    }
    else
    {
        const double* Xi = dst->Xinv.data();
        for (int k = 0; k < nd; ++k)
        {
            const double* row = Xi + (size_t) k * (size_t) nd;
            double sq = 0.0, sv = 0.0;
            for (int i = 0; i < nd; ++i) { sq += row[i] * uq[(size_t) i]; sv += row[i] * uv[(size_t) i]; }
            q[(size_t) k] = sq;
            v[(size_t) k] = sv;
        }
    }

    // High band: carried over band by band.
    const int hs = src->n - src->nExact, hd = dst->n - dst->nExact;
    for (int b = 0; b < hs && b < hd; ++b)
    {
        q[(size_t) (nd + b)] = other.q[(size_t) (ns + b)];
        v[(size_t) (nd + b)] = other.v[(size_t) (ns + b)];
    }
}

} // namespace foundry
