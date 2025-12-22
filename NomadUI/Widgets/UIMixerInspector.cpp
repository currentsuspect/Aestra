// ¶¸ 2025 Nomad Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerInspector.h"

#include "../Core/NUIThemeSystem.h"
#include "../Graphics/NUIRenderer.h"
#include "../../Source/MixerViewModel.h"

#include <algorithm>
#include <cstdio>

namespace NomadUI {

namespace {
    constexpr float PAD = 10.0f;
    constexpr float TAB_H = 26.0f;
    constexpr float TAB_RADIUS = 6.0f;
    constexpr float SECTION_GAP = 10.0f;
    constexpr float HEADER_H = 44.0f;
    constexpr float ROW_H = 26.0f;
    constexpr float ROW_RADIUS = 6.0f;
}

UIMixerInspector::UIMixerInspector(Nomad::MixerViewModel* viewModel)
    : m_viewModel(viewModel)
{
    cacheThemeColors();
}

void UIMixerInspector::cacheThemeColors()
{
    auto& theme = NUIThemeManager::getInstance();
    m_bg = theme.getColor("backgroundPrimary");
    m_border = theme.getColor("borderSubtle").withAlpha(0.65f);
    m_text = theme.getColor("textPrimary");
    m_textSecondary = theme.getColor("textSecondary");
    m_tabBg = theme.getColor("surfaceTertiary");
    m_tabActive = theme.getColor("accentPrimary").withAlpha(0.22f);
    m_tabHover = theme.getColor("surfaceSecondary");
    m_addBg = theme.getColor("surfaceTertiary");
    m_addHover = theme.getColor("surfaceSecondary");
    m_addText = theme.getColor("textPrimary");
}

void UIMixerInspector::setActiveTab(Tab tab)
{
    if (m_activeTab == tab) return;
    m_activeTab = tab;
    repaint();
}

void UIMixerInspector::layoutHitRects()
{
    const auto b = getBounds();
    const float w = std::max(1.0f, b.width - PAD * 2.0f);
    const float x = b.x + PAD;
    const float y = b.y + PAD;

    const float tabW = std::floor((w - 6.0f) / 3.0f);
    const float gap = 3.0f;
    for (int i = 0; i < 3; ++i) {
        m_tabRects[i] = NUIRect{x + i * (tabW + gap), y, tabW, TAB_H};
    }

    const float contentY = y + TAB_H + SECTION_GAP + HEADER_H + SECTION_GAP;
    m_addFxRect = NUIRect{x, contentY + 18.0f, w, ROW_H};
}

int UIMixerInspector::hitTestTab(const NUIPoint& p) const
{
    for (int i = 0; i < 3; ++i) {
        if (m_tabRects[i].contains(p)) return i;
    }
    return -1;
}

void UIMixerInspector::onResize(int width, int height)
{
    NUIComponent::onResize(width, height);
    layoutHitRects();
}

int UIMixerInspector::findTrackNumber(uint32_t channelId) const
{
    if (!m_viewModel || channelId == 0) return 0;
    const size_t count = m_viewModel->getChannelCount();
    for (size_t i = 0; i < count; ++i) {
        const auto* ch = m_viewModel->getChannelByIndex(i);
        if (ch && ch->id == channelId) {
            return static_cast<int>(i + 1);
        }
    }
    return 0;
}

void UIMixerInspector::updateHeaderCache(const Nomad::ChannelViewModel* channel)
{
    const uint32_t selectedId = channel ? channel->id : 0xFFFFFFFFu;
    const bool identityUnchanged =
        (m_cachedSelectedId == selectedId) &&
        (channel ? (m_cachedName == channel->name && m_cachedRoute == channel->routeName) : (m_cachedName.empty() && m_cachedRoute.empty()));
    if (identityUnchanged) return;

    m_cachedSelectedId = selectedId;
    m_cachedHeaderTitle.clear();
    m_cachedHeaderSubtitle.clear();
    m_cachedTrackNumber = 0;
    m_cachedName = channel ? channel->name : std::string();
    m_cachedRoute = channel ? channel->routeName : std::string();

    if (!channel) {
        m_cachedHeaderTitle = "Inspector";
        return;
    }

    if (channel->id == 0) {
        m_cachedHeaderTitle = "MASTER";
        m_cachedHeaderSubtitle = "Output";
        return;
    }

    m_cachedTrackNumber = findTrackNumber(channel->id);
    if (m_cachedTrackNumber > 0) {
        m_cachedHeaderTitle = "Track " + std::to_string(m_cachedTrackNumber) + " — " + channel->name;
    } else {
        m_cachedHeaderTitle = channel->name;
    }

    // Track type is currently audio-only.
    if (!channel->routeName.empty()) {
        m_cachedHeaderSubtitle = "Audio → " + channel->routeName;
    } else {
        m_cachedHeaderSubtitle = "Audio";
    }
}

void UIMixerInspector::onRender(NUIRenderer& renderer)
{
    const auto b = getBounds();
    if (b.isEmpty()) return;

    renderer.fillRect(b, m_bg);

    // Left separator line (container draws outer separators too; keep this subtle).
    renderer.drawLine({b.x, b.y}, {b.x, b.bottom()}, 1.0f, m_border);

    const auto* channel = m_viewModel ? m_viewModel->getSelectedChannel() : nullptr;
    updateHeaderCache(channel);

    // Tabs
    static constexpr const char* tabLabels[3] = {"Inserts", "Sends", "I/O"};
    for (int i = 0; i < 3; ++i) {
        const bool active = (static_cast<int>(m_activeTab) == i);
        const bool hovered = (m_hoveredTab == i);
        NUIColor bg = active ? m_tabActive : (hovered ? m_tabHover : m_tabBg);
        renderer.fillRoundedRect(m_tabRects[i], TAB_RADIUS, bg);
        renderer.strokeRoundedRect(m_tabRects[i], TAB_RADIUS, 1.0f, m_border);
        renderer.drawTextCentered(tabLabels[i], m_tabRects[i], 10.0f, active ? m_text : m_textSecondary);
    }

    // Header
    const float headerY = b.y + PAD + TAB_H + SECTION_GAP;
    const NUIRect headerRect{b.x + PAD, headerY, b.width - PAD * 2.0f, HEADER_H};

    renderer.drawText(m_cachedHeaderTitle, {headerRect.x, headerRect.y}, 12.0f, m_text);
    if (!m_cachedHeaderSubtitle.empty()) {
        renderer.drawText(m_cachedHeaderSubtitle, {headerRect.x, headerRect.y + 16.0f}, 10.0f, m_textSecondary);
    }
    if (channel) {
        renderer.drawText("Input → Trim → Inserts → Sends → Fader → Output",
                          {headerRect.x, headerRect.y + 32.0f}, 9.0f, m_textSecondary.withAlpha(0.85f));
    }

    const float contentTop = b.y + PAD + TAB_H + SECTION_GAP + HEADER_H + SECTION_GAP;
    const NUIRect contentRect{b.x + PAD, contentTop, b.width - PAD * 2.0f, b.height - (contentTop - b.y) - PAD};

    // Content
    if (!channel) {
        renderer.drawTextCentered("Select a track to edit Inserts, Sends, and I/O", contentRect, 11.0f, m_textSecondary);
        return;
    }

    if (m_activeTab == Tab::Inserts) {
        const int fxCount = channel->fxCount;
        char buf[64];
        if (fxCount <= 0) {
            std::snprintf(buf, sizeof(buf), "No inserts");
        } else {
            std::snprintf(buf, sizeof(buf), "%d insert%s active", fxCount, fxCount == 1 ? "" : "s");
        }
        renderer.drawText(buf, {contentRect.x, contentRect.y}, 11.0f, m_textSecondary);

        // "Add FX" placeholder button
        NUIColor addBg = m_addPressed ? m_addHover : (m_addHovered ? m_addHover : m_addBg);
        renderer.fillRoundedRect(m_addFxRect, ROW_RADIUS, addBg);
        renderer.strokeRoundedRect(m_addFxRect, ROW_RADIUS, 1.0f, m_border);
        renderer.drawTextCentered("Add FX", m_addFxRect, 11.0f, m_addText);
    } else {
        renderer.drawTextCentered("Coming soon", contentRect, 11.0f, m_textSecondary);
    }
}

bool UIMixerInspector::onMouseEvent(const NUIMouseEvent& event)
{
    if (!isVisible() || !isEnabled()) return false;

    const auto b = getBounds();
    if (!b.contains(event.position) && event.button != NUIMouseButton::None) return false;

    if (event.button == NUIMouseButton::None) {
        const int tab = hitTestTab(event.position);
        if (tab != m_hoveredTab) {
            m_hoveredTab = tab;
            repaint();
        }

        const bool addHover = (m_viewModel && m_viewModel->getSelectedChannel()) && m_addFxRect.contains(event.position);
        if (addHover != m_addHovered) {
            m_addHovered = addHover;
            repaint();
        }
        return false;
    }

    if (event.pressed && event.button == NUIMouseButton::Left) {
        const int tab = hitTestTab(event.position);
        if (tab >= 0) {
            setActiveTab(static_cast<Tab>(tab));
            return true;
        }

        if ((m_viewModel && m_viewModel->getSelectedChannel()) && m_addFxRect.contains(event.position)) {
            m_addPressed = true;
            repaint();
            return true;
        }
    }

    if (event.released && event.button == NUIMouseButton::Left) {
        if (m_addPressed) {
            m_addPressed = false;
            repaint();
            // Placeholder action (effect insertion is handled elsewhere).
            return true;
        }
    }

    return false;
}

} // namespace NomadUI
