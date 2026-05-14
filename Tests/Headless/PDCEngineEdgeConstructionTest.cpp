// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// PDCEngineEdgeConstructionTest — PDC v2 P4b.1 verification that the engine
// builds `LatencyGraph::edges` from real MixerChannel routing state
// (mainOutputId + sends) and passes them through the solver into
// SolvedLatencyTopology.
//
// Engine RT path is unchanged in P4b.1: it still applies only per-node
// outputCompensation. P4b.2 will wire the edge compensation values produced
// here into per-send ring buffers in processBlock.
//
// See AestraDocs/PDC-v2-Design.md §12 P4b.1.

#include "Core/AudioEngine.h"
#include "Core/LatencyTopology.h"
#include "Core/MixerChannel.h"
#include "Core/AudioGraph.h"
#include "Models/TrackManager.h"
#include "Plugin/EffectChain.h"

#include "PDCTestHelpers.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kBufferFrames = 256;
constexpr uint32_t kChannels = 2;
constexpr uint32_t kMasterSentinelId = 0xFFFFFFFFu;

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

    Fixture() {
        trackManager = std::make_shared<TrackManager>();
        engine.setTrackManager(trackManager);
        engine.setSampleRate(kSampleRate);
        engine.setBufferConfig(kBufferFrames, kChannels);
    }

    MixerChannel* addTrack(const std::string& name) { return trackManager->addChannel(name); }
};

// Counts edges from src->dst with the given sidechain flag in the solved
// topology. We translate channelIds back to topology indices because indices
// in SolvedLatencyTopology are graph-local, not channel-global.
size_t countEdges(const SolvedLatencyTopology& topology,
                  uint32_t srcChannelId,
                  uint32_t dstChannelId,
                  bool wantSidechain) {
    // Build channelId -> nodeIdx from the topology's node list.
    size_t srcIdx = topology.nodes.size();
    size_t dstIdx = topology.nodes.size();
    for (size_t i = 0; i < topology.nodes.size(); ++i) {
        if (topology.nodes[i].channelId == srcChannelId) srcIdx = i;
        if (topology.nodes[i].channelId == dstChannelId) dstIdx = i;
    }
    if (srcIdx == topology.nodes.size() || dstIdx == topology.nodes.size()) return 0;
    size_t count = 0;
    for (const auto& e : topology.edges) {
        if (e.srcNodeIdx == srcIdx && e.dstNodeIdx == dstIdx && e.sidechain == wantSidechain) ++count;
    }
    return count;
}

void testMasterNodeAdded() {
    std::printf("[PDCEngineEdgeConstructionTest] engine appends a synthetic master node...\n");
    Fixture fx;
    fx.addTrack("a");
    fx.addTrack("b");
    fx.engine.calculateLatencyCompensation();
    const auto topology = fx.engine.getLastSolvedLatencyTopology();
    EXPECT_EQ(topology.nodes.size(), 3u); // 2 tracks + master
    // Last node should be the master sentinel.
    EXPECT_EQ(topology.nodes.back().channelId, kMasterSentinelId);
}

void testImplicitMainOutputEdges() {
    std::printf("[PDCEngineEdgeConstructionTest] every track gets an implicit mainOutputId edge to master...\n");
    Fixture fx;
    auto* t0 = fx.addTrack("a");
    auto* t1 = fx.addTrack("b");
    auto* t2 = fx.addTrack("c");
    fx.engine.calculateLatencyCompensation();
    const auto topology = fx.engine.getLastSolvedLatencyTopology();

    EXPECT_EQ(topology.edges.size(), 3u);
    EXPECT_EQ(countEdges(topology, t0->getChannelId(), kMasterSentinelId, false), 1u);
    EXPECT_EQ(countEdges(topology, t1->getChannelId(), kMasterSentinelId, false), 1u);
    EXPECT_EQ(countEdges(topology, t2->getChannelId(), kMasterSentinelId, false), 1u);
}

void testExplicitSendsAreAdded() {
    std::printf("[PDCEngineEdgeConstructionTest] explicit sends produce additional edges...\n");
    Fixture fx;
    auto* t0 = fx.addTrack("a");
    auto* t1 = fx.addTrack("b");
    auto* t2 = fx.addTrack("c");

    AudioRoute s01;
    s01.targetChannelId = t1->getChannelId();
    s01.gain = 0.5f;
    s01.sidechainOnly = false;
    t0->addSend(s01);

    AudioRoute s02;
    s02.targetChannelId = t2->getChannelId();
    s02.gain = 0.5f;
    s02.sidechainOnly = false;
    t0->addSend(s02);

    fx.engine.calculateLatencyCompensation();
    const auto topology = fx.engine.getLastSolvedLatencyTopology();

    // Expected: 3 mainOutput edges to master + 2 explicit sends from t0 = 5.
    EXPECT_EQ(topology.edges.size(), 5u);
    EXPECT_EQ(countEdges(topology, t0->getChannelId(), t1->getChannelId(), false), 1u);
    EXPECT_EQ(countEdges(topology, t0->getChannelId(), t2->getChannelId(), false), 1u);
}

