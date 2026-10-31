// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// One note in the piano roll's flat look (FD-48). Its own file because PianoRollNoteLayer.cpp is size-pinned.
#include "NUIPianoRollWidgets.h"
#include "NUIRenderer.h"
#include "PianoRollLook.h"

#include <algorithm>
#include <cmath>

namespace AestraUI {

void PianoRollNoteLayer::paintNote(NUIRenderer& renderer, const NUIRect& r, const MidiNote& n, bool hovered, bool sounding,
                                   float rawWidth) {
    PianoRollLook::NoteState state;
    state.selected = n.selected;
    state.hovered = hovered;
    state.sounding = sounding;
    // Flat and opaque, like a clip: no shadow, no gradient. FD-63: shaded by velocity, then lifted by state.
    const NUIColor fill = PianoRollLook::noteFill(PianoRollLook::velocityFill(noteColor_, n.velocity), state);
    renderer.fillRoundedRect(r, 2.0f, fill);

    // A note that may not play is striped; a ratchet is ticked where its hits split the note.
    if (n.chance < 0.995f) {
        const NUIColor stripe = PianoRollLook::chanceStripe(fill);
        const NUIRect inner(r.x + 1.0f, r.y + 1.0f, r.width - 2.0f, r.height - 2.0f);
        for (const auto& s : PianoRollLook::chanceStripes(inner)) renderer.drawLine(s.a, s.b, 2.5f, stripe);
    }
    for (const float x : PianoRollLook::ratchetTicks(r, n.ratchet))
        renderer.fillRect(NUIRect(std::round(x) - 1.0f, r.y, 2.0f, r.height), NUIColor(0.0f, 0.0f, 0.0f, 0.85f));

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

    // Inside, when there is room: the note's chance and ratchet if it has them ("60%", "×3"), else its pitch.
    constexpr float kMinWidthForLabel = 22.0f;
    if (rawWidth >= kMinWidthForLabel) {
        std::string label = PianoRollLook::expressionTag(n.chance, n.ratchet);
        if (label.empty()) label = MusicTheory::getPitchName(n.pitch);
        constexpr float kFontSize = 10.0f;
        const auto measured = renderer.measureText(label, kFontSize);
        const float labelX = r.x + 6.0f;
        if (labelX + measured.width < r.right() - 4.0f)
            renderer.drawText(label, NUIPoint(labelX, r.y + (r.height - measured.height) * 0.5f), kFontSize,
                              PianoRollLook::noteLabelColor(fill));
    }
}

} // namespace AestraUI
