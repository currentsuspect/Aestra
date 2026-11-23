// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIParamBar.h"
#include "../Graphics/NUIDepth.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"

#include <algorithm>
#include <cmath>

namespace AestraUI {

namespace {
constexpr float kLabelW = 46.0f;
constexpr float kValueW = 58.0f;
constexpr float kFont = 10.0f;
} // namespace

float UIParamBar::clampValue(float v) const {
    return std::clamp(std::isfinite(v) ? v : m_default, m_min, m_max);
}

void UIParamBar::setRange(float minValue, float maxValue, float defaultValue) {
    m_min = minValue;
    m_max = std::max(minValue + 1e-6f, maxValue);
    m_default = std::clamp(defaultValue, m_min, m_max);
    m_value = clampValue(m_value);
    repaint();
}

void UIParamBar::setBipolar(bool bipolar) {
    m_bipolar = bipolar;
    repaint();
}

void UIParamBar::setLabel(std::string label) {
    m_label = std::move(label);
    repaint();
}

void UIParamBar::setFormatter(std::function<std::string(float)> formatter) {
    m_formatter = std::move(formatter);
    repaint();
}

void UIParamBar::setValue(float value) {
    if (m_dragging)
        return;
    const float v = clampValue(value);
    if (std::fabs(v - m_value) < 1e-6f)
        return;
    m_value = v;
    repaint();
}

NUIRect UIParamBar::barRect() const {
    const auto b = getBounds();
    if (m_look == Look::Bar)
        return NUIRect{b.x, b.y, b.width, b.height};
    const float x = b.x + (m_label.empty() ? 0.0f : kLabelW);
    return NUIRect{x, b.y + b.height * 0.5f - 3.0f, std::max(8.0f, b.right() - kValueW - x), 6.0f};
}

void UIParamBar::onRender(NUIRenderer& renderer) {
    const auto b = getBounds();
    if (b.isEmpty())
        return;
    auto& theme = NUIThemeManager::getInstance();
    const NUIColor text = theme.getColor("textPrimary");
    const NUIColor muted = theme.getColor("textSecondary");
    const NUIColor fill = (m_hovered || m_dragging) ? text : text.withAlpha(0.82f);

    NUIRect bar = barRect();
    if (m_look == Look::Bar) {
        // The pan row (board p-mixer2): a 4px sunk track centred in the row, the primary travel 2px thick from the
        // centre, and a 10px round thumb. (drawn below)
        bar = NUIRect{b.x + 5.0f, b.y + b.height * 0.5f - 2.0f, std::max(1.0f, b.width - 10.0f), 4.0f};
    } else {
        if (!m_label.empty())
            renderer.drawText(m_label, {b.x, b.y + (b.height - kFont) * 0.5f - 1.0f}, kFont, muted.withAlpha(0.9f));
        const std::string valueText = m_formatter ? m_formatter(m_value) : std::to_string(m_value);
        const float w = renderer.measureText(valueText, kFont).width;
        renderer.drawText(valueText, {b.right() - w, b.y + (b.height - kFont) * 0.5f - 1.0f}, kFont, text.withAlpha(0.95f));
    }

    // Design Spec, Slider (locked): a sunk slot with the travel in primary (from the centre when bipolar). A row
    // carries the fader cap on its side; the compact bar under a strip keeps a 2 px marker, where a cap would crowd it.
    const float t = std::clamp((m_value - m_min) / (m_max - m_min), 0.0f, 1.0f);
    if (m_look == Look::Bar) {
        const auto& th = theme.getCurrentTheme();
        renderer.fillRoundedRect(bar, 2.0f, th.meterBackground);
        renderer.drawInnerShadow(bar, 2.0f, 1.0f, 2.0f, th.neuLo);
        const float origin = m_bipolar ? 0.5f : 0.0f;
        const float x = bar.x + t * bar.width;
        const float x0 = std::min(x, bar.x + origin * bar.width);
        const float x1 = std::max(x, bar.x + origin * bar.width);
        if (x1 - x0 >= 1.0f)
            renderer.fillRect({x0, bar.y + 1.0f, x1 - x0, 2.0f}, th.primary);
        // The thumb: a 10px disc of surfaceRaised, lit on top and shaded underneath, with a short shadow below.
        const NUIRect thumb{x - 5.0f, b.y + b.height * 0.5f - 5.0f, 10.0f, 10.0f};
        renderer.drawShadow(thumb, 0.0f, 2.0f, 3.0f, th.neuLo);
        renderer.fillRoundedRectVertical(thumb, 5.0f, NUIColor::lerp(th.surfaceRaised, NUIColor::white(), 0.16f),
                                         NUIColor::lerp(th.surfaceRaised, NUIColor::black(), 0.22f));
        (void)fill;
    } else {
        const NUIRect slot{bar.x + 11.0f, bar.y, std::max(1.0f, bar.width - 22.0f), bar.height};
        Depth::sliderSlot(renderer, slot, t, m_bipolar ? 0.5f : 0.0f);
        Depth::sliderCap(renderer, {slot.x + t * slot.width, slot.y + slot.height * 0.5f}, m_dragging);
    }
}

void UIParamBar::onMouseLeave() {
    if (m_hovered) {
        m_hovered = false;
        repaint();
    }
    NUIComponent::onMouseLeave();
}

void UIParamBar::applyUserValue(float v) {
    const float next = clampValue(v);
    if (std::fabs(next - m_value) < 1e-6f)
        return;
    m_value = next;
    repaint();
    if (onValueChanged)
        onValueChanged(m_value);
}

bool UIParamBar::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible() || !isEnabled())
        return false;
    const auto b = getBounds();
    const bool inside = b.contains(event.position);
    if (!m_dragging && event.button == NUIMouseButton::None) {
        if (inside != m_hovered) {
            m_hovered = inside;
            repaint();
        }
        return false;
    }
    if (!inside && !m_dragging)
        return false;

    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (m_clickTracker.registerPress() || event.doubleClick) {
            applyUserValue(m_default);
            if (onValueCommitted)
                onValueCommitted(m_value);
            return true;
        }
        m_dragging = true;
        m_dragStartX = event.position.x;
        m_dragStartValue = m_value;
        repaint();
        return true;
    }
    if (m_dragging && event.released && event.button == NUIMouseButton::Left) {
        m_dragging = false;
        repaint();
        if (onValueCommitted)
            onValueCommitted(m_value);
        return true;
    }
    if (m_dragging && event.button == NUIMouseButton::None) {
        const float width = std::max(8.0f, barRect().width);
        const float fine = (event.modifiers & NUIModifiers::Shift) ? 0.1f : 1.0f;
        applyUserValue(m_dragStartValue + (event.position.x - m_dragStartX) / width * (m_max - m_min) * fine);
        return true;
    }
    return false;
}

} // namespace AestraUI
