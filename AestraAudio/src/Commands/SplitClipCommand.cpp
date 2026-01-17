// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Commands/SplitClipCommand.h"
#include <iostream>

namespace Aestra {
namespace Audio {

SplitClipCommand::SplitClipCommand(PlaylistModel& model, ClipInstanceID clipId, double splitBeat)
    : m_model(model), m_targetClipId(clipId), m_splitBeat(splitBeat) {
}

void SplitClipCommand::execute() {
    if (m_executed) {
        redo();
        return;
    }

    // 1. Capture original state
    auto* clip = m_model.getClip(m_targetClipId);
    if (!clip) return;
    
    m_originalDuration = clip->durationBeats;
    m_originalFadeOut = clip->edits.fadeOutBeats;
    m_laneId = m_model.findClipLane(m_targetClipId);
    
    // 2. Perform the split using the model's logic
    m_newClipId = m_model.splitClip(m_targetClipId, m_splitBeat);
    
    // 3. Capture the result for redo
    auto* newClip = m_model.getClip(m_newClipId);
    if (newClip) {
        m_newClipState = *newClip;
    }
    
    m_executed = true;
}

void SplitClipCommand::undo() {
    // 1. Remove the new clip
    if (m_newClipId.isValid()) {
        m_model.removeClip(m_newClipId);
    }
    
    // 2. Restore original duration and properties of the first clip
    m_model.setClipDuration(m_targetClipId, m_originalDuration);
    
    auto* clip = m_model.getClip(m_targetClipId);
    if (clip) {
        clip->edits.fadeOutBeats = m_originalFadeOut;
    }
}

void SplitClipCommand::redo() {
    // 1. Trim the first clip
    auto* clip = m_model.getClip(m_targetClipId);
    if (!clip) return;
    
    double newDuration = m_splitBeat - clip->startBeat;
    if (newDuration > 0) {
        m_model.setClipDuration(m_targetClipId, newDuration);
        clip->edits.fadeOutBeats = 0.0f; // Clear fade out (seamless split)
    }
    
    // 2. Re-add the second clip
    // Verify lane exists
    if (!m_model.getLane(m_laneId)) {
        return; 
    }
    
    // Add the clip back with its preserved ID
    m_model.addClip(m_laneId, m_newClipState);
}

} // namespace Audio
} // namespace Aestra
