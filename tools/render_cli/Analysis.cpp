#include "Analysis.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>

namespace foundry::analysis
{

Spectrum spectrum (const std::vector<float>& x, double sr, double startSec, double lengthSec)
{
    const int order = juce::jlimit (10, 18, (int) std::ceil (std::log2 (lengthSec * sr)));
    const int n = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) (2 * n), 0.0f);
    const size_t start = (size_t) (startSec * sr);
    const size_t len = std::min ((size_t) (lengthSec * sr), (size_t) n);
    double winSum = 0.0;
    for (size_t i = 0; i < len && start + i < x.size(); ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (double) i / (double) (len - 1));
        buf[i] = (float) (w * x[start + i]);
        winSum += w;
    }
    fft.performFrequencyOnlyForwardTransform (buf.data());
    Spectrum s;
    for (int k = 1; k < n / 2; ++k)
    {
        const double mag = 2.0 * buf[(size_t) k] / juce::jmax (1.0, winSum);
        s.freq.push_back (k * sr / n);
        s.db.push_back (20.0 * std::log10 (std::max (mag, 1e-12)));
    }
    return s;
}

std::vector<Peak> findPeaks (const Spectrum& s, int maxPeaks, double minFreq, double floorDb)
{
    std::vector<Peak> peaks;
    for (size_t i = 2; i + 2 < s.db.size(); ++i)
    {
        if (s.freq[i] < minFreq || s.db[i] < floorDb) continue;
        if (s.db[i] > s.db[i - 1] && s.db[i] >= s.db[i + 1] && s.db[i] > s.db[i - 2] && s.db[i] >= s.db[i + 2])
        {
            // Parabolic interpolation for a sub-bin frequency estimate.
            const double a = s.db[i - 1], b = s.db[i], c = s.db[i + 1];
            const double p = 0.5 * (a - c) / (a - 2 * b + c);
            const double df = s.freq[1] - s.freq[0];
            peaks.push_back ({ s.freq[i] + p * df, b - 0.25 * (a - c) * p });
        }
    }
    std::sort (peaks.begin(), peaks.end(), [] (const Peak& x, const Peak& y) { return x.db > y.db; });
    if ((int) peaks.size() > maxPeaks) peaks.resize ((size_t) maxPeaks);
    std::sort (peaks.begin(), peaks.end(), [] (const Peak& x, const Peak& y) { return x.freq < y.freq; });
    return peaks;
}

double measureT60 (const std::vector<float>& x, double sr, double freq, double startSec)
{
    // Narrowband level in 100 ms Hann windows every 50 ms; least-squares slope of dB vs time
    // over the part that is at least 30 dB above the floor.
    const size_t win = (size_t) (0.1 * sr), hop = (size_t) (0.05 * sr);
    std::vector<double> t, level;
    const double w = 2.0 * juce::MathConstants<double>::pi * freq / sr;
    for (size_t s = (size_t) (startSec * sr); s + win < x.size(); s += hop)
    {
        double re = 0, im = 0;
        for (size_t i = 0; i < win; ++i)
        {
            const double h = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (double) i / (double) (win - 1));
            re += h * x[s + i] * std::cos (w * (double) i);
            im -= h * x[s + i] * std::sin (w * (double) i);
        }
        t.push_back ((double) s / sr);
        level.push_back (10.0 * std::log10 (re * re + im * im + 1e-30));
    }
    if (t.size() < 3) return 0.0;
    const double top = level.front();
    double st = 0, sl = 0, stt = 0, stl = 0;
    int n = 0;
    for (size_t i = 0; i < t.size(); ++i)
    {
        if (level[i] < top - 40.0) break;
        st += t[i]; sl += level[i]; stt += t[i] * t[i]; stl += t[i] * level[i];
        ++n;
    }
    if (n < 3) return 0.0;
    const double slope = (n * stl - st * sl) / (n * stt - st * st); // dB per second
    return slope < 0.0 ? -60.0 / slope : 0.0;
}

juce::Image plot (const std::vector<float>& x, double sr, const Spectrum& s, const std::vector<double>& predicted,
                  const juce::String& title)
{
    const int W = 1200, H = 700;
    juce::Image img (juce::Image::RGB, W, H, true);
    juce::Graphics g (img);
    g.fillAll (juce::Colour (0xff1b1c1a));

    auto area = juce::Rectangle<float> (60, 40, W - 90, 420);
    const double fMin = 20.0, fMax = std::min (20000.0, sr / 2.0);
    auto fx = [&] (double f) { return area.getX() + area.getWidth() * (float) (std::log (f / fMin) / std::log (fMax / fMin)); };
    auto dy = [&] (double db) { return area.getY() + area.getHeight() * (float) ((0.0 - db) / 120.0); };

    g.setColour (juce::Colour (0xff34352f));
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        if (f <= fMax)
        {
            g.drawVerticalLine ((int) fx (f), area.getY(), area.getBottom());
            g.setColour (juce::Colour (0xffa6a59d));
            g.setFont (12.0f);
            g.drawText (f >= 1000 ? juce::String (f / 1000.0) + "k" : juce::String ((int) f), (int) fx (f) - 20, (int) area.getBottom() + 4, 40, 14, juce::Justification::centred);
            g.setColour (juce::Colour (0xff34352f));
        }
    for (int db = 0; db >= -120; db -= 20)
    {
        g.drawHorizontalLine ((int) dy (db), area.getX(), area.getRight());
        g.setColour (juce::Colour (0xffa6a59d));
        g.drawText (juce::String (db), 10, (int) dy (db) - 7, 44, 14, juce::Justification::centredRight);
        g.setColour (juce::Colour (0xff34352f));
    }
    g.setColour (juce::Colour (0xffff7a1a).withAlpha (0.35f));
    for (double f : predicted)
        if (f > fMin && f < fMax)
            g.drawVerticalLine ((int) fx (f), area.getY(), area.getBottom());

    juce::Path p;
    bool started = false;
    for (size_t i = 0; i < s.freq.size(); ++i)
    {
        if (s.freq[i] < fMin || s.freq[i] > fMax) continue;
        const float px = fx (s.freq[i]), py = juce::jlimit (area.getY(), area.getBottom(), dy (s.db[i]));
        if (! started) { p.startNewSubPath (px, py); started = true; }
        else p.lineTo (px, py);
    }
    g.setColour (juce::Colour (0xffd4b06a));
    g.strokePath (p, juce::PathStrokeType (1.2f));

    // Waveform.
    auto wave = juce::Rectangle<float> (60, 500, W - 90, 170);
    g.setColour (juce::Colour (0xff20211e));
    g.fillRect (wave);
    g.setColour (juce::Colour (0xffd4b06a));
    const size_t perPx = std::max<size_t> (1, x.size() / (size_t) wave.getWidth());
    for (int px = 0; px < (int) wave.getWidth(); ++px)
    {
        float lo = 0, hi = 0;
        for (size_t i = (size_t) px * perPx; i < std::min (x.size(), (size_t) (px + 1) * perPx); ++i)
        {
            lo = std::min (lo, x[i]);
            hi = std::max (hi, x[i]);
        }
        const float cy = wave.getCentreY(), hh = wave.getHeight() * 0.5f;
        g.drawVerticalLine ((int) wave.getX() + px, cy - hi * hh, cy - lo * hh + 1.0f);
    }

    g.setColour (juce::Colour (0xffecebe6));
    g.setFont (15.0f);
    g.drawText (title, 60, 10, W - 90, 22, juce::Justification::centredLeft);
    return img;
}
} // namespace foundry::analysis
