// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUISlider.h"
#include "NUIRenderer.h"
#include "NUITheme.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace AestraUI {

NUISlider::NUISlider(const std::string& name)
    : NUIComponent()
{
    setId(name);
    setSize(100, 6); // Modern 6px height for horizontal slider
}

void NUISlider::onRender(NUIRenderer& renderer)
{
    if (!isVisible()) return;

    // Draw the appropriate slider style
    switch (style_)
    {
        case Style::Linear:
            drawLinearSlider(renderer);
            break;
        case Style::Rotary:
            drawRotarySlider(renderer);
            break;
        case Style::TwoValue:
            drawTwoValueSlider(renderer);
            break;
        case Style::ThreeValue:
            drawThreeValueSlider(renderer);
            break;
    }

    // Draw text box if visible
    if (textBoxVisible_)
    {
        drawSliderText(renderer);
    }
}

bool NUISlider::onMouseEvent(const NUIMouseEvent& event)
{
    if (!isEnabled() || !isVisible()) return false;
    const bool isOverSlider = isPointOnSlider(event.position);

    if (event.doubleClick && event.pressed && event.button == NUIMouseButton::Left)
    {
        if (doubleClickReturnValueEnabled_)
        {
            setValue(doubleClickReturnValue_);
        }
        return true;
    }

    // Keep drag interaction alive even when cursor leaves bounds.
    if (isDragging_)
    {
        if (event.released && event.button == NUIMouseButton::Left)
        {
            isDragging_ = false;
            triggerDragEnd();
            setDirty(true);
            return true;
        }
        if (event.button == NUIMouseButton::None)
        {
            if (valueChangeMode_ != ValueChangeMode::Click)
            {
                updateValueFromMousePosition(event.position);
            }
            return true;
        }
    }

    // Not dragging yet: only start when pointer is in bounds.
    if (!isOverSlider) return false;

    if (event.pressed && event.button == NUIMouseButton::Left)
    {
        // Start dragging
        isDragging_ = true;
        lastMousePosition_ = event.position;
        valueWhenDragStarted_ = value_;
        
        // Click mode updates only on click, drag mode updates only while dragging.
        if (valueChangeMode_ == ValueChangeMode::Normal || valueChangeMode_ == ValueChangeMode::Click)
        {
            updateValueFromMousePosition(event.position);
        }
        
        triggerDragStart();
        setDirty(true);
        return true;
    }

    return false;
}

void NUISlider::onMouseEnter()
{
    isHovered_ = true;
    setDirty(true);
}

void NUISlider::onMouseLeave()
{
    isHovered_ = false;
    setDirty(true);
}

void NUISlider::setValue(double value)
{
    double newValue = std::clamp(value, minValue_, maxValue_);
    if (std::abs(newValue - value_) > 1e-9) // Avoid floating point precision issues
    {
        value_ = newValue;
        triggerValueChange();
        setDirty(true);
    }
}

void NUISlider::setRange(double minValue, double maxValue)
{
    minValue_ = minValue;
    maxValue_ = maxValue;
    
    // Clamp current value to new range
    setValue(value_);
}

void NUISlider::setDefaultValue(double defaultValue)
{
    defaultValue_ = defaultValue;
}

void NUISlider::setValueChangeMode(ValueChangeMode mode)
{
    valueChangeMode_ = mode;
}

void NUISlider::setOrientation(Orientation orientation)
{
    orientation_ = orientation;
    setDirty(true);
}

void NUISlider::setStyle(Style style)
{
    style_ = style;
    setDirty(true);
}

void NUISlider::setEnabled(bool enabled)
{
    enabled_ = enabled;
    NUIComponent::setEnabled(enabled);
    setDirty(true);
}

void NUISlider::setTextValueSuffix(const std::string& suffix)
{
    textValueSuffix_ = suffix;
    setDirty(true);
}

void NUISlider::setTextBoxVisible(bool visible)
{
    textBoxVisible_ = visible;
    setDirty(true);
}

void NUISlider::setTextBoxPosition(bool above, bool below)
{
    textBoxAbove_ = above;
    textBoxBelow_ = below;
    setDirty(true);
}

void NUISlider::setSliderThickness(float thickness)
{
    sliderThickness_ = thickness;
    setDirty(true);
}

