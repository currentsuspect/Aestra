// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIOutputChip.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"

#include <algorithm>
#include <cmath>

namespace AestraUI {

namespace {
constexpr float kRadius = 4.0f;
constexpr float kDot = 6.0f;
constexpr float kFont = 9.5f;
} // namespace

void UIOutputChip::setLabel(std::string label) {
    if (label == m_label)
        return;
    m_label = std::move(label);
    repaint();
}

void UIOutputChip::setColor(const NUIColor& color) {
    m_color = color;
    repaint();
}

void UIOutputChip::setDimmed(bool dimmed) {
    if (dimmed == m_dimmed)
        return;
    m_dimmed = dimmed;
    repaint();
}

void UIOutputChip::setCompact(bool compact) {
    if (compact == m_compact) return;
    m_compact = compact;
    repaint();
}

void UIOutputChip::onRender(NUIRenderer& renderer) {
    const auto b = getBounds();
    if (b.isEmpty())
        return;
    auto& theme = NUIThemeManager::getInstance();
    const float alpha = m_dimmed ? 0.45f : 1.0f;
    if (m_compact) {
        renderer.fillRoundedRect(NUIRect{b.x, b.y, b.width, b.height}, 2.0f, m_color.withAlpha(0.85f * alpha));
        return;
    }
    const NUIRect r{std::floor(b.x) + 0.5f, std::floor(b.y) + 0.5f, std::max(1.0f, std::floor(b.width) - 1.0f),
                    std::max(1.0f, std::floor(b.height) - 1.0f)};

    renderer.fillRoundedRect(r, kRadius, m_color.withAlpha((m_hovered ? 0.24f : 0.14f) * alpha));
    renderer.strokeRoundedRect(r, kRadius, 1.0f, m_color.withAlpha((m_hovered ? 0.70f : 0.42f) * alpha));

    const NUIRect dot{r.x + 5.0f, r.y + (r.height - kDot) * 0.5f, kDot, kDot};
    renderer.fillRoundedRect(dot, kDot * 0.5f, m_color.withAlpha(alpha));

    const float textX = dot.right() + 4.0f;
    const NUIRect textRect{textX, r.y, std::max(1.0f, r.right() - textX - 4.0f), r.height};
    // The arrow says "goes to"; ellipsis keeps a long bus name inside the pill.
    std::string text = "\xE2\x86\x92 " + m_label;
    const float avail = textRect.width;
    while (text.size() > 4 && renderer.measureText(text, kFont).width > avail)
        text.pop_back();
    if (text.size() < m_label.size() + 4)
        text += "\xE2\x80\xA6";
    renderer.drawText(text, {textRect.x, textRect.y + (textRect.height - kFont) * 0.5f - 1.0f}, kFont,
                      theme.getColor("textPrimary").withAlpha(0.9f * alpha));
}

bool UIOutputChip::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible() || !isEnabled())
        return false;
    const bool inside = getBounds().contains(event.position);
    if (event.button == NUIMouseButton::None) {
        if (inside != m_hovered) {
            m_hovered = inside;
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
