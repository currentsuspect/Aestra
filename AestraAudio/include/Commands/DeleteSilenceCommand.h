// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Analysis/SilenceDetector.h"
#include "Commands/ICommand.h"
#include "Commands/MacroCommand.h"
#include "Models/ClipInstance.h"

#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

class TrackManager;

/// Which clips close the gap a Delete Silence cut leaves.
enum class SilenceRipple {
    /// Only clips later on the cut clip's own lane move left. The founder's default (2026-11-07).
    ThisLane,
    /// Every clip on every lane after the cut moves left, so the project stays in sync.
    AllLanes,
};

/**
 * @brief Cut the measured dead air out of audio clips, one undo step.
 *
 * Founder rulings (2026-11-07): ripple scope is chosen per use and defaults to
 * "this lane only"; the other choice is "every lane after the cut".
 *
 * For each selected audio clip the command measures its audio (SilenceDetector),
 * then for every range, right to left: splits the clip at both ends of the range,
 * removes the middle piece, moves later clips left by the range's length, and
 * puts a short fade on the two new edges. Right to left means the cuts still to
 * make never shift. Clips are taken latest-first for the same reason. Pattern and
 * MIDI clips are ignored.
 *
 * It is composed from the ordinary clip commands inside a MacroCommand, so undo
 * and redo are exactly theirs. The sub-commands cannot be built up front (a
 * split's second half only gets its id when it runs), so the first execute()
 * builds and runs them in turn; redo() replays the recorded macro.
 */
class DeleteSilenceCommand final : public ICommand {
public:
    DeleteSilenceCommand(TrackManager& manager, std::vector<ClipInstanceID> clips,
                         SilenceRipple ripple = SilenceRipple::ThisLane, SilenceParams params = {});

    void execute() override;
    void undo() override { m_macro.undo(); }
    void redo() override { m_macro.redo(); }

    std::string getName() const override { return "Delete Silence"; }
    size_t getSizeInBytes() const override { return sizeof(*this) + m_macro.getSizeInBytes(); }
    bool changesProjectState() const override { return true; }
    /// False when nothing was cut: no silence found, or no audio clip to look at.
    bool isUndoable() const override { return !m_macro.empty(); }

    /// Seconds of silence removed by the first execute(), summed over every cut (timeline seconds).
    double removedSeconds() const { return m_removedSeconds; }
    size_t cutCount() const { return m_cuts; }

private:
    void cutClip(const ClipInstanceID& clipId);

    TrackManager& m_manager;
    std::vector<ClipInstanceID> m_clips;
    SilenceRipple m_ripple;
    SilenceParams m_params;
    MacroCommand m_macro{"Delete Silence"};
    bool m_executed{false};
    double m_removedSeconds{0.0};
    size_t m_cuts{0};
};

} // namespace Audio
} // namespace Aestra