void testMutedSendIsExcluded() {
    std::printf("[PDCEngineEdgeConstructionTest] muted sends are excluded from the audible graph...\n");
    Fixture fx;
    auto* t0 = fx.addTrack("a");
    auto* t1 = fx.addTrack("b");

    AudioRoute send;
    send.targetChannelId = t1->getChannelId();
    send.gain = 0.5f;
    send.mute = true; // explicitly muted
    send.sidechainOnly = false;
    t0->addSend(send);

    fx.engine.calculateLatencyCompensation();
    const auto topology = fx.engine.getLastSolvedLatencyTopology();
    // Only the 2 mainOutput edges (t0->master, t1->master). The muted send is
    // not present in the graph.
    EXPECT_EQ(topology.edges.size(), 2u);
    EXPECT_EQ(countEdges(topology, t0->getChannelId(), t1->getChannelId(), false), 0u);
}

void testSidechainSendPreservesFlag() {
    std::printf("[PDCEngineEdgeConstructionTest] sidechain-only sends carry the sidechain flag...\n");
    Fixture fx;
    auto* t0 = fx.addTrack("a");
    auto* t1 = fx.addTrack("b");

    AudioRoute send;
    send.targetChannelId = t1->getChannelId();
    send.gain = 1.0f;
    send.sidechainOnly = true;
    t0->addSend(send);

    fx.engine.calculateLatencyCompensation();
    const auto topology = fx.engine.getLastSolvedLatencyTopology();
    EXPECT_EQ(countEdges(topology, t0->getChannelId(), t1->getChannelId(), true), 1u);

    // Sidechain edges currently get zero compensation (P5 / G2 territory).
    for (const auto& e : topology.edges) {
        if (e.sidechain) {
            EXPECT_EQ(e.compensationSamples, 0u);
        }
    }
}

void testBusChainAlignment() {
    std::printf("[PDCEngineEdgeConstructionTest] end-to-end bus-chain: T0(256) -> T1(512 bus) -> master...\n");
    Fixture fx;
    auto* t0 = fx.addTrack("source");
    auto* t1 = fx.addTrack("bus");

    // Plugin latencies.
    auto srcPlugin = std::make_shared<PDCTest::MockLatencyPlugin>(256, "SrcLatency");
    auto busPlugin = std::make_shared<PDCTest::MockLatencyPlugin>(512, "BusLatency");
    EXPECT_TRUE(t0->getEffectChain().insertPlugin(0, srcPlugin));
    EXPECT_TRUE(t1->getEffectChain().insertPlugin(0, busPlugin));

    // Route t0 -> t1 instead of -> master.
    t0->setMainOutputId(t1->getChannelId());

    fx.engine.calculateLatencyCompensation();
    const auto topology = fx.engine.getLastSolvedLatencyTopology();

    // Locate the t0 node in the topology.
    const SolvedLatencyTopology::NodeSolution* t0Sol = nullptr;
    const SolvedLatencyTopology::NodeSolution* t1Sol = nullptr;
    for (const auto& n : topology.nodes) {
        if (n.channelId == t0->getChannelId()) t0Sol = &n;
        if (n.channelId == t1->getChannelId()) t1Sol = &n;
    }
    EXPECT_TRUE(t0Sol != nullptr);
    EXPECT_TRUE(t1Sol != nullptr);

    // Expected math:
    //   downstream(master) = 0; downstream(t1) = intrinsic(master)+0 = 0
    //   downstream(t0) = intrinsic(t1) + downstream(t1) = 512.
    //   totalPath(t0) = 256 + 512 = 768. (slowest audible source)
    //   t1 is a non-source (t0 sends to it via mainOutputId), so outputComp(t1) = 0.
    EXPECT_EQ(t0Sol->intrinsicLatency, 256u);
    EXPECT_EQ(t0Sol->downstreamLatency, 512u);
    EXPECT_EQ(t0Sol->totalPathLatency, 768u);
    EXPECT_EQ(t0Sol->outputCompensationSamples, 0u); // slowest source
    EXPECT_EQ(t1Sol->outputCompensationSamples, 0u); // non-source

    EXPECT_EQ(topology.projectAlignmentLatency, 768u);
    EXPECT_EQ(fx.engine.getMaxProjectLatency(), 768u);
}

