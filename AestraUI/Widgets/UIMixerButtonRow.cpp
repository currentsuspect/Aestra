// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerButtonRow.h"

#include "NUIThemeSystem.h"
#include "NUIRenderer.h"

#include <algorithm>

namespace AestraUI {

namespace {
    constexpr float BTN_W = 24.0f;
    constexpr float BTN_H = 20.0f;
    constexpr float BTN_GAP = 5.0f;
    constexpr float BTN_RADIUS = 8.0f;
}

UIMixerButtonRow::UIMixerButtonRow()
{
    cacheThemeColors();
    layoutButtons();
}

void UIMixerButtonRow::cacheThemeColors()
{
    auto& theme = NUIThemeManager::getInstance();
    m_bg = theme.getColor("buttonBgDefault").withAlpha(0.98f);
    m_border = theme.getColor("border").withAlpha(0.28f);
    m_hoverBorder = theme.getColor("border").withAlpha(0.38f);
    m_text = theme.getColor("textSecondary").withAlpha(0.86f);
    m_textOnBright = theme.getColor("textPrimary");
    m_textOnRed = theme.getColor("textPrimary");

    // Active glow colors (amber mute, yellow solo, red arm)
    m_muteOn = NUIColor(1.0f, 0.75f, 0.2f, 1.0f);   // amber
    m_soloOn = NUIColor(1.0f, 0.92f, 0.3f, 1.0f);   // yellow
    m_armOn  = NUIColor(1.0f, 0.25f, 0.25f, 1.0f);  // red
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
    auto& theme = NUIThemeManager::getInstance();

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

        NUIRect rect = m_buttonBounds[i];
        NUIRect visualRect{
            std::floor(rect.x) + 0.5f,
            std::floor(rect.y) + 0.5f,
            std::max(1.0f, std::floor(rect.width) - 1.0f),
            std::max(1.0f, std::floor(rect.height) - 1.0f)
        };
        
        NUIColor bg = m_bg;
        NUIColor border = m_border;

        if (active) {
            bg = activeBg.withAlpha(0.32f);
            border = activeBg.withAlpha(0.85f);
            textColor = (i == 2) ? m_textOnRed : m_textOnBright;
        } else if (hovered) {
            bg = theme.getColor("buttonBgHover").withAlpha(0.99f);
            border = m_hoverBorder;
            textColor = theme.getColor("textPrimary").withAlpha(0.92f);
        } else {
            textColor = m_text;
        }

        if (pressed) {
            bg = active ? activeBg.withAlpha(0.28f) : theme.getColor("buttonBgActive").withAlpha(0.99f);
        }

        // Active glow behind pill
        if (active) {
            renderer.drawGlow(visualRect, BTN_RADIUS, 0.6f, activeBg.withAlpha(0.45f));
        }

        renderer.drawShadow(visualRect, 0.0f, 4.0f, 12.0f, NUIColor(0, 0, 0, 0.12f));
        renderer.fillRoundedRect(visualRect, BTN_RADIUS, bg);
        renderer.strokeRoundedRect(visualRect, BTN_RADIUS, 1.0f, border);
        renderer.strokeRoundedRect({visualRect.x + 1.0f, visualRect.y + 1.0f, visualRect.width - 2.0f, visualRect.height - 2.0f},
                                   std::max(0.0f, BTN_RADIUS - 1.0f),
                                   1.0f,
                                   NUIColor::white().withAlpha(0.025f));
        renderer.drawTextCentered(labels[i], visualRect, 10.0f, textColor);
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
            
            // Tooltip Logic
            if (m_hovered != -1) {
                std::string text;
                if (m_hovered == 0) text = "Mute";
                else if (m_hovered == 1) text = "Solo";
                else if (m_hovered == 2) text = "Record Arm";
                
                const auto& rect = m_buttonBounds[m_hovered];
                NUIPoint center(rect.x + rect.width * 0.5f, rect.y + rect.height + 8.0f);
                NUIPoint globalPos = localToGlobal(center);
                
                NUIComponent::showRemoteTooltip(text, globalPos, this);
            } else {
                NUIComponent::hideRemoteTooltip(this);
            }
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

void UIMixerButtonRow::onMouseLeave()
{
    if (m_hovered != -1) {
        m_hovered = -1;
        requestInvalidate();
        NUIComponent::hideRemoteTooltip(this);
    }
    NUIComponent::onMouseLeave();
}

} // namespace AestraUI
