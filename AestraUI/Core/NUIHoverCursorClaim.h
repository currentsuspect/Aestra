// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// NUIHoverCursorClaim — when a hovered widget may touch the shared cursor.
//
// The cursor is one global value: the last setCursorStyle() wins. A widget that resets it to the
// arrow on EVERY move outside itself overrides whatever the widget under the pointer set, and
// does so on every motion event. The piano-roll minimap did exactly that: it sits in front of
// the note layer, so mid-note-drag it reset the Grabbing hand to the arrow on the next move
// (SPEC 3 §3.4, "when I drag, it changes back to the normal cursor").
//
// The rule: a widget sets the cursor only while the pointer is over it, and hands it back ONCE,
// on the way out, and only if it set it.

#pragma once

#include "../Platform/NUICursorStyle.h"

#include <optional>

namespace AestraUI {

class NUIHoverCursorClaim {
public:
    /** The pointer is over the widget: it may set @p style (and now owns the cursor). */
    std::optional<NUICursorStyle> inside(NUICursorStyle style) {
        m_claimed = true;
        return style;
    }
    /** The pointer is elsewhere: the arrow once if this widget set the cursor, else hands off. */
    std::optional<NUICursorStyle> outside() {
        if (!m_claimed) {
            return std::nullopt;
        }
        m_claimed = false;
        return NUICursorStyle::Arrow;
    }
    bool claimed() const { return m_claimed; }

private:
    bool m_claimed = false;
};

} // namespace AestraUI
