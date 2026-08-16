// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// PDCBranchingConvergenceTest — PDC v2 P4a mandatory correctness gate for the
// reconvergence case. Solver level for P4a; gains audio-rendering coverage in
// P4b once the engine consumes per-edge compensation.
//
// Topology (from Aestra-Internals: aestra-docs/PDC-v2-Design.md §10 test #8):
//
//   TrackA (intrinsic 0)  --+--> Bus1 (intrinsic 100) --+--> Master (intrinsic 0)
//                            |                         |
//   TrackC (intrinsic 0)  --+                          |
//                            |                         |
//                            +--> Bus2 (intrinsic 300) +
//                            |                         |
//   TrackB (intrinsic 0)  ---/                         |
//
// TrackC sends to BOTH Bus1 and Bus2; the reconvergence at Master is the bug
// surface that flat-per-track PDC fails to handle. This test pins down the
// solver output the engine must apply (in P4b).
//
// Expected solver output:
//   * downstream(Master) = 0
//   * downstream(Bus1)   = intrinsic(Master) + downstream(Master) = 0
//   * downstream(Bus2)   = 0
//   * downstream(A)      = intrinsic(Bus1) + 0 = 100
//   * downstream(B)      = intrinsic(Bus2) + 0 = 300
//   * downstream(C)      = max(intrinsic(Bus1), intrinsic(Bus2)) = 300
//   * totalPath: A=100, B=300, C=300, Bus1=100, Bus2=300, Master=0
//   * projectAlignmentLatency = 300 (max over audible sources A, B, C)
//   * outputCompensation: A=200, B=0, C=0
//   * Edge compensation — the entire reason this test exists:
//       C->Bus1 = downstream(C) - (intrinsic(Bus1)+downstream(Bus1)) = 300 - 100 = 200
//       C->Bus2 = 0
//       A->Bus1 = 0
//       B->Bus2 = 0
//       Bus1->Master = 0
//       Bus2->Master = 0
//
// Bug classes this test catches if the solver regresses:
//   * Duplicated compensation: C delayed twice because it has two outgoing edges.
//   * Branch-local overdelay: Bus1's smaller latency leaking into C's Bus2 path.
//   * Reconvergence drift: Bus1 and Bus2 outputs misaligned at the master mixer.

#include "Core/LatencyTopology.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace Aestra::Audio;

