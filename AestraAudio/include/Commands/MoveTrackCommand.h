// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/TrackManager.h"

#include <string>
#include <algorithm>
#include <unordered_set>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Move a track one row up or down the timeline (DAW Essentials Checklist: reorder tracks).
 *
 * The timeline lists rows in playlist lane order: a track sits at its first lane, a lane no track owns is a row of its
 * own (TrackManagerUI::refreshTracks). Moving a row swaps it with its neighbour; the track's other lanes travel with
 * it. Undo restores the exact lane order from before. A row with no neighbour that way is a no-op.
 *
 * Rows the timeline does not show (folded empty tracks) are stepped over when @p shown is given: the row lands next
 * to the nearest row the producer can see, and the hidden rows between keep their order.
 */
class MoveTrackCommand : public ICommand {
public:
    /// @p rowLane: the row's lane (any lane of the track works). @p direction: -1 up, +1 down.
    MoveTrackCommand(TrackManager& trackManager, PlaylistLaneID rowLane, int direction,
                     std::vector<PlaylistLaneID> shown = {})
        : m_trackManager(trackManager), m_rowLane(rowLane), m_direction(direction < 0 ? -1 : 1),
          m_shown(std::move(shown)) {}

    /// The rows in display order, each with its lanes (head lane first). Exposed for the menu and tests.
    static std::vector<std::vector<PlaylistLaneID>> rows(TrackManager& tm) {
        auto& playlist = tm.getPlaylistModel();
        std::vector<std::vector<PlaylistLaneID>> out;
        std::unordered_set<uint64_t> seen;
        for (size_t i = 0; i < playlist.getLaneCount(); ++i) {
            const auto laneId = playlist.getLaneId(i);
            const auto* lane = playlist.getLane(laneId);
            if (!lane) continue;
            const auto* track = lane->trackId != 0 ? tm.getTrack(lane->trackId) : nullptr;
            if (!track || track->laneIds.empty()) {
                out.push_back({laneId});
                continue;
            }
            if (!seen.insert(track->trackId).second) continue;
            std::vector<PlaylistLaneID> row{track->laneIds.front()};
            for (size_t j = 0; j < playlist.getLaneCount(); ++j) { // its other lanes, in their playlist order
                const auto other = playlist.getLaneId(j);
                const auto* l = playlist.getLane(other);
                if (l && l->trackId == track->trackId && other != row.front()) row.push_back(other);
            }
            out.push_back(std::move(row));
        }
        return out;
    }

    /// Index of the row holding @p laneId, or -1.
    static int rowIndexOf(const std::vector<std::vector<PlaylistLaneID>>& rs, PlaylistLaneID laneId) {
        for (size_t i = 0; i < rs.size(); ++i)
            for (const auto& id : rs[i])
                if (id == laneId) return static_cast<int>(i);
        return -1;
    }

    /// The nearest row from @p i in @p direction that holds a shown lane (any row when @p shown is empty), or -1.
    static int neighbour(const std::vector<std::vector<PlaylistLaneID>>& rs, int i, int direction,
                         const std::vector<PlaylistLaneID>& shown) {
        for (int j = i + direction; j >= 0 && j < static_cast<int>(rs.size()); j += direction) {
            if (shown.empty()) return j;
            for (const auto& id : rs[static_cast<size_t>(j)])
                if (std::find(shown.begin(), shown.end(), id) != shown.end()) return j;
        }
        return -1;
    }

    /// Whether the row holding @p laneId can move that way (for enabling the menu items).
    static bool canMove(TrackManager& tm, PlaylistLaneID laneId, int direction, const std::vector<PlaylistLaneID>& shown = {}) {
        const auto rs = rows(tm);
        const int i = rowIndexOf(rs, laneId);
        return i >= 0 && neighbour(rs, i, direction < 0 ? -1 : 1, shown) >= 0;
    }

    void execute() override {
        auto& playlist = m_trackManager.getPlaylistModel();
        m_before.clear();
        for (size_t i = 0; i < playlist.getLaneCount(); ++i) m_before.push_back(playlist.getLaneId(i));
        auto rs = rows(m_trackManager);
        const int i = rowIndexOf(rs, m_rowLane);
        const int j = i >= 0 ? neighbour(rs, i, m_direction, m_shown) : -1;
        m_moved = j >= 0;
        if (!m_moved) return;
        // Take the row out and put it on the far side of its neighbour; rows in between keep their order.
        auto row = std::move(rs[static_cast<size_t>(i)]);
        rs.erase(rs.begin() + i);
        // Up: j is the neighbour, so inserting at j lands before it. Down: erasing shifted the neighbour to j - 1,
        // so inserting at j lands after it.
        rs.insert(rs.begin() + j, std::move(row));
        std::vector<PlaylistLaneID> order;
        for (const auto& row : rs) order.insert(order.end(), row.begin(), row.end());
        apply(order);
    }

    void undo() override {
        if (m_moved) apply(m_before);
    }

    void redo() override { execute(); }

    std::string getName() const override { return m_direction < 0 ? "Move Track Up" : "Move Track Down"; }
    size_t getSizeInBytes() const override { return sizeof(*this) + m_before.size() * sizeof(PlaylistLaneID); }
    bool changesProjectState() const override { return m_moved; }
    bool moved() const { return m_moved; }

private:
    void apply(const std::vector<PlaylistLaneID>& order) {
        auto& playlist = m_trackManager.getPlaylistModel();
        for (size_t k = 0; k < order.size(); ++k) playlist.moveLaneToIndex(order[k], k);
        m_trackManager.requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
    }

    TrackManager& m_trackManager;
    PlaylistLaneID m_rowLane;
    int m_direction;
    std::vector<PlaylistLaneID> m_shown;
    std::vector<PlaylistLaneID> m_before;
    bool m_moved = false;
};

} // namespace Audio
} // namespace Aestra
