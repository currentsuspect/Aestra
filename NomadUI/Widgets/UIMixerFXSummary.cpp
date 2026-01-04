// ¶¸ 2025 Nomad Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerFXSummary.h"

#include "../Core/NUIThemeSystem.h"
#include "../Graphics/NUIRenderer.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace NomadUI {

namespace {
    constexpr float RADIUS = 5.0f;
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
    m_bg = theme.getColor("surfaceTertiary");
    m_border = theme.getColor("borderSubtle").withAlpha(0.55f);
    m_borderHover = theme.getColor("border").withAlpha(0.85f);
    m_textPrimary = theme.getColor("textPrimary");
    m_textSecondary = theme.getColor("textSecondary");
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

    NUIColor bg = m_bg;
    if (m_pressed) {
        bg = bg.withAlpha(std::min(1.0f, bg.a + 0.12f));
    }
    renderer.fillRoundedRect(b, RADIUS, bg);
    const bool hasFx = (m_fxCount > 0);
    const NUIColor border = hasFx ? m_accent.withAlpha(m_hovered ? 0.85f : 0.55f) : (m_hovered ? m_borderHover : m_border);
    renderer.strokeRoundedRect(b, RADIUS, 1.0f, border);

    const NUIColor text = hasFx ? m_textPrimary : m_textSecondary;
    renderer.drawTextCentered(m_labelText.empty() ? std::string("FX") : m_labelText, b, 10.0f, text);
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

} // namespace NomadUI
