// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// The Mute tool (owner, 2026-10-20: "I could not mute individual patterns ... without muting the whole track"; it takes
// the place Erase had, since right-click and Delete already remove a clip). A left click on a clip mutes it, or unmutes
// it if it was muted; holding the button and moving sets every clip the pointer crosses on this lane to the state the
// first one went to, so a sweep never flips some back. Each change is its own undo step. Alt+click does the same once
// with any tool active. Kept out of TrackUIComponent.cpp, which is at its size pin.
#include "TrackUIComponent.h"

#include "TrackManagerUI.h"
#include "../AestraUI/Core/NUIThemeSystem.h"

#include <algorithm>

namespace Aestra {
namespace Audio {

bool TrackUIComponent::handleMuteTool(const AestraUI::NUIMouseEvent& event) {
    auto* manager = dynamic_cast<TrackManagerUI*>(getParent());
    const bool automation = m_playlistMode == PlaylistMode::Automation;
    const bool toolActive = manager && manager->getCurrentTool() == PlaylistTool::Mute && !automation;
    const bool leftPress = event.pressed && event.button == AestraUI::NUIMouseButton::Left;
    const bool altClick = manager && !automation && leftPress && (event.modifiers & AestraUI::NUIModifiers::Alt);
    if (!toolActive && !altClick) {
        m_muteStroke = false;
        return false;
    }
    if (event.released && event.button == AestraUI::NUIMouseButton::Left) {
        const bool wasMuting = m_muteStroke;
        m_muteStroke = false;
        m_muteStrokeDone.clear();
        return wasMuting;
    }
    if (!leftPress && !m_muteStroke) return false;
    if (leftPress) {
        // The header keeps its own buttons: a press there is not a mute.
        const float controlsWidth = AestraUI::NUIThemeManager::getInstance().getLayoutDimensions().trackControlsWidth;
        if (!getBounds().contains(event.position) || event.position.x < getBounds().x + controlsWidth) return false;
        m_muteStroke = toolActive; // Alt+click is one click, not a stroke
        m_muteStrokeDone.clear();
    }

    ClipInstanceID hit;
    for (const auto& [clipId, clipBounds] : m_allClipBounds) {
        if (clipBounds.contains(event.position)) {
            hit = clipId;
            break;
        }
    }
    if (!hit.isValid()) return true; // a press on empty grid with the tool active is still the tool's
    if (std::find(m_muteStrokeDone.begin(), m_muteStrokeDone.end(), hit) != m_muteStrokeDone.end()) return true;

    if (m_muteStrokeDone.empty()) m_muteStrokeTarget = !manager->isClipMuted(hit); // the first clip decides the sweep
    manager->toggleClipMute(hit, m_muteStrokeTarget);
    m_muteStrokeDone.push_back(hit);
    return true;
}

} // namespace Audio
} // namespace Aestra
