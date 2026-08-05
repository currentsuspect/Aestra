// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// LatencyTopology — PDC v2 data model and pure solver.
//
// Architectural contract (see Aestra-Internals: aestra-docs/PDC-v2-Design.md §4.0):
//   * `LatencyGraph` is an immutable input artifact, built off-RT from routing state.
//   * `solveLatency(LatencyGraph) -> SolvedLatencyTopology` is a pure function.
//     No engine references, no globals, no I/O, fully deterministic.
//   * `SolvedLatencyTopology` is an immutable output artifact consumed by the
//     RT engine read-only. The RT engine never mutates it.
//   * Compensation values are derived render state. They are NEVER stored on
//     `AudioRoute` or any user-visible / serialized structure.
//
// P2 scope (this commit): types are introduced; solver reproduces v1 behavior
// exactly (flat per-node, no graph traversal). Graph-aware DFS lands in P4.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Latency policy domain for a node.
 *
 * V2 only exercises `FullyCompensated`. `Realtime` is reserved and declared now
 * so future work (low-latency monitoring, hardware inserts, frozen paths,
 * anticipative processors) does not require a solver rewrite.
 */
enum class LatencyDomain : uint8_t {
    FullyCompensated = 0, ///< Default. Participates in graph alignment.
    Realtime = 1,         ///< Reserved (v3): live-monitored path, exempt from compensation.
};

/**
 * @brief Immutable input artifact for the PDC solver.
 *
 * Built off-RT from the current routing state. Never mutated after solve().
 */
struct LatencyGraph {
    struct Node {
        uint32_t channelId{0};         ///< Stable channel/track identity (track index for P2).
        uint32_t intrinsicLatency{0};  ///< Sum of non-bypassed plugin latencies on this node's effect chain.
        bool muted{false};             ///< Mute state at build time.
        LatencyDomain domain{LatencyDomain::FullyCompensated};
    };

    struct Edge {
        uint32_t srcNodeIdx{0};
        uint32_t dstNodeIdx{0};
        bool sidechainOnly{false};
    };

    std::vector<Node> nodes;
    std::vector<Edge> edges;        ///< Empty in P2; populated for graph-aware solve in P4+.
    uint64_t generation{0};         ///< Monotonic, bumped on rebuild.
};

/**
 * @brief Machine-readable classification for a solver warning.
 *
 * Consumers must switch on this instead of matching `SolverWarning::message`
 * text. The prose is a human-facing description and may be reworded freely;
 * the code is the contract that diagnostic surfaces (e.g. the Muse latency
 * report) map to their own stable issue codes.
 */
enum class SolverWarningCode : uint8_t {
    /// A routing cycle was found; back edges contribute zero compensation.
    RoutingCycle,
    /// Edge(s) referenced out-of-range node indices; their compensation is zero.
    InvalidEdgeIndices,
};

/**
 * @brief One diagnostic raised during solve, classified and described.
 */
struct SolverWarning {
    SolverWarningCode code{SolverWarningCode::RoutingCycle};
    std::string message;
};

/**
 * @brief Immutable output artifact produced by the PDC solver.
 *
 * Consumed read-only by the RT engine. Per-edge / per-node ring-buffer state
 * lives elsewhere (RT-side `SendRTState` / `TrackRTState`); this topology only
 * carries solved values + diagnostics.
 */
struct SolvedLatencyTopology {
    struct NodeSolution {
        uint32_t channelId{0};                  ///< Mirrors LatencyGraph::Node::channelId.
        uint32_t intrinsicLatency{0};
        uint32_t downstreamLatency{0};          ///< P2: always zero (flat solver).
        uint32_t totalPathLatency{0};           ///< intrinsicLatency + downstreamLatency.
        uint32_t outputCompensationSamples{0};  ///< Delay applied at this node's audible output.
    };

    struct EdgeSolution {
        uint32_t srcNodeIdx{0};
        uint32_t dstNodeIdx{0};
        uint32_t compensationSamples{0}; ///< Delay applied at the feeder side. P2: empty (no edges).
        bool sidechain{false};
    };

    std::vector<NodeSolution> nodes;
    std::vector<EdgeSolution> edges;

    /**
     * @brief Inter-track alignment latency (samples).
     *
     * Max totalPathLatency across audible (non-muted, FullyCompensated) leaves.
     * This is the existing v1 `getMaxProjectLatency()` semantic.
     */
    uint32_t projectAlignmentLatency{0};

    /**
     * @brief User-visible "monitor-shift" latency (samples).
     *
     * P2: equal to `projectAlignmentLatency`. P9 (G6) will add master FX +
     * reported device output latency to this value.
     */
    uint32_t monitoringLatency{0};

    /**
     * @brief Diagnostic / approximation warnings raised during solve.
     *
     * Consumed off-RT (e.g. logged once per generation). Never read from the
     * audio thread.
     */
    std::vector<SolverWarning> warnings;

    uint64_t generation{0}; ///< Mirrors LatencyGraph::generation.
};

/**
 * @brief Pure PDC solver.
 *
 * @param graph Immutable input. Not modified.
 * @return Solved topology. Deterministic for a given input.
 *
 * P2 contract:
 *   * Flat computation: each node's `totalPathLatency = intrinsicLatency`,
 *     `downstreamLatency = 0`.
 *   * `projectAlignmentLatency` = max `intrinsicLatency` over all
 *     `FullyCompensated` non-muted nodes (matches v1 exactly).
 *   * `outputCompensationSamples` = `projectAlignmentLatency - intrinsicLatency`
 *     for `FullyCompensated` nodes, zero for `Realtime` nodes.
 *   * `monitoringLatency` = `projectAlignmentLatency` (master/device addend in P9).
 *   * Edge solutions are not produced in P2 (graph-aware solve lands in P4).
 *
 * RT-safety: solver may allocate (off-RT). Callable from any non-audio thread.
 */
SolvedLatencyTopology solveLatency(const LatencyGraph& graph);

} // namespace Audio
} // namespace Aestra
