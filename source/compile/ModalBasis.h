#pragma once

#include <vector>

namespace foundry
{

constexpr int kMaxExactModes = 128; // modes solved exactly per part
constexpr int kMaxHighModes = 128;  // statistical high band
constexpr int kMaxModes = kMaxExactModes + kMaxHighModes;

struct Vec2
{
    double x = 0.0, y = 0.0;
};

// Symmetric 2x2 tensor (in-plane stress, as a fraction of the base tension).
struct Sym2
{
    double xx = 0.0, xy = 0.0, yy = 0.0;
};

struct QuadPoint
{
    Vec2 p;
    double weight = 0.0; // area, m^2
};

// The untreated modes of one part, mass-normalised (integral of mass * phi_i * phi_j = delta_ij),
// so stiffness in modal coordinates is diag(omega^2). Each part type implements this;
// the treatment solver and the DSP only talk to this interface.
class ModalBasis
{
public:
    virtual ~ModalBasis() = default;

    [[nodiscard]] virtual int size() const noexcept = 0;
    [[nodiscard]] virtual double omega (int k) const noexcept = 0;  // rad/s
    [[nodiscard]] virtual bool contains (Vec2 p) const noexcept = 0;
    [[nodiscard]] virtual double characteristicSize() const noexcept = 0; // metres (radius)
    [[nodiscard]] virtual double area() const noexcept = 0;
    [[nodiscard]] virtual double massPerArea() const noexcept = 0;

    // Mode shapes at p (part-local metres, origin at centre). Real-time safe.
    virtual void shapes (Vec2 p, double* phi) const noexcept = 0;
    virtual void shapesAndGradients (Vec2 p, double* phi, double* gx, double* gy) const noexcept = 0;

    // Area quadrature over the whole part, roughly `resolution` points per side.
    virtual void quadrature (std::vector<QuadPoint>& out, int resolution) const = 0;

    // Local stiffness change at a point: isotropic fraction fIso of the base stiffness
    // plus an anisotropic stress tensor S (fraction of base tension). Writes the
    // per-mode-pair kernel value K_ij = w * (...) into a dense row-major matrix.
    virtual void addStiffness (const double* phi, const double* gx, const double* gy,
                               double weight, double fIso, Sym2 aniso, double* K) const noexcept = 0;

    // Weyl-style estimate of how many modes lie below omega (for the statistical high band).
    [[nodiscard]] virtual double modeCountBelow (double omega) const noexcept = 0;
    // Wavenumber (rad/m) of a mode at omega, for contact-patch filtering.
    [[nodiscard]] virtual double wavenumber (double omega) const noexcept = 0;

    // How this kind of part responds to a dent (fraction of local stiffness change per
    // unit dent intensity) and the extra loss factor a dent adds.
    [[nodiscard]] virtual double dentStiffness() const noexcept = 0;
    [[nodiscard]] virtual double dentLoss() const noexcept = 0;
};

} // namespace foundry
