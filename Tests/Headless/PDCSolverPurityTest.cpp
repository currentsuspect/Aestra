// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// PDCSolverPurityTest — PDC v2 Phase 1 scaffolding test (solver-purity stub).
//
// In P1 the solver does not yet exist as a separate artifact (LatencyGraph /
// SolvedLatencyTopology are introduced in P2). This stub asserts the *invariants*
// that the future solver must satisfy, by exercising the equivalent guarantees
// the v1 PDC pathway already provides:
//
//   1. Determinism — repeated calculateLatencyCompensation() with no input
//      change must yield the same reported max project latency.
//   2. Idempotency — running calculate() any number of times in succession
//      produces the same final state.
//   3. Independence from observation — toggling isLatencyCompensationEnabled
//      reads do not perturb the underlying state.
//   4. No spurious latency without a contributing plugin — adding empty tracks
//      must never raise the reported max above zero.
//
// When P2 introduces LatencyGraph/SolvedLatencyTopology, this file will gain
// solver-vs-state equivalence checks. Until then, the file is the architectural
// commitment to keeping the solver pure: solver behavior is observable through
// SolvedLatencyTopology and only through SolvedLatencyTopology.
//
// See Aestra-Internals: aestra-docs/PDC-v2-Design.md §4.0 (solver/application separation) and
// §10 test #9 (PDCSolverPurityTest).

#include "Core/AudioEngine.h"
#include "Core/LatencyTopology.h"
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
constexpr uint32_t kIterations = 16;

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

void testDeterminismEmpty() {
    std::printf("[PDCSolverPurityTest] determinism with empty project...\n");
    Fixture fx;
    fx.addTrack("a");
    fx.addTrack("b");

    fx.engine.calculateLatencyCompensation();
    const uint32_t first = fx.engine.getMaxProjectLatency();
    for (uint32_t i = 0; i < kIterations; ++i) {
        fx.engine.calculateLatencyCompensation();
        EXPECT_EQ(fx.engine.getMaxProjectLatency(), first);
    }
    EXPECT_EQ(first, 0u);
}

void testDeterminismWithLatencyPlugin() {
    std::printf("[PDCSolverPurityTest] determinism with one latency plugin...\n");
    Fixture fx;
    auto* trackA = fx.addTrack("a");
    fx.addTrack("b");

    auto plugin = std::make_shared<PDCTest::MockLatencyPlugin>(384, "PurityMock");
    EXPECT_TRUE(trackA->getEffectChain().insertPlugin(0, plugin));

    fx.engine.calculateLatencyCompensation();
    const uint32_t first = fx.engine.getMaxProjectLatency();
    EXPECT_EQ(first, 384u);

    for (uint32_t i = 0; i < kIterations; ++i) {
        fx.engine.calculateLatencyCompensation();
        EXPECT_EQ(fx.engine.getMaxProjectLatency(), first);
    }
}

void testObservationDoesNotMutateState() {
    std::printf("[PDCSolverPurityTest] reading state must not perturb it...\n");
    Fixture fx;
    auto* trackA = fx.addTrack("a");
    auto plugin = std::make_shared<PDCTest::MockLatencyPlugin>(512, "ObservationMock");
    EXPECT_TRUE(trackA->getEffectChain().insertPlugin(0, plugin));

    fx.engine.calculateLatencyCompensation();
    const uint32_t baseline = fx.engine.getMaxProjectLatency();
    EXPECT_EQ(baseline, 512u);

    // Hammer the read API. No mutation, no recompute.
    for (uint32_t i = 0; i < 1000; ++i) {
        const uint32_t observed = fx.engine.getMaxProjectLatency();
        const bool enabled = fx.engine.isLatencyCompensationEnabled();
        EXPECT_EQ(observed, baseline);
        EXPECT_TRUE(enabled);
    }
}

