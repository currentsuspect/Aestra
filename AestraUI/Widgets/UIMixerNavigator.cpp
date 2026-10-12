// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerNavigator.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"

#include <algorithm>
#include <cmath>

namespace AestraUI {

NUIRect UIMixerNavigator::tickRow() const {
    const auto b = getBounds();
    const float labels = m_groups.empty() ? 0.0f : kGroupRowH;
    return NUIRect{b.x, b.y + labels + 2.0f, b.width, kTickRowH};
}

float UIMixerNavigator::toNavX(float contentX) const {
    const auto b = getBounds();
    return b.x + (m_contentW > 0.0f ? contentX / m_contentW : 0.0f) * b.width;
}

float UIMixerNavigator::toContentDelta(float navDx) const {
    const auto b = getBounds();
    return b.width > 0.0f ? navDx / b.width * m_contentW : 0.0f;
}

void UIMixerNavigator::setContent(std::vector<Tick> ticks, std::vector<Group> groups, float contentWidth) {
    m_ticks = std::move(ticks);
    m_groups = std::move(groups);
    m_contentW = std::max(1.0f, contentWidth);
    repaint();
}

void UIMixerNavigator::setViewport(float scrollX, float viewWidth) {
    m_scrollX = std::max(0.0f, scrollX);
    m_viewW = std::max(1.0f, viewWidth);
}

NUIRect UIMixerNavigator::tickRect(size_t index) const {
    if (index >= m_ticks.size()) return NUIRect{};
    const auto row = tickRow();
    const auto& t = m_ticks[index];
    const float x = toNavX(t.x);
    return NUIRect{x, row.y, std::max(2.0f, toNavX(t.x + t.w) - x - 1.0f), row.height};
}

NUIRect UIMixerNavigator::groupLabelRect(size_t index) const {
    if (index >= m_groups.size()) return NUIRect{};
    const auto b = getBounds();
    const auto& g = m_groups[index];
    const float x = toNavX(g.x);
    return NUIRect{x, b.y, std::max(8.0f, toNavX(g.x + g.w) - x), kGroupRowH};
}

NUIRect UIMixerNavigator::bracketRect() const {
    const auto row = tickRow();
    const float x = toNavX(m_scrollX);
    const float w = std::max(14.0f, toNavX(m_scrollX + m_viewW) - x);
    return NUIRect{x - 1.0f, row.y - 2.0f, w + 2.0f, row.height + 4.0f};
}

float UIMixerNavigator::scrollToCentre(float x, float w) const {
    return x + w * 0.5f - m_viewW * 0.5f;
}

void UIMixerNavigator::onRender(NUIRenderer& renderer) {
    const auto b = getBounds();
    if (b.isEmpty() || m_ticks.empty()) return;
    auto& theme = NUIThemeManager::getInstance();
    const NUIColor text = theme.getColor("textPrimary");
    const NUIColor muted = theme.getColor("textSecondary");
    const NUIColor accent = theme.getColor("accentPrimary");
    const NUIColor safe = theme.getColor("meterSafe");
    const NUIColor crit = theme.getColor("meterCrit");
    const NUIColor solo = theme.getColor("soloed");

    for (size_t i = 0; i < m_groups.size(); ++i) {
        const auto r = groupLabelRect(i);
        renderer.fillRect(NUIRect{r.x, r.bottom() - 1.0f, std::max(2.0f, r.width - 2.0f), 1.0f}, muted.withAlpha(0.35f));
        std::string label = m_groups[i].label;
        while (label.size() > 1 && renderer.measureText("\xE2\x86\x92 " + label, 9.0f).width > r.width - 4.0f) label.pop_back();
        if (r.width > 20.0f) renderer.drawText("\xE2\x86\x92 " + label, {r.x + 1.0f, r.y}, 9.0f, muted.withAlpha(0.85f));
    }

    for (size_t i = 0; i < m_ticks.size(); ++i) {
        const auto& t = m_ticks[i];
        const auto r = tickRect(i);
        renderer.fillRect(r, theme.getColor("border").withAlpha(0.22f));
        const float lvl = std::clamp(t.level, 0.0f, 1.0f);
        const float bodyH = r.height - 4.0f;
        renderer.fillRect(NUIRect{r.x, r.bottom() - std::max(0.0f, bodyH * lvl), r.width, std::max(0.0f, bodyH * lvl)},
                          safe.withAlpha(0.55f));
        renderer.fillRect(NUIRect{r.x, r.y, r.width, 3.0f}, t.color);
        if (t.selected) renderer.strokeRect(r, 1.0f, text.withAlpha(0.7f));
        // Flags stay visible however far the channel is from the bracket.
        if (t.clipped) renderer.fillRect(NUIRect{r.x, r.bottom() - 3.0f, r.width, 3.0f}, crit);
        else if (t.soloed) renderer.fillRect(NUIRect{r.x, r.bottom() - 3.0f, r.width, 3.0f}, solo);
    }

    const auto br = bracketRect();
    renderer.fillRoundedRect(br, 3.0f, accent.withAlpha(0.10f));
    renderer.strokeRoundedRect(br, 3.0f, 1.25f, accent.withAlpha(m_draggingBracket ? 0.95f : 0.65f));
}

bool UIMixerNavigator::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible() || !isEnabled() || m_ticks.empty()) return false;
    const auto b = getBounds();

    if (m_draggingBracket) {
        if (event.released && event.button == NUIMouseButton::Left) {
            m_draggingBracket = false;
            repaint();
            return true;
        }
        if (event.button == NUIMouseButton::None && onScrollRequested)
            onScrollRequested(m_dragStartScroll + toContentDelta(event.position.x - m_dragStartX));
        return true;
    }
    if (!b.contains(event.position)) return false;
    if (!(event.pressed && event.button == NUIMouseButton::Left)) return false;

    if (bracketRect().contains(event.position)) {
        m_draggingBracket = true;
        m_dragStartX = event.position.x;
        m_dragStartScroll = m_scrollX;
        repaint();
        return true;
    }
    for (size_t i = 0; i < m_groups.size(); ++i) {
        if (groupLabelRect(i).contains(event.position)) {
            if (onScrollRequested) onScrollRequested(m_groups[i].x);
            return true;
        }
    }
    for (size_t i = 0; i < m_ticks.size(); ++i) {
        auto r = tickRect(i);
        r.x -= 0.5f;
        r.width += 1.0f; // the gap between two ticks belongs to the nearer one
        if (r.contains(event.position)) {
            if (onScrollRequested) onScrollRequested(scrollToCentre(m_ticks[i].x, m_ticks[i].w));
            if (onTickClicked) onTickClicked(m_ticks[i].channelId);
            return true;
        }
    }
    return true; // empty space inside the navigator is still the navigator's
}

} // namespace AestraUI
