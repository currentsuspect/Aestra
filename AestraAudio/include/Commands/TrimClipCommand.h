// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/ClipInstance.h"
#include "Models/PlaylistModel.h"

#include <memory>
#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Command to trim a clip's start/end positions
 */
class TrimClipCommand : public ICommand {
public:
    /**
     * @brief Construct a trim command
     * @param model The playlist model
     * @param clipId The clip to trim
     * @param newStartBeat New start position (-1 = keep current)
     * @param newEndBeat New end position (-1 = keep current)
     */
    TrimClipCommand(PlaylistModel& model, ClipInstanceID clipId, double newStartBeat, double newEndBeat)
        : m_model(model), m_clipId(clipId), m_newStartBeat(newStartBeat), m_newEndBeat(newEndBeat) {}

    void execute() override {
        if (m_executed)
            return;

        auto* clip = m_model.getClip(m_clipId);
        if (!clip)
            return;

        // Capture original state
        m_originalStartBeat = clip->startBeat;
        m_originalDuration = clip->durationBeats;

        // Apply trim
        double newStart = (m_newStartBeat < 0) ? clip->startBeat : m_newStartBeat;
        double newEnd = (m_newEndBeat < 0) ? (clip->startBeat + clip->durationBeats) : m_newEndBeat;

        // Validate: end must be after start
        if (newEnd <= newStart) {
            return;  // Invalid trim range - don't apply
        }

        clip->startBeat = newStart;
        clip->durationBeats = newEnd - newStart;

        m_executed = true;
    }

    void undo() override {
        if (!m_executed)
            return;

        auto* clip = m_model.getClip(m_clipId);
        if (!clip)
            return;

        clip->startBeat = m_originalStartBeat;
        clip->durationBeats = m_originalDuration;

        m_executed = false;
    }

    void redo() override {
        execute();
    }

    std::string getName() const override { return "Trim Clip"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    PlaylistModel& m_model;
    ClipInstanceID m_clipId;
    double m_newStartBeat;
    double m_newEndBeat;

    // Original state for undo
    double m_originalStartBeat = 0.0;
    double m_originalDuration = 0.0;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
