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
//      button can never finalize someone else's gesture or move its band.
//   2. Per-move work is pure bookkeeping (endpoint update only). This type
//      carries no invalidation, cache, or render surface at all, so a move
//      cannot trigger a per-move invalidation storm.
//
// The widget maps NUIMouseButton onto MarqueeButton tokens, clamps cursor
// positions into the grid area, and routes an active marquee BEFORE sibling
// controls (the minimap consumes in-bounds releases even when idle, which
// would strand the gesture) — clamping and routing policy stay with the
// widget, sequencing rules live here.

#pragma once

#include <algorithm>

namespace Aestra {
namespace Components {

/// Button identity token; the widget maps NUIMouseButton onto it. 0 = none.
using MarqueeButton = int;

/// Event classification the widget derives from NUIMouseEventType. The
/// machine never infers event shape from flag combinations — a wheel event
/// (pressed=false, released=false) maps to Other, never to Move (#847).
enum class MarqueeEventKind {
    Move,    // pointer move/drag: endpoint follows the position
    Press,   // button down: consumed, no state change
    Release, // button up: endpoint follows, finalizes when initiating button
    Other,   // scroll, enter/leave, synthetic: consumed, no state change
};

struct TimelineMarqueeDrag {
    /// Begin a drag on a marquee-button press inside the track area. Returns
    /// true when a new drag started (the event is consumed).
    bool begin(bool pressed, MarqueeButton btn, float x, float y, bool inTrackArea) {
        if (!pressed || !inTrackArea || m_active || btn == 0) {
            return false;
        }
        m_active = true;
        m_button = btn;
        m_startX = x;
        m_startY = y;
        m_endX = x;
        m_endY = y;
        return true;
    }

    /// While a drag is active it owns every mouse event (#847).
    bool ownsEvent() const { return m_active; }

    /// Apply one routed event while active. Endpoint moves on Move events and
    /// on the initiating button's Release (its position is part of the
    /// gesture); Press, Other, and a foreign button's Release change nothing.
    /// Returns true exactly when this event finalizes the drag.
    bool onEvent(MarqueeEventKind kind, MarqueeButton btn, float x, float y) {
        if (!m_active) {
            return false;
        }
        if (kind == MarqueeEventKind::Release) {
            if (btn != m_button) {
                return false; // foreign release: consumed, endpoint untouched
            }
            m_endX = x;
            m_endY = y;
            return true;
        }
        if (kind == MarqueeEventKind::Move) {
            m_endX = x;
            m_endY = y;
        }
        return false; // Press and Other: consumed, no state change
    }

    /// End the drag; selection applies outside the machine.
    void finalize() {
        m_active = false;
        m_button = 0;
    }

    // ── Read-only queries (consumers + tests) ──
    bool active() const { return m_active; }
    MarqueeButton button() const { return m_button; }
    float startX() const { return m_startX; }
    float startY() const { return m_startY; }
    float endX() const { return m_endX; }
    float endY() const { return m_endY; }

    /// Normalized rectangle (min corner + extents) in the same coordinate
    /// space the widget feeds in.
    float rectMinX() const { return std::min(m_startX, m_endX); }
    float rectMinY() const { return std::min(m_startY, m_endY); }
    float rectMaxX() const { return std::max(m_startX, m_endX); }
    float rectMaxY() const { return std::max(m_startY, m_endY); }
    float rectWidth() const { return rectMaxX() - rectMinX(); }
    float rectHeight() const { return rectMaxY() - rectMinY(); }

private:
    bool m_active = false;
    MarqueeButton m_button = 0; // the button that began the drag
    float m_startX = 0.0f;
    float m_startY = 0.0f;
    float m_endX = 0.0f;
    float m_endY = 0.0f;
};

} // namespace Components
} // namespace Aestra