void testV1FlatBehaviorPreservedForNoSendsCase() {
    std::printf("[PDCEngineEdgeConstructionTest] no-sends projects behave exactly as v1 (flat)...\n");
    Fixture fx;
    auto* t0 = fx.addTrack("a");
    auto* t1 = fx.addTrack("b");
    auto plugin = std::make_shared<PDCTest::MockLatencyPlugin>(1000, "FlatMock");
    EXPECT_TRUE(t0->getEffectChain().insertPlugin(0, plugin));

    fx.engine.calculateLatencyCompensation();
    const auto topology = fx.engine.getLastSolvedLatencyTopology();
    EXPECT_EQ(topology.projectAlignmentLatency, 1000u);

    // Each track has its mainOutputId -> master implicit edge but no incoming
    // edges, so both are sources. Compensations are v1-equivalent.
    for (const auto& n : topology.nodes) {
        if (n.channelId == t0->getChannelId()) {
            EXPECT_EQ(n.outputCompensationSamples, 0u);
        } else if (n.channelId == t1->getChannelId()) {
            EXPECT_EQ(n.outputCompensationSamples, 1000u);
        }
    }
}

// ===========================================================================
// P4b.2 — per-edge compensation lands in TrackRTState slots.
// Verifies that the off-RT apply pass writes the right delay values and
// preallocates power-of-two-sized buffers. RT-side consumption is P4b.3.
// ===========================================================================

bool isPowerOfTwo(uint32_t v) {
    return v != 0 && (v & (v - 1)) == 0;
}

void testBranchingApplyPassWritesPerEdgeDelays() {
    std::printf("[PDCEngineEdgeConstructionTest] P4b.2 apply pass: per-edge delays for branching topology...\n");
    Fixture fx;

    // Layout (track index : intrinsic latency)
    //   0: TrackA  (0)  --> Bus1 (mainOutputId)
    //   1: TrackB  (0)  --> Bus2 (mainOutputId)
    //   2: TrackC  (0)  --> master (mainOutputId), sends -> Bus1, Bus2
    //   3: Bus1    (100) -> master (mainOutputId)
    //   4: Bus2    (300) -> master (mainOutputId)
    auto* trackA = fx.addTrack("a");
    auto* trackB = fx.addTrack("b");
    auto* trackC = fx.addTrack("c");
    auto* bus1   = fx.addTrack("bus1");
    auto* bus2   = fx.addTrack("bus2");
    EXPECT_TRUE(trackA && trackB && trackC && bus1 && bus2);

    auto bus1Plug = std::make_shared<PDCTest::MockLatencyPlugin>(100, "Bus1Plug");
    auto bus2Plug = std::make_shared<PDCTest::MockLatencyPlugin>(300, "Bus2Plug");
    EXPECT_TRUE(bus1->getEffectChain().insertPlugin(0, bus1Plug));
    EXPECT_TRUE(bus2->getEffectChain().insertPlugin(0, bus2Plug));

    trackA->setMainOutputId(bus1->getChannelId());
    trackB->setMainOutputId(bus2->getChannelId());
    // trackC's mainOutputId stays as master (0xFFFFFFFF default).

    AudioRoute sCBus1;
    sCBus1.targetChannelId = bus1->getChannelId();
    sCBus1.gain = 1.0f;
    sCBus1.sidechainOnly = false;
    trackC->addSend(sCBus1);

    AudioRoute sCBus2;
    sCBus2.targetChannelId = bus2->getChannelId();
    sCBus2.gain = 1.0f;
    sCBus2.sidechainOnly = false;
    trackC->addSend(sCBus2);

    fx.engine.calculateLatencyCompensation();

    // Expected solver math (from PDCBranchingConvergenceTest, with engine-built
    // mainOutputId edges):
    //   projectAlignmentLatency = 300
    //   A: mainOutEdge -> Bus1 comp = 0 (only outgoing path)
    //   B: mainOutEdge -> Bus2 comp = 0
    //   C: mainOutEdge -> master comp = 300, send[0] -> Bus1 comp = 200, send[1] -> Bus2 comp = 0
    //   Bus1: mainOutEdge -> master comp = 0
    //   Bus2: mainOutEdge -> master comp = 0
    EXPECT_EQ(fx.engine.getMaxProjectLatency(), 300u);

    const auto snapA = fx.engine.getTrackEdgeDelaySnapshot(0);
    const auto snapB = fx.engine.getTrackEdgeDelaySnapshot(1);
    const auto snapC = fx.engine.getTrackEdgeDelaySnapshot(2);
    const auto snapBus1 = fx.engine.getTrackEdgeDelaySnapshot(3);
    const auto snapBus2 = fx.engine.getTrackEdgeDelaySnapshot(4);
    EXPECT_TRUE(snapA.valid && snapB.valid && snapC.valid && snapBus1.valid && snapBus2.valid);

    EXPECT_EQ(snapA.mainOutEdgeDelay.compensationSamples, 0u);
    EXPECT_EQ(snapB.mainOutEdgeDelay.compensationSamples, 0u);
    EXPECT_EQ(snapBus1.mainOutEdgeDelay.compensationSamples, 0u);
    EXPECT_EQ(snapBus2.mainOutEdgeDelay.compensationSamples, 0u);

    // C — the canonical branching-source case.
    EXPECT_EQ(snapC.mainOutEdgeDelay.compensationSamples, 300u);
    EXPECT_EQ(snapC.sendEdgeDelays.size(), 2u);
    EXPECT_EQ(snapC.sendEdgeDelays[0].compensationSamples, 200u); // -> Bus1
    EXPECT_EQ(snapC.sendEdgeDelays[1].compensationSamples, 0u);   // -> Bus2

    // Buffer capacity = power-of-two, at least delay + one block of headroom.
    EXPECT_TRUE(isPowerOfTwo(snapC.mainOutEdgeDelay.capacityMask + 1));
    EXPECT_TRUE(snapC.mainOutEdgeDelay.bufferBytes >= sizeof(float) * 2u * 300u);
    EXPECT_TRUE(isPowerOfTwo(snapC.sendEdgeDelays[0].capacityMask + 1));
    EXPECT_TRUE(snapC.sendEdgeDelays[0].bufferBytes >= sizeof(float) * 2u * 200u);
}

