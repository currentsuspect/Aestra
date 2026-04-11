// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once
#include "NUIComponent.h"
#include "NUIRenderer.h"
#include "NUITypes.h"
#include "NUIThemeSystem.h"
#include <functional>
#include <string>
#include <vector>

namespace AestraUI {

/**
     * Construct a segmented control populated with the given segment labels.
     *
     * Initializes with the first segment selected, the sliding indicator positioned
     * at index 0, a corner radius of 12.0, a default purple accent color, and no
     * hovered segment (hover index set to -1). The component id is set to
     * "SegmentedControl".
     *
     * @param segments Vector of labels for each segment; order defines segment indices.
     */
class NUISegmentedControl : public NUIComponent {
public:
    NUISegmentedControl(const std::vector<std::string>& segments)
        : segments_(segments)
        , selectedIndex_(0)
        , indicatorPosition_(0.0f)
        , cornerRadius_(12.0f)
        , accentColor_(NUIColor(0.55f, 0.36f, 0.96f, 1.0f))
        , hoveredIndex_(-1)
    {
        setId("SegmentedControl");
    }
    
    void setSelectedIndex(size_t index, bool animate = true) {
        if (index >= segments_.size()) return;
        if (index == selectedIndex_) {
            if (!animate) {
                indicatorPosition_ = static_cast<float>(index);
                setDirty(true);
            }
            return;
        }
        selectedIndex_ = index;
        if (!animate) {
            indicatorPosition_ = static_cast<float>(index);
        }
        if (onSelectionChanged_) {
            onSelectionChanged_(selectedIndex_);
        }
        setDirty(true);
    }
    
    size_t getSelectedIndex() const { return selectedIndex_; }
    
    /**
     * Set the callback to be invoked when the selected segment changes.
     *
     * @param callback Function called with the new selected index when a different segment is selected.
     *                 Passing an empty `std::function` clears any previously set callback.
     */
    void setOnSelectionChanged(std::function<void(size_t)> callback) {
        onSelectionChanged_ = callback;
    }
    
    /**
 * Set the rounded corner radius used when rendering the control.
 *
 * @param radius Corner radius in pixels applied to the control's rounded corners.
 */
void setCornerRadius(float radius) { cornerRadius_ = radius; }
    /**
 * Update the control's accent color and mark the component as needing redraw.
 *
 * @param color New accent color used for the indicator and outlines.
 */
void setAccentColor(const NUIColor& color) { accentColor_ = color; setDirty(true); }
    
    void onRender(NUIRenderer& renderer) override {
        auto bounds = getBounds();
        auto& theme = NUIThemeManager::getInstance();
        
        // Background track
        NUIColor trackColor = theme.getColor("surfaceRaised").withAlpha(0.72f);
        renderer.fillRoundedRect(bounds, cornerRadius_, trackColor);
        
        renderer.strokeRoundedRect(bounds, cornerRadius_, 1.0f,
            theme.getColor("borderSubtle").withAlpha(0.34f));
        renderer.strokeRoundedRect({bounds.x + 1.0f, bounds.y + 1.0f, bounds.width - 2.0f, bounds.height - 2.0f},
            std::max(0.0f, cornerRadius_ - 1.0f), 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.018f));

        if (segments_.empty()) {
            NUIComponent::onRender(renderer);
            return;
        }
        
        // Calculate segment dimensions
        float segmentWidth = bounds.width / static_cast<float>(segments_.size());
        float padding = 2.0f;
        float indicatorWidth = segmentWidth - padding * 2;
        float indicatorHeight = bounds.height - padding * 2;
        
        // Draw inactive / hovered segment plates
        for (size_t i = 0; i < segments_.size(); ++i) {
            if (i != selectedIndex_) {
                float segmentX = bounds.x + padding + i * segmentWidth;
                NUIRect inactiveRect(segmentX, bounds.y + padding, indicatorWidth, indicatorHeight);
                const bool hovered = static_cast<int>(i) == hoveredIndex_;
                renderer.fillRoundedRect(inactiveRect, cornerRadius_ - padding, 
                    hovered ? theme.getColor("buttonBgHover").withAlpha(0.72f)
                            : theme.getColor("buttonBgDefault").withAlpha(0.46f));
            }
        }
        
        // Sliding indicator
        float indicatorX = bounds.x + padding + indicatorPosition_ * segmentWidth;
        NUIRect indicatorRect(indicatorX, bounds.y + padding, indicatorWidth, indicatorHeight);
        
        renderer.fillRoundedRect(indicatorRect, cornerRadius_ - padding, accentColor_.withAlpha(0.22f));
        renderer.strokeRoundedRect(indicatorRect, cornerRadius_ - padding, 1.0f, accentColor_.withAlpha(0.46f));
        
