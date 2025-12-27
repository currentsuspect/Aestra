// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once
#include "../Core/NUIComponent.h"
#include "../Graphics/NUIRenderer.h"
#include "../Core/NUITypes.h"
#include <vector>
#include <string>
#include <functional>

namespace NomadUI {

/**
 * NUISegmentedControl - A modern segmented toggle control with sliding indicator
 * 
 * Creates a pill-shaped container with multiple segments. Click to switch between them.
 * Features a smooth sliding indicator that moves to the selected segment.
 */
class NUISegmentedControl : public NUIComponent {
public:
    NUISegmentedControl(const std::vector<std::string>& segments)
        : segments_(segments)
        , selectedIndex_(0)
        , indicatorPosition_(0.0f)
        , cornerRadius_(12.0f)
    {
        setId("SegmentedControl");
    }
    
    void setSelectedIndex(size_t index, bool animate = true) {
        if (index >= segments_.size()) return;
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
    
    void setOnSelectionChanged(std::function<void(size_t)> callback) {
        onSelectionChanged_ = callback;
    }
    
    void setCornerRadius(float radius) { cornerRadius_ = radius; }
    
    void onRender(NUIRenderer& renderer) override {
        auto bounds = getBounds();
        auto& theme = NUIThemeManager::getInstance();
        
        // Background track (dark, almost black)
        NUIColor trackColor = NUIColor(0.08f, 0.08f, 0.10f, 0.95f);
        renderer.fillRoundedRect(bounds, cornerRadius_, trackColor);
        
        // Subtle outer border
        renderer.strokeRoundedRect(bounds, cornerRadius_, 1.0f, 
            NUIColor(1.0f, 1.0f, 1.0f, 0.08f));
        
        // Calculate segment dimensions
        float segmentWidth = bounds.width / static_cast<float>(segments_.size());
        float padding = 2.0f;
        float indicatorWidth = segmentWidth - padding * 2;
        float indicatorHeight = bounds.height - padding * 2;
        
        // Draw INACTIVE segment background first (subtle grey)
        for (size_t i = 0; i < segments_.size(); ++i) {
            if (i != selectedIndex_) {
                float segmentX = bounds.x + padding + i * segmentWidth;
                NUIRect inactiveRect(segmentX, bounds.y + padding, indicatorWidth, indicatorHeight);
                // Subtle grey background for inactive
                renderer.fillRoundedRect(inactiveRect, cornerRadius_ - padding, 
                    NUIColor(0.25f, 0.25f, 0.28f, 0.5f));
            }
        }
        
        // Sliding indicator (purple accent for active)
        float indicatorX = bounds.x + padding + indicatorPosition_ * segmentWidth;
        NUIRect indicatorRect(indicatorX, bounds.y + padding, indicatorWidth, indicatorHeight);
        
        // Purple accent indicator
        NUIColor indicatorColor = theme.getColor("primary");
        if (indicatorColor.r < 0.1f && indicatorColor.g < 0.1f && indicatorColor.b < 0.1f) {
            indicatorColor = NUIColor(0.55f, 0.36f, 0.96f, 1.0f); // Fallback purple
        }
        renderer.fillRoundedRect(indicatorRect, cornerRadius_ - padding, indicatorColor);
        
        // Subtle inner highlight on indicator (glass effect)
        NUIRect highlightRect(indicatorRect.x + 2, indicatorRect.y, indicatorRect.width - 4, 1.0f);
        renderer.fillRect(highlightRect, NUIColor(1.0f, 1.0f, 1.0f, 0.20f));
        
        // Draw segment labels
        float fontSize = 11.0f;
        for (size_t i = 0; i < segments_.size(); ++i) {
            float segmentX = bounds.x + i * segmentWidth;
            NUIRect segmentBounds(segmentX, bounds.y, segmentWidth, bounds.height);
            
            bool isSelected = (i == selectedIndex_);
            NUIColor textColor = isSelected 
                ? NUIColor(1.0f, 1.0f, 1.0f, 1.0f)  // Bright white for active
                : NUIColor(0.7f, 0.7f, 0.72f, 1.0f); // Grey for inactive
            
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
    
    bool onMouseEvent(const NUIMouseEvent& event) override {
        if (!isVisible() || !isEnabled()) return false;
        
        auto bounds = getBounds();
        
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
    
private:
    std::vector<std::string> segments_;
    size_t selectedIndex_;
    float indicatorPosition_; // Animated position (0.0 to segments_.size()-1)
    float cornerRadius_;
    std::function<void(size_t)> onSelectionChanged_;
};

} // namespace NomadUI
