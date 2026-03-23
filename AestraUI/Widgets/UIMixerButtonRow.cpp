// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerButtonRow.h"

#include "NUIThemeSystem.h"
#include "NUIRenderer.h"

#include <algorithm>

namespace AestraUI {

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

        NUIRect rect = m_buttonBounds[i];
        
        // --- Neon / Glass Rendering ---
        
        if (active) {
            // Active: "Neon Glass"
            // 1. Bloom/Glow (behind)
            renderer.fillRoundedRect(
                NUIRect(rect.x - 2, rect.y - 2, rect.width + 4, rect.height + 4), 
                BTN_RADIUS + 2.0f, 
                activeBg.withAlpha(0.25f)
            );
            
            // 2. Glass Body
            renderer.fillRoundedRect(rect, BTN_RADIUS, activeBg.withAlpha(0.2f));
            
            // 3. Neon Border
            renderer.strokeRoundedRect(rect, BTN_RADIUS, 1.0f, activeBg.withAlpha(0.9f));
            
            // 4. Text (Bright/Glow)
            renderer.drawTextCentered(labels[i], rect, 10.0f, textColor);
        } 
        else {
            // Inactive: "Subtle Glass"
            NUIColor bg = m_bg.withAlpha(0.05f); // Very faint background
            NUIColor border = m_border;
            
            if (hovered) {
                bg = m_text.withAlpha(0.1f);
                border = m_hoverBorder;
                textColor = m_text; // Brighten text
            } else {
                textColor = m_text.withAlpha(0.6f); // Dim text
            }
            
            if (pressed) {
                 bg = bg.withAlpha(0.2f);
            }

            renderer.fillRoundedRect(rect, BTN_RADIUS, bg);
            
            if (border.a > 0.0f) {
                renderer.strokeRoundedRect(rect, BTN_RADIUS, 1.0f, border);
            }
            
            renderer.drawTextCentered(labels[i], rect, 10.0f, textColor);
        }
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
                
                NUIComponent::showRemoteTooltip(text, globalPos);
            } else {
                NUIComponent::hideRemoteTooltip();
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
        NUIComponent::hideRemoteTooltip();
    }
    NUIComponent::onMouseLeave();
}

} // namespace AestraUI
