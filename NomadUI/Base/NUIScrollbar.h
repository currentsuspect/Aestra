// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITypes.h"
#include "NUIIcon.h"
#include <functional>

namespace NomadUI {

/**
 * NUIScrollbar - A scrollbar component for scrollable content
 * Supports both horizontal and vertical scrolling with customizable appearance
 * Replaces juce::ScrollBar with NomadUI styling and theming
 */
class NUIScrollbar : public NUIComponent
{
public:
    // Scrollbar orientations
    enum class Orientation
    {
        Horizontal,
        Vertical
    };

    // Scrollbar styles
    enum class Style
    {
        Standard,
        Timeline
    };

    // Scrollbar parts
    enum class Part
    {
        None,
        Track,
        Thumb,
        ThumbStartEdge, // Left or Top edge of thumb
        ThumbEndEdge,   // Right or Bottom edge of thumb
        LeftArrow,   // or UpArrow for vertical
        RightArrow,  // or DownArrow for vertical
        LeftTrack,   // Track before thumb
        RightTrack   // Track after thumb
    };

    NUIScrollbar(Orientation orientation = Orientation::Vertical);
    ~NUIScrollbar() override = default;

    // Component interface
    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onMouseEnter() override;
    void onMouseLeave() override;

    // Scroll properties
    void setCurrentRange(double start, double size);
    double getCurrentRangeStart() const { return currentRangeStart_; }
    double getCurrentRangeSize() const { return currentRangeSize_; }

    void setRangeLimit(double start, double size);
    double getRangeLimitStart() const { return rangeLimitStart_; }
    double getRangeLimitSize() const { return rangeLimitSize_; }

    void setSingleStepSize(double step);
    double getSingleStepSize() const { return singleStepSize_; }

    void setPageStepSize(double step);
    double getPageStepSize() const { return pageStepSize_; }

    void setAutoHide(bool autoHide);
    bool isAutoHide() const { return autoHide_; }

    void setAutoHideDelay(double delay);
    double getAutoHideDelay() const { return autoHideDelay_; }

    // Visual properties
    void setOrientation(Orientation orientation);
    Orientation getOrientation() const { return orientation_; }

    void setStyle(Style style);
    Style getStyle() const { return style_; }

    void setThumbSize(double size);
    double getThumbSize() const { return thumbSize_; }

    void setMinimumThumbSize(double size);
    double getMinimumThumbSize() const { return minimumThumbSize_; }

    void setTrackColor(const NUIColor& color);
    NUIColor getTrackColor() const { return trackColor_; }

    void setThumbColor(const NUIColor& color);
    NUIColor getThumbColor() const { return thumbColor_; }

    void setThumbHoverColor(const NUIColor& color);
    NUIColor getThumbHoverColor() const { return thumbHoverColor_; }

    void setThumbPressedColor(const NUIColor& color);
    NUIColor getThumbPressedColor() const { return thumbPressedColor_; }

    void setArrowColor(const NUIColor& color);
    NUIColor getArrowColor() const { return arrowColor_; }

    void setArrowHoverColor(const NUIColor& color);
    NUIColor getArrowHoverColor() const { return arrowHoverColor_; }

    void setArrowPressedColor(const NUIColor& color);
    NUIColor getArrowPressedColor() const { return arrowPressedColor_; }

    void setBorderColor(const NUIColor& color);
    NUIColor getBorderColor() const { return borderColor_; }

    void setBorderWidth(float width);
    float getBorderWidth() const { return borderWidth_; }

    void setBorderRadius(float radius);
    float getBorderRadius() const { return borderRadius_; }

    void setArrowSize(float size);
    float getArrowSize() const { return arrowSize_; }

    // Scrolling methods
    void scrollBy(double delta);
    void scrollTo(double position);
    void scrollToStart();
    void scrollToEnd();
    void scrollByPage(double direction);
    void scrollByLine(double direction);

    // Utility
    double getCurrentPosition() const;
    double getMaximumPosition() const;
    double getThumbPosition() const;
    double getThumbLength() const;
    bool isAtStart() const;
    bool isAtEnd() const;

    // Event callbacks
    void setOnScroll(std::function<void(double)> callback);
    void setOnRangeChange(std::function<void(double start, double size)> callback);
    void setOnScrollStart(std::function<void()> callback);
    void setOnScrollEnd(std::function<void()> callback);

protected:
    // Override these for custom scrollbar appearance
    virtual void drawTrack(NUIRenderer& renderer);
    virtual void drawThumb(NUIRenderer& renderer);
    virtual void drawArrows(NUIRenderer& renderer);
    virtual void drawLeftArrow(NUIRenderer& renderer);
    virtual void drawRightArrow(NUIRenderer& renderer);

