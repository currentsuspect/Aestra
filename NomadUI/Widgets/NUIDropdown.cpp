// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUIDropdown.h"
#include "../Graphics/NUIRenderer.h"
#include "../../NomadCore/include/NomadProfiler.h"
#include "../Core/NUIThemeSystem.h"
#include "../Core/NUITheme.h"
#include <algorithm>
#include <cmath>

namespace NomadUI {

// Track the currently open dropdown so other dropdowns ignore clicks while one is open
static NUIDropdown* s_openDropdown = nullptr;

NUIDropdown::NUIDropdown()
    : NUIComponent()
    , selectedIndex_(-1)
    , isOpen_(false)
    , dropdownAnimProgress_(0.0f)
    , maxVisibleItems_(5)
    , placeholderText_("Select an option...")
    , hoveredIndex_(-1)
{
    setLayer(NUILayer::Dropdown);
    // Initialize colors from the active theme so dropdowns match app styling
    try {
        auto& mgr = NUIThemeManager::getInstance();
        const auto& props = mgr.getCurrentTheme();
        // Choose sensible defaults from theme properties
        backgroundColor_ = props.surfaceRaised;
        hoverColor_ = NUIColor(0.3f, 0.3f, 0.35f, 1.0f); // Grey hover color
        selectedColor_ = props.selected;
        borderColor_ = props.border;
        textColor_ = props.textPrimary;
        arrowColor_ = props.textSecondary;
    } catch (...) {
        // Fall back to existing defaults if theme manager isn't available
        hoverColor_ = NUIColor(0.3f, 0.3f, 0.35f, 1.0f); // Grey hover fallback
    }
}

NUIDropdown::~NUIDropdown() {
    closeDropdown();
}

void NUIDropdown::addItem(const std::string& text, int value) {
    items_.push_back(std::make_shared<NUIDropdownItem>(text, value));
    setDirty(true);
    itemWidthCacheValid_ = false;
    cacheDirty_ = true;
}

void NUIDropdown::addItem(std::shared_ptr<NUIDropdownItem> item) {
    items_.push_back(item);
    setDirty(true);
    itemWidthCacheValid_ = false;
    cacheDirty_ = true;
}

void NUIDropdown::setItemVisible(int index, bool visible) {
    if (index >= 0 && index < static_cast<int>(items_.size())) {
        items_[index]->setVisible(visible);
        setDirty(true);
        itemWidthCacheValid_ = false;
        cacheDirty_ = true;
    }
}

void NUIDropdown::setItemEnabled(int index, bool enabled) {
    if (index >= 0 && index < static_cast<int>(items_.size())) {
        items_[index]->setEnabled(enabled);
        setDirty(true);
        itemWidthCacheValid_ = false;
        cacheDirty_ = true;
    }
}

void NUIDropdown::clearItems() {
    items_.clear();
    selectedIndex_ = -1;
    setDirty(true);
    itemWidthCacheValid_ = false;
    cacheDirty_ = true;
}

int NUIDropdown::getSelectedValue() const {
    if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(items_.size())) {
        return items_[selectedIndex_]->getValue();
    }
    return -1;
}

std::string NUIDropdown::getSelectedText() const {
    if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(items_.size())) {
        return items_[selectedIndex_]->getText();
    }
    return placeholderText_;
}

void NUIDropdown::setSelectedIndex(int index) {
    if (selectedIndex_ == index) return;

    if (index >= -1 && index < static_cast<int>(items_.size())) {
        selectedIndex_ = index;
        if (onSelectionChanged_ && selectedIndex_ >= 0) {
            const auto& item = items_[selectedIndex_];
            onSelectionChanged_(selectedIndex_, item->getValue(), item->getText());
        }
        setDirty(true);
    }
}

