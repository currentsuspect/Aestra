// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"
#include "NUICoreWidgets.h"
#include <string>
#include <vector>

namespace NomadUI {

class SplashScreen : public NUIComponent {
public:
    SplashScreen();

    void onRender(NUIRenderer& renderer) override;
    void setMessage(const std::string& message);

private:
    std::string message_;
};

class LoadingSpinner : public NUIComponent {
public:
    LoadingSpinner();

    void onRender(NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;

    void setSpeed(double speed);

private:
    double angle_;
    double speed_;
};

class ThemeSelector : public NUIComponent {
public:
    ThemeSelector();

    void onRender(NUIRenderer& renderer) override;

    void setThemes(const std::vector<std::string>& themes);
    const std::vector<std::string>& getThemes() const { return themes_; }

private:
    std::vector<std::string> themes_;
};

class ReflectionPanel : public NUIComponent {
public:
    ReflectionPanel();

    void onRender(NUIRenderer& renderer) override;

    void setContent(const std::string& content);
    const std::string& getContent() const { return content_; }

private:
    std::string content_;
};

class StatusBar : public NUIComponent {
public:
    StatusBar();

    void onRender(NUIRenderer& renderer) override;

    void setLeftText(const std::string& text);
    void setRightText(const std::string& text);

    const std::string& getLeftText() const { return leftText_; }
    const std::string& getRightText() const { return rightText_; }

private:
    std::string leftText_;
    std::string rightText_;
};

} // namespace NomadUI

