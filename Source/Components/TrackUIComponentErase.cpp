// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// The Erase tool (FD-50 board 7, owner choice 2026-10-19): a left click on a clip deletes it, exactly as the
// right-click fast delete does, and holding the button while moving keeps deleting every clip the pointer crosses
// on this lane. Each deletion is its own undo step, the same command right-click uses. Kept out of
// TrackUIComponent.cpp, which is at its size pin.
#include "TrackUIComponent.h"

#include "TrackManagerUI.h"
#include "../AestraUI/Core/NUIThemeSystem.h"

namespace Aestra {
namespace Audio {

bool TrackUIComponent::handleEraseTool(const AestraUI::NUIMouseEvent& event) {
    auto* manager = dynamic_cast<TrackManagerUI*>(getParent());
    const bool eraseActive = manager && manager->getCurrentTool() == PlaylistTool::Erase &&
                             m_playlistMode != PlaylistMode::Automation;
    if (!eraseActive) {
        m_eraseStroke = false;
        return false;
    }
    if (event.released && event.button == AestraUI::NUIMouseButton::Left) {
        const bool wasErasing = m_eraseStroke;
        m_eraseStroke = false;
        return wasErasing;
    }
    const bool press = event.pressed && event.button == AestraUI::NUIMouseButton::Left;
    if (!press && !m_eraseStroke) return false;
    if (press) {
        // The header keeps its own buttons: a press there is not an erase.
        const float controlsWidth =
            AestraUI::NUIThemeManager::getInstance().getLayoutDimensions().trackControlsWidth;
        if (!getBounds().contains(event.position) || event.position.x < getBounds().x + controlsWidth) return false;
        m_eraseStroke = true;
    }

    ClipInstanceID hit;
    for (const auto& [clipId, clipBounds] : m_allClipBounds) {
        if (clipBounds.contains(event.position)) {
            hit = clipId;
            break;
        }
    }
    if (hit.isValid()) {
        if (m_onClipSelectedCallback) m_onClipSelectedCallback(this, hit);
        manager->deleteSelectedClip();
        m_allClipBounds.erase(hit); // gone until the next paint republishes the rest
    }
    return true;
}

} // namespace Audio
} // namespace Aestra
