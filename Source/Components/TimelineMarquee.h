// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// TimelineMarquee — widget-independent state machine for the timeline's
// marquee (multi-select) drag (#847).
//
// Header-only and widget-free by design, like TimelineInteractionPolicy.h:
// the drag contract is reachable headless (AESTRA_CI=ON disables linking
// AestraUI) so the two failure modes that presented as an app hang are pinned
// by tests:
//   1. An active marquee OWNS every mouse event until the button that began
//      it is released — the pointer can never escape a drag, and a foreign
//      button can never finalize someone else's gesture.
//   2. Per-move work is pure bookkeeping (endpoint update only). This type
//      carries no invalidation, cache, or render surface at all, so a move
//      cannot trigger a per-move invalidation storm.
//
// The widget maps NUIMouseButton onto MarqueeButton tokens and clamps cursor
// positions into the grid area before calling update() — clamping policy
// stays with the widget, sequencing rules live here.

#pragma once

#include <algorithm>

namespace Aestra {
namespace Components {

/// Button identity token; the widget maps NUIMouseButton onto it. 0 = none.
using MarqueeButton = int;

struct TimelineMarqueeDrag {
    bool active = false;
    MarqueeButton button = 0; // the button that began the drag
    float startX = 0.0f;
    float startY = 0.0f;
    float endX = 0.0f;
    float endY = 0.0f;

    /// Begin a drag on a marquee-button press inside the track area. Returns
    /// true when a new drag started (the event is consumed).
    bool begin(bool pressed, MarqueeButton btn, float x, float y, bool inTrackArea) {
        if (!pressed || !inTrackArea || active || btn == 0) {
            return false;
        }
        active = true;
        button = btn;
        startX = x;
        startY = y;
        endX = x;
        endY = y;
        return true;
    }

    /// Move endpoint. The only state change a mouse move may make.
    void update(float x, float y) {
        if (!active) {
            return;
        }
        endX = x;
        endY = y;
    }

    /// While a drag is active it owns every mouse event (#847).
    bool ownsEvent() const { return active; }

    /// True exactly on release of the button that began the drag.
    bool shouldFinalize(bool released, MarqueeButton btn) const {
        return active && released && btn == button;
    }

    /// End the drag; selection applies outside the machine.
    void finalize() {
        active = false;
        button = 0;
    }

    /// Normalized rectangle (min corner + extents) in the same coordinate
    /// space the widget feeds in.
    float rectMinX() const { return std::min(startX, endX); }
    float rectMinY() const { return std::min(startY, endY); }
    float rectMaxX() const { return std::max(startX, endX); }
    float rectMaxY() const { return std::max(startY, endY); }
    float rectWidth() const { return rectMaxX() - rectMinX(); }
    float rectHeight() const { return rectMaxY() - rectMinY(); }
};

} // namespace Components
} // namespace Aestra
