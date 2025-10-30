// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITypes.h"
#include "NUIIcon.h"
#include <functional>
#include <string>
#include <memory>

namespace NomadUI {

/**
 * NUICheckbox - A checkbox/toggle component
 * Supports both checkbox and toggle switch styles
 * Replaces juce::ToggleButton with NomadUI styling and theming
 */
class NUICheckbox : public NUIComponent
{
public:
    // Checkbox styles
    enum class Style
    {
        Checkbox,   // Traditional checkbox with checkmark
        Toggle,     // Toggle switch style
        Radio       // Radio button style (exclusive selection)
    };

    // Checkbox states
    enum class State
    {
        Unchecked,
        Checked,
        Indeterminate  // For tri-state checkboxes
    };

    NUICheckbox(const std::string& text = "");
    ~NUICheckbox() override = default;

    // Component interface
    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onMouseEnter() override;
    void onMouseLeave() override;

    // Checkbox properties
    void setText(const std::string& text);
    const std::string& getText() const { return text_; }

    void setStyle(Style style);
    Style getStyle() const { return style_; }

    void setState(State state);
    State getState() const { return state_; }

    void setChecked(bool checked);
    bool isChecked() const { return state_ == State::Checked; }

    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled_; }

    void setToggleable(bool toggleable);
    bool isToggleable() const { return toggleable_; }

    // Tri-state support
    void setTriState(bool triState);
    bool isTriState() const { return triState_; }

    void setIndeterminate(bool indeterminate);
    bool isIndeterminate() const { return state_ == State::Indeterminate; }

    // Visual properties
    void setCheckboxSize(float size);
    float getCheckboxSize() const { return checkboxSize_; }

    void setCheckboxRadius(float radius);
    float getCheckboxRadius() const { return checkboxRadius_; }

    void setTextColor(const NUIColor& color);
    NUIColor getTextColor() const { return textColor_; }

    void setBackgroundColor(const NUIColor& color);
    NUIColor getBackgroundColor() const { return backgroundColor_; }

    void setBorderColor(const NUIColor& color);
    NUIColor getBorderColor() const { return borderColor_; }

    void setCheckColor(const NUIColor& color);
    NUIColor getCheckColor() const { return checkColor_; }

    void setHoverColor(const NUIColor& color);
    NUIColor getHoverColor() const { return hoverColor_; }

    void setPressedColor(const NUIColor& color);
    NUIColor getPressedColor() const { return pressedColor_; }

    // Toggle switch specific properties
    void setToggleThumbColor(const NUIColor& color);
    NUIColor getToggleThumbColor() const { return toggleThumbColor_; }

    void setToggleTrackColor(const NUIColor& color);
    NUIColor getToggleTrackColor() const { return toggleTrackColor_; }

    void setToggleTrackCheckedColor(const NUIColor& color);
    NUIColor getToggleTrackCheckedColor() const { return toggleTrackCheckedColor_; }

    // Text properties
    void setTextAlignment(NUITextAlignment alignment);
    NUITextAlignment getTextAlignment() const { return textAlignment_; }

    void setTextMargin(float margin);
    float getTextMargin() const { return textMargin_; }

    // Event callbacks
    void setOnStateChange(std::function<void(State)> callback);
    void setOnCheckedChange(std::function<void(bool)> callback);
    void setOnClick(std::function<void()> callback);

    // Utility
    void toggle();
    void setNextState();

protected:
    // Override these for custom checkbox styles
    virtual void drawCheckbox(NUIRenderer& renderer);
    virtual void drawToggle(NUIRenderer& renderer);
    virtual void drawRadio(NUIRenderer& renderer);
    virtual void drawText(NUIRenderer& renderer);

    // Hit testing
    virtual bool isPointOnCheckbox(const NUIPoint& point) const;
    virtual bool isPointOnText(const NUIPoint& point) const;

    // Helper drawing methods
    virtual void drawCheckmark(NUIRenderer& renderer, const NUIRect& rect);
    virtual void drawIndeterminate(NUIRenderer& renderer, const NUIRect& rect);

private:
    void updateState();
    void triggerStateChange();
    void triggerCheckedChange();
    void triggerClick();
    
    // Enhanced drawing methods
    void drawEnhancedCheckbox(NUIRenderer& renderer, const NUIRect& bounds);
    void drawEnhancedToggle(NUIRenderer& renderer, const NUIRect& bounds);
    void drawEnhancedRadio(NUIRenderer& renderer, const NUIRect& bounds);
    void drawGlowingCheckmark(NUIRenderer& renderer, const NUIRect& rect);

    // Basic properties
    std::string text_;
    Style style_ = Style::Checkbox;
    State state_ = State::Unchecked;
    bool enabled_ = true;
    bool toggleable_ = true;
    bool triState_ = false;

    // Visual properties
    float checkboxSize_ = 16.0f;
    float checkboxRadius_ = 2.0f;
    NUIColor textColor_ = NUIColor::fromHex(0xffffffff);
    NUIColor backgroundColor_ = NUIColor::fromHex(0xff2a2d32);
    NUIColor borderColor_ = NUIColor::fromHex(0xff666666);
    NUIColor checkColor_ = NUIColor::fromHex(0xffa855f7);
    NUIColor hoverColor_ = NUIColor::fromHex(0xff3a3d42);
    NUIColor pressedColor_ = NUIColor::fromHex(0xff1a1d22);

    // Toggle switch colors
    NUIColor toggleThumbColor_ = NUIColor::fromHex(0xffffffff);
    NUIColor toggleTrackColor_ = NUIColor::fromHex(0xff2a2d32);
    NUIColor toggleTrackCheckedColor_ = NUIColor::fromHex(0xffa855f7);

    // Text properties
    NUITextAlignment textAlignment_ = NUITextAlignment::Left;
    float textMargin_ = 8.0f;

    // Interaction state
    bool isHovered_ = false;
    bool isPressed_ = false;

    // Callbacks
    std::function<void(State)> onStateChangeCallback_;
    std::function<void(bool)> onCheckedChangeCallback_;
    std::function<void()> onClickCallback_;
    
    // Icon for checkmark
    std::shared_ptr<NUIIcon> checkIcon_;
};

} // namespace NomadUI
