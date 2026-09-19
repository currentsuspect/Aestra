// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITypes.h"
#include <functional>

namespace AestraUI {

// ---------------------------------------------------------------------------
// Overlay scrollbar look — one authority for every scrollbar in the DAW
//
// Four surfaces drew their own scrollbar before this: NUIScrollbar (timeline,
// piano roll), the File Browser's file list, and the File Browser's Collections
// rail. They agreed on nothing — 3px vs 6px vs 16px thumbs, gradients and grip
// markers in two of them, a permanently visible gradient track in another.
//
// The painting lives here as free functions rather than in the component so the
// File Browser can share it without having to give up its own scroll and drag
// state machine, which is wired through its event handling. Nothing about the
// *behaviour* of a scrollbar is decided here; callers keep owning position,
// range and interaction, and hand over the two rects and how hot they are.
//
// Spec 2 §2: minimal track, subtle thumb, no dominant border, and nothing drawn
// at all when the content fits.
// ---------------------------------------------------------------------------

/** @brief Nominal gutter thickness. Layout reserves this; the thumb insets within it. */
inline constexpr float kOverlayScrollbarThickness = 10.0f;

/** @brief Shortest a thumb is ever drawn, so a long document keeps a grabbable target. */
inline constexpr float kOverlayScrollbarMinThumb = 28.0f;

/** @brief How hot the scrollbar is, and how visible. */
struct ScrollbarPaintState {
    bool hovered = false;
    bool pressed = false;
    /** @brief 0 fades the whole scrollbar out; callers that never fade pass 1. */
    float opacity = 1.0f;
};

/**
 * @brief Paint one overlay scrollbar.
 *
 * @param gutter The full strip the scrollbar occupies (the "track").
 * @param thumb  The thumb inside that strip, already positioned by the caller.
 *
 * Draws nothing for an empty thumb — that is how "the content fits, so there is
 * no scrollbar" is expressed, and it is why callers may simply pass a zero rect
 * rather than branching around the call.
 */
void drawOverlayScrollbar(NUIRenderer& renderer, const NUIRect& gutter, const NUIRect& thumb,
                          const ScrollbarPaintState& state);


/**
 * NUIScrollbar - A scrollbar component for scrollable content
 * Supports both horizontal and vertical scrolling with customizable appearance
 * Replaces juce::ScrollBar with AestraUI styling and theming
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

    // Scrollbar parts
    enum class Part
    {
        None,
        Track,
        Thumb,
        ThumbStartEdge, // Left or Top edge of thumb
        ThumbEndEdge,   // Right or Bottom edge of thumb
        LeftTrack,      // Track before thumb
        RightTrack      // Track after thumb
    };

    NUIScrollbar(Orientation orientation = Orientation::Vertical);
    ~NUIScrollbar() override = default;

    // Component interface
    void onRender(NUIRenderer& renderer) override;
    void onThemeChanged(const NUIThemeProperties& theme) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onUpdate(double deltaTime) override;
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

    void setThumbSize(double size);
    double getThumbSize() const { return thumbSize_; }

    void setMinimumThumbSize(double size);
    double getMinimumThumbSize() const { return minimumThumbSize_; }


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

    // Hit testing
    virtual Part getPartAtPosition(const NUIPoint& position) const;
    virtual NUIRect getThumbRect() const;
    virtual NUIRect getTrackRect() const;

    // Scrolling calculations
    virtual double positionToValue(const NUIPoint& position) const;
    virtual NUIPoint valueToPosition(double value) const;

private:
    void updateThumbSize();
    void updateThumbPosition();
    void startAutoHideTimer();
    void stopAutoHideTimer();
    void triggerScroll();
    void triggerScrollStart();
    void triggerScrollEnd();
    
    // Scroll state
    double currentRangeStart_ = 0.0;
    double currentRangeSize_ = 0.0;
    double rangeLimitStart_ = 0.0;
    double rangeLimitSize_ = 1.0;
    double singleStepSize_ = 0.1;
    double pageStepSize_ = 0.5;
    double thumbSize_ = 0.0;
    double minimumThumbSize_ = 0.1;

    // Visual properties. Everything else about how a scrollbar looks lives in
    // drawOverlayScrollbar() above, deliberately out of reach of call sites.
    Orientation orientation_ = Orientation::Vertical;

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
};

} // namespace AestraUI
