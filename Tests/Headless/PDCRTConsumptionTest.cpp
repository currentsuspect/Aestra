// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// PDCRTConsumptionTest — PDC v2 P4b.3 RT-path exercise.
//
// Verifies:
//   * processBlock can be called repeatedly with the per-edge compensation
//     state configured, without crashing or corrupting memory.
//   * The off-RT apply pass + RT consumption + double-buffer publish work
//     end-to-end through the public engine API.
//   * Toggling the latency state mid-rendering (recompute under simulated
//     playback) does not crash. This is the buffer-retirement protocol stress
//     test introduced in P4b.3.
//
// Full sample-accurate impulse-alignment verification is deferred to a later
// phase that adds a dedicated audio render harness. This file's scope is RT
// safety + smoke-level path execution.
//
// See Aestra-Internals: aestra-docs/PDC-v2-Design.md §12 P4b.3.

#include "Core/AudioEngine.h"
#include "Core/MixerChannel.h"
#include "Core/AudioGraph.h"
#include "Core/AudioGraphBuilder.h"
#include "Models/TrackManager.h"
#include "Plugin/EffectChain.h"

#include "PDCTestHelpers.h"

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

    // Publish a real RT graph so renderGraph() iterates actual tracks and the
    // compensation rings are consumed by mixAndMeterTrack().
    void publishGraph() { engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*trackManager)); }

    void renderBlocks(uint32_t n) {
        for (uint32_t b = 0; b < n; ++b) {
            std::fill(outputBuf.begin(), outputBuf.end(), 0.0f);
            engine.processBlock(outputBuf.data(), nullptr, kBufferFrames,
                                static_cast<double>(b * kBufferFrames) / kSampleRate);
        }
    }
};

void testRTPathDoesNotCrashWithCompensatedSend() {
    std::printf("[PDCRTConsumptionTest] RT path stable under repeated processBlock with compensated send...\n");
    Fixture fx;
    auto* source = fx.addTrack("source");
    auto* bus = fx.addTrack("bus");

    // Bus has a high-latency plugin; source sends to bus. Both share master
    // via mainOutputId, so the source -> bus send becomes a compensated edge.
    auto busPlug = std::make_shared<PDCTest::MockLatencyPlugin>(512, "BusLatency");
    EXPECT_TRUE(bus->getEffectChain().insertPlugin(0, busPlug));

    AudioRoute send;
    send.targetChannelId = bus->getChannelId();
    send.gain = 1.0f;
    send.sidechainOnly = false;
    source->addSend(send);

    fx.engine.calculateLatencyCompensation();

    // Confirm a non-trivial send-edge compensation was published before we
    // start beating on the RT path.
    const auto snap = fx.engine.getTrackEdgeDelaySnapshot(0);
    EXPECT_TRUE(snap.valid);
    EXPECT_TRUE(snap.sendEdgeDelays.size() >= 1);

    // Render several blocks. Pure smoke test: no crash, no memory corruption.
    fx.renderBlocks(32);
}

void testRTPathStableAcrossRecomputes() {
    std::printf("[PDCRTConsumptionTest] RT path tolerates off-RT recomputes between blocks (buffer retirement)...\n");
    Fixture fx;
    auto* source = fx.addTrack("source");
    auto* bus = fx.addTrack("bus");

    auto busPlug = std::make_shared<PDCTest::MockLatencyPlugin>(128, "BusLatency");
    EXPECT_TRUE(bus->getEffectChain().insertPlugin(0, busPlug));

    AudioRoute send;
    send.targetChannelId = bus->getChannelId();
    send.gain = 1.0f;
    send.sidechainOnly = false;
    source->addSend(send);

    // Drive several rounds of: render -> change plugin latency -> recompute.
    // Each recompute potentially grows the per-send buffer; the retirement
    // protocol must keep the previous allocation alive long enough for any
    // in-flight RT block.
    for (uint32_t iter = 0; iter < 8; ++iter) {
        const uint32_t newLatency = 128u + iter * 256u;
        busPlug->setLatencySamples(newLatency);
        fx.engine.calculateLatencyCompensation();
        fx.renderBlocks(4);
    }

    // After all the churn the topology should still report sane values.
    const auto finalSnap = fx.engine.getTrackEdgeDelaySnapshot(0);
    EXPECT_TRUE(finalSnap.valid);
    EXPECT_TRUE(finalSnap.sendEdgeDelays.size() >= 1);
}

