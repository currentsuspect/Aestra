// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUITypes.h"

#include <cmath>
#include <cstdint>

namespace AestraUI {

/**
 * @brief Pairs mouse presses into double-clicks, in the one place mouse events are made (NUIPlatformBridge).
 *
 * Nothing in the platform ever assigned NUIMouseEvent::doubleClick, so every control that read only the flag was dead
 * (see NUIDoubleClickTracker, which says so). Setting it here makes all of them work at once.
 *
 * A press pairs with the one before it when it is the same button, within kWindowMs and within kTolerancePx of where
 * that press was. A pair is consumed, so a third rapid press is single again.
 *
 * Knobs need one more rule. The first click of a knob's double-click starts a cursor capture, and ending it warps the
 * cursor to the knob's centre, so the second press lands there and not where the first did. noteCursorWarp() records
 * that spot as a second place the pending press counts from. Where the warp does nothing (native Wayland) the pointer
 * stays where the first press left it, which is the first place, so both are accepted.
 *
 * Pure: the caller passes the time, so a test controls it.
 */
class NUIPressPairer {
public:
    static constexpr int64_t kWindowMs = 400;
    static constexpr float kTolerancePx = 6.0f;

    /// @return true when this press completes a double-click.
    bool registerPress(NUIMouseButton button, float x, float y, int64_t nowMs) {
        const bool pair = m_pending && button == m_button && (nowMs - m_ms) < kWindowMs &&
                          (near(x, y, m_x, m_y) || (m_warpValid && near(x, y, m_warpX, m_warpY)));
        if (pair) {
            m_pending = false;
            m_warpValid = false;
            return true;
        }
        m_pending = true;
        m_button = button;
        m_x = x;
        m_y = y;
        m_ms = nowMs;
        m_warpValid = false;
        return false;
    }

    /// The cursor was warped to (x, y) when a capture ended: the pending press also counts from there.
    void noteCursorWarp(float x, float y) {
        if (!m_pending) return;
        m_warpX = x;
        m_warpY = y;
        m_warpValid = true;
    }

    void reset() {
        m_pending = false;
        m_warpValid = false;
    }

private:
    static bool near(float ax, float ay, float bx, float by) {
        return std::fabs(ax - bx) <= kTolerancePx && std::fabs(ay - by) <= kTolerancePx;
    }

    bool m_pending = false;
    NUIMouseButton m_button = NUIMouseButton::None;
    float m_x = 0.0f, m_y = 0.0f;
    int64_t m_ms = 0;
    bool m_warpValid = false;
    float m_warpX = 0.0f, m_warpY = 0.0f;
};

} // namespace AestraUI
