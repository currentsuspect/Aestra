// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUIThematicWidgets.h"

#include "../Graphics/NUIRenderer.h"
#include <algorithm>

namespace NomadUI {

SplashScreen::SplashScreen() = default;

void SplashScreen::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void SplashScreen::setMessage(const std::string& message)
{
    message_ = message;
    repaint();
}

LoadingSpinner::LoadingSpinner()
    : angle_(0.0), speed_(180.0)
{
}

void LoadingSpinner::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void LoadingSpinner::onUpdate(double deltaTime)
{
    angle_ += speed_ * deltaTime;
    while (angle_ > 360.0)
        angle_ -= 360.0;
}

void LoadingSpinner::setSpeed(double speed)
{
    speed_ = std::max(0.0, speed);
}

ThemeSelector::ThemeSelector() = default;

void ThemeSelector::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void ThemeSelector::setThemes(const std::vector<std::string>& themes)
{
    themes_ = themes;
    repaint();
}

ReflectionPanel::ReflectionPanel() = default;

void ReflectionPanel::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void ReflectionPanel::setContent(const std::string& content)
{
    content_ = content;
    repaint();
}

StatusBar::StatusBar() = default;

void StatusBar::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void StatusBar::setLeftText(const std::string& text)
{
    leftText_ = text;
    repaint();
}

void StatusBar::setRightText(const std::string& text)
{
    rightText_ = text;
    repaint();
}

} // namespace NomadUI