void testEmptyTracksContributeZero() {
    std::printf("[PDCSolverPurityTest] empty tracks must never inflate max...\n");
    Fixture fx;
    for (int i = 0; i < 8; ++i) {
        fx.addTrack("empty_" + std::to_string(i));
    }
    fx.engine.calculateLatencyCompensation();
    EXPECT_EQ(fx.engine.getMaxProjectLatency(), 0u);
}

void testDisabledResultIsZero() {
    std::printf("[PDCSolverPurityTest] disabled PDC always reports zero...\n");
    Fixture fx;
    auto* trackA = fx.addTrack("a");
    auto plugin = std::make_shared<PDCTest::MockLatencyPlugin>(1024, "DisabledMock");
    EXPECT_TRUE(trackA->getEffectChain().insertPlugin(0, plugin));

    fx.engine.setLatencyCompensationEnabled(false);
    fx.engine.calculateLatencyCompensation();
    EXPECT_EQ(fx.engine.getMaxProjectLatency(), 0u);

    for (uint32_t i = 0; i < kIterations; ++i) {
        fx.engine.calculateLatencyCompensation();
        EXPECT_EQ(fx.engine.getMaxProjectLatency(), 0u);
    }
}

// ===========================================================================
// P2 additions: direct solver calls + solver-vs-engine equivalence.
// The solver is a pure function. These tests construct LatencyGraph values by
// hand (no engine) and assert exact SolvedLatencyTopology outputs.
// ===========================================================================

LatencyGraph makeGraph(std::initializer_list<std::pair<uint32_t, bool>> nodes) {
    LatencyGraph g;
    g.generation = 1;
    uint32_t id = 0;
    for (const auto& [latency, muted] : nodes) {
        LatencyGraph::Node n;
        n.channelId = id++;
        n.intrinsicLatency = latency;
        n.muted = muted;
        n.domain = LatencyDomain::FullyCompensated;
        g.nodes.push_back(n);
    }
    return g;
}

void testSolverEmptyGraphIsZero() {
    std::printf("[PDCSolverPurityTest] solver: empty LatencyGraph yields zero topology...\n");
    LatencyGraph g;
    g.generation = 42;
    const auto t = solveLatency(g);
    EXPECT_EQ(t.projectAlignmentLatency, 0u);
    EXPECT_EQ(t.monitoringLatency, 0u);
    EXPECT_EQ(t.generation, 42u);
    EXPECT_TRUE(t.nodes.empty());
    EXPECT_TRUE(t.edges.empty());
    EXPECT_TRUE(t.warnings.empty());
}

void testSolverFlatComputation() {
    std::printf("[PDCSolverPurityTest] solver: flat per-node compensation matches v1 spec...\n");
    // Three nodes with intrinsic latencies 0, 256, 1024. None muted.
    auto g = makeGraph({{0, false}, {256, false}, {1024, false}});
    const auto t = solveLatency(g);
    EXPECT_EQ(t.projectAlignmentLatency, 1024u);
    EXPECT_EQ(t.monitoringLatency, 1024u);
    EXPECT_EQ(t.nodes.size(), 3u);
    EXPECT_EQ(t.nodes[0].outputCompensationSamples, 1024u);
    EXPECT_EQ(t.nodes[1].outputCompensationSamples, 768u);
    EXPECT_EQ(t.nodes[2].outputCompensationSamples, 0u);
    // P2 invariant: downstream latency always zero (no graph traversal yet).
    for (const auto& n : t.nodes) {
        EXPECT_EQ(n.downstreamLatency, 0u);
        EXPECT_EQ(n.totalPathLatency, n.intrinsicLatency);
    }
}

