// ¶¸ 2025 Aestra Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerKnob.h"

#include "NUIThemeSystem.h"
#include "NUIRenderer.h"
#include "../Platform/NUIPlatformBridge.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace AestraUI {

namespace {
    constexpr float LABEL_H = 11.0f; // Micro-label below knob cap

    constexpr float ARC_START = 135.0f * 3.14159265f / 180.0f; // 7 o'clock
    constexpr float ARC_END = 405.0f * 3.14159265f / 180.0f;   // 5 o'clock (wrapping)
    constexpr float DRAG_SLOP = 2.5f;
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
    m_bgHover = theme.getColor("controlHover");
    m_ring = theme.getColor("borderSubtle").withAlpha(0.65f);
    m_ringHover = theme.getColor("border").withAlpha(0.85f);
    m_indicator = theme.getColor("accentPrimary");
    // Resting arcs are neutral. When every knob carried a saturated accent the
    // whole strip read as "everything is active"; colour now marks engagement.
    m_arcResting = theme.getColor("textPrimary").withAlpha(0.42f);
    m_text = theme.getColor("textPrimary");
    m_textSecondary = theme.getColor("textSecondary");
    m_tooltipBg = theme.getColor("backgroundSecondary").withAlpha(0.95f);
    m_tooltipText = theme.getColor("textPrimary");
}


float UIMixerKnob::minValue() const
{
    if (m_type == UIMixerKnobType::Send) return 0.0f;
    if (m_type == UIMixerKnobType::Width) return 0.0f; // Mono
    return (m_type == UIMixerKnobType::Trim) ? -24.0f : -1.0f;
}

float UIMixerKnob::maxValue() const
{
    if (m_type == UIMixerKnobType::Send) return 1.0f;
    if (m_type == UIMixerKnobType::Width) return 4.0f; // 400%
    return (m_type == UIMixerKnobType::Trim) ? 24.0f : 1.0f;
}

float UIMixerKnob::defaultValue() const
{
    if (m_type == UIMixerKnobType::Width) return 1.0f; // Normal Stereo
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

    if (m_type == UIMixerKnobType::Send) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "Send %.2f", m_value);
        m_cachedText = buf;
        return;
    }

    if (m_type == UIMixerKnobType::Width) {
        int pct = static_cast<int>(std::round(m_value * 100.0f));
        if (pct == 0) m_cachedText = "Mono";
        else if (pct == 100) m_cachedText = "Width 100%";
        else if (pct > 100) {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "Wide %d%%", pct);
            m_cachedText = buf;
        } else {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "Width %d%%", pct);
            m_cachedText = buf;
        }
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
    if (m_type == UIMixerKnobType::Send) return "SEND";
    if (m_type == UIMixerKnobType::Width) return "WID";
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

void UIMixerKnob::updateGlobalTooltip() const
{
    const auto b = getBounds();
    const NUIPoint anchor{b.x + b.width * 0.5f, b.y + b.height + 8.0f};
    NUIComponent::showRemoteTooltip(m_cachedText, anchor, this);
}

// Helper to draw an arc using polyline
void drawRoughArc(NUIRenderer& renderer, NUIPoint center, float radius, float startAngle, float endAngle, float thickness, NUIColor color) {
    const int segments = 32;
    // ensure endAngle > startAngle
    if (endAngle < startAngle) std::swap(startAngle, endAngle);
    
    // Clamp range
    if (endAngle - startAngle <= 0.001f) return;

    std::vector<NUIPoint> points;
    points.reserve(segments + 1);
    
    float angleStep = (endAngle - startAngle) / static_cast<float>(segments);
    
    for (int i = 0; i <= segments; ++i) {
        float theta = startAngle + i * angleStep;
        points.push_back({
            center.x + std::cos(theta) * radius,
            center.y + std::sin(theta) * radius
        });
    }
    
    renderer.drawPolyline(points.data(), static_cast<int>(points.size()), thickness, color);
}

