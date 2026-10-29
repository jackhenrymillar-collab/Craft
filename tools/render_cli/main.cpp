// render_cli: headless offline renderer.
//   render_cli --out hit.wav [options]          (see --help)
// Writes the WAV plus <out>.spectrum.csv, <out>.png and <out>.json (analysis report).

#include "Analysis.h"
#include "compile/CompiledInstrument.h"
#include "dsp/PadVoice.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <iostream>

using namespace foundry;

namespace
{
const char* kHelp = R"(render_cli: render Foundry hits offline and analyse them.

  --out FILE.wav        output file (required)
  --sr HZ               sample rate (48000)
  --length SEC          length (3.0)
  --hits LIST           comma list of time:velocity, e.g. 0:1,0.5:0.6 (default 0:0.8)
  --at R,THETA          strike at a fixed point (0 centre..1 edge, radians) instead of Position/Spread
  --part NAME           Membrane | Bar | Plate | Shell (Bowl, Bell) | Tube | Spring | Box (Membrane)
  --material NAME       any material the part allows (default: the part's own)
  --mount N|NAME        0/1 or the mount's name (On cords, Clamped, Hung, Framed, Resting, ...)
  --shape-a/-b/-c U     the part's three shape controls, normalised 0..1 (default: the part's)
  --profile U           shorthand for --shape-b on a Shell (0 bowl .. 1 bell)
  --striker NAME        Mallet | Hammer | Hand | Drop (Mallet)
  --force, --hardness, --spread, --position     0..1
  --mass KG             mallet mass (0.012, Mallet only)
  --air-mix U           0 contact mic .. 1 air mic (0)
  --mic-distance M      air mic distance in metres (0.5)
  --tune ST             Size/Tune in semitones (0)
  --quality N           exactly solved modes (96)
  --age A --climate C
  --deform I --deform-size S --deform-density D
  --twist T --twist-pivot P --twist-focus F --wrinkle W
  --holes N(0..1) --hole-size S --hole-place P --hole-scatter S --ragged R
  --seed N              layout seed for dents/holes/cracks
)";

