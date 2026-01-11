// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerHeader.h"

#include "../Core/NUIThemeSystem.h"
#include "../Graphics/NUIRenderer.h"

#include <algorithm>

namespace NomadUI {

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

    if (m_selected) {
        renderer.fillRect(bounds, m_selectedBg);
    }

    // Color chip
    NUIRect chip{bounds.x, bounds.y, CHIP_W, bounds.height};
    renderer.fillRect(chip, colorFromARGB(m_trackColorArgb));

    // Text area
    NUIRect textRect{bounds.x + CHIP_W + PAD_X, bounds.y, bounds.width - CHIP_W - PAD_X, bounds.height};

    const float nameFont = m_isMaster ? 13.0f : 11.0f;
    const float routeFont = m_isMaster ? 10.0f : 9.0f;

    // Name (top)
    NUIRect nameRect{textRect.x, textRect.y + 2.0f, textRect.width, textRect.height * 0.6f};
    renderer.drawTextCentered(m_name, nameRect, nameFont, m_selected ? m_selectedText : m_text);

    // Route (bottom)
    if (!m_route.empty()) {
        const float routeH = m_isMaster ? 14.0f : 12.0f;
        NUIRect routeRect{textRect.x, bounds.y + bounds.height - routeH, textRect.width, routeH};
        renderer.drawTextCentered(m_route, routeRect, routeFont, m_textSecondary);
    }
}

} // namespace NomadUI
