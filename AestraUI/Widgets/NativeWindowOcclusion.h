// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// When a plugin editor's own window must be hidden because another panel is on top of it.
//
// The editor is a real X window, so it always draws over anything Aestra paints. A panel raised over an editor (the mixer, an EQ,
// a second plugin) would go behind the plugin's window, or leave the editor's frame behind the panel while its window stays in
// front, so its controls and its close button become unreachable. A covered editor's window is therefore hidden until the panel is
// raised. The covered test follows the real z-order: later siblings are above, and layers are transparent. Pure, so a test can
// pin it without an X server.

#include "../Core/NUIComponent.h"

namespace AestraUI {

/// How long an editor must stay covered before its window is hidden, so a toast or a quick drag across it does not make it flicker.
constexpr double kHideAfterCoveredSeconds = 0.12;

inline bool shouldHideNativeWindow(double coveredSeconds) { return coveredSeconds >= kHideAfterCoveredSeconds; }

/// Seconds covered after another @p dt: counting only while covered, starting again the moment it is uncovered.
inline double awayCoveredSeconds(double previous, double dt, bool coveredNow) {
    return coveredNow ? previous + dt : 0.0;
}

namespace detail {
inline bool overlaps(const NUIRect& a, const NUIRect& b) {
    return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

// Whether @p c, or something inside it, is an opaque panel over @p view. A layer is transparent: only what it holds counts.
inline bool occludes(const NUIComponent& c, const NUIRect& view) {
    if (!c.isVisible() || !overlaps(c.getBounds(), view)) return false;
    if (c.occludesNativeWindows()) return true;
    for (const auto& child : c.getChildren()) {
        if (child && occludes(*child, view)) return true;
    }
    return false;
}
} // namespace detail

/// True when an opaque panel above @p node in the real z-order overlaps @p view (the rectangle of its native window).
inline bool coveredByPanelAbove(const NUIComponent& node, const NUIRect& view) {
    for (const NUIComponent* n = &node; n->getParent(); n = n->getParent()) {
        bool above = false;
        for (const auto& sibling : n->getParent()->getChildren()) {
            if (sibling.get() == n) {
                above = true; // everything after this one is drawn over it
                continue;
            }
            if (above && sibling && detail::occludes(*sibling, view)) return true;
        }
    }
    return false;
}

} // namespace AestraUI