void UIMixerKnob::onRender(NUIRenderer& renderer)
{
    const auto b = getBounds();
    if (b.isEmpty()) return;

    const float knobAreaH = std::max(1.0f, b.height - LABEL_H);
    const float cx = b.x + b.width * 0.5f;
    const float cy = b.y + knobAreaH * 0.5f;
    
    // Size tuning: make it slightly punchier
    float r = std::min(b.width, knobAreaH) * 0.42f;
    r = std::clamp(r, 7.0f, 16.0f);

    const bool hovered = isHovered() || m_dragging;
    
    // 1. Background Track Arc (Dark Grey)
    // Full scale from 7 o'clock to 5 o'clock
    NUIColor trackColor = NUIThemeManager::getInstance().getColor("background").withAlpha(0.5f);
    drawRoughArc(renderer, {cx, cy}, r, ARC_START, ARC_END, 3.0f, trackColor);

    // 2. Active Value Arc (Accent Color)
    // Determine span
    float startAng = ARC_START;
    float currentAng = ARC_START;
    const float t = (m_value - minValue()) / std::max(1e-5f, (maxValue() - minValue()));
    
    if (m_type == UIMixerKnobType::Pan) {
         // Pan centers at 12 o'clock (approx -PI/2 or 270 deg)
         // Map 0.0 (Left) -> 1.0 (Right)
         // Center is 0.5
         float midAng = (ARC_START + ARC_END) * 0.5f;
         startAng = midAng;
         currentAng = ARC_START + t * (ARC_END - ARC_START);
    } else {
         // Standard: Start to Current
         currentAng = ARC_START + t * (ARC_END - ARC_START);
    }
    
    // `hovered` already folds in m_dragging above.
    NUIColor activeColor = hovered ? m_indicator.withAlpha(1.0f) : m_arcResting;
    
    drawRoughArc(renderer, {cx, cy}, r, std::min(startAng, currentAng), std::max(startAng, currentAng), 3.0f, activeColor);

    // 3. Knob Cap (Inner Circle)
    // Slightly smaller than the ring
    float capR = r * 0.75f;

    // Flat modern look: Dark grey cap
    NUIColor capColor = m_bg;
    if (hovered) capColor = m_bgHover;

    // Flat cap: no drop shadow. The edge highlight below defines it.
    renderer.fillCircle({cx, cy}, capR, capColor);

    // Edge highlight for 3D feel
    renderer.strokeCircle({cx, cy}, capR, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.1f));

    // 4. Pointer Line on Cap
    // Points exactly to current value
    const float ptrLen = capR * 0.8f;
    const float ptrAng = ARC_START + t * (ARC_END - ARC_START);
    NUIPoint ptrStart = {cx, cy};
    NUIPoint ptrEnd = {
        cx + std::cos(ptrAng) * ptrLen,
        cy + std::sin(ptrAng) * ptrLen
    };

    // White pointer for contrast
    renderer.drawLine(ptrStart, ptrEnd, 2.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.9f));

    // 5. Micro-label below knob
    if (LABEL_H > 0.0f) {
        NUIRect labelRect{b.x, b.y + knobAreaH, b.width, LABEL_H};
        renderer.drawTextCentered(label(), labelRect, 9.5f, m_textSecondary.withAlpha(0.7f));
    }

    if (m_dragging) {
        updateGlobalTooltip();
    }

}

void UIMixerKnob::setPlatformBridge(NUIPlatformBridge* bridge)
{
    m_platformBridge = bridge;
}

UIMixerKnob::~UIMixerKnob() {
    // Torn down mid-drag: cancel the capture so the bridge never routes to a
    // dangling owner and the cursor is never stranded hidden.
    if (m_platformBridge && m_platformBridge->isCursorCaptureOwner(this)) {
        m_platformBridge->cancelCursorCapture();
    }
}

