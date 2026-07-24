// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerButtonRow.h"

#include "NUIThemeSystem.h"
#include "NUIRenderer.h"
#include "../Graphics/NUISVGParser.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>

namespace AestraUI {

namespace {
    constexpr float BTN_W = 24.0f;
    constexpr float BTN_H = 20.0f;
    constexpr float BTN_GAP = 5.0f;
    constexpr float BTN_RADIUS = 8.0f;

    // Mute/solo/record glyphs — authored on a 24x24 grid, identical to the
    // track-header control icons (Source/Components/TrackUIComponent.cpp) so the
    // mixer and the arrangement lanes speak one icon language.
    constexpr const char* kMuteIconSvg =
        R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path d="M4 9v6h4l5 4.5v-15L8 9H4z" fill="currentColor"/><path d="M16 9.5 21 14.5 M21 9.5 16 14.5" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" fill="none"/></svg>)";
    constexpr const char* kSoloIconSvg =
        R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path d="M12 4.5a7.5 7.5 0 0 0-7.5 7.5v5.2a1.8 1.8 0 0 0 1.8 1.8h1.2a1 1 0 0 0 1-1v-4.2a1 1 0 0 0-1-1H6.5V12a5.5 5.5 0 0 1 11 0v.8h-1a1 1 0 0 0-1 1V18a1 1 0 0 0 1 1h1.2a1.8 1.8 0 0 0 1.8-1.8V12A7.5 7.5 0 0 0 12 4.5z" fill="currentColor"/></svg>)";
    constexpr const char* kRecordIconSvg =
        R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><circle cx="12" cy="12" r="7.2" fill="none" stroke="currentColor" stroke-width="2"/><circle cx="12" cy="12" r="3.4" fill="currentColor"/></svg>)";

    // Parsed once, then rasterized+cached per size/tint by NUISVGRenderer, so the
    // per-frame cost is a single texture draw.
    const NUISVGDocument* mixerControlIcon(const char* svg) {
        static std::unordered_map<const char*, std::shared_ptr<NUISVGDocument>> docs;
        auto& doc = docs[svg];
        if (!doc) doc = NUISVGParser::parse(svg);
        return doc.get();
    }
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
    m_armOn = theme.getColor("armed");
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
    static constexpr const char* icons[kButtonCount] = {kMuteIconSvg, kSoloIconSvg, kRecordIconSvg};
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

        // Flat active state (no glow): the coloured fill + border + white icon
        // carry the on-state, matching the flat-active language used elsewhere.
        renderer.fillRoundedRect(visualRect, BTN_RADIUS, bg);
        renderer.strokeRoundedRect(visualRect, BTN_RADIUS, 1.0f, border);
        renderer.strokeRoundedRect({visualRect.x + 1.0f, visualRect.y + 1.0f, visualRect.width - 2.0f, visualRect.height - 2.0f},
                                   std::max(0.0f, BTN_RADIUS - 1.0f),
                                   1.0f,
                                   NUIColor::white().withAlpha(0.025f));
        // Centre the glyph in the raw button bounds (not the half-pixel-inset
        // visualRect) so the offsets stay symmetric integers — matches the
        // track-header control icons exactly.
        if (const auto* doc = mixerControlIcon(icons[i])) {
            const float iconSize = std::round(std::min(rect.width, rect.height) - 6.0f);
            const NUIRect iconRect(std::round(rect.x + (rect.width - iconSize) * 0.5f),
                                   std::round(rect.y + (rect.height - iconSize) * 0.5f),
                                   iconSize, iconSize);
            NUISVGRenderer::render(renderer, *doc, iconRect, textColor);
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
