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
    m_selectedBg = theme.getColor("accentPrimary").withAlpha(0.10f);
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

    // Background for selection
    if (m_selected) {
        renderer.fillRect(bounds, m_selectedBg);
    }

    // Top Colored Bar (Visual Indicator) - Replaces side chip
    constexpr float TOP_BAR_H = 3.0f;
    
    // Explicitly define top bar rect to cover full width
    // Use floor/ceil to snap to pixels and avoid subpixel gaps (which causes "missing right/top" look)
    NUIRect topBar{
        std::floor(bounds.x), 
        std::floor(bounds.y), 
        std::ceil(bounds.width), 
        TOP_BAR_H
    };
    
    // Use Primary Purple for Master if detection fails, otherwise use track color
    NUIColor barColor = (m_isMaster && m_trackColorArgb == 0) // Fallback for master
                        ? NUIThemeManager::getInstance().getColor("primary") 
                        : colorFromARGB(m_trackColorArgb);
                        
    // Ensure alpha is 1.0 for the bar itself
    barColor = barColor.withAlpha(1.0f);
    
    renderer.fillRect(topBar, barColor);

    // Text area (Below the top bar)
    // textRect starts AFTER top bar + padding
    NUIRect textRect{
        bounds.x + PAD_X, 
        bounds.y + TOP_BAR_H + 2.0f, // padding below bar
        bounds.width - (PAD_X * 2), 
        bounds.height - (TOP_BAR_H + 2.0f)
    };

    const float nameFont = m_isMaster ? 13.0f : 12.0f;
    const float routeFont = m_isMaster ? 10.0f : 9.0f;

    // Name (top half of remaining space)
    NUIRect nameRect{textRect.x, textRect.y, textRect.width, textRect.height * 0.55f};
    renderer.drawTextCentered(m_name, nameRect, nameFont, m_selected ? m_selectedText : m_text);

    // Route (bottom half)
    if (!m_route.empty()) {
        const float routeH = m_isMaster ? 14.0f : 12.0f;
        NUIRect routeRect{textRect.x, textRect.y + textRect.height - routeH - 2.0f, textRect.width, routeH};
        renderer.drawTextCentered(m_route, routeRect, routeFont, m_textSecondary);
    }
}

} // namespace AestraUI
