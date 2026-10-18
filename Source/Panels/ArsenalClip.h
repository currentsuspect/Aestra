// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIRenderer.h"
#include "NUITypes.h"

namespace Aestra::ArsenalClip {

/// The renderer keeps one clip rectangle, not a stack: a nested part that sets its own clip and then clears it
/// leaves everything drawn after it unclipped. Parts drawn inside the Arsenal list take the list viewport as
/// @p outer, clip to their rectangle within it, and put it back afterwards, so scrolled rows stay in the list.
inline bool hasOuter(const AestraUI::NUIRect& outer) { return outer.width > 0.0f && outer.height > 0.0f; }

inline void within(AestraUI::NUIRenderer& r, const AestraUI::NUIRect& rect, const AestraUI::NUIRect& outer) {
    r.setClipRect(hasOuter(outer) ? AestraUI::intersectRects(rect, outer) : rect);
}

inline void restore(AestraUI::NUIRenderer& r, const AestraUI::NUIRect& outer) {
    if (hasOuter(outer))
        r.setClipRect(outer);
    else
        r.clearClipRect();
}

} // namespace Aestra::ArsenalClip
