// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUICheckbox.h"
#include "NUIRenderer.h"
#include "NUITheme.h"
#include "NUIThemeSystem.h"
#include <algorithm>
#include <cmath>

namespace AestraUI {

NUICheckbox::NUICheckbox(const std::string& text)
    : NUIComponent()
    , text_(text)
{
    auto& theme = NUIThemeManager::getInstance();
    const auto& props = theme.getCurrentTheme();
    textColor_ = props.textPrimary;
    backgroundColor_ = props.buttonBgDefault;
    borderColor_ = props.borderSubtle;
    checkColor_ = props.primary;
    hoverColor_ = props.buttonBgHover;
    pressedColor_ = props.buttonBgActive;
    toggleThumbColor_ = props.textPrimary;
    toggleTrackColor_ = props.toggleDefault;
    toggleTrackCheckedColor_ = props.toggleActive;
    checkboxRadius_ = props.radiusXS;
    setSize(100, props.layout.minimumHitArea);
    
    // Create checkmark icon
    checkIcon_ = NUIIcon::createCheckIcon();
    checkIcon_->setIconSize(12.0f, 12.0f); // Smaller checkmark for checkbox
}

void NUICheckbox::onRender(NUIRenderer& renderer)
{
    if (!isVisible()) return;

    const NUIRect stableBounds = getBounds();

    // Draw the appropriate checkbox style
    switch (style_)
    {
        case Style::Checkbox:
            drawEnhancedCheckbox(renderer, stableBounds);
            break;
        case Style::Toggle:
            drawEnhancedToggle(renderer, stableBounds);
            break;
        case Style::Radio:
            drawEnhancedRadio(renderer, stableBounds);
            break;
    }

    // Draw text if present
    if (!text_.empty())
    {
        drawText(renderer);
    }

    if (isEnabled() && isFocused()) {
        const auto& props = NUIThemeManager::getInstance().getCurrentTheme();
        renderer.strokeRoundedRect(stableBounds, props.radiusS, 1.5f, props.focusRing);
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
        setFocused(true);
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
    auto theme = getTheme();
    float fontSize = theme ? theme->getFontSize("checkbox.label", 11.0f)
                           : NUIThemeManager::getInstance().getFontSize("s");

    NUISize textSize = renderer.measureText(text_, fontSize);

    // Calculate text position: to the right of the checkbox square, vertically centered
    float textX = bounds.x + checkboxSize_ + textMargin_;
    float textY = bounds.y + (bounds.height - textSize.height) * 0.5f;

    const NUIColor color = isEnabled() ? textColor_ : NUIThemeManager::getInstance().getColor("textDisabled");
    renderer.drawText(text_, NUIPoint(textX, textY), fontSize, color);
}

bool NUICheckbox::isPointOnCheckbox(const NUIPoint& point) const
{
    const NUIRect bounds = getBounds();
    const float visibleWidth = style_ == Style::Toggle ? checkboxSize_ * 2.0f : checkboxSize_;
    const float minimumHitArea = NUIThemeManager::getInstance().getLayoutDimension("minimumHitArea");
    const float hitWidth = std::max(visibleWidth, minimumHitArea);
    const float hitHeight = std::max(checkboxSize_, std::min(bounds.height, minimumHitArea));
    const NUIRect hitRect(bounds.x, bounds.y + (bounds.height - hitHeight) * 0.5f, hitWidth, hitHeight);
    return hitRect.contains(point);
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
    const auto& theme = NUIThemeManager::getInstance().getCurrentTheme();
    NUIRect checkboxRect(bounds.x, bounds.y + (bounds.height - checkboxSize_) * 0.5f,
                         checkboxSize_, checkboxSize_);

    NUIColor bgColor = backgroundColor_;
    NUIColor borderColor = borderColor_;

    if (state_ == State::Checked) {
        bgColor = checkColor_;
        borderColor = checkColor_;
    } else if (isPressed_) {
        bgColor = pressedColor_;
    } else if (isHovered_) {
        bgColor = hoverColor_;
    }
    if (!isEnabled()) {
        bgColor = bgColor.withAlpha(0.42f);
        borderColor = theme.borderSubtle.withAlpha(0.52f);
    }

    renderer.fillRoundedRect(checkboxRect, checkboxRadius_, bgColor);
    renderer.strokeRoundedRect(checkboxRect, checkboxRadius_, theme.layout.dividerWidth, borderColor);

    if (state_ == State::Checked) {
        drawGlowingCheckmark(renderer, checkboxRect);
    } else if (state_ == State::Indeterminate) {
        drawIndeterminate(renderer, checkboxRect);
    }
}

void NUICheckbox::drawEnhancedToggle(NUIRenderer& renderer, const NUIRect& bounds)
{
    const auto& theme = NUIThemeManager::getInstance().getCurrentTheme();
    const float toggleWidth = checkboxSize_ * 2.0f;
    const float toggleHeight = checkboxSize_ * 0.6f;
    const float toggleX = bounds.x;
    const float toggleY = bounds.y + (bounds.height - toggleHeight) * 0.5f;
    NUIRect toggleRect(toggleX, toggleY, toggleWidth, toggleHeight);

    NUIColor trackColor = (state_ == State::Checked) ? toggleTrackCheckedColor_ : toggleTrackColor_;
    if (state_ != State::Checked) {
        if (isPressed_) trackColor = pressedColor_;
        else if (isHovered_) trackColor = hoverColor_;
    }
    if (!isEnabled()) trackColor = trackColor.withAlpha(0.38f);

    renderer.fillRoundedRect(toggleRect, toggleHeight * 0.5f, trackColor);
    renderer.strokeRoundedRect(toggleRect, toggleHeight * 0.5f, theme.layout.dividerWidth,
                               isEnabled() ? theme.borderStrong : theme.borderSubtle);

    const float thumbSize = toggleHeight * 0.8f;
    const float thumbY = toggleY + (toggleHeight - thumbSize) * 0.5f;
    const float thumbX = toggleX + (state_ == State::Checked ? toggleWidth - thumbSize - 2.0f : 2.0f);
    const NUIPoint thumbCenter = NUIRect(thumbX, thumbY, thumbSize, thumbSize).center();
    renderer.fillCircle(thumbCenter, thumbSize * 0.5f,
                        isEnabled() ? toggleThumbColor_ : toggleThumbColor_.withAlpha(0.42f));
}

void NUICheckbox::drawEnhancedRadio(NUIRenderer& renderer, const NUIRect& bounds)
{
    const auto& theme = NUIThemeManager::getInstance().getCurrentTheme();
    const float radioY = bounds.y + (bounds.height - checkboxSize_) * 0.5f;
    const NUIPoint radioCenter(bounds.x + checkboxSize_ * 0.5f, radioY + checkboxSize_ * 0.5f);
    const float radioRadius = checkboxSize_ * 0.5f;

    NUIColor bgColor = backgroundColor_;
    NUIColor borderColor = borderColor_;
    if (isPressed_) bgColor = pressedColor_;
    else if (isHovered_) bgColor = hoverColor_;
    if (!isEnabled()) {
        bgColor = bgColor.withAlpha(0.42f);
        borderColor = theme.borderSubtle.withAlpha(0.52f);
    }

    renderer.fillCircle(radioCenter, radioRadius, bgColor);
    renderer.strokeCircle(radioCenter, radioRadius, theme.layout.dividerWidth, borderColor);

    if (state_ == State::Checked) {
        renderer.fillCircle(radioCenter, radioRadius * 0.42f,
                            isEnabled() ? checkColor_ : checkColor_.withAlpha(0.42f));
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
    checkIcon_->setColor(NUIThemeManager::getInstance().getColor(isEnabled() ? "textOnPrimary" : "textDisabled"));
    
    // Render the checkmark
    checkIcon_->onRender(renderer);
}

} // namespace AestraUI
