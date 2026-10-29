#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "compile/CompileWorker.h"
#include "dsp/PadVoice.h"

#include <array>
#include <atomic>
#include <mutex>

namespace foundry
{

// What the UI needs to draw the instrument; copied off the compile thread.
struct InstrumentSnapshot
{
    std::vector<TreatmentFeature> features;
    PartType part = PartType::Membrane;
    Vec2 halfExtents { 0.178, 0.178 };
    bool round = true;
    int mount = 0;
    std::array<double, 3> shape { 0.5, 0.5, 0.5 };
    Vec2 contactMic;
    MaterialId material = MaterialId::Mylar;
    int modes = 0, exactModes = 0;
    double solveMs = 0.0;
    double fundamentalHz = 0.0;
    std::uint64_t version = 0;
};

class FoundryProcessor final : public juce::AudioProcessor,
                               private juce::AudioProcessorValueTreeState::Listener,
                               private juce::ValueTree::Listener,
                               private juce::Timer
{
public:
    FoundryProcessor();
    ~FoundryProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& state() noexcept { return apvts; }

    // UI -> audio: play pad 1 at the Position/Spread setting, or at a point on the part
    // (part-local metres, tap test).
    void queueHit (float velocity);
    void queueTap (float velocity, Vec2 localPoint);

    // Structural choices (state tree, not automatable). Changing the part resets its
    // material, mount and shape controls to that part's defaults.
    void setPartType (PartType t);
    PartType getPartType() const;
    void setMaterial (MaterialId m);
    MaterialId getMaterial() const;
    void setMount (int mount);
    int getMount() const;
    void setStriker (StrikerType t);
    StrikerType getStriker() const;

    InstrumentSnapshot getSnapshot() const;
    std::uint64_t snapshotVersion() const noexcept { return snapshotCounter.load(); }
    float takePeak() noexcept { return peakLevel.exchange (0.0f); }
    int blowUpCount() const noexcept { return voice.blowUpCount(); }

    static constexpr int kPad1Note = 36; // GM-style map starts at C1

    InstrumentSpec buildSpec() const;

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void timerCallback() override;
    void retireVoiceInstruments() noexcept;
    PadParams readLiveParams() const noexcept;

    juce::AudioProcessorValueTreeState apvts;
    InstrumentExchange exchange;
    std::unique_ptr<CompileWorker> worker;
    PadVoice voice;
    double currentSampleRate = 48000.0;

    std::atomic<bool> specDirty { true };

    struct UiHit { float velocity; bool atPoint; float x, y; };
    juce::AbstractFifo hitFifo { 32 };
    std::array<UiHit, 32> hitBuffer {};
    std::mutex hitWriteLock; // several UI callers may queue; the audio thread never takes it

    std::atomic<float> peakLevel { 0.0f };

    mutable std::mutex snapshotLock;
    InstrumentSnapshot snapshot;
    std::atomic<std::uint64_t> snapshotCounter { 0 };

    struct LiveParams
    {
        std::atomic<float>* force = nullptr, *hardness = nullptr, *spread = nullptr, *position = nullptr,
                          *level = nullptr, *pan = nullptr, *airMix = nullptr, *micDistance = nullptr;
    } live;
    std::atomic<int> strikerType { 0 };
    void migrateState();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FoundryProcessor)
};

} // namespace foundry
