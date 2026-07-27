// © 2025 Aestra Studios All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Internal helpers shared by the TrackManagerUI* translation units (split
// out of the former monolithic TrackManagerUI.cpp). Not part of the public
// API — include only from TrackManagerUI*.cpp files, after TrackManagerUI.h.

#include "../AestraUI/Base/NUIContextMenu.h"
#include "TrackManagerUIMath.h" // kTimeline* constants + safeClampFloat (no widget deps)

#include <algorithm>
#include <cmath>
#include <memory>

// Remotery profiling stubs (disabled)
#define rmt_ScopedCPUSample(name, flags) ((void)0)
#define rmt_BeginCPUSample(name, flags) ((void)0)
#define rmt_EndCPUSample() ((void)0)

namespace Aestra {
namespace Audio {

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
