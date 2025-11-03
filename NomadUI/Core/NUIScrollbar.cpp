// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUIScrollbar.h"
#include "../Graphics/NUIRenderer.h"
#include <algorithm>
#include <cmath>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace NomadUI {

NUIScrollbar::NUIScrollbar(Orientation orientation)
    : NUIComponent()
    , orientation_(orientation)
{
    // Normal 16px width scrollbar
    setSize(orientation == Orientation::Vertical ? 16 : 200, 
            orientation == Orientation::Vertical ? 200 : 16);
    updateThumbSize();
    
    // Create SVG arrow icons based on orientation (Bug #11: Scrollbar Icons)
    // CRITICAL: Horizontal scrollbars need left/right arrows, vertical need up/down arrows
    if (orientation == Orientation::Vertical) {
        // Vertical scrollbar: up arrow (top), down arrow (bottom)
        const char* upArrowSvg = R"(
            <svg viewBox="0 0 24 24" fill="currentColor">
                <path d="M7 14l5-5 5 5z"/>
            </svg>
        )";
        upArrowIcon_ = std::make_shared<NUIIcon>(upArrowSvg);
        upArrowIcon_->setIconSize(NUIIconSize::Medium);  // 24px for better visibility
        upArrowIcon_->setColor(NUIColor(1.0f, 1.0f, 1.0f, 1.0f));  // Bright white
        
        const char* downArrowSvg = R"(
            <svg viewBox="0 0 24 24" fill="currentColor">
                <path d="M7 10l5 5 5-5z"/>
            </svg>
        )";
        downArrowIcon_ = std::make_shared<NUIIcon>(downArrowSvg);
        downArrowIcon_->setIconSize(NUIIconSize::Medium);  // 24px for better visibility
        downArrowIcon_->setColor(NUIColor(1.0f, 1.0f, 1.0f, 1.0f));  // Bright white
    } else {
        // Horizontal scrollbar: left arrow (left side), right arrow (right side)
        const char* leftArrowSvg = R"(
            <svg viewBox="0 0 24 24" fill="currentColor">
                <path d="M14 7l-5 5 5 5z"/>
            </svg>
        )";
        upArrowIcon_ = std::make_shared<NUIIcon>(leftArrowSvg);  // upArrowIcon_ = left for horizontal
        upArrowIcon_->setIconSize(NUIIconSize::Medium);  // 24px for better visibility
        upArrowIcon_->setColor(NUIColor(1.0f, 1.0f, 1.0f, 1.0f));  // Bright white
        
        const char* rightArrowSvg = R"(
            <svg viewBox="0 0 24 24" fill="currentColor">
                <path d="M10 7l5 5-5 5z"/>
            </svg>
        )";
        downArrowIcon_ = std::make_shared<NUIIcon>(rightArrowSvg);  // downArrowIcon_ = right for horizontal
        downArrowIcon_->setIconSize(NUIIconSize::Medium);  // 24px for better visibility
        downArrowIcon_->setColor(NUIColor(1.0f, 1.0f, 1.0f, 1.0f));  // Bright white
    }
}

void NUIScrollbar::onRender(NUIRenderer& renderer)
{
    if (!isVisible() || isAutoHidden_) return;

    drawTrack(renderer);
    drawThumb(renderer);
    drawArrows(renderer);
}

