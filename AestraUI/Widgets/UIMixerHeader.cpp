// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerHeader.h"

#include "NUIThemeSystem.h"
#include "NUIRenderer.h"

#include <algorithm>
#include <cmath>

namespace AestraUI {

namespace {
    constexpr float CHIP_W = 4.0f;
    constexpr float PAD_X = 6.0f;
}

UIMixerHeader::UIMixerHeader()
{
    cacheThemeColors();
}

void UIMixerHeader::cacheThemeColors()
{
    auto& theme = NUIThemeManager::getInstance();
    m_text = theme.getColor("textPrimary");
    m_textSecondary = theme.getColor("textSecondary");
    m_selectedText = theme.getColor("textPrimary");
    m_selectedBg = theme.getColor("primary").withAlpha(0.06f);
    m_selectedBorder = theme.getColor("primary").withAlpha(0.16f);
}

NUIColor UIMixerHeader::colorFromARGB(uint32_t argb)
{
    const float a = ((argb >> 24) & 0xFF) / 255.0f;
    const float r = ((argb >> 16) & 0xFF) / 255.0f;
    const float g = ((argb >> 8) & 0xFF) / 255.0f;
    const float b = (argb & 0xFF) / 255.0f;
    return {r, g, b, a};
}

void UIMixerHeader::setTrackName(std::string name)
{
    if (m_name == name) return;
    m_name = std::move(name);
    repaint();
}

void UIMixerHeader::setRouteName(std::string route)
{
    if (m_route == route) return;
    m_route = std::move(route);
    repaint();
}

void UIMixerHeader::setTrackColor(uint32_t argb)
{
    if (m_trackColorArgb == argb) return;
    m_trackColorArgb = argb;
    repaint();
}

void UIMixerHeader::setTrackColorIndex(int index)
{
    if (m_colorIndex == index) return;
    m_colorIndex = index;
    setTrackColor(paletteIndexToARGB(index));
}

void UIMixerHeader::setSelected(bool selected)
{
    if (m_selected == selected) return;
    m_selected = selected;
    repaint();
}

void UIMixerHeader::setIsMaster(bool isMaster)
{
    if (m_isMaster == isMaster) return;
    m_isMaster = isMaster;
    repaint();
}

void UIMixerHeader::onRender(NUIRenderer& renderer)
{
    auto bounds = getBounds();
    
    // Safety check for invalid bounds
    if (bounds.width <= 0 || bounds.height <= 0) return;

    // Background for selection (tint with track color)
    if (m_selected) {
        renderer.fillRoundedRect(bounds, 10.0f, m_selectedBg);
        renderer.strokeRoundedRect(bounds, 10.0f, 1.0f, m_selectedBorder);
    }

    // Top Colored Bar (Visual Identity) — pulled from Timeline track color
    constexpr float TOP_BAR_H = 6.0f;

    NUIRect topBar{
        std::floor(bounds.x),
        std::floor(bounds.y),
        std::ceil(bounds.width),
        TOP_BAR_H
    };

    NUIColor trackColor = (m_isMaster && m_trackColorArgb == 0)
                        ? NUIThemeManager::getInstance().getColor("primary")
                        : colorFromARGB(m_trackColorArgb);
    trackColor = trackColor.withAlpha(1.0f);

    // Full-width track-color bar
    renderer.fillRect(topBar, trackColor);

    // Subtle background tint across the rest of the header for stronger color identity
    if (!m_isMaster && m_trackColorArgb != 0) {
        NUIRect tintRect{bounds.x, bounds.y + TOP_BAR_H, bounds.width, bounds.height - TOP_BAR_H};
        renderer.fillRoundedRect(tintRect, std::max(0.0f, 10.0f - TOP_BAR_H), trackColor.withAlpha(0.06f));
    }

    // Text area (below the top bar)
    NUIRect textRect{
        bounds.x + PAD_X,
        bounds.y + TOP_BAR_H + 3.0f,
        bounds.width - (PAD_X * 2),
        bounds.height - (TOP_BAR_H + 3.0f)
    };

    const float nameFont = m_isMaster ? 13.0f : 12.5f;
    const float routeFont = m_isMaster ? 10.0f : 9.5f;

    if (m_isMaster) {
        // Master strip: eyebrow label + large title, vertically centered as a stack.
        // The eyebrow ("MAIN BUS") signals this is the global mix bus, not just a strip.
        constexpr float EYEBROW_H = 10.0f;
        constexpr float MASTER_NAME_H = 16.0f;
        constexpr float MASTER_ROUTE_H = 11.0f;
        constexpr float STACK_GAP = 2.0f;

        const float stackH = EYEBROW_H + STACK_GAP + MASTER_NAME_H +
                              (m_route.empty() ? 0.0f : STACK_GAP + MASTER_ROUTE_H);
        const float stackY = textRect.y + std::max(0.0f, (textRect.height - stackH) * 0.5f) - 1.0f;

        // Eyebrow
        NUIColor eyebrowColor = trackColor.withAlpha(0.85f);
        NUIRect eyebrowRect{textRect.x, stackY, textRect.width, EYEBROW_H};
        renderer.drawTextCentered("MAIN BUS", eyebrowRect, 9.0f, eyebrowColor);

        // Title
        NUIRect nameRect{textRect.x, stackY + EYEBROW_H + STACK_GAP,
                          textRect.width, MASTER_NAME_H};
        renderer.drawTextCentered(m_name, nameRect, 14.0f,
                                  m_selected ? m_selectedText : m_text);

        // Subtitle
        if (!m_route.empty()) {
            NUIRect routeRect{textRect.x,
                              stackY + EYEBROW_H + STACK_GAP + MASTER_NAME_H + STACK_GAP,
                              textRect.width, MASTER_ROUTE_H};
            renderer.drawTextCentered(m_route, routeRect, routeFont,
                                      m_textSecondary.withAlpha(0.88f));
        }
        return;
    }

    constexpr float TRACK_NAME_H = 13.5f;
    constexpr float TRACK_ROUTE_H = 10.5f;
    constexpr float TRACK_GAP = 1.5f;
    const float stackH = m_route.empty()
        ? TRACK_NAME_H
        : (TRACK_NAME_H + TRACK_GAP + TRACK_ROUTE_H);
    const float stackY = textRect.y + std::max(0.0f, (textRect.height - stackH) * 0.5f) - 0.5f;

    NUIRect nameRect{textRect.x, stackY, textRect.width, TRACK_NAME_H};
    renderer.drawTextCentered(m_name, nameRect, nameFont, m_selected ? m_selectedText : m_text);

    if (!m_route.empty()) {
        NUIRect routeRect{textRect.x, stackY + TRACK_NAME_H + TRACK_GAP, textRect.width, TRACK_ROUTE_H};
        renderer.drawTextCentered(m_route,
                                  routeRect,
                                  routeFont,
                                  m_selected ? m_textSecondary.withAlpha(0.96f) : m_textSecondary.withAlpha(0.88f));
    }
}

bool UIMixerHeader::onMouseEvent(const NUIMouseEvent& event)
{
    if (!isVisible() || !isEnabled()) return false;
    if (!getBounds().contains(event.position)) return false;

    if (event.pressed && event.button == NUIMouseButton::Right) {
        if (!m_colorMenu) {
            m_colorMenu = std::make_shared<NUIContextMenu>();
            m_colorMenu->setCloseOnSelection(true);
        }
        m_colorMenu->clear();

        for (int i = 0; i < PALETTE_SIZE; ++i) {
            const bool selected = (i == m_colorIndex);
            m_colorMenu->addRadioItem(PALETTE_NAMES[i], "color", selected, [this, i]() {
                m_colorIndex = i;
                m_trackColorArgb = paletteIndexToARGB(i);
                repaint();
                if (onColorChanged) onColorChanged(i);
            });
        }

        NUIComponent* root = this;
        while (root->getParent()) root = root->getParent();
        root->addChild(m_colorMenu);
        m_colorMenu->showAt(event.position);
        root->repaint();
        return true;
    }

    return false;
}

} // namespace AestraUI
