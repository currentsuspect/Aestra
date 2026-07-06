// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// RTAllocationTrapTest — machine-checkable real-time-safety guard.
//
// This test binary overrides global operator new/delete. While the engine is
// inside processBlock it marks the calling thread via ScopedRealtimeAudioThread
// (AudioEngine.cpp:594) — so any allocation the trap counts while
// isRealtimeAudioThread() is true happened ON THE AUDIO CALLBACK PATH.
//
// Policy asserted here (AGENTS §10):
//   - Steady-state blocks (after the first) must perform ZERO heap
//     allocations and ZERO deallocations.
//   - First-block (warmup) allocations are reported separately: they are a
//     latent xrun risk on stream start and belong in the findings table
//     (AestraDocs/rt-safety-audit.md), but only steady state is a hard gate
//     so this test stays green while any warmup work is triaged.
//
// Scope honesty: the trap covers C++ operator new/delete (all std containers,
// strings, shared_ptr control blocks, etc). It does NOT cover raw malloc(),
// pthread mutex waits, or syscalls — those are covered by the grep audit
// (scripts/rt_safety_audit.sh) and reportRealtimeMisuse() instrumentation.

#include "GoldenAudio/GoldenAudioHarness.h"

#include "RealtimeThreadGuard.h"
#include "DSP/ContinuousParamBuffer.h"
#include "Plugin/SamplerPlugin.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>

// =============================================================================
// Global allocation trap (test binary only)
// =============================================================================

namespace {
std::atomic<bool> g_trapArmed{false};
std::atomic<uint64_t> g_rtAllocCount{0};
std::atomic<uint64_t> g_rtAllocBytes{0};
std::atomic<uint64_t> g_rtFreeCount{0};

inline void noteAlloc(std::size_t size) noexcept {
    if (g_trapArmed.load(std::memory_order_relaxed) && Aestra::Audio::isRealtimeAudioThread()) {
        g_rtAllocCount.fetch_add(1, std::memory_order_relaxed);
        g_rtAllocBytes.fetch_add(size, std::memory_order_relaxed);
    }
}

inline void noteFree() noexcept {
    if (g_trapArmed.load(std::memory_order_relaxed) && Aestra::Audio::isRealtimeAudioThread()) {
        g_rtFreeCount.fetch_add(1, std::memory_order_relaxed);
    }
}
} // namespace

void* operator new(std::size_t size) {
    noteAlloc(size);
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) {
    noteAlloc(size);
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    noteAlloc(size);
    return std::malloc(size ? size : 1);
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    noteAlloc(size);
    return std::malloc(size ? size : 1);
}
void operator delete(void* p) noexcept {
    noteFree();
    std::free(p);
}
void operator delete[](void* p) noexcept {
    noteFree();
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    noteFree();
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    noteFree();
    std::free(p);
}
void operator delete(void* p, const std::nothrow_t&) noexcept {
    noteFree();
    std::free(p);
}
void operator delete[](void* p, const std::nothrow_t&) noexcept {
    noteFree();
    std::free(p);
}

// =============================================================================
// Test
// =============================================================================

using namespace Aestra::Audio;
using namespace GoldenAudio;

namespace {

constexpr double kTau = 6.28318530717958647692;

std::vector<float> makeSine(double freqHz, float amplitude, uint32_t frames, uint32_t sampleRate) {
    std::vector<float> s(static_cast<size_t>(frames) * 2, 0.0f);
    for (uint32_t i = 0; i < frames; ++i) {
        const float v = static_cast<float>(std::sin(kTau * freqHz * static_cast<double>(i) / sampleRate)) *
                        amplitude;
        s[static_cast<size_t>(i) * 2] = v;
        s[static_cast<size_t>(i) * 2 + 1] = v;
    }
    return s;
}

} // namespace