void NUIDropdown::onRender(NUIRenderer& renderer) {
    if (!isVisible()) return;

    auto bounds = getBounds();

    // Draw main button background with subtle gradient
    float cornerRadius = 6.0f;
    renderer.fillRoundedRect(bounds, cornerRadius, backgroundColor_);

    // Draw text (vertical center with truncation to prevent bleeding)
    std::string displayText = getSelectedText();
    float padding = 12.0f;
    float arrowSpace = 40.0f; // Large buffer for arrow area

    NUIRect textBounds = bounds;
    textBounds.x += padding;
    textBounds.width -= (padding + arrowSpace); // Reserve arrow space
    textBounds.y += 2.0f; // Slight offset for better centering
    textBounds.height -= 4.0f;

    float fontSize = 18.0f;
    if (auto th = getTheme()) {
        fontSize = th->getFontSize("large");
    }

    // Truncate text with ellipsis if it's too long
    if (textBounds.width > 20.0f) {
        float maxWidth = textBounds.width - 10.0f; // Extra safety margin
        
        // Measure text width
        NUISize textSize = renderer.measureText(displayText, fontSize);
        
        // If text is too wide, truncate with ellipsis
        if (textSize.width > maxWidth) {
            std::string truncated = displayText;
            while (truncated.length() > 3) {
                truncated.pop_back();
                NUISize truncSize = renderer.measureText(truncated + "...", fontSize);
                if (truncSize.width <= maxWidth) break;
            }
            displayText = truncated + "...";
        }
        
        // Left-aligned text for better readability (top-left Y positioning)
        float textY = bounds.y + (bounds.height - fontSize) * 0.5f;
        renderer.drawText(displayText, NUIPoint(textBounds.x, textY), fontSize, textColor_);
    }

    // Draw animated chevron arrow (SVG-style)
    float arrowSize = 6.0f;
    float centerY = bounds.y + bounds.height / 2;
    float arrowX = bounds.x + bounds.width - padding - arrowSize - 4.0f;
    
    // Smooth rotation animation
    float rotationAngle = isOpen_ ? 180.0f : 0.0f;
    
    // Chevron pointing down (flips up when open)
    NUIPoint p1, p2, p3;
    if (isOpen_) {
        // Point up
        p1 = NUIPoint(arrowX, centerY + arrowSize / 3);
        p2 = NUIPoint(arrowX + arrowSize, centerY + arrowSize / 3);
        p3 = NUIPoint(arrowX + arrowSize / 2, centerY - arrowSize / 3);
    } else {
        // Point down
        p1 = NUIPoint(arrowX, centerY - arrowSize / 3);
        p2 = NUIPoint(arrowX + arrowSize, centerY - arrowSize / 3);
        p3 = NUIPoint(arrowX + arrowSize / 2, centerY + arrowSize / 3);
    }
    
    // Draw thicker, smoother chevron lines
    float lineWidth = 1.5f;
    renderer.drawLine(p1, p3, lineWidth, arrowColor_);
    renderer.drawLine(p2, p3, lineWidth, arrowColor_);

    // Draw outer border (darker/thicker)
    NUIColor outerBorder = NUIColor(0.0f, 0.0f, 0.0f, 0.6f);
    renderer.strokeRoundedRect(bounds, cornerRadius, 2.0f, outerBorder);
    
    // Draw inner border (themed)
    renderer.strokeRoundedRect(bounds, cornerRadius, 1.0f, borderColor_.withAlpha(0.5f));

    // Render dropdown list on top if open
    if (isOpen_) {
        renderDropdownList(renderer);
    }
}

bool NUIDropdown::onMouseEvent(const NUIMouseEvent& event) {
    if (!isEnabled()) return false;

    auto bounds = getBounds();

    // Handle left-clicks
    if (event.pressed && event.button == NUIMouseButton::Left) {
        // If dropdown is open, check for clicks on list items first
        if (isOpen_) {
            int clickedIndex = getItemUnderMouse(event.position);
            if (clickedIndex >= 0 && clickedIndex < static_cast<int>(items_.size())) {
                auto item = items_[clickedIndex];
                if (item->isEnabled() && item->isVisible()) {
                    setSelectedIndex(clickedIndex);
                }
                closeDropdown();
                return true;
            }
        }

        // Click on main button
        if (bounds.contains(event.position)) {
            // If another dropdown is open, ignore clicks on this one to avoid accidental toggles
            if (!isOpen_ && s_openDropdown != nullptr && s_openDropdown != this) {
                return false;
            }
            toggleDropdown();
            return true;
        }

        // Click outside - close dropdown if open
        if (isOpen_) {
            closeDropdown();
            return true;
        }
    }

    // Hover handling when open
    if (isOpen_) {
        hoveredIndex_ = getItemUnderMouse(event.position);
        setDirty(true);
    }

    return false;
}

