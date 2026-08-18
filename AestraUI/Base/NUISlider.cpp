// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUISlider.h"
#include "NUIRenderer.h"
#include "NUITheme.h"
#include "NUIThemeSystem.h"
#include "../Platform/NUIPlatformBridge.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

namespace AestraUI {

// Sensitivity constants for rotary knob drag
namespace {
    constexpr float COARSE_DRAG_SENSITIVITY = 0.005f;  // 0.5% per pixel
    constexpr float FINE_DRAG_SENSITIVITY = 0.0005f;   // 0.05% per pixel
    constexpr double TOOLTIP_FADE_DURATION = 0.4;      // 400ms fade-out
}

NUISlider::NUISlider(const std::string& name)
    : NUIComponent()
{
    setId(name);
    const auto& theme = NUIThemeManager::getInstance().getCurrentTheme();
    trackColor_ = theme.sliderTrack;
    fillColor_ = theme.sliderHandle;
    thumbColor_ = theme.textPrimary;
    thumbHoverColor_ = theme.sliderHandleHover;
    sliderRadius_ = theme.radiusM;
    setSize(100, 6); // Modern 6px height for horizontal slider
}

NUISlider::~NUISlider()
{
    // Torn down mid-drag: cancel the capture so the bridge never routes to a
    // dangling owner and the cursor is never stranded hidden.
    if (platformBridge_ && platformBridge_->isCursorCaptureOwner(this)) {
        platformBridge_->cancelCursorCapture();
    }
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

void NUISlider::onThemeChanged(const NUIThemeProperties& theme)
{
    if (!customColors_) {
        trackColor_ = theme.sliderTrack;
        fillColor_ = theme.sliderHandle;
        thumbColor_ = theme.textPrimary;
        thumbHoverColor_ = theme.sliderHandleHover;
        sliderRadius_ = theme.radiusM;
    }
    NUIComponent::onThemeChanged(theme);
}

bool NUISlider::onMouseEvent(const NUIMouseEvent& event)
{
    if (!isEnabled() || !isVisible()) return false;
    const bool isOverSlider = isPointOnSlider(event.position);
    const bool isRotary = (style_ == Style::Rotary);

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
            // Cleanup: restore cursor to knob center (current position) and show cursor
            if (platformBridge_)
            {
                if (isRotary) {
                    // Knob: end capture via the cursor service — warps to the
                    // knob center (cursor reappears where the knob is), then
                    // unhides, then releases confinement, in that order.
                    auto bounds = getBounds();
                    platformBridge_->endCursorCapture(
                        static_cast<int>(bounds.x + bounds.width * 0.5f),
                        static_cast<int>(bounds.y + bounds.height * 0.5f));
                } else {
                    // Linear: warp to thumb position (matches current value).
                    // Linear drags never hide the cursor today; adoption of the
                    // capture service here is a later migration phase.
                    auto b = getBounds();
                    if (orientation_ == Orientation::Horizontal) {
                        float inset = std::min(sliderRadius_, b.width * 0.5f);
                        float usableWidth = std::max(0.0f, b.width - inset * 2.0f);
                        platformBridge_->setCursorPosition(
                            static_cast<int>(b.x + inset + usableWidth * valueToProportionOfLength(value_)),
                            static_cast<int>(b.y + b.height * 0.5f));
                    } else {
                        float inset = std::min(sliderRadius_, b.height * 0.5f);
                        float usableHeight = std::max(0.0f, b.height - inset * 2.0f);
                        platformBridge_->setCursorPosition(
                            static_cast<int>(b.x + b.width * 0.5f),
                            static_cast<int>(b.y + inset + usableHeight * (1.0f - valueToProportionOfLength(value_))));
                    }
                    platformBridge_->setCursorStyle(NUICursorStyle::Arrow);
                }
            }

            isDragging_ = false;
            triggerDragEnd();
            setDirty(true);
            return true;
        }
        if (event.button == NUIMouseButton::None)
        {
            if (valueChangeMode_ != ValueChangeMode::Click)
            {
                // Rotary: frame-to-frame delta with hidden cursor
                if (isRotary && platformBridge_)
                {
                    // Check modifier state for fine-tuning (can change mid-drag)
                    isFineDrag_ = (event.modifiers & NUIModifiers::Shift) != 0;  // Shift = fine (unified across all knobs/sliders)

                    // Service-owned delta (recentered; no absolute-coord read).
                    float dy = event.delta.y;

                    float sensitivity = isFineDrag_ ? FINE_DRAG_SENSITIVITY : COARSE_DRAG_SENSITIVITY;
                    float delta = -dy * sensitivity * (maxValue_ - minValue_);

                    setValue(std::clamp(getValue() + delta, minValue_, maxValue_));
                }
                else
                {
                    // Linear or no platform bridge: use absolute position (existing behavior)
                    updateValueFromMousePosition(event.position);
                }
            }
            return true;
        }
    }

