// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// PDCBusChainTest — PDC v2 P4a solver-level correctness gate for bus-chain
// latency propagation (G1 partial: solver only; engine RT path in P4b).
//
// Topology:
//   TrackA (intrinsic 256) ---+
//                              +--> Bus (intrinsic 512) --> Master (intrinsic 0)
//   TrackB (intrinsic 0)   ---+
//
// Expected solver output:
//   * downstream(Bus)   = 0 (only edge to Master, Master intrinsic 0)
//   * downstream(A)     = intrinsic(Bus) + downstream(Bus) = 512
//   * downstream(B)     = 512
//   * totalPath(A)      = 256 + 512 = 768
//   * totalPath(B)      = 0   + 512 = 512
//   * projectAlignment  = 768 (B not a source-with-downstream-bigger than A's totalPath)
//   * outputCompensation(A) = 0    (slowest source)
//   * outputCompensation(B) = 256  (catches up to A)
//   * outputCompensation(Bus / Master) = 0 (not audible sources)
//   * edge compensation on every edge = 0 (no reconvergence at multi-output nodes)
//
// See AestraDocs/PDC-v2-Design.md §10 test #2.

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

// Indexed by NodeName for readability.
enum NodeName : uint32_t { kA = 0, kB = 1, kBus = 2, kMaster = 3 };

LatencyGraph buildBusChainGraph() {
    LatencyGraph g;
    g.generation = 1;

    LatencyGraph::Node a{kA, 256, false, LatencyDomain::FullyCompensated};
    LatencyGraph::Node b{kB, 0, false, LatencyDomain::FullyCompensated};
    LatencyGraph::Node bus{kBus, 512, false, LatencyDomain::FullyCompensated};
    LatencyGraph::Node master{kMaster, 0, false, LatencyDomain::FullyCompensated};
    g.nodes = {a, b, bus, master};

    LatencyGraph::Edge eA{kA, kBus, false};
    LatencyGraph::Edge eB{kB, kBus, false};
    LatencyGraph::Edge eBus{kBus, kMaster, false};
    g.edges = {eA, eB, eBus};

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

void testDownstreamPropagatesThroughBus() {
    std::printf("[PDCBusChainTest] downstream latency propagates from leaves through the bus...\n");
    const auto t = solveLatency(buildBusChainGraph());

    const auto* a = findNode(t, kA);
    const auto* b = findNode(t, kB);
    const auto* bus = findNode(t, kBus);
    const auto* master = findNode(t, kMaster);
    EXPECT_TRUE(a && b && bus && master);

    EXPECT_EQ(master->downstreamLatency, 0u);
    EXPECT_EQ(bus->downstreamLatency, 0u);    // Bus -> Master (Master intrinsic 0)
    EXPECT_EQ(a->downstreamLatency, 512u);    // A -> Bus (intrinsic 512) -> Master
    EXPECT_EQ(b->downstreamLatency, 512u);

    EXPECT_EQ(a->totalPathLatency, 768u);
    EXPECT_EQ(b->totalPathLatency, 512u);
    EXPECT_EQ(bus->totalPathLatency, 512u);
    EXPECT_EQ(master->totalPathLatency, 0u);
}

void testProjectAlignmentReflectsSlowestSource() {
    std::printf("[PDCBusChainTest] project alignment = max totalPath across audible sources...\n");
    const auto t = solveLatency(buildBusChainGraph());
    // Only A and B are audible sources (no incoming audible edges). Bus and
    // Master are aggregators and must NOT participate in the max.
    EXPECT_EQ(t.projectAlignmentLatency, 768u);
    EXPECT_EQ(t.monitoringLatency, 768u);
}

void testOutputCompensationOnSourcesOnly() {
    std::printf("[PDCBusChainTest] output compensation lives on audible sources, not on buses...\n");
    const auto t = solveLatency(buildBusChainGraph());

    EXPECT_EQ(findNode(t, kA)->outputCompensationSamples, 0u);
    EXPECT_EQ(findNode(t, kB)->outputCompensationSamples, 256u);
    EXPECT_EQ(findNode(t, kBus)->outputCompensationSamples, 0u);
    EXPECT_EQ(findNode(t, kMaster)->outputCompensationSamples, 0u);
}

void testNoEdgeCompensationForSharedBusFeeders() {
    std::printf("[PDCBusChainTest] shared-bus feeders need no edge compensation (handled at source)...\n");
    const auto t = solveLatency(buildBusChainGraph());

    EXPECT_EQ(findEdge(t, kA, kBus)->compensationSamples, 0u);
    EXPECT_EQ(findEdge(t, kB, kBus)->compensationSamples, 0u);
    EXPECT_EQ(findEdge(t, kBus, kMaster)->compensationSamples, 0u);

    // No warnings expected for a clean bus chain.
    EXPECT_TRUE(t.warnings.empty());
}

} // namespace

int main() {
    std::printf("=== PDCBusChainTest (PDC v2 P4a — solver level) ===\n");

    testDownstreamPropagatesThroughBus();
    testProjectAlignmentReflectsSlowestSource();
    testOutputCompensationOnSourcesOnly();
    testNoEdgeCompensationForSharedBusFeeders();

    if (g_failures == 0) {
        std::printf("=== PDCBusChainTest: all checks passed ===\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "=== PDCBusChainTest: %d failure(s) ===\n", g_failures);
    return EXIT_FAILURE;
}
