// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// RTGuardConsolidationTest
// ─────────────────────────────────────────────────────────────────────────────
// Regression guard for the real-time-guard consolidation (R1 #553, T-2 #257).
//
// The hazard this pins is "incomplete detection -> false confidence": a second
// RT-state flag, or a second violation counter, that only some call sites
// consult. Such a surface reports "clean" for thread state it never observed,
// which is worse than having no detection at all.
//
// Two rounds of consolidation removed the parallel machinery:
//
//   R1 (#553)  removed g_isAudioThread + AudioThreadGuard, a second thread-local
//              flag in Source/AudioThreadConstraints.h that the app-layer
//              callback path set while the engine path set the canonical one.
//
//   T-2 (#257) removed the rest of that header — the AudioThreadStats counter
//              block and the AESTRA_TRACK_ / AESTRA_ASSERT_ macro families —
//              once the call-site audit showed nothing in production read them.
//              Callback counting lives on AudioTelemetry; violation reporting
//              lives on reportRealtimeMisuse.
//
// What remains, and what this test pins, is one flag and one reporting call:
//   • g_realtimeAudioThreadDepth, marked only by ScopedRealtimeAudioThread
//   • reportRealtimeMisuse(apiName), dispatching to the installed handler
//
// If a parallel flag or counter is ever reintroduced, the contract asserted
// here is the one it would drift from.

#include "RealtimeThreadGuard.h" // canonical: the only RT flag and reporting call
#include "Core/MixerChannel.h"

#include <cstdio>
#include <string>

using namespace Aestra::Audio;

static int g_failures = 0;
static const char* g_firedApi = nullptr;
static int g_fireCount = 0;

static void onMisuse(const char* apiName) noexcept {
    g_firedApi = apiName;
    ++g_fireCount;
}

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL (line %d): %s\n", __LINE__, #cond);     \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

int main() {
    // ── Precondition: worker/UI thread is not real-time ────────────────────
    CHECK(!isRealtimeAudioThread());

    // ── The canonical guard marks and unmarks the thread ───────────────────
    {
        ScopedRealtimeAudioThread rtScope;
        CHECK(isRealtimeAudioThread());
    }
    CHECK(!isRealtimeAudioThread());

    // ── Nesting parity with AudioEngine::processBlock's inner guard ────────
    // The device-callback guard and processBlock's guard nest; depth-counting
    // (not a bool) must keep the thread real-time until the OUTERMOST scope
    // exits. A bool would clear on the inner exit and blind every check that
    // ran between there and the end of the callback.
    {
        ScopedRealtimeAudioThread outer;
        {
            ScopedRealtimeAudioThread inner;
            CHECK(isRealtimeAudioThread());
        }
        CHECK(isRealtimeAudioThread()); // still RT after inner exits
    }
    CHECK(!isRealtimeAudioThread());

    // ── reportRealtimeMisuse: the single reporting call ────────────────────
    // Its return value is the refusal signal call sites branch on, so the
    // off-thread/on-thread answers are as much a contract as the handler
    // dispatch is.
    {
        g_firedApi = nullptr;
        g_fireCount = 0;
        auto* previous = setRealtimeMisuseHandler(onMisuse);

        // Off the audio thread: not misuse, no report, no handler call.
        CHECK(reportRealtimeMisuse("Test::offThread") == false);
        CHECK(g_fireCount == 0);

        {
            ScopedRealtimeAudioThread rtScope;
            CHECK(reportRealtimeMisuse("Test::onThread") == true);
            CHECK(g_fireCount == 1);
            CHECK(g_firedApi != nullptr && std::string(g_firedApi) == "Test::onThread");
        }

        // Back off-thread: reporting goes quiet again.
        CHECK(reportRealtimeMisuse("Test::afterScope") == false);
        CHECK(g_fireCount == 1);

        // The setter returns the previous handler so scopes can restore it.
        auto* restored = setRealtimeMisuseHandler(previous);
        CHECK(restored == &onMisuse);
    }

    // ── Mixer mutation tripwires (T-2 coverage audit) ───────────────────────
    // The mixer setter surface must refuse mutations from the audio thread:
    // the graph snapshot is the audio thread's view, and a stray setter call
    // from inside the callback is a real-time violation that would otherwise
    // go unnoticed. Off the audio thread, behavior is unchanged.
    {
        MixerChannel channel("Tripwire Test", 1);
        channel.setMute(false);

        CHECK(!isRealtimeAudioThread());
        channel.setMute(true); // off-thread: normal mutation
        CHECK(channel.isMuted() == true);

        const char* firedApi = nullptr;
        g_firedApi = nullptr;
        setRealtimeMisuseHandler(onMisuse);

        channel.setMute(true); // off-thread: no report (state unchanged)
        CHECK(g_firedApi == nullptr);

        {
            ScopedRealtimeAudioThread rtScope;
            CHECK(isRealtimeAudioThread());
            channel.setMute(false); // on-thread: must report + refuse
        }
        firedApi = g_firedApi;
        CHECK(firedApi != nullptr && std::string(firedApi) == "MixerChannel::setMute");
        CHECK(channel.isMuted() == true); // mutation refused on the audio thread

        setRealtimeMisuseHandler(nullptr);
        channel.setMute(false); // off-thread again: works
        CHECK(channel.isMuted() == false);
    }

    if (g_failures == 0) {
        std::puts("RTGuardConsolidationTest: PASS");
        return 0;
    }
    std::fprintf(stderr, "RTGuardConsolidationTest: FAILED (%d checks)\n", g_failures);
    return 1;
}
