#pragma once

#include "compile/CompiledInstrument.h"
#include "dsp/PadVoice.h"

#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

namespace foundry::test
{

struct Render
{
    std::vector<float> left, right;
    double sampleRate = 48000.0;
};

inline Render renderHits (const CompiledInstrument& ci, const PadParams& params,
                          const std::vector<std::pair<double, double>>& hits, double seconds)
{
    PadVoice voice;
    voice.prepare (ci.sampleRate);
    voice.setInstrument (&ci);
    voice.setParams (params);
    Render r;
    r.sampleRate = ci.sampleRate;
    const int total = (int) std::lround (seconds * ci.sampleRate);
    r.left.assign ((size_t) total, 0.0f);
    r.right.assign ((size_t) total, 0.0f);
    size_t next = 0;
    int pos = 0;
    while (pos < total)
    {
        int end = total;
        if (next < hits.size())
        {
            const int at = (int) std::lround (hits[next].first * ci.sampleRate);
            if (at <= pos)
            {
                voice.trigger (hits[next].second);
                ++next;
                continue;
            }
            end = std::min (end, at);
        }
        const int n = std::min (end - pos, 256);
        voice.process (r.left.data() + pos, r.right.data() + pos, n);
        pos += n;
    }
    return r;
}

// Hann-windowed single-frequency DFT magnitude over [start, start + len).
inline double toneMagnitude (const std::vector<float>& x, double sr, double freq, size_t start, size_t len)
{
    double re = 0.0, im = 0.0;
    const double w = 2.0 * std::numbers::pi * freq / sr;
    for (size_t i = 0; i < len && start + i < x.size(); ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * (double) i / (double) (len - 1));
        re += win * x[start + i] * std::cos (w * (double) i);
        im -= win * x[start + i] * std::sin (w * (double) i);
    }
    return std::sqrt (re * re + im * im);
}

// Frequency of the strongest spectral peak within +-band (fraction) of `guess`.
inline double findPeak (const std::vector<float>& x, double sr, double guess, double band,
                        size_t start, size_t len)
{
    double best = guess, bestMag = -1.0;
    const int steps = 400;
    for (int i = 0; i <= steps; ++i)
    {
        const double f = guess * (1.0 - band + 2.0 * band * i / steps);
        const double m = toneMagnitude (x, sr, f, start, len);
        if (m > bestMag) { bestMag = m; best = f; }
    }
    return best;
}

inline InstrumentSpec defaultSpec()
{
    InstrumentSpec s;
    s.treatments = {};
    return s;
}

} // namespace foundry::test
