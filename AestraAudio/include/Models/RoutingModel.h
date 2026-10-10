// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

class TrackManager;

/**
 * @brief Read-only view of the routing graph (V8-C15, FD-29).
 *
 * A snapshot taken from TrackManager at a point in time. It answers the
 * questions a routing UI asks — what feeds this channel, where does it go,
 * which channels share an output — without touching the live graph. It never
 * decides whether a route is legal: that stays in TrackManager::canRouteTo,
 * and every mutation still goes through the routing commands.
 *
 * Master is spelled kMaster (0, model space) everywhere in this view; the
 * engine's 0xFFFFFFFF spelling is normalised on the way in.
 */
class RoutingModel {
public:
    static constexpr uint32_t kMaster = 0;

    enum class EdgeKind { Main, Send };

    struct Edge {
        uint32_t sourceId{0};
        uint32_t targetId{kMaster};
        EdgeKind kind{EdgeKind::Main};
        uint64_t sendId{0}; ///< Stable send identity (Contract D2); 0 for Main.
        float gain{1.0f};   ///< Send level, linear; 1 for Main.
        bool postFader{true};
        bool muted{false};
        bool sidechainOnly{false}; ///< Control input only, not part of the audible mix.
    };

    struct Channel {
        uint32_t id{0};
        std::string name;
        uint32_t mainOutputId{kMaster};
    };

    /// Channels sharing one main output, in mixer order. Name is the target's.
    struct OutputGroup {
        uint32_t targetId{kMaster};
        std::string targetName;
        std::vector<uint32_t> sourceIds;
    };

    /// Snapshot the manager's channels. Master is included as a target only.
    static RoutingModel capture(const TrackManager& manager);

    const std::vector<Channel>& channels() const { return m_channels; }
    const std::vector<Edge>& edges() const { return m_edges; }

    /// Display name for an id; "Master" for kMaster; empty when unknown.
    std::string nameOf(uint32_t id) const;

    /// Edges arriving at `id`, mains first then sends, in source order.
    /// Includes muted and sidechain edges; filter on the flags to taste.
    std::vector<Edge> feeds(uint32_t id) const;

    /// Edges leaving `id`: its main output first, then its sends.
    std::vector<Edge> outputsOf(uint32_t id) const;

    /// Ids of channels whose name equals `name` (exact, case-sensitive).
    /// "Master" resolves to kMaster. Names are not unique, so this is a list.
    std::vector<uint32_t> idsNamed(const std::string& name) const;

    /// Main-output groups: Master first when non-empty, then by target order.
    std::vector<OutputGroup> groupByOutput() const;

private:
    std::vector<Channel> m_channels;
    std::vector<Edge> m_edges;
};

} // namespace Audio
} // namespace Aestra
