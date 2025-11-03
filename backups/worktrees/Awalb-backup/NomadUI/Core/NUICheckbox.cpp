// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUICheckbox.h"
#include "../Graphics/NUIRenderer.h"
#include <algorithm>
#include <cmath>

namespace NomadUI {

NUICheckbox::NUICheckbox(const std::string& text)
    : NUIComponent()
    , text_(text)
{
    setSize(100, 20); // Default size
    
    // Create checkmark icon
    checkIcon_ = NUIIcon::createCheckIcon();
    checkIcon_->setIconSize(12.0f, 12.0f); // Smaller checkmark for checkbox
}

void NUICheckbox::onRender(NUIRenderer& renderer)
{
    if (!isVisible()) return;

    // Calculate scale animation for hover and press effects
    float scale = 1.0f;
    if (isPressed_)
    {
        scale = 0.9f; // Scale down when pressed
    }
    else if (isHovered_)
    {
        scale = 1.05f; // Scale up on hover
    }
    
    // Apply scaling to bounds
    NUIRect originalBounds = getBounds();
    NUIRect scaledBounds = originalBounds;
    if (scale != 1.0f)
    {
        float scaleOffset = (1.0f - scale) * 0.5f;
        scaledBounds.x += originalBounds.width * scaleOffset;
        scaledBounds.y += originalBounds.height * scaleOffset;
        scaledBounds.width *= scale;
        scaledBounds.height *= scale;
    }

    // Draw the appropriate checkbox style
    switch (style_)
    {
        case Style::Checkbox:
            drawEnhancedCheckbox(renderer, scaledBounds);
            break;
        case Style::Toggle:
            drawEnhancedToggle(renderer, scaledBounds);
            break;
        case Style::Radio:
            drawEnhancedRadio(renderer, scaledBounds);
            break;
    }

    // Draw text if present
    if (!text_.empty())
    {
        drawText(renderer);
    }
}

bool NUICheckbox::onMouseEvent(const NUIMouseEvent& event)
{
    if (!isEnabled() || !isVisible()) return false;

    // Check if mouse is over the checkbox or text
    if (!isPointOnCheckbox(event.position) && !isPointOnText(event.position))
        return false;

    if (event.pressed && event.button == NUIMouseButton::Left)
    {
        isPressed_ = true;
        setDirty(true);
        return true;
    }
    else if (event.released && event.button == NUIMouseButton::Left && isPressed_)
    {
        isPressed_ = false;
        
        if (toggleable_)
        {
            setNextState();
        }
        
        triggerClick();
        setDirty(true);
        return true;
    }

    return false;
}

void NUICheckbox::onMouseEnter()
{
    isHovered_ = true;
    setDirty(true);
}

void NUICheckbox::onMouseLeave()
{
    isHovered_ = false;
    isPressed_ = false;
    setDirty(true);
}

void NUICheckbox::setText(const std::string& text)
{
    text_ = text;
    setDirty(true);
}

void NUICheckbox::setStyle(Style style)
{
    style_ = style;
    setDirty(true);
}

void NUICheckbox::setState(State state)
{
    if (state_ != state)
    {
        state_ = state;
        updateState();
        triggerStateChange();
        triggerCheckedChange();
        setDirty(true);
    }
}

void NUICheckbox::setChecked(bool checked)
{
    setState(checked ? State::Checked : State::Unchecked);
}

void NUICheckbox::setEnabled(bool enabled)
{
    enabled_ = enabled;
    NUIComponent::setEnabled(enabled);
    setDirty(true);
}

void NUICheckbox::setToggleable(bool toggleable)
{
    toggleable_ = toggleable;
}

void NUICheckbox::setTriState(bool triState)
{
    triState_ = triState;
}

void NUICheckbox::setIndeterminate(bool indeterminate)
{
    setState(indeterminate ? State::Indeterminate : State::Unchecked);
}

void NUICheckbox::setCheckboxSize(float size)
{
    checkboxSize_ = size;
    setDirty(true);
}

void NUICheckbox::setCheckboxRadius(float radius)
{
    checkboxRadius_ = radius;
    setDirty(true);
}

void NUICheckbox::setTextColor(const NUIColor& color)
{
    textColor_ = color;
    setDirty(true);
}

void NUICheckbox::setBackgroundColor(const NUIColor& color)
{
    backgroundColor_ = color;
    setDirty(true);
}

void NUICheckbox::setBorderColor(const NUIColor& color)
{
    borderColor_ = color;
    setDirty(true);
}

void NUICheckbox::setCheckColor(const NUIColor& color)
{
    checkColor_ = color;
    setDirty(true);
}

void NUICheckbox::setHoverColor(const NUIColor& color)
{
    hoverColor_ = color;
    setDirty(true);
}

void NUICheckbox::setPressedColor(const NUIColor& color)
{
    pressedColor_ = color;
    setDirty(true);
}

void NUICheckbox::setToggleThumbColor(const NUIColor& color)
{
    toggleThumbColor_ = color;
    setDirty(true);
}

void NUICheckbox::setToggleTrackColor(const NUIColor& color)
{
    toggleTrackColor_ = color;
    setDirty(true);
}

void NUICheckbox::setToggleTrackCheckedColor(const NUIColor& color)
{
    toggleTrackCheckedColor_ = color;
    setDirty(true);
}

void NUICheckbox::setTextAlignment(NUITextAlignment alignment)
{
    textAlignment_ = alignment;
    setDirty(true);
}

void NUICheckbox::setTextMargin(float margin)
{
    textMargin_ = margin;
    setDirty(true);
}

void NUICheckbox::setOnStateChange(std::function<void(State)> callback)
{
    onStateChangeCallback_ = callback;
}

void NUICheckbox::setOnCheckedChange(std::function<void(bool)> callback)
{
    onCheckedChangeCallback_ = callback;
}

void NUICheckbox::setOnClick(std::function<void()> callback)
{
    onClickCallback_ = callback;
}

void NUICheckbox::toggle()
{
    if (triState_)
    {
        setNextState();
    }
    else
    {
        setChecked(!isChecked());
    }
}

void NUICheckbox::setNextState()
{
    if (triState_)
    {
        switch (state_)
        {
            case State::Unchecked:
                setState(State::Checked);
                break;
            case State::Checked:
                setState(State::Indeterminate);
                break;
            case State::Indeterminate:
                setState(State::Unchecked);
                break;
        }
    }
    else
    {
        setChecked(!isChecked());
    }
}

void NUICheckbox::drawCheckbox(NUIRenderer& renderer)
{
    NUIRect bounds = getBounds();
    
    // Calculate checkbox position
    float checkboxX = bounds.x;
    float checkboxY = bounds.y + (bounds.height - checkboxSize_) * 0.5f;
    NUIRect checkboxRect(checkboxX, checkboxY, checkboxSize_, checkboxSize_);
    
    // Choose colors based on state
    NUIColor bgColor = backgroundColor_;
    NUIColor borderColor = borderColor_;
    
    // When checked, use accent color for background
    if (state_ == State::Checked)
    {
        bgColor = checkColor_; // Use accent color for checked background
        borderColor = checkColor_;
    }
    else if (isPressed_)
    {
        bgColor = pressedColor_;
    }
    else if (isHovered_)
    {
        bgColor = hoverColor_;
    }
    
    // Enhanced checkbox with shadow and gradient
    NUIRect shadowRect = checkboxRect;
    shadowRect.x += 1;
    shadowRect.y += 1;
    renderer.fillRoundedRect(shadowRect, checkboxRadius_, NUIColor(0, 0, 0, 0.2f));
    
    // Gradient background effect
    NUIColor topColor = bgColor.lightened(0.1f);
    NUIColor bottomColor = bgColor.darkened(0.05f);
    
    // Draw gradient background (simulated with multiple rectangles)
    for (int i = 0; i < 2; ++i)
    {
        float factor = static_cast<float>(i);
        NUIColor gradientColor = NUIColor::lerp(topColor, bottomColor, factor);
        NUIRect gradientRect = checkboxRect;
        gradientRect.y += i;
        gradientRect.height -= i;
        renderer.fillRoundedRect(gradientRect, checkboxRadius_, gradientColor);
    }
    
    // Enhanced border
    renderer.strokeRoundedRect(checkboxRect, checkboxRadius_, 1.5f, borderColor.lightened(0.2f));
    
    // Draw checkmark or indeterminate indicator
    if (state_ == State::Checked)
    {
        drawCheckmark(renderer, checkboxRect);
    }
    else if (state_ == State::Indeterminate)
    {
        drawIndeterminate(renderer, checkboxRect);
    }
}

void NUICheckbox::drawToggle(NUIRenderer& renderer)
{
    NUIRect bounds = getBounds();
    
    // Calculate toggle dimensions
    float toggleWidth = checkboxSize_ * 2.0f;
    float toggleHeight = checkboxSize_ * 0.6f;
    float toggleX = bounds.x;
    float toggleY = bounds.y + (bounds.height - toggleHeight) * 0.5f;
    
    NUIRect toggleRect(toggleX, toggleY, toggleWidth, toggleHeight);
    
    // Choose track color based on state
    NUIColor trackColor = (state_ == State::Checked) ? toggleTrackCheckedColor_ : toggleTrackColor_;
    
    // Enhanced toggle track with shadow and gradient
    NUIRect shadowRect = toggleRect;
    shadowRect.x += 1;
    shadowRect.y += 1;
    renderer.fillRoundedRect(shadowRect, toggleHeight * 0.5f, NUIColor(0, 0, 0, 0.2f));
    
    // Gradient track background
    NUIColor topColor = trackColor.lightened(0.1f);
    NUIColor bottomColor = trackColor.darkened(0.1f);
    
    for (int i = 0; i < 2; ++i)
    {
        float factor = static_cast<float>(i);
        NUIColor gradientColor = NUIColor::lerp(topColor, bottomColor, factor);
        NUIRect gradientRect = toggleRect;
        gradientRect.y += i;
        gradientRect.height -= i;
        renderer.fillRoundedRect(gradientRect, toggleHeight * 0.5f, gradientColor);
    }
    
    // Track border
    renderer.strokeRoundedRect(toggleRect, toggleHeight * 0.5f, 1.0f, trackColor.lightened(0.3f));
    
    // Calculate thumb position with smooth animation
    float thumbSize = toggleHeight * 0.8f;
    float thumbY = toggleY + (toggleHeight - thumbSize) * 0.5f;
    float thumbX = toggleX + (state_ == State::Checked ? toggleWidth - thumbSize - 2.0f : 2.0f);
    
    NUIRect thumbRect(thumbX, thumbY, thumbSize, thumbSize);
    NUIPoint thumbCenter = thumbRect.center();
    
    // Enhanced thumb with shadow and gradient
    NUIPoint shadowCenter = thumbCenter;
    shadowCenter.x += 1;
    shadowCenter.y += 1;
    renderer.fillCircle(shadowCenter, thumbSize * 0.5f, NUIColor(0, 0, 0, 0.3f));
    
    // Gradient thumb
    NUIColor thumbTopColor = toggleThumbColor_.lightened(0.2f);
    NUIColor thumbBottomColor = toggleThumbColor_.darkened(0.1f);
    renderer.fillCircle(thumbCenter, thumbSize * 0.5f, thumbTopColor);
    renderer.fillCircle(thumbCenter, thumbSize * 0.4f, thumbBottomColor);
    
    // Thumb border
    renderer.strokeCircle(thumbCenter, thumbSize * 0.5f, 1.0f, toggleThumbColor_.lightened(0.4f));
}

void NUICheckbox::drawRadio(NUIRenderer& renderer)
{
    NUIRect bounds = getBounds();
    
    // Calculate radio button position
    float radioX = bounds.x;
    float radioY = bounds.y + (bounds.height - checkboxSize_) * 0.5f;
    NUIPoint radioCenter(radioX + checkboxSize_ * 0.5f, radioY + checkboxSize_ * 0.5f);
    float radioRadius = checkboxSize_ * 0.5f;
    
    // Choose colors based on state
    NUIColor bgColor = backgroundColor_;
    NUIColor borderColor = borderColor_;
    
    if (isPressed_)
    {
        bgColor = pressedColor_;
    }
    else if (isHovered_)
    {
        bgColor = hoverColor_;
    }
    
    // Enhanced radio button with shadow and gradient
    NUIPoint shadowCenter = radioCenter;
    shadowCenter.x += 1;
    shadowCenter.y += 1;
    renderer.fillCircle(shadowCenter, radioRadius, NUIColor(0, 0, 0, 0.2f));
    
    // Gradient background
    NUIColor topColor = bgColor.lightened(0.1f);
    NUIColor bottomColor = bgColor.darkened(0.05f);
    renderer.fillCircle(radioCenter, radioRadius, topColor);
    renderer.fillCircle(radioCenter, radioRadius * 0.8f, bottomColor);
    
    // Enhanced border
    renderer.strokeCircle(radioCenter, radioRadius, 1.5f, borderColor.lightened(0.2f));
    
    // Draw radio button center if checked
    if (state_ == State::Checked)
    {
        float centerRadius = radioRadius * 0.4f;
        renderer.fillCircle(radioCenter, centerRadius, checkColor_);
    }
}

void NUICheckbox::drawText(NUIRenderer& renderer)
{
    if (text_.empty()) return;
    
    NUIRect bounds = getBounds();
    
    // Calculate text position
    float textX = bounds.x + checkboxSize_ + textMargin_;
    float textY = bounds.y + bounds.height * 0.5f;
    
    // TODO: Implement text rendering when NUIFont is available
    // For now, this is a placeholder
    // renderer.setColor(textColor_);
    // renderer.drawText(text_, textX, textY, textAlignment_);
}

bool NUICheckbox::isPointOnCheckbox(const NUIPoint& point) const
{
    NUIRect bounds = getBounds();
    NUIRect checkboxRect(bounds.x, bounds.y + (bounds.height - checkboxSize_) * 0.5f, 
                        checkboxSize_, checkboxSize_);
    return checkboxRect.contains(point);
}

bool NUICheckbox::isPointOnText(const NUIPoint& point) const
{
    if (text_.empty()) return false;
    
    NUIRect bounds = getBounds();
    float textX = bounds.x + checkboxSize_ + textMargin_;
    NUIRect textRect(textX, bounds.y, bounds.width - checkboxSize_ - textMargin_, bounds.height);
    return textRect.contains(point);
}

void NUICheckbox::updateState()
{
    // Update any internal state based on the current state
    // This could include animations, etc.
}

void NUICheckbox::triggerStateChange()
{
    if (onStateChangeCallback_)
    {
        onStateChangeCallback_(state_);
    }
}

void NUICheckbox::triggerCheckedChange()
{
    if (onCheckedChangeCallback_)
    {
        onCheckedChangeCallback_(isChecked());
    }
}

void NUICheckbox::triggerClick()
{
    if (onClickCallback_)
    {
        onClickCallback_();
    }
}

void NUICheckbox::drawCheckmark(NUIRenderer& renderer, const NUIRect& rect)
{
    // Use NUIIcon for crisp, scalable checkmark
    float centerX = rect.x + rect.width * 0.5f;
    float centerY = rect.y + rect.height * 0.5f;
    float iconSize = std::min(rect.width, rect.height) * 0.75f;
    
    // Position the checkmark icon
    checkIcon_->setIconSize(iconSize, iconSize);
    checkIcon_->setPosition(centerX - iconSize * 0.5f, centerY - iconSize * 0.5f);
    // Use white/primary color for checkmark to contrast with accent background
    checkIcon_->setColor(NUIColor(1.0f, 1.0f, 1.0f, 1.0f));
    
    // Render the checkmark
    checkIcon_->onRender(renderer);
}

void NUICheckbox::drawIndeterminate(NUIRenderer& renderer, const NUIRect& rect)
{
    // Draw a horizontal line for indeterminate state
    float centerX = rect.x + rect.width * 0.5f;
    float centerY = rect.y + rect.height * 0.5f;
    float lineWidth = rect.width * 0.6f;
    
    NUIPoint p1(centerX - lineWidth * 0.5f, centerY);
    NUIPoint p2(centerX + lineWidth * 0.5f, centerY);
    
    renderer.drawLine(p1, p2, 2.0f, checkColor_);
}

void NUICheckbox::drawEnhancedCheckbox(NUIRenderer& renderer, const NUIRect& bounds)
{
    // Calculate checkbox position
    float checkboxX = bounds.x;
    float checkboxY = bounds.y + (bounds.height - checkboxSize_) * 0.5f;
    NUIRect checkboxRect(checkboxX, checkboxY, checkboxSize_, checkboxSize_);
    
    // Choose colors based on state
    NUIColor bgColor = backgroundColor_;
    NUIColor borderColor = borderColor_;
    
    // When checked, use accent color for background
    if (state_ == State::Checked)
    {
        bgColor = checkColor_; // Use accent color for checked background
        borderColor = checkColor_;
    }
    else if (isPressed_)
    {
        bgColor = pressedColor_;
    }
    else if (isHovered_)
    {
        bgColor = hoverColor_;
    }
    
    // Pulse effect when checked
    if (state_ == State::Checked)
    {
        // Draw pulse rings with accent color
        for (int i = 3; i >= 1; --i)
        {
            NUIRect pulseRect = checkboxRect;
            pulseRect.x -= i * 2;
            pulseRect.y -= i * 2;
            pulseRect.width += i * 4;
            pulseRect.height += i * 4;
            renderer.strokeRoundedRect(pulseRect, checkboxRadius_ + i, 1.0f, 
                checkColor_.withAlpha(0.3f / i));
        }
    }
    
    // Enhanced checkbox with shadow and gradient
    NUIRect shadowRect = checkboxRect;
    shadowRect.x += 1;
    shadowRect.y += 1;
    renderer.fillRoundedRect(shadowRect, checkboxRadius_, NUIColor(0, 0, 0, 0.2f));
    
    // Gradient background effect
    NUIColor topColor = bgColor.lightened(0.1f);
    NUIColor bottomColor = bgColor.darkened(0.05f);
    
    // Draw gradient background (simulated with multiple rectangles)
    for (int i = 0; i < 2; ++i)
    {
        float factor = static_cast<float>(i);
        NUIColor gradientColor = NUIColor::lerp(topColor, bottomColor, factor);
        NUIRect gradientRect = checkboxRect;
        gradientRect.y += i;
        gradientRect.height -= i;
        renderer.fillRoundedRect(gradientRect, checkboxRadius_, gradientColor);
    }
    
    // Enhanced border
    renderer.strokeRoundedRect(checkboxRect, checkboxRadius_, 1.5f, borderColor.lightened(0.2f));
    
    // Draw checkmark or indeterminate indicator with glow
    if (state_ == State::Checked)
    {
        drawGlowingCheckmark(renderer, checkboxRect);
    }
    else if (state_ == State::Indeterminate)
    {
        drawIndeterminate(renderer, checkboxRect);
    }
}

void NUICheckbox::drawEnhancedToggle(NUIRenderer& renderer, const NUIRect& bounds)
{
    // Calculate toggle dimensions
    float toggleWidth = checkboxSize_ * 2.0f;
    float toggleHeight = checkboxSize_ * 0.6f;
    float toggleX = bounds.x;
    float toggleY = bounds.y + (bounds.height - toggleHeight) * 0.5f;
    
    NUIRect toggleRect(toggleX, toggleY, toggleWidth, toggleHeight);
    
    // Choose track color based on state
    NUIColor trackColor = (state_ == State::Checked) ? toggleTrackCheckedColor_ : toggleTrackColor_;
    
    // Enhanced toggle track with shadow and gradient
    NUIRect shadowRect = toggleRect;
    shadowRect.x += 1;
    shadowRect.y += 1;
    renderer.fillRoundedRect(shadowRect, toggleHeight * 0.5f, NUIColor(0, 0, 0, 0.2f));
    
    // Gradient track background
    NUIColor topColor = trackColor.lightened(0.1f);
    NUIColor bottomColor = trackColor.darkened(0.1f);
    
    for (int i = 0; i < 2; ++i)
    {
        float factor = static_cast<float>(i);
        NUIColor gradientColor = NUIColor::lerp(topColor, bottomColor, factor);
        NUIRect gradientRect = toggleRect;
        gradientRect.y += i;
        gradientRect.height -= i;
        renderer.fillRoundedRect(gradientRect, toggleHeight * 0.5f, gradientColor);
    }
    
    // Track border
    renderer.strokeRoundedRect(toggleRect, toggleHeight * 0.5f, 1.0f, trackColor.lightened(0.3f));
    
    // Calculate thumb position with smooth animation
    float thumbSize = toggleHeight * 0.8f;
    float thumbY = toggleY + (toggleHeight - thumbSize) * 0.5f;
    float thumbX = toggleX + (state_ == State::Checked ? toggleWidth - thumbSize - 2.0f : 2.0f);
    
    NUIRect thumbRect(thumbX, thumbY, thumbSize, thumbSize);
    NUIPoint thumbCenter = thumbRect.center();
    
    // Enhanced thumb with shadow and gradient
    NUIPoint shadowCenter = thumbCenter;
    shadowCenter.x += 1;
    shadowCenter.y += 1;
    renderer.fillCircle(shadowCenter, thumbSize * 0.5f, NUIColor(0, 0, 0, 0.3f));
    
    // Gradient thumb
    NUIColor thumbTopColor = toggleThumbColor_.lightened(0.2f);
    NUIColor thumbBottomColor = toggleThumbColor_.darkened(0.1f);
    renderer.fillCircle(thumbCenter, thumbSize * 0.5f, thumbTopColor);
    renderer.fillCircle(thumbCenter, thumbSize * 0.4f, thumbBottomColor);
    
    // Thumb border
    renderer.strokeCircle(thumbCenter, thumbSize * 0.5f, 1.0f, toggleThumbColor_.lightened(0.4f));
}

void NUICheckbox::drawEnhancedRadio(NUIRenderer& renderer, const NUIRect& bounds)
{
    // Calculate radio button position
    float radioX = bounds.x;
    float radioY = bounds.y + (bounds.height - checkboxSize_) * 0.5f;
    NUIPoint radioCenter(radioX + checkboxSize_ * 0.5f, radioY + checkboxSize_ * 0.5f);
    float radioRadius = checkboxSize_ * 0.5f;
    
    // Choose colors based on state
    NUIColor bgColor = backgroundColor_;
    NUIColor borderColor = borderColor_;
    
    if (isPressed_)
    {
        bgColor = pressedColor_;
    }
    else if (isHovered_)
    {
        bgColor = hoverColor_;
    }
    
    // Pulse effect when checked
    if (state_ == State::Checked)
    {
        // Draw pulse rings
        for (int i = 3; i >= 1; --i)
        {
            renderer.strokeCircle(radioCenter, radioRadius + i * 3, 1.0f, 
                checkColor_.withAlpha(0.3f / i));
        }
    }
    
    // Enhanced radio button with shadow and gradient
    NUIPoint shadowCenter = radioCenter;
    shadowCenter.x += 1;
    shadowCenter.y += 1;
    renderer.fillCircle(shadowCenter, radioRadius, NUIColor(0, 0, 0, 0.2f));
    
    // Gradient background
    NUIColor topColor = bgColor.lightened(0.1f);
    NUIColor bottomColor = bgColor.darkened(0.05f);
    renderer.fillCircle(radioCenter, radioRadius, topColor);
    renderer.fillCircle(radioCenter, radioRadius * 0.8f, bottomColor);
    
    // Enhanced border
    renderer.strokeCircle(radioCenter, radioRadius, 1.5f, borderColor.lightened(0.2f));
    
    // Draw radio button center if checked
    if (state_ == State::Checked)
    {
        float centerRadius = radioRadius * 0.4f;
        renderer.fillCircle(radioCenter, centerRadius, checkColor_);
        
        // Inner glow
        renderer.strokeCircle(radioCenter, centerRadius, 1.0f, checkColor_.lightened(0.5f));
    }
}

void NUICheckbox::drawGlowingCheckmark(NUIRenderer& renderer, const NUIRect& rect)
{
    // Use NUIIcon for crisp, scalable checkmark
    float centerX = rect.x + rect.width * 0.5f;
    float centerY = rect.y + rect.height * 0.5f;
    float iconSize = std::min(rect.width, rect.height) * 0.75f;
    
    // Position the checkmark icon
    checkIcon_->setIconSize(iconSize, iconSize);
    checkIcon_->setPosition(centerX - iconSize * 0.5f, centerY - iconSize * 0.5f);
    // Use white/primary color for checkmark to contrast with accent background
    checkIcon_->setColor(NUIColor(1.0f, 1.0f, 1.0f, 1.0f));
    
    // Render the checkmark
    checkIcon_->onRender(renderer);
}

} // namespace NomadUI
