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

/**
 * @brief Cache theme-derived colors into the header's member color fields.
 *
 * Retrieves color values from the global theme and stores them into the instance members:
 * - m_text        <- theme key "textPrimary"
 * - m_textSecondary <- theme key "textSecondary"
 * - m_selectedText  <- theme key "textPrimary"
 * - m_selectedBg    <- theme key "accentPrimary" with alpha set to 0.13f
 */
void UIMixerHeader::cacheThemeColors()
{
    auto& theme = NUIThemeManager::getInstance();
    m_text = theme.getColor("textPrimary");
    m_textSecondary = theme.getColor("textSecondary");
    m_selectedText = theme.getColor("textPrimary");
    m_selectedBg = theme.getColor("accentPrimary").withAlpha(0.13f);
}

/**
 * @brief Create an NUIColor from a 32-bit packed ARGB value.
 *
 * @param argb 32-bit integer in ARGB order where the highest byte is alpha,
 *             followed by red, green, and blue (0xAARRGGBB).
 * @return NUIColor Color with red, green, blue, and alpha channels normalized
 *         to the range [0.0, 1.0] (returned as {r, g, b, a}).
 */
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

/**
 * @brief Renders the mixer header widget into the provided renderer.
 *
 * Draws an optional rounded selection background, a full-width top color bar (uses track color
 * or the theme primary color when this is a master with no track color), and a vertically centered
 * title (track name) with an optional subtitle (route). Font sizes, vertical spacing, and
 * color/opacity for the subtitle differ between master and non-master modes and change when selected.
 *
 * @param renderer Rendering backend used to draw rects and centered text.
 */
void UIMixerHeader::onRender(NUIRenderer& renderer)
{
    auto bounds = getBounds();
    
    // Safety check for invalid bounds
    if (bounds.width <= 0 || bounds.height <= 0) return;

    // Background for selection
    if (m_selected) {
        renderer.fillRoundedRect(bounds, 10.0f, m_selectedBg);
    }

    // Top Colored Bar (Visual Indicator) - Replaces side chip
    constexpr float TOP_BAR_H = 4.0f;
    
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

    const float nameFont = m_isMaster ? 13.0f : 12.5f;
    const float routeFont = m_isMaster ? 10.0f : 9.5f;

    if (m_isMaster) {
        // The master strip reads better when title + subtitle are treated as one
        // vertically centered stack instead of using the generic track split.
        constexpr float MASTER_NAME_H = 14.0f;
        constexpr float MASTER_ROUTE_H = 11.0f;
        constexpr float MASTER_GAP = 1.5f;
        const float stackH = m_route.empty()
            ? MASTER_NAME_H
            : (MASTER_NAME_H + MASTER_GAP + MASTER_ROUTE_H);
        const float stackY = textRect.y + std::max(0.0f, (textRect.height - stackH) * 0.5f) - 1.0f;

        NUIRect nameRect{textRect.x, stackY, textRect.width, MASTER_NAME_H};
        renderer.drawTextCentered(m_name, nameRect, nameFont, m_selected ? m_selectedText : m_text);

        if (!m_route.empty()) {
            NUIRect routeRect{textRect.x, stackY + MASTER_NAME_H + MASTER_GAP, textRect.width, MASTER_ROUTE_H};
            renderer.drawTextCentered(m_route, routeRect, routeFont, m_textSecondary);
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

} // namespace AestraUI
