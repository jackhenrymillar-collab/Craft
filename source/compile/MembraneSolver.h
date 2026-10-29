#pragma once

#include "ModalBasis.h"
#include "model/Instrument.h"

#include <memory>

namespace foundry
{

struct MembraneMode
{
    int m = 0;            // angular order (nodal diameters)
    int n = 1;            // radial order (nodal circles incl. rim)
    bool isSine = false;  // sin(m theta) partner of a degenerate pair
    double zero = 0.0;    // Bessel zero j_mn
    double omega = 0.0;   // rad/s
    double norm = 0.0;    // 1 / sqrt(modal mass)
};

// Circular membrane, clamped at the rim, ideal (no air loading, no bending stiffness).
// f_mn = j_mn / (2 pi a) * sqrt(T / sigma); shape J_m(j_mn r / a) * {cos, sin}(m theta).
class MembraneBasis final : public ModalBasis
{
public:
    struct Params
    {
        double radius = 0.178;
        double tension = 1100.0;
        double arealDensity = 0.26;
        double maxFrequency = 20000.0;
        int maxModes = 96;
    };

    explicit MembraneBasis (const Params& p);

    PartType type() const noexcept override { return PartType::Membrane; }
    int size() const noexcept override { return static_cast<int> (modes.size()); }
    double omega (int k) const noexcept override { return modes[(size_t) k].omega; }
    Vec2 halfExtents() const noexcept override { return { params.radius, params.radius }; }
    bool isRound() const noexcept override { return true; }
    bool contains (Vec2 p) const noexcept override;
    double area() const noexcept override;
    double massPerArea() const noexcept override { return params.arealDensity; }
    double referenceTension() const noexcept override { return params.tension; }
    double normalisation (int k) const noexcept override { return modes[(size_t) k].norm; }

    void shapes (Vec2 p, double* phi) const noexcept override;
    void shapesAndGradients (Vec2 p, double* phi, double* gx, double* gy) const noexcept override;
    void addStiffness (const double* phi, const double* gx, const double* gy,
                       double weight, double fIso, Sym2 aniso, double* K) const noexcept override;
    // Exact: only m = 0 modes have net volume, only m = 1 modes a dipole moment.
    void radiation (std::vector<double>& volume, std::vector<double>& dipoleX, std::vector<double>& dipoleY) const override;

    double modeCountBelow (double w) const noexcept override;
    double wavenumber (double w) const noexcept override;

    // A dent in a tensioned head is a stretched, slack spot: local tension drops and it flaps.
    double dentStiffness() const noexcept override { return -0.85; }
    double dentLoss() const noexcept override { return 0.08; }

    const MembraneMode& mode (int k) const noexcept { return modes[(size_t) k]; }
    const Params& getParams() const noexcept { return params; }

    static Vec2 polarToLocal (double rFraction, double theta, double radius) noexcept;

    static constexpr int kTable = 512;
    struct Radial
    {
        // R(rho), dR/drho and R(rho)/rho on a uniform grid rho in [0, 1]. These depend only
        // on (m, n), not on size or tension, so they're computed once and shared.
        std::vector<double> R, dR, Rr;
    };
    static std::shared_ptr<const Radial> radialTable (int m, int n);

private:

    void evalRadial (int k, double rho, double& R, double& dR, double& Rr) const noexcept;

    Params params;
    std::vector<MembraneMode> modes;
    std::vector<std::shared_ptr<const Radial>> radial; // one per distinct (m, n), shared by cos/sin partners
    std::vector<int> radialIndex;
};

// A drum: the struck (batter) head on a closed shell, with a front (resonant) head that may
// have a port hole. The air inside is a spring, rho c^2 / V per unit volume squeezed, that
// couples every head mode with net volume (the m = 0 family) on both heads and the air plug
// in the port: K = diag(omega^2, 0) + (rho c^2 / V) g g^T over [batter modes, front-head
// m = 0 modes, port], with the outside air's mass on each head (M = I + mu g g^T per head),
// re-solved into coupled modes like the Box. The drum's net breathing radiates (a monopole),
// which is most of a kick's loss. This is what gives a kick or
// floor tom its deep, pushed fundamental: the air stiffens the batter's (0,1) mode, the two
// heads beat against each other through the air, and the port adds a low Helmholtz mode.
// Positive displacement is into the shell on both heads.
class DrumShellBasis final : public ModalBasis
{
public:
    struct Params
    {
        MembraneBasis::Params batter;
        double depth = 0.4;            // m
        double portRadius = 0.05;      // m (0 = sealed front head)
        double frontTension = 0.85;    // front head tension, fraction of the batter's
        int frontModes = 6;            // m = 0 modes of the front head
        double portQ = 6.0;            // the port's own losses (radiation and friction)
    };

    explicit DrumShellBasis (const Params& p);

    PartType type() const noexcept override { return PartType::Membrane; }
    int size() const noexcept override { return n; }
    double omega (int k) const noexcept override { return omegas[(size_t) k]; }
    Vec2 halfExtents() const noexcept override { return batter->halfExtents(); }
    bool isRound() const noexcept override { return true; }
    bool contains (Vec2 p) const noexcept override { return batter->contains (p); }
    double area() const noexcept override { return batter->area(); }
    double massPerArea() const noexcept override { return batter->massPerArea(); }
    double referenceTension() const noexcept override { return batter->referenceTension(); }

    void shapes (Vec2 p, double* phi) const noexcept override;
    void shapesAndGradients (Vec2 p, double* phi, double* gx, double* gy) const noexcept override;
    void addStiffness (const double* phi, const double* gx, const double* gy,
                       double weight, double fIso, Sym2 aniso, double* K) const noexcept override;

    double modeCountBelow (double w) const noexcept override { return batter->modeCountBelow (w); }
    double wavenumber (double w) const noexcept override { return batter->wavenumber (w); }
    double dentStiffness() const noexcept override { return batter->dentStiffness(); }
    double dentLoss() const noexcept override { return batter->dentLoss(); }
    double extraDecay (int k) const noexcept override { return extra[(size_t) k]; }
    double materialLossWeight (int k) const noexcept override { return 1.0 - portShare[(size_t) k]; }
    // The drum's net breathing (both heads and the port) plus the batter's dipoles.
    void radiation (std::vector<double>& volume, std::vector<double>& dipoleX, std::vector<double>& dipoleY) const override;

    // Bare Helmholtz frequency of the shell and port (rigid heads), Hz; 0 when sealed.
    double helmholtzHz() const noexcept { return helmholtz; }
    // The batter's (0,1) frequency without the shell, Hz.
    double bareFundamentalHz() const noexcept { return batter->omega (0) / (2.0 * 3.141592653589793); }
    double portParticipation (int k) const noexcept { return portShare[(size_t) k]; }
    const Params& getParams() const noexcept { return params; }


private:
    Params params;
    std::shared_ptr<MembraneBasis> batter;
    int nb = 0, nr = 0, n = 0;
    bool hasPort = false;
    std::vector<double> omegas, X, portShare, extra, netVolume, g, frontVolume;
    std::vector<double> batterVolume, batterDx, batterDy;
    double helmholtz = 0.0;
};

// Table of Bessel zeros j_mn, computed once and cached (thread-safe).
double besselZeroCached (int m, int n) noexcept;

} // namespace foundry
