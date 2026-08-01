// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Commands/ImportAudioClipCommand.h"

#include "AestraLog.h"
#include "Models/TrackManager.h"

namespace Aestra {
namespace Audio {

void ImportAudioClipCommand::rollbackFailedFirstExecute() {
    auto& patterns = m_manager.getPatternManager();
    if (m_patternId.isValid()) {
        m_detachedPattern = patterns.detachPattern(m_patternId);
        m_detachedPattern.reset();
        m_patternId = PatternID{};
    }
    // Only a source this command introduced may be withdrawn, and only here:
    // nothing has referenced it yet. A source the project already had stays.
    if (m_createdSource && m_sourceId.isValid()) {
        m_manager.getSourceManager().removeSource(m_sourceId);
        m_sourceId = ClipSourceID{};
        m_createdSource = false;
    }
}

void ImportAudioClipCommand::execute() {
    if (m_executed) {
        return;
    }

    auto& sources = m_manager.getSourceManager();
    auto& patterns = m_manager.getPatternManager();
    auto& playlist = m_manager.getPlaylistModel();

    const bool firstRun = !m_sourceId.isValid();

    // Register the decoded audio here rather than in the factory, so the whole
    // import is one mutation the history owns.
    if (firstRun) {
        if (!m_buffer || !m_buffer->isValid()) {
            Log::error("[ImportAudioClipCommand] No decoded audio to import: " + m_filePath);
            return;
        }
        const ClipSourceID existing = sources.findSourceByPath(m_filePath);
        if (existing.isValid()) {
            // Already in the project (path is the dedupe key): adopt it, and
            // never withdraw it on failure.
            m_sourceId = existing;
            m_createdSource = false;
        } else {
            m_sourceId = sources.createRecordedSource(m_filePath, m_displayName, m_buffer);
            if (!m_sourceId.isValid()) {
                Log::error("[ImportAudioClipCommand] Could not register the decoded source for: " + m_filePath);
                return;
            }
            m_createdSource = true;
        }
    }

    const ClipSource* source = sources.getSource(m_sourceId);
    const uint64_t sourceFrames = source ? source->getNumFrames() : 0;
    if (sourceFrames == 0) {
        Log::error("[ImportAudioClipCommand] The registered source carries no audio: " + m_filePath);
        if (firstRun) {
            rollbackFailedFirstExecute();
        }
        return;
    }

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
        fullSlice.lengthSamples = static_cast<double>(sourceFrames);
        payload.slices.push_back(fullSlice);

        m_patternId = patterns.createAudioPattern(m_displayName, m_durationBeats, payload);
        if (!m_patternId.isValid()) {
            Log::error("[ImportAudioClipCommand] Could not create the audio pattern for: " + m_displayName);
            if (firstRun) {
                rollbackFailedFirstExecute();
            }
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
        Log::error("[ImportAudioClipCommand] Could not place the imported clip on its lane.");
        if (firstRun) {
            rollbackFailedFirstExecute();
        } else {
            // A failed redo keeps the source: undo is what put it in reach.
            m_detachedPattern = patterns.detachPattern(m_patternId);
        }
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
    // Detach rather than remove, and keep the source: redo reuses both instead
    // of decoding the file a second time.
    m_detachedPattern = m_manager.getPatternManager().detachPattern(m_patternId);
    m_manager.markModified();
    m_manager.requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
    m_executed = false;
}

} // namespace Audio
} // namespace Aestra