namespace {

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

enum NodeName : uint32_t { kA = 0, kB = 1, kC = 2, kBus1 = 3, kBus2 = 4, kMaster = 5 };

LatencyGraph buildBranchingGraph() {
    LatencyGraph g;
    g.generation = 1;

    g.nodes.push_back({kA, 0, false, LatencyDomain::FullyCompensated});
    g.nodes.push_back({kB, 0, false, LatencyDomain::FullyCompensated});
    g.nodes.push_back({kC, 0, false, LatencyDomain::FullyCompensated});
    g.nodes.push_back({kBus1, 100, false, LatencyDomain::FullyCompensated});
    g.nodes.push_back({kBus2, 300, false, LatencyDomain::FullyCompensated});
    g.nodes.push_back({kMaster, 0, false, LatencyDomain::FullyCompensated});

    g.edges.push_back({kA, kBus1, false});
    g.edges.push_back({kB, kBus2, false});
    g.edges.push_back({kC, kBus1, false});
    g.edges.push_back({kC, kBus2, false});
    g.edges.push_back({kBus1, kMaster, false});
    g.edges.push_back({kBus2, kMaster, false});

    return g;
}

const SolvedLatencyTopology::NodeSolution* findNode(const SolvedLatencyTopology& t, uint32_t channelId) {
    for (const auto& n : t.nodes) {
        if (n.channelId == channelId) return &n;
    }
    return nullptr;
}

const SolvedLatencyTopology::EdgeSolution* findEdge(const SolvedLatencyTopology& t, uint32_t src, uint32_t dst) {
    for (const auto& e : t.edges) {
        if (e.srcNodeIdx == src && e.dstNodeIdx == dst) return &e;
    }
    return nullptr;
}

void testDownstreamPicksSlowestPathFromBranchingNode() {
    std::printf("[PDCBranchingConvergenceTest] downstream(C) follows the slowest outgoing branch (Bus2)...\n");
    const auto t = solveLatency(buildBranchingGraph());

    EXPECT_EQ(findNode(t, kMaster)->downstreamLatency, 0u);
    EXPECT_EQ(findNode(t, kBus1)->downstreamLatency, 0u);
    EXPECT_EQ(findNode(t, kBus2)->downstreamLatency, 0u);
    EXPECT_EQ(findNode(t, kA)->downstreamLatency, 100u);
    EXPECT_EQ(findNode(t, kB)->downstreamLatency, 300u);
    EXPECT_EQ(findNode(t, kC)->downstreamLatency, 300u); // KEY: max over outgoing
}

void testTotalPathLatency() {
    std::printf("[PDCBranchingConvergenceTest] totalPath = intrinsic + downstream per node...\n");
    const auto t = solveLatency(buildBranchingGraph());
    EXPECT_EQ(findNode(t, kA)->totalPathLatency, 100u);
    EXPECT_EQ(findNode(t, kB)->totalPathLatency, 300u);
    EXPECT_EQ(findNode(t, kC)->totalPathLatency, 300u);
    EXPECT_EQ(findNode(t, kBus1)->totalPathLatency, 100u);
    EXPECT_EQ(findNode(t, kBus2)->totalPathLatency, 300u);
    EXPECT_EQ(findNode(t, kMaster)->totalPathLatency, 0u);
}

void testProjectAlignmentLatency() {
    std::printf("[PDCBranchingConvergenceTest] project alignment = max source totalPath...\n");
    const auto t = solveLatency(buildBranchingGraph());
    EXPECT_EQ(t.projectAlignmentLatency, 300u);
}

void testOutputCompensationOnAudibleSourcesOnly() {
    std::printf("[PDCBranchingConvergenceTest] output compensation lives on A, B, C only...\n");
    const auto t = solveLatency(buildBranchingGraph());
    EXPECT_EQ(findNode(t, kA)->outputCompensationSamples, 200u); // 300 - 100
    EXPECT_EQ(findNode(t, kB)->outputCompensationSamples, 0u);
    EXPECT_EQ(findNode(t, kC)->outputCompensationSamples, 0u);
    // Aggregators must NOT receive output compensation.
    EXPECT_EQ(findNode(t, kBus1)->outputCompensationSamples, 0u);
    EXPECT_EQ(findNode(t, kBus2)->outputCompensationSamples, 0u);
    EXPECT_EQ(findNode(t, kMaster)->outputCompensationSamples, 0u);
}

void testEdgeCompensationDelaysFastBranch() {
    std::printf("[PDCBranchingConvergenceTest] reconvergence: C->Bus1 carries 200 samples of delay...\n");
    const auto t = solveLatency(buildBranchingGraph());

    // The critical assertion. Without per-edge compensation, C's contribution
    // would arrive at Master from Bus1 200 samples earlier than from Bus2.
    EXPECT_EQ(findEdge(t, kC, kBus1)->compensationSamples, 200u);
    EXPECT_EQ(findEdge(t, kC, kBus2)->compensationSamples, 0u);

    // Non-branching feeders need no edge compensation because their source has
    // only one outgoing audible edge (no reconvergence in their subtree).
    EXPECT_EQ(findEdge(t, kA, kBus1)->compensationSamples, 0u);
    EXPECT_EQ(findEdge(t, kB, kBus2)->compensationSamples, 0u);

    // Bus -> Master edges have nothing slower behind them.
    EXPECT_EQ(findEdge(t, kBus1, kMaster)->compensationSamples, 0u);
    EXPECT_EQ(findEdge(t, kBus2, kMaster)->compensationSamples, 0u);
}

void testReconvergenceAlignmentArithmetic() {
    std::printf("[PDCBranchingConvergenceTest] all paths from each source arrive at master at sample == projectAlignment...\n");
    const auto t = solveLatency(buildBranchingGraph());
    const uint32_t target = t.projectAlignmentLatency; // 300

    // Walk each (source, path) and assert arrival sample at master.
    // Arrival = outputComp(source) + intrinsic(source) + edgeComp(source->bus)
    //          + intrinsic(bus) + edgeComp(bus->master) + intrinsic(master)
    auto arrivalVia = [&](uint32_t src, uint32_t bus) -> uint32_t {
        const auto* s = findNode(t, src);
        const auto* b = findNode(t, bus);
        const auto* m = findNode(t, kMaster);
        const auto* e1 = findEdge(t, src, bus);
        const auto* e2 = findEdge(t, bus, kMaster);
        EXPECT_TRUE(s && b && m && e1 && e2);
        return s->outputCompensationSamples + s->intrinsicLatency + e1->compensationSamples +
               b->intrinsicLatency + e2->compensationSamples + m->intrinsicLatency;
    };

    EXPECT_EQ(arrivalVia(kA, kBus1), target);
    EXPECT_EQ(arrivalVia(kB, kBus2), target);
    EXPECT_EQ(arrivalVia(kC, kBus1), target);
    EXPECT_EQ(arrivalVia(kC, kBus2), target);
}

void testNoCycleWarning() {
    std::printf("[PDCBranchingConvergenceTest] clean DAG emits no cycle warnings...\n");
    const auto t = solveLatency(buildBranchingGraph());
    EXPECT_TRUE(t.warnings.empty());
}

// P9 (G6): the Master strip's insert-chain latency must surface in project
// and monitoring latency while cancelling out of per-track/per-edge
// compensation — it delays every path uniformly, so no track may be delayed
// further to "align" with it.
void testMasterIntrinsicLatency() {
    std::printf("[PDCBranchingConvergenceTest] master intrinsic latency: uniform, cancels from compensation...\n");
    constexpr uint32_t kMasterLatency = 128;

    LatencyGraph direct;
    direct.generation = 1;
    // channelId is a label; edges reference node *indices* (0, 1 here).
    direct.nodes.push_back({kA, 0, false, LatencyDomain::FullyCompensated});
    direct.nodes.push_back({kMaster, kMasterLatency, false, LatencyDomain::FullyCompensated});
    direct.edges.push_back({0, 1, false});

    const auto t = solveLatency(direct);
    EXPECT_EQ(findNode(t, kA)->totalPathLatency, kMasterLatency);
    EXPECT_EQ(t.projectAlignmentLatency, kMasterLatency);
    EXPECT_EQ(t.monitoringLatency, kMasterLatency);
    // The whole point: master latency must NOT delay the source track.
    EXPECT_EQ(findNode(t, kA)->outputCompensationSamples, 0u);
    EXPECT_EQ(findEdge(t, 0, 1)->compensationSamples, 0u);
    EXPECT_EQ(findNode(t, kMaster)->outputCompensationSamples, 0u);

    // Through a bus: A(0) -> Bus(100) -> Master(128). totalPath(A) = 228,
    // alignment = 228, compensation still zero everywhere on the path.
    LatencyGraph chained;
    chained.generation = 2;
    chained.nodes.push_back({kA, 0, false, LatencyDomain::FullyCompensated});
    chained.nodes.push_back({kBus1, 100, false, LatencyDomain::FullyCompensated});
    chained.nodes.push_back({kMaster, kMasterLatency, false, LatencyDomain::FullyCompensated});
    chained.edges.push_back({0, 1, false});
    chained.edges.push_back({1, 2, false});

    const auto tc = solveLatency(chained);
    EXPECT_EQ(findNode(tc, kA)->totalPathLatency, 228u);
    EXPECT_EQ(tc.projectAlignmentLatency, 228u);
    EXPECT_EQ(tc.monitoringLatency, 228u);
    EXPECT_EQ(findNode(tc, kA)->outputCompensationSamples, 0u);
    EXPECT_EQ(findNode(tc, kBus1)->outputCompensationSamples, 0u);
    EXPECT_EQ(findEdge(tc, 0, 1)->compensationSamples, 0u);
    EXPECT_EQ(findEdge(tc, 1, 2)->compensationSamples, 0u);
}

} // namespace

int main() {
    std::printf("=== PDCBranchingConvergenceTest (PDC v2 P4a — solver level, correctness gate) ===\n");

    testDownstreamPicksSlowestPathFromBranchingNode();
    testTotalPathLatency();
    testProjectAlignmentLatency();
    testOutputCompensationOnAudibleSourcesOnly();
    testEdgeCompensationDelaysFastBranch();
    testReconvergenceAlignmentArithmetic();
    testNoCycleWarning();
    testMasterIntrinsicLatency();

    if (g_failures == 0) {
        std::printf("=== PDCBranchingConvergenceTest: all checks passed ===\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "=== PDCBranchingConvergenceTest: %d failure(s) ===\n", g_failures);
    return EXIT_FAILURE;
}