    // Not dragging yet: only start when pointer is in bounds.
    if (!isOverSlider) return false;

    if (event.pressed && event.button == NUIMouseButton::Left)
    {
        setFocused(true);
        // Start dragging
        isDragging_ = true;
        lastMousePosition_ = event.position;
        valueWhenDragStarted_ = value_;

        // Rotary-specific cursor capture setup
        if (isRotary && platformBridge_)
        {
            // Initialize drag origin and tracking
            dragOrigin_ = event.position;
            m_lastDragY = event.position.y;

            // Check for fine-tuning modifier at drag start
            isFineDrag_ = (event.modifiers & NUIModifiers::Shift) != 0;  // Shift = fine (unified across all knobs/sliders)

            // Begin cursor capture (this gives the "infinite travel" feel):
            // hides the cursor and confines the pointer to the window so the
            // release-warp is always valid (native Wayland warps silently
            // no-op once the hidden pointer drifts out and loses focus).
            platformBridge_->beginCursorCapture(
                this, NUICursorRestorePolicy::KnobCenter,
                static_cast<int>(event.position.x), static_cast<int>(event.position.y));
        }

        // Click mode updates only on click, drag mode updates only while dragging.
        if (valueChangeMode_ == ValueChangeMode::Normal || valueChangeMode_ == ValueChangeMode::Click)
        {
            // For rotary with cursor capture, initial value update is handled by delta logic
            if (!isRotary || !platformBridge_)
            {
                updateValueFromMousePosition(event.position);
            }
        }

        triggerDragStart();
        setDirty(true);
        return true;
    }

    return false;
}

void NUISlider::onMouseEnter()
{
    if (platformBridge_ && platformBridge_->getCursorStyle() == NUICursorStyle::Hidden) return;
    isHovered_ = true;
    // Hand/grab affordance: the control is draggable.
    if (platformBridge_) platformBridge_->setCursorStyle(NUICursorStyle::Grab);
    setDirty(true);
}

void NUISlider::onMouseLeave()
{
    if (platformBridge_ && platformBridge_->getCursorStyle() == NUICursorStyle::Hidden) return;
    isHovered_ = false;
    if (platformBridge_ && !isDragging_) platformBridge_->setCursorStyle(NUICursorStyle::Arrow);
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
    customColors_ = true;
    setDirty(true);
}

void NUISlider::setFillColor(const NUIColor& color)
{
    fillColor_ = color;
    customColors_ = true;
    setDirty(true);
}

void NUISlider::setThumbColor(const NUIColor& color)
{
    thumbColor_ = color;
    customColors_ = true;
    setDirty(true);
}