bool NUIScrollbar::onMouseEvent(const NUIMouseEvent& event)
{
    if (!isVisible() || isAutoHidden_) return false;

    NUIRect bounds = getBounds();
    
    // If we're dragging, we need to handle mouse events even outside bounds
    // If not dragging and mouse is outside bounds, ignore the event
    if (!isDragging_ && !bounds.contains(event.position)) return false;
    
    std::cout << "Scrollbar received mouse event at (" << event.position.x << ", " << event.position.y << ")" << std::endl;

    Part part = getPartAtPosition(event.position);
    
    if (event.pressed && event.button == NUIMouseButton::Left)
    {
        std::cout << "Mouse pressed on part: " << static_cast<int>(part) << std::endl;
        isPressed_ = true;
        pressedPart_ = part;
        dragStartPosition_ = event.position;
        dragStartValue_ = currentRangeStart_;

        switch (part)
        {
            case Part::Thumb:
                isDragging_ = true;
                std::cout << "Started dragging thumb" << std::endl;
                break;
            case Part::LeftArrow:
                scrollByLine(-1.0);
                std::cout << "Left arrow clicked, scrolled by line" << std::endl;
                break;
            case Part::RightArrow:
                scrollByLine(1.0);
                std::cout << "Right arrow clicked, scrolled by line" << std::endl;
                break;
            case Part::LeftTrack:
                scrollByPage(-1.0);
                break;
            case Part::RightTrack:
                scrollByPage(1.0);
                break;
            default:
                break;
        }

        triggerScrollStart();
        setDirty(true);
        return true;
    }
    else if (event.released && event.button == NUIMouseButton::Left && isPressed_)
    {
        isPressed_ = false;
        isDragging_ = false;
        pressedPart_ = Part::None;
        triggerScrollEnd();
        setDirty(true);
        return true;
    }
    else if (isDragging_ && event.button == NUIMouseButton::None)
    {
        // Handle thumb dragging (mouse move events have button = None)
        // Use relative drag delta to preserve initial grab offset within thumb
        NUIRect trackRect = getTrackRect();
        double deltaPixels, trackLength;
        
        if (orientation_ == Orientation::Vertical)
        {
            deltaPixels = event.position.y - dragStartPosition_.y;
            trackLength = trackRect.height;
        }
        else
        {
            deltaPixels = event.position.x - dragStartPosition_.x;
            trackLength = trackRect.width;
        }
        
        // Convert pixel delta to value delta
        double proportion = deltaPixels / trackLength;
        double valueDelta = proportion * rangeLimitSize_;
        double newValue = dragStartValue_ + valueDelta;
        
        std::cout << "Dragging: delta=" << deltaPixels << "px, newValue=" << newValue << std::endl;
        scrollTo(newValue);
        return true;
    }

    return false;
}

void NUIScrollbar::onMouseEnter()
{
    isHovered_ = true;
    if (autoHide_)
    {
        isAutoHidden_ = false;
        stopAutoHideTimer();
    }
    setDirty(true);
}

void NUIScrollbar::onMouseLeave()
{
    isHovered_ = false;
    
    // If we're dragging and mouse leaves, we need to continue tracking
    // Don't stop dragging just because mouse left the component
    if (!isDragging_)
    {
        if (autoHide_)
        {
            startAutoHideTimer();
        }
    }
    
    setDirty(true);
}

void NUIScrollbar::setCurrentRange(double start, double size)
{
    currentRangeStart_ = std::clamp(start, rangeLimitStart_, rangeLimitStart_ + rangeLimitSize_ - size);
    currentRangeSize_ = std::clamp(size, 0.0, rangeLimitSize_);
    updateThumbSize();
    updateThumbPosition();
    setDirty(true);
}

void NUIScrollbar::setRangeLimit(double start, double size)
{
    rangeLimitStart_ = start;
    rangeLimitSize_ = std::max(size, 0.0);
    updateThumbSize();
    setDirty(true);
}

void NUIScrollbar::setSingleStepSize(double step)
{
    singleStepSize_ = std::max(step, 0.0);
}

void NUIScrollbar::setPageStepSize(double step)
{
    pageStepSize_ = std::max(step, 0.0);
}

void NUIScrollbar::setAutoHide(bool autoHide)
{
    autoHide_ = autoHide;
    if (!autoHide_)
    {
        isAutoHidden_ = false;
        stopAutoHideTimer();
    }
    setDirty(true);
}

void NUIScrollbar::setAutoHideDelay(double delay)
{
    autoHideDelay_ = std::max(delay, 0.0);
}

