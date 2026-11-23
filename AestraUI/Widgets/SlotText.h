// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cmath>
#include <string>

namespace AestraUI {
namespace SlotText {

/**
 * @brief Where a label's top edge goes so it sits centred in its slot (owner, 22 Nov: "text on the mixer's slots is not
 *        vertically aligned"). What the eye reads is the capitals and digits, so it is the cap height that is centred
 *        on the slot (not the line box, which reserves room for descenders and left 11px pill digits a pixel low),
 *        and the result is whole pixels. A slot shows the same baseline whether it holds a name, a dot or nothing.
 *        Pure: no hand-tuned offsets.
 */
inline float slotTextY(float slotY, float slotHeight, float ascent, float capHeight) {
    return std::round(slotY + slotHeight * 0.5f + capHeight * 0.5f - ascent);
}

/// The same for a renderer and a slot rectangle. The renderer raises small sizes to a readability floor before it
/// draws, and getFontMetrics() answers for the size asked, so its ascent and cap height are scaled by how much
/// taller the line measureText() reports (which applies the floor) is than the line the metrics describe.
template <class Renderer, class Rect>
float slotTextY(Renderer& renderer, const Rect& slot, float fontSize) {
    const auto m = renderer.getFontMetrics(fontSize);
    const float drawnLine = renderer.measureText(std::string(), fontSize).height;
    const float k = (m.lineHeight > 0.0f && drawnLine > 0.0f) ? drawnLine / m.lineHeight : 1.0f;
    const float ascent = (m.ascent > 0.0f ? m.ascent : fontSize * 0.8f) * k;
    const float cap = (m.capHeight > 0.0f ? m.capHeight : fontSize * 0.7f) * k;
    return slotTextY(slot.y, slot.height, ascent, cap);
}

} // namespace SlotText
} // namespace AestraUI
