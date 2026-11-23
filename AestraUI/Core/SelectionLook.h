// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The one place that turns a theme and a selection state into colours (Selection System Spec, section 2.2; Ink,
// FD-100). Every surface that marks something selected paints through this, so selection cannot look different in
// the Arsenal, the timeline and the browser. Nothing here draws: theme + state in, colours out, so
// SelectionLookTest checks every state in every kit theme.
//
// The rule: selection is a SOLID lit fill under its content, never an alpha wash. It reads only the selection
// family of tokens (selected, selectedText, selectionFill, selectionTint, selectionEdge, hoverSelected, accentText).
//
// Alpha 0 means "this part has no selection look": the surface keeps its own colour for it.

#include "NUIThemeSystem.h"

#include <algorithm>
#include <cmath>

namespace AestraUI {

struct SelectionState {
    bool selected = false;
    bool hovered = false;
    bool pressed = false;
    bool disabled = false;
    bool keyOwner = false; ///< this item's surface owns the selection keys right now (the title-bar tick)
    bool multi = false;    ///< selected together with others: members get an outline so neighbours stay separate
};

struct SelectionColors {
    NUIColor fill;      ///< under the item, resolved for this state (hover and press included); alpha 0 = none
    NUIColor text;      ///< a label on that fill; alpha 0 = the surface keeps its own text colour
    NUIColor edge;      ///< the item's outline; alpha 0 = none
    NUIColor handle;    ///< a grip or glyph that sits on the fill
    NUIColor hoverFill; ///< what `fill` becomes under the pointer, whatever the current hover state is
    NUIColor rail;      ///< the key-owner tick on a panel title; alpha 0 = not the key owner
};

namespace selection_look_detail {
inline NUIColor none() { return NUIColor(0.0f, 0.0f, 0.0f, 0.0f); }
inline float lin(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
inline float lum(const NUIColor& c) { return 0.2126f * lin(c.r) + 0.7152f * lin(c.g) + 0.0722f * lin(c.b); }
inline float ratio(const NUIColor& a, const NUIColor& b) {
    const float x = lum(a), y = lum(b);
    return (std::max(x, y) + 0.05f) / (std::min(x, y) + 0.05f);
}
constexpr float kText = 4.5f; ///< a label on its fill
constexpr float kMark = 3.0f; ///< a part that must be found

/// The label colour for a fill: the theme's own selected text if it reads, else its primary text, else whichever of
/// the two reads better. The token docs name selectedText for the solid fill and textPrimary for the quieter ones
/// (selectionFill, selectionTint), and a theme's hover or press fill may land on either side of that line.
inline NUIColor textOn(const NUIThemeProperties& t, const NUIColor& fill) {
    if (ratio(t.selectedText, fill) >= kText) return t.selectedText;
    if (ratio(t.textPrimary, fill) >= kText) return t.textPrimary;
    return ratio(t.selectedText, fill) >= ratio(t.textPrimary, fill) ? t.selectedText : t.textPrimary;
}
/// A fill is opaque in the finished picture. A theme whose `selected` is a translucent wash (the legacy kits) is
/// flattened over the card so the colour that reaches the screen is the one the contrast table measured.
inline NUIColor solid(const NUIColor& c, const NUIColor& under) {
    if (c.a >= 1.0f) return c;
    NUIColor out = NUIColor::lerp(under, c, c.a);
    out.a = 1.0f;
    return out;
}
} // namespace selection_look_detail

inline SelectionColors selectionLook(const NUIThemeProperties& t, const SelectionState& s) {
    using selection_look_detail::none;
    using selection_look_detail::solid;
    const NUIColor under = t.backgroundSecondary;
    SelectionColors c;
    c.rail = s.keyOwner ? solid(t.accentText, under) : none();

    if (!s.selected) {
        // Not selected: no fill at rest. Under the pointer a quiet tint; while pressed, the step deeper that previews
        // the selection the press is about to make.
        c.hoverFill = solid(t.selectionTint, under);
        c.fill = s.disabled ? none() : (s.pressed ? solid(t.selectionFill, under) : (s.hovered ? c.hoverFill : none()));
        c.text = none();
        c.edge = none();
        c.handle = none();
        return c;
    }

    const NUIColor lit = solid(t.selected, under);
    // The pointer lightens (or darkens) the lit fill toward the theme's hoverSelected, as far as the label still
    // reads on it. A theme whose hoverSelected would cost the label its 4.5:1 gets the nearest step that does, never
    // an unreadable hover and never a hover that looks like the resting state when a readable step exists.
    NUIColor hover = solid(t.hoverSelected, under);
    for (int step = 10; step >= 0 && selection_look_detail::ratio(t.selectedText, hover) < selection_look_detail::kText;
         --step)
        hover = NUIColor::lerp(lit, solid(t.hoverSelected, under), static_cast<float>(step) / 10.0f);
    c.hoverFill = hover;
    if (s.disabled) {
        // A disabled item that is selected still reads as selected, but quiet: the tint, not the lit fill.
        c.fill = solid(t.selectionTint, under);
        c.text = t.selectedText.withAlpha(0.45f);
        c.handle = t.selectedText.withAlpha(0.45f);
        c.edge = s.multi ? solid(t.selectionEdge, under) : none();
        return c;
    }
    c.fill = s.pressed ? solid(t.selectionFill, under) : (s.hovered ? c.hoverFill : lit);
    c.text = selection_look_detail::textOn(t, c.fill);
    c.handle = c.text;
    // One selected item needs no outline when the solid fill is itself a mark you can find (3:1 on the bed and on a
    // card). Where it is not (the high-contrast kits pair a translucent fill with a strong edge) the edge carries the
    // mark. Several side by side would read as one block, so each member outlines itself.
    const bool fillIsAMark = selection_look_detail::ratio(lit, t.backgroundPrimary) >= selection_look_detail::kMark &&
                             selection_look_detail::ratio(lit, t.backgroundSecondary) >= selection_look_detail::kMark;
    c.edge = (s.multi || !fillIsAMark) ? solid(t.selectionEdge, under) : none();
    return c;
}

} // namespace AestraUI
