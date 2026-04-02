// ¶¸ 2025 Aestra Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerFXSummary.h"

#include "NUIThemeSystem.h"
#include "NUIRenderer.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace AestraUI {

namespace {
    constexpr float RADIUS = 7.0f;
}

UIMixerFXSummary::UIMixerFXSummary()
{
    cacheThemeColors();
    // Prime label text.
    m_fxCount = -1;
    setFxCount(0);
}

void UIMixerFXSummary::cacheThemeColors()
{
    auto& theme = NUIThemeManager::getInstance();
    m_bg = theme.getColor("buttonBgDefault").withAlpha(0.98f);
    m_border = theme.getColor("border").withAlpha(0.28f);
    m_borderHover = theme.getColor("border").withAlpha(0.38f);
    m_textPrimary = theme.getColor("textPrimary");
    m_textSecondary = theme.getColor("textSecondary").withAlpha(0.86f);
    m_accent = theme.getColor("accentPrimary");
}

void UIMixerFXSummary::requestInvalidate()
{
    repaint();
    if (onInvalidateRequested) onInvalidateRequested();
}

void UIMixerFXSummary::setFxCount(int count)
{
    const int clamped = std::max(0, count);
    if (clamped == m_fxCount) return;
    m_fxCount = clamped;
    if (m_fxCount <= 0) {
        m_labelText = "+ Add FX";
    } else {
        m_labelText = std::to_string(m_fxCount) + " FX";
    }
    requestInvalidate();
}

void UIMixerFXSummary::onRender(NUIRenderer& renderer)
{
    const auto b = getBounds();
    if (b.isEmpty()) return;
    const NUIRect visualRect{
        std::floor(b.x) + 0.5f,
        std::floor(b.y) + 0.5f,
        std::max(1.0f, std::floor(b.width) - 1.0f),
        std::max(1.0f, std::floor(b.height) - 1.0f)
    };

    NUIColor bg = m_bg;
    if (m_pressed) {
        bg = NUIThemeManager::getInstance().getColor("buttonBgActive").withAlpha(0.99f);
    }
    renderer.drawShadow(visualRect, 0.0f, 4.0f, 12.0f, NUIColor(0, 0, 0, 0.12f));
    renderer.fillRoundedRect(visualRect, RADIUS, bg);
    const bool hasFx = (m_fxCount > 0);
    const NUIColor border = hasFx
        ? m_accent.withAlpha(m_hovered ? 0.28f : 0.20f)
        : (m_hovered ? m_borderHover : m_border);
    renderer.strokeRoundedRect(visualRect, RADIUS, 1.0f, border);
    renderer.strokeRoundedRect({visualRect.x + 1.0f, visualRect.y + 1.0f, visualRect.width - 2.0f, visualRect.height - 2.0f},
                               std::max(0.0f, RADIUS - 1.0f),
                               1.0f,
                               NUIColor::white().withAlpha(0.025f));

    const NUIColor text = hasFx ? m_textPrimary : (m_hovered ? m_textPrimary.withAlpha(0.92f) : m_textSecondary);
    renderer.drawTextCentered(m_labelText.empty() ? std::string("FX") : m_labelText, visualRect, 10.0f, text);
}

bool UIMixerFXSummary::onMouseEvent(const NUIMouseEvent& event)
{
    if (!isVisible() || !isEnabled()) return false;

    const auto b = getBounds();
    if (!b.contains(event.position) && !m_pressed && event.button != NUIMouseButton::None) return false;

    if (event.button == NUIMouseButton::None) {
        const bool hoveredNow = b.contains(event.position);
        if (hoveredNow != m_hovered) {
            m_hovered = hoveredNow;
            requestInvalidate();
        }
        return false;
    }

    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (b.contains(event.position)) {
            m_pressed = true;
            requestInvalidate();
            return true;
        }
    }

    if (event.released && event.button == NUIMouseButton::Left) {
        const bool wasPressed = m_pressed;
        if (m_pressed) {
            m_pressed = false;
            requestInvalidate();
        }
        if (wasPressed && b.contains(event.position)) {
            if (onClicked) onClicked();
            return true;
        }
    }

    return false;
}

} // namespace AestraUI
