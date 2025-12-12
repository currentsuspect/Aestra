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

    lastMousePosition_ = event.position;

    NUIRect bounds = getBounds();
    
    // If we're dragging, we need to handle mouse events even outside bounds
    // If not dragging and mouse is outside bounds, ignore the event
    if (!isDragging_ && !bounds.contains(event.position)) {
        // Reset hover when mouse leaves
        if (hoveredPart_ != Part::None) {
            hoveredPart_ = Part::None;
            setDirty(true);
        }
        return false;
    }
    
    // Track hover state for visual feedback (on any mouse move within bounds)
    if (!isDragging_) {
        Part newHoveredPart = getPartAtPosition(event.position);
        if (newHoveredPart != hoveredPart_) {
            hoveredPart_ = newHoveredPart;
            setDirty(true);
        }
    }
    
    // std::cout << "Scrollbar received mouse event at (" << event.position.x << ", " << event.position.y << ")" << std::endl;

    Part part = getPartAtPosition(event.position);
    
    if (event.pressed && event.button == NUIMouseButton::Left)
    {
        // std::cout << "Mouse pressed on part: " << static_cast<int>(part) << std::endl;
        isPressed_ = true;
        pressedPart_ = part;
        dragStartPosition_ = event.position;
        lastMousePosition_ = event.position; // Initialize for relative drag
        dragStartValue_ = currentRangeStart_;

        switch (part)
        {
            case Part::Thumb:
                isDragging_ = true;
                // std::cout << "Started dragging thumb" << std::endl;
                break;
            case Part::ThumbStartEdge:
            case Part::ThumbEndEdge:
                isDragging_ = true;
                resizeStartSize_ = currentRangeSize_;
                resizeStartValue_ = currentRangeStart_;
                // std::cout << "Started resizing thumb" << std::endl;
                break;
            case Part::LeftArrow:
                scrollByLine(-1.0);
                // std::cout << "Left arrow clicked, scrolled by line" << std::endl;
                break;
            case Part::RightArrow:
                scrollByLine(1.0);
                // std::cout << "Right arrow clicked, scrolled by line" << std::endl;
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
        NUIRect thumbRect = getThumbRect();
        double deltaPixels, trackLength, thumbLengthPixels;
        
        if (orientation_ == Orientation::Vertical)
        {
            deltaPixels = event.position.y - dragStartPosition_.y;
            trackLength = trackRect.height;
            thumbLengthPixels = thumbRect.height;
        }
        else
        {
            deltaPixels = event.position.x - dragStartPosition_.x;
            trackLength = trackRect.width;
            thumbLengthPixels = thumbRect.width;
        }
        
        // Guard against division by zero when trackLength is zero
        if (trackLength <= 0.0) {
            return false;
        }
        
        if (pressedPart_ == Part::Thumb) {
            // Calculate available space for movement
            double availableTrack = trackLength - thumbLengthPixels;
            double availableValue = rangeLimitSize_ - currentRangeSize_;
            
            if (availableTrack > 0.5 && availableValue > 0.0) {
                double valueDelta = (deltaPixels / availableTrack) * availableValue;
                double newValue = dragStartValue_ + valueDelta;
                scrollTo(newValue);
            }
        } else if (pressedPart_ == Part::ThumbStartEdge) {
            // Dragging left/top edge: changes start and size
            // Use relative movement (step delta) to handle dynamic range changes during drag
            bool isTimelineResize = (style_ == Style::Timeline);
            
            double stepDeltaPixels;
            if (orientation_ == Orientation::Vertical) stepDeltaPixels = event.position.y - lastMousePosition_.y;
            else stepDeltaPixels = event.position.x - lastMousePosition_.x;

            double valuePerPixel = rangeLimitSize_ / trackLength;
            double valueDelta = stepDeltaPixels * valuePerPixel;

            double newStart = currentRangeStart_ + valueDelta;
            double newSize = currentRangeSize_ - valueDelta;
            
            // Constraints
            if (newSize < 0.001) {
                newSize = 0.001;
                newStart = currentRangeStart_ + currentRangeSize_ - 0.001;
            }
            
            // Clamp start
            if (newStart < rangeLimitStart_) {
                double diff = rangeLimitStart_ - newStart;
                newStart = rangeLimitStart_;
                newSize -= diff;
            }
            
            currentRangeStart_ = newStart;
            currentRangeSize_ = newSize;
            updateThumbSize();
            updateThumbPosition();
            setDirty(true);
            if (onRangeChangeCallback_) onRangeChangeCallback_(currentRangeStart_, currentRangeSize_);
            
        } else if (pressedPart_ == Part::ThumbEndEdge) {
            // Dragging right/bottom edge: changes size only
            // Use relative movement (step delta)
            
            double stepDeltaPixels;
            if (orientation_ == Orientation::Vertical) stepDeltaPixels = event.position.y - lastMousePosition_.y;
            else stepDeltaPixels = event.position.x - lastMousePosition_.x;

            double valuePerPixel = rangeLimitSize_ / trackLength;
            double valueDelta = stepDeltaPixels * valuePerPixel;

            double newSize = currentRangeSize_ + valueDelta;
             if (newSize < 0.001) {
                newSize = 0.001;
            }
            
            // Clamp size so start + size <= limit
            if (currentRangeStart_ + newSize > rangeLimitStart_ + rangeLimitSize_) {
                newSize = rangeLimitStart_ + rangeLimitSize_ - currentRangeStart_;
            }
            
            currentRangeSize_ = newSize;
            updateThumbSize();
            updateThumbPosition();
            setDirty(true);
            if (onRangeChangeCallback_) onRangeChangeCallback_(currentRangeStart_, currentRangeSize_);
        }
        
        lastMousePosition_ = event.position;
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
    
    // Reset hover state when mouse leaves (unless dragging)
    if (!isDragging_) {
        hoveredPart_ = Part::None;
        
        if (autoHide_) {
            startAutoHideTimer();
        }
    }
    
    setDirty(true);
}

void NUIScrollbar::setCurrentRange(double start, double size)
{
    // Clamp size first to ensure valid range
    currentRangeSize_ = std::clamp(size, 0.0, rangeLimitSize_);
    
    // Calculate max start position, ensuring min <= max for clamp
    double maxStart = rangeLimitStart_ + rangeLimitSize_ - currentRangeSize_;
    maxStart = std::max(maxStart, rangeLimitStart_);  // Ensure max >= min
    
    currentRangeStart_ = std::clamp(start, rangeLimitStart_, maxStart);
    
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

void NUIScrollbar::setStyle(Style style)
{
    style_ = style;
    if (style_ == Style::Timeline) {
        arrowSize_ = 0.0f;
    } else {
        arrowSize_ = 12.0f;
    }
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

void NUIScrollbar::setOnRangeChange(std::function<void(double start, double size)> callback)
{
    onRangeChangeCallback_ = callback;
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
    
    if (style_ == Style::Timeline) {
        // Enhanced FL Studio style thumb with hover feedback on edges
        NUIColor thumbColor = thumbColor_;
        bool isThumbHovered = (hoveredPart_ == Part::Thumb);
        bool isStartEdgeHovered = (hoveredPart_ == Part::ThumbStartEdge);
        bool isEndEdgeHovered = (hoveredPart_ == Part::ThumbEndEdge);
        bool isDraggingThumb = (isDragging_ && pressedPart_ == Part::Thumb);
        bool isDraggingStart = (isDragging_ && pressedPart_ == Part::ThumbStartEdge);
        bool isDraggingEnd = (isDragging_ && pressedPart_ == Part::ThumbEndEdge);
        
        // Adjust thumb color based on overall state
        if (isDraggingThumb || isDraggingStart || isDraggingEnd) {
            thumbColor = thumbPressedColor_;
        } else if (isThumbHovered || isStartEdgeHovered || isEndEdgeHovered) {
            thumbColor = thumbHoverColor_;
        }
        
        // Main thumb body
        renderer.fillRoundedRect(thumbRect, 4.0f, thumbColor);
        
        // Draw enhanced edge handles with hover/active states
        float handleWidth = 12.0f;  // Match edge detection zone
        NUIColor handleBaseColor = thumbColor.lightened(0.3f);
        NUIColor handleHoverColor = NUIColor(0.73f, 0.52f, 0.99f, 1.0f);  // Purple accent
        NUIColor handleActiveColor = NUIColor(0.6f, 0.4f, 0.9f, 1.0f);   // Darker purple when dragging
        
        if (orientation_ == Orientation::Horizontal) {
            NUIRect leftHandle(thumbRect.x, thumbRect.y, handleWidth, thumbRect.height);
            NUIRect rightHandle(thumbRect.x + thumbRect.width - handleWidth, thumbRect.y, handleWidth, thumbRect.height);
            
            // Left/Start handle color
            NUIColor leftColor = handleBaseColor;
            if (isDraggingStart) {
                leftColor = handleActiveColor;
            } else if (isStartEdgeHovered) {
                leftColor = handleHoverColor;
                // Add subtle glow on hover
                renderer.drawGlow(leftHandle, 4.0f, 0.5f, handleHoverColor.withAlpha(0.4f));
            }
            
            // Right/End handle color
            NUIColor rightColor = handleBaseColor;
            if (isDraggingEnd) {
                rightColor = handleActiveColor;
            } else if (isEndEdgeHovered) {
                rightColor = handleHoverColor;
                // Add subtle glow on hover
                renderer.drawGlow(rightHandle, 4.0f, 0.5f, handleHoverColor.withAlpha(0.4f));
            }
            
            renderer.fillRoundedRect(leftHandle, 2.0f, leftColor);
            renderer.fillRoundedRect(rightHandle, 2.0f, rightColor);
            
            // Draw vertical grip lines inside handles for visual clarity
            float lineX = leftHandle.x + handleWidth * 0.5f;
            float lineTop = leftHandle.y + 4.0f;
            float lineBottom = leftHandle.y + leftHandle.height - 4.0f;
            renderer.drawLine(NUIPoint(lineX - 2.0f, lineTop), NUIPoint(lineX - 2.0f, lineBottom), 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.5f));
            renderer.drawLine(NUIPoint(lineX + 2.0f, lineTop), NUIPoint(lineX + 2.0f, lineBottom), 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.5f));
            
            lineX = rightHandle.x + handleWidth * 0.5f;
            renderer.drawLine(NUIPoint(lineX - 2.0f, lineTop), NUIPoint(lineX - 2.0f, lineBottom), 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.5f));
            renderer.drawLine(NUIPoint(lineX + 2.0f, lineTop), NUIPoint(lineX + 2.0f, lineBottom), 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.5f));
        } else {
            NUIRect topHandle(thumbRect.x, thumbRect.y, thumbRect.width, handleWidth);
            NUIRect bottomHandle(thumbRect.x, thumbRect.y + thumbRect.height - handleWidth, thumbRect.width, handleWidth);
            
            // Top/Start handle color
            NUIColor topColor = handleBaseColor;
            if (isDraggingStart) {
                topColor = handleActiveColor;
            } else if (isStartEdgeHovered) {
                topColor = handleHoverColor;
                renderer.drawGlow(topHandle, 4.0f, 0.5f, handleHoverColor.withAlpha(0.4f));
            }
            
            // Bottom/End handle color
            NUIColor bottomColor = handleBaseColor;
            if (isDraggingEnd) {
                bottomColor = handleActiveColor;
            } else if (isEndEdgeHovered) {
                bottomColor = handleHoverColor;
                renderer.drawGlow(bottomHandle, 4.0f, 0.5f, handleHoverColor.withAlpha(0.4f));
            }
            
            renderer.fillRoundedRect(topHandle, 2.0f, topColor);
            renderer.fillRoundedRect(bottomHandle, 2.0f, bottomColor);
            
            // Draw horizontal grip lines inside handles
            float lineY = topHandle.y + handleWidth * 0.5f;
            float lineLeft = topHandle.x + 4.0f;
            float lineRight = topHandle.x + topHandle.width - 4.0f;
            renderer.drawLine(NUIPoint(lineLeft, lineY - 2.0f), NUIPoint(lineRight, lineY - 2.0f), 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.5f));
            renderer.drawLine(NUIPoint(lineLeft, lineY + 2.0f), NUIPoint(lineRight, lineY + 2.0f), 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.5f));
            
            lineY = bottomHandle.y + handleWidth * 0.5f;
            renderer.drawLine(NUIPoint(lineLeft, lineY - 2.0f), NUIPoint(lineRight, lineY - 2.0f), 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.5f));
            renderer.drawLine(NUIPoint(lineLeft, lineY + 2.0f), NUIPoint(lineRight, lineY + 2.0f), 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.5f));
        }
        
        return;
    }

    // Enhanced thumb with translucent effect and smooth animations
    drawEnhancedThumb(renderer, thumbRect);
}

