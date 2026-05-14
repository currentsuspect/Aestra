// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// PDCFlatChainTest — PDC v2 Phase 1 scaffolding test.
//
// Asserts public-API contracts for the existing PDC v1 implementation:
//   * Engine reports zero project latency for empty effect chains.
//   * Inserting a plugin that reports N samples of latency into one of two tracks
//     raises the reported max project latency to N after recompute.
//   * Disabling PDC drops the reported max to zero; re-enabling restores it.
//   * The reported value tracks plugin latency changes through recompute.
//
// This test must remain green across all PDC v2 phases (P2 introduces
// LatencyGraph/SolvedLatencyTopology, P4 switches to graph-aware solving, etc.).
// Public-API semantics may not regress.
//
// See AestraDocs/PDC-v2-Design.md §10 test #1 and §12 P1.

#include "Core/AudioEngine.h"
#include "Core/MixerChannel.h"
#include "Models/TrackManager.h"
#include "Plugin/EffectChain.h"

#include "PDCTestHelpers.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kBufferFrames = 256;
constexpr uint32_t kChannels = 2;
constexpr uint32_t kMockLatencySamples = 256;

int g_failures = 0;

void reportFailure(const char* expr, const char* file, int line, const std::string& detail = {}) {
    std::fprintf(stderr, "[FAIL] %s (%s:%d)", expr, file, line);
    if (!detail.empty()) {
        std::fprintf(stderr, " — %s", detail.c_str());
    }
    std::fprintf(stderr, "\n");
    ++g_failures;
}

#define EXPECT_EQ(expr, expected)                                                                                      \
    do {                                                                                                               \
        const auto _v = (expr);                                                                                        \
        const auto _e = (expected);                                                                                    \
        if (_v != _e) {                                                                                                \
            reportFailure(#expr " == " #expected, __FILE__, __LINE__,                                                  \
                          "got " + std::to_string(_v) + " expected " + std::to_string(_e));                            \
        }                                                                                                              \
    } while (0)

#define EXPECT_TRUE(expr)                                                                                              \
    do {                                                                                                               \
        if (!(expr)) {                                                                                                 \
            reportFailure(#expr, __FILE__, __LINE__);                                                                  \
        }                                                                                                              \
    } while (0)

struct Fixture {
    std::shared_ptr<TrackManager> trackManager;
    std::shared_ptr<PDCTest::MockLatencyPlugin> plugin;
    AudioEngine engine;
    MixerChannel* trackZero{nullptr};
    MixerChannel* trackOne{nullptr};

    Fixture() {
        trackManager = std::make_shared<TrackManager>();
        trackZero = trackManager->addChannel("Track 0 (with latency)");
        trackOne = trackManager->addChannel("Track 1 (clean)");

        engine.setTrackManager(trackManager);
        engine.setSampleRate(kSampleRate);
        engine.setBufferConfig(kBufferFrames, kChannels);

        plugin = std::make_shared<PDCTest::MockLatencyPlugin>(kMockLatencySamples, "FlatChainMock");
    }
};

void testInitialStateIsZero() {
    std::printf("[PDCFlatChainTest] initial state reports zero latency...\n");
    Fixture fx;
    EXPECT_TRUE(fx.engine.isLatencyCompensationEnabled());
    EXPECT_EQ(fx.engine.getMaxProjectLatency(), 0u);
}

void testLatencyPluginRaisesMax() {
    std::printf("[PDCFlatChainTest] inserting a latency-reporting plugin raises the max...\n");
    Fixture fx;
    EXPECT_TRUE(fx.trackZero != nullptr);
    EXPECT_TRUE(fx.trackZero->getEffectChain().insertPlugin(0, fx.plugin));

    fx.engine.calculateLatencyCompensation();
    EXPECT_EQ(fx.engine.getMaxProjectLatency(), kMockLatencySamples);
}

void testDisableThenEnable() {
    std::printf("[PDCFlatChainTest] disable PDC zeroes reported max; re-enable restores...\n");
    Fixture fx;
    EXPECT_TRUE(fx.trackZero->getEffectChain().insertPlugin(0, fx.plugin));
    fx.engine.calculateLatencyCompensation();
    EXPECT_EQ(fx.engine.getMaxProjectLatency(), kMockLatencySamples);

    fx.engine.setLatencyCompensationEnabled(false);
    fx.engine.calculateLatencyCompensation();
    EXPECT_EQ(fx.engine.getMaxProjectLatency(), 0u);
    EXPECT_TRUE(!fx.engine.isLatencyCompensationEnabled());

    fx.engine.setLatencyCompensationEnabled(true);
    fx.engine.calculateLatencyCompensation();
    EXPECT_EQ(fx.engine.getMaxProjectLatency(), kMockLatencySamples);
    EXPECT_TRUE(fx.engine.isLatencyCompensationEnabled());
}

void testLatencyChangePropagates() {
    std::printf("[PDCFlatChainTest] mock plugin latency change reflects through recompute...\n");
    Fixture fx;
    EXPECT_TRUE(fx.trackZero->getEffectChain().insertPlugin(0, fx.plugin));
    fx.engine.calculateLatencyCompensation();
    EXPECT_EQ(fx.engine.getMaxProjectLatency(), kMockLatencySamples);

    fx.plugin->setLatencySamples(kMockLatencySamples * 2);
    fx.engine.calculateLatencyCompensation();
    EXPECT_EQ(fx.engine.getMaxProjectLatency(), kMockLatencySamples * 2);

    fx.plugin->setLatencySamples(0);
    fx.engine.calculateLatencyCompensation();
    EXPECT_EQ(fx.engine.getMaxProjectLatency(), 0u);
}

void testCleanTrackDoesNotInflate() {
    std::printf("[PDCFlatChainTest] a clean track does not contribute to the max...\n");
    Fixture fx;
    // Plugin only on track 0; track 1 has nothing.
    EXPECT_TRUE(fx.trackZero->getEffectChain().insertPlugin(0, fx.plugin));
    fx.engine.calculateLatencyCompensation();
    EXPECT_EQ(fx.engine.getMaxProjectLatency(), kMockLatencySamples);

    // Sanity: track 1 has no plugins; ensure we did not somehow double-count.
    EXPECT_EQ(fx.trackOne->getEffectChain().getTotalLatency(), 0u);
}

} // namespace

int main() {
    std::printf("=== PDCFlatChainTest (PDC v2 P1 scaffolding) ===\n");

    testInitialStateIsZero();
    testLatencyPluginRaisesMax();
    testDisableThenEnable();
    testLatencyChangePropagates();
    testCleanTrackDoesNotInflate();

    if (g_failures == 0) {
        std::printf("=== PDCFlatChainTest: all checks passed ===\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "=== PDCFlatChainTest: %d failure(s) ===\n", g_failures);
    return EXIT_FAILURE;
}