bool NUIDropdown::onKeyEvent(const NUIKeyEvent& event) {
    if (!isEnabled() || !isFocused()) return false;

    if (event.pressed) {
        switch (event.keyCode) {
            case NUIKeyCode::Enter:
            case NUIKeyCode::Space:
                toggleDropdown();
                return true;

            case NUIKeyCode::Escape:
                if (isOpen_) {
                    closeDropdown();
                    return true;
                }
                break;

            case NUIKeyCode::Up:
                if (isOpen_) {
                    int newIndex = selectedIndex_ > 0 ? selectedIndex_ - 1 : static_cast<int>(items_.size()) - 1;
                    setSelectedIndex(newIndex);
                    return true;
                }
                break;

            case NUIKeyCode::Down:
                if (isOpen_) {
                    int newIndex = selectedIndex_ < static_cast<int>(items_.size()) - 1 ? selectedIndex_ + 1 : 0;
                    setSelectedIndex(newIndex);
                    return true;
                }
                break;

            default:
                break;
        }
    }

    return false;
}

void NUIDropdown::onFocusGained() {
    NUIComponent::onFocusGained();
}

void NUIDropdown::onFocusLost() {
    closeDropdown();
    NUIComponent::onFocusLost();
}

void NUIDropdown::toggleDropdown() {
    if (isOpen_) closeDropdown(); else openDropdown();
}

void NUIDropdown::openDropdown() {
    if (isOpen_) return;
    isOpen_ = true;
    hoveredIndex_ = selectedIndex_;
    if (onOpen_) onOpen_();
    s_openDropdown = this;
    setDirty(true);
    cacheDirty_ = true; // regenerate cache when opened
}

void NUIDropdown::closeDropdown() {
    if (!isOpen_) return;
    isOpen_ = false;
    hoveredIndex_ = -1;
    if (onClose_) onClose_();
    if (s_openDropdown == this) s_openDropdown = nullptr;
    setDirty(true);
}

void NUIDropdown::updateAnimations() {
    float targetProgress = isOpen_ ? 1.0f : 0.0f;
    dropdownAnimProgress_ += (targetProgress - dropdownAnimProgress_) * 0.15f;
}

void NUIDropdown::renderDropdownListInternal(NUIRenderer& renderer) {
    if (items_.empty()) return;

    auto bounds = getBounds();
    float itemHeight = 36.0f; // room for larger text
    int visibleItems = std::min(maxVisibleItems_, static_cast<int>(items_.size()));
    float listHeight = itemHeight * visibleItems;

    NUIRect dropdownBounds(bounds.x, bounds.y + bounds.height + 2.0f, bounds.width, listHeight);

    renderer.setOpacity(1.0f);
    renderer.pushTransform(0, 0, 0, 1.0f);

    // Build a simple cache of measured text widths for visible items to avoid repeated expensive measureText() calls.
    if (!itemWidthCacheValid_) {
        itemTextWidthCache_.clear();
        itemTextWidthCache_.reserve(items_.size());
        float fontSize = 18.0f;
        if (auto th = getTheme()) fontSize = th->getFontSize("large");
        for (const auto& it : items_) {
            if (it) {
                float w = static_cast<float>(renderer.measureText(it->getText(), fontSize).width);
                itemTextWidthCache_.push_back(w);
            } else {
                itemTextWidthCache_.push_back(0.0f);
            }
        }
        itemWidthCacheValid_ = true;
    }

    // Draw shadow (darker and larger)
    NUIRect shadowBounds = dropdownBounds;
    shadowBounds.x += 0.0f;
    shadowBounds.y += 2.0f;
    shadowBounds.width += 2.0f;
    shadowBounds.height += 2.0f;
    renderer.fillRoundedRect(shadowBounds, 6.0f, NUIColor(0,0,0,0.4f));

    // Draw background
    renderer.fillRoundedRect(dropdownBounds, 6.0f, backgroundColor_);
    
    // Draw outer border (black)
    renderer.strokeRoundedRect(dropdownBounds, 6.0f, 2.0f, NUIColor(0.0f, 0.0f, 0.0f, 0.6f));
    
    // Draw inner border (themed)
    renderer.strokeRoundedRect(dropdownBounds, 6.0f, 1.0f, borderColor_.withAlpha(0.5f));

    // Render items with dividers
    for (int i = 0; i < visibleItems && i < static_cast<int>(items_.size()); ++i) {
        NUIRect itemBounds(dropdownBounds.x, dropdownBounds.y + i * itemHeight, dropdownBounds.width, itemHeight);
        bool isSelected = (i == selectedIndex_);
        bool isHovered = (i == hoveredIndex_);
        renderItem(renderer, i, itemBounds, isSelected, isHovered);
        
        // Draw divider line between items (except after last item)
        if (i < visibleItems - 1) {
            float dividerY = itemBounds.y + itemBounds.height;
            float dividerPadding = 8.0f;
            NUIPoint p1(itemBounds.x + dividerPadding, dividerY);
            NUIPoint p2(itemBounds.x + itemBounds.width - dividerPadding, dividerY);
            // Use black dividers for better visibility
            renderer.drawLine(p1, p2, 1.0f, NUIColor(0.0f, 0.0f, 0.0f, 0.4f));
        }
    }

    renderer.popTransform();
}