void NUIScrollbar::drawArrows(NUIRenderer& renderer)
{
    if (style_ == Style::Timeline) return;
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
    else if (hoveredPart_ == Part::LeftArrow)
    {
        arrowColor = arrowHoverColor_;
    }
    
    // Draw arrow background
    const float bgAlpha = std::clamp(arrowColor.a * 0.4f, 0.0f, 1.0f);
    renderer.fillRoundedRect(arrowRect, borderRadius_, arrowColor.withAlpha(bgAlpha));
    
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
    else if (hoveredPart_ == Part::RightArrow)
    {
        arrowColor = arrowHoverColor_;
    }
    
    // Draw arrow background
    const float bgAlpha = std::clamp(arrowColor.a * 0.4f, 0.0f, 1.0f);
    renderer.fillRoundedRect(arrowRect, borderRadius_, arrowColor.withAlpha(bgAlpha));
    
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
    NUIRect thumbRect = getThumbRect();
    
    if (style_ == Style::Timeline && thumbRect.contains(position))
    {
        float edgeSize = 12.0f; // Increased for easier grabbing
        if (orientation_ == Orientation::Horizontal)
        {
            if (position.x < thumbRect.x + edgeSize) return Part::ThumbStartEdge;
            if (position.x > thumbRect.x + thumbRect.width - edgeSize) return Part::ThumbEndEdge;
        }
        else
        {
            if (position.y < thumbRect.y + edgeSize) return Part::ThumbStartEdge;
            if (position.y > thumbRect.y + thumbRect.height - edgeSize) return Part::ThumbEndEdge;
        }
        return Part::Thumb;
    }
    
    if (orientation_ == Orientation::Vertical)
    {
        float y = position.y - bounds.y;
        float height = bounds.height;
        
        if (y < arrowSize_)
            return Part::LeftArrow;
        if (y > height - arrowSize_)
            return Part::RightArrow;
        
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
        
        // Guard against division by zero when trackHeight is zero
        if (trackHeight == 0.0f) {
            return rangeLimitStart_;
        }
        
        double proportion = static_cast<double>(relativeY) / trackHeight;
        return rangeLimitStart_ + proportion * rangeLimitSize_;
    }
    else
    {
        float relativeX = position.x - trackRect.x;
        float trackWidth = trackRect.width;
        
        // Guard against division by zero when trackWidth is zero
        if (trackWidth == 0.0f) {
            return rangeLimitStart_;
        }
        
        double proportion = static_cast<double>(relativeX) / trackWidth;
        return rangeLimitStart_ + proportion * rangeLimitSize_;
    }
}

