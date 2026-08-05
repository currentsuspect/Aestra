// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <chrono>

namespace AestraUI {

/**
 * @brief Double-click detection for platforms that never populate the event flag.
 *
 * Nothing in the tree assigns NUIMouseEvent::doubleClick, so any control that
 * only checks that flag has a dead code path. Every control that wanted a
 * double-click therefore paired up quick presses by hand, each with its own
 * timestamp field and its own copy of the threshold — which is exactly how the
 * window silently drifts apart between controls when one copy gets tuned.
 *
 * Usage:
 * @code
 *   if (event.pressed && event.button == NUIMouseButton::Left) {
 *       if (m_clickTracker.registerPress() || event.doubleClick) {
 *           resetToDefault();
 *           return true;
 *       }
 *   }
 * @endcode
 *
 * The platform flag is still worth OR-ing in: if a backend ever starts setting
 * it, that path keeps working.
 */
class NUIDoubleClickTracker {
public:
    /// Window within which a second press counts as a double-click.
    static constexpr long long kDoubleClickMs = 400;

    /**
     * @brief Record a press and report whether it completed a double-click.
     *
     * A detected double-click consumes the stored timestamp, so three rapid
     * presses read as one double-click plus one single press rather than two
     * overlapping double-clicks.
     */
    bool registerPress()
    {
        const long long now = nowMillis();
        const bool isDouble = (m_lastPressMs != 0) && (now - m_lastPressMs < kDoubleClickMs);
        m_lastPressMs = isDouble ? 0 : now;
        return isDouble;
    }

    /// Forget any pending first press (e.g. when a drag starts instead).
    void reset() { m_lastPressMs = 0; }

private:
    static long long nowMillis()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    long long m_lastPressMs{0};
};

} // namespace AestraUI
