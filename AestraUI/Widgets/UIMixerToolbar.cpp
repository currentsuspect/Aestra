// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerToolbar.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"

#include <algorithm>

namespace AestraUI {

namespace {
constexpr float kFont = 10.0f;
constexpr float kSegW = 52.0f;
constexpr float kGap = 10.0f;
constexpr float kToggleW = 128.0f;
constexpr float kResetW = 78.0f;
constexpr float kH = 20.0f;
} // namespace

NUIRect UIMixerToolbar::resetRect() const {
    const auto b = getBounds();
    return NUIRect{b.right() - kResetW - 6.0f, b.y + (b.height - kH) * 0.5f, kResetW, kH};
}

NUIRect UIMixerToolbar::groupRect() const {
    const auto r = resetRect();
    return NUIRect{r.x - kGap - kToggleW, r.y, kToggleW, kH};
}

NUIRect UIMixerToolbar::densityRect(MixerDensity d) const {
    const auto g = groupRect();
    const float left = g.x - kGap - kSegW * 3.0f;
    return NUIRect{left + kSegW * static_cast<float>(d), g.y, kSegW, kH};
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
    const NUIColor border = theme.getColor("border").withAlpha(0.45f);
    const NUIColor accent = theme.getColor("accentPrimary");

    renderer.drawText("Mixer", {b.x + 8.0f, b.y + (b.height - 11.0f) * 0.5f - 1.0f}, 11.0f, text.withAlpha(0.95f));
    renderer.drawText(m_summary, {b.x + 52.0f, b.y + (b.height - kFont) * 0.5f - 1.0f}, kFont, muted.withAlpha(0.8f));

    static constexpr const char* names[3] = {"Wide", "Normal", "Narrow"};
    const auto seg = NUIRect{densityRect(MixerDensity::Wide).x, densityRect(MixerDensity::Wide).y, kSegW * 3.0f, kH};
    renderer.strokeRoundedRect(seg, 3.0f, 1.0f, border);
    for (int i = 0; i < 3; ++i) {
        const auto r = densityRect(static_cast<MixerDensity>(i));
        const bool on = static_cast<int>(m_density) == i;
        if (on) renderer.fillRoundedRect(r, 3.0f, accent.withAlpha(0.28f));
        else if (m_hover == i) renderer.fillRoundedRect(r, 3.0f, border.withAlpha(0.35f));
        renderer.drawTextCentered(names[i], r, kFont, on ? text : muted);
    }

    const auto g = groupRect();
    renderer.strokeRoundedRect(g, 3.0f, 1.0f, border);
    // The switch: a small track with a knob that sits right when on.
    const NUIRect track{g.x + 8.0f, g.y + (g.height - 10.0f) * 0.5f, 22.0f, 10.0f};
    renderer.fillRoundedRect(track, 5.0f, m_group ? accent.withAlpha(0.85f) : border);
    renderer.fillRoundedRect(NUIRect{m_group ? track.right() - 9.0f : track.x + 1.0f, track.y + 1.0f, 8.0f, 8.0f}, 4.0f, text);
    renderer.drawText("Group by output", {track.right() + 6.0f, g.y + (g.height - 9.0f) * 0.5f - 1.0f}, 9.0f,
                      m_hover == 3 ? text : muted);

    const auto r = resetRect();
    if (m_hover == 4) renderer.fillRoundedRect(r, 3.0f, border.withAlpha(0.35f));
    renderer.strokeRoundedRect(r, 3.0f, 1.0f, border);
    renderer.drawTextCentered("Reset peaks", r, kFont, m_hover == 4 ? text : muted);
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
