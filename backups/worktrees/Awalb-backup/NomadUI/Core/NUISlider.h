// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITypes.h"
#include <functional>
#include <string>

namespace NomadUI {

/**
 * NUISlider - A versatile slider component for DAW controls
 * Supports horizontal/vertical orientation, different styles (linear, rotary, etc.)
 * Replaces juce::Slider with NomadUI styling and theming
 */
class NUISlider : public NUIComponent
{
public:
    // Slider orientations
    enum class Orientation
    {
        Horizontal,
        Vertical
    };

    // Slider styles
    enum class Style
    {
        Linear,     // Standard linear slider
        Rotary,     // Circular knob
        TwoValue,   // Range slider with two handles
        ThreeValue  // Range slider with three handles (for EQ bands)
    };

    // Value change modes
    enum class ValueChangeMode
    {
        Normal,     // Direct value change
        Drag,       // Only change while dragging
        Click       // Only change on click
    };

    NUISlider(const std::string& name = "");
    ~NUISlider() override = default;

    // Component interface
    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onMouseEnter() override;
    void onMouseLeave() override;

    // Value properties
    void setValue(double value);
    double getValue() const { return value_; }

    void setRange(double minValue, double maxValue);
    double getMinValue() const { return minValue_; }
    double getMaxValue() const { return maxValue_; }

    void setDefaultValue(double defaultValue);
    double getDefaultValue() const { return defaultValue_; }

    void setValueChangeMode(ValueChangeMode mode);
    ValueChangeMode getValueChangeMode() const { return valueChangeMode_; }

    // Slider properties
    void setOrientation(Orientation orientation);
    Orientation getOrientation() const { return orientation_; }

    void setStyle(Style style);
    Style getStyle() const { return style_; }

    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled_; }

    // Text display
    void setTextValueSuffix(const std::string& suffix);
    const std::string& getTextValueSuffix() const { return textValueSuffix_; }

    void setTextBoxVisible(bool visible);
    bool isTextBoxVisible() const { return textBoxVisible_; }

    void setTextBoxPosition(bool above, bool below);
    bool isTextBoxAbove() const { return textBoxAbove_; }
    bool isTextBoxBelow() const { return textBoxBelow_; }

    // Visual properties
    void setSliderThickness(float thickness);
    float getSliderThickness() const { return sliderThickness_; }

    void setSliderRadius(float radius);
    float getSliderRadius() const { return sliderRadius_; }

    void setTrackColor(const NUIColor& color);
    NUIColor getTrackColor() const { return trackColor_; }

    void setFillColor(const NUIColor& color);
    NUIColor getFillColor() const { return fillColor_; }

    void setThumbColor(const NUIColor& color);
    NUIColor getThumbColor() const { return thumbColor_; }

    void setThumbHoverColor(const NUIColor& color);
    NUIColor getThumbHoverColor() const { return thumbHoverColor_; }

    // Snapping
    void setSnapToMousePosition(bool snap);
    bool isSnapToMousePosition() const { return snapToMousePosition_; }

    void setSnapValue(double snapValue);
    double getSnapValue() const { return snapValue_; }

    // Double-click behavior
    void setDoubleClickReturnValue(bool enabled, double valueToReturn);
    bool isDoubleClickReturnValue() const { return doubleClickReturnValue_; }
    double getDoubleClickReturnValue() const { return doubleClickReturnValue_; }

    // Event callbacks
    void setOnValueChange(std::function<void(double)> callback);
    void setOnDragStart(std::function<void()> callback);
    void setOnDragEnd(std::function<void()> callback);

    // Utility
    double valueToProportionOfLength(double value) const;
    double proportionOfLengthToValue(double proportion) const;
    double snapValue(double value) const;

protected:
    // Override these for custom slider styles
    virtual void drawLinearSlider(NUIRenderer& renderer);
    virtual void drawRotarySlider(NUIRenderer& renderer);
    virtual void drawTwoValueSlider(NUIRenderer& renderer);
    virtual void drawThreeValueSlider(NUIRenderer& renderer);

    virtual void drawSliderTrack(NUIRenderer& renderer);
    virtual void drawSliderThumb(NUIRenderer& renderer);
    virtual void drawSliderText(NUIRenderer& renderer);

    // Hit testing
    virtual bool isPointOnSlider(const NUIPoint& point) const;
    virtual bool isPointOnThumb(const NUIPoint& point) const;
    virtual double getValueFromMousePosition(const NUIPoint& point) const;

private:
    void updateValueFromMousePosition(const NUIPoint& point);
    void updateThumbPosition();
    void triggerValueChange();
    void triggerDragStart();
    void triggerDragEnd();
    
    // Enhanced drawing methods
    void drawEnhancedTrack(NUIRenderer& renderer, const NUIRect& trackRect);
    void drawActiveTrack(NUIRenderer& renderer, const NUIRect& fillRect);
    void drawEnhancedThumb(NUIRenderer& renderer, const NUIPoint& thumbPos);
    void drawNumericDisplay(NUIRenderer& renderer, const NUIPoint& thumbPos);

    // Value properties
    double value_ = 0.0;
    double minValue_ = 0.0;
    double maxValue_ = 1.0;
    double defaultValue_ = 0.0;
    double snapValue_ = 0.0;
    ValueChangeMode valueChangeMode_ = ValueChangeMode::Normal;

    // Slider properties
    Orientation orientation_ = Orientation::Horizontal;
    Style style_ = Style::Linear;
    bool enabled_ = true;

    // Text display
    std::string textValueSuffix_;
    bool textBoxVisible_ = true;
    bool textBoxAbove_ = false;
    bool textBoxBelow_ = false;

    // Visual properties
    float sliderThickness_ = 4.0f;
    float sliderRadius_ = 8.0f;
    NUIColor trackColor_ = NUIColor::fromHex(0xff2a2d32);
    NUIColor fillColor_ = NUIColor::fromHex(0xffa855f7);
    NUIColor thumbColor_ = NUIColor::fromHex(0xffffffff);
    NUIColor thumbHoverColor_ = NUIColor::fromHex(0xffe5e7eb);

    // Interaction state
    bool isDragging_ = false;
    bool isHovered_ = false;
    NUIPoint lastMousePosition_;
    double valueWhenDragStarted_ = 0.0;

    // Snapping
    bool snapToMousePosition_ = false;

    // Double-click behavior
    bool doubleClickReturnValueEnabled_ = false;
    double doubleClickReturnValue_ = 0.0;

    // Callbacks
    std::function<void(double)> onValueChangeCallback_;
    std::function<void()> onDragStartCallback_;
    std::function<void()> onDragEndCallback_;
};

} // namespace NomadUI
