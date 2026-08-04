// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

/**
 * @file NUICursorService.h
 * @brief Single owner of pointer-capture ("infinite drag") cursor behavior.
 *
 * Continuous parameter controls (knobs, parameter sliders, faders) hide the
 * cursor while dragging and restore it at a semantically meaningful position
 * on release. Before this service, three widgets each hand-rolled that
 * pattern with divergent warp targets, restore ordering, and no platform
 * awareness (see Aestra-Internals: aestra-docs/cursor-unification-map-2026-07.md).
 *
 * The service is the one place that knows:
 *  - hide/unhide ordering (always warp BEFORE unhide, so the cursor never
 *    renders a frame at the drifted position),
 *  - pointer confinement during capture (native Wayland warps are silent
 *    no-ops without pointer focus; grabbing keeps focus valid so the
 *    release-warp lands — with a safe no-warp fallback otherwise),
 *  - platform coordinate conversion (delegated to the platform window,
 *    whose contract is window-relative UI coordinates).
 *
 * Phase 1 scope: rotary NUISlider is the reference client. Interaction-based
 * product rule (owner, 2026-07-19): all continuous parameter controls adopt
 * capture in later phases; spatial interactions (scrollbars, timeline/clip
 * drags, piano roll, range handles) keep the normal cursor.
 */

namespace AestraUI {

/** Where the cursor reappears when a capture ends. */
enum class NUICursorRestorePolicy {
    KnobCenter,     ///< Center of the control's bounds (rotary knobs).
    ThumbPosition,  ///< Position representing the current value (linear thumbs).
    GrabOrigin      ///< Where the pointer was when capture began.
};

/**
 * @brief Minimal host the service drives. Implemented by NUIPlatformBridge;
 * a fake in tests exercises the state machine headlessly.
 */
class NUICursorHost {
public:
    virtual ~NUICursorHost() = default;
    virtual void hostHideCursor() = 0;                 ///< Style-channel hide (Hidden).
    virtual void hostShowCursor() = 0;                 ///< Style-channel restore (Arrow).
    virtual void hostWarpCursor(int x, int y) = 0;     ///< Window-relative UI coords.
    virtual void hostSetPointerGrab(bool grabbed) = 0; ///< Confine pointer to window.
};

/**
 * @brief State machine: Idle <-> Captured. One capture at a time (pointer
 * interactions are inherently exclusive); re-begin while captured is treated
 * as a cancel + begin so a lost release event can never wedge the cursor.
 */
class NUICursorService {
public:
    explicit NUICursorService(NUICursorHost& host) : m_host(host) {}

    /**
     * Begin an infinite-drag capture: hides the cursor and confines the pointer.
     * @return false (no capture started) when called reentrantly from an
     * end/cancel host callback — the old capture must fully transition to
     * idle before a new one may begin.
     */
    bool beginDragCapture(NUICursorRestorePolicy policy, int grabOriginX, int grabOriginY);

    /**
     * End the capture: warp to the restore point, then unhide, then release
     * the grab. @p restoreX/Y are the client-computed restore coordinates for
     * the policy (KnobCenter/ThumbPosition); ignored for GrabOrigin, which
     * uses the stored begin position.
     */
    void endDragCapture(int restoreX, int restoreY);

    /**
     * Abort without warping (focus lost, window deactivated, client destroyed
     * mid-drag). Unhides and releases the grab; the cursor stays where the
     * platform last had it — the safe fallback when a warp cannot land.
     */
    void cancelDragCapture();

    bool isCaptured() const { return m_captured; }
    NUICursorRestorePolicy activePolicy() const { return m_policy; }

    /**
     * Anchored logical cursor position while captured (the grab origin — a
     * point on the control). The physical pointer only produces deltas during
     * capture; anything that needs "where the cursor is" (wheel routing,
     * position queries) should see the anchor, not the drifting pointer.
     */
    int anchorX() const { return m_grabOriginX; }
    int anchorY() const { return m_grabOriginY; }

    /** Semantic drag delta returned by feedPhysicalMotion(). */
    struct Delta { int dx = 0; int dy = 0; };

    /**
     * Feed one raw physical pointer position while captured. Returns the
     * semantic drag delta (motion since the last recenter) and, as a side
     * effect, recenters the physical pointer to the anchor via the host so it
     * can never reach the window edge (which would saturate deltas on long
     * drags). The synthetic motion produced by that recenter warp is detected
     * and returned as a zero delta — so the OS pointer becomes a pure
     * implementation detail; widgets consume only the delta.
     * No-op returning {0,0} when not captured.
     */
    Delta feedPhysicalMotion(int physX, int physY);

private:
    NUICursorHost& m_host;
    bool m_captured = false;
    bool m_inTransition = false; // guards against re-begin from end/cancel callbacks
    NUICursorRestorePolicy m_policy = NUICursorRestorePolicy::GrabOrigin;
    int m_grabOriginX = 0;
    int m_grabOriginY = 0;
    // Delta/recenter accounting (feedPhysicalMotion):
    int m_lastPhysX = 0;
    int m_lastPhysY = 0;
    bool m_expectRecenterEvent = false; // next motion to the anchor is our own warp
};

} // namespace AestraUI
