#pragma once

#include "ModalBank.h"
#include "Striker.h"

#include <atomic>
#include <cstdint>

namespace foundry
{

// Live per-pad controls, read every block (automatable parameters).
struct PadParams
{
    double force = 0.6;          // 0..1 scales strike speed (Drop: drop height)
    double hardness = 0.5;       // 0..1
    double spread = 0.15;        // 0..1 random strike position jitter
    double strikePosition = 0.3; // 0 centre .. 1 edge
    StrikerType striker = StrikerType::Mallet;
    double malletMass = 0.012;   // kg (effective stick/beater mass, Mallet only)
    double level = 0.8;          // linear gain
    double pan = 0.0;            // -1..1
    double airMix = 0.0;         // 0 contact mic .. 1 air mic
    double micDistance = 0.5;    // air mic distance, m
};

// One pad: an instrument's ringing modes, a striker and two pickups (contact + air mic).
// Real-time safe. Hitting a ringing pad adds energy to the running state; it never resets it.
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
    // Strike at an explicit placement (r 0 centre .. 1 edge, theta radians).
    void triggerAt (double velocity, double r, double theta) noexcept;
    // Strike at a point in part-local metres (tap test).
    void triggerAtPoint (double velocity, Vec2 p) noexcept;

    // Adds n samples into left/right.
    void process (float* left, float* right, int n) noexcept;

    void panic() noexcept;
    // Hand back every instrument the voice holds (only while audio is stopped).
    void detach (const CompiledInstrument*& activeOut, const CompiledInstrument*& fadingOut) noexcept;

    [[nodiscard]] double energy() const noexcept { return active.energy(); }
    [[nodiscard]] bool hadBlowUp() const noexcept { return blowUps.load (std::memory_order_relaxed) > 0; }
    [[nodiscard]] int blowUpCount() const noexcept { return blowUps.load (std::memory_order_relaxed); }
    [[nodiscard]] const ModalBank& bank() const noexcept { return active; }
    [[nodiscard]] const Striker& striker() const noexcept { return hitter; }
    [[nodiscard]] bool isAsleep() const noexcept { return asleep; }

    // Strike speed in m/s for a given velocity and Force setting (before the striker's own scale).
    static double strikeSpeed (double velocity, double force) noexcept;
    // Drop height in metres for a given velocity and Force setting.
    static double dropHeight (double velocity, double force) noexcept;

    static constexpr int kMaxDelay = 16384;

private:
    double nextRandom() noexcept;
    double renderSample() noexcept;

    double sampleRate = 48000.0, dt = 1.0 / 48000.0;
    PadParams params;
    ModalBank active, fading;
    Striker hitter;
    std::array<const CompiledInstrument*, 4> retired {};
    int retiredCount = 0;
    void pushRetired (const CompiledInstrument* ci) noexcept;
    int fadeRemaining = 0, fadeLength = 960;
    std::array<double, kMaxModes> scratch {}, scratch2 {};
    std::uint64_t rng = 0x243F6A8885A308D3ull;
    double gritEnv = 0.0, gritState = 0.0, gritPrev = 0.0;
    // Air mic: previous volume flows (for d/dt), a delay line and a gentle high cut.
    double flowPrevActive = 0.0, flowPrevFading = 0.0;
    std::array<double, kMaxDelay> delayLine {};
    int delayWrite = 0;
    double delaySamples = 0.0, airLowpass = 0.0, airLowpass1 = 0.0, airCoeff = 1.0, airLevel = 1.0;
    int tailRemaining = 0;
    bool asleep = true;
    std::atomic<int> blowUps { 0 };
};

} // namespace foundry
