// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// RTGuardConsolidationTest
// ─────────────────────────────────────────────────────────────────────────────
// Regression guard for the real-time-thread-guard consolidation (R1).
//
// Before consolidation there were TWO independent thread-local flags:
//   • g_realtimeAudioThreadDepth  (RealtimeThreadGuard.h, set by
//     ScopedRealtimeAudioThread — the engine / processBlock path)
//   • g_isAudioThread             (AudioThreadConstraints.h, set by the removed
//     AudioThreadGuard — the app-layer callback path)
//
// The app-layer constraint macros (AESTRA_ASSERT_AUDIO_THREAD / AESTRA_TRACK_*)
// keyed off the *app* flag, while the engine's RT-misuse checks keyed off the
// *engine* flag. A thread marked only by ScopedRealtimeAudioThread would NOT be
// recognized by the app-layer constraint layer — the "incomplete detection ->
// false confidence" hazard.
//
// This test marks the thread using ONLY ScopedRealtimeAudioThread (exactly what
// AestraAudioController::audioCallback now does) and proves the app-layer
// constraint macros in AudioThreadConstraints.h recognize it as real-time. If a
// second parallel flag is ever reintroduced, this test fails.

// Force the debug constraint macros active regardless of the test build's
// NDEBUG state, so the allocation/lock/IO detection paths are actually exercised.
#define AESTRA_AUDIO_DEBUG 1

#include "RealtimeThreadGuard.h"   // canonical: ScopedRealtimeAudioThread / isRealtimeAudioThread
#include "AudioThreadConstraints.h" // app-layer constraint macros + AudioThreadStats

#include <cstdio>

using namespace Aestra::Audio;

static int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL (line %d): %s\n", __LINE__, #cond);     \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

int main() {
    auto& stats = AudioThreadStats::instance();
    stats.reset();

    // ── Precondition: worker/UI thread is not real-time ────────────────────
    CHECK(!isRealtimeAudioThread());
    AESTRA_ASSERT_NOT_AUDIO_THREAD(); // must not fire off the audio thread
    AESTRA_TRACK_ALLOCATION(128, "off-thread");
    CHECK(stats.totalAllocations.load() == 1);
    CHECK(stats.allocationViolations.load() == 0); // no violation off-thread

    // ── Mark the thread with ONLY the canonical engine guard ───────────────
    {
        ScopedRealtimeAudioThread rtScope;

        // The app-layer constraint layer must recognize this as real-time.
        CHECK(isRealtimeAudioThread());
        AESTRA_ASSERT_AUDIO_THREAD(); // must pass while marked

        // Allocation / lock / file-IO tracking must flag violations here.
        AESTRA_TRACK_ALLOCATION(256, "rt-thread");
        AESTRA_TRACK_LOCK();
        AESTRA_TRACK_FILE_IO();

        CHECK(stats.totalAllocations.load() == 2);
        CHECK(stats.allocationViolations.load() == 1);
        CHECK(stats.lockViolations.load() == 1);
        CHECK(stats.ioViolations.load() == 1);
        CHECK(stats.peakAllocationSize.load() == 256);
    }

    // ── Guard exit restores the non-real-time state ────────────────────────
    CHECK(!isRealtimeAudioThread());
    AESTRA_TRACK_ALLOCATION(64, "post-scope");
    CHECK(stats.allocationViolations.load() == 1); // unchanged off-thread

    // ── Nesting parity with AudioEngine::processBlock's inner guard ────────
    // The callback guard and processBlock's guard nest; depth-counting (not a
    // bool) must keep the thread real-time until the OUTERMOST scope exits.
    {
        ScopedRealtimeAudioThread outer;
        {
            ScopedRealtimeAudioThread inner;
            CHECK(isRealtimeAudioThread());
        }
        CHECK(isRealtimeAudioThread()); // still RT after inner exits
    }
    CHECK(!isRealtimeAudioThread());

    if (g_failures == 0) {
        std::puts("RTGuardConsolidationTest: PASS");
        return 0;
    }
    std::fprintf(stderr, "RTGuardConsolidationTest: FAILED (%d checks)\n", g_failures);
    return 1;
}