        NUIRect highlightRect(indicatorRect.x + 2, indicatorRect.y, indicatorRect.width - 4, 1.0f);
        renderer.fillRect(highlightRect, NUIColor(1.0f, 1.0f, 1.0f, 0.12f));
        
        // Draw segment labels
        float fontSize = 10.5f;
        for (size_t i = 0; i < segments_.size(); ++i) {
            float segmentX = bounds.x + i * segmentWidth;
            /**
     * Animates the sliding indicator toward the currently selected segment and updates the component state.
     *
     * Moves `indicatorPosition_` toward `selectedIndex_` at a fixed interpolation speed (12.0f), snapping to the target when within 0.01 and marking the component dirty if the position changes. After animation updates, delegates to the base class update.
     *
     * @param deltaTime Time, in seconds, since the last update tick.
     */
    NUIRect segmentBounds(segmentX, bounds.y, segmentWidth, bounds.height);
            
            bool isSelected = (i == selectedIndex_);
            NUIColor textColor = isSelected 
                ? theme.getColor("textPrimary")
                : theme.getColor("textSecondary").withAlpha(static_cast<int>(i) == hoveredIndex_ ? 0.92f : 0.82f);
            
            renderer.drawTextCentered(segments_[i], segmentBounds, fontSize, textColor);
        }
        
        NUIComponent::onRender(renderer);
    }
    
    void onUpdate(double deltaTime) override {
        // Animate indicator sliding
        float targetPos = static_cast<float>(selectedIndex_);
        float diff = targetPos - indicatorPosition_;
        if (std::abs(diff) > 0.001f) {
            float speed = 12.0f; // Animation speed
            indicatorPosition_ += diff * speed * static_cast<float>(deltaTime);
            if (std::abs(targetPos - indicatorPosition_) < 0.01f) {
                indicatorPosition_ = targetPos;
            }
            setDirty(true);
        }
        
        NUIComponent::onUpdate(deltaTime);
    }
    
    /**
     * Handle mouse input for the segmented control, updating hover tracking and selecting segments on left-click.
     *
     * Updates the hovered segment index when the mouse moves within the control's bounds, marks the component dirty
     * when hover changes, and selects a segment (with animation) when the left mouse button is pressed inside bounds.
     * If the selection changes, the selection-change callback may be invoked via setSelectedIndex.
     *
     * @param event Mouse event containing position, button, and pressed state.
     * @returns `true` if the event was handled by the control (for example, a click inside its bounds); `false` otherwise.
     */
    bool onMouseEvent(const NUIMouseEvent& event) override {
        if (!isVisible() || !isEnabled()) return false;
        if (segments_.empty()) return false;

        auto bounds = getBounds();

        if (event.button == NUIMouseButton::None) {
            int newHover = -1;
            if (bounds.contains(event.position)) {
                float relativeX = event.position.x - bounds.x;
                float segmentWidth = bounds.width / static_cast<float>(segments_.size());
                size_t hoverIndex = static_cast<size_t>(relativeX / segmentWidth);
                if (hoverIndex < segments_.size()) {
                    newHover = static_cast<int>(hoverIndex);
                }
            }
            if (newHover != hoveredIndex_) {
                hoveredIndex_ = newHover;
                setDirty(true);
            }
        }
        
        if (event.pressed && event.button == NUIMouseButton::Left) {
            if (bounds.contains(event.position)) {
                // Determine which segment was clicked
                float relativeX = event.position.x - bounds.x;
                float segmentWidth = bounds.width / static_cast<float>(segments_.size());
                size_t clickedIndex = static_cast<size_t>(relativeX / segmentWidth);
                
                if (clickedIndex < segments_.size() && clickedIndex != selectedIndex_) {
                    setSelectedIndex(clickedIndex, true);
                }
                return true;
            }
        }
        
        return NUIComponent::onMouseEvent(event);
    }

    /**
     * Clear hovered segment state when the mouse leaves the component.
     *
     * If a segment was hovered, resets the hovered index to -1 and marks the component dirty.
     * Delegates to the base class mouse-leave handler afterwards.
     */
    void onMouseLeave() override
    {
        if (hoveredIndex_ != -1) {
            hoveredIndex_ = -1;
            setDirty(true);
        }
        NUIComponent::onMouseLeave();
    }
    
private:
    std::vector<std::string> segments_;
    size_t selectedIndex_;
    float indicatorPosition_; // Animated position (0.0 to segments_.size()-1)
    float cornerRadius_;
    NUIColor accentColor_;
    int hoveredIndex_;
    std::function<void(size_t)> onSelectionChanged_;
};

} // namespace AestraUI
