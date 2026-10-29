#pragma once

#include "model/PartCatalogue.h"

#include <vector>

namespace foundry
{

constexpr int kMaxExactModes = 128;  // modes solved exactly per part
constexpr int kMaxAttachModes = 48;  // modes of a second, joined part
constexpr int kMaxHighModes = 128;   // statistical high band
constexpr int kMaxCombinedExact = kMaxExactModes + kMaxAttachModes;
constexpr int kMaxModes = kMaxCombinedExact + kMaxHighModes;
constexpr int kMaxContacts = 48;     // snare wires + rivets + loose objects

struct Vec2
{
    double x = 0.0, y = 0.0;
};

// Symmetric 2x2 tensor (in-plane stress, as a fraction of the reference tension).
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
// the treatment solver, the compile step and the DSP only talk to this interface.
//
// Coordinates are part-local metres seen from above, origin at the centre. Round parts
// (membrane, shell) are discs of radius halfExtents().x; everything else is a rectangle
// of +-halfExtents().
class ModalBasis
{
public:
    virtual ~ModalBasis() = default;

    [[nodiscard]] virtual PartType type() const noexcept = 0;
    [[nodiscard]] virtual int size() const noexcept = 0;
    [[nodiscard]] virtual double omega (int k) const noexcept = 0;  // rad/s
    [[nodiscard]] virtual Vec2 halfExtents() const noexcept = 0;
    [[nodiscard]] virtual bool isRound() const noexcept { return false; }
    [[nodiscard]] virtual bool contains (Vec2 p) const noexcept;
    [[nodiscard]] virtual double area() const noexcept;
    [[nodiscard]] virtual double massPerArea() const noexcept = 0;
    // Mass per area at a point, for parts with a thickness profile (undercut bars).
    [[nodiscard]] virtual double massPerAreaAt (Vec2) const noexcept { return massPerArea(); }

    // Radius of a disc with the same area (scales treatment features).
    [[nodiscard]] double characteristicSize() const noexcept;
    [[nodiscard]] double minHalfExtent() const noexcept;

    // Mode shapes at p. Real-time safe.
    virtual void shapes (Vec2 p, double* phi) const noexcept = 0;
    // Default: forward differences of shapes().
    virtual void shapesAndGradients (Vec2 p, double* phi, double* gx, double* gy) const noexcept;

    // Area quadrature over the whole part, about resolution^2 points in total.
    virtual void quadrature (std::vector<QuadPoint>& out, int resolution) const;

    // Placement: r = 0 centre .. 1 edge, theta in radians (an ellipse on rectangular parts).
    [[nodiscard]] virtual Vec2 place (double r, double theta) const noexcept;
    // A random interior point from two uniforms in [0, 1), within `margin` of the size.
    [[nodiscard]] virtual Vec2 interiorPoint (double u1, double u2, double margin) const noexcept;
    // A point just inside the edge (u in [0, 1) runs once round it) and the inward direction.
    virtual void edgePoint (double u, Vec2& p, double& inwardAngle) const noexcept;

    // Local stiffness change at a point: isotropic fraction fIso of the local stiffness plus an
    // anisotropic in-plane stress S (fraction of referenceTension()). Adds the per-mode-pair
    // kernel K_ij (upper triangle, dense row-major). The default treats local strain energy as
    // proportional to local kinetic energy at each mode's frequency (exact for sinusoidal
    // bending modes away from free edges) and adds the stress term from the gradients.
    virtual void addStiffness (const double* phi, const double* gx, const double* gy,
                               double weight, double fIso, Sym2 aniso, double* K) const noexcept;
    // The in-plane tension a stress fraction of 1 stands for. For a membrane it is the real
    // tension; for stiff parts it is the tension that would put a membrane of the same size
    // and mass at the same fundamental, so Twist feels comparable on every part.
    [[nodiscard]] virtual double referenceTension() const noexcept;

    // Estimate of how many modes lie below omega (for the statistical high band).
    [[nodiscard]] virtual double modeCountBelow (double omega) const noexcept = 0;
    // Structural wavenumber (rad/m) at omega, for contact-patch filtering.
    [[nodiscard]] virtual double wavenumber (double omega) const noexcept = 0;

    // How this kind of part responds to a dent (fraction of local stiffness change per unit
    // intensity) and the extra loss factor a dent adds.
    [[nodiscard]] virtual double dentStiffness() const noexcept = 0;
    [[nodiscard]] virtual double dentLoss() const noexcept = 0;

    // 1 / sqrt(modal mass) of basis mode k in the shape's own units: carrying state across a
    // recompile rescales by the ratio so the physical motion is unchanged.
    [[nodiscard]] virtual double normalisation (int k) const noexcept;

    // Sound radiation per unit modal velocity: net volume (m^3 per unit coordinate) and dipole
    // moments. The default integrates the shapes; parts with extra radiators (a box's port,
    // a tube's open ends) add theirs.
    virtual void radiation (std::vector<double>& volume, std::vector<double>& dipoleX,
                            std::vector<double>& dipoleY) const;

    // Extra loss on individual modes (mounts, air columns), added to the material loss.
    [[nodiscard]] virtual double extraDecay (int) const noexcept { return 0.0; }
    // Material-loss weight of a mode (0 for air-column modes, which don't lose energy in the walls).
    [[nodiscard]] virtual double materialLossWeight (int) const noexcept { return 1.0; }
};

} // namespace foundry
