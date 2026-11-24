// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIOutputChip.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"

#include <algorithm>
#include <cmath>

namespace AestraUI {

namespace {
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
    // Design Spec, Mixer (board p-mixer2): the output is one line of muted text under the name, "to Master", with
    // no pill. Hover brightens it; it is still the click target for the route picker.
    constexpr float kLine = 10.0f;
    std::string text = "to " + m_label;
    while (text.size() > 4 && renderer.measureText(text, kLine).width > b.width) {
        text.pop_back();
        while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) text.pop_back();
        if (renderer.measureText(text + "\xE2\x80\xA6", kLine).width <= b.width) { text += "\xE2\x80\xA6"; break; }
    }
    renderer.drawTextCentered(text, b, kLine,
                              (m_hovered ? theme.getColor("textPrimary") : theme.getColor("textMuted")).withAlpha(alpha));
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
