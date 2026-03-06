// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ClipInstance.h"
#include "Commands/ICommand.h"
#include "PlaylistModel.h"

namespace Aestra {
namespace Audio {

class SplitClipCommand : public ICommand {
public:
    SplitClipCommand(PlaylistModel& model, ClipInstanceID clipId, double splitBeat);

    void execute() override;
    void undo() override;
    void redo() override;

    std::string getName() const override { return "Split Clip"; }

private:
    PlaylistModel& m_model;
    ClipInstanceID m_targetClipId;
    double m_splitBeat;

    // State captured after first execution
    bool m_executed = false;
    ClipInstanceID m_newClipId;
    ClipInstance m_newClipState; // Copy of the create clip for redo

    // Original state for undo
    double m_originalDuration = 0.0;
    float m_originalFadeOut = 0.0f;

    // Helper to find which lane the clip is on
    PlaylistLaneID m_laneId;
};

} // namespace Audio
} // namespace Aestra
