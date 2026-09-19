// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUIScrollbar.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include <algorithm>
#include <cmath>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace AestraUI {

void drawOverlayScrollbar(NUIRenderer& renderer, const NUIRect& gutter, const NUIRect& thumb,
                          const ScrollbarPaintState& state)
{
    const float opacity = std::clamp(state.opacity, 0.0f, 1.0f);
    if (opacity <= 0.01f || thumb.width <= 0.0f || thumb.height <= 0.0f) {
        // An empty thumb means the content fits. Nothing is drawn, not even the
        // track — a track with no thumb is exactly the permanently visible
        // chrome this look exists to remove.
        return;
    }

    auto& theme = NUIThemeManager::getInstance();
    const bool hot = state.hovered || state.pressed;
    const bool vertical = gutter.height >= gutter.width;

    // Track: absent at rest, a whisper once the pointer is near. It only says
    // how far the thumb can travel, which only matters while reaching for it.
    if (hot && !gutter.isEmpty()) {
        const float trackRadius = (vertical ? gutter.width : gutter.height) * 0.5f;
        renderer.fillRoundedRect(gutter, trackRadius,
                                 theme.getColor("border").withAlpha(0.10f * opacity));
    }

    // Thumb: a plain pill that grows toward the gutter edges on hover rather
    // than only changing colour, so the affordance survives at low alpha. No
    // gradient, no grip markers, no border — all three read as chrome at the
    // sizes these gutters actually get.
    const float inset = hot ? 1.0f : 2.5f;
    NUIRect pill = thumb;
    if (vertical) {
        pill.x += inset;
        pill.width = std::max(1.0f, pill.width - inset * 2.0f);
    } else {
        pill.y += inset;
        pill.height = std::max(1.0f, pill.height - inset * 2.0f);
    }

    float alpha = 0.30f;
    if (state.pressed) {
        alpha = 0.62f;
    } else if (state.hovered) {
        alpha = 0.46f;
    }

    const float radius = std::min(pill.width, pill.height) * 0.5f;
    renderer.fillRoundedRect(pill, radius, theme.getColor("textPrimary").withAlpha(alpha * opacity));
}


NUIScrollbar::NUIScrollbar(Orientation orientation)
    : NUIComponent()
    , orientation_(orientation)
{
    // No per-instance styling: drawOverlayScrollbar() is the one authority for
    // how a scrollbar looks, so there is nothing here to configure and nothing
    // a call site can set that would make this scrollbar differ from the rest.
    setSize(orientation == Orientation::Vertical ? kOverlayScrollbarThickness : 200.0f,
            orientation == Orientation::Vertical ? 200.0f : kOverlayScrollbarThickness);
    updateThumbSize();
}

void NUIScrollbar::onRender(NUIRenderer& renderer)
{
    if (!isVisible() || isAutoHidden_) return;

    drawTrack(renderer);
    drawThumb(renderer);
}

void NUIScrollbar::onThemeChanged(const NUIThemeProperties& theme)
{
    // Colours are read from the theme at paint time, so a theme swap needs no
    // cached state refreshed here.
    NUIComponent::onThemeChanged(theme);
}

void NUIScrollbar::onUpdate(double deltaTime)
{
    if (isAnimating_ && !isDragging_) {
        animationTime_ += deltaTime;
        const double t = std::clamp(animationTime_ / animationDuration_, 0.0, 1.0);
        const double eased = 1.0 - std::pow(1.0 - t, 3.0);
        const double next = animationStartValue_ + (animationTargetValue_ - animationStartValue_) * eased;
        setCurrentRange(next, currentRangeSize_);
        triggerScroll();

        if (t >= 1.0) {
            isAnimating_ = false;
            setCurrentRange(animationTargetValue_, currentRangeSize_);
            triggerScroll();
        }
    }

    if (autoHide_ && !isHovered_ && !isDragging_ && autoHideTimer_ > 0.0) {
        autoHideTimer_ = std::max(0.0, autoHideTimer_ - deltaTime);
        if (autoHideTimer_ <= 0.0) {
            isAutoHidden_ = true;
            setDirty(true);
        }
    }

    NUIComponent::onUpdate(deltaTime);
}