void NUISlider::setSliderRadius(float radius)
{
    sliderRadius_ = radius;
    setDirty(true);
}

void NUISlider::setTrackColor(const NUIColor& color)
{
    trackColor_ = color;
    setDirty(true);
}

void NUISlider::setFillColor(const NUIColor& color)
{
    fillColor_ = color;
    setDirty(true);
}

void NUISlider::setThumbColor(const NUIColor& color)
{
    thumbColor_ = color;
    setDirty(true);
}

void NUISlider::setThumbHoverColor(const NUIColor& color)
{
    thumbHoverColor_ = color;
    setDirty(true);
}

void NUISlider::setSnapToMousePosition(bool snap)
{
    snapToMousePosition_ = snap;
}

void NUISlider::setSnapValue(double snapValue)
{
    snapValue_ = snapValue;
}

void NUISlider::setDoubleClickReturnValue(bool enabled, double valueToReturn)
{
    doubleClickReturnValueEnabled_ = enabled;
    doubleClickReturnValue_ = valueToReturn;
}

void NUISlider::setOnValueChange(std::function<void(double)> callback)
{
    onValueChangeCallback_ = callback;
}

void NUISlider::setOnDragStart(std::function<void()> callback)
{
    onDragStartCallback_ = callback;
}

void NUISlider::setOnDragEnd(std::function<void()> callback)
{
    onDragEndCallback_ = callback;
}

double NUISlider::valueToProportionOfLength(double value) const
{
    if (maxValue_ == minValue_) return 0.0;
    return (value - minValue_) / (maxValue_ - minValue_);
}

double NUISlider::proportionOfLengthToValue(double proportion) const
{
    return minValue_ + proportion * (maxValue_ - minValue_);
}

double NUISlider::snapValue(double value) const
{
    if (snapValue_ > 0.0)
    {
        return std::round(value / snapValue_) * snapValue_;
    }
    return value;
}

void NUISlider::drawLinearSlider(NUIRenderer& renderer)
{
    drawSliderTrack(renderer);
    drawSliderThumb(renderer);
}

void NUISlider::drawRotarySlider(NUIRenderer& renderer)
{
    drawSliderTrack(renderer);
    drawSliderThumb(renderer);
}

void NUISlider::drawTwoValueSlider(NUIRenderer& renderer)
{
    drawSliderTrack(renderer);
    drawSliderThumb(renderer);
}

void NUISlider::drawThreeValueSlider(NUIRenderer& renderer)
{
    drawSliderTrack(renderer);
    drawSliderThumb(renderer);
}

void NUISlider::drawSliderTrack(NUIRenderer& renderer)
{
    NUIRect bounds = getBounds();
    
    if (orientation_ == Orientation::Horizontal)
    {
        // Draw horizontal track
        float trackY = bounds.y + (bounds.height - sliderThickness_) * 0.5f;
        float inset = std::min(sliderRadius_, bounds.width * 0.5f);
        NUIRect trackRect(bounds.x + inset, trackY, std::max(0.0f, bounds.width - inset * 2.0f), sliderThickness_);
        
        // Enhanced track with gradient and glow
        drawEnhancedTrack(renderer, trackRect);
        
        // Draw filled portion with neon accent
        float fillWidth = trackRect.width * valueToProportionOfLength(value_);
        if (fillWidth > 0)
        {
            NUIRect fillRect(trackRect.x, trackY, fillWidth, sliderThickness_);
            drawActiveTrack(renderer, fillRect);
        }
    }
    else
    {
        // Draw vertical track
        float trackX = bounds.x + (bounds.width - sliderThickness_) * 0.5f;
        float inset = std::min(sliderRadius_, bounds.height * 0.5f);
        NUIRect trackRect(trackX, bounds.y + inset, sliderThickness_, std::max(0.0f, bounds.height - inset * 2.0f));
        
        // Enhanced track with gradient and glow
        drawEnhancedTrack(renderer, trackRect);
        
        // Draw filled portion with neon accent
        float fillHeight = trackRect.height * valueToProportionOfLength(value_);
        if (fillHeight > 0)
        {
            NUIRect fillRect(trackX, trackRect.bottom() - fillHeight, sliderThickness_, fillHeight);
            drawActiveTrack(renderer, fillRect);
        }
    }
}