bool UIMixerKnob::onMouseEvent(const NUIMouseEvent& event)
{
    if (!isVisible() || !isEnabled()) return false;

    const auto b = getBounds();
    if (!event.cursorCaptured) {
        setHovered(b.contains(event.position));
    }
    if (!b.contains(event.position) && !m_dragging) return false;

    // Double-click reset. The platform never populates event.doubleClick, so
    // quick same-button presses are paired up here (house pattern) — relying on
    // the flag alone left this reset unreachable.
    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (m_clickTracker.registerPress() || event.doubleClick) {
            setValue(defaultValue());
            updateGlobalTooltip();
            return true;
        }
    }

    if (event.pressed && event.button == NUIMouseButton::Left) {
        m_dragging = true;
        m_dragStartPos = event.position;
        m_dragStartValue = m_value;
        m_dragAxis = DragAxis::Undecided;

        // Cursor capture for infinite drag: service hides + confines and will
        // restore at the knob center on release.
        if (m_platformBridge) {
            m_lastDragY = event.position.y;
            m_platformBridge->beginCursorCapture(
                this, NUICursorRestorePolicy::KnobCenter,
                static_cast<int>(event.position.x), static_cast<int>(event.position.y));
        }

        updateGlobalTooltip();
        repaint();
        return true;
    }

    if (event.released && event.button == NUIMouseButton::Left && m_dragging) {
        m_dragging = false;
        m_dragAxis = DragAxis::Undecided;

        if (m_platformBridge) {
            // End capture: service warps to the VISUAL knob-cap center (the
            // cap is centered in height - LABEL_H, shifted up to leave room for
            // the micro-label), unhides, releases confinement — in that order.
            auto bounds = getBounds();
            const float knobAreaH = std::max(1.0f, bounds.height - LABEL_H);
            m_platformBridge->endCursorCapture(
                static_cast<int>(bounds.x + bounds.width * 0.5f),
                static_cast<int>(bounds.y + knobAreaH * 0.5f));
        }

        NUIComponent::hideRemoteTooltip(this);
        repaint();
        return true;
    }

    // Dragging (mouse move events set button = None)
    if (m_dragging && event.button == NUIMouseButton::None) {
        // Cursor-warp mode: use frame-to-frame delta
        if (m_platformBridge) {
            // Service-owned delta (recentered; no absolute-coord read).
            float dy = event.delta.y;

            // Reduced sensitivity for cursor-warp mode since every pixel counts
            float sensitivity = (event.modifiers & NUIModifiers::Shift) ? 0.11f : 0.5f;
            float delta = -dy * sensitivity; // Invert Y: up = positive

            // Apply type-specific scaling
            if (m_type == UIMixerKnobType::Trim) {
                delta *= 0.11f;
            } else if (m_type == UIMixerKnobType::Send) {
                delta *= 0.004f;
            } else if (m_type == UIMixerKnobType::Width) {
                delta *= 0.008f;
            } else {
                delta *= 0.0048f;
            }

            setValue(m_value + delta);
            updateGlobalTooltip();
            return true;
        }

        // Non-warp mode: use drag start position with axis detection
        float dx = event.position.x - m_dragStartPos.x;
        float dy = m_dragStartPos.y - event.position.y;
        const float absDx = std::abs(dx);
        const float absDy = std::abs(dy);

        if (m_dragAxis == DragAxis::Undecided && std::max(absDx, absDy) > DRAG_SLOP) {
            m_dragAxis = (absDy >= absDx) ? DragAxis::Vertical : DragAxis::Horizontal;
        }

        float primaryDelta = 0.0f;
        if (m_dragAxis == DragAxis::Horizontal) {
            primaryDelta = dx * 0.75f + dy * 0.25f;
        } else {
            primaryDelta = dy + dx * 0.15f;
        }

        float sensitivity = 1.0f;
        if (event.modifiers & NUIModifiers::Shift) {
            sensitivity *= 0.22f;
        }

        float delta = 0.0f;
        if (m_type == UIMixerKnobType::Trim) {
            delta = primaryDelta * 0.11f;
        } else if (m_type == UIMixerKnobType::Send) {
            delta = primaryDelta * 0.004f;
        } else if (m_type == UIMixerKnobType::Width) {
            delta = primaryDelta * 0.008f;
        } else {
            delta = primaryDelta * 0.0048f;
        }

        setValue(m_dragStartValue + delta * sensitivity);
        updateGlobalTooltip();
        return true;
    }

    if (!b.contains(event.position) && !m_dragging) {
        NUIComponent::hideRemoteTooltip(this);
    }

    return false;
}

} // namespace AestraUI
