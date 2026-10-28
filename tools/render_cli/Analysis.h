#pragma once

#include <juce_graphics/juce_graphics.h>

#include <vector>

namespace foundry::analysis
{
struct Spectrum
{
    std::vector<double> freq, db; // magnitude spectrum in dB re full-scale sine
};

struct Peak
{
    double freq, db;
};

Spectrum spectrum (const std::vector<float>& x, double sr, double startSec, double lengthSec);
std::vector<Peak> findPeaks (const Spectrum& s, int maxPeaks, double minFreq, double floorDb);

// T60 of the component at `freq`, from the slope of its narrowband level over time.
double measureT60 (const std::vector<float>& x, double sr, double freq, double startSec);

// Spectrum (log-frequency) on top, waveform below, with predicted mode markers.
juce::Image plot (const std::vector<float>& x, double sr, const Spectrum& s, const std::vector<double>& predicted,
                  const juce::String& title);
} // namespace foundry::analysis