void NUISlider::setThumbHoverColor(const NUIColor& color)
{
    thumbHoverColor_ = color;
    customColors_ = true;
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

void NUISlider::setPlatformBridge(NUIPlatformBridge* bridge)
{
    platformBridge_ = bridge;
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

std::string NUISlider::formatValueForTooltip(double value) const
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1);
    oss << value;
    if (!textValueSuffix_.empty()) {
        oss << " " << textValueSuffix_;
    }
    return oss.str();
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

    // Render value tooltip during drag and fade-out
    if (style_ == Style::Rotary && (isDragging_ || dragEndTime_ > 0.0))
    {
        double currentTime = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();

        double opacity = 1.0;
        if (!isDragging_ && dragEndTime_ > 0.0)
        {
            double elapsed = currentTime - dragEndTime_;
            if (elapsed >= TOOLTIP_FADE_DURATION)
            {
                dragEndTime_ = 0.0; // Fade complete
                return;
            }
            opacity = 1.0 - (elapsed / TOOLTIP_FADE_DURATION);

            // Trigger repaint during fade for smooth animation
            setDirty(true);
        }

        // Calculate tooltip position (8px below knob center)
        NUIRect bounds = getBounds();
        float knobCenterX = bounds.x + bounds.width / 2.0f;
        float knobCenterY = bounds.y + bounds.height / 2.0f;
        float pillY = knobCenterY + std::min(bounds.width, bounds.height) / 2.0f + 8.0f;

        // Get formatted value
        std::string label = formatValueForTooltip(value_);

        // Calculate pill dimensions
        const auto& theme = NUIThemeManager::getInstance().getCurrentTheme();
        float pillPadding = theme.spacingXS + 2.0f;
        float pillHeight = theme.layout.compactControlHeight;
        float fontSize = theme.fontSizeXS;
        float pillWidth = renderer.measureText(label, fontSize).width + pillPadding * 2.0f;

        NUIRect pillRect(knobCenterX - pillWidth / 2.0f, pillY, pillWidth, pillHeight);

        // Draw pill background with opacity
        renderer.fillRoundedRect(pillRect, theme.radiusS,
                                 theme.surfaceTertiary.withAlpha(static_cast<float>(opacity)));
        renderer.strokeRoundedRect(pillRect, theme.radiusS, theme.layout.dividerWidth,
                                   theme.borderStrong.withAlpha(static_cast<float>(opacity)));

        // Draw text with opacity
        renderer.drawTextCentered(label, pillRect, fontSize,
                                  theme.textPrimary.withAlpha(static_cast<float>(opacity)));
    }
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
    auto& themeManager = NUIThemeManager::getInstance();
    NUIColor textColor = theme ? theme->getText()
                               : themeManager.getColor(isEnabled() ? "textPrimary" : "textDisabled");
    float fontSize = theme ? theme->getFontSize("slider.value", 11.0f) : themeManager.getFontSize("s");

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
    NUIRect hitBounds = getBounds();
    const float minimumHitArea = NUIThemeManager::getInstance().getLayoutDimension("minimumHitArea");
    if (orientation_ == Orientation::Horizontal && hitBounds.height < minimumHitArea) {
        const float expansion = (minimumHitArea - hitBounds.height) * 0.5f;
        hitBounds.y -= expansion;
        hitBounds.height = minimumHitArea;
    } else if (orientation_ == Orientation::Vertical && hitBounds.width < minimumHitArea) {
        const float expansion = (minimumHitArea - hitBounds.width) * 0.5f;
        hitBounds.x -= expansion;
        hitBounds.width = minimumHitArea;
    }
    return hitBounds.contains(point);
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
    // Record drag end time for tooltip fade-out
    dragEndTime_ = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();

    if (onDragEndCallback_)
    {
        onDragEndCallback_();
    }
}

void NUISlider::drawEnhancedTrack(NUIRenderer& renderer, const NUIRect& trackRect)
{
    auto& theme = NUIThemeManager::getInstance();
    const NUIColor track = isEnabled() ? trackColor_ : trackColor_.withAlpha(0.45f);
    renderer.fillRoundedRect(trackRect, trackRect.height * 0.5f, track);
    renderer.strokeRoundedRect(trackRect, trackRect.height * 0.5f, 1.0f,
                               theme.getColor("borderSubtle"));
}

void NUISlider::drawActiveTrack(NUIRenderer& renderer, const NUIRect& fillRect)
{
    if (fillRect.width > 1.0f) {
        NUIColor activeColor = fillColor_.a > 0.0f ? fillColor_ : thumbColor_;
        renderer.fillRoundedRect(fillRect, fillRect.height * 0.5f,
                                 activeColor.withAlpha(isEnabled() ? 0.92f : 0.35f));
    }
}

void NUISlider::drawEnhancedThumb(NUIRenderer& renderer, const NUIPoint& thumbPos)
{
    const float radius = sliderRadius_;

    NUIColor thumbFill = isDragging_ ? thumbColor_.lightened(0.08f)
                                     : (isHovered_ ? thumbHoverColor_ : thumbColor_);
    renderer.fillCircle(thumbPos, radius, isEnabled() ? thumbFill : thumbFill.withAlpha(0.38f));
    renderer.strokeCircle(thumbPos, radius, isFocused() && isEnabled() ? 1.5f : 1.0f,
                          isFocused() && isEnabled()
                              ? NUIThemeManager::getInstance().getColor("focusRing")
                              : NUIThemeManager::getInstance().getColor("borderStrong"));
}

void NUISlider::drawNumericDisplay(NUIRenderer& renderer, const NUIPoint& thumbPos)
{
    const auto& theme = NUIThemeManager::getInstance().getCurrentTheme();
    std::string valueText = std::to_string(static_cast<int>(value_));
    const float width = renderer.measureText(valueText, theme.fontSizeXS).width + theme.spacingS * 2.0f;
    NUIRect textBg(thumbPos.x - width * 0.5f, thumbPos.y - 27.0f, width, 18.0f);
    renderer.fillRoundedRect(textBg, theme.radiusS, theme.surfaceTertiary);
    renderer.strokeRoundedRect(textBg, theme.radiusS, theme.layout.dividerWidth, theme.borderStrong);
    renderer.drawTextCentered(valueText, textBg, theme.fontSizeXS, theme.textPrimary);
}

} // namespace AestraUI
