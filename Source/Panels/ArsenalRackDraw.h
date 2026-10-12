// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ArsenalRackLayout.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"

#include <string>

namespace Aestra {

/// A group header in the Arsenal rack: "DRUMS  5 ————" (FD-27 point 2), drawn across the row width.
inline void drawArsenalGroupHeader(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& rect, const char* label, int count) {
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    const auto muted = theme.getColor("textSecondary");
    std::string text = label;
    for (auto& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const float y = rect.y + rect.height - 20.0f;
    renderer.drawText(text, {rect.x + 8.0f, y}, 10.0f, muted.withAlpha(0.8f));
    const float tw = renderer.measureText(text, 10.0f).width;
    const std::string n = std::to_string(count);
    renderer.drawText(n, {rect.x + 8.0f + tw + 10.0f, y}, 10.0f, muted.withAlpha(0.5f));
    const float lx = rect.x + 8.0f + tw + 10.0f + renderer.measureText(n, 10.0f).width + 10.0f;
    renderer.fillRect(AestraUI::NUIRect{lx, y + 6.0f, std::max(0.0f, rect.right() - 8.0f - lx), 1.0f}, theme.getColor("border").withAlpha(0.35f));
}

/// Every header of the rack that is in view, clipped to the list viewport.
inline void drawArsenalRackHeaders(AestraUI::NUIRenderer& renderer, const ArsenalRackLayout& rack,
                                   const AestraUI::NUIRect& viewport, float rackTop) {
    if (viewport.height <= 0.0f) return;
    renderer.setClipRect(viewport);
    for (const auto& item : rack.items) {
        if (item.kind == ArsenalRackItem::Kind::Header) {
            drawArsenalGroupHeader(renderer, AestraUI::NUIRect(viewport.x, rackTop + item.y, viewport.width, item.h),
                                   ArsenalRackLayout::groupLabel(item.group), item.count);
        }
    }
    renderer.clearClipRect();
}

} // namespace Aestra
