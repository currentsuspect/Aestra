// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// TrackUIComponent's side of automation clips (V8-A7): the lane's view of AutomationClipLayer,
// kept out of TrackUIComponent.cpp, which is at its size pin.
#include "TrackUIComponent.h"

#include "AutomationLaneEditing.h"
#include "TrackManagerUI.h"
#include "../AestraUI/Platform/NUIPlatformBridge.h"
#include "Models/TrackManager.h"

namespace Aestra {
namespace Audio {

void TrackUIComponent::renderAutomationClips(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& bounds,
                                             float gridStartX, int editedIndex) {
    const auto* lane = m_trackManager ? m_trackManager->getPlaylistModel().getLane(m_laneId) : nullptr;
    if (!lane)
        return;
    m_clipLayer.render(renderer, *lane, editedIndex, {bounds, gridStartX, m_pixelsPerBeat, m_timelineScrollOffset});
}

bool TrackUIComponent::handleAutomationClipMouse(const AestraUI::NUIMouseEvent& event,
                                                 const AestraUI::NUIRect& bounds, float gridStartX) {
    AutomationClipLayer::Hooks hooks;
    hooks.snapBeat = [this](double beat) { return snapBeatToGrid(std::max(0.0, beat)); };
    hooks.captureMouse = [this](bool on) {
        if (auto* parent = dynamic_cast<TrackManagerUI*>(getParent()))
            if (auto* window = parent->getPlatformWindow())
                window->setMouseCapture(on);
    };
    hooks.changed = [this]() {
        setDirty(true);
        repaint();
        if (m_onCacheInvalidationCallback)
            m_onCacheInvalidationCallback();
    };
    int edited = -1;
    const bool handled = m_clipLayer.onMouse(event, *m_trackManager, m_laneId,
                                             {bounds, gridStartX, m_pixelsPerBeat, m_timelineScrollOffset}, hooks, edited);
    if (edited >= 0) {
        if (const auto* lane = m_trackManager->getPlaylistModel().getLane(m_laneId))
            if (edited < static_cast<int>(lane->automationCurves.size()))
                setEditedAutomationTarget(lane->automationCurves[static_cast<size_t>(edited)].getAutomationTarget());
    }
    return handled;
}

} // namespace Audio
} // namespace Aestra