void testApplyPassClearsStaleSendDelays() {
    std::printf("[PDCEngineEdgeConstructionTest] P4b.2 apply pass: removing a send zeros its slot...\n");
    Fixture fx;
    auto* trackC = fx.addTrack("c");
    auto* bus = fx.addTrack("bus");
    auto plug = std::make_shared<PDCTest::MockLatencyPlugin>(512, "BusPlug");
    EXPECT_TRUE(bus->getEffectChain().insertPlugin(0, plug));

    AudioRoute send;
    send.targetChannelId = bus->getChannelId();
    send.gain = 1.0f;
    send.sidechainOnly = false;
    trackC->addSend(send);

    fx.engine.calculateLatencyCompensation();
    const auto first = fx.engine.getTrackEdgeDelaySnapshot(0);
    EXPECT_TRUE(first.valid);
    // C has one send to Bus. C is also a source with mainOutputId -> master.
    // downstream(C) = max(intrinsic(master), intrinsic(bus)) = 512.
    // C->master comp = 512; C->bus send comp = 0.
    EXPECT_EQ(first.mainOutEdgeDelay.compensationSamples, 512u);
    EXPECT_EQ(first.sendEdgeDelays.size(), 1u);
    EXPECT_EQ(first.sendEdgeDelays[0].compensationSamples, 0u);

    // Remove the send and recompute. The slot's compensation must go to zero.
    trackC->removeSend(0);
    fx.engine.calculateLatencyCompensation();
    const auto second = fx.engine.getTrackEdgeDelaySnapshot(0);
    EXPECT_TRUE(second.valid);
    // No more sends; main output now goes through master only. C downstream = 0, no compensation needed.
    EXPECT_EQ(second.mainOutEdgeDelay.compensationSamples, 0u);
    if (!second.sendEdgeDelays.empty()) {
        // Old slot is preserved (capacity kept for stability) but comp is zero.
        EXPECT_EQ(second.sendEdgeDelays[0].compensationSamples, 0u);
    }
}

} // namespace

int main() {
    std::printf("=== PDCEngineEdgeConstructionTest (PDC v2 P4b.1 + P4b.2) ===\n");

    testMasterNodeAdded();
    testImplicitMainOutputEdges();
    testExplicitSendsAreAdded();
    testMutedSendIsExcluded();
    testSidechainSendPreservesFlag();
    testBusChainAlignment();
    testV1FlatBehaviorPreservedForNoSendsCase();

    // P4b.2 — per-edge compensation lands in TrackRTState.
    testBranchingApplyPassWritesPerEdgeDelays();
    testApplyPassClearsStaleSendDelays();

    if (g_failures == 0) {
        std::printf("=== PDCEngineEdgeConstructionTest: all checks passed ===\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "=== PDCEngineEdgeConstructionTest: %d failure(s) ===\n", g_failures);
    return EXIT_FAILURE;
}
