// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Commands/MuseStubs.h"
#include "Models/TrackManager.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Delete a track from the timeline: its lanes (clips, automation, takes and comp), the track, and its own
 *        mixer channel, as one undo step (DAW Essentials Checklist: delete a track).
 *
 * The channel goes only when it is the track's own: not the master, and no other track and no Arsenal unit plays
 * through it. It is removed by the mixer's own undoable delete (DeleteTrackCommand), which keeps the channel object,
 * its plugins and every route into it for undo. A row with no track (a lane nobody owns) deletes just that lane.
 * Undo puts lanes back at their places with their ids, then the track with its id (older undo steps refer to both).
 */
class RemoveTrackCommand : public ICommand {
public:
    RemoveTrackCommand(TrackManager& tm, PlaylistLaneID rowLane) : m_tm(tm), m_rowLane(rowLane) {}

    /// How many clips the row's lanes hold (the menu asks before deleting a track that has any).
    static size_t clipCount(TrackManager& tm, PlaylistLaneID rowLane) {
        auto& pl = tm.getPlaylistModel();
        const auto* lane = pl.getLane(rowLane);
        if (!lane) return 0;
        const Track* track = lane->trackId != 0 ? tm.getTrack(lane->trackId) : nullptr;
        size_t n = 0;
        for (const auto& id : track ? track->laneIds : std::vector<PlaylistLaneID>{rowLane})
            if (const auto* l = pl.getLane(id)) n += l->clips.size();
        return n;
    }

    void execute() override {
        if (m_done) return;
        auto& pl = m_tm.getPlaylistModel();
        if (!m_captured) {
            const auto* lane = pl.getLane(m_rowLane);
            if (!lane) return;
            std::vector<PlaylistLaneID> ids{m_rowLane};
            if (const Track* track = lane->trackId != 0 ? m_tm.getTrack(lane->trackId) : nullptr) {
                m_track = *track;
                m_hasTrack = true;
                ids = track->laneIds;
                m_deleteChannel = channelIsOwnedBy(track->channelId, track->trackId);
            }
            for (const auto& id : ids)
                if (const auto* l = pl.getLane(id)) m_lanes.push_back({*l, pl.getLaneIndex(id)});
            std::sort(m_lanes.begin(), m_lanes.end(), [](const Saved& a, const Saved& b) { return a.index < b.index; });
            m_captured = true;
        }
        if (m_hasTrack) m_tm.removeTrack(m_track.trackId);
        for (const auto& saved : m_lanes) pl.removeLane(saved.lane.id);
        if (m_deleteChannel) {
            if (!m_channelCmd) {
                for (size_t i = 0; i < m_tm.getChannelCount(); ++i) {
                    const auto* ch = m_tm.getChannel(i);
                    if (ch && ch->getChannelId() == m_track.channelId) {
                        m_channelCmd = std::make_shared<DeleteTrackCommand>(m_tm, static_cast<int>(i));
                        break;
                    }
                }
                if (m_channelCmd) m_channelCmd->execute();
            } else {
                m_channelCmd->redo();
            }
        }
        m_tm.requestAudioGraphRebuild(GraphDirtyReason::TrackStructureChanged);
        m_done = true;
    }

    void undo() override {
        if (!m_done) return;
        if (m_channelCmd) m_channelCmd->undo();
        auto& pl = m_tm.getPlaylistModel();
        for (const auto& saved : m_lanes) { // lowest index first, so each lands where it was
            const PlaylistLaneID id = pl.createLaneWithId(saved.lane.id, saved.lane.name);
            if (auto* lane = pl.getLane(id)) {
                const int index = lane->index;
                *lane = saved.lane;
                lane->index = index;
                lane->trackId = 0; // owned again by restoreTrack below
            }
            if (saved.index >= 0) pl.moveLaneToIndex(id, static_cast<size_t>(saved.index));
        }
        if (m_hasTrack) m_tm.restoreTrack(m_track);
        m_tm.requestAudioGraphRebuild(GraphDirtyReason::TrackStructureChanged);
        m_done = false;
    }

    void redo() override { execute(); }
    std::string getName() const override { return m_hasTrack ? "Delete Track" : "Delete Lane"; }
    bool changesProjectState() const override { return m_captured; }
    bool deletesChannel() const { return m_deleteChannel; }

private:
    struct Saved {
        PlaylistLane lane;
        int index = -1;
    };

    bool channelIsOwnedBy(uint32_t channelId, uint64_t trackId) {
        if (channelId == 0 || !m_tm.getChannelById(channelId)) return false;
        for (const Track* other : m_tm.getTracks())
            if (other && other->trackId != trackId && other->channelId == channelId) return false;
        auto& units = m_tm.getUnitManager();
        for (UnitID unit : units.getAllUnitIDs())
            if (units.getUnitMixerChannel(unit) == channelId) return false;
        return true;
    }

    TrackManager& m_tm;
    PlaylistLaneID m_rowLane;
    Track m_track;
    bool m_hasTrack = false;
    bool m_deleteChannel = false;
    bool m_captured = false;
    bool m_done = false;
    std::vector<Saved> m_lanes;
    std::shared_ptr<DeleteTrackCommand> m_channelCmd;
};

} // namespace Audio
} // namespace Aestra
