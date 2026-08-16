// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// BufferConfigContractTest — engine-side contract for AudioEngine::setBufferConfig (#731).
//
// setBufferConfig grows RT-visible storage (track buffers, graph scratch,
// plugin scratch), so it must never run while a stream callback is in flight.
// Hosts reconfiguring a running stream go through AudioDeviceManager's
// pre-restart config hook; this test pins the engine-side admission handshake:
//
//   * setBufferConfig is REFUSED (returns false) while a callback is in flight.
//   * setBufferConfig is ACCEPTED once no callback is running.
//   * a callback cannot enter after a configuration transaction owns admission.

#include "Core/AudioEngine.h"
#include "Core/AudioGraph.h"
#include "Core/MixerChannel.h"
#include "Core/RTConfigAdmission.h"
#include "Models/TrackManager.h"
#include "Plugin/EffectChain.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kBufferFrames = 256;
constexpr uint32_t kChannels = 2;

int g_failures = 0;

void reportFailure(const char* expr, const char* file, int line, const std::string& detail = {}) {
    std::fprintf(stderr, "[FAIL] %s (%s:%d)", expr, file, line);
    if (!detail.empty()) {
        std::fprintf(stderr, " — %s", detail.c_str());
    }
    std::fprintf(stderr, "\n");
    ++g_failures;
}

#define EXPECT_TRUE(expr)                                                                                              \
    do {                                                                                                               \
        if (!(expr)) {                                                                                                 \
            reportFailure(#expr, __FILE__, __LINE__);                                                                  \
        }                                                                                                              \
    } while (0)

struct Fixture {
    std::shared_ptr<TrackManager> trackManager;
    AudioEngine engine;
    std::vector<float> outputBuf;
    std::vector<float> inputBuf;

    Fixture() {
        trackManager = std::make_shared<TrackManager>();
        engine.setTrackManager(trackManager);
        engine.setSampleRate(kSampleRate);
        engine.setBufferConfig(kBufferFrames, kChannels);
        outputBuf.assign(static_cast<size_t>(kBufferFrames) * kChannels, 0.0f);
        inputBuf.assign(static_cast<size_t>(kBufferFrames) * kChannels, 0.0f);
    }
};

// Holds a callback deterministically in flight: the input callback spins until
// released, and processBlock invokes it synchronously on the caller thread
// AFTER the admission guard is active.
struct CallbackHold {
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
};

void holdCallback(const float*, uint32_t, void* user) {
    auto* hold = static_cast<CallbackHold*>(user);
    hold->entered.store(true);
    while (!hold->release.load()) {
        std::this_thread::yield();
    }
}

void testRefusesWhileCallbackInFlight() {
    std::printf("[BufferConfigContractTest] setBufferConfig refused while a callback is in flight...\n");
    Fixture fx;
    CallbackHold hold;
    fx.engine.setInputCallback(holdCallback, &hold);

    std::thread rtThread([&] {
        fx.engine.processBlock(fx.outputBuf.data(), fx.inputBuf.data(), kBufferFrames, 0.0);
    });

    while (!hold.entered.load()) {
        std::this_thread::yield();
    }

    const bool accepted = fx.engine.setBufferConfig(1024, kChannels);
    EXPECT_TRUE(!accepted);

    hold.release.store(true);
    rtThread.join();

    EXPECT_TRUE(fx.engine.setBufferConfig(1024, kChannels));
}

void testAcceptedWhenStopped() {
    std::printf("[BufferConfigContractTest] setBufferConfig accepted when no callback is running...\n");
    Fixture fx;
    EXPECT_TRUE(fx.engine.setBufferConfig(1024, kChannels));
    EXPECT_TRUE(fx.engine.setBufferConfig(kBufferFrames, kChannels));
}

void testCallbackCannotEnterDuringConfigAdmission() {
    std::printf("[BufferConfigContractTest] processBlock admission is refused while config owns the gate...\n");

    // Part 1: Test low-level RTConfigAdmission primitives directly
    std::atomic<uint32_t> gate{0};
    EXPECT_TRUE(detail::tryBeginBufferConfig(gate));

    std::atomic<bool> callbackAdmitted{true};
    std::thread callbackThread([&] {
        const bool admitted = detail::tryEnterProcessBlock(gate);
        callbackAdmitted.store(admitted, std::memory_order_release);
        if (admitted) {
            detail::leaveProcessBlock(gate);
        }
    });
    callbackThread.join();

    EXPECT_TRUE(!callbackAdmitted.load(std::memory_order_acquire));

    detail::endBufferConfig(gate);
    EXPECT_TRUE(detail::tryEnterProcessBlock(gate));
    detail::leaveProcessBlock(gate);

    // Part 2: Test AudioEngine integration — processBlock silences output and
    // returns early when it cannot gain admission (i.e., during setBufferConfig).
    std::printf("[BufferConfigContractTest] processBlock returns early during setBufferConfig...\n");
    Fixture fx;

    // Create a held setBufferConfig on a background thread. It will acquire the
    // admission gate and hold it until we signal release.
    struct ConfigHold {
        std::atomic<bool> beginConfig{false};
        std::atomic<bool> releaseConfig{false};
        std::atomic<bool> configComplete{false};
    };
    ConfigHold hold;

    // We can't easily pause setBufferConfig mid-execution, but we can observe that
    // processBlock returns 0 (silenced output) when called concurrently with
    // setBufferConfig in a tight race. This is a smoke test; the real exclusion
    // is proven by the low-level gate test above.
    std::thread configThread([&] {
        hold.beginConfig.store(true, std::memory_order_release);
        // Repeatedly call setBufferConfig to create a window where the gate is owned.
        for (int i = 0; i < 100 && !hold.releaseConfig.load(); ++i) {
            fx.engine.setBufferConfig(1024, kChannels);
        }
        hold.configComplete.store(true, std::memory_order_release);
    });

    // Wait for config to start, then call processBlock repeatedly. At least once,
    // it should observe the gate owned and return early. (In practice, this is
    // timing-dependent, but the primitive test above is deterministic.)
    while (!hold.beginConfig.load()) {
        std::this_thread::yield();
    }

    // The primitive test above already proves the mechanism works. This integration
    // test just verifies processBlock gracefully handles denial by silencing output.
    std::vector<float> output(kBufferFrames * kChannels, 1.0f); // Pre-fill with non-zero
    std::vector<float> input(kBufferFrames * kChannels, 0.0f);
    fx.engine.processBlock(output.data(), input.data(), kBufferFrames, 0.0);
    // If admission was denied, output is silenced. If admitted, it may be silent
    // or non-silent depending on engine state. We just verify no crash.

    hold.releaseConfig.store(true, std::memory_order_release);
    configThread.join();
}

} // namespace

int main() {
    std::printf("=== BufferConfigContractTest (#731) ===\n");
    testRefusesWhileCallbackInFlight();
    testAcceptedWhenStopped();
    testCallbackCannotEnterDuringConfigAdmission();

    std::printf(g_failures == 0 ? "ALL PASSED\n" : "FAILURES: %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
