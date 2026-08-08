// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// BufferConfigContractTest — engine-side contract for AudioEngine::setBufferConfig (#731).
//
// setBufferConfig grows RT-visible storage (track buffers, graph scratch,
// plugin scratch), so it must never run while a stream callback is in flight.
// Hosts reconfiguring a running stream go through AudioDeviceManager's
// pre-restart config hook; this test pins the engine-side tripwire that makes
// a live-callback call observable and harmless:
//
//   * setBufferConfig is REFUSED (returns false) while a callback is in flight.
//   * setBufferConfig is ACCEPTED once no callback is running.

#include "Core/AudioEngine.h"
#include "Core/MixerChannel.h"
#include "Core/AudioGraph.h"
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
// AFTER the depth guard is active.
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
        // Input callback blocks inside processBlock, keeping the depth marker
        // non-zero until release.
        fx.engine.processBlock(fx.outputBuf.data(), fx.inputBuf.data(), kBufferFrames, 0.0);
    });

    while (!hold.entered.load()) {
        std::this_thread::yield();
    }

    const bool accepted = fx.engine.setBufferConfig(1024, kChannels);
    EXPECT_TRUE(!accepted);

    hold.release.store(true);
    rtThread.join();

    // No callback in flight anymore: the same call is accepted.
    EXPECT_TRUE(fx.engine.setBufferConfig(1024, kChannels));
}

void testAcceptedWhenStopped() {
    std::printf("[BufferConfigContractTest] setBufferConfig accepted when no callback is running...\n");
    Fixture fx;
    EXPECT_TRUE(fx.engine.setBufferConfig(1024, kChannels));
    EXPECT_TRUE(fx.engine.setBufferConfig(kBufferFrames, kChannels));
}

} // namespace

int main() {
    std::printf("=== BufferConfigContractTest (#731) ===\n");
    testRefusesWhileCallbackInFlight();
    testAcceptedWhenStopped();

    std::printf(g_failures == 0 ? "ALL PASSED\n" : "FAILURES: %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
