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

/// The FIT | SCROLL segmented control in the command header; the active mode is lightly filled.
inline void drawArsenalFitToggle(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& toggle, const AestraUI::NUIRect& fitRect,
                                 const AestraUI::NUIRect& scrollRect, bool fitToWidth) {
    using namespace AestraUI;
    if (toggle.width <= 0.0f) return;
    auto& theme = NUIThemeManager::getInstance();
    const auto& tp = theme.getCurrentTheme();
    renderer.fillRoundedRect(toggle, tp.radiusS, theme.getColor("surfaceTertiary").withAlpha(0.85f));
    renderer.strokeRoundedRect(toggle, tp.radiusS, 1.0f, theme.getColor("borderSubtle").withAlpha(0.5f));
    const NUIColor activeFill = theme.getColor("accentPrimary").withAlpha(0.16f);
    const NUIColor activeText = theme.getColor("accentPrimary").withAlpha(0.95f);
    const NUIColor idleText = theme.getColor("textSecondary").withAlpha(0.85f);
    if (fitRect.width > 0.0f) {
        if (fitToWidth) renderer.fillRoundedRect(fitRect, tp.radiusXS, activeFill);
        renderer.drawTextCentered("FIT", fitRect, tp.fontSizeMicro, fitToWidth ? activeText : idleText);
    }
    if (scrollRect.width > 0.0f) {
        if (!fitToWidth) renderer.fillRoundedRect(scrollRect, tp.radiusXS, activeFill);
        renderer.drawTextCentered("SCROLL", scrollRect, tp.fontSizeMicro, fitToWidth ? idleText : activeText);
    }
    const float dividerX = fitRect.width > 0.0f ? fitRect.right() : toggle.x + toggle.width * 0.5f;
    renderer.drawLine(NUIPoint(dividerX, toggle.y + 5.0f), NUIPoint(dividerX, toggle.bottom() - 5.0f), 1.0f,
                      theme.getColor("borderSubtle").withAlpha(0.6f));
}

/// A length control as plain text (FD-27: the header reads like a sentence, not a row of boxes): the value in
/// the middle and a quiet minus and plus either side, dimmed when that way is at its limit.
inline void drawArsenalStepper(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& dec, const AestraUI::NUIRect& value,
                               const AestraUI::NUIRect& inc, const std::string& text, bool decEnabled, bool incEnabled) {
    using namespace AestraUI;
    auto& theme = NUIThemeManager::getInstance();
    const auto& tp = theme.getCurrentTheme();
    const NUIColor glyph = theme.getColor("textSecondary").withAlpha(0.85f);
    const NUIColor off = theme.getColor("textDisabled");
    renderer.drawTextCentered("\xE2\x88\x92", dec, tp.fontSizeS, decEnabled ? glyph : off);
    renderer.drawTextCentered(text, value, tp.fontSizeXS, theme.getColor("textPrimary").withAlpha(0.92f));
    renderer.drawTextCentered("+", inc, tp.fontSizeS, incEnabled ? glyph : off);
}

/// The "+ ADD UNIT" type picker: a 2x2 card of unit kinds with a one-line description each.
inline void drawArsenalUnitTypePicker(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& card) {
    using namespace AestraUI;
    using Audio::UnitType;
    auto& theme = NUIThemeManager::getInstance();
    const auto& tp = theme.getCurrentTheme();
    const float radius = std::max(tp.radiusL - 2.0f, 0.0f);
    renderer.fillRoundedRect(card, radius, theme.getColor("backgroundSecondary").withAlpha(0.98f));
    renderer.strokeRoundedRect(card, radius, 1.0f, theme.getColor("borderSubtle").withAlpha(0.6f));
    const float padding = 10.0f, gap = 8.0f;
    const float cellW = (card.width - padding * 2.0f - gap) * 0.5f, cellH = (card.height - padding * 2.0f - gap) * 0.5f;
    struct Kind { UnitType type; const char* icon; const char* name; const char* text; };
    static const Kind kinds[4] = {{UnitType::Sampler, "▦", "Sampler", "Classic step sequencer"},
                                  {UnitType::PitchedSampler, "≋", "808", "808s, bass, slides"},
                                  {UnitType::Instrument, "♫", "MIDI", "Notes, chords, instruments"},
                                  {UnitType::Audio, "◫", "Audio", "Drop a clip directly"}};
    for (int i = 0; i < 4; ++i) {
        const NUIRect cell(card.x + padding + (i % 2) * (cellW + gap), card.y + padding + (i / 2) * (cellH + gap), cellW, cellH);
        renderer.fillRoundedRect(cell, tp.radiusM, theme.getColor("surfaceTertiary").withAlpha(0.8f));
        renderer.strokeRoundedRect(cell, tp.radiusM, 1.0f, theme.getColor("borderSubtle").withAlpha(0.5f));
        renderer.drawText(kinds[i].icon, NUIPoint(cell.x + 10.0f, cell.y + 11.0f), tp.fontSizeS - 1.0f, theme.getColor("accentPrimary").withAlpha(0.9f));
        renderer.drawText(kinds[i].name, NUIPoint(cell.x + 32.0f, cell.y + 10.0f), tp.fontSizeXS - 1.0f, theme.getColor("textPrimary").withAlpha(0.95f));
        renderer.drawText(kinds[i].text, NUIPoint(cell.x + 10.0f, cell.y + 28.0f), tp.fontSizeMicro, theme.getColor("textSecondary").withAlpha(0.68f));
    }
}

/// The dashed "a new unit will land here" ghost shown while a sample is dragged over empty space.
inline void drawArsenalDropGhost(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& ghost, const std::string& label) {
    using namespace AestraUI;
    auto& theme = NUIThemeManager::getInstance();
    const auto outline = theme.getColor("accentPrimary").withAlpha(0.85f);
    constexpr float kDash = 7.0f, kGap = 5.0f;
    for (float x = ghost.x; x < ghost.right(); x += kDash + kGap) {
        const float x1 = std::min(x + kDash, ghost.right());
        renderer.drawLine(NUIPoint(x, ghost.y), NUIPoint(x1, ghost.y), 1.6f, outline);
        renderer.drawLine(NUIPoint(x, ghost.bottom()), NUIPoint(x1, ghost.bottom()), 1.6f, outline);
    }
    for (float y = ghost.y; y < ghost.bottom(); y += kDash + kGap) {
        const float y1 = std::min(y + kDash, ghost.bottom());
        renderer.drawLine(NUIPoint(ghost.x, y), NUIPoint(ghost.x, y1), 1.6f, outline);
        renderer.drawLine(NUIPoint(ghost.right(), y), NUIPoint(ghost.right(), y1), 1.6f, outline);
    }
    const auto textSize = renderer.measureText(label, 10.5f);
    renderer.drawText(label, NUIPoint(ghost.x + (ghost.width - textSize.width) * 0.5f, ghost.y + (ghost.height - textSize.height) * 0.5f), 10.5f,
                      theme.getColor("textPrimary").withAlpha(0.9f));
}

} // namespace Aestra
