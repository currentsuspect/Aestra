// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerButtonRow.h"

#include "../Core/NUIThemeSystem.h"
#include "../Graphics/NUIRenderer.h"

#include <algorithm>

namespace NomadUI {

namespace {
    constexpr float BTN_W = 22.0f;
    constexpr float BTN_H = 18.0f;
    constexpr float BTN_GAP = 4.0f;
    constexpr float BTN_RADIUS = 4.0f;
}

UIMixerButtonRow::UIMixerButtonRow()
{
    cacheThemeColors();
    layoutButtons();
}

void UIMixerButtonRow::cacheThemeColors()
{
    auto& theme = NUIThemeManager::getInstance();
    m_bg = theme.getColor("surfaceTertiary");
    m_border = theme.getColor("borderSubtle").withAlpha(0.55f);
    m_hoverBorder = theme.getColor("border").withAlpha(0.8f);
    m_text = theme.getColor("textPrimary");

    // High-contrast text on bright accent fills (mute/solo).
    m_textOnBright = NUIColor(0.05f, 0.05f, 0.06f, 1.0f);
    m_textOnRed = NUIColor::white();

    m_muteOn = theme.getColor("accentAmber");
    m_soloOn = theme.getColor("accentCyan");
    m_armOn = theme.getColor("error");
}

void UIMixerButtonRow::layoutButtons()
{
    const auto b = getBounds();
    const float totalW = BTN_W * kButtonCount + BTN_GAP * (kButtonCount - 1);
    const float startX = std::round(b.x + (b.width - totalW) * 0.5f);
    const float y = std::round(b.y + (b.height - BTN_H) * 0.5f);

    for (int i = 0; i < kButtonCount; ++i) {
        const float x = startX + i * (BTN_W + BTN_GAP);
        m_buttonBounds[i] = NUIRect{x, y, BTN_W, BTN_H};
    }
}

int UIMixerButtonRow::hitTest(const NUIPoint& p) const
{
    for (int i = 0; i < kButtonCount; ++i) {
        if (m_buttonBounds[i].contains(p)) return i;
    }
    return -1;
}

void UIMixerButtonRow::requestInvalidate()
{
    repaint();
    if (onInvalidateRequested) {
        onInvalidateRequested();
    }
}

void UIMixerButtonRow::setMuted(bool muted)
{
    if (m_muted == muted) return;
    m_muted = muted;
    requestInvalidate();
}

void UIMixerButtonRow::setSoloed(bool soloed)
{
    if (m_soloed == soloed) return;
    m_soloed = soloed;
    requestInvalidate();
}

void UIMixerButtonRow::setArmed(bool armed)
{
    if (m_armed == armed) return;
    m_armed = armed;
    requestInvalidate();
}

void UIMixerButtonRow::onResize(int width, int height)
{
    NUIComponent::onResize(width, height);
    layoutButtons();
}

void UIMixerButtonRow::onRender(NUIRenderer& renderer)
{
    static constexpr const char* labels[kButtonCount] = {"M", "S", "R"};

    for (int i = 0; i < kButtonCount; ++i) {
        const bool hovered = (i == m_hovered);
        const bool pressed = (i == m_pressed);

        bool active = false;
        NUIColor activeBg = m_bg;
        NUIColor textColor = m_text;

        if (i == 0) {
            active = m_muted;
            activeBg = m_muteOn;
            if (active) textColor = m_textOnBright;
        } else if (i == 1) {
            active = m_soloed;
            activeBg = m_soloOn;
            if (active) textColor = m_textOnBright;
        } else if (i == 2) {
            active = m_armed;
            activeBg = m_armOn;
            if (active) textColor = m_textOnRed;
        }

        NUIColor bg = active ? activeBg : m_bg;
        if (pressed) {
            bg = bg.withAlpha(std::min(1.0f, bg.a + 0.12f));
        }

        const auto& rect = m_buttonBounds[i];
        renderer.fillRoundedRect(rect, BTN_RADIUS, bg);

        // Border (subtle)
        const NUIColor border = hovered ? m_hoverBorder : m_border;
        if (border.a > 0.0f) {
            renderer.strokeRoundedRect(rect, BTN_RADIUS, 1.0f, border);
        }

        renderer.drawTextCentered(labels[i], rect, 10.0f, textColor);
    }
}

bool UIMixerButtonRow::onMouseEvent(const NUIMouseEvent& event)
{
    if (!isVisible() || !isEnabled()) return false;

    const int hit = hitTest(event.position);

    if (event.button == NUIMouseButton::None) {
        if (hit != m_hovered) {
            m_hovered = hit;
            requestInvalidate();
        }
    }

    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (hit >= 0) {
            m_pressed = hit;
            requestInvalidate();
            return true;
        }
    }

    if (event.released && event.button == NUIMouseButton::Left) {
        const int wasPressed = m_pressed;
        if (m_pressed != -1) {
            m_pressed = -1;
            requestInvalidate();
        }

        if (wasPressed >= 0 && wasPressed == hit) {
            if (wasPressed == 0) {
                m_muted = !m_muted;
                requestInvalidate();
                if (onMuteToggled) onMuteToggled(m_muted);
            } else if (wasPressed == 1) {
                m_soloed = !m_soloed;
                requestInvalidate();
                if (onSoloToggled) onSoloToggled(m_soloed, event.modifiers);
            } else if (wasPressed == 2) {
                m_armed = !m_armed;
                requestInvalidate();
                if (onArmToggled) onArmToggled(m_armed);
            }
            return true;
        }
    }

    return false;
}

} // namespace NomadUI
