// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// LatencyTopology — PDC v2 pure solver.
//
// P2: flat per-node solve (v1-equivalent).
// P4a: graph-aware DFS + per-edge compensation + three-color cycle detection.
//
// See Aestra-Internals: aestra-docs/PDC-v2-Design.md §4 for the architectural contract.

#include "Core/LatencyTopology.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

namespace Aestra {
namespace Audio {

namespace {

// Realtime-domain nodes contribute zero intrinsic latency to downstream
// computations (§4.5). Centralized to keep the rule consistent.
inline uint32_t contributedIntrinsic(const LatencyGraph::Node& node) {
    return (node.domain == LatencyDomain::Realtime) ? 0u : node.intrinsicLatency;
}

} // namespace

SolvedLatencyTopology solveLatency(const LatencyGraph& graph) {
    SolvedLatencyTopology topology;
    topology.generation = graph.generation;

    const size_t N = graph.nodes.size();
    topology.nodes.resize(N);

    // Initialize node solutions from input. downstream / totalPath / output
    // compensation are filled in later passes.
    for (size_t i = 0; i < N; ++i) {
        topology.nodes[i].channelId = graph.nodes[i].channelId;
        topology.nodes[i].intrinsicLatency = graph.nodes[i].intrinsicLatency;
        topology.nodes[i].downstreamLatency = 0;
        topology.nodes[i].totalPathLatency = 0;
        topology.nodes[i].outputCompensationSamples = 0;
    }

    // Build adjacency: outgoing edge indices per source node + per-node
    // "has audible incoming edge" flag. Sidechain edges do not count for
    // either downstream latency or audible-source identification (§5 / G2;
    // sidechain compensation lands in P5).
    std::vector<std::vector<size_t>> outEdges(N);
    std::vector<bool> hasIncomingAudible(N, false);
    std::vector<bool> edgeOutOfRange(graph.edges.size(), false);
    bool anyEdgeOutOfRange = false;
    for (size_t e = 0; e < graph.edges.size(); ++e) {
        const auto& edge = graph.edges[e];
        if (edge.srcNodeIdx >= N || edge.dstNodeIdx >= N) {
            edgeOutOfRange[e] = true;
            anyEdgeOutOfRange = true;
            continue;
        }
        outEdges[edge.srcNodeIdx].push_back(e);
        if (!edge.sidechainOnly) {
            hasIncomingAudible[edge.dstNodeIdx] = true;
        }
    }
    if (anyEdgeOutOfRange) {
        topology.warnings.push_back(
            {SolverWarningCode::InvalidEdgeIndices,
             "LatencyGraph contains edge(s) with out-of-range node indices; their compensation was set to zero"});
    }

    // Post-order DFS computing downstreamLatency per node, with three-color
    // cycle detection. White = 0, Gray = 1, Black = 2.
    std::vector<uint8_t> color(N, 0);
    std::vector<bool> nodeOnCycle(N, false);
    bool cycleDetected = false;

    std::function<void(size_t)> visit = [&](size_t idx) {
        if (color[idx] == 2) return;
        if (color[idx] == 1) {
            cycleDetected = true;
            nodeOnCycle[idx] = true;
            return;
        }
        color[idx] = 1;

        uint32_t maxDownstream = 0;
        for (size_t edgeIdx : outEdges[idx]) {
            const auto& edge = graph.edges[edgeIdx];
            if (edge.sidechainOnly) {
                // Sidechain edges do not propagate audio path latency.
                continue;
            }
            const size_t dst = edge.dstNodeIdx;

            // If destination is gray, recursing would form a cycle; record and
            // skip this edge's contribution. The cycle is broken at this back
            // edge.
            if (color[dst] == 1) {
                cycleDetected = true;
                nodeOnCycle[dst] = true;
                continue;
            }
            visit(dst);

            const uint32_t pathViaEdge =
                contributedIntrinsic(graph.nodes[dst]) + topology.nodes[dst].downstreamLatency;
            if (pathViaEdge > maxDownstream) {
                maxDownstream = pathViaEdge;
            }
        }

        topology.nodes[idx].downstreamLatency = maxDownstream;
        topology.nodes[idx].totalPathLatency =
            contributedIntrinsic(graph.nodes[idx]) + maxDownstream;
        color[idx] = 2;
    };

    for (size_t i = 0; i < N; ++i) {
        visit(i);
    }

    if (cycleDetected) {
        topology.warnings.push_back(
            {SolverWarningCode::RoutingCycle,
             "routing cycle detected in LatencyGraph; back edges contribute zero compensation"});
    }

    // Per-edge compensation: for each audible edge F -> D, delay = F's slowest
    // downstream path minus the path taken via this edge. Equalizes timing for
    // nodes with multiple outgoing edges so reconvergence at master is sample-
    // accurate (PDCBranchingConvergenceTest is the canonical correctness gate).
    topology.edges.reserve(graph.edges.size());
    for (size_t e = 0; e < graph.edges.size(); ++e) {
        const auto& edge = graph.edges[e];
        SolvedLatencyTopology::EdgeSolution sol{};
        sol.srcNodeIdx = edge.srcNodeIdx;
        sol.dstNodeIdx = edge.dstNodeIdx;
        sol.sidechain = edge.sidechainOnly;
        sol.compensationSamples = 0;

        if (edgeOutOfRange[e] || edge.sidechainOnly) {
            // Sidechain edge compensation is P5 (G2). Out-of-range edges are
            // already warned above.
            topology.edges.push_back(sol);
            continue;
        }

        const uint32_t srcDownstream = topology.nodes[edge.srcNodeIdx].downstreamLatency;
        const uint32_t viaThisEdge =
            contributedIntrinsic(graph.nodes[edge.dstNodeIdx]) +
            topology.nodes[edge.dstNodeIdx].downstreamLatency;
        sol.compensationSamples = (srcDownstream >= viaThisEdge) ? (srcDownstream - viaThisEdge) : 0;
        topology.edges.push_back(sol);
    }

    // Project alignment latency: max totalPathLatency over audible, audible-
    // source, FullyCompensated nodes. "Source" = no incoming audible edge.
    // This matches the design model: leaves carry signal; internal nodes
    // aggregate it and are aligned per-edge at their feeders.
    //
    // Backward compatibility: an empty edge list makes every node a source
    // (hasIncomingAudible[i] = false for all i), so the result reduces to the
    // flat P2 behavior exactly.
    uint32_t maxAlignment = 0;
    for (size_t i = 0; i < N; ++i) {
        const auto& node = graph.nodes[i];
        if (node.muted) continue;
        if (node.domain == LatencyDomain::Realtime) continue;
        if (hasIncomingAudible[i]) continue; // not an audible source
        if (topology.nodes[i].totalPathLatency > maxAlignment) {
            maxAlignment = topology.nodes[i].totalPathLatency;
        }
    }

    // Output compensation: only audible sources receive output compensation.
    // Internal nodes (aggregators / buses) are aligned per-edge.
    for (size_t i = 0; i < N; ++i) {
        const auto& node = graph.nodes[i];
        auto& sol = topology.nodes[i];

        if (node.domain == LatencyDomain::Realtime) {
            sol.outputCompensationSamples = 0;
            continue;
        }
        if (hasIncomingAudible[i]) {
            // Non-source nodes don't accumulate output compensation; alignment
            // is enforced at the feeder boundary (per-edge compensation above).
            sol.outputCompensationSamples = 0;
            continue;
        }
        // Audible source. Clamp to zero defensively (muted-with-large-intrinsic
        // sources have totalPath > maxAlignment and must not produce negative
        // delays).
        sol.outputCompensationSamples =
            (maxAlignment >= sol.totalPathLatency) ? (maxAlignment - sol.totalPathLatency) : 0;
    }

    topology.projectAlignmentLatency = maxAlignment;
    // P9 (G6) will add master FX + device output to monitoring latency.
    topology.monitoringLatency = maxAlignment;

    return topology;
}

} // namespace Audio
} // namespace Aestra
