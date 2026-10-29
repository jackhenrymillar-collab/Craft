#pragma once

#include "BeamFunctions.h"
#include "ModalBasis.h"

#include <memory>

namespace foundry
{

// Thin rectangular plate (Kirchhoff). Simply supported ("Framed"): exact sine modes.
// Free ("Hung"): Warburton's approximation, with products of free-free beam functions as
// the mode shapes (Warburton 1954; Leissa, Vibration of Plates, 1969).
class PlateBasis final : public ModalBasis
{
public:
    struct Params
    {
        double width = 0.5, height = 0.35, thickness = 0.0015;
        double youngs = 200e9, density = 7850.0, poisson = 0.3;
        bool freeEdges = true;
        int maxModes = 96;
        double maxOmega = 1e9;
        PartType type = PartType::Plate;
    };

    struct Mode
    {
        int i = 0, j = 0;        // beam-function index along x and y
        double omega = 0.0;
    };

    explicit PlateBasis (const Params& p);

    PartType type() const noexcept override { return params.type; }
    int size() const noexcept override { return (int) modes.size(); }
    double omega (int k) const noexcept override { return modes[(size_t) k].omega; }
    Vec2 halfExtents() const noexcept override { return { 0.5 * params.width, 0.5 * params.height }; }
    double massPerArea() const noexcept override { return params.density * params.thickness; }

    void shapes (Vec2 p, double* phi) const noexcept override;
    void shapesAndGradients (Vec2 p, double* phi, double* gx, double* gy) const noexcept override;

    double modeCountBelow (double w) const noexcept override;
    double wavenumber (double w) const noexcept override;
    double dentStiffness() const noexcept override { return 0.6; }
    double dentLoss() const noexcept override { return 0.015; }

    const Mode& mode (int k) const noexcept { return modes[(size_t) k]; }
    const Params& getParams() const noexcept { return params; }
    double bendingSpeedSquared() const noexcept; // sqrt(D / rho h), m^2/s

    // Warburton's G, H, J coefficients for free-free beam function index i (0, 1 = rigid).
    static void warburton (int i, double& G, double& H, double& J) noexcept;

private:
    Params params;
    std::vector<Mode> modes;
    std::vector<std::shared_ptr<const beam::Table>> tx, ty; // per mode
    double norm = 0.0;
};

// A wooden box with one struck face (a cajon): the face is a simply supported plate, and
// the air inside is a spring that couples every face mode with net volume to every other
// and, through the port, to the air plug in the hole (the Helmholtz resonance). Built as
// K = diag(omega^2, 0) + (rho c^2 / V) g g^T over the face modes plus the port coordinate,
// re-solved into coupled modes.
class BoxBasis final : public ModalBasis
{
public:
    struct Params
    {
        PlateBasis::Params face;
        double depth = 0.3;       // m
        double portRadius = 0.04; // m (0 = sealed)
        double backThickness = 0.006;
    };

    explicit BoxBasis (const Params& p);

    PartType type() const noexcept override { return PartType::Box; }
    int size() const noexcept override { return n; }
    double omega (int k) const noexcept override { return omegas[(size_t) k]; }
    Vec2 halfExtents() const noexcept override { return face->halfExtents(); }
    double massPerArea() const noexcept override { return face->massPerArea(); }

    void shapes (Vec2 p, double* phi) const noexcept override;
    void shapesAndGradients (Vec2 p, double* phi, double* gx, double* gy) const noexcept override;

    double modeCountBelow (double w) const noexcept override { return face->modeCountBelow (w); }
    double wavenumber (double w) const noexcept override { return face->wavenumber (w); }
    double dentStiffness() const noexcept override { return face->dentStiffness(); }
    double dentLoss() const noexcept override { return face->dentLoss(); }
    double extraDecay (int k) const noexcept override { return portDecay[(size_t) k]; }
    double materialLossWeight (int k) const noexcept override { return 1.0 - portShare[(size_t) k]; }
    void radiation (std::vector<double>& volume, std::vector<double>& dipoleX, std::vector<double>& dipoleY) const override;

    // Frequency of the bare Helmholtz resonance (rigid walls), Hz; 0 when sealed.
    double helmholtzHz() const noexcept { return helmholtz; }
    // Share of mode k's kinetic energy in the port air (0..1).
    double portParticipation (int k) const noexcept { return portShare[(size_t) k]; }

private:
    Params params;
    std::shared_ptr<PlateBasis> face;
    int nf = 0, n = 0;
    bool hasPort = false;
    std::vector<double> omegas, X, portShare, portDecay, faceVolume, portGain;
    double helmholtz = 0.0;
};

} // namespace foundry