bool NUIScrollbar::onMouseEvent(const NUIMouseEvent& event)
{
    if (!isVisible() || !isEnabled() || isAutoHidden_) return false;

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
    else if (event.wheelDelta != 0.0f && bounds.contains(event.position))
    {
        const double direction = (event.wheelDelta > 0.0f) ? -1.0 : 1.0;
        scrollBy(direction * singleStepSize_ * 2.0);
        return true;
    }
    else if (event.released && event.button == NUIMouseButton::Left && isPressed_)
    {
        isPressed_ = false;
        isDragging_ = false;
        pressedPart_ = Part::None;
        isAnimating_ = false;
        triggerScrollEnd();
        setDirty(true);
        return true;
    }
    else if (isDragging_ && event.button == NUIMouseButton::None)
    {
        // Handle thumb dragging (mouse move events have button = None)
        // Use relative drag delta to preserve initial grab offset within thumb
        const double kMinRangeSize = 0.001;  // Minimum allowed range size
        
        NUIRect trackRect = getTrackRect();
        NUIRect thumbRect = getThumbRect();
        double deltaPixels = 0.0, trackLength = 0.0, thumbLengthPixels = 0.0;
        
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
            
            double stepDeltaPixels;
            if (orientation_ == Orientation::Vertical) stepDeltaPixels = event.position.y - lastMousePosition_.y;
            else stepDeltaPixels = event.position.x - lastMousePosition_.x;

            double valuePerPixel = rangeLimitSize_ / trackLength;
            double valueDelta = stepDeltaPixels * valuePerPixel;

            double newStart = currentRangeStart_ + valueDelta;
            double newSize = currentRangeSize_ - valueDelta;
            
            // Constraints
            if (newSize < kMinRangeSize) {
                newSize = kMinRangeSize;
                newStart = currentRangeStart_ + currentRangeSize_ - kMinRangeSize;
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
             if (newSize < kMinRangeSize) {
                newSize = kMinRangeSize;
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
        isAnimating_ = false;
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

void NUIScrollbar::scrollBy(double delta)
{
    scrollTo(currentRangeStart_ + delta);
}

void NUIScrollbar::scrollTo(double position)
{
    double maxStart = std::max(rangeLimitStart_, rangeLimitStart_ + rangeLimitSize_ - currentRangeSize_);
    double clamped = std::clamp(position, rangeLimitStart_, maxStart);
    if (isDragging_) {
        isAnimating_ = false;
        setCurrentRange(clamped, currentRangeSize_);
        triggerScroll();
        return;
    }
    if (std::abs(clamped - currentRangeStart_) < 1e-6) {
        return;
    }
    animationStartValue_ = currentRangeStart_;
    animationTargetValue_ = clamped;
    animationTime_ = 0.0;
    animationDuration_ = 0.16;
    isAnimating_ = true;
    setDirty(true);
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

// Track and thumb are one paint: drawOverlayScrollbar() needs both rects at once
// to decide whether the track should show at all. drawThumb() therefore paints
// the pair and drawTrack() is the no-op half — both stay as override points so a
// subclass can still replace either.
void NUIScrollbar::drawTrack(NUIRenderer& renderer)
{
    (void)renderer;
}

void NUIScrollbar::drawThumb(NUIRenderer& renderer)
{
    if (currentRangeSize_ >= rangeLimitSize_) return; // Content fits: draw nothing at all.

    ScrollbarPaintState state;
    state.hovered = isHovered_ || hoveredPart_ == Part::Thumb;
    state.pressed = isDragging_ || (isPressed_ && pressedPart_ == Part::Thumb);
    drawOverlayScrollbar(renderer, getTrackRect(), getThumbRect(), state);
}

NUIScrollbar::Part NUIScrollbar::getPartAtPosition(const NUIPoint& position) const
{
    NUIRect bounds = getBounds();
    NUIRect thumbRect = getThumbRect();

    // Thumb ends are grab handles for resizing the visible range (the timeline
    // overview's drag-to-zoom). Only worth offering on a thumb long enough that
    // the middle is still grabbable for a plain pan.
    const float thumbExtent = (orientation_ == Orientation::Horizontal) ? thumbRect.width : thumbRect.height;
    const float edgeSize = std::min(12.0f, thumbExtent * 0.25f);
    if (edgeSize > 1.0f && thumbRect.contains(position))
    {
        if (orientation_ == Orientation::Horizontal)
        {
            if (position.x < thumbRect.x + edgeSize) return Part::ThumbStartEdge;
            if (position.x > thumbRect.right() - edgeSize) return Part::ThumbEndEdge;
        }
        else
        {
            if (position.y < thumbRect.y + edgeSize) return Part::ThumbStartEdge;
            if (position.y > thumbRect.bottom() - edgeSize) return Part::ThumbEndEdge;
        }
        return Part::Thumb;
    }

    if (thumbRect.contains(position))
        return Part::Thumb;

    if (orientation_ == Orientation::Vertical)
    {
        if (position.y < thumbRect.y)
            return Part::LeftTrack;
        if (position.y > thumbRect.bottom())
            return Part::RightTrack;
    }
    else
    {
        if (position.x < thumbRect.x)
            return Part::LeftTrack;
        if (position.x > thumbRect.right())
            return Part::RightTrack;
    }

    return Part::Track;
}

NUIRect NUIScrollbar::getThumbRect() const
{
    NUIRect bounds = getBounds();
    const double thumbPos = getThumbPosition();
    const double thumbLen = getThumbLength();

    // minimumThumbSize_ is a *fraction* of the range, so on a long timeline it
    // still resolves to a few unusable pixels. The pixel floor is what actually
    // keeps a thumb grabbable, and it is shared with every other scrollbar.
    if (orientation_ == Orientation::Vertical)
    {
        const float track = bounds.height;
        const float h = std::min(track, std::max(kOverlayScrollbarMinThumb, static_cast<float>(thumbLen * track)));
        const float y = bounds.y + static_cast<float>(thumbPos * track) * (track - h) /
                                       std::max(1.0f, track - static_cast<float>(thumbLen * track));
        return NUIRect(bounds.x, std::clamp(y, bounds.y, bounds.bottom() - h), bounds.width, h);
    }

    const float track = bounds.width;
    const float w = std::min(track, std::max(kOverlayScrollbarMinThumb, static_cast<float>(thumbLen * track)));
    const float x = bounds.x + static_cast<float>(thumbPos * track) * (track - w) /
                                   std::max(1.0f, track - static_cast<float>(thumbLen * track));
    return NUIRect(std::clamp(x, bounds.x, bounds.right() - w), bounds.y, w, bounds.height);
}

NUIRect NUIScrollbar::getTrackRect() const
{
    // The whole component is the gutter now that there are no arrow buttons.
    return getBounds();
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

} // namespace AestraUI
