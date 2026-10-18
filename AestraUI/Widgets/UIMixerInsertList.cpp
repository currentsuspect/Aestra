// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerInsertList.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"

#include <algorithm>
#include <cmath>

namespace AestraUI {

namespace {
constexpr float kFont = 9.5f;
constexpr float kRadius = 5.0f;
} // namespace

float UIMixerInsertList::heightForRows(int rowCount) {
    const int rows = std::clamp(rowCount, 1, kMaxRows);
    return rows * kRowH + kPad * 2.0f;
}

int UIMixerInsertList::visibleRowCapacity() const {
    return std::max(1, static_cast<int>(std::floor((getBounds().height - kPad * 2.0f) / kRowH)));
}

void UIMixerInsertList::setRows(std::vector<Row> rows) {
    const bool same = rows.size() == m_rows.size() &&
                      std::equal(rows.begin(), rows.end(), m_rows.begin(), [](const Row& a, const Row& b) {
                          return a.name == b.name && a.bypassed == b.bypassed;
                      });
    if (same)
        return;
    m_rows = std::move(rows);
    if (m_rows.empty())
        NUIComponent::hideRemoteTooltip(this);
    repaint();
    if (onInvalidateRequested)
        onInvalidateRequested();
}

void UIMixerInsertList::setStatusText(std::string text) {
    if (text == m_status)
        return;
    m_status = std::move(text);
    repaint();
    if (onInvalidateRequested)
        onInvalidateRequested();
}

void UIMixerInsertList::onRender(NUIRenderer& renderer) {
    const auto b = getBounds();
    if (b.isEmpty())
        return;
    auto& theme = NUIThemeManager::getInstance();
    const NUIRect r{std::floor(b.x) + 0.5f, std::floor(b.y) + 0.5f, std::max(1.0f, std::floor(b.width) - 1.0f),
                    std::max(1.0f, std::floor(b.height) - 1.0f)};
    const NUIColor textPrimary = theme.getColor("textPrimary");
    const NUIColor textSecondary = theme.getColor("textSecondary");
    const NUIColor accent = theme.getColor("accentPrimary");

    renderer.fillRoundedRect(r, kRadius, theme.getColor("buttonBgDefault").withAlpha(m_hovered ? 0.98f : 0.7f));
    renderer.strokeRoundedRect(r, kRadius, 1.0f, theme.getColor("border").withAlpha(m_hovered ? 0.5f : 0.26f));

    if (m_rows.empty()) {
        renderer.drawTextCentered("+ insert", r, kFont, textSecondary.withAlpha(m_hovered ? 0.95f : 0.62f));
        return;
    }

    const int capacity = visibleRowCapacity();
    const bool overflow = static_cast<int>(m_rows.size()) > capacity;
    const int shown = overflow ? std::max(1, capacity - 1) : static_cast<int>(m_rows.size());

    float y = r.y + kPad;
    for (int i = 0; i < shown; ++i) {
        const Row& row = m_rows[static_cast<size_t>(i)];
        const float alpha = row.bypassed ? 0.38f : 1.0f;
        // A dot per insert: lit when it is processing, hollow-dim when bypassed.
        const NUIRect dot{r.x + 6.0f, y + (kRowH - 5.0f) * 0.5f, 5.0f, 5.0f};
        renderer.fillRoundedRect(dot, 2.5f, row.bypassed ? textSecondary.withAlpha(0.35f) : accent.withAlpha(0.9f));

        const float textX = dot.right() + 5.0f;
        float rightInset = 5.0f;
        std::string status;
        if (i == 0 && !m_status.empty()) {
            status = m_status;
            rightInset += renderer.measureText(status, 8.5f).width + 4.0f;
        }
        const float avail = std::max(1.0f, r.right() - textX - rightInset);
        std::string name = row.name;
        bool cut = false;
        while (name.size() > 1 && renderer.measureText(name, kFont).width > avail) {
            name.pop_back();
            cut = true;
        }
        if (cut)
            name += "\xE2\x80\xA6";
        const float textY = y + (kRowH - kFont) * 0.5f - 1.0f;
        renderer.drawText(name, {textX, textY}, kFont, textPrimary.withAlpha(0.92f * alpha));
        if (row.bypassed) {
            const float w = std::min(avail, renderer.measureText(name, kFont).width);
            renderer.fillRect(NUIRect{textX, y + kRowH * 0.5f, w, 1.0f}, textSecondary.withAlpha(0.45f));
        }
        if (!status.empty()) {
            renderer.drawText(status, {r.right() - rightInset + 4.0f, y + (kRowH - 8.5f) * 0.5f - 1.0f}, 8.5f,
                              accent.withAlpha(0.92f));
        }
        y += kRowH;
    }
    if (overflow) {
        const int hidden = static_cast<int>(m_rows.size()) - shown;
        renderer.drawText("+" + std::to_string(hidden) + " more",
                          {r.x + 6.0f, y + (kRowH - kFont) * 0.5f - 1.0f}, kFont, textSecondary.withAlpha(0.7f));
    }
}

bool UIMixerInsertList::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible() || !isEnabled())
        return false;
    const auto b = getBounds();
    const bool inside = b.contains(event.position);
    if (event.button == NUIMouseButton::None) {
        if (inside != m_hovered) {
            m_hovered = inside;
            if (m_hovered && m_rows.empty())
                NUIComponent::showRemoteTooltip("Add insert", {b.center().x, b.bottom() + 8.0f}, this);
            else if (!m_hovered)
                NUIComponent::hideRemoteTooltip(this);
            repaint();
        }
        return false;
    }
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
