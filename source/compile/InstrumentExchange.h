#pragma once

#include "CompiledInstrument.h"

#include <array>
#include <atomic>
#include <memory>

namespace foundry
{

// Hands compiled instruments from the compile thread to the audio thread without locks,
// and hands used ones back so they're deleted off the audio thread.
//
//   compile thread:  publish()          (replaces anything the audio thread hasn't taken)
//   audio thread:    take(), retire()
//   message thread:  collectGarbage()
class InstrumentExchange
{
public:
    InstrumentExchange() = default;
    ~InstrumentExchange();
    InstrumentExchange (const InstrumentExchange&) = delete;
    InstrumentExchange& operator= (const InstrumentExchange&) = delete;

    void publish (std::unique_ptr<CompiledInstrument> ci) noexcept;

    // Audio thread. Returns the newest instrument, or nullptr if nothing new (or if the
    // retire queue is full, in which case it's picked up on a later block).
    CompiledInstrument* take() noexcept;
    // Audio thread. Queue an instrument the voice no longer uses for deletion.
    bool retire (const CompiledInstrument* ci) noexcept;

    // Message thread (or any non-audio thread).
    void collectGarbage() noexcept;

private:
    static constexpr std::size_t kQueue = 64;
    std::atomic<CompiledInstrument*> pending { nullptr };
    std::array<std::atomic<const CompiledInstrument*>, kQueue> queue {};
    std::atomic<std::size_t> head { 0 }, tail { 0 }; // single producer (audio), single consumer
};

} // namespace foundry
