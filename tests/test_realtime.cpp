// Verifies the audio path never allocates, by counting global operator new calls.
#include "test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdlib>
#include <new>

#if defined(__SANITIZE_ADDRESS__)
 #define FOUNDRY_ASAN 1
#elif defined(__has_feature)
 #if __has_feature(address_sanitizer)
  #define FOUNDRY_ASAN 1
 #endif
#endif

// ASan replaces operator new itself, so the counting allocator is skipped in sanitizer builds.
#ifndef FOUNDRY_ASAN
namespace
{
std::atomic<bool> counting { false };
std::atomic<long> allocations { 0 };
}

void* operator new (std::size_t size)
{
    if (counting.load (std::memory_order_relaxed))
        allocations.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size) { return operator new (size); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
#endif

using namespace foundry;

TEST_CASE ("The audio path does not allocate", "[realtime]")
{
#ifdef FOUNDRY_ASAN
    SKIP ("allocation counting is disabled under AddressSanitizer");
#else
    auto spec = test::defaultSpec();
    auto a = compileInstrument (spec, 48000.0);
    spec.treatments.age.amount = 0.8;
    spec.treatments.puncture.count = 0.5;
    auto b = compileInstrument (spec, 48000.0);

    PadVoice v;
    v.prepare (48000.0);
    v.setInstrument (a.get());
    float l[128] {}, r[128] {};

    allocations = 0;
    counting = true;
    for (int i = 0; i < 400; ++i)
    {
        if (i % 50 == 0) v.trigger (0.9);
        if (i == 120) v.setInstrument (b.get());
        if (i == 300) v.triggerAt (0.5, 0.9, 1.0);
        v.process (l, r, 128);
        (void) v.takeRetired();
    }
    counting = false;
    CHECK (allocations.load() == 0);
#endif
}

TEST_CASE ("Every part and striker plays without allocating", "[realtime]")
{
#ifdef FOUNDRY_ASAN
    SKIP ("allocation counting is disabled under AddressSanitizer");
#else
    std::vector<std::unique_ptr<CompiledInstrument>> parts;
    for (int t = 0; t < kNumPartTypes; ++t)
    {
        auto spec = test::defaultSpec();
        spec.part = PartSpec::defaultFor ((PartType) t);
        spec.treatments.deform.intensity = 0.4;
        parts.push_back (compileInstrument (spec, 48000.0));
    }
    PadVoice v;
    v.prepare (48000.0);
    v.setInstrument (parts[0].get());
    float l[128] {}, r[128] {};

    allocations = 0;
    counting = true;
    for (int i = 0; i < 700; ++i)
    {
        PadParams p;
        p.striker = (StrikerType) ((i / 25) % kNumStrikerTypes);
        p.airMix = 0.5;
        p.micDistance = 0.3 + 0.01 * (i % 50);
        v.setParams (p);
        if (i % 25 == 0) v.trigger (0.9);
        if (i % 100 == 50) v.setInstrument (parts[(size_t) ((i / 100) % parts.size())].get());
        v.process (l, r, 128);
        (void) v.takeRetired();
    }
    counting = false;
    CHECK (allocations.load() == 0);
#endif
}

TEST_CASE ("Joints, rattles, loose objects, sweeps and oversampling play without allocating", "[realtime]")
{
#ifdef FOUNDRY_ASAN
    SKIP ("allocation counting is disabled under AddressSanitizer");
#else
    std::vector<std::unique_ptr<CompiledInstrument>> ins;
    {
        auto s = test::defaultSpec();
        s.part = PartSpec::defaultFor (PartType::Plate);
        s.rattle.type = RattleType::Snare;
        s.loose.type = LooseType::Rice;
        ins.push_back (compileInstrument (s, 48000.0));
        s.attach.enabled = true;
        s.attach.part = PartSpec::defaultFor (PartType::Box);
        ins.push_back (compileInstrument (s, 48000.0));
        s.oversample = true;
        s.rattle.type = RattleType::Sizzle;
        s.loose.type = LooseType::Chain;
        ins.push_back (compileInstrument (s, 48000.0));
        s.oversample = false;
        s.attach.joint = JointType::Rubber;
        s.loose.type = LooseType::Beads;
        ins.push_back (compileInstrument (s, 48000.0));
    }
    PadVoice v;
    v.prepare (48000.0);
    v.setInstrument (ins[0].get());
    float l[128] {}, r[128] {};

    allocations = 0;
    counting = true;
    for (int i = 0; i < 800; ++i)
    {
        PadParams p;
        p.striker = (StrikerType) ((i / 20) % kNumStrikerTypes);
        p.rattle = 0.01 * (i % 100);
        p.airMix = 0.5;
        v.setParams (p);
        if (i % 20 == 0) v.trigger (0.9);
        if (i % 100 == 60) v.setInstrument (ins[(size_t) ((i / 100) % ins.size())].get());
        v.process (l, r, 128);
        (void) v.takeRetired();
    }
    counting = false;
    CHECK (allocations.load() == 0);
#endif
}