void NUIDropdown::renderDropdownList(NUIRenderer& renderer) {
    if (items_.empty()) return;

    NOMAD_ZONE("Dropdown_RenderList");

    auto bounds = getBounds();
    float itemHeight = 36.0f;
    int visibleItems = std::min(maxVisibleItems_, static_cast<int>(items_.size()));
    float listHeight = itemHeight * visibleItems;
    NUIRect dropdownBounds(bounds.x, bounds.y + bounds.height + 2.0f, bounds.width, listHeight);

    // Try to use cached texture when available
    if (cachedTextureId_ != 0 && !cacheDirty_) {
        // Draw cached texture directly
        renderer.drawTexture(cachedTextureId_, dropdownBounds, NUIRect(0,0,cachedTextureWidth_, cachedTextureHeight_));
        return;
    }

    // No automatic cache generation available in this build; render directly.
    renderDropdownListInternal(renderer);
}

void NUIDropdown::renderItem(NUIRenderer& renderer, int index, const NUIRect& bounds, bool isSelected, bool isHovered) {
    auto item = items_[index];

    // Draw selection/hover background
    if (isSelected) {
        renderer.fillRect(bounds, selectedColor_);
    } else if (isHovered) {
        renderer.fillRect(bounds, hoverColor_);
    }

    NUIColor curText = item->isEnabled() ? textColor_ : textColor_.withAlpha(0.5f);
    float padding = 12.0f;
    
    NUIRect textBounds = bounds;
    textBounds.x += padding;
    textBounds.width -= (padding * 2 + 20.0f); // Large safety margin on right
    textBounds.y += 2.0f;
    textBounds.height -= 4.0f;

    float fontSize = 18.0f;
    if (auto th = getTheme()) fontSize = th->getFontSize("large");

        // Truncate text with ellipsis if too long. Use cached measured width and a simple heuristic to reduce measureText calls.
        if (textBounds.width > 20.0f && textBounds.height > 0) {
            float maxWidth = textBounds.width - 10.0f; // Extra safety margin
            std::string displayText = item->getText();
            float measuredFull = 0.0f;
            if (index >= 0 && index < static_cast<int>(itemTextWidthCache_.size())) measuredFull = itemTextWidthCache_[index];
            
            if (measuredFull <= maxWidth) {
                // full text fits
            } else {
                // Estimate average char width and truncate roughly, then refine once
                float avgChar = measuredFull / std::max(1u, static_cast<unsigned int>(displayText.length()));
                int allowed = std::max(1, static_cast<int>(maxWidth / avgChar) - 3);
                if (allowed < 1) allowed = 1;
                if (allowed < static_cast<int>(displayText.length())) {
                    displayText = displayText.substr(0, allowed) + "...";
                }
                // One refinement measurement
                NUISize finalSize = renderer.measureText(displayText, fontSize);
                while (finalSize.width > maxWidth && displayText.length() > 4) {
                    // chop a few more characters
                    displayText = displayText.substr(0, displayText.length() - 4);
                    displayText = displayText + "...";
                    finalSize = renderer.measureText(displayText, fontSize);
                }
            }
        
        // Left-aligned text with vertical centering (top-left Y positioning)
        float textY = bounds.y + (bounds.height - fontSize) * 0.5f;
        renderer.drawText(displayText, NUIPoint(textBounds.x, textY), fontSize, curText);
    }
}

int NUIDropdown::getItemUnderMouse(const NUIPoint& mousePos) const {
    if (!isOpen_) return -1;
    auto bounds = getBounds();
    float itemHeight = 32.0f; // Match the updated item height
    int visibleItems = std::min(maxVisibleItems_, static_cast<int>(items_.size()));
    NUIRect dropdownBounds(bounds.x, bounds.y + bounds.height + 2.0f, bounds.width, itemHeight * visibleItems);
    if (!dropdownBounds.contains(mousePos)) return -1;
    float localY = mousePos.y - dropdownBounds.y;
    int index = static_cast<int>(localY / itemHeight);
    if (index >= 0 && index < static_cast<int>(items_.size())) return index;
    return -1;
}

} // namespace NomadUI