void NUIScrollbar::setOrientation(Orientation orientation)
{
    orientation_ = orientation;
    updateThumbSize();
    setDirty(true);
}

void NUIScrollbar::setThumbSize(double size)
{
    thumbSize_ = std::clamp(size, minimumThumbSize_, rangeLimitSize_);
    updateThumbPosition();
    setDirty(true);
}

void NUIScrollbar::setMinimumThumbSize(double size)
{
    minimumThumbSize_ = std::max(size, 0.0);
    updateThumbSize();
    setDirty(true);
}

void NUIScrollbar::setTrackColor(const NUIColor& color)
{
    trackColor_ = color;
    setDirty(true);
}

void NUIScrollbar::setThumbColor(const NUIColor& color)
{
    thumbColor_ = color;
    setDirty(true);
}

void NUIScrollbar::setThumbHoverColor(const NUIColor& color)
{
    thumbHoverColor_ = color;
    setDirty(true);
}

void NUIScrollbar::setThumbPressedColor(const NUIColor& color)
{
    thumbPressedColor_ = color;
    setDirty(true);
}

void NUIScrollbar::setArrowColor(const NUIColor& color)
{
    arrowColor_ = color;
    setDirty(true);
}

void NUIScrollbar::setArrowHoverColor(const NUIColor& color)
{
    arrowHoverColor_ = color;
    setDirty(true);
}

void NUIScrollbar::setArrowPressedColor(const NUIColor& color)
{
    arrowPressedColor_ = color;
    setDirty(true);
}

void NUIScrollbar::setBorderColor(const NUIColor& color)
{
    borderColor_ = color;
    setDirty(true);
}

void NUIScrollbar::setBorderWidth(float width)
{
    borderWidth_ = width;
    setDirty(true);
}

void NUIScrollbar::setBorderRadius(float radius)
{
    borderRadius_ = radius;
    setDirty(true);
}

void NUIScrollbar::setArrowSize(float size)
{
    arrowSize_ = size;
    setDirty(true);
}

void NUIScrollbar::scrollBy(double delta)
{
    double newStart = currentRangeStart_ + delta;
    setCurrentRange(newStart, currentRangeSize_);
    triggerScroll();
}

void NUIScrollbar::scrollTo(double position)
{
    setCurrentRange(position, currentRangeSize_);
    triggerScroll();
}

void NUIScrollbar::scrollToStart()
{
    scrollTo(rangeLimitStart_);
}

void NUIScrollbar::scrollToEnd()
{
    scrollTo(rangeLimitStart_ + rangeLimitSize_ - currentRangeSize_);
}

void NUIScrollbar::scrollByPage(double direction)
{
    scrollBy(direction * pageStepSize_);
}

void NUIScrollbar::scrollByLine(double direction)
{
    scrollBy(direction * singleStepSize_);
}

double NUIScrollbar::getCurrentPosition() const
{
    return currentRangeStart_;
}

double NUIScrollbar::getMaximumPosition() const
{
    return rangeLimitStart_ + rangeLimitSize_ - currentRangeSize_;
}

double NUIScrollbar::getThumbPosition() const
{
    if (rangeLimitSize_ <= 0.0) return 0.0;
    return (currentRangeStart_ - rangeLimitStart_) / rangeLimitSize_;
}

double NUIScrollbar::getThumbLength() const
{
    if (rangeLimitSize_ <= 0.0) return 1.0;
    return currentRangeSize_ / rangeLimitSize_;
}

bool NUIScrollbar::isAtStart() const
{
    return currentRangeStart_ <= rangeLimitStart_;
}

bool NUIScrollbar::isAtEnd() const
{
    return currentRangeStart_ >= rangeLimitStart_ + rangeLimitSize_ - currentRangeSize_;
}

void NUIScrollbar::setOnScroll(std::function<void(double)> callback)
{
    onScrollCallback_ = callback;
}

