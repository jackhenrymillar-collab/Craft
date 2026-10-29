#pragma once

#include "BeamFunctions.h"
#include "ModalBasis.h"

#include <functional>
#include <memory>

namespace foundry
{

// A long thin part (bar, tube, spring) seen from above as a length x width strip. Its modes
// are 1-D functions along the length, either uniform across the width (bending, air column)
// or linear across it (torsion: one edge up, the other down).
class StripBasis final : public ModalBasis
{
public:
    struct Mode
    {
        double omega = 0.0;
        int table = 0;              // index into tables
        bool torsion = false;       // linear across the width
        double coupling = 1.0;      // scales the surface shape (air-column modes couple weakly)
        double extraSigma = 0.0;    // added decay rate (1/s)
        double lossWeight = 1.0;    // share of the material loss (0 for air modes)
        double airVolume = 0.0;     // extra radiating volume per unit coordinate (open tube ends)
        double airDipole = 0.0;
    };

    StripBasis (PartType type, double length, double width, double massPerArea);

    PartType type() const noexcept override { return partType; }
    int size() const noexcept override { return (int) modes.size(); }
    double omega (int k) const noexcept override { return modes[(size_t) k].omega; }
    Vec2 halfExtents() const noexcept override { return { 0.5 * length, 0.5 * width }; }
    double massPerArea() const noexcept override { return mass; }
    double massPerAreaAt (Vec2 p) const noexcept override;

    void shapes (Vec2 p, double* phi) const noexcept override;
    void shapesAndGradients (Vec2 p, double* phi, double* gx, double* gy) const noexcept override;

    double modeCountBelow (double w) const noexcept override;
    double wavenumber (double w) const noexcept override { return kOf ? kOf (w) : 0.0; }
    double dentStiffness() const noexcept override { return dentK; }
    double dentLoss() const noexcept override { return dentL; }
    double extraDecay (int k) const noexcept override { return modes[(size_t) k].extraSigma; }
    double materialLossWeight (int k) const noexcept override { return modes[(size_t) k].lossWeight; }
    void radiation (std::vector<double>& volume, std::vector<double>& dipoleX, std::vector<double>& dipoleY) const override;

    // Building (compile thread only).
    int addTable (std::shared_ptr<const beam::Table> t);
    void addMode (const Mode& m) { modes.push_back (m); }
    void sortAndTrim (int maxModes, double maxOmega);

    std::function<double (double)> kOf;          // structural wavenumber at omega
    double dentK = 0.5, dentL = 0.02;
    std::vector<double> massProfile;             // relative mass per area along the length (empty = uniform)

    const Mode& mode (int k) const noexcept { return modes[(size_t) k]; }
    double getLength() const noexcept { return length; }
    double getWidth() const noexcept { return width; }

private:
    PartType partType;
    double length, width, mass, norm;
    std::vector<Mode> modes;
    std::vector<std::shared_ptr<const beam::Table>> tables;
};

struct PartSpec;
struct AgeGlobals;
struct Material;

// Builders. `ratio` is the Size/Tune frequency ratio (all lengths scale by 1/ratio).
std::shared_ptr<StripBasis> makeBarBasis (const PartSpec& spec, const Material& mat, const AgeGlobals& age,
                                          double ratio, int maxModes, double maxOmega);
std::shared_ptr<StripBasis> makeTubeBasis (const PartSpec& spec, const Material& mat, const AgeGlobals& age,
                                           double ratio, int maxModes, double maxOmega);
std::shared_ptr<StripBasis> makeSpringBasis (const PartSpec& spec, const Material& mat, const AgeGlobals& age,
                                             double ratio, int maxModes, double maxOmega);

// Undercut thickness profile of a bar (1 = full thickness), xi in [0, 1].
double barThicknessProfile (double xi, double undercut) noexcept;

} // namespace foundry
