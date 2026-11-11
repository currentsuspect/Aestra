// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V82-T2 (canvas-2 board 7): a new project has forty numbered tracks, almost all untouched. They fold into one row
// under the last track, "31 empty tracks · show", and the header counts them. That row is where a drop makes a new
// track (TrackManagerUIDragDrop), so dragging onto it still works. Only tracks nobody has touched fold: no clips, no
// automation, the default "Track N" name, one lane. A renamed or used track always shows.
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "TimelineFold.h"
#include "TrackManager.h"
#include "TrackManagerUI.h"

#include <string>

namespace Aestra {
namespace Audio {

bool TrackManagerUI::isFoldableEmptyLane(const PlaylistLane& lane, size_t laneCountOfItsTrack) const {
    return isUntouchedTrackLane(lane, laneCountOfItsTrack); // TimelineFold.h
}

void TrackManagerUI::renderFoldedEmptyRow(AestraUI::NUIRenderer& renderer) {
    m_foldedToggleRect = {};
    if (m_foldedEmptyTracks == 0 && !m_showEmptyTracks)
        return;
    if (m_foldedRowRect.isEmpty())
        return;
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    const float controlsW = theme.getLayoutDimensions().trackControlsWidth;
    const AestraUI::NUIRect header(m_foldedRowRect.x, m_foldedRowRect.y, controlsW, m_foldedRowRect.height);
    renderer.drawLine({m_foldedRowRect.x, m_foldedRowRect.bottom() - 0.5f},
                      {m_foldedRowRect.right(), m_foldedRowRect.bottom() - 0.5f}, 1.0f,
                      theme.getColor("border").withAlpha(0.5f));

    const float font = 12.5f;
    const float x = header.x + 34.0f;
    const float y = renderer.calculateTextY(header, font);
    std::string lead;
    std::string action;
    if (m_showEmptyTracks) {
        action = "hide empty tracks";
    } else {
        lead = std::to_string(m_foldedEmptyTracks) + (m_foldedEmptyTracks == 1 ? " empty track" : " empty tracks") +
               " \xc2\xb7 ";
        action = "show";
    }
    renderer.drawText(lead, {x, y}, font, theme.getColor("textMuted"));
    const float actionX = x + renderer.measureText(lead, font).width;
    const float actionW = renderer.measureText(action, font).width;
    renderer.drawText(action, {actionX, y}, font, theme.getColor("textSecondary"));
    m_foldedToggleRect = {actionX - 4.0f, header.y, actionW + 8.0f, header.height};
}

bool TrackManagerUI::handleFoldedRowMouse(const AestraUI::NUIMouseEvent& event) {
    if (!event.pressed || event.button != AestraUI::NUIMouseButton::Left)
        return false;
    if (m_foldedToggleRect.isEmpty() || !m_foldedToggleRect.contains(event.position))
        return false;
    m_showEmptyTracks = !m_showEmptyTracks;
    refreshTracks();
    layoutTracks();
    invalidateCache();
    setDirty(true);
    return true;
}

} // namespace Audio
} // namespace Aestra