void NUIScrollbar::setOnScrollStart(std::function<void()> callback)
{
    onScrollStartCallback_ = callback;
}

void NUIScrollbar::setOnScrollEnd(std::function<void()> callback)
{
    onScrollEndCallback_ = callback;
}

void NUIScrollbar::drawTrack(NUIRenderer& renderer)
{
    NUIRect trackRect = getTrackRect();
    
    // Enhanced track with gradient and subtle appearance
    drawEnhancedTrack(renderer, trackRect);
}

void NUIScrollbar::drawThumb(NUIRenderer& renderer)
{
    if (currentRangeSize_ >= rangeLimitSize_) return; // No thumb needed if content fits
    
    NUIRect thumbRect = getThumbRect();
    
    // Enhanced thumb with translucent effect and smooth animations
    drawEnhancedThumb(renderer, thumbRect);
}

void NUIScrollbar::drawArrows(NUIRenderer& renderer)
{
    drawLeftArrow(renderer);
    drawRightArrow(renderer);
}

void NUIScrollbar::drawLeftArrow(NUIRenderer& renderer)
{
    NUIRect arrowRect = getLeftArrowRect();
    
    // Choose arrow color based on state
    NUIColor arrowColor = arrowColor_;
    if (isPressed_ && pressedPart_ == Part::LeftArrow)
    {
        arrowColor = arrowPressedColor_;
    }
    else if (isHovered_)
    {
        arrowColor = arrowHoverColor_;
    }
    
    // Draw arrow background
    renderer.fillRoundedRect(arrowRect, borderRadius_, arrowColor.withAlpha(0.3f));
    
    // Draw SVG arrow icon instead of geometric shapes (Bug #11: Scrollbar Icons)
    if (upArrowIcon_) {
        upArrowIcon_->setColor(arrowColor);
        upArrowIcon_->setBounds(arrowRect);
        upArrowIcon_->onRender(renderer);
    }
}

void NUIScrollbar::drawRightArrow(NUIRenderer& renderer)
{
    NUIRect arrowRect = getRightArrowRect();
    
    // Choose arrow color based on state
    NUIColor arrowColor = arrowColor_;
    if (isPressed_ && pressedPart_ == Part::RightArrow)
    {
        arrowColor = arrowPressedColor_;
    }
    else if (isHovered_)
    {
        arrowColor = arrowHoverColor_;
    }
    
    // Draw arrow background
    renderer.fillRoundedRect(arrowRect, borderRadius_, arrowColor.withAlpha(0.3f));
    
    // Draw SVG arrow icon instead of geometric shapes (Bug #11: Scrollbar Icons)
    if (downArrowIcon_) {
        downArrowIcon_->setColor(arrowColor);
        downArrowIcon_->setBounds(arrowRect);
        downArrowIcon_->onRender(renderer);
    }
}

NUIScrollbar::Part NUIScrollbar::getPartAtPosition(const NUIPoint& position) const
{
    NUIRect bounds = getBounds();
    
    if (orientation_ == Orientation::Vertical)
    {
        float y = position.y - bounds.y;
        float height = bounds.height;
        
        if (y < arrowSize_)
            return Part::LeftArrow;
        if (y > height - arrowSize_)
            return Part::RightArrow;
        
        NUIRect thumbRect = getThumbRect();
        if (thumbRect.contains(position))
            return Part::Thumb;
        
        if (y < thumbRect.y)
            return Part::LeftTrack;
        if (y > thumbRect.y + thumbRect.height)
            return Part::RightTrack;
    }
    else
    {
        float x = position.x - bounds.x;
        float width = bounds.width;
        
        if (x < arrowSize_)
            return Part::LeftArrow;
        if (x > width - arrowSize_)
            return Part::RightArrow;
        
        NUIRect thumbRect = getThumbRect();
        if (thumbRect.contains(position))
            return Part::Thumb;
        
        if (x < thumbRect.x)
            return Part::LeftTrack;
        if (x > thumbRect.x + thumbRect.width)
            return Part::RightTrack;
    }
    
    return Part::Track;
}

