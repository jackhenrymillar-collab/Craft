#include "Calibrate.h"

#include "ModalBank.h"
#include "Striker.h"

#include <algorithm>
#include <cmath>

namespace foundry
{
namespace
{
struct Levels
{
    double rmsContact = 0.0, rmsAir = 0.0, peakContact = 0.0, peakAir = 0.0;
};

Levels playReference (const CompiledInstrument& ci, double hardness)
{
    ModalBank bank;
    bank.setInstrument (&ci);
    bank.reset();

    std::vector<double> shape ((size_t) ci.n), scratch ((size_t) ci.n);
    ci.shapeAt (ci.place ({ 0.35, 0.5 }), shape.data(), scratch.data());
    for (int k = 0; k < ci.n; ++k)
        if (ci.b1[(size_t) k] == 0.0)
            shape[(size_t) k] = 0.0;

    Striker s;
    StrikerParams sp;
    sp.type = StrikerType::Mallet;
    sp.hardness = hardness;
    s.start (bank, shape.data(), 1.5, Striker::modelFor (sp));

    const double dt = 1.0 / ci.sampleRate;
    const int length = std::max (16, (int) std::lround (0.04 * ci.sampleRate));
    // The air mic's gentle high cut at the reference distance (see PadVoice).
    const double coeff = 1.0 - std::exp (-2.0 * 3.141592653589793 * std::min (10000.0, 0.45 * ci.sampleRate) / ci.sampleRate);
    double prevQ = 0.0, lp1 = 0.0, lp2 = 0.0;
    Levels lv;
    for (int i = 0; i < length; ++i)
    {
        if (s.isActive())
            bank.step (s.shape(), s.process (bank, dt));
        else
            bank.stepFree();
        const double c = bank.velocityAt (ci.micShape.data());
        const double q = bank.velocityAt (ci.airShape.data());
        const double a = (q - prevQ) * ci.sampleRate;
        prevQ = q;
        lp1 += coeff * (a - lp1);
        lp2 += coeff * (lp1 - lp2);
        lv.rmsContact += c * c;
        lv.rmsAir += lp2 * lp2;
        lv.peakContact = std::max (lv.peakContact, std::abs (c));
        lv.peakAir = std::max (lv.peakAir, std::abs (lp2));
    }
    lv.rmsContact = std::sqrt (lv.rmsContact / length);
    lv.rmsAir = std::sqrt (lv.rmsAir / length);
    return lv;
}
} // namespace

void calibrateOutputLevels (CompiledInstrument& ci)
{
    if (ci.n == 0)
        return;
    // Loudness from a medium mallet; a hard one sets a ceiling so hard-on-hard clicks
    // (a steel ball on a bell) don't come out 20 dB hotter than everything else.
    const Levels mid = playReference (ci, 0.5);
    const Levels hard = playReference (ci, 1.0);
    auto gainFor = [] (double rms, double peak) {
        if (! (rms > 1e-15) || ! std::isfinite (rms))
            return 1.0;
        double g = kCalibrationTarget / rms;
        if (peak > 0.0 && std::isfinite (peak))
            g = std::min (g, kCalibrationPeak / peak);
        return std::clamp (g, 1e-6, 1e9);
    };
    ci.contactGain = gainFor (mid.rmsContact, hard.peakContact);
    ci.airGain = gainFor (mid.rmsAir, hard.peakAir);
}

} // namespace foundry