void NUISlider::drawSliderThumb(NUIRenderer& renderer)
{
    NUIRect bounds = getBounds();
    NUIPoint thumbPos;
    
    if (orientation_ == Orientation::Horizontal)
    {
        float inset = std::min(sliderRadius_, bounds.width * 0.5f);
        float usableWidth = std::max(0.0f, bounds.width - inset * 2.0f);
        float thumbX = bounds.x + inset + usableWidth * valueToProportionOfLength(value_);
        float thumbY = bounds.y + bounds.height * 0.5f;
        thumbPos = {thumbX, thumbY};
    }
    else
    {
        float thumbX = bounds.x + bounds.width * 0.5f;
        float inset = std::min(sliderRadius_, bounds.height * 0.5f);
        float usableHeight = std::max(0.0f, bounds.height - inset * 2.0f);
        float thumbY = bounds.y + inset + usableHeight * (1.0f - valueToProportionOfLength(value_));
        thumbPos = {thumbX, thumbY};
    }
    
    // Enhanced thumb with glow, shadow, and hover scaling
    drawEnhancedThumb(renderer, thumbPos);
    
    // Draw numeric display while dragging
    if (isDragging_)
    {
        drawNumericDisplay(renderer, thumbPos);
    }
}

void NUISlider::drawSliderText(NUIRenderer& renderer)
{
    if (!textBoxVisible_) return;

    NUIRect bounds = getBounds();
    auto theme = getTheme();
    NUIColor textColor = theme ? theme->getText() : NUIColor::fromHex(0xffffffff);
    float fontSize = theme ? theme->getFontSize("slider.value", 11.0f) : 11.0f;

    // Format value
    auto s = std::to_string(value_);
    s.erase(s.find_last_not_of('0') + 1);
    if (s.back() == '.') s.pop_back();
    std::string valueText = s + textValueSuffix_;

    NUISize textSize = renderer.measureText(valueText, fontSize);

    // Centered horizontally, 4px above the bottom edge of the track area
    float x = bounds.x + (bounds.width - textSize.width) * 0.5f;
    float y = bounds.y + bounds.height - textSize.height - 4.0f;

    renderer.drawText(valueText, NUIPoint(x, y), fontSize, textColor);
}

bool NUISlider::isPointOnSlider(const NUIPoint& point) const
{
    return getBounds().contains(point);
}

bool NUISlider::isPointOnThumb(const NUIPoint& point) const
{
    NUIRect bounds = getBounds();
    NUIPoint thumbPos;
    
    if (orientation_ == Orientation::Horizontal)
    {
        float inset = std::min(sliderRadius_, bounds.width * 0.5f);
        float usableWidth = std::max(0.0f, bounds.width - inset * 2.0f);
        float thumbX = bounds.x + inset + usableWidth * valueToProportionOfLength(value_);
        float thumbY = bounds.y + bounds.height * 0.5f;
        thumbPos = {thumbX, thumbY};
    }
    else
    {
        float thumbX = bounds.x + bounds.width * 0.5f;
        float inset = std::min(sliderRadius_, bounds.height * 0.5f);
        float usableHeight = std::max(0.0f, bounds.height - inset * 2.0f);
        float thumbY = bounds.y + inset + usableHeight * (1.0f - valueToProportionOfLength(value_));
        thumbPos = {thumbX, thumbY};
    }
    
    float distance = std::sqrt(std::pow(point.x - thumbPos.x, 2) + std::pow(point.y - thumbPos.y, 2));
    return distance <= sliderRadius_;
}

double NUISlider::getValueFromMousePosition(const NUIPoint& point) const
{
    NUIRect bounds = getBounds();
    
    if (orientation_ == Orientation::Horizontal)
    {
        float inset = std::min(sliderRadius_, bounds.width * 0.5f);
        float usableWidth = std::max(1.0f, bounds.width - inset * 2.0f);
        float proportion = (point.x - (bounds.x + inset)) / usableWidth;
        proportion = std::clamp(proportion, 0.0f, 1.0f);
        return proportionOfLengthToValue(proportion);
    }
    else
    {
        float inset = std::min(sliderRadius_, bounds.height * 0.5f);
        float usableHeight = std::max(1.0f, bounds.height - inset * 2.0f);
        float proportion = 1.0f - (point.y - (bounds.y + inset)) / usableHeight;
        proportion = std::clamp(proportion, 0.0f, 1.0f);
        return proportionOfLengthToValue(proportion);
    }
}