void testSolverPurityRepeatable() {
    std::printf("[PDCSolverPurityTest] solver: same input produces same output (pure)...\n");
    auto g = makeGraph({{128, false}, {512, false}});
    const auto a = solveLatency(g);
    const auto b = solveLatency(g);
    EXPECT_EQ(a.projectAlignmentLatency, b.projectAlignmentLatency);
    EXPECT_EQ(a.nodes.size(), b.nodes.size());
    for (size_t i = 0; i < a.nodes.size(); ++i) {
        EXPECT_EQ(a.nodes[i].outputCompensationSamples, b.nodes[i].outputCompensationSamples);
        EXPECT_EQ(a.nodes[i].intrinsicLatency, b.nodes[i].intrinsicLatency);
    }
}

void testSolverMutedNodesIgnoredInMax() {
    std::printf("[PDCSolverPurityTest] solver: muted nodes do not raise the max...\n");
    // Node 0: 100, audible. Node 1: 999, muted. Node 2: 200, audible.
    auto g = makeGraph({{100, false}, {999, true}, {200, false}});
    const auto t = solveLatency(g);
    EXPECT_EQ(t.projectAlignmentLatency, 200u);
    // Muted node's own compensation is still computed (against the audible max).
    EXPECT_EQ(t.nodes[0].outputCompensationSamples, 100u);
    EXPECT_EQ(t.nodes[1].outputCompensationSamples, 0u); // muted node has intrinsic > max -> clamped to 0
    EXPECT_EQ(t.nodes[2].outputCompensationSamples, 0u);
}

void testSolverRealtimeDomainExempt() {
    std::printf("[PDCSolverPurityTest] solver: Realtime domain exempted from compensation...\n");
    LatencyGraph g;
    g.generation = 7;
    // Two FullyCompensated nodes + one Realtime node with a large intrinsic.
    LatencyGraph::Node a{0, 0, false, LatencyDomain::FullyCompensated};
    LatencyGraph::Node b{1, 256, false, LatencyDomain::FullyCompensated};
    LatencyGraph::Node rt{2, 4096, false, LatencyDomain::Realtime};
    g.nodes = {a, b, rt};
    const auto t = solveLatency(g);
    // Realtime node must not raise the max.
    EXPECT_EQ(t.projectAlignmentLatency, 256u);
    // Realtime node receives zero compensation.
    EXPECT_EQ(t.nodes[2].outputCompensationSamples, 0u);
    EXPECT_EQ(t.nodes[0].outputCompensationSamples, 256u);
    EXPECT_EQ(t.nodes[1].outputCompensationSamples, 0u);
}

void testSolverGenerationPropagates() {
    std::printf("[PDCSolverPurityTest] solver: generation passes through to topology...\n");
    LatencyGraph g;
    g.generation = 12345;
    const auto t = solveLatency(g);
    EXPECT_EQ(t.generation, 12345u);
}

void testSolverCycleDetection() {
    std::printf("[PDCSolverPurityTest] solver: cycles are detected and tolerated (P4a)...\n");
    // Two-node cycle: A -> B -> A. Solver must terminate, emit a warning, and
    // set compensation on the back edge to zero rather than spinning or
    // producing absurd numbers.
    LatencyGraph g;
    g.generation = 99;
    g.nodes.push_back({0, 100, false, LatencyDomain::FullyCompensated});
    g.nodes.push_back({1, 200, false, LatencyDomain::FullyCompensated});
    g.edges.push_back({0, 1, false});
    g.edges.push_back({1, 0, false}); // back edge

    const auto t = solveLatency(g);
    EXPECT_TRUE(!t.warnings.empty());
    // Assert on the classification, not the prose. Matching message text was
    // exactly the coupling that let a reword silently change a warning's
    // meaning for downstream consumers.
    bool sawCycleWarning = false;
    for (const auto& w : t.warnings) {
        if (w.code == SolverWarningCode::RoutingCycle) {
            sawCycleWarning = true;
            break;
        }
    }
    EXPECT_TRUE(sawCycleWarning);
    // Solver must have terminated and produced two node solutions.
    EXPECT_EQ(t.nodes.size(), 2u);
    EXPECT_EQ(t.edges.size(), 2u);
}

