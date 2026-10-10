// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerButtonRow.h"

#include "NUIThemeSystem.h"
#include "NUIRenderer.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>

namespace AestraUI {

namespace {
    constexpr float BTN_W = 24.0f;
    constexpr float BTN_H = 20.0f;
    constexpr float BTN_GAP = 5.0f;
    constexpr float BTN_RADIUS = 3.0f;  // keys, matching the track-header M / S / R

    // Mute / solo / input-monitor render as lettered keys, the same language
    // as the arrangement track headers. FD-14 #6: the strip's third slot is
    // input monitoring; record arm lives on the Track only.
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

    m_muteOn = theme.getColor("muted");
    m_soloOn = theme.getColor("soloed");
    m_monitorOn = theme.getColor("armed");
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

void UIMixerButtonRow::setMonitored(bool monitored)
{
    if (m_monitored == monitored) return;
    m_monitored = monitored;
    requestInvalidate();
}

void UIMixerButtonRow::onResize(int width, int height)
{
    NUIComponent::onResize(width, height);
    layoutButtons();
}

void UIMixerButtonRow::onRender(NUIRenderer& renderer)
{
    // Lettered keys, the same language as the track-header M / S / R: a
    // state reads without hovering, and an active key is a solid fill in its
    // theme colour. The third key is input monitoring (it stopped being record
    // arm in v0.7.1), so it says IN rather than borrowing a record glyph.
    static constexpr const char* letters[kButtonCount] = {"M", "S", "IN"};
    auto& theme = NUIThemeManager::getInstance();
    const NUIColor inkOnActive = theme.getColor("backgroundPrimary");

    for (int i = 0; i < kButtonCount; ++i) {
        const bool hovered = (i == m_hovered);
        const bool pressed = (i == m_pressed);

        bool active = false;
        NUIColor activeBg = m_bg;
        if (i == 0) {
            active = m_muted;
            activeBg = m_muteOn;
        } else if (i == 1) {
            active = m_soloed;
            activeBg = m_soloOn;
        } else if (i == 2) {
            active = m_monitored;
            activeBg = m_monitorOn;
        }

        const NUIRect rect = m_buttonBounds[i];
        const NUIRect visualRect{
            std::floor(rect.x) + 0.5f,
            std::floor(rect.y) + 0.5f,
            std::max(1.0f, std::floor(rect.width) - 1.0f),
            std::max(1.0f, std::floor(rect.height) - 1.0f)
        };

        NUIColor textColor = m_text;
        if (active) {
            renderer.fillRoundedRect(visualRect, BTN_RADIUS, pressed ? activeBg.withAlpha(0.8f) : activeBg);
            textColor = inkOnActive;
        } else {
            if (hovered || pressed) {
                renderer.fillRoundedRect(visualRect, BTN_RADIUS,
                                         theme.getColor(pressed ? "buttonBgActive" : "buttonBgHover").withAlpha(0.99f));
                textColor = theme.getColor("textPrimary");
            }
            renderer.strokeRoundedRect(visualRect, BTN_RADIUS, 1.0f, hovered ? m_hoverBorder : m_border);
        }
        renderer.drawTextCentered(letters[i], rect, 9.5f, textColor);
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
                else if (m_hovered == 2) text = "Input Monitor";
                
                const auto& rect = m_buttonBounds[m_hovered];
                // m_buttonBounds are window-absolute (from getBounds()); so is the tooltip anchor.
                const NUIPoint anchor(rect.x + rect.width * 0.5f, rect.y + rect.height + 8.0f);
                NUIComponent::showRemoteTooltip(text, anchor, this);
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
                m_monitored = !m_monitored;
                requestInvalidate();
                if (onMonitorToggled) onMonitorToggled(m_monitored);
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
