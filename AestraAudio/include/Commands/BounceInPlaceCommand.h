// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/ClipInstance.h"
#include "Models/PatternSource.h"
#include "Core/AutomationCurve.h"
#include "Models/PlaylistModel.h"

#include <memory>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

class TrackManager;

/**
 * @brief Bounce in Place (V8-W5): replace clips with the audio their track made of them.
 *
 * The caller has already rendered the channel's own output over the clips' span
 * (AudioExporter with captureMixerChannelId: inserts, insert-parameter
 * automation, delay compensation; fader and pan stay live afterwards). This
 * command is the project edit, as ONE undo step:
 *
 *  1. the rendered audio becomes a new source and pattern;
 *  2. the selected clips are removed and one clip spanning them takes their place;
 *  3. the channel's inserts are bypassed, so the effects are not applied twice;
 *  4. the channel's plugin-parameter automation is muted, for the same reason.
 *
 * Nothing is deleted: removed clips, the previous bypass flags and the muted
 * curves' points are all kept so undo restores them exactly, and redo reuses
 * the rendered file instead of rendering again.
 */
class BounceInPlaceCommand final : public ICommand {
public:
    BounceInPlaceCommand(TrackManager& manager, std::vector<ClipInstanceID> clipIds, uint32_t mixerChannelId,
                         std::shared_ptr<AudioBufferData> rendered, double startBeat, double lengthBeats);

    void execute() override;
    void undo() override;
    void redo() override { execute(); }

    std::string getName() const override { return "Bounce in Place"; }
    size_t getSizeInBytes() const override;
    bool changesProjectState() const override { return true; }
    bool isUndoable() const override { return m_executed; }

    /** @brief The clip that replaced the selection; invalid until execute() succeeds. */
    ClipInstanceID bouncedClipId() const { return m_newClip.id; }

private:
    struct RemovedClip {
        ClipInstance clip;
        PlaylistLaneID lane;
    };
    struct LaneAutomation {
        PlaylistLaneID lane;
        std::vector<AutomationCurve> before;
    };

    void bypassInserts();
    void muteInsertAutomation();
    void restoreInserts();
    void restoreAutomation();

    TrackManager& m_manager;
    std::vector<ClipInstanceID> m_clipIds;
    uint32_t m_mixerChannelId;
    std::shared_ptr<AudioBufferData> m_rendered;
    double m_startBeat;
    double m_lengthBeats;

    std::vector<RemovedClip> m_removed;
    ClipInstance m_newClip;
    PlaylistLaneID m_newClipLane;
    PatternID m_patternId;
    std::unique_ptr<PatternSource> m_detachedPattern;
    std::vector<size_t> m_bypassedByUs;        ///< slots this command switched to bypassed
    std::vector<LaneAutomation> m_mutedLanes;  ///< lanes whose curves this command changed, as they were
    bool m_executed{false};
};

} // namespace Audio
} // namespace Aestra
