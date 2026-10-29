#pragma once

#include "ModalBasis.h"

#include <memory>

namespace foundry
{

// Bowl / bell, seen from above as a disc (pole at the centre, rim at the edge).
//
// This is a parametric approximation, tuned by ear rather than solved: modes are labelled
// (m, n) with m >= 2 nodal meridians and n nodal circles. Their frequencies blend (in log
// frequency) between two published patterns:
//   - Bowl (Profile 0): inextensional ring bending, f ~ m (m^2 - 1) / sqrt(m^2 + 1), which
//     gives the 1 : 2.8 : 5.4 : 8.8 series of singing bowls, with axial orders above.
//   - Bell (Profile 1): the classic church-bell partials relative to the hum (hum, prime,
//     tierce, quint, nominal, ...), extrapolated with the ring law above m = 9.
// Each (m, n) is a cos/sin pair, split very slightly (casting asymmetry) so it beats.
// Shapes are rho^2 cos(n pi (1 - rho)) {cos, sin}(m theta): still at the pole, moving most
// at the rim, with n nodal circles.
class ShellBasis final : public ModalBasis
{
public:
    struct Params
    {
        double radius = 0.1;
        double wall = 0.003;
        double profile = 0.0;     // 0 bowl .. 1 bell
        double youngs = 100e9, density = 8800.0;
        bool resting = true;      // sitting on a cushion: low modes lose a little more
        int maxModes = 96;
        double maxOmega = 1e9;
    };

    struct Mode
    {
        int m = 2, n = 0;
        bool isSine = false;
        double omega = 0.0, norm = 0.0;
        int table = 0;
    };

    explicit ShellBasis (const Params& p);

    PartType type() const noexcept override { return PartType::Shell; }
    int size() const noexcept override { return (int) modes.size(); }
    double omega (int k) const noexcept override { return modes[(size_t) k].omega; }
    Vec2 halfExtents() const noexcept override { return { params.radius, params.radius }; }
    bool isRound() const noexcept override { return true; }
    double massPerArea() const noexcept override;

    void shapes (Vec2 p, double* phi) const noexcept override;

    double modeCountBelow (double w) const noexcept override;
    double wavenumber (double w) const noexcept override;
    double dentStiffness() const noexcept override { return 0.5; }
    double dentLoss() const noexcept override { return 0.01; }
    double extraDecay (int k) const noexcept override;
    double normalisation (int k) const noexcept override { return modes[(size_t) k].norm; }

    const Mode& mode (int k) const noexcept { return modes[(size_t) k]; }
    const Params& getParams() const noexcept { return params; }

    // Frequency of mode (m, n) relative to (2, 0) for a given profile.
    static double ratio (int m, int n, double profile) noexcept;

    static constexpr int kTable = 256;

private:
    double radialValue (int table, double rho) const noexcept;

    Params params;
    std::vector<Mode> modes;
    std::vector<std::vector<double>> radial; // per (m, n): R(rho) on kTable + 1 points
};

} // namespace foundry