struct Hit { double t, v; };
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit; // fonts + image codecs
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (argv[i]);
    if (args.isEmpty() || args.contains ("--help"))
    {
        std::cout << kHelp;
        return args.isEmpty() ? 1 : 0;
    }
    auto opt = [&] (const char* name, double def) {
        const int i = args.indexOf (name);
        return i >= 0 && i + 1 < args.size() ? args[i + 1].getDoubleValue() : def;
    };
    auto optS = [&] (const char* name, const juce::String& def) {
        const int i = args.indexOf (name);
        return i >= 0 && i + 1 < args.size() ? args[i + 1] : def;
    };

    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (optS ("--out", ""));
    if (optS ("--out", "").isEmpty())
    {
        std::cerr << "--out is required\n";
        return 1;
    }
    const double sr = opt ("--sr", 48000.0);
    const double length = opt ("--length", 3.0);

    InstrumentSpec spec;
    const auto partName = optS ("--part", "Membrane");
    spec.part = PartSpec::defaultFor (partFromName (partName.toStdString(), PartType::Membrane));
    const auto& info = partInfo (spec.part.type);
    if (args.contains ("--material"))
    {
        const auto m = materialFromName (optS ("--material", "").toStdString(), info.defaultMaterial);
        if (! materialAllowed (spec.part.type, m))
        {
            std::cerr << "material not allowed on a " << info.name << "\n";
            return 1;
        }
        spec.part.material = m;
    }
    if (args.contains ("--mount"))
    {
        const auto m = optS ("--mount", "0");
        spec.part.mount = m.containsOnly ("0123456789") ? m.getIntValue() : -1;
        for (int i = 0; i < info.mountCount; ++i)
            if (m.equalsIgnoreCase (juce::String (info.mounts[(size_t) i].data())))
                spec.part.mount = i;
        if (spec.part.mount < 0 || spec.part.mount >= info.mountCount)
        {
            std::cerr << "unknown mount '" << m << "' for a " << info.name << "\n";
            return 1;
        }
    }
    const char* shapeOpts[] = { "--shape-a", "--shape-b", "--shape-c" };
    for (int i = 0; i < 3; ++i)
        spec.part.shape[(size_t) i] = std::clamp (opt (shapeOpts[i], spec.part.shape[(size_t) i]), 0.0, 1.0);
    if (partName.equalsIgnoreCase ("Bell") && ! args.contains ("--shape-b"))
        spec.part.shape[1] = 1.0;
    if (args.contains ("--profile"))
        spec.part.shape[1] = std::clamp (opt ("--profile", 0.0), 0.0, 1.0);
    spec.tuneSemitones = opt ("--tune", 0.0);
    spec.quality = (int) opt ("--quality", 96);
    auto& t = spec.treatments;
    t.age.amount = opt ("--age", 0.0);
    t.age.climate = opt ("--climate", 0.5);
    t.deform.intensity = opt ("--deform", 0.0);
    t.deform.size = opt ("--deform-size", 0.4);
    t.deform.density = opt ("--deform-density", 0.3);
    t.twist.amount = opt ("--twist", 0.0);
    t.twist.pivot = opt ("--twist-pivot", 0.0);
    t.twist.focus = opt ("--twist-focus", 0.3);
    t.twist.wrinkle = opt ("--wrinkle", 0.0);
    t.puncture.count = opt ("--holes", 0.0);
    t.puncture.size = opt ("--hole-size", 0.3);
    t.puncture.placement = opt ("--hole-place", 0.5);
    t.puncture.scatter = opt ("--hole-scatter", 0.5);
    t.puncture.ragged = opt ("--ragged", 0.3);
    t.seed = (std::uint32_t) opt ("--seed", (double) t.seed);

    PadParams pp;
    pp.force = opt ("--force", 0.6);
    pp.hardness = opt ("--hardness", 0.5);
    pp.spread = opt ("--spread", 0.0);
    pp.strikePosition = opt ("--position", 0.3);
    pp.malletMass = opt ("--mass", 0.012);
    pp.striker = strikerFromName (optS ("--striker", "Mallet").toStdString(), StrikerType::Mallet);
    pp.airMix = std::clamp (opt ("--air-mix", 0.0), 0.0, 1.0);
    pp.micDistance = std::clamp (opt ("--mic-distance", 0.5), 0.1, 3.0);
    pp.level = 1.0;

    std::vector<Hit> hits;
    for (auto& h : juce::StringArray::fromTokens (optS ("--hits", "0:0.8"), ",", ""))
        hits.push_back ({ h.upToFirstOccurrenceOf (":", false, false).getDoubleValue(),
                          h.fromFirstOccurrenceOf (":", false, false).getDoubleValue() });
    std::sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b) { return a.t < b.t; });
    const bool fixedPoint = args.contains ("--at");
    double atR = 0.3, atTheta = 0.0;
    if (fixedPoint)
    {
        const auto at = optS ("--at", "0.3,0");
        atR = at.upToFirstOccurrenceOf (",", false, false).getDoubleValue();
        atTheta = at.fromFirstOccurrenceOf (",", false, false).getDoubleValue();
    }

    auto ci = compileInstrument (spec, sr);

    PadVoice voice;
    voice.prepare (sr);
    voice.setInstrument (ci.get());
    voice.setParams (pp);
    const int total = (int) std::lround (length * sr);
    std::vector<float> L ((size_t) total, 0.0f), R ((size_t) total, 0.0f);
    size_t next = 0;
    int pos = 0, contactSamples = 0;
    double maxEnergy = 0.0, energyAfterLastHit = -1.0;
    bool energyGrew = false, finite = true;
    double peakForce = 0.0;
    while (pos < total)
    {
        if (next < hits.size() && (int) std::lround (hits[next].t * sr) <= pos)
        {
            if (fixedPoint) voice.triggerAt (hits[next].v, atR, atTheta);
            else voice.trigger (hits[next].v);
            ++next;
            energyAfterLastHit = -1.0;
            continue;
        }
        voice.process (L.data() + pos, R.data() + pos, 1);
        const double e = voice.energy();
        maxEnergy = std::max (maxEnergy, e);
        if (voice.striker().isActive())
        {
            contactSamples = voice.striker().contactSamples();
            peakForce = std::max (peakForce, voice.striker().peakForce());
        }
        else
        {
            if (energyAfterLastHit >= 0.0 && e > energyAfterLastHit * (1.0 + 1e-9) + 1e-30)
                energyGrew = true;
            energyAfterLastHit = e;
        }
        if (! std::isfinite (L[(size_t) pos])) finite = false;
        ++pos;
    }

    float peak = 0.0f;
    for (float s : L) peak = std::max (peak, std::abs (s));

    // WAV (24-bit stereo).
    out.getParentDirectory().createDirectory();
    out.deleteFile();
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (out.createOutputStream().release());
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
        if (writer == nullptr)
        {
            std::cerr << "could not write " << out.getFullPathName() << "\n";
            return 1;
        }
        juce::AudioBuffer<float> buf (2, total);
        buf.copyFrom (0, 0, L.data(), total);
        buf.copyFrom (1, 0, R.data(), total);
        writer->writeFromAudioSampleBuffer (buf, 0, total);
    }

    // Analysis.
    const double firstHit = hits.empty() ? 0.0 : hits.front().t;
    const auto spec1 = analysis::spectrum (L, sr, firstHit + 0.02, std::min (1.0, length - firstHit - 0.02));
    const auto peaks = analysis::findPeaks (spec1, 16, 30.0, -110.0);
    std::vector<double> predicted;
    for (int k = 0; k < ci->nExact && k < 40; ++k)
        predicted.push_back (ci->omega[(size_t) k] / juce::MathConstants<double>::twoPi);

    {
        juce::FileOutputStream csv (out.withFileExtension ("spectrum.csv"));
        csv.setPosition (0);
        csv.truncate();
        csv << "freq_hz,db\n";
        for (size_t i = 0; i < spec1.freq.size(); ++i)
            if (spec1.freq[i] <= 20000.0)
                csv << juce::String (spec1.freq[i], 2) << "," << juce::String (spec1.db[i], 2) << "\n";
    }
    {
        auto img = analysis::plot (L, sr, spec1, predicted, out.getFileNameWithoutExtension()
                                    + "   (orange = predicted modes, gold = rendered spectrum)");
        juce::FileOutputStream png (out.withFileExtension ("png"));
        png.setPosition (0);
        png.truncate();
        juce::PNGImageFormat().writeImageToStream (img, png);
    }

    const double f0 = predicted.empty() ? 0.0 : predicted.front();
    const double t60Measured = analysis::measureT60 (L, sr, f0, firstHit + 0.05);
    const double t60Predicted = std::log (1000.0) / ci->sigma[0];
    // The loudest mode (on a tube with air, or a box, the lowest mode is the air and is
    // not what you hear most).
    size_t mainK = 0;
    if (! peaks.empty() && ! predicted.empty())
    {
        const auto loudest = *std::max_element (peaks.begin(), peaks.end(), [] (const auto& a, const auto& b) { return a.db < b.db; });
        double best = 1e9;
        for (size_t k = 0; k < predicted.size(); ++k)
            if (const double c = std::abs (std::log2 (loudest.freq / predicted[k])); c < best) { best = c; mainK = k; }
    }
    const double mainHz = predicted.empty() ? 0.0 : predicted[mainK];
    const double mainT60Measured = mainK == 0 ? t60Measured : analysis::measureT60 (L, sr, mainHz, firstHit + 0.05);
    const double mainT60Predicted = std::log (1000.0) / ci->sigma[mainK];

    auto* report = new juce::DynamicObject();
    report->setProperty ("file", out.getFileName());
    report->setProperty ("sampleRate", sr);
    report->setProperty ("part", juce::String (info.name.data()));
    report->setProperty ("material", juce::String (getMaterial (spec.part.material).name.data()));
    report->setProperty ("mount", juce::String (info.mounts[(size_t) spec.part.mount].data()));
    report->setProperty ("striker", juce::String (strikerInfo (pp.striker).name.data()));
    {
        auto* sh = new juce::DynamicObject();
        for (int i = 0; i < 3; ++i)
        {
            const auto& c = info.controls[(size_t) i];
            sh->setProperty (juce::String (c.name.data()), c.value (spec.part.shape[(size_t) i]) * c.display);
        }
        report->setProperty ("shape", juce::var (sh));
    }
    report->setProperty ("airMix", pp.airMix);
    report->setProperty ("peakDbfs", juce::Decibels::gainToDecibels ((double) peak, -200.0));
    report->setProperty ("finite", finite);
    report->setProperty ("clipped", peak >= 1.0f);
    report->setProperty ("energyGrewWhileFree", energyGrew);
    report->setProperty ("modesExact", ci->nExact);
    report->setProperty ("modesTotal", ci->n);
    report->setProperty ("solveMs", ci->solveMilliseconds);
    report->setProperty ("grit", ci->grit);
    report->setProperty ("contactMsLastHit", 1000.0 * contactSamples / sr);
    report->setProperty ("peakForceN", peakForce);
    report->setProperty ("fundamentalHz", f0);
    report->setProperty ("t60FundamentalPredicted", t60Predicted);
    report->setProperty ("t60FundamentalMeasured", t60Measured);
    report->setProperty ("loudestModeHz", mainHz);
    report->setProperty ("t60LoudestPredicted", mainT60Predicted);
    report->setProperty ("t60LoudestMeasured", mainT60Measured);
    juce::Array<juce::var> pk, pr;
    for (const auto& p : peaks)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("hz", std::round (p.freq * 100.0) / 100.0);
        o->setProperty ("db", std::round (p.db * 10.0) / 10.0);
        // Nearest predicted mode and the error in cents.
        double best = 0, bestCents = 1e9;
        for (double f : predicted)
        {
            const double c = 1200.0 * std::log2 (p.freq / f);
            if (std::abs (c) < std::abs (bestCents)) { bestCents = c; best = f; }
        }
        o->setProperty ("nearestModeHz", std::round (best * 100.0) / 100.0);
        o->setProperty ("centsFromMode", std::round (bestCents * 10.0) / 10.0);
        pk.add (juce::var (o));
    }
    for (int k = 0; k < (int) predicted.size() && k < 16; ++k)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("hz", std::round (predicted[(size_t) k] * 100.0) / 100.0);
        o->setProperty ("ratio", std::round (predicted[(size_t) k] / f0 * 10000.0) / 10000.0);
        o->setProperty ("t60", std::round (std::log (1000.0) / ci->sigma[(size_t) k] * 1000.0) / 1000.0);
        pr.add (juce::var (o));
    }
    report->setProperty ("measuredPeaks", pk);
    report->setProperty ("predictedModes", pr);
    const juce::var reportVar (report);
    out.withFileExtension ("json").replaceWithText (juce::JSON::toString (reportVar));

    std::cout << out.getFileName() << ": peak " << juce::String (juce::Decibels::gainToDecibels ((double) peak, -200.0), 1)
              << " dBFS, f0 " << juce::String (f0, 1) << " Hz, T60 " << juce::String (t60Measured, 2) << " s (predicted "
              << juce::String (t60Predicted, 2) << ")"
              << (mainK != 0 ? ", loudest " + juce::String (mainHz, 1) + " Hz T60 " + juce::String (mainT60Measured, 2) + " s (predicted "
                                   + juce::String (mainT60Predicted, 2) + ")"
                             : juce::String())
              << ", contact " << juce::String (1000.0 * contactSamples / sr, 2) << " ms, "
              << ci->n << " modes solved in " << juce::String (ci->solveMilliseconds, 1) << " ms"
              << (finite ? "" : "  NON-FINITE!") << (energyGrew ? "  ENERGY GREW!" : "")
              << (peak >= 1.0f ? "  CLIPPED!" : "") << "\n";
    return finite && ! energyGrew && peak < 1.0f ? 0 : 2;
}
