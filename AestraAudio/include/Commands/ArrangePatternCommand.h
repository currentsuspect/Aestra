// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/TrackManager.h"

#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Command to place a MIDI pattern on the timeline as a clip (undoable)
 *
 * Mirrors the whole pattern-drop gesture the timeline UI performs, not just
 * the clip insert:
 *  - appends a Playlist lane when the target is the next lane index,
 *  - adds a pattern-bound clip sized to the pattern length.
 *
 * Unit-to-mixer routing is intentionally independent from clip placement.
 * Undo removes the clip and any lanes this command created. Like
 * AddChannelCommand, redo after undo mints fresh lane/clip IDs.
 */
class ArrangePatternCommand : public ICommand {
public:
    ArrangePatternCommand(TrackManager& trackManager, PatternID patternId, size_t trackIndex,
                          double startBeat)
        : m_trackManager(trackManager), m_patternId(patternId), m_trackIndex(trackIndex),
          m_startBeat(startBeat) {}

    void execute() override {
        if (m_executed) return;
        m_createdLanes.clear();

        auto& playlist = m_trackManager.getPlaylistModel();
        const PatternSource* pattern = m_trackManager.getPatternManager().getPattern(m_patternId);
        if (!pattern || !pattern->isMidi()) return;

        // A failure after partial mutation must not leave lanes behind.
        const auto rollbackStaged = [&]() {
            for (auto it = m_createdLanes.rbegin(); it != m_createdLanes.rend(); ++it) {
                playlist.removeLane(*it);
            }
            m_createdLanes.clear();
        };

        while (playlist.getLaneCount() <= m_trackIndex) {
            PlaylistLaneID laneId =
                playlist.createLane("Lane " + std::to_string(playlist.getLaneCount() + 1));
            if (!laneId.isValid()) {
                rollbackStaged();
                return;
            }
            m_createdLanes.push_back(laneId);
        }
        const PlaylistLaneID laneId = playlist.getLaneId(m_trackIndex);
        if (!laneId.isValid()) {
            rollbackStaged();
            return;
        }

        ClipInstance clip;
        clip.id = ClipInstanceID::generate();
        clip.name = pattern->name;
        clip.startBeat = m_startBeat;
        clip.durationBeats = pattern->lengthBeats;
        clip.patternId = m_patternId;
        clip.sourceId = m_patternId.value;

        m_clipId = playlist.addClip(laneId, clip);
        if (!m_clipId.isValid()) {
            rollbackStaged();
            return;
        }
        m_executed = true;
    }

    void undo() override {
        if (!m_executed) return;
        auto& playlist = m_trackManager.getPlaylistModel();

        playlist.removeClip(m_clipId);
        for (auto it = m_createdLanes.rbegin(); it != m_createdLanes.rend(); ++it) {
            playlist.removeLane(*it);
        }
        m_executed = false;
    }

    void redo() override {
        if (m_executed) return;
        execute();
    }

    std::string getName() const override { return "Arrange Pattern"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    TrackManager& m_trackManager;
    PatternID m_patternId;
    size_t m_trackIndex;
    double m_startBeat;

    // Recorded by execute() for undo
    std::vector<PlaylistLaneID> m_createdLanes;
    ClipInstanceID m_clipId;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
