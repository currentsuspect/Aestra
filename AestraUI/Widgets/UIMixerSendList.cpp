// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerSendList.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace AestraUI {

namespace {
constexpr float kFont = 9.5f;
} // namespace

float UIMixerSendList::heightForRows(int rowCount) {
    return rowCount <= 0 ? 0.0f : std::min(rowCount, kMaxRows) * kRowH + 2.0f;
}

int UIMixerSendList::visibleRowCapacity() const {
    return std::max(0, static_cast<int>(std::floor((getBounds().height - 2.0f) / kRowH)));
}

std::string UIMixerSendList::formatLevel(float db) {
    if (!std::isfinite(db) || db <= -90.0f)
        return "\xE2\x88\x92\xE2\x88\x9E"; // minus infinity
    char buf[16];
    if (std::fabs(db) < 0.05f)
        return "0";
    if (std::fabs(db) >= 9.95f)
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(std::fabs(db))));
    else
        std::snprintf(buf, sizeof(buf), "%.1f", std::fabs(db));
    return std::string(db < 0.0f ? "\xE2\x88\x92" : "+") + buf;
}

void UIMixerSendList::setRows(std::vector<Row> rows) {
    const bool same = rows.size() == m_rows.size() &&
                      std::equal(rows.begin(), rows.end(), m_rows.begin(), [](const Row& a, const Row& b) {
                          return a.name == b.name && std::fabs(a.levelDb - b.levelDb) < 0.05f &&
                                 a.sidechain == b.sidechain && a.muted == b.muted;
                      });
    if (same)
        return;
    m_rows = std::move(rows);
    repaint();
}

void UIMixerSendList::onRender(NUIRenderer& renderer) {
    const auto b = getBounds();
    if (b.isEmpty() || m_rows.empty())
        return;
    auto& theme = NUIThemeManager::getInstance();
    const NUIColor text = theme.getColor("textPrimary");
    const NUIColor muted = theme.getColor("textSecondary");
    const NUIColor amber = theme.getColor("warning");

    const int capacity = std::max(1, visibleRowCapacity());
    const bool overflow = static_cast<int>(m_rows.size()) > capacity;
    const int shown = overflow ? std::max(1, capacity - 1) : static_cast<int>(m_rows.size());
    float y = b.y + 1.0f;
    for (int i = 0; i < shown; ++i) {
        const Row& row = m_rows[static_cast<size_t>(i)];
        const float alpha = row.muted ? 0.4f : 1.0f;
        const float cy = y + kRowH * 0.5f;
        if (row.sidechain) {
            // An amber diamond: outline only, rotated square.
            const float s = 4.0f;
            renderer.drawLine({b.x + 8.0f, cy - s}, {b.x + 8.0f + s, cy}, 1.4f, amber.withAlpha(alpha));
            renderer.drawLine({b.x + 8.0f + s, cy}, {b.x + 8.0f, cy + s}, 1.4f, amber.withAlpha(alpha));
            renderer.drawLine({b.x + 8.0f, cy + s}, {b.x + 8.0f - s, cy}, 1.4f, amber.withAlpha(alpha));
            renderer.drawLine({b.x + 8.0f - s, cy}, {b.x + 8.0f, cy - s}, 1.4f, amber.withAlpha(alpha));
        } else {
            renderer.fillRect(NUIRect{b.x + 5.0f, cy - 3.0f, 6.0f, 6.0f}, row.color.withAlpha(alpha));
        }
        const std::string level = row.sidechain ? std::string("SC") : formatLevel(row.levelDb);
        const float levelW = renderer.measureText(level, kFont).width;
        const float textX = b.x + 16.0f;
        const float avail = std::max(1.0f, b.right() - 6.0f - levelW - 4.0f - textX);
        std::string name = row.name;
        bool cut = false;
        while (name.size() > 1 && renderer.measureText(name, kFont).width > avail) {
            name.pop_back();
            cut = true;
        }
        if (cut)
            name += "\xE2\x80\xA6";
        const float ty = y + (kRowH - kFont) * 0.5f - 1.0f;
        renderer.drawText(name, {textX, ty}, kFont, text.withAlpha(0.85f * alpha));
        renderer.drawText(level, {b.right() - 6.0f - levelW, ty}, kFont, muted.withAlpha(0.85f * alpha));
        y += kRowH;
    }
    if (overflow) {
        renderer.drawText("+" + std::to_string(static_cast<int>(m_rows.size()) - shown) + " more",
                          {b.x + 16.0f, y + (kRowH - kFont) * 0.5f - 1.0f}, kFont, muted.withAlpha(0.7f));
    }
}

bool UIMixerSendList::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible() || !isEnabled() || m_rows.empty())
        return false;
    const bool inside = getBounds().contains(event.position);
    if (event.pressed && event.button == NUIMouseButton::Left && inside) {
        m_pressed = true;
        return true;
    }
    if (event.released && event.button == NUIMouseButton::Left) {
        const bool fire = m_pressed && inside;
        m_pressed = false;
        if (fire && onClicked)
            onClicked();
        return fire;
    }
    return false;
}

} // namespace AestraUI