NUIRect NUIScrollbar::getThumbRect() const
{
    NUIRect bounds = getBounds();
    double thumbPos = getThumbPosition();
    double thumbLen = getThumbLength();
    
    if (orientation_ == Orientation::Vertical)
    {
        float trackHeight = bounds.height - arrowSize_ * 2;
        float thumbHeight = static_cast<float>(thumbLen * trackHeight);
        float thumbY = bounds.y + arrowSize_ + static_cast<float>(thumbPos * trackHeight);
        
        return NUIRect(bounds.x + 2, thumbY, bounds.width - 4, thumbHeight);
    }
    else
    {
        float trackWidth = bounds.width - arrowSize_ * 2;
        float thumbWidth = static_cast<float>(thumbLen * trackWidth);
        float thumbX = bounds.x + arrowSize_ + static_cast<float>(thumbPos * trackWidth);
        
        return NUIRect(thumbX, bounds.y + 2, thumbWidth, bounds.height - 4);
    }
}

NUIRect NUIScrollbar::getLeftArrowRect() const
{
    NUIRect bounds = getBounds();
    
    if (orientation_ == Orientation::Vertical)
    {
        return NUIRect(bounds.x, bounds.y, bounds.width, arrowSize_);
    }
    else
    {
        return NUIRect(bounds.x, bounds.y, arrowSize_, bounds.height);
    }
}

NUIRect NUIScrollbar::getRightArrowRect() const
{
    NUIRect bounds = getBounds();
    
    if (orientation_ == Orientation::Vertical)
    {
        return NUIRect(bounds.x, bounds.y + bounds.height - arrowSize_, bounds.width, arrowSize_);
    }
    else
    {
        return NUIRect(bounds.x + bounds.width - arrowSize_, bounds.y, arrowSize_, bounds.height);
    }
}

NUIRect NUIScrollbar::getTrackRect() const
{
    NUIRect bounds = getBounds();
    
    if (orientation_ == Orientation::Vertical)
    {
        return NUIRect(bounds.x, bounds.y + arrowSize_, bounds.width, bounds.height - arrowSize_ * 2);
    }
    else
    {
        return NUIRect(bounds.x + arrowSize_, bounds.y, bounds.width - arrowSize_ * 2, bounds.height);
    }
}

double NUIScrollbar::positionToValue(const NUIPoint& position) const
{
    NUIRect trackRect = getTrackRect();
    
    if (orientation_ == Orientation::Vertical)
    {
        float relativeY = position.y - trackRect.y;
        float trackHeight = trackRect.height;
        double proportion = static_cast<double>(relativeY) / trackHeight;
        return rangeLimitStart_ + proportion * rangeLimitSize_;
    }
    else
    {
        float relativeX = position.x - trackRect.x;
        float trackWidth = trackRect.width;
        double proportion = static_cast<double>(relativeX) / trackWidth;
        return rangeLimitStart_ + proportion * rangeLimitSize_;
    }
}

NUIPoint NUIScrollbar::valueToPosition(double value) const
{
    NUIRect trackRect = getTrackRect();
    double proportion = (value - rangeLimitStart_) / rangeLimitSize_;
    
    if (orientation_ == Orientation::Vertical)
    {
        float y = trackRect.y + static_cast<float>(proportion * trackRect.height);
        return NUIPoint(trackRect.x + trackRect.width * 0.5f, y);
    }
    else
    {
        float x = trackRect.x + static_cast<float>(proportion * trackRect.width);
        return NUIPoint(x, trackRect.y + trackRect.height * 0.5f);
    }
}

void NUIScrollbar::updateThumbSize()
{
    if (rangeLimitSize_ <= 0.0)
    {
        thumbSize_ = 0.0;
        return;
    }
    
    double proportion = currentRangeSize_ / rangeLimitSize_;
    thumbSize_ = std::max(proportion, minimumThumbSize_);
}

