// © 2025 Aestra Studios All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Internal helpers shared by the TrackManagerUI* translation units (split
// out of the former monolithic TrackManagerUI.cpp). Not part of the public
// API — include only from TrackManagerUI*.cpp files, after TrackManagerUI.h.

#include "../AestraUI/Base/NUIContextMenu.h"

#include <algorithm>
#include <cmath>
#include <memory>

// Remotery profiling stubs (disabled)
#define rmt_ScopedCPUSample(name, flags) ((void)0)
#define rmt_BeginCPUSample(name, flags) ((void)0)
#define rmt_EndCPUSample() ((void)0)

namespace Aestra {
namespace Audio {

// Shared by layout, rendering, hit testing, and drag math so visible and
// interactive geometry cannot drift independently.
constexpr float kTimelineHeaderHeight = 38.0f;
constexpr float kTimelineRulerHeight = 28.0f;
constexpr float kTimelineHorizontalScrollbarHeight = 24.0f;
constexpr float kTimelineScrollbarWidth = 15.0f;

/**
 * @brief Gap between the track-controls column and the first grid pixel.
 *
 * Appeared as a bare `+ 5` in 30-odd `gridStartX` expressions across every
 * TrackManagerUI translation unit (#550). Rendering, hit testing and drag math
 * all have to agree on it, and a literal repeated that many times agrees only
 * by luck.
 */
constexpr float kTimelineGridInsetX = 5.0f;

/** @brief Clamp to [a, b] (order-agnostic); non-finite inputs collapse to a safe bound. */
inline float safeClampFloat(float value, float a, float b) {
    if (!std::isfinite(value))
        return 0.0f;
    if (!std::isfinite(a) && !std::isfinite(b))
        return 0.0f;
    if (!std::isfinite(a))
        a = b;
    if (!std::isfinite(b))
        b = a;
    const float lo = std::min(a, b);
    const float hi = std::max(a, b);
    if (value <= lo)
        return lo;
    if (value >= hi)
        return hi;
    return value;
}

/** @brief Walk up the parent chain to the root component. */
inline AestraUI::NUIComponent* getRootComponent(AestraUI::NUIComponent* component) {
    AestraUI::NUIComponent* root = component;
    while (root && root->getParent()) {
        root = root->getParent();
    }
    return root;
}

/** @brief Remove a context menu from its current parent, if any. */
inline void detachContextMenu(const std::shared_ptr<AestraUI::NUIContextMenu>& menu) {
    if (!menu)
        return;
    if (auto* parent = menu->getParent()) {
        parent->removeChild(menu);
    }
}

/** @brief Attach a context menu to the owner's root component and show it. */
inline void attachAndShowContextMenu(AestraUI::NUIComponent* owner, const std::shared_ptr<AestraUI::NUIContextMenu>& menu,
                              const AestraUI::NUIPoint& position) {
    if (!owner || !menu)
        return;
    AestraUI::NUIComponent* root = getRootComponent(owner);
    if (!root)
        root = owner;
    root->addChild(menu);
    menu->showAt(position);
    root->repaint();
}

} // namespace Audio
} // namespace Aestra
