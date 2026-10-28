#pragma once

#include "InstrumentExchange.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace foundry
{

// Background thread that turns specs into CompiledInstruments. Requests coalesce: if
// several arrive while a compile is running, only the newest is built next.
class CompileWorker
{
public:
    using Listener = std::function<void (const CompiledInstrument&)>; // called on the worker thread

    explicit CompileWorker (InstrumentExchange& target);
    ~CompileWorker();

    void request (const InstrumentSpec& spec, double sampleRate);
    void setListener (Listener l);

    [[nodiscard]] std::uint64_t lastCompletedVersion() const noexcept { return completed.load(); }
    [[nodiscard]] std::uint64_t lastRequestedVersion() const noexcept { return requested.load(); }

private:
    void run();

    InstrumentExchange& exchange;
    std::mutex lock;
    std::condition_variable wake;
    std::optional<std::pair<InstrumentSpec, double>> job;
    Listener listener;
    std::atomic<std::uint64_t> requested { 0 }, completed { 0 };
    bool quit = false;
    std::thread thread;
};

} // namespace foundry
