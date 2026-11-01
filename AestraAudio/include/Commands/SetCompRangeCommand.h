// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Comping (SPEC-022 CT-2, FD-70): make a span of the track's comp play one take. The first swipe creates the master
// ("Comp") lane; every swipe rebuilds the master lane's clips from the comp and silences the take lanes it reads from.
// One swipe is one undo step; undo restores the clips, the comp, the mute flags and (for the first swipe) removes the lane.

#include "Commands/AttachLaneToTrackCommand.h"
#include "Commands/CompSync.h"
#include "Commands/CreateLaneCommand.h"
#include "Commands/ICommand.h"
#include "Models/CompRanges.h"
#include "Models/TrackManager.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Aestra {
namespace Audio {

class SetCompRangeCommand : public ICommand {
public:
    SetCompRangeCommand(TrackManager& tracks, uint64_t trackId, double startBeat, double endBeat, PlaylistLaneID takeLaneId)
        : m_tracks(tracks), m_trackId(trackId), m_start(startBeat), m_end(endBeat), m_take(takeLaneId) {}

    void execute() override {
        if (m_applied) return;
        Track* track = m_tracks.getTrack(m_trackId);
        const PlaylistLane* take = m_tracks.getPlaylistModel().getLane(m_take);
        m_valid = false;
        if (!track || !take || take->trackId != m_trackId || m_take == track->compLaneId || m_end - m_start <= kCompEpsilon) return;
        if (std::find(track->laneIds.begin(), track->laneIds.end(), m_take) == track->laneIds.end()) return;

        m_before = capture(*track);
        if (!track->compLaneId.isValid()) { // the first swipe makes the master lane
            m_create = std::make_shared<CreateLaneCommand>(m_tracks.getPlaylistModel(), "Comp");
            m_create->execute();
            m_attach = std::make_shared<AttachLaneToTrackCommand>(m_tracks, m_trackId, m_create->getLaneId());
            m_attach->execute();
            track = m_tracks.getTrack(m_trackId);
            if (!track || !m_tracks.getPlaylistModel().getLane(m_create->getLaneId())) return;
            track->compLaneId = m_create->getLaneId();
        }
        track->comp.set(m_start, m_end, m_take);
        rebuild(*track);
        m_after = capture(*track);
        m_valid = true;
        m_applied = true;
    }

    void undo() override {
        if (!m_applied) return;
        Track* track = m_tracks.getTrack(m_trackId);
        if (track) {
            restore(*track, m_before);
            if (m_attach) {
                m_attach->undo();
                m_create->undo();
                if (Track* t = m_tracks.getTrack(m_trackId)) t->compLaneId = PlaylistLaneID{};
            }
        }
        m_applied = false;
        m_tracks.requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
    }

    void redo() override {
        if (m_applied || !m_valid) return;
        if (m_attach) { // the same lane id comes back (CreateLaneCommand restores identity)
            m_create->redo();
            m_attach->redo();
        }
        if (Track* track = m_tracks.getTrack(m_trackId)) {
            restore(*track, m_after);
            m_applied = true;
        }
        m_tracks.requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
    }

    std::string getName() const override { return "Set Comp"; }
    bool isUndoable() const override { return m_valid; }
    size_t getSizeInBytes() const override {
        return sizeof(*this) + (m_before.clips.size() + m_after.clips.size()) * sizeof(ClipInstance);
    }
    std::string type() const override { return "set_comp_range"; }

private:
    using State = CompSnapshot;
    State capture(const Track& track) { return captureComp(m_tracks.getPlaylistModel(), track); }
    void restore(Track& track, const State& s) { restoreComp(m_tracks.getPlaylistModel(), track, s); }
    void rebuild(Track& track) { rebuildComp(m_tracks.getPlaylistModel(), track); }

    TrackManager& m_tracks;
    uint64_t m_trackId;
    double m_start, m_end;
    PlaylistLaneID m_take;
    State m_before, m_after;
    std::shared_ptr<CreateLaneCommand> m_create;
    std::shared_ptr<AttachLaneToTrackCommand> m_attach;
    bool m_applied = false;
    bool m_valid = false;
};

} // namespace Audio
} // namespace Aestra