void testCompensationGoesToZeroSafely() {
    std::printf("[PDCRTConsumptionTest] dropping comp to zero mid-render does not crash...\n");
    Fixture fx;
    auto* source = fx.addTrack("source");
    auto* bus = fx.addTrack("bus");

    auto busPlug = std::make_shared<PDCTest::MockLatencyPlugin>(1024, "BusLatency");
    EXPECT_TRUE(bus->getEffectChain().insertPlugin(0, busPlug));

    AudioRoute send;
    send.targetChannelId = bus->getChannelId();
    send.gain = 1.0f;
    send.sidechainOnly = false;
    source->addSend(send);

    fx.engine.calculateLatencyCompensation();
    fx.renderBlocks(8);

    // Remove the latency-introducing plugin → comp drops to zero.
    busPlug->setLatencySamples(0);
    fx.engine.calculateLatencyCompensation();

    const auto snap = fx.engine.getTrackEdgeDelaySnapshot(0);
    EXPECT_TRUE(snap.valid);
    EXPECT_TRUE(snap.sendEdgeDelays.size() >= 1);
    EXPECT_EQ(snap.sendEdgeDelays[0].compensationSamples, 0u);

    // Continue rendering — should now exercise the fast-path branch in the
    // consume loop (comp == 0).
    fx.renderBlocks(8);
}

void testRTPathBufferLayoutInvariants() {
    std::printf("[PDCRTConsumptionTest] buffer slots respect power-of-two capacity + headroom invariants...\n");
    Fixture fx;
    // To produce a NON-ZERO send-edge compensation, the source must have a
    // slower outgoing path that the send is being delayed to match. Setup:
    //   source -> busFast (intrinsic 100)  [send slot 0]
    //   source -> busSlow (intrinsic 2048) [send slot 1]
    //   source -> master  (mainOutputId, intrinsic 0)
    // downstream(source) = 2048. Send to busFast needs delay = 2048 - 100 = 1948.
    auto* source = fx.addTrack("source");
    auto* busFast = fx.addTrack("busFast");
    auto* busSlow = fx.addTrack("busSlow");
    auto fastPlug = std::make_shared<PDCTest::MockLatencyPlugin>(100, "Fast");
    auto slowPlug = std::make_shared<PDCTest::MockLatencyPlugin>(2048, "Slow");
    EXPECT_TRUE(busFast->getEffectChain().insertPlugin(0, fastPlug));
    EXPECT_TRUE(busSlow->getEffectChain().insertPlugin(0, slowPlug));

    AudioRoute sFast;
    sFast.targetChannelId = busFast->getChannelId();
    sFast.gain = 1.0f;
    sFast.sidechainOnly = false;
    source->addSend(sFast);

    AudioRoute sSlow;
    sSlow.targetChannelId = busSlow->getChannelId();
    sSlow.gain = 1.0f;
    sSlow.sidechainOnly = false;
    source->addSend(sSlow);

    fx.engine.calculateLatencyCompensation();
    const auto snap = fx.engine.getTrackEdgeDelaySnapshot(0);
    EXPECT_TRUE(snap.valid);
    EXPECT_TRUE(snap.sendEdgeDelays.size() >= 2);

    // Fast send is the one carrying delay; slow send is the slowest path itself.
    const auto& fastSlot = snap.sendEdgeDelays[0];
    const auto& slowSlot = snap.sendEdgeDelays[1];
    EXPECT_EQ(fastSlot.compensationSamples, 1948u);
    EXPECT_EQ(slowSlot.compensationSamples, 0u);

    // Buffer for the fast send must be power-of-two ≥ delay + one block.
    const uint32_t cap = fastSlot.capacityMask + 1u;
    EXPECT_TRUE((cap & (cap - 1u)) == 0u);
    EXPECT_TRUE(cap >= fastSlot.compensationSamples + kBufferFrames);
}