NUIPoint NUIScrollbar::valueToPosition(double value) const
{
    NUIRect trackRect = getTrackRect();
    
    // Guard against division by zero when rangeLimitSize_ is zero
    if (rangeLimitSize_ <= 0.0) {
        // Return center of track when range is invalid
        return NUIPoint(trackRect.x + trackRect.width * 0.5f, trackRect.y + trackRect.height * 0.5f);
    }
    
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
    // Track: subtle gradient based on configured color
    const float trackAlphaMul = (isHovered_ || isDragging_) ? 0.18f : 0.08f;
    NUIColor trackBase = trackColor_.withAlpha(std::clamp(trackColor_.a * trackAlphaMul, 0.0f, 1.0f));
    NUIColor trackTop = trackBase.lightened(0.03f);
    NUIColor trackBottom = trackBase.darkened(0.06f);
    const float radius = std::min(trackRect.width, trackRect.height) * 0.5f;
    
    // Draw gradient track background (16px wide)
    for (int i = 0; i < 4; ++i)
    {
        float factor = static_cast<float>(i) / 3.0f;
        NUIColor gradientColor = NUIColor::lerp(trackTop, trackBottom, factor);
        NUIRect gradientRect = trackRect;
        gradientRect.y += i * 0.5f;
        gradientRect.height -= i * 0.5f;
        renderer.fillRoundedRect(gradientRect, radius, gradientColor);
    }
    
    // Very subtle inner highlight
    NUIRect highlightRect = trackRect;
    highlightRect.x += 1.0f;
    highlightRect.y += 1.0f;
    highlightRect.width -= 2.0f;
    highlightRect.height = trackRect.height * 0.3f;
    const float highlightAlphaMul = (isHovered_ || isDragging_) ? 0.35f : 0.25f;
    renderer.fillRoundedRect(highlightRect, std::max(0.0f, radius - 1.0f),
                             trackTop.withAlpha(trackTop.a * highlightAlphaMul));
}

