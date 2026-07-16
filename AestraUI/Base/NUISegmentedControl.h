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
 * NUISegmentedControl - A modern segmented toggle control with sliding indicator
 * 
 * Creates a pill-shaped container with multiple segments. Click to switch between them.
 * Features a smooth sliding indicator that moves to the selected segment.
 */
class NUISegmentedControl : public NUIComponent {
public:
    enum class VisualStyle {
        Pill,
        UnderlineTabs
    };

    NUISegmentedControl(const std::vector<std::string>& segments)
        : segments_(segments)
        , selectedIndex_(0)
        , indicatorPosition_(0.0f)
        , cornerRadius_(-1.0f)
        , accentColor_(NUIThemeManager::getInstance().getColor("accentPrimary"))
        , hoveredIndex_(-1)
        , visualStyle_(VisualStyle::Pill)
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
    
    void setOnSelectionChanged(std::function<void(size_t)> callback) {
        onSelectionChanged_ = callback;
    }
    
    void setCornerRadius(float radius) { cornerRadius_ = radius; }
    void setAccentColor(const NUIColor& color) { accentColor_ = color; setDirty(true); }
    void setVisualStyle(VisualStyle style) { visualStyle_ = style; setDirty(true); }
    
    void onRender(NUIRenderer& renderer) override {
        auto bounds = getBounds();
        auto& theme = NUIThemeManager::getInstance();
        const auto& props = theme.getCurrentTheme();
        const float radius = cornerRadius_ >= 0.0f ? cornerRadius_ : props.radiusL;
        const float fontSize = props.fontSizeS;
        const bool enabled = isEnabled();

        if (visualStyle_ == VisualStyle::UnderlineTabs) {
            renderer.fillRect(bounds, theme.getColor("backgroundSecondary"));
            renderer.drawLine({bounds.x, bounds.bottom() - 1.0f},
                              {bounds.right(), bounds.bottom() - 1.0f},
                              1.0f,
                              theme.getColor("border").withAlpha(0.88f));

            if (!segments_.empty()) {
                const float segmentWidth = bounds.width / static_cast<float>(segments_.size());
                for (size_t i = 0; i < segments_.size(); ++i) {
                    const float segmentX = bounds.x + static_cast<float>(i) * segmentWidth;
                    const NUIRect segmentBounds(segmentX, bounds.y, segmentWidth, bounds.height);
                    const bool isSelected = i == selectedIndex_;
                    const bool hovered = enabled && static_cast<int>(i) == hoveredIndex_;
                    if (hovered && !isSelected) {
                        renderer.fillRect(segmentBounds, theme.getColor("surfaceRaised").withAlpha(0.38f));
                    }
                    const NUIColor textColor = !enabled
                        ? theme.getColor("textDisabled")
                        : isSelected
                        ? theme.getColor("textPrimary")
                        : theme.getColor("textPrimary").withAlpha(hovered ? 0.72f : 0.50f);
                    renderer.drawTextCentered(segments_[i], segmentBounds, fontSize, textColor);
                    if (isSelected) {
                        renderer.fillRect({segmentBounds.x, segmentBounds.bottom() - 2.0f, segmentBounds.width, 2.0f}, accentColor_);
                    }
                }
            }

            if (enabled && isFocused()) {
                renderer.strokeRect(bounds, 1.5f, theme.getColor("focusRing"));
            }

            NUIComponent::onRender(renderer);
            return;
        }
        
        // Background track
        renderer.fillRoundedRect(bounds, radius, theme.getColor("controlBackground"));
        
        renderer.strokeRoundedRect(bounds, radius, isFocused() && enabled ? 1.5f : 1.0f,
                                   isFocused() && enabled ? theme.getColor("focusRing")
                                                          : theme.getColor("borderSubtle"));

        if (segments_.empty()) {
            NUIComponent::onRender(renderer);
            return;
        }
        
        // Calculate segment dimensions
        float segmentWidth = bounds.width / static_cast<float>(segments_.size());
        float padding = props.spacingXS * 0.5f;
        float indicatorWidth = segmentWidth - padding * 2;
        float indicatorHeight = bounds.height - padding * 2;
        
        // Draw inactive / hovered segment plates
        for (size_t i = 0; i < segments_.size(); ++i) {
            if (i != selectedIndex_) {
                float segmentX = bounds.x + padding + i * segmentWidth;
                NUIRect inactiveRect(segmentX, bounds.y + padding, indicatorWidth, indicatorHeight);
                const bool hovered = static_cast<int>(i) == hoveredIndex_;
                renderer.fillRoundedRect(inactiveRect, radius - padding,
                    hovered && enabled ? theme.getColor("controlHover") : NUIColor::transparent());
            }
        }
        
        // Sliding indicator
        float indicatorX = bounds.x + padding + indicatorPosition_ * segmentWidth;
        NUIRect indicatorRect(indicatorX, bounds.y + padding, indicatorWidth, indicatorHeight);
        
        renderer.fillRoundedRect(indicatorRect, radius - padding,
                                 enabled ? theme.getColor("selection") : theme.getColor("controlDisabled"));
        renderer.strokeRoundedRect(indicatorRect, radius - padding, 1.0f,
                                   enabled ? accentColor_.withAlpha(0.64f) : theme.getColor("borderSubtle"));
        
        // Draw segment labels
        for (size_t i = 0; i < segments_.size(); ++i) {
            float segmentX = bounds.x + i * segmentWidth;
            NUIRect segmentBounds(segmentX, bounds.y, segmentWidth, bounds.height);
            
            bool isSelected = (i == selectedIndex_);
            NUIColor textColor = !enabled
                ? theme.getColor("textDisabled")
                : isSelected
                ? theme.getColor("textPrimary").withAlpha(0.96f)
                : theme.getColor("textSecondary").withAlpha(static_cast<int>(i) == hoveredIndex_ ? 0.86f : 0.66f);
            
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
            if (newHover != hoveredIndex_ && !event.cursorCaptured) {
                hoveredIndex_ = newHover;
                setDirty(true);
            }
        }
        
        if (event.pressed && event.button == NUIMouseButton::Left) {
            if (bounds.contains(event.position)) {
                setFocused(true);
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
    VisualStyle visualStyle_;
    std::function<void(size_t)> onSelectionChanged_;
};

} // namespace AestraUI