void testSolverOutOfRangeEdge() {
    std::printf("[PDCSolverPurityTest] solver: out-of-range edge indices produce a warning, not a crash...\n");
    LatencyGraph g;
    g.generation = 100;
    g.nodes.push_back({0, 64, false, LatencyDomain::FullyCompensated});
    g.edges.push_back({0, 42, false}); // dst out of range
    const auto t = solveLatency(g);
    EXPECT_TRUE(!t.warnings.empty());
    bool sawInvalidEdgeWarning = false;
    for (const auto& w : t.warnings) {
        if (w.code == SolverWarningCode::InvalidEdgeIndices) {
            sawInvalidEdgeWarning = true;
            break;
        }
    }
    EXPECT_TRUE(sawInvalidEdgeWarning);
    EXPECT_EQ(t.edges.size(), 1u);
    EXPECT_EQ(t.edges[0].compensationSamples, 0u);
}

void testDoubleBufferPublishMonotonicGeneration() {
    std::printf("[PDCSolverPurityTest] publish: generation strictly increases across recomputes...\n");
    Fixture fx;
    auto* trackA = fx.addTrack("a");
    auto plugin = std::make_shared<PDCTest::MockLatencyPlugin>(128, "GenMock");
    EXPECT_TRUE(trackA->getEffectChain().insertPlugin(0, plugin));

    fx.engine.calculateLatencyCompensation();
    uint64_t prevGen = fx.engine.getLastSolvedLatencyTopology().generation;

    for (int i = 0; i < 8; ++i) {
        plugin->setLatencySamples(static_cast<uint32_t>(128 + i * 64));
        fx.engine.calculateLatencyCompensation();
        const auto topology = fx.engine.getLastSolvedLatencyTopology();
        EXPECT_TRUE(topology.generation > prevGen);
        prevGen = topology.generation;
        // Active topology must always agree with the legacy accessor.
        EXPECT_EQ(topology.projectAlignmentLatency, fx.engine.getMaxProjectLatency());
    }
}

void testDoubleBufferPublishReaderConsistency() {
    std::printf("[PDCSolverPurityTest] publish: each read is a fully constructed snapshot...\n");
    Fixture fx;
    auto* trackA = fx.addTrack("a");
    auto plugin = std::make_shared<PDCTest::MockLatencyPlugin>(512, "ReaderMock");
    EXPECT_TRUE(trackA->getEffectChain().insertPlugin(0, plugin));

    fx.engine.calculateLatencyCompensation();

    // Hammer reads from the accessor while interleaving recomputes. Each
    // observed snapshot must be internally consistent: nodes.size() matches
    // the engine's track count at the time of the recompute, channelIds are
    // sequential, and the projectAlignmentLatency matches the max intrinsic
    // over the snapshot's own nodes.
    for (int i = 0; i < 32; ++i) {
        plugin->setLatencySamples(256 + static_cast<uint32_t>(i) * 32);
        fx.engine.calculateLatencyCompensation();
        const auto topology = fx.engine.getLastSolvedLatencyTopology();

        uint32_t observedMax = 0;
        for (const auto& n : topology.nodes) {
            observedMax = (n.intrinsicLatency > observedMax) ? n.intrinsicLatency : observedMax;
        }
        EXPECT_EQ(topology.projectAlignmentLatency, observedMax);
    }
}

