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
    live.airMix = apvts.getRawParameterValue (params::id::airMix);
    live.micDistance = apvts.getRawParameterValue (params::id::micDistance);

    for (auto* p : getParameters())
        if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            if (params::affectsModes (wp->paramID))
                apvts.addParameterListener (wp->paramID, this);

    apvts.state.setProperty (params::partType, juce::String (partInfo (PartType::Membrane).name.data()), nullptr);
    apvts.state.setProperty (params::material, juce::String (foundry::getMaterial (MaterialId::Mylar).name.data()), nullptr);
    apvts.state.setProperty (params::mount, 0, nullptr);
    apvts.state.setProperty (params::striker, juce::String (strikerInfo (StrikerType::Mallet).name.data()), nullptr);
    apvts.state.addListener (this);

    worker = std::make_unique<CompileWorker> (exchange);
    worker->setListener ([this] (const CompiledInstrument& ci) {
        InstrumentSnapshot s;
        s.features = ci.features;
        s.part = ci.partType();
        s.halfExtents = ci.basis->halfExtents();
        s.round = ci.basis->isRound();
        s.mount = ci.spec.part.mount;
        s.shape = ci.spec.part.shape;
        s.contactMic = ci.place (ci.spec.mic.contact);
        s.material = ci.spec.part.material;
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
    s.part = PartSpec::defaultFor (getPartType());
    s.part.material = getMaterial();
    s.part.mount = getMount();
    for (int i = 0; i < 3; ++i)
        s.part.shape[(size_t) i] = get (params::id::shape[i]);
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
    p.airMix = live.airMix->load();
    p.micDistance = live.micDistance->load();
    p.striker = (StrikerType) juce::jlimit (0, kNumStrikerTypes - 1, strikerType.load (std::memory_order_relaxed));
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
                if (! h.atPoint) voice.trigger (h.velocity);
                else voice.triggerAtPoint (h.velocity, { h.x, h.y });
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

void FoundryProcessor::queueHit (float velocity)
{
    std::lock_guard g (hitWriteLock);
    const auto scope = hitFifo.write (1);
    const UiHit h { velocity, false, 0.0f, 0.0f };
    if (scope.blockSize1 > 0) hitBuffer[(size_t) scope.startIndex1] = h;
    else if (scope.blockSize2 > 0) hitBuffer[(size_t) scope.startIndex2] = h;
}

void FoundryProcessor::queueTap (float velocity, Vec2 p)
{
    std::lock_guard g (hitWriteLock);
    const auto scope = hitFifo.write (1);
    const UiHit h { velocity, true, (float) p.x, (float) p.y };
    if (scope.blockSize1 > 0) hitBuffer[(size_t) scope.startIndex1] = h;
    else if (scope.blockSize2 > 0) hitBuffer[(size_t) scope.startIndex2] = h;
}

void FoundryProcessor::setPartType (PartType t)
{
    if (t == getPartType())
        return;
    const auto& info = partInfo (t);
    auto& st = apvts.state;
    st.setProperty (params::partType, juce::String (info.name.data()), nullptr);
    st.setProperty (params::material, juce::String (foundry::getMaterial (info.defaultMaterial).name.data()), nullptr);
    st.setProperty (params::mount, 0, nullptr);
    for (int i = 0; i < 3; ++i)
        if (auto* p = apvts.getParameter (params::id::shape[i]))
            p->setValueNotifyingHost ((float) info.controls[(size_t) i].defaultNormalised());
    specDirty = true;
}

PartType FoundryProcessor::getPartType() const
{
    return partFromName (apvts.state.getProperty (params::partType).toString().toStdString(), PartType::Membrane);
}

void FoundryProcessor::setMaterial (MaterialId m)
{
    if (materialAllowed (getPartType(), m))
        apvts.state.setProperty (params::material, juce::String (foundry::getMaterial (m).name.data()), nullptr);
}

MaterialId FoundryProcessor::getMaterial() const
{
    const auto type = getPartType();
    const auto m = materialFromName (apvts.state.getProperty (params::material).toString().toStdString(), partInfo (type).defaultMaterial);
    return materialAllowed (type, m) ? m : partInfo (type).defaultMaterial;
}

void FoundryProcessor::setMount (int m)
{
    apvts.state.setProperty (params::mount, juce::jlimit (0, partInfo (getPartType()).mountCount - 1, m), nullptr);
}

int FoundryProcessor::getMount() const
{
    return juce::jlimit (0, partInfo (getPartType()).mountCount - 1, (int) apvts.state.getProperty (params::mount, 0));
}

void FoundryProcessor::setStriker (StrikerType t)
{
    apvts.state.setProperty (params::striker, juce::String (strikerInfo (t).name.data()), nullptr);
}

StrikerType FoundryProcessor::getStriker() const
{
    return strikerFromName (apvts.state.getProperty (params::striker).toString().toStdString(), StrikerType::Mallet);
}

void FoundryProcessor::migrateState()
{
    // M1 sessions had one drum and stored its head material as "headMaterial".
    auto& st = apvts.state;
    if (! st.hasProperty (params::partType))
        st.setProperty (params::partType, juce::String (partInfo (PartType::Membrane).name.data()), nullptr);
    if (! st.hasProperty (params::material))
        st.setProperty (params::material, st.getProperty (params::headMaterial, "Mylar"), nullptr);
    if (! st.hasProperty (params::striker))
        st.setProperty (params::striker, juce::String (strikerInfo (StrikerType::Mallet).name.data()), nullptr);
    strikerType = (int) getStriker();
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
    if (prop == params::partType || prop == params::material || prop == params::mount)
        specDirty = true;
    else if (prop == params::striker)
        strikerType = (int) getStriker();
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
            migrateState();
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
