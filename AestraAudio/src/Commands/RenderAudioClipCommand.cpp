// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Commands/RenderAudioClipCommand.h"

#include "AestraLog.h"
#include "Models/ClipRenderService.h"
#include "Models/TrackManager.h"

#include <algorithm>
#include <cmath>

namespace Aestra {
namespace Audio {

void RenderAudioClipCommand::execute() {
    if (m_executed) {
        return;
    }
    const ClipInstance* clip = m_manager.getPlaylistModel().getClip(m_clipId);
    if (!clip) {
        return;
    }
    if (!m_originalPatternId.isValid()) {
        m_originalPatternId = clip->patternId;
        m_originalEdits = clip->edits;
    }

    // Redo path: the audio was rendered once already, so put the detached
    // pattern back instead of writing a second identical file.
    if (m_detachedPattern) {
        if (!m_manager.getPatternManager().reinsertPattern(m_detachedPattern)) {
            return;
        }
    } else {
        ClipInstance clipCopy = *clip;
        auto rendered = renderBuffer(clipCopy);
        if (!rendered || !rendered->isValid()) {
            return;
        }

        ClipRenderService service(m_manager.getSourceManager(), m_manager.getPatternManager());
        const auto region = service.resolveClipRegion(clipCopy);
        const std::string baseName = (region.name.empty() ? clipCopy.name : region.name) + " " + renderSuffix();

        const auto result = service.commit(*rendered, m_manager.renderRootDirectory(), baseName, region.lengthBeats,
                                           region.mixerChannelId);
        if (!result.isValid()) {
            return;
        }
        m_renderedPatternId = result.patternId;
    }

    if (!m_manager.getPlaylistModel().setClipPattern(m_clipId, m_renderedPatternId)) {
        // Leave no orphan pattern behind if the repoint failed.
        m_detachedPattern = m_manager.getPatternManager().detachPattern(m_renderedPatternId);
        return;
    }

    ClipEdits edits = m_originalEdits;
    adjustEditsAfterRender(edits);
    if (!(edits.gainLinear == m_originalEdits.gainLinear && edits.fadeInBeats == m_originalEdits.fadeInBeats &&
          edits.fadeOutBeats == m_originalEdits.fadeOutBeats)) {
        m_editsChanged = m_manager.getPlaylistModel().setClipEdits(m_clipId, edits);
    }

    m_manager.markModified();
    m_manager.requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
    m_executed = true;
}

void RenderAudioClipCommand::undo() {
    if (!m_executed) {
        return;
    }
    if (!m_manager.getPlaylistModel().setClipPattern(m_clipId, m_originalPatternId)) {
        return;
    }
    if (m_editsChanged) {
        m_manager.getPlaylistModel().setClipEdits(m_clipId, m_originalEdits);
        m_editsChanged = false;
    }
    // Detach rather than remove: redo reinserts this exact pattern, and the
    // rendered file it points at is still on disk.
    m_detachedPattern = m_manager.getPatternManager().detachPattern(m_renderedPatternId);
    m_manager.markModified();
    m_manager.requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
    m_executed = false;
}

// --- Reverse ----------------------------------------------------------------

std::shared_ptr<AudioBufferData> ReverseAudioClipCommand::renderBuffer(const ClipInstance& clip) {
    ClipRenderService service(m_manager.getSourceManager(), m_manager.getPatternManager());
    const auto region = service.resolveClipRegion(clip);
    if (!region.isValid()) {
        Log::warning("[ReverseAudioClipCommand] Clip has no resolvable audio to reverse.");
        return nullptr;
    }

    auto buffer = ClipRenderService::extractRegion(*region.buffer, region.startFrame, region.frameCount);
    if (!buffer) {
        return nullptr;
    }
    ClipRenderService::reverseInPlace(*buffer);
    return buffer;
}

// --- Commit clip edits ------------------------------------------------------

std::shared_ptr<AudioBufferData> CommitAudioClipEditsCommand::renderBuffer(const ClipInstance& clip) {
    ClipRenderService service(m_manager.getSourceManager(), m_manager.getPatternManager());
    const auto region = service.resolveClipRegion(clip);
    if (!region.isValid()) {
        Log::warning("[CommitAudioClipEditsCommand] Clip has no resolvable audio to commit.");
        return nullptr;
    }

    auto buffer = ClipRenderService::extractRegion(*region.buffer, region.startFrame, region.frameCount);
    if (!buffer) {
        return nullptr;
    }

    // Fades are authored in beats, so they only become frame counts at the
    // project tempo the user is hearing them at.
    const double bpm = std::max(1.0, m_manager.getPlaylistModel().getBPM());
    const double framesPerBeat = (static_cast<double>(buffer->sampleRate) * 60.0) / bpm;
    const auto toFrames = [framesPerBeat](float beats) -> uint64_t {
        if (!std::isfinite(beats) || beats <= 0.0f) {
            return 0;
        }
        const double frames = static_cast<double>(beats) * framesPerBeat;
        return frames > 0.0 ? static_cast<uint64_t>(frames) : 0;
    };

    ClipRenderService::applyFades(*buffer, toFrames(clip.edits.fadeInBeats), toFrames(clip.edits.fadeOutBeats));

    const float gain = std::isfinite(clip.edits.gainLinear) ? clip.edits.gainLinear : 1.0f;
    ClipRenderService::applyGain(*buffer, gain);

    return buffer;
}

void CommitAudioClipEditsCommand::adjustEditsAfterRender(ClipEdits& edits) const {
    // These are now part of the audio; leaving them set would apply them twice.
    edits.gainLinear = 1.0f;
    edits.fadeInBeats = 0.0f;
    edits.fadeOutBeats = 0.0f;
}

} // namespace Audio
} // namespace Aestra
