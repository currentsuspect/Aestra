// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// PDCDisableClearsCompensationTest — regression cover for #684.
//
// Disabling latency compensation must actually stop compensating. Before the
// fix, setLatencyCompensationEnabled(false) never re-solved, the disabled
// early-return in calculateLatencyCompensation() never zeroed the applied
// per-node/per-edge delays, and the vestigial per-track enable flag left the
// RT gate as `compensationDelaySamples > 0`. Tracks therefore kept their last
// solved delay after PDC was switched off.
//
// The setter has no production callers today, so this was latent rather than
// something users could hear — but it is one UI hookup away from being
// audible, and the engine API is expected to honour the toggle regardless.
//
// These tests assert the invariant over *every* track and *every* edge slot
// rather than a sampled one, so a partially-cleared apply pass fails here.

#include "Core/AudioEngine.h"
#include "Core/AudioGraph.h"
#include "Core/MixerChannel.h"
#include "Models/TrackManager.h"
#include "PDCTestHelpers.h"
#include "Plugin/EffectChain.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
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

#define EXPECT_EQ(expr, expected)                                                           \
    do {                                                                                    \
        const auto _v = (expr);                                                             \
        const auto _e = (expected);                                                         \
        if (_v != _e) {                                                                     \
            reportFailure(#expr " == " #expected, __FILE__, __LINE__,                       \
                          "got " + std::to_string(_v) + " expected " + std::to_string(_e)); \
        }                                                                                   \
    } while (0)

