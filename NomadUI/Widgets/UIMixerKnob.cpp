// ¶¸ 2025 Nomad Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerKnob.h"

#include "../Core/NUIThemeSystem.h"
#include "../Graphics/NUIRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace NomadUI {

namespace {
    constexpr float LABEL_H = 0.0f; // Labels are shown via tooltip to reduce strip noise.
    constexpr float TOOLTIP_H = 18.0f;
    constexpr float TOOLTIP_PAD_X = 6.0f;
    constexpr float TOOLTIP_RADIUS = 5.0f;

    constexpr float ARC_START = -3.0f * 3.14159265f / 4.0f; // -135°
    constexpr float ARC_END = 3.0f * 3.14159265f / 4.0f;    // +135°
}

UIMixerKnob::UIMixerKnob(UIMixerKnobType type)
    : m_type(type)
{
    cacheThemeColors();
    updateCachedText();
}

void UIMixerKnob::cacheThemeColors()
{
    auto& theme = NUIThemeManager::getInstance();
    m_bg = theme.getColor("surfaceTertiary");
    m_bgHover = theme.getColor("surfaceSecondary");
    m_ring = theme.getColor("borderSubtle").withAlpha(0.65f);
    m_ringHover = theme.getColor("border").withAlpha(0.85f);
    m_indicator = theme.getColor("accentPrimary");
    m_text = theme.getColor("textPrimary");
    m_textSecondary = theme.getColor("textSecondary");
    m_tooltipBg = theme.getColor("backgroundSecondary").withAlpha(0.95f);
    m_tooltipText = theme.getColor("textPrimary");
}

float UIMixerKnob::minValue() const
{
    return (m_type == UIMixerKnobType::Trim) ? -24.0f : -1.0f;
}

float UIMixerKnob::maxValue() const
{
    return (m_type == UIMixerKnobType::Trim) ? 24.0f : 1.0f;
}

float UIMixerKnob::defaultValue() const
{
    return 0.0f;
}

float UIMixerKnob::clampValue(float value) const
{
    return std::clamp(value, minValue(), maxValue());
}

void UIMixerKnob::updateCachedText()
{
    if (std::abs(m_cachedValue - m_value) < 1e-4f) {
        return;
    }
    m_cachedValue = m_value;

    if (m_type == UIMixerKnobType::Trim) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "Trim %+.1f dB", m_value);
        m_cachedText = buf;
        return;
    }

    // Pan (-1..1) -> -100..+100
    const int pct = static_cast<int>(std::round(m_value * 100.0f));
    if (pct == 0) {
        m_cachedText = "Pan C";
        return;
    }
    char buf[16];
    if (pct < 0) {
        std::snprintf(buf, sizeof(buf), "Pan L%d", -pct);
    } else {
        std::snprintf(buf, sizeof(buf), "Pan R%d", pct);
    }
    m_cachedText = buf;
}

const char* UIMixerKnob::label() const
{
    return (m_type == UIMixerKnobType::Trim) ? "TRIM" : "PAN";
}

void UIMixerKnob::setValue(float value)
{
    const float clamped = clampValue(value);
    if (std::abs(clamped - m_value) < 1e-5f) {
        return;
    }

    m_value = clamped;
    updateCachedText();
    repaint();

    if (onValueChanged) {
        onValueChanged(m_value);
    }
}

void UIMixerKnob::renderTooltip(NUIRenderer& renderer, const NUIRect& knobBounds) const
{
    const float fontSize = 10.0f;
    const auto textSize = renderer.measureText(m_cachedText, fontSize);
    const float w = std::max(28.0f, textSize.width + TOOLTIP_PAD_X * 2.0f);

    const float x = std::round(knobBounds.x + (knobBounds.width - w) * 0.5f);
    const float y = std::round(knobBounds.y - TOOLTIP_H - 6.0f);
    const NUIRect tipRect{x, y, w, TOOLTIP_H};
    renderer.fillRoundedRect(tipRect, TOOLTIP_RADIUS, m_tooltipBg);
    renderer.drawTextCentered(m_cachedText, tipRect, fontSize, m_tooltipText);
}

void UIMixerKnob::onRender(NUIRenderer& renderer)
{
    const auto b = getBounds();
    if (b.isEmpty()) return;

    const float knobAreaH = std::max(1.0f, b.height - LABEL_H);
    const float cx = b.x + b.width * 0.5f;
    const float cy = b.y + knobAreaH * 0.5f;
    float r = std::min(b.width, knobAreaH) * 0.38f;
    r = std::clamp(r, 8.0f, 14.0f);

    const bool hovered = isHovered() || m_dragging;
    // Drop Shadow (Subtle)
    renderer.fillCircle({cx, cy + 2.0f}, r, NUIColor(0.0f, 0.0f, 0.0f, 0.4f));

    renderer.fillCircle({cx, cy}, r, hovered ? m_bgHover : m_bg);
    renderer.strokeCircle({cx, cy}, r, 1.0f, hovered ? m_ringHover : m_ring);

    // Indicator
    const float t = (m_value - minValue()) / std::max(1e-5f, (maxValue() - minValue()));
    const float angle = ARC_START + std::clamp(t, 0.0f, 1.0f) * (ARC_END - ARC_START);
    const float ix = cx + std::cos(angle) * (r * 0.72f);
    const float iy = cy + std::sin(angle) * (r * 0.72f);
    renderer.drawLine({cx, cy}, {ix, iy}, 2.0f, m_indicator);
    renderer.fillCircle({ix, iy}, 2.0f, m_indicator);

    // Tooltip (while dragging)
    if (m_dragging) {
        renderTooltip(renderer, b);
    }
}

bool UIMixerKnob::onMouseEvent(const NUIMouseEvent& event)
{
    if (!isVisible() || !isEnabled()) return false;

    const auto b = getBounds();
    setHovered(b.contains(event.position));
    if (!b.contains(event.position) && !m_dragging) return false;

    // Double-click reset
    if (event.doubleClick && event.pressed && event.button == NUIMouseButton::Left) {
        setValue(defaultValue());
        return true;
    }

    if (event.pressed && event.button == NUIMouseButton::Left) {
        m_dragging = true;
        m_dragStartPos = event.position;
        m_dragStartValue = m_value;
        repaint();
        return true;
    }

    if (event.released && event.button == NUIMouseButton::Left && m_dragging) {
        m_dragging = false;
        repaint();
        return true;
    }

    // Dragging (mouse move events set button = None)
    if (m_dragging && event.button == NUIMouseButton::None) {
        const float dy = (m_dragStartPos.y - event.position.y); // up increases

        float sensitivity = 1.0f;
        if (event.modifiers & NUIModifiers::Shift) {
            sensitivity *= 0.1f;
        }

        float delta = 0.0f;
        if (m_type == UIMixerKnobType::Trim) {
            delta = dy * 0.15f; // dB per px
        } else {
            delta = dy * 0.006f; // pan per px (~166px full range)
        }

        setValue(m_dragStartValue + delta * sensitivity);
        return true;
    }

    return false;
}

} // namespace NomadUI
