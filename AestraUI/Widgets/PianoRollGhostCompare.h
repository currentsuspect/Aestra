// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIPianoRollWidgets.h"

#include <vector>

namespace AestraUI {

// What a ghost overlay draws. PianoRollPanel rebuilds the ghosts from every pattern on
// every frame; re-setting identical ones must not force a repaint (SPEC 3 §4).
inline bool sameGhostPatterns(const std::vector<PianoRollNoteLayer::GhostPattern>& a,
                              const std::vector<PianoRollNoteLayer::GhostPattern>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto& x = a[i];
        const auto& y = b[i];
        if (x.color != y.color || x.fillAlpha != y.fillAlpha || x.strokeAlpha != y.strokeAlpha ||
            x.notes.size() != y.notes.size()) {
            return false;
        }
        for (size_t n = 0; n < x.notes.size(); ++n) {
            const auto& p = x.notes[n];
            const auto& q = y.notes[n];
            if (p.pitch != q.pitch || p.startBeat != q.startBeat || p.durationBeats != q.durationBeats ||
                p.velocity != q.velocity || p.pan != q.pan || p.unitId != q.unitId || p.isDeleted != q.isDeleted) {
                return false;
            }
        }
    }
    return true;
}

} // namespace AestraUI
