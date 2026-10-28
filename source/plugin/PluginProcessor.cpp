#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace foundry
{

FoundryProcessor::FoundryProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Foundry", params::createLayout())
{
    live.force = apvts.getRawParameterValue (params::id::force);
    live.hardness = apvts.getRawParameterValue (params::id::hardness);
    live.spread = apvts.getRawParameterValue (params::id::spread);
    live.position = apvts.getRawParameterValue (params::id::position);
    live.level = apvts.getRawParameterValue (params::id::level);
    live.pan = apvts.getRawParameterValue (params::id::pan);

    for (auto* p : getParameters())
        if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            if (params::affectsModes (wp->paramID))
                apvts.addParameterListener (wp->paramID, this);

    apvts.state.setProperty (params::headMaterial, juce::String (getMaterial (MaterialId::Mylar).name.data()), nullptr);
    apvts.state.addListener (this);

    worker = std::make_unique<CompileWorker> (exchange);
    worker->setListener ([this] (const CompiledInstrument& ci) {
        InstrumentSnapshot s;
        s.features = ci.features;
        s.radius = ci.radius;
        s.contactMic = ci.spec.contactMic;
        s.material = ci.spec.membrane.material;
        s.modes = ci.n;
        s.exactModes = ci.nExact;
        s.solveMs = ci.solveMilliseconds;
        s.fundamentalHz = ci.n > 0 ? ci.omega[0] / juce::MathConstants<double>::twoPi : 0.0;
        s.version = ci.version;
        {
            std::lock_guard g (snapshotLock);
            snapshot = std::move (s);
        }
        snapshotCounter.fetch_add (1);
    });

    startTimerHz (30);
}

FoundryProcessor::~FoundryProcessor()
{
    stopTimer();
    apvts.state.removeListener (this);
    worker.reset(); // joins the compile thread
    retireVoiceInstruments();
    exchange.collectGarbage();
}

bool FoundryProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

InstrumentSpec FoundryProcessor::buildSpec() const
{
    auto get = [this] (const char* pid) { return (double) apvts.getRawParameterValue (pid)->load(); };
    InstrumentSpec s;
    s.membrane.material = getHeadMaterial();
    s.tuneSemitones = get (params::id::tune);
    auto& t = s.treatments;
    t.age.amount = get (params::id::ageAmount);
    t.age.climate = get (params::id::ageClimate);
    t.deform.intensity = get (params::id::deformIntensity);
    t.deform.size = get (params::id::deformSize);
    t.deform.density = get (params::id::deformDensity);
    t.twist.amount = get (params::id::twistAmount);
    t.twist.pivot = get (params::id::twistPivot);
    t.twist.focus = get (params::id::twistFocus);
    t.twist.wrinkle = get (params::id::twistWrinkle);
    t.puncture.count = get (params::id::punctureCount);
    t.puncture.size = get (params::id::punctureSize);
    t.puncture.placement = get (params::id::puncturePlacement);
    t.puncture.scatter = get (params::id::punctureScatter);
    t.puncture.ragged = get (params::id::punctureRagged);
    return s;
}

void FoundryProcessor::retireVoiceInstruments() noexcept
{
    // Only called while audio isn't running (constructor/destructor/prepareToPlay).
    const CompiledInstrument* a = nullptr;
    const CompiledInstrument* b = nullptr;
    voice.detach (a, b);
    exchange.retire (a);
    exchange.retire (b);
    while (auto* r = voice.takeRetired())
        exchange.retire (r);
}

void FoundryProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    retireVoiceInstruments();
    exchange.collectGarbage();

    // Build synchronously so the first block already has an instrument.
    auto ci = compileInstrument (buildSpec(), sampleRate);
    voice.prepare (sampleRate);
    voice.setInstrument (ci.release());
    specDirty = true; // also refresh the UI snapshot via the worker
}

PadParams FoundryProcessor::readLiveParams() const noexcept
{
    PadParams p;
    p.force = live.force->load();
    p.hardness = live.hardness->load();
    p.spread = live.spread->load();
    p.strikePosition = live.position->load();
    p.level = juce::Decibels::decibelsToGain ((double) live.level->load(), -60.0);
    p.pan = live.pan->load();
    return p;
}

void FoundryProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    if (auto* fresh = exchange.take())
    {
        if (juce::approximatelyEqual (fresh->sampleRate, currentSampleRate))
            voice.setInstrument (fresh);
        else
        {
            exchange.retire (fresh);
            specDirty = true;
        }
    }

    voice.setParams (readLiveParams());

    const int numSamples = buffer.getNumSamples();
    float* left = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    // Mono layouts: render the right channel into a scratch area that we fold back in.
    std::array<float, 512> monoScratch {};

    auto render = [&] (int start, int end) {
        while (start < end)
        {
            const int n = std::min (end - start, (int) monoScratch.size());
            if (right != nullptr)
                voice.process (left + start, right + start, n);
            else
            {
                std::fill (monoScratch.begin(), monoScratch.begin() + n, 0.0f);
                voice.process (left + start, monoScratch.data(), n);
                for (int i = 0; i < n; ++i)
                    left[start + i] = 0.5f * (left[start + i] + monoScratch[(size_t) i]);
            }
            start += n;
        }
    };

    // Hits from the UI land at the start of the block.
    {
        const auto scope = hitFifo.read (hitFifo.getNumReady());
        auto play = [this] (int start, int size) {
            for (int i = 0; i < size; ++i)
            {
                const auto& h = hitBuffer[(size_t) (start + i)];
                if (h.r < 0.0f) voice.trigger (h.velocity);
                else voice.triggerAt (h.velocity, h.r, h.theta);
            }
        };
        play (scope.startIndex1, scope.blockSize1);
        play (scope.startIndex2, scope.blockSize2);
    }

    int pos = 0;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const int at = juce::jlimit (0, numSamples, meta.samplePosition);
        render (pos, at);
        pos = at;
        if (msg.isNoteOn() && msg.getNoteNumber() == kPad1Note)
            voice.trigger (msg.getFloatVelocity());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            voice.panic();
    }
    render (pos, numSamples);

    while (auto* r = voice.takeRetired())
        exchange.retire (r);

    // Safety: gentle soft clip above -1 dBFS so nothing harsh ever leaves the plugin.
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* d = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            float x = d[i];
            const float a = std::abs (x);
            if (a > 0.89f)
                x = std::copysign (0.89f + 0.11f * std::tanh ((a - 0.89f) / 0.11f), x);
            d[i] = x;
            peak = std::max (peak, std::abs (x));
        }
    }
    float prev = peakLevel.load (std::memory_order_relaxed);
    while (peak > prev && ! peakLevel.compare_exchange_weak (prev, peak)) {}
}

void FoundryProcessor::queueHit (float velocity, float r, float theta)
{
    std::lock_guard g (hitWriteLock);
    const auto scope = hitFifo.write (1);
    if (scope.blockSize1 > 0)
        hitBuffer[(size_t) scope.startIndex1] = { velocity, r, theta };
    else if (scope.blockSize2 > 0)
        hitBuffer[(size_t) scope.startIndex2] = { velocity, r, theta };
}

void FoundryProcessor::setHeadMaterial (MaterialId m)
{
    apvts.state.setProperty (params::headMaterial, juce::String (getMaterial (m).name.data()), nullptr);
}

MaterialId FoundryProcessor::getHeadMaterial() const
{
    const auto name = apvts.state.getProperty (params::headMaterial).toString().toStdString();
    const auto m = materialFromName (name, MaterialId::Mylar);
    return getMaterial (m).arealDensity > 0.0 ? m : MaterialId::Mylar;
}

InstrumentSnapshot FoundryProcessor::getSnapshot() const
{
    std::lock_guard g (snapshotLock);
    return snapshot;
}

void FoundryProcessor::parameterChanged (const juce::String&, float)
{
    specDirty = true; // may be called on the audio thread: just flag it
}

void FoundryProcessor::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& prop)
{
    if (prop == params::headMaterial)
        specDirty = true;
}

void FoundryProcessor::timerCallback()
{
    exchange.collectGarbage();
    if (specDirty.exchange (false))
        worker->request (buildSpec(), currentSampleRate);
}

void FoundryProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void FoundryProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            apvts.state.addListener (this);
            specDirty = true;
        }
}

juce::AudioProcessorEditor* FoundryProcessor::createEditor()
{
    return new FoundryEditor (*this);
}

} // namespace foundry

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new foundry::FoundryProcessor();
}
