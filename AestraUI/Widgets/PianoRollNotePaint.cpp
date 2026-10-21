// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// One note in the piano roll's flat look (FD-48). Its own file because PianoRollNoteLayer.cpp is size-pinned.
#include "NUIPianoRollWidgets.h"
#include "NUIRenderer.h"
#include "PianoRollLook.h"

#include <algorithm>

namespace AestraUI {

void PianoRollNoteLayer::paintNote(NUIRenderer& renderer, const NUIRect& r, const MidiNote& n, bool hovered, bool sounding,
                                   float rawWidth) {
    PianoRollLook::NoteState state;
    state.selected = n.selected;
    state.hovered = hovered;
    state.sounding = sounding;
    const NUIColor fill = PianoRollLook::noteFill(noteColor_, state);

    // Flat and opaque, like a clip: no shadow, no gradient, no alpha by velocity (the velocity lane says that).
    renderer.fillRoundedRect(r, 2.0f, fill);
    if (n.selected) renderer.strokeRoundedRect(r, 2.0f, 1.0f, NUIColor::white().withAlpha(0.95f));

    // Selected notes keep their resize grips; the edge under the pointer goes full white.
    if (n.selected || (hovered && (hoverOnRightEdge_ || hoverOnLeftEdge_))) {
        const NUIColor idleGrip = NUIColor::white().withAlpha(0.9f);
        const NUIColor activeGrip = NUIColor::white();
        if (n.selected || hoverOnRightEdge_) {
            const NUIColor grip = (hovered && hoverOnRightEdge_) ? activeGrip : idleGrip;
            renderer.fillRect(NUIRect(r.right() - 3.0f, r.y + 3.0f, 2.0f, r.height - 6.0f), grip);
        }
        if (n.selected || hoverOnLeftEdge_) {
            const NUIColor grip = (hovered && hoverOnLeftEdge_) ? activeGrip : idleGrip;
            renderer.fillRect(NUIRect(r.x + 1.0f, r.y + 3.0f, 2.0f, r.height - 6.0f), grip);
        }
    }

    // The pitch name inside, when there is room for it.
    constexpr float kMinWidthForLabel = 22.0f;
    if (rawWidth >= kMinWidthForLabel) {
        const std::string label = MusicTheory::getPitchName(n.pitch);
        constexpr float kFontSize = 10.0f;
        const auto measured = renderer.measureText(label, kFontSize);
        const float labelX = r.x + 6.0f;
        if (labelX + measured.width < r.right() - 4.0f)
            renderer.drawText(label, NUIPoint(labelX, r.y + (r.height - measured.height) * 0.5f), kFontSize,
                              PianoRollLook::noteLabelColor(fill));
    }
}

} // namespace AestraUI