void NUIScrollbar::updateThumbPosition()
{
    // Thumb position is calculated dynamically in getThumbRect()
    setDirty(true);
}

void NUIScrollbar::startAutoHideTimer()
{
    autoHideTimer_ = autoHideDelay_;
}

void NUIScrollbar::stopAutoHideTimer()
{
    autoHideTimer_ = 0.0;
}

void NUIScrollbar::triggerScroll()
{
    if (onScrollCallback_)
    {
        onScrollCallback_(currentRangeStart_);
    }
}

void NUIScrollbar::triggerScrollStart()
{
    if (onScrollStartCallback_)
    {
        onScrollStartCallback_();
    }
}

void NUIScrollbar::triggerScrollEnd()
{
    if (onScrollEndCallback_)
    {
        onScrollEndCallback_();
    }
}

void NUIScrollbar::drawArrowIcon(NUIRenderer& renderer, const NUIRect& rect, float rotation, const NUIColor& color)
{
    // Draw a simple arrow using lines
    NUIPoint center = rect.center();
    float size = std::min(rect.width, rect.height) * 0.3f;
    
    // Arrow points (pointing right by default)
    NUIPoint p1(center.x - size * 0.5f, center.y - size * 0.3f);
    NUIPoint p2(center.x + size * 0.5f, center.y);
    NUIPoint p3(center.x - size * 0.5f, center.y + size * 0.3f);
    
    // Apply rotation
    if (rotation != 0.0f)
    {
        float radians = rotation * M_PI / 180.0f;
        float cos_r = std::cos(radians);
        float sin_r = std::sin(radians);
        
        // Rotate each point around the center
        auto rotatePoint = [&](const NUIPoint& p) -> NUIPoint {
            float dx = p.x - center.x;
            float dy = p.y - center.y;
            return NUIPoint(
                center.x + dx * cos_r - dy * sin_r,
                center.y + dx * sin_r + dy * cos_r
            );
        };
        
        p1 = rotatePoint(p1);
        p2 = rotatePoint(p2);
        p3 = rotatePoint(p3);
    }
    
    renderer.drawLine(p1, p2, 2.0f, color);
    renderer.drawLine(p2, p3, 2.0f, color);
}

void NUIScrollbar::drawEnhancedTrack(NUIRenderer& renderer, const NUIRect& trackRect)
{
    // Normal 16px track with subtle dark gradient
    NUIColor trackBase = NUIColor(0.15f, 0.15f, 0.18f, 1.0f); // Dark charcoal
    NUIColor trackTop = trackBase.lightened(0.05f);
    NUIColor trackBottom = trackBase.darkened(0.1f);
    
    // Draw gradient track background (16px wide)
    for (int i = 0; i < 4; ++i)
    {
        float factor = static_cast<float>(i) / 3.0f;
        NUIColor gradientColor = NUIColor::lerp(trackTop, trackBottom, factor);
        NUIRect gradientRect = trackRect;
        gradientRect.y += i * 0.5f;
        gradientRect.height -= i * 0.5f;
        renderer.fillRoundedRect(gradientRect, 8.0f, gradientColor); // 8px radius for 16px track
    }
    
    // Very subtle inner highlight
    NUIRect highlightRect = trackRect;
    highlightRect.x += 1.0f;
    highlightRect.y += 1.0f;
    highlightRect.width -= 2.0f;
    highlightRect.height = trackRect.height * 0.3f;
    renderer.fillRoundedRect(highlightRect, 7.0f, trackTop.withAlpha(0.2f));
}