void testCompensationRingRetirementProtocol() {
    std::printf("[PDCRTConsumptionTest] ring generations: publish/ack monotonic, retired generations drained on ack...\n");
    Fixture fx;
    auto* latent = fx.addTrack("latent");
    auto* dry = fx.addTrack("dry");

    // dry is an audible source with no plugin, aligned against a latent sibling.
    // Solver math (matches PDCSolverPurityTest flat case): maxAlignment = 256,
    // dry's totalPathLatency = 0 => dry gets outputCompensationSamples = 256.
    auto latentPlug = std::make_shared<PDCTest::MockLatencyPlugin>(256, "LatentPlugin");
    EXPECT_TRUE(latent->getEffectChain().insertPlugin(0, latentPlug));
    fx.publishGraph();

    // First solve publishes generation 1. No RT blocks yet, so ack stays 0.
    fx.engine.calculateLatencyCompensation();
    auto snap = fx.engine.getTrackEdgeDelaySnapshot(1);
    EXPECT_TRUE(snap.valid);
    EXPECT_TRUE(snap.outputCompensationSamples > 0);
    EXPECT_EQ(snap.outputCompensationGeneration, 1u);
    EXPECT_EQ(snap.outputCompensationAckedGeneration, 0u);

    // Render one block: RT consumes gen 1 and acks it. The generated ring is
    // still the current descriptor (nothing retired yet), so ack == published.
    fx.renderBlocks(1);
    snap = fx.engine.getTrackEdgeDelaySnapshot(1);
    EXPECT_EQ(snap.outputCompensationGeneration, 1u);
    EXPECT_EQ(snap.outputCompensationAckedGeneration, 1u);

    // Change the delay: publish gen 2, retiring gen 1's ring. RT has NOT yet
    // consumed gen 2, so ack stays at 1 and gen 1 must remain held (retained,
    // not leaked). We cannot observe the internal retired-vector from here, but
    // the externally visible contract is: ack must lag published until RT runs.
    latentPlug->setLatencySamples(512);
    fx.engine.calculateLatencyCompensation();
    snap = fx.engine.getTrackEdgeDelaySnapshot(1);
    EXPECT_EQ(snap.outputCompensationGeneration, 2u);
    EXPECT_EQ(snap.outputCompensationAckedGeneration, 1u);

    // Let RT consume gen 2: ack must catch up to published.
    fx.renderBlocks(1);
    snap = fx.engine.getTrackEdgeDelaySnapshot(1);
    EXPECT_EQ(snap.outputCompensationGeneration, 2u);
    EXPECT_EQ(snap.outputCompensationAckedGeneration, 2u);

    // A few more generations, alternating publish/consume: generation must be
    // monotonic and ack must stay in lockstep with what RT has actually seen.
    latentPlug->setLatencySamples(384);
    fx.engine.calculateLatencyCompensation();
    fx.renderBlocks(1);
    latentPlug->setLatencySamples(768);
    fx.engine.calculateLatencyCompensation();
    fx.renderBlocks(1);
    snap = fx.engine.getTrackEdgeDelaySnapshot(1);
    EXPECT_EQ(snap.outputCompensationGeneration, 4u);
    EXPECT_EQ(snap.outputCompensationAckedGeneration, 4u);
    EXPECT_TRUE(snap.outputCompensationAckedGeneration <= snap.outputCompensationGeneration);
}

} // namespace

int main() {
    std::printf("=== PDCRTConsumptionTest (PDC v2 P4b.3 — RT path exercise) ===\n");

    testRTPathDoesNotCrashWithCompensatedSend();
    testRTPathStableAcrossRecomputes();
    testCompensationGoesToZeroSafely();
    testRTPathBufferLayoutInvariants();
    testCompensationRingRetirementProtocol();

    if (g_failures == 0) {
        std::printf("=== PDCRTConsumptionTest: all checks passed ===\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "=== PDCRTConsumptionTest: %d failure(s) ===\n", g_failures);
    return EXIT_FAILURE;
}
