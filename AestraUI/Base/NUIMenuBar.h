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
    
    NUIMenuBar() : hoveredIndex_(-1) {
        setId("MenuBar");
    }
    
    void addItem(const std::string& label, std::function<void()> onClick = nullptr) {
        items_.push_back({label, onClick});
        setDirty(true);
    }
    
    void clear() {
        items_.clear();
        hoveredIndex_ = -1;
        setDirty(true);
    }
    
    void onRender(NUIRenderer& renderer) override {
        auto bounds = getBounds();
        auto& theme = NUIThemeManager::getInstance();
        
        NUIColor textColor = theme.getColor("textPrimary");
        NUIColor hoverColor = theme.getColor("primary").withAlpha(0.2f); // Aestra Purple hover
        
        float fontSize = 12.0f;
        float padding = 8.0f;
        float x = bounds.x;
        
        // Calculate and render each menu item
        itemRects_.clear();
        for (size_t i = 0; i < items_.size(); ++i) {
            const auto& item = items_[i];
            NUISize sz = renderer.measureText(item.label, fontSize);
            
            NUIRect itemRect(x, bounds.y, sz.width + padding * 2, bounds.height);
            itemRects_.push_back(itemRect);
            
            // Draw hover background
            if (hoveredIndex_ == static_cast<int>(i)) {
                renderer.fillRoundedRect(itemRect, 4.0f, hoverColor);
            }
            
            // Draw text centered
            float textY = bounds.y + (bounds.height - sz.height) / 2.0f;
            renderer.drawText(item.label, NUIPoint(x + padding, textY), fontSize, textColor);
            
            x += itemRect.width + 4.0f; // 4px gap between items
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
        
        // Handle click
        if (event.pressed && event.button == NUIMouseButton::Left) {
            if (hoveredIndex_ >= 0 && hoveredIndex_ < static_cast<int>(items_.size())) {
                if (items_[hoveredIndex_].onClick) {
                    items_[hoveredIndex_].onClick();
                }
                return true;
            }
        }
        
        return true; // Consume events in our bounds
    }
    
private:
    std::vector<MenuItem> items_;
    std::vector<NUIRect> itemRects_; // Computed during render
    int hoveredIndex_;
};

} // namespace AestraUI