void NUISlider::updateValueFromMousePosition(const NUIPoint& point)
{
    double newValue = getValueFromMousePosition(point);
    
    if (snapToMousePosition_)
    {
        newValue = snapValue(newValue);
    }
    
    setValue(newValue);
}

void NUISlider::updateThumbPosition()
{
    // This would be called when the slider is resized
    // to update the thumb position
    setDirty(true);
}

void NUISlider::triggerValueChange()
{
    if (onValueChangeCallback_)
    {
        onValueChangeCallback_(value_);
    }
}

void NUISlider::triggerDragStart()
{
    if (onDragStartCallback_)
    {
        onDragStartCallback_();
    }
}

void NUISlider::triggerDragEnd()
{
    if (onDragEndCallback_)
    {
        onDragEndCallback_();
    }
}

void NUISlider::drawEnhancedTrack(NUIRenderer& renderer, const NUIRect& trackRect)
{
    renderer.fillRoundedRect({trackRect.x, trackRect.y + 1.0f, trackRect.width, trackRect.height},
                             trackRect.height * 0.5f,
                             NUIColor(0, 0, 0, 0.20f));
    renderer.fillRoundedRect(trackRect, trackRect.height * 0.5f, trackColor_.darkened(0.08f));
    renderer.strokeRoundedRect(trackRect, trackRect.height * 0.5f, 1.0f, NUIColor::white().withAlpha(0.05f));
}

void NUISlider::drawActiveTrack(NUIRenderer& renderer, const NUIRect& fillRect)
{
    if (fillRect.width > 1.0f) {
        NUIColor activeColor = fillColor_.a > 0.0f ? fillColor_ : thumbColor_;
        renderer.fillRoundedRect(fillRect, fillRect.height * 0.5f, activeColor.withAlpha(0.92f));
        renderer.fillRoundedRect({fillRect.x, fillRect.y, fillRect.width, std::max(1.0f, fillRect.height * 0.42f)},
                                 fillRect.height * 0.5f,
                                 NUIColor::white().withAlpha(0.06f));
    }
}

void NUISlider::drawEnhancedThumb(NUIRenderer& renderer, const NUIPoint& thumbPos)
{
    float scale = isDragging_ ? 1.0f : (isHovered_ ? 1.1f : 1.0f);
    float radius = sliderRadius_ * scale;

    NUIPoint shadowPos = thumbPos;
    shadowPos.y += 2;
    renderer.fillCircle(shadowPos, radius + 1.0f, NUIColor(0, 0, 0, 0.26f));

    NUIColor thumbFill = isDragging_ ? thumbColor_.lightened(0.08f)
                                     : (isHovered_ ? thumbHoverColor_ : thumbColor_);
    renderer.fillCircle(thumbPos, radius, thumbFill);
    renderer.strokeCircle(thumbPos, radius, 1.0f, NUIColor::white().withAlpha(0.10f));
    renderer.fillCircle({thumbPos.x, thumbPos.y - radius * 0.18f}, std::max(1.0f, radius * 0.42f), NUIColor::white().withAlpha(0.07f));

    if (isDragging_) {
        renderer.fillCircle(thumbPos, 2.5f, NUIColor::white().withAlpha(0.22f));
    }
}

void NUISlider::drawNumericDisplay(NUIRenderer& renderer, const NUIPoint& thumbPos)
{
    // Mini numeric display above thumb
    std::string valueText = std::to_string(static_cast<int>(value_));
    
    // Background for text
    NUIRect textBg(thumbPos.x - 15, thumbPos.y - 25, 30, 15);
    renderer.fillRoundedRect(textBg, 3.0f, NUIColor(0, 0, 0, 0.8f));
    
    // Text (placeholder - would need proper font rendering)
    // renderer.drawTextCentered(valueText, textBg, 10.0f, NUIColor::white());
}

} // namespace AestraUI