    // Hit testing
    virtual Part getPartAtPosition(const NUIPoint& position) const;
    virtual NUIRect getThumbRect() const;
    virtual NUIRect getLeftArrowRect() const;
    virtual NUIRect getRightArrowRect() const;
    virtual NUIRect getTrackRect() const;

    // Scrolling calculations
    virtual double positionToValue(const NUIPoint& position) const;
    virtual NUIPoint valueToPosition(double value) const;

    // Helper drawing methods
    virtual void drawArrowIcon(NUIRenderer& renderer, const NUIRect& rect, float rotation, const NUIColor& color);

private:
    void updateThumbSize();
    void updateThumbPosition();
    void startAutoHideTimer();
    void stopAutoHideTimer();
    void triggerScroll();
    void triggerScrollStart();
    void triggerScrollEnd();
    
    // Enhanced drawing methods
    void drawEnhancedTrack(NUIRenderer& renderer, const NUIRect& trackRect);
    void drawEnhancedThumb(NUIRenderer& renderer, const NUIRect& thumbRect);

    // Scroll state
    double currentRangeStart_ = 0.0;
    double currentRangeSize_ = 0.0;
    double rangeLimitStart_ = 0.0;
    double rangeLimitSize_ = 1.0;
    double singleStepSize_ = 0.1;
    double pageStepSize_ = 0.5;
    double thumbSize_ = 0.0;
    double minimumThumbSize_ = 0.1;

    // Visual properties
    Orientation orientation_ = Orientation::Vertical;
    NUIColor trackColor_ = NUIColor(0.15f, 0.15f, 0.18f, 1.0f);          // Track base (alpha applied at draw time)
    NUIColor thumbColor_ = NUIColor(0.85f, 0.85f, 0.90f, 0.28f);         // Quiet thumb default
    NUIColor thumbHoverColor_ = NUIColor(0.95f, 0.95f, 1.00f, 0.45f);    // Brighter on hover
    NUIColor thumbPressedColor_ = NUIColor(0.70f, 0.70f, 0.80f, 0.65f);  // Stronger on press
    NUIColor arrowColor_ = NUIColor(0.85f, 0.85f, 0.90f, 0.25f);         // Subtle arrows
    NUIColor arrowHoverColor_ = NUIColor(0.95f, 0.95f, 1.00f, 0.45f);    // Brighter on hover
    NUIColor arrowPressedColor_ = NUIColor(0.70f, 0.70f, 0.80f, 0.65f);  // Stronger on press
    NUIColor borderColor_ = NUIColor(0.30f, 0.30f, 0.35f, 0.35f);        // Subtle border
    float borderWidth_ = 1.0f;
    float borderRadius_ = 4.0f;
    float arrowSize_ = 12.0f;

    // Auto-hide behavior
    bool autoHide_ = false;
    double autoHideDelay_ = 1.0;
    double autoHideTimer_ = 0.0;
    bool isAutoHidden_ = false;

    // Interaction state
    bool isHovered_ = false;
    bool isPressed_ = false;
    Part pressedPart_ = Part::None;
    NUIPoint dragStartPosition_;
    NUIPoint lastMousePosition_;
    double dragStartValue_ = 0.0;
    bool isDragging_ = false;
    Part hoveredPart_ = Part::None;  // Track hover state for visual feedback on edge handles

    // Animation state
    bool isAnimating_ = false;
    double animationStartValue_ = 0.0;
    double animationTargetValue_ = 0.0;
    double animationTime_ = 0.0;
    double animationDuration_ = 0.2;

    // Callbacks
    std::function<void(double)> onScrollCallback_;
    std::function<void(double start, double size)> onRangeChangeCallback_;
    std::function<void()> onScrollStartCallback_;
    std::function<void()> onScrollEndCallback_;
    
    // Dragging state for resizing
    double resizeStartSize_ = 0.0;
    double resizeStartValue_ = 0.0;
    Style style_ = Style::Standard;

    // SVG Icons for arrow buttons (Bug #11: Scrollbar Icons)
    std::shared_ptr<NUIIcon> upArrowIcon_;
    std::shared_ptr<NUIIcon> downArrowIcon_;
};

} // namespace NomadUI