int main() {
    std::cout << "=== Aestra RT Allocation Trap Test ===\n\n";

    // Trap self-check: prove the override actually fires, so a zero result
    // below is meaningful. Allocate inside a marked RT scope and verify the
    // counters move.
    {
        g_trapArmed.store(true, std::memory_order_release);
        const uint64_t a0 = g_rtAllocCount.load(std::memory_order_relaxed);
        const uint64_t f0 = g_rtFreeCount.load(std::memory_order_relaxed);
        {
            ScopedRealtimeAudioThread rtScope;
            // Call the operator directly with a runtime-derived size: paired
            // new/delete expressions visible to the compiler are legal to
            // elide (C++14 allocation elision), which would fake a pass here.
            volatile std::size_t n = 64;
            void* p = ::operator new(n);
            ::operator delete(p);
        }
        g_trapArmed.store(false, std::memory_order_release);
        const bool trapWorks = (g_rtAllocCount.load(std::memory_order_relaxed) == a0 + 1) &&
                               (g_rtFreeCount.load(std::memory_order_relaxed) == f0 + 1);
        std::cout << "[" << (trapWorks ? "PASS" : "FAIL") << "] trap self-check (counted "
                  << (g_rtAllocCount.load(std::memory_order_relaxed) - a0) << " alloc, "
                  << (g_rtFreeCount.load(std::memory_order_relaxed) - f0) << " free in RT scope)\n";
        if (!trapWorks) return 1;
        g_rtAllocCount.store(0, std::memory_order_relaxed);
        g_rtAllocBytes.store(0, std::memory_order_relaxed);
        g_rtFreeCount.store(0, std::memory_order_relaxed);
    }

    SessionConfig cfg;
    const uint32_t totalFrames = cfg.sampleRate * 2; // 2 s
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(cfg.sampleRate));
    // Multi-track session with fader/pan engaged so the full mixing path runs.
    addAudioTrack(*tm, "RTTrapA", makeSine(440.0, 0.30f, totalFrames, cfg.sampleRate), totalFrames, cfg);
    addAudioTrack(*tm, "RTTrapB", makeSine(660.0, 0.20f, totalFrames, cfg.sampleRate), totalFrames, cfg);
    addAudioTrack(*tm, "RTTrapC", makeSine(880.0, 0.15f, totalFrames, cfg.sampleRate), totalFrames, cfg);

    // Active insert on track 1 so EffectChain::process runs on the RT path too.
    if (auto* ch = tm->getChannel(1)) {
        auto plugin = std::make_shared<Plugins::SamplerPlugin>();
        plugin->initialize(static_cast<double>(cfg.sampleRate), cfg.blockSize);
        ch->getEffectChain().prepare(static_cast<double>(cfg.sampleRate), cfg.blockSize);
        ch->getEffectChain().insertPlugin(0, plugin);
    }

    AudioEngine engine;
    prepareEngine(engine, tm, cfg);
    auto params = std::make_shared<ContinuousParamBuffer>();
    params->setFaderDb(0, -3.0f);
    params->setPan(0, -0.5f);
    params->setFaderDb(1, -6.0f);
    params->setPan(1, 0.5f);
    engine.setContinuousParams(params);
    engine.setTransportPlaying(true);

    std::vector<float> block(static_cast<size_t>(cfg.blockSize) * cfg.channels, 0.0f);
    const uint32_t numBlocks = totalFrames / cfg.blockSize;

    uint64_t firstBlockAllocs = 0, firstBlockBytes = 0, firstBlockFrees = 0;
    uint64_t steadyAllocs = 0, steadyBytes = 0, steadyFrees = 0;
    uint32_t firstSteadyViolationBlock = 0;

    g_trapArmed.store(true, std::memory_order_release);
    for (uint32_t b = 0; b < numBlocks; ++b) {
        const uint64_t a0 = g_rtAllocCount.load(std::memory_order_relaxed);
        const uint64_t y0 = g_rtAllocBytes.load(std::memory_order_relaxed);
        const uint64_t f0 = g_rtFreeCount.load(std::memory_order_relaxed);

        std::fill(block.begin(), block.end(), 0.0f);
        engine.processBlock(block.data(), nullptr, cfg.blockSize, 0.0);

        const uint64_t da = g_rtAllocCount.load(std::memory_order_relaxed) - a0;
        const uint64_t dy = g_rtAllocBytes.load(std::memory_order_relaxed) - y0;
        const uint64_t df = g_rtFreeCount.load(std::memory_order_relaxed) - f0;

        if (b == 0) {
            firstBlockAllocs = da;
            firstBlockBytes = dy;
            firstBlockFrees = df;
        } else {
            if ((da || df) && steadyAllocs == 0 && steadyFrees == 0) firstSteadyViolationBlock = b;
            steadyAllocs += da;
            steadyBytes += dy;
            steadyFrees += df;
        }
    }
    g_trapArmed.store(false, std::memory_order_release);
    engine.setTransportPlaying(false);

    std::cout << "first block (warmup):  " << firstBlockAllocs << " allocs / " << firstBlockBytes
              << " bytes / " << firstBlockFrees << " frees\n";
    std::cout << "steady state (" << (numBlocks - 1) << " blocks): " << steadyAllocs << " allocs / "
              << steadyBytes << " bytes / " << steadyFrees << " frees\n";

    const bool pass = (steadyAllocs == 0 && steadyFrees == 0);
    if (!pass) {
        std::cout << "\n[FAIL] heap activity on the audio callback path at steady state\n"
                  << "  first violating block: " << firstSteadyViolationBlock << "\n"
                  << "  Every allocation inside processBlock is an xrun risk (AGENTS §10).\n"
                  << "  Find it: run this test under gdb with a breakpoint on the operator\n"
                  << "  new in RTAllocationTrapTest.cpp conditioned on\n"
                  << "  Aestra::Audio::isRealtimeAudioThread(), or use massif/heaptrack.\n";
    } else {
        std::cout << "\n[PASS] zero steady-state heap activity on the audio callback path\n";
        if (firstBlockAllocs || firstBlockFrees) {
            std::cout << "  note: first-block warmup performed " << firstBlockAllocs
                      << " allocs — reported as a finding, not a failure (see\n"
                      << "  AestraDocs/rt-safety-audit.md).\n";
        }
    }
    return pass ? 0 : 1;
}
