// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITypes.h"
#include <functional>
#include <string>

namespace NomadUI {

/**
 * NUIButton - A customizable button component
 * Replaces juce::TextButton with NomadUI styling and theming
 */
class NUIButton : public NUIComponent
{
public:
    // Button states
    enum class State
    {
        Normal,
        Hovered,
        Pressed,
        Disabled
    };

    // Button styles
    enum class Style
    {
        Primary,    // Main action button
        Secondary,  // Secondary action button
        Text,       // Text-only button
        Icon        // Icon-only button
    };

    NUIButton(const std::string& text = "");
    ~NUIButton() override = default;

    // Component interface
    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onMouseEnter() override;
    void onMouseLeave() override;
    void onMouseDown(int x, int y, int button);
    void onMouseUp(int x, int y, int button);

    // Button properties
    void setText(const std::string& text);
    const std::string& getText() const { return text_; }

    void setStyle(Style style);
    Style getStyle() const { return style_; }

    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled_; }

    void setToggleable(bool toggleable);
    bool isToggleable() const { return toggleable_; }

    void setToggled(bool toggled);
    bool isToggled() const { return toggled_; }

    // State access
    bool isHovered() const { return state_ == State::Hovered; }
    bool isPressed() const { return isPressed_; }

    // Event callbacks
    void setOnClick(std::function<void()> callback);
    void setOnToggle(std::function<void(bool)> callback);

    // Theme colors
    void setBackgroundColor(const NUIColor& color);
    void setTextColor(const NUIColor& color);
    void setHoverColor(const NUIColor& color);
    void setPressedColor(const NUIColor& color);

private:
    void updateState();
    void triggerClick();
    void triggerToggle();

    std::string text_;
    Style style_ = Style::Primary;
    State state_ = State::Normal;
    bool enabled_ = true;
    bool toggleable_ = false;
    bool toggled_ = false;
    bool isPressed_ = false;

    // Colors
    NUIColor backgroundColor_ = NUIColor::fromHex(0xff9933ff); // Purple theme
    NUIColor textColor_ = NUIColor::fromHex(0xffffffff);
    NUIColor hoverColor_ = NUIColor::fromHex(0xffaa44ff);
    NUIColor pressedColor_ = NUIColor::fromHex(0xff8822ee);

    // Callbacks
    std::function<void()> onClickCallback_;
    std::function<void(bool)> onToggleCallback_;
};

} // namespace NomadUI
