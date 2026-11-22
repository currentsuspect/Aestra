// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerInsertList.h"
#include "SlotText.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"

#include <algorithm>
#include <cmath>

namespace AestraUI {

namespace {
constexpr float kFont = 10.0f;
constexpr float kRadius = 5.0f;
constexpr float kDotSize = 6.0f;
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

    // Design Spec, Mixer (board p-mixer2, locked): an insert is a flat 20px chip, surfaceTertiary, radius 5, with a 6px
    // primary dot and the name in 10px. An empty chain is one quiet "+ insert" row in textMuted, no fill.
    if (m_rows.empty()) {
        const NUIRect row{r.x, r.y, r.width, kChipH};
        if (m_hovered)
            renderer.fillRoundedRect(row, kRadius, theme.getColor("hover"));
        const char* invite = renderer.measureText("+ insert", kFont).width <= row.width - 10.0f ? "+ insert" : "+";
        // Centred in its 20px row by the renderer's own metrics, on the baseline a filled chip's name uses.
        const float w = renderer.measureText(invite, kFont).width;
        renderer.drawText(invite, {std::round(row.x + (row.width - w) * 0.5f), SlotText::slotTextY(renderer, row, kFont)},
                          kFont, m_hovered ? textPrimary : theme.getColor("textMuted"));
        return;
    }

    const int capacity = visibleRowCapacity();
    const bool overflow = static_cast<int>(m_rows.size()) > capacity;
    const int shown = overflow ? std::max(1, capacity - 1) : static_cast<int>(m_rows.size());

    float y = r.y + kPad;
    for (int i = 0; i < shown; ++i) {
        const Row& row = m_rows[static_cast<size_t>(i)];
        const float alpha = row.bypassed ? 0.38f : 1.0f;
        const NUIRect chip{r.x, y, r.width, kChipH};
        renderer.fillRoundedRect(chip, kRadius, theme.getColor(m_hovered ? "surfaceRaised" : "surfaceTertiary"));
        // A dot per insert: lit when it is processing, dim when bypassed.
        const NUIRect dot{chip.x + 5.0f, chip.y + (kChipH - kDotSize) * 0.5f, kDotSize, kDotSize};
        renderer.fillRoundedRect(dot, kDotSize * 0.5f, row.bypassed ? textSecondary.withAlpha(0.35f) : theme.getColor("primary"));

        const float textX = dot.right() + 4.0f;
        float rightInset = 5.0f;
        std::string status;
        if (i == 0 && !m_status.empty()) {
            status = m_status;
            rightInset += renderer.measureText(status, 8.5f).width + 4.0f;
        }
        const float avail = std::max(1.0f, chip.right() - textX - rightInset);
        std::string name = row.name;
        bool cut = false;
        while (name.size() > 1 && renderer.measureText(name, kFont).width > avail) {
            name.pop_back();
            cut = true;
        }
        if (cut)
            name += "\xE2\x80\xA6";
        const float textY = SlotText::slotTextY(renderer, chip, kFont);
        renderer.drawText(name, {textX, textY}, kFont, textPrimary.withAlpha(alpha));
        if (row.bypassed) {
            const float w = std::min(avail, renderer.measureText(name, kFont).width);
            renderer.fillRect(NUIRect{textX, y + kChipH * 0.5f, w, 1.0f}, textSecondary.withAlpha(0.45f));
        }
        if (!status.empty()) {
            renderer.drawText(status, {chip.right() - rightInset + 4.0f, SlotText::slotTextY(renderer, chip, 8.5f)}, 8.5f,
                              accent.withAlpha(0.92f));
        }
        y += kRowH;
    }
    if (overflow) {
        const int hidden = static_cast<int>(m_rows.size()) - shown;
        renderer.drawText("+" + std::to_string(hidden) + " more",
                          {r.x + 5.0f, SlotText::slotTextY(renderer, NUIRect{r.x, y, r.width, kChipH}, kFont)}, kFont,
                          theme.getColor("textMuted"));
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
