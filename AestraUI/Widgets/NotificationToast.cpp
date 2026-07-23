// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NotificationToast.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include <algorithm>

namespace AestraUI {

NotificationToast::NotificationToast()
    : duration_(2.0), elapsed_(0.0)
{
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
    const float pillH = std::min(bounds.height, 28.0f);
    const NUIRect pill(bounds.x + (bounds.width - pillW) * 0.5f,
                       bounds.y + (bounds.height - pillH) * 0.5f, pillW, pillH);
    renderer.fillRoundedRect(pill, pillH * 0.5f, theme.getColor("surfaceTertiary").withAlpha(0.92f * alpha));
    renderer.strokeRoundedRect(pill, pillH * 0.5f, 1.0f, theme.getColor("accentPrimary").withAlpha(0.55f * alpha));
    renderer.drawTextCentered(text_, pill, kFontSize, theme.getColor("textPrimary").withAlpha(alpha));
}

void NotificationToast::onUpdate(double deltaTime)
{
    elapsed_ += deltaTime;
    if (elapsed_ >= duration_)
    {
        setVisible(false);
    }
    repaint(); // animate the fade while visible
}

void NotificationToast::setText(const std::string& text)
{
    text_ = text;
    repaint();
}

void NotificationToast::setDuration(double duration)
{
    duration_ = std::max(0.1, duration);
    elapsed_ = 0.0;
}

} // namespace AestraUI