void NUIScrollbar::drawEnhancedThumb(NUIRenderer& renderer, const NUIRect& thumbRect)
{
    // Thumb: subtle gradient based on configured colors
    const bool thumbPressed = (isPressed_ && pressedPart_ == Part::Thumb) || (isDragging_ && pressedPart_ == Part::Thumb);
    const bool thumbHot = thumbPressed || (hoveredPart_ == Part::Thumb);

    NUIColor thumbBase = thumbColor_;
    if (thumbPressed) {
        thumbBase = thumbPressedColor_;
    } else if (thumbHot) {
        thumbBase = thumbHoverColor_;
    }
    NUIColor thumbTop = thumbBase.lightened(0.06f);
    NUIColor thumbBottom = thumbBase.darkened(0.06f);

    // Thickness affordance: slightly narrower by default, slightly wider on hover.
    NUIRect visualThumb = thumbRect;
    const float inset = thumbHot ? 1.0f : 2.0f;
    if (orientation_ == Orientation::Vertical) {
        visualThumb.x += inset;
        visualThumb.width = std::max(0.0f, visualThumb.width - inset * 2.0f);
    } else {
        visualThumb.y += inset;
        visualThumb.height = std::max(0.0f, visualThumb.height - inset * 2.0f);
    }

    const float radius = std::min(visualThumb.width, visualThumb.height) * 0.5f;
    
    // Faded white gradient thumb (16px wide)
    for (int i = 0; i < 4; ++i)
    {
        float factor = static_cast<float>(i) / 3.0f;
        NUIColor gradientColor = NUIColor::lerp(thumbTop, thumbBottom, factor);
        NUIRect gradientRect = visualThumb;
        gradientRect.y += i * 0.5f;
        gradientRect.height -= i * 0.5f;
        renderer.fillRoundedRect(gradientRect, radius, gradientColor);
    }
    
    // White markers within the thumb (orientation-aware)
    if (orientation_ == Orientation::Vertical)
    {
        // Vertical scrollbar: horizontal white lines
        float markerHeight = 2.0f; // Thickness of horizontal lines
        float markerSpacing = 3.0f; // Gap between markers
        float totalMarkerHeight = (markerHeight * 2) + markerSpacing;
        
        // Center the markers vertically in the thumb
        float markerY = visualThumb.y + (visualThumb.height - totalMarkerHeight) * 0.5f;
        
        // Top horizontal white marker
        NUIRect topMarker(visualThumb.x + 2.0f, markerY, visualThumb.width - 4.0f, markerHeight);
        const float markerAlpha = thumbHot ? 0.24f : 0.12f;
        renderer.fillRoundedRect(topMarker, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, markerAlpha));
        
        // Bottom horizontal white marker
        NUIRect bottomMarker(visualThumb.x + 2.0f, markerY + markerHeight + markerSpacing, 
                             visualThumb.width - 4.0f, markerHeight);
        renderer.fillRoundedRect(bottomMarker, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, markerAlpha));
    }
    else
    {
        // Horizontal scrollbar: vertical white lines
        float markerWidth = 2.0f; // Thickness of vertical lines
        float markerSpacing = 3.0f; // Gap between markers
        float totalMarkerWidth = (markerWidth * 2) + markerSpacing;
        
        // Center the markers horizontally in the thumb
        float markerX = visualThumb.x + (visualThumb.width - totalMarkerWidth) * 0.5f;
        
        // Left vertical white marker
        NUIRect leftMarker(markerX, visualThumb.y + 2.0f, markerWidth, visualThumb.height - 4.0f);
        const float markerAlpha = thumbHot ? 0.24f : 0.12f;
        renderer.fillRoundedRect(leftMarker, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, markerAlpha));
        
        // Right vertical white marker
        NUIRect rightMarker(markerX + markerWidth + markerSpacing, visualThumb.y + 2.0f, 
                            markerWidth, visualThumb.height - 4.0f);
        renderer.fillRoundedRect(rightMarker, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, markerAlpha));
    }
    
    // Subtle inner highlight
    NUIRect highlightRect = visualThumb;
    highlightRect.x += 1.0f;
    highlightRect.y += 1.0f;
    highlightRect.width -= 2.0f;
    highlightRect.height = visualThumb.height * 0.4f;
    renderer.fillRoundedRect(highlightRect, std::max(0.0f, radius - 1.0f),
                             thumbTop.withAlpha(thumbTop.a * 0.25f));
    
    // Very subtle border
    renderer.strokeRoundedRect(visualThumb, radius, 1.0f,
        thumbBase.lightened(0.05f).withAlpha(std::clamp(thumbBase.a * (thumbHot ? 0.55f : 0.45f), 0.0f, 1.0f)));
}

} // namespace NomadUI
