// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// V82-PR1 (FD-52): duplicate like FL. The rules are in PianoRollDuplicate.h; this applies them to the note layer.
// Its own file because PianoRollNoteLayer.cpp is size-pinned.
#include "NUIPianoRollWidgets.h"
#include "PianoRollDuplicate.h"

#include <algorithm>

namespace AestraUI {

std::optional<std::pair<double, double>>
PianoRollNoteLayer::duplicateLikeFl(const std::optional<std::pair<double, double>>& ruler) {
    namespace Dup = PianoRollDuplicate;
    // The step follows the snap grid; with snap off it is the selection's own length.
    const double snapStep = snap_ == SnapGrid::None ? 0.0 : MusicTheory::getSnapDuration(snap_);
    const auto plan = Dup::plan(notes_, ruler, snapStep);
    if (!plan) return std::nullopt;

    const auto made = Dup::copies(notes_, *plan);
    // The pattern has to hold the copies, and in range mode the highlight that moves onto them.
    double needed = Dup::endOf(made);
    if (plan->rangeMode) needed = std::max(needed, plan->to.second);
    if (needed > 0.0 && onNeedPatternEnd_) onNeedPatternEnd_(needed);

    if (!made.empty()) {
        for (auto& n : notes_) n.selected = false;
        notes_.insert(notes_.end(), made.begin(), made.end());
        commitNotes(); // one step for the whole press
    }
    repaint();
    return plan->rangeMode ? std::optional<std::pair<double, double>>(plan->to) : std::nullopt;
}

void PianoRollNoteLayer::selectStartingIn(double startBeat, double endBeat) {
    for (auto& n : notes_) n.selected = !n.isDeleted && PianoRollDuplicate::startsIn(n, {startBeat, endBeat});
    repaint();
}

} // namespace AestraUI
