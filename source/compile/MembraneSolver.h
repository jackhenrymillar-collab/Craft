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

// Table of Bessel zeros j_mn, computed once and cached (thread-safe).
double besselZeroCached (int m, int n) noexcept;

} // namespace foundry
