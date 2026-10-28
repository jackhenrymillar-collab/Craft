#include "CompileWorker.h"

namespace foundry
{

CompileWorker::CompileWorker (InstrumentExchange& target) : exchange (target)
{
    thread = std::thread ([this] { run(); });
}

CompileWorker::~CompileWorker()
{
    {
        std::lock_guard g (lock);
        quit = true;
    }
    wake.notify_all();
    thread.join();
}

void CompileWorker::request (const InstrumentSpec& spec, double sampleRate)
{
    {
        std::lock_guard g (lock);
        job = std::make_pair (spec, sampleRate);
        requested.fetch_add (1);
    }
    wake.notify_one();
}

void CompileWorker::setListener (Listener l)
{
    std::lock_guard g (lock);
    listener = std::move (l);
}

void CompileWorker::run()
{
    for (;;)
    {
        std::pair<InstrumentSpec, double> todo;
        std::uint64_t version = 0;
        Listener notify;
        {
            std::unique_lock g (lock);
            wake.wait (g, [this] { return quit || job.has_value(); });
            if (quit)
                return;
            todo = *job;
            job.reset();
            version = requested.load();
            notify = listener;
        }
        auto ci = compileInstrument (todo.first, todo.second, version);
        if (notify)
            notify (*ci);
        exchange.publish (std::move (ci));
        completed.store (version);
    }
}

} // namespace foundry
