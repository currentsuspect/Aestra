// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerToolbar.h"
#include "../Graphics/NUIDepth.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"

#include <algorithm>

namespace AestraUI {

namespace {
constexpr float kFont = 10.0f;
constexpr float kGap = 10.0f;
constexpr float kH = 20.0f;
// Below this width (a laptop mixer with the inspector open) the controls take their short forms.
constexpr float kCompactBelow = 470.0f;
} // namespace

bool UIMixerToolbar::compact() const { return getBounds().width < kCompactBelow; }
float UIMixerToolbar::segW() const { return compact() ? 46.0f : 52.0f; }
float UIMixerToolbar::toggleW() const { return compact() ? 74.0f : 128.0f; }
float UIMixerToolbar::resetW() const { return compact() ? 52.0f : 78.0f; }

NUIRect UIMixerToolbar::resetRect() const {
    const auto b = getBounds();
    return NUIRect{b.right() - resetW() - 6.0f, b.y + (b.height - kH) * 0.5f, resetW(), kH};
}

NUIRect UIMixerToolbar::groupRect() const {
    const auto r = resetRect();
    return NUIRect{r.x - kGap - toggleW(), r.y, toggleW(), kH};
}

NUIRect UIMixerToolbar::densityRect(MixerDensity d) const {
    const auto g = groupRect();
    const float left = g.x - kGap - segW() * 3.0f;
    return NUIRect{left + segW() * static_cast<float>(d), g.y, segW(), kH};
}

void UIMixerToolbar::setDensity(MixerDensity density) {
    if (density == m_density) return;
    m_density = density;
    repaint();
}

void UIMixerToolbar::setGroupByOutput(bool on) {
    if (on == m_group) return;
    m_group = on;
    repaint();
}

void UIMixerToolbar::setSummary(int channelCount, bool grouped) {
    std::string text = std::to_string(channelCount) + (channelCount == 1 ? " channel" : " channels");
    if (grouped) text += " \xC2\xB7 grouped by output";
    if (text == m_summary) return;
    m_summary = std::move(text);
    repaint();
}

void UIMixerToolbar::onRender(NUIRenderer& renderer) {
    const auto b = getBounds();
    if (b.isEmpty()) return;
    auto& theme = NUIThemeManager::getInstance();
    const NUIColor text = theme.getColor("textPrimary");
    const NUIColor muted = theme.getColor("textSecondary");

    // The title and summary take only the room the controls leave: the summary shortens, then goes, then the title.
    const float room = densityRect(MixerDensity::Wide).x - 8.0f;
    const float titleW = renderer.measureText("Mixer", 11.0f).width;
    if (b.x + 8.0f + titleW <= room)
        renderer.drawText("Mixer", {b.x + 8.0f, b.y + (b.height - 11.0f) * 0.5f - 1.0f}, 11.0f, text.withAlpha(0.95f));
    const float sx = b.x + 8.0f + titleW + 10.0f;
    std::string summary = m_summary;
    if (sx + renderer.measureText(summary, kFont).width > room) summary = m_summary.substr(0, m_summary.find(" \xC2\xB7"));
    if (sx + renderer.measureText(summary, kFont).width <= room)
        renderer.drawText(summary, {sx, b.y + (b.height - kFont) * 0.5f - 1.0f}, kFont, muted.withAlpha(0.8f));

    static constexpr const char* names[3] = {"Wide", "Normal", "Narrow"};
    const auto seg = NUIRect{densityRect(MixerDensity::Wide).x, densityRect(MixerDensity::Wide).y, segW() * 3.0f, kH};
    // Design Spec, Segment switch (locked): a sunk pill track, the choice on a lit thumb, flat text, quiet hover.
    Depth::bed(renderer, seg, seg.height * 0.5f, Depth::Cut::Shallow);
    for (int i = 0; i < 3; ++i) {
        const auto r = densityRect(static_cast<MixerDensity>(i));
        const bool on = static_cast<int>(m_density) == i;
        if (on) {
            const NUIRect thumb{r.x + 2.0f, r.y + 2.0f, r.width - 4.0f, r.height - 4.0f};
            Depth::object(renderer, thumb, thumb.height * 0.5f);
        }
        renderer.drawTextCentered(names[i], r, kFont, on || m_hover == i ? text : theme.getColor("textMuted"));
    }

    const auto g = groupRect();
    // Design Spec, Toggle (locked): a sunk pill track that fills with the accent when on, a lit thumb that slides.
    const NUIRect track{g.x + 6.0f, g.y + (g.height - 12.0f) * 0.5f, 22.0f, 12.0f};
    if (m_group) renderer.fillRoundedRect(track, 6.0f, theme.getColor("primary"));
    else Depth::bed(renderer, track, 6.0f, Depth::Cut::Slot);
    Depth::object(renderer, NUIRect{m_group ? track.right() - 10.0f : track.x + 2.0f, track.y + 2.0f, 8.0f, 8.0f}, 4.0f);
    renderer.drawText(compact() ? "Group" : "Group by output", {track.right() + 6.0f, g.y + (g.height - 9.0f) * 0.5f - 1.0f}, 9.0f,
                      m_hover == 3 ? text : muted);

    const auto r = resetRect();
    renderer.fillRoundedRect(r, r.height * 0.5f, theme.getColor(m_hover == 4 ? "surfaceRaised" : "surfaceTertiary"));
    renderer.drawTextCentered(compact() ? "Reset" : "Reset peaks", r, kFont, m_hover == 4 ? text : muted);
}

void UIMixerToolbar::onMouseLeave() {
    if (m_hover != -1) { m_hover = -1; repaint(); }
    NUIComponent::onMouseLeave();
}

bool UIMixerToolbar::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible() || !isEnabled()) return false;
    int hit = -1;
    for (int i = 0; i < 3; ++i)
        if (densityRect(static_cast<MixerDensity>(i)).contains(event.position)) hit = i;
    if (groupRect().contains(event.position)) hit = 3;
    if (resetRect().contains(event.position)) hit = 4;

    if (event.button == NUIMouseButton::None) {
        if (hit != m_hover) { m_hover = hit; repaint(); }
        return false;
    }
    if (event.pressed && event.button == NUIMouseButton::Left && hit >= 0) {
        if (hit < 3) {
            const auto d = static_cast<MixerDensity>(hit);
            if (d != m_density) {
                m_density = d;
                repaint();
                if (onDensityChanged) onDensityChanged(d);
            }
        } else if (hit == 3) {
            m_group = !m_group;
            repaint();
            if (onGroupToggled) onGroupToggled(m_group);
        } else if (onResetPeaks) {
            onResetPeaks();
        }
        return true;
    }
    return false;
}

} // namespace AestraUI
