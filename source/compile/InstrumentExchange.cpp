#include "InstrumentExchange.h"

namespace foundry
{

InstrumentExchange::~InstrumentExchange()
{
    collectGarbage();
    delete pending.exchange (nullptr);
}

void InstrumentExchange::publish (std::unique_ptr<CompiledInstrument> ci) noexcept
{
    // Anything still pending was never seen by the audio thread, so it's safe to delete here.
    delete pending.exchange (ci.release(), std::memory_order_acq_rel);
}

CompiledInstrument* InstrumentExchange::take() noexcept
{
    const auto h = head.load (std::memory_order_relaxed);
    const auto t = tail.load (std::memory_order_acquire);
    if (h - t >= kQueue - 2)
        return nullptr; // keep room to retire what this swap displaces
    return pending.exchange (nullptr, std::memory_order_acq_rel);
}

bool InstrumentExchange::retire (const CompiledInstrument* ci) noexcept
{
    if (ci == nullptr)
        return true;
    const auto h = head.load (std::memory_order_relaxed);
    const auto t = tail.load (std::memory_order_acquire);
    if (h - t >= kQueue)
        return false;
    queue[h % kQueue].store (ci, std::memory_order_relaxed);
    head.store (h + 1, std::memory_order_release);
    return true;
}

void InstrumentExchange::collectGarbage() noexcept
{
    auto t = tail.load (std::memory_order_relaxed);
    const auto h = head.load (std::memory_order_acquire);
    while (t != h)
    {
        delete queue[t % kQueue].exchange (nullptr, std::memory_order_relaxed);
        ++t;
        tail.store (t, std::memory_order_release);
    }
}

} // namespace foundry