#define EXPECT_TRUE(expr)                             \
    do {                                              \
        if (!(expr)) {                                \
            reportFailure(#expr, __FILE__, __LINE__); \
        }                                             \
    } while (0)

struct Fixture {
    std::shared_ptr<TrackManager> trackManager;
    AudioEngine engine;
    std::vector<float> outputBuf;

    Fixture() {
        trackManager = std::make_shared<TrackManager>();
        engine.setTrackManager(trackManager);
        engine.setSampleRate(kSampleRate);
        engine.setBufferConfig(kBufferFrames, kChannels);
        outputBuf.assign(static_cast<size_t>(kBufferFrames) * kChannels, 0.0f);
    }

    MixerChannel* addTrack(const std::string& name) { return trackManager->addChannel(name); }

    void renderBlocks(uint32_t n) {
        for (uint32_t b = 0; b < n; ++b) {
            std::fill(outputBuf.begin(), outputBuf.end(), 0.0f);
            engine.processBlock(outputBuf.data(), nullptr, kBufferFrames,
                                static_cast<double>(b * kBufferFrames) / kSampleRate);
        }
    }

    size_t trackCount() const { return trackManager->getChannelCount(); }

    /// Sum of every compensation value the RT path could act on: per-node
    /// output compensation plus every per-edge slot. Zero means nothing is
    /// being delayed anywhere.
    uint64_t totalAppliedCompensation() const {
        uint64_t total = 0;
        for (size_t i = 0; i < trackCount(); ++i) {
            const auto snap = engine.getTrackEdgeDelaySnapshot(i);
            if (!snap.valid) {
                continue;
            }
            total += snap.outputCompensationSamples;
            total += snap.mainOutEdgeDelay.compensationSamples;
            for (const auto& slot : snap.sendEdgeDelays) {
                total += slot.compensationSamples;
            }
        }
        return total;
    }

    /// Builds a graph that stages both kinds of compensation: a dry track that
    /// must be delayed to align with a latent sibling (per-node), and a send
    /// into a latent bus (per-edge).
    void buildCompensatedGraph() {
        auto* latent = addTrack("latent");
        addTrack("dry");
        auto* source = addTrack("source");
        auto* bus = addTrack("bus");

        auto latentPlug = std::make_shared<PDCTest::MockLatencyPlugin>(256, "LatentPlugin");
        EXPECT_TRUE(latent->getEffectChain().insertPlugin(0, latentPlug));

        auto busPlug = std::make_shared<PDCTest::MockLatencyPlugin>(512, "BusPlugin");
        EXPECT_TRUE(bus->getEffectChain().insertPlugin(0, busPlug));

        AudioRoute send;
        send.targetChannelId = bus->getChannelId();
        send.gain = 1.0f;
        send.sidechainOnly = false;
        source->addSend(send);
    }
};

void testDisableClearsEveryAppliedDelay() {
    std::printf("[PDCDisableClearsCompensationTest] disabling compensation clears every applied delay...\n");
    Fixture fx;
    fx.buildCompensatedGraph();
    fx.engine.calculateLatencyCompensation();

    // Precondition: the fixture really does stage compensation. Without this
    // the post-disable assertions would pass vacuously.
    const uint64_t compensated = fx.totalAppliedCompensation();
    EXPECT_TRUE(compensated > 0);
    EXPECT_TRUE(fx.engine.getMaxProjectLatency() > 0);

    fx.engine.setLatencyCompensationEnabled(false);

    // Every track, every edge slot — nothing may still be delaying audio.
    EXPECT_EQ(fx.totalAppliedCompensation(), static_cast<uint64_t>(0));
    EXPECT_EQ(fx.engine.getMaxProjectLatency(), 0u);
    EXPECT_TRUE(!fx.engine.isLatencyCompensationEnabled());

    // The diagnostic snapshot must not contradict the engine-wide toggle
    // either. This guards the #683 fix: the snapshot used to forward a
    // per-track flag that nothing wrote, so it reported compensation as
    // enabled on every track while the engine had it switched off. Checked
    // for every track, not a sampled one.
    for (size_t i = 0; i < fx.trackCount(); ++i) {
        const auto snap = fx.engine.getTrackEdgeDelaySnapshot(i);
        if (!snap.valid) {
            continue;
        }
        EXPECT_TRUE(snap.compensationEnabled == fx.engine.isLatencyCompensationEnabled());
    }
}

void testReEnableRestoresCompensation() {
    std::printf("[PDCDisableClearsCompensationTest] re-enabling compensation restores the solved delays...\n");
    Fixture fx;
    fx.buildCompensatedGraph();
    fx.engine.calculateLatencyCompensation();
    const uint64_t before = fx.totalAppliedCompensation();
    EXPECT_TRUE(before > 0);

    fx.engine.setLatencyCompensationEnabled(false);
    EXPECT_EQ(fx.totalAppliedCompensation(), static_cast<uint64_t>(0));

    // Clearing on disable is only correct if enabling puts it back. Nothing
    // about the graph changed, so the restored total must match exactly.
    fx.engine.setLatencyCompensationEnabled(true);
    EXPECT_EQ(fx.totalAppliedCompensation(), before);
    EXPECT_TRUE(fx.engine.isLatencyCompensationEnabled());
}

void testDisabledEngineKeepsRenderingSafely() {
    std::printf("[PDCDisableClearsCompensationTest] RT path stays stable across a mid-render toggle...\n");
    Fixture fx;
    fx.buildCompensatedGraph();
    fx.engine.calculateLatencyCompensation();

    fx.renderBlocks(8);
    fx.engine.setLatencyCompensationEnabled(false);
    fx.renderBlocks(8);
    EXPECT_EQ(fx.totalAppliedCompensation(), static_cast<uint64_t>(0));

    fx.engine.setLatencyCompensationEnabled(true);
    fx.renderBlocks(8);
    EXPECT_TRUE(fx.totalAppliedCompensation() > 0);
}

void testDisableIsIdempotentAndSurvivesResolve() {
    std::printf("[PDCDisableClearsCompensationTest] repeat disables and re-solves stay cleared...\n");
    Fixture fx;
    fx.buildCompensatedGraph();
    fx.engine.calculateLatencyCompensation();

    fx.engine.setLatencyCompensationEnabled(false);
    fx.engine.setLatencyCompensationEnabled(false);
    EXPECT_EQ(fx.totalAppliedCompensation(), static_cast<uint64_t>(0));

    // A recompute requested while disabled must not quietly re-apply delays.
    fx.engine.markLatencyDirty();
    fx.engine.calculateLatencyCompensation();
    EXPECT_EQ(fx.totalAppliedCompensation(), static_cast<uint64_t>(0));
    EXPECT_EQ(fx.engine.getMaxProjectLatency(), 0u);
}

} // namespace

int main() {
    std::printf("=== PDCDisableClearsCompensationTest (#684) ===\n");

    testDisableClearsEveryAppliedDelay();
    testReEnableRestoresCompensation();
    testDisabledEngineKeepsRenderingSafely();
    testDisableIsIdempotentAndSurvivesResolve();

    if (g_failures == 0) {
        std::printf("=== PDCDisableClearsCompensationTest: all checks passed ===\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "=== PDCDisableClearsCompensationTest: %d failure(s) ===\n", g_failures);
    return EXIT_FAILURE;
}