void NUIScrollbar::drawEnhancedThumb(NUIRenderer& renderer, const NUIRect& thumbRect)
{
    // Normal 16px thumb with faded white gradient and white markers
    NUIColor thumbBase = NUIColor(0.8f, 0.8f, 0.8f, 0.8f); // Faded white gradient base
    NUIColor thumbTop = thumbBase.lightened(0.1f);
    NUIColor thumbBottom = thumbBase.darkened(0.1f);
    
    // Calculate hover scale (subtle)
    float scale = 1.0f;
    if (isPressed_ && pressedPart_ == Part::Thumb)
    {
        scale = 0.98f; // Very subtle press effect
    }
    else if (isHovered_)
    {
        scale = 1.02f; // Very subtle scale up on hover
    }
    
    // Apply scaling
    NUIRect scaledThumb = thumbRect;
    if (scale != 1.0f)
    {
        float scaleOffset = (1.0f - scale) * 0.5f;
        scaledThumb.x += thumbRect.width * scaleOffset;
        scaledThumb.y += thumbRect.height * scaleOffset;
        scaledThumb.width *= scale;
        scaledThumb.height *= scale;
    }
    
    // Faded white gradient thumb (16px wide)
    for (int i = 0; i < 4; ++i)
    {
        float factor = static_cast<float>(i) / 3.0f;
        NUIColor gradientColor = NUIColor::lerp(thumbTop, thumbBottom, factor);
        NUIRect gradientRect = scaledThumb;
        gradientRect.y += i * 0.5f;
        gradientRect.height -= i * 0.5f;
        renderer.fillRoundedRect(gradientRect, 8.0f, gradientColor); // 8px radius for 16px thumb
    }
    
    // White markers within the thumb (orientation-aware)
    if (orientation_ == Orientation::Vertical)
    {
        // Vertical scrollbar: horizontal white lines
        float markerHeight = 2.0f; // Thickness of horizontal lines
        float markerSpacing = 3.0f; // Gap between markers
        float totalMarkerHeight = (markerHeight * 2) + markerSpacing;
        
        // Center the markers vertically in the thumb
        float markerY = scaledThumb.y + (scaledThumb.height - totalMarkerHeight) * 0.5f;
        
        // Top horizontal white marker
        NUIRect topMarker(scaledThumb.x + 2.0f, markerY, scaledThumb.width - 4.0f, markerHeight);
        renderer.fillRoundedRect(topMarker, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.9f));
        
        // Bottom horizontal white marker
        NUIRect bottomMarker(scaledThumb.x + 2.0f, markerY + markerHeight + markerSpacing, 
                             scaledThumb.width - 4.0f, markerHeight);
        renderer.fillRoundedRect(bottomMarker, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.9f));
    }
    else
    {
        // Horizontal scrollbar: vertical white lines
        float markerWidth = 2.0f; // Thickness of vertical lines
        float markerSpacing = 3.0f; // Gap between markers
        float totalMarkerWidth = (markerWidth * 2) + markerSpacing;
        
        // Center the markers horizontally in the thumb
        float markerX = scaledThumb.x + (scaledThumb.width - totalMarkerWidth) * 0.5f;
        
        // Left vertical white marker
        NUIRect leftMarker(markerX, scaledThumb.y + 2.0f, markerWidth, scaledThumb.height - 4.0f);
        renderer.fillRoundedRect(leftMarker, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.9f));
        
        // Right vertical white marker
        NUIRect rightMarker(markerX + markerWidth + markerSpacing, scaledThumb.y + 2.0f, 
                            markerWidth, scaledThumb.height - 4.0f);
        renderer.fillRoundedRect(rightMarker, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.9f));
    }
    
    // Subtle inner highlight
    NUIRect highlightRect = scaledThumb;
    highlightRect.x += 1.0f;
    highlightRect.y += 1.0f;
    highlightRect.width -= 2.0f;
    highlightRect.height = scaledThumb.height * 0.4f;
    renderer.fillRoundedRect(highlightRect, 7.0f, thumbTop.withAlpha(0.3f));
    
    // Very subtle border
    renderer.strokeRoundedRect(scaledThumb, 8.0f, 1.0f, 
        thumbBase.lightened(0.1f).withAlpha(0.6f));
}

} // namespace NomadUI
