// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NotificationToast.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "NUIDepth.h"
#include <algorithm>

namespace AestraUI {

NotificationToast::NotificationToast()
    : duration_(2.0), elapsed_(0.0)
{
    setDropPassThrough(true); // mouse-transparent status text: drops reach the panel under it
}

void NotificationToast::onRender(NUIRenderer& renderer)
{
    if (text_.empty())
        return;

    // Fade out over the toast's final moments instead of vanishing.
    constexpr double kFadeSeconds = 0.35;
    const double remaining = duration_ - elapsed_;
    const float alpha = remaining >= kFadeSeconds
        ? 1.0f
        : std::max(0.0f, static_cast<float>(remaining / kFadeSeconds));

    auto& theme = NUIThemeManager::getInstance();
    const NUIRect bounds = getBounds();
    constexpr float kFontSize = 12.0f;
    const NUISize textSize = renderer.measureText(text_, kFontSize);
    const float pillW = std::min(bounds.width, textSize.width + 32.0f);
    const float pillH = std::min(bounds.height, 32.0f);
    const NUIRect pill(bounds.x + (bounds.width - pillW) * 0.5f,
                       bounds.y + (bounds.height - pillH) * 0.5f, pillW, pillH);
    // Design Spec, Toast (locked 12 Nov 2026): an accent-filled pill floating on a soft shadow, no outline.
    const auto& t = theme.getCurrentTheme();
    renderer.setOpacity(alpha);
    Depth::floatingCard(renderer, pill, pillH * 0.5f, t.primary);
    renderer.drawTextCentered(text_, pill, kFontSize, t.textOnPrimary);
    renderer.setOpacity(1.0f);
}

void NotificationToast::onUpdate(double deltaTime)
{
    elapsed_ += deltaTime;
    if (elapsed_ >= duration_)
    {
        if (auto next = queue_.next()) { // the next waiting message takes its turn
            setText(next->text);
            setDuration(next->seconds);
        } else {
            setVisible(false);
        }
    }
    repaint(); // animate the fade while visible
}

bool NotificationToast::onMouseEvent(const NUIMouseEvent& /*event*/)
{
    return false; // passive overlay — never intercept
}

void NotificationToast::setText(const std::string& text)
{
    text_ = text;
    repaint();
}

void NotificationToast::show(const std::string& text, double seconds)
{
    if (auto now = queue_.offer({text, seconds}, isVisible() ? &text_ : nullptr)) {
        setText(now->text);
        setDuration(now->seconds);
        setVisible(true);
    }
}

void NotificationToast::setDuration(double duration)
{
    duration_ = std::max(0.1, duration);
    elapsed_ = 0.0;
}

} // namespace AestraUI
