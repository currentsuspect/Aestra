// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NotificationToast.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "NUIDepth.h"
#include <algorithm>

namespace AestraUI {

NotificationToast::NotificationToast()
{
    setDropPassThrough(true); // mouse-transparent status text: drops reach the panel under it
}

void NotificationToast::onRender(NUIRenderer& renderer)
{
    if (stack_.empty())
        return;

    auto& theme = NUIThemeManager::getInstance();
    const auto& t = theme.getCurrentTheme();
    const NUIRect bounds = getBounds();
    constexpr float kFontSize = 12.0f;
    constexpr double kFadeIn = 0.12;  // arrives quickly
    constexpr double kFadeOut = 0.35; // leaves gently instead of vanishing

    // Newest in the bottom slot, each older one a slot higher.
    const auto& entries = stack_.entries();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const ToastEntry& e = entries[entries.size() - 1 - i];
        const double remaining = kToastSeconds - e.age;
        const float alpha = static_cast<float>(std::clamp(std::min(e.age / kFadeIn, remaining / kFadeOut), 0.0, 1.0));
        const NUISize textSize = renderer.measureText(e.text, kFontSize);
        const float pillW = std::min(bounds.width, textSize.width + 32.0f);
        const float pillH = std::min(kSlotHeight, 32.0f);
        const float slotY = bounds.bottom() - kSlotHeight * static_cast<float>(i + 1);
        const NUIRect pill(bounds.x + (bounds.width - pillW) * 0.5f, slotY + (kSlotHeight - pillH) * 0.5f, pillW, pillH);
        // Design Spec, Toast (locked 12 Nov 2026): an accent-filled pill floating on a soft shadow, no outline.
        renderer.setOpacity(alpha);
        Depth::floatingCard(renderer, pill, pillH * 0.5f, t.primary);
        renderer.drawTextCentered(e.text, pill, kFontSize, t.textOnPrimary);
    }
    renderer.setOpacity(1.0f);
}

void NotificationToast::onUpdate(double deltaTime)
{
    if (stack_.empty())
        return;
    stack_.update(deltaTime);
    if (stack_.empty())
        setVisible(false);
    repaint(); // animate the fades while anything is showing
}

bool NotificationToast::onMouseEvent(const NUIMouseEvent& /*event*/)
{
    return false; // passive overlay — never intercept
}

void NotificationToast::show(const std::string& text)
{
    stack_.push(text);
    setVisible(true);
    repaint();
}

} // namespace AestraUI
