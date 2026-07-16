// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once
#include "NUIComponent.h"
#include "NUIRenderer.h"
#include "NUITypes.h"
#include "NUIThemeSystem.h"
#include <vector>
#include <string>
#include <functional>

namespace AestraUI {

/**
 * NUIMenuBar - A menu bar with File/Edit/View style labels
 * 
 * Each label can be clicked to trigger a callback (e.g., show a dropdown menu).
 * Supports hover highlighting.
 */
class NUIMenuBar : public NUIComponent {
public:
    struct MenuItem {
        std::string label;
        std::function<void()> onClick;
    };
    
    NUIMenuBar() : hoveredIndex_(-1), pressedIndex_(-1) {
        setId("MenuBar");
    }
    
    void addItem(const std::string& label, std::function<void()> onClick = nullptr) {
        items_.push_back({label, onClick});
        setDirty(true);
    }
    
    void clear() {
        items_.clear();
        hoveredIndex_ = -1;
        pressedIndex_ = -1;
        setDirty(true);
    }
    
    void onRender(NUIRenderer& renderer) override {
        auto bounds = getBounds();
        auto& theme = NUIThemeManager::getInstance();
        const auto& props = theme.getCurrentTheme();

        const float fontSize = props.fontSizeXS;
        const float paddingX = props.spacingS + props.spacingXS;
        const float gap = props.spacingXS;
        const float radius = props.radiusS;
        float x = bounds.x;

        // Calculate and render each menu item
        itemRects_.clear();
        for (size_t i = 0; i < items_.size(); ++i) {
            const auto& item = items_[i];
            NUISize sz = renderer.measureText(item.label, fontSize);

            NUIRect itemRect(x, bounds.y + 2.0f, sz.width + paddingX * 2, bounds.height - 4.0f);
            itemRects_.push_back(itemRect);

            const bool hovered = (hoveredIndex_ == static_cast<int>(i));
            const bool pressed = (pressedIndex_ == static_cast<int>(i));
            NUIControlVisualState state;
            state.enabled = isEnabled();
            state.hovered = hovered;
            state.pressed = pressed;
            state.focused = isFocused() && hovered;
            const auto colors = resolveControlColors(props, state);
            if (hovered || pressed) {
                renderer.fillRoundedRect(itemRect, radius, colors.background);
                renderer.strokeRoundedRect(itemRect, radius, props.layout.dividerWidth, colors.border);
            }

            renderer.drawTextCentered(item.label, itemRect, fontSize, colors.text);

            x += itemRect.width + gap;
        }

        NUIComponent::onRender(renderer);
    }
    
    bool onMouseEvent(const NUIMouseEvent& event) override {
        if (!isVisible() || !isEnabled()) return false;
        
        // Use global bounds since mouse event position is in window coordinates
        auto globalBounds = getGlobalBounds();
        
        // Check if mouse is in our bounds
        if (!globalBounds.contains(event.position)) {
            if (hoveredIndex_ != -1) {
                hoveredIndex_ = -1;
                setDirty(true);
            }
            if (event.released) {
                pressedIndex_ = -1;
            }
            return false;
        }
        
        // Calculate global item rects for hit testing
        auto localBounds = getBounds();
        float offsetX = globalBounds.x - localBounds.x;
        float offsetY = globalBounds.y - localBounds.y;
        
        // Update hover state
        int previousHover = hoveredIndex_;
        hoveredIndex_ = -1;
        
        for (size_t i = 0; i < itemRects_.size(); ++i) {
            // Convert item rect to global coords for hit testing
            NUIRect globalItemRect(
                itemRects_[i].x + offsetX,
                itemRects_[i].y + offsetY,
                itemRects_[i].width,
                itemRects_[i].height
            );
            if (globalItemRect.contains(event.position)) {
                hoveredIndex_ = static_cast<int>(i);
                break;
            }
        }
        
        if (previousHover != hoveredIndex_) {
            setDirty(true);
        }
        
        // Arm on press and invoke on a matching release so pressed feedback is
        // distinct from hover and release cannot click through to another item.
        if (event.pressed && event.button == NUIMouseButton::Left) {
            if (hoveredIndex_ >= 0 && hoveredIndex_ < static_cast<int>(items_.size())) {
                pressedIndex_ = hoveredIndex_;
                setDirty(true);
                return true;
            }
        }
        if (event.released && event.button == NUIMouseButton::Left) {
            const int armedIndex = pressedIndex_;
            pressedIndex_ = -1;
            setDirty(true);
            if (armedIndex >= 0 && armedIndex == hoveredIndex_ &&
                armedIndex < static_cast<int>(items_.size()) && items_[armedIndex].onClick) {
                items_[armedIndex].onClick();
            }
            return true;
        }
        
        return true; // Consume events in our bounds
    }
    
private:
    std::vector<MenuItem> items_;
    std::vector<NUIRect> itemRects_; // Computed during render
    int hoveredIndex_;
    int pressedIndex_;
};

} // namespace AestraUI
