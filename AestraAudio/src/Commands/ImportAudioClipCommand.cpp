// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Commands/ImportAudioClipCommand.h"

#include "AestraLog.h"
#include "Models/TrackManager.h"

#include <cmath>

namespace Aestra {
namespace Audio {

void ImportAudioClipCommand::execute() {
    if (m_executed) {
        return;
    }

    auto& patterns = m_manager.getPatternManager();
    auto& playlist = m_manager.getPlaylistModel();

    if (m_detachedPattern) {
        // Redo: the pattern already describes this import, so put it back
        // rather than minting a second one for the same audio.
        if (!patterns.reinsertPattern(m_detachedPattern)) {
            return;
        }
    } else {
        AudioSlicePayload payload;
        payload.audioSourceId = m_sourceId;
        payload.durationSeconds = m_durationSeconds;
        AudioSlice fullSlice;
        fullSlice.startSamples = 0.0;
        fullSlice.lengthSamples = static_cast<double>(m_sourceFrames);
        payload.slices.push_back(fullSlice);

        m_patternId = patterns.createAudioPattern(m_displayName, m_durationBeats, payload);
        if (!m_patternId.isValid()) {
            Log::error("[ImportAudioClipCommand] Could not create the audio pattern for: " + m_displayName);
            return;
        }
    }

    ClipInstance clip;
    clip.id = m_clipId.isValid() ? m_clipId : ClipInstanceID::generate();
    clip.name = m_displayName;
    clip.startBeat = m_startBeat;
    clip.durationBeats = m_durationBeats;
    clip.durationSeconds = m_durationSeconds;
    clip.patternId = m_patternId;
    clip.sourceId = m_patternId.value;
    clip.edits = ClipEdits::forNewAudioClip();

    const ClipInstanceID placed = playlist.addClip(m_laneId, clip);
    if (!placed.isValid() || !playlist.getClip(placed)) {
        // Leave nothing behind: the pattern only existed for this clip.
        m_detachedPattern = patterns.detachPattern(m_patternId);
        Log::error("[ImportAudioClipCommand] Could not place the imported clip on its lane.");
        return;
    }

    m_clipId = placed;
    m_manager.markModified();
    m_manager.requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
    m_executed = true;
}

void ImportAudioClipCommand::undo() {
    if (!m_executed) {
        return;
    }
    auto& playlist = m_manager.getPlaylistModel();
    playlist.removeClip(m_clipId);
    if (playlist.getClip(m_clipId)) {
        return;
    }
    // Detach rather than remove so redo reuses this pattern and the decoded
    // source stays referenced.
    m_detachedPattern = m_manager.getPatternManager().detachPattern(m_patternId);
    m_manager.markModified();
    m_manager.requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
    m_executed = false;
}

} // namespace Audio
} // namespace Aestra
