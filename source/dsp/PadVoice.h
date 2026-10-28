#pragma once

#include "HertzMallet.h"
#include "ModalBank.h"

#include <atomic>
#include <cstdint>

namespace foundry
{

// Live per-pad controls, read every block (automatable parameters).
struct PadParams
{
    double force = 0.6;        // 0..1 scales strike speed
    double hardness = 0.5;     // 0..1
    double spread = 0.15;      // 0..1 random strike position jitter
    double strikePosition = 0.3; // 0 centre .. 1 rim
    double malletMass = 0.012; // kg (effective stick/beater mass)
    double level = 0.8;        // linear gain
    double pan = 0.0;          // -1..1
};

// One pad: an instrument's ringing modes, a mallet and the contact mic. Real-time safe.
// Hitting a ringing pad adds energy to the running state; it never resets it.
class PadVoice
{
public:
    void prepare (double sampleRate) noexcept;

    // Swap in a newly compiled instrument; the old one crossfades out over ~20 ms with its
    // state carried into the new modes. The pointer must stay valid until takeRetired()
    // hands it back.
    void setInstrument (const CompiledInstrument* ci) noexcept;
    // Returns an instrument the voice no longer uses (or nullptr). Call after process()
    // until it returns nullptr.
    const CompiledInstrument* takeRetired() noexcept;
    [[nodiscard]] const CompiledInstrument* instrument() const noexcept { return active.instrument(); }

    void setParams (const PadParams& p) noexcept { params = p; }

    // velocity 0..1
    void trigger (double velocity) noexcept;
    // Strike at an explicit position (tap test). r 0..1, theta radians.
    void triggerAt (double velocity, double r, double theta) noexcept;

    // Adds n samples into left/right.
    void process (float* left, float* right, int n) noexcept;

    void panic() noexcept;
    // Hand back every instrument the voice holds (only while audio is stopped).
    void detach (const CompiledInstrument*& activeOut, const CompiledInstrument*& fadingOut) noexcept;

    [[nodiscard]] double energy() const noexcept { return active.energy(); }
    [[nodiscard]] bool hadBlowUp() const noexcept { return blowUps.load (std::memory_order_relaxed) > 0; }
    [[nodiscard]] int blowUpCount() const noexcept { return blowUps.load (std::memory_order_relaxed); }
    [[nodiscard]] const ModalBank& bank() const noexcept { return active; }
    [[nodiscard]] const HertzMallet& mallet() const noexcept { return hammer; }

    // Strike speed in m/s for a given velocity and Force setting.
    static double strikeSpeed (double velocity, double force) noexcept;
    // Contact mic sensitivity: surface velocity (m/s) -> full scale.
    static constexpr double kMicGain = 0.2;

private:
    double nextRandom() noexcept;
    double renderSample() noexcept;

    double sampleRate = 48000.0, dt = 1.0 / 48000.0;
    PadParams params;
    ModalBank active, fading;
    HertzMallet hammer;
    std::array<const CompiledInstrument*, 4> retired {};
    int retiredCount = 0;
    void pushRetired (const CompiledInstrument* ci) noexcept;
    int fadeRemaining = 0, fadeLength = 960;
    std::array<double, kMaxModes> scratch {}, scratch2 {};
    std::uint64_t rng = 0x243F6A8885A308D3ull;
    double gritEnv = 0.0, gritState = 0.0, gritPrev = 0.0;
    bool asleep = true;
    std::atomic<int> blowUps { 0 };
};

} // namespace foundry