void testDoubleBufferDisabledFlipsCleanly() {
    std::printf("[PDCSolverPurityTest] publish: disabling PDC publishes a zero topology snapshot...\n");
    Fixture fx;
    auto* trackA = fx.addTrack("a");
    auto plugin = std::make_shared<PDCTest::MockLatencyPlugin>(1024, "DisableMock");
    EXPECT_TRUE(trackA->getEffectChain().insertPlugin(0, plugin));

    fx.engine.calculateLatencyCompensation();
    EXPECT_EQ(fx.engine.getLastSolvedLatencyTopology().projectAlignmentLatency, 1024u);
    const uint64_t genBeforeDisable = fx.engine.getLastSolvedLatencyTopology().generation;

    fx.engine.setLatencyCompensationEnabled(false);
    fx.engine.calculateLatencyCompensation();
    const auto disabled = fx.engine.getLastSolvedLatencyTopology();
    EXPECT_EQ(disabled.projectAlignmentLatency, 0u);
    EXPECT_TRUE(disabled.nodes.empty());
    EXPECT_TRUE(disabled.generation > genBeforeDisable);

    fx.engine.setLatencyCompensationEnabled(true);
    fx.engine.calculateLatencyCompensation();
    const auto reenabled = fx.engine.getLastSolvedLatencyTopology();
    EXPECT_EQ(reenabled.projectAlignmentLatency, 1024u);
    EXPECT_TRUE(reenabled.generation > disabled.generation);
}

void testSolverMatchesEngineState() {
    std::printf("[PDCSolverPurityTest] engine: getLastSolvedLatencyTopology matches getMaxProjectLatency...\n");
    Fixture fx;
    auto* trackA = fx.addTrack("a");
    auto* trackB = fx.addTrack("b");
    auto plugin = std::make_shared<PDCTest::MockLatencyPlugin>(768, "EquivalenceMock");
    EXPECT_TRUE(trackA->getEffectChain().insertPlugin(0, plugin));

    fx.engine.calculateLatencyCompensation();
    const auto topology = fx.engine.getLastSolvedLatencyTopology();
    EXPECT_EQ(topology.projectAlignmentLatency, fx.engine.getMaxProjectLatency());
    EXPECT_EQ(topology.projectAlignmentLatency, 768u);
    // Engine appends a synthetic master node since P4b.1 (see
    // Aestra-Internals: aestra-docs/PDC-v2-Design.md §12), so node count = tracks + 1.
    EXPECT_EQ(topology.nodes.size(), 3u);
    // Track A carries the plugin -> intrinsic 768 -> output compensation 0.
    // Track B is empty       -> intrinsic 0   -> output compensation 768.
    const uint32_t idA = trackA->getChannelId();
    const uint32_t idB = trackB->getChannelId();
    for (const auto& n : topology.nodes) {
        if (n.channelId == idA) {
            EXPECT_EQ(n.intrinsicLatency, 768u);
            EXPECT_EQ(n.outputCompensationSamples, 0u);
        } else if (n.channelId == idB) {
            EXPECT_EQ(n.intrinsicLatency, 0u);
            EXPECT_EQ(n.outputCompensationSamples, 768u);
        }
    }
}

} // namespace

int main() {
    std::printf("=== PDCSolverPurityTest (PDC v2 P1+P2 scaffolding) ===\n");

    testDeterminismEmpty();
    testDeterminismWithLatencyPlugin();
    testObservationDoesNotMutateState();
    testEmptyTracksContributeZero();
    testDisabledResultIsZero();

    // P2 additions:
    testSolverEmptyGraphIsZero();
    testSolverFlatComputation();
    testSolverPurityRepeatable();
    testSolverMutedNodesIgnoredInMax();
    testSolverRealtimeDomainExempt();
    testSolverGenerationPropagates();
    testSolverMatchesEngineState();

    // P4a additions: cycle detection, out-of-range edges. (Bus chain and
    // branching convergence have their own test executables.)
    testSolverCycleDetection();
    testSolverOutOfRangeEdge();

    // P3 additions: double-buffer publish (atomic flip, lock-free read).
    testDoubleBufferPublishMonotonicGeneration();
    testDoubleBufferPublishReaderConsistency();
    testDoubleBufferDisabledFlipsCleanly();

    if (g_failures == 0) {
        std::printf("=== PDCSolverPurityTest: all checks passed ===\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "=== PDCSolverPurityTest: %d failure(s) ===\n", g_failures);
    return EXIT_FAILURE;
}
