// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUISlider.h"
#include "../Graphics/NUIRenderer.h"
#include <algorithm>
#include <cmath>

namespace NomadUI {

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

    // Check if mouse is over the slider
    if (!isPointOnSlider(event.position)) return false;

    if (event.pressed && event.button == NUIMouseButton::Left)
    {
        // Start dragging
        isDragging_ = true;
        lastMousePosition_ = event.position;
        valueWhenDragStarted_ = value_;
        
        // Update value if not in drag-only mode
        if (valueChangeMode_ != ValueChangeMode::Drag)
        {
            updateValueFromMousePosition(event.position);
        }
        
        triggerDragStart();
        setDirty(true);
        return true;
    }
    else if (event.released && event.button == NUIMouseButton::Left && isDragging_)
    {
        // Stop dragging
        isDragging_ = false;
        triggerDragEnd();
        setDirty(true);
        return true;
    }
    else if (isDragging_ && event.button == NUIMouseButton::None)
    {
        // Handle dragging (mouse move events have button = None)
        updateValueFromMousePosition(event.position);
        return true;
    }
    else if (event.pressed && event.button == NUIMouseButton::Left && event.doubleClick)
    {
        // Double-click to reset value
        if (doubleClickReturnValue_)
        {
            setValue(doubleClickReturnValue_);
        }
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
    doubleClickReturnValue_ = enabled;
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
    // TODO: Implement rotary slider rendering
    // This would draw a circular knob with tick marks
    drawSliderTrack(renderer);
    drawSliderThumb(renderer);
}

void NUISlider::drawTwoValueSlider(NUIRenderer& renderer)
{
    // TODO: Implement two-value slider rendering
    // This would draw a range slider with two handles
    drawSliderTrack(renderer);
    drawSliderThumb(renderer);
}

void NUISlider::drawThreeValueSlider(NUIRenderer& renderer)
{
    // TODO: Implement three-value slider rendering
    // This would draw a range slider with three handles (for EQ bands)
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
        NUIRect trackRect(bounds.x, trackY, bounds.width, sliderThickness_);
        
        // Enhanced track with gradient and glow
        drawEnhancedTrack(renderer, trackRect);
        
        // Draw filled portion with neon accent
        float fillWidth = bounds.width * valueToProportionOfLength(value_);
        if (fillWidth > 0)
        {
            NUIRect fillRect(bounds.x, trackY, fillWidth, sliderThickness_);
            drawActiveTrack(renderer, fillRect);
        }
    }
    else
    {
        // Draw vertical track
        float trackX = bounds.x + (bounds.width - sliderThickness_) * 0.5f;
        NUIRect trackRect(trackX, bounds.y, sliderThickness_, bounds.height);
        
        // Enhanced track with gradient and glow
        drawEnhancedTrack(renderer, trackRect);
        
        // Draw filled portion with neon accent
        float fillHeight = bounds.height * valueToProportionOfLength(value_);
        if (fillHeight > 0)
        {
            NUIRect fillRect(trackX, bounds.y + bounds.height - fillHeight, sliderThickness_, fillHeight);
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
        float thumbX = bounds.x + bounds.width * valueToProportionOfLength(value_);
        float thumbY = bounds.y + bounds.height * 0.5f;
        thumbPos = {thumbX, thumbY};
    }
    else
    {
        float thumbX = bounds.x + bounds.width * 0.5f;
        float thumbY = bounds.y + bounds.height * (1.0f - valueToProportionOfLength(value_));
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
    
    // TODO: Implement text rendering when NUIFont is available
    // For now, this is a placeholder
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
        float thumbX = bounds.x + bounds.width * valueToProportionOfLength(value_);
        float thumbY = bounds.y + bounds.height * 0.5f;
        thumbPos = {thumbX, thumbY};
    }
    else
    {
        float thumbX = bounds.x + bounds.width * 0.5f;
        float thumbY = bounds.y + bounds.height * (1.0f - valueToProportionOfLength(value_));
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
        float proportion = (point.x - bounds.x) / bounds.width;
        proportion = std::clamp(proportion, 0.0f, 1.0f);
        return proportionOfLengthToValue(proportion);
    }
    else
    {
        float proportion = 1.0f - (point.y - bounds.y) / bounds.height;
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
    // Track shadow
    NUIRect shadowRect = trackRect;
    shadowRect.x += 1;
    shadowRect.y += 1;
    renderer.fillRoundedRect(shadowRect, trackRect.height * 0.5f, NUIColor(0, 0, 0, 0.3f));
    
    // Gradient track background (dark to darker)
    NUIColor topColor = trackColor_.darkened(0.1f);
    NUIColor bottomColor = trackColor_.darkened(0.3f);
    
    for (int i = 0; i < 3; ++i)
    {
        float factor = static_cast<float>(i) / 2.0f;
        NUIColor gradientColor = NUIColor::lerp(topColor, bottomColor, factor);
        NUIRect gradientRect = trackRect;
        gradientRect.y += i;
        gradientRect.height -= i;
        renderer.fillRoundedRect(gradientRect, trackRect.height * 0.5f, gradientColor);
    }
    
    // Inner glow effect
    NUIRect glowRect = trackRect;
    glowRect.x += 1;
    glowRect.y += 1;
    glowRect.width -= 2;
    glowRect.height -= 2;
    renderer.strokeRoundedRect(glowRect, glowRect.height * 0.5f, 1.0f, trackColor_.lightened(0.2f).withAlpha(0.5f));
}

void NUISlider::drawActiveTrack(NUIRenderer& renderer, const NUIRect& fillRect)
{
    // Neon accent with glow
    NUIColor neonColor = NUIColor::fromHex(0xa855f7); // Purple neon
    NUIColor glowColor = neonColor.withAlpha(0.6f);
    
    // Glow effect (multiple layers)
    for (int i = 3; i >= 1; --i)
    {
        NUIRect glowRect = fillRect;
        glowRect.x -= i;
        glowRect.y -= i;
        glowRect.width += i * 2;
        glowRect.height += i * 2;
        renderer.fillRoundedRect(glowRect, glowRect.height * 0.5f, glowColor.withAlpha(0.1f * i));
    }
    
    // Main neon track
    renderer.fillRoundedRect(fillRect, fillRect.height * 0.5f, neonColor);
    
    // Bright highlight
    NUIRect highlightRect = fillRect;
    highlightRect.height = 2;
    renderer.fillRoundedRect(highlightRect, 1.0f, neonColor.lightened(0.3f));
}

void NUISlider::drawEnhancedThumb(NUIRenderer& renderer, const NUIPoint& thumbPos)
{
    // Calculate hover scale
    float scale = isHovered_ ? 1.1f : 1.0f;
    float radius = sliderRadius_ * scale;
    
    // Drop shadow
    NUIPoint shadowPos = thumbPos;
    shadowPos.x += 2;
    shadowPos.y += 2;
    renderer.fillCircle(shadowPos, radius, NUIColor(0, 0, 0, 0.4f));
    
    // Choose thumb color based on hover state
    NUIColor thumbColor = isHovered_ ? thumbHoverColor_ : thumbColor_;
    
    // Gradient thumb
    NUIColor topColor = thumbColor.lightened(0.2f);
    NUIColor bottomColor = thumbColor.darkened(0.1f);
    
    // Outer circle
    renderer.fillCircle(thumbPos, radius, topColor);
    
    // Inner circle
    renderer.fillCircle(thumbPos, radius * 0.8f, bottomColor);
    
    // 2px border
    renderer.strokeCircle(thumbPos, radius, 2.0f, thumbColor.lightened(0.4f));
    
    // Inner highlight
    renderer.strokeCircle(thumbPos, radius * 0.6f, 1.0f, thumbColor.lightened(0.6f));
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

} // namespace NomadUI
