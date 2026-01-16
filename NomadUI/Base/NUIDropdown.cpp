// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUIDropdown.h"
#include "../Graphics/NUIRenderer.h"
#include "../../NomadCore/include/NomadProfiler.h"
#include "NUIThemeSystem.h"
#include "NUITheme.h"
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
    , itemHeight_(28.0f)
    , placeholderText_("Select an option...")
    , hoveredIndex_(-1)
{
    setLayer(NUILayer::Dropdown);
    
    // Initialize colors from the active theme
    try {
        auto& mgr = NUIThemeManager::getInstance();
        const auto& props = mgr.getCurrentTheme();
        backgroundColor_ = props.surfaceRaised;
        hoverColor_ = NUIColor(0.3f, 0.3f, 0.35f, 1.0f);
        selectedColor_ = props.selected;
        borderColor_ = props.border;
        textColor_ = props.textPrimary;
        arrowColor_ = props.textSecondary;
    } catch (...) {
        backgroundColor_ = NUIColor(0.15f, 0.15f, 0.15f, 1.0f);
        hoverColor_ = NUIColor(0.3f, 0.3f, 0.35f, 1.0f);
        selectedColor_ = NUIColor(0.25f, 0.35f, 0.45f, 1.0f);
        borderColor_ = NUIColor(0.3f, 0.3f, 0.3f, 1.0f);
        textColor_ = NUIColor(0.9f, 0.9f, 0.9f, 1.0f);
        arrowColor_ = NUIColor(0.7f, 0.7f, 0.7f, 1.0f);
    }
}

NUIDropdown::~NUIDropdown() {
    if (s_openDropdown == this) {
        s_openDropdown = nullptr;
    }
}

void NUIDropdown::addItem(const std::string& text, int value) {
    items_.push_back(DropdownItem(text, value));
    setDirty(true);
    itemWidthCacheValid_ = false;
    cacheDirty_ = true;
}

void NUIDropdown::addItem(const std::string& text, const std::function<void()>& callback) {
    DropdownItem item(text, 0);
    item.callback = callback;
    items_.push_back(item);
    setDirty(true);
    itemWidthCacheValid_ = false;
    cacheDirty_ = true;
}

void NUIDropdown::addItem(const std::string& text, int value, const std::function<void()>& callback) {
    DropdownItem item(text, value);
    item.callback = callback;
    items_.push_back(item);
    setDirty(true);
    itemWidthCacheValid_ = false;
    cacheDirty_ = true;
}

void NUIDropdown::addItem(const DropdownItem& item) {
    items_.push_back(item);
    setDirty(true);
    itemWidthCacheValid_ = false;
    cacheDirty_ = true;
}

void NUIDropdown::setItemVisible(int index, bool visible) {
    if (index >= 0 && index < static_cast<int>(items_.size())) {
        items_[index].visible = visible;
        setDirty(true);
        itemWidthCacheValid_ = false;
        cacheDirty_ = true;
    }
}

void NUIDropdown::setItemEnabled(int index, bool enabled) {
    if (index >= 0 && index < static_cast<int>(items_.size())) {
        items_[index].enabled = enabled;
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
        return items_[selectedIndex_].value;
    }
    return 0;
}

std::string NUIDropdown::getSelectedText() const {
    if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(items_.size())) {
        return items_[selectedIndex_].text;
    }
    return placeholderText_;
}

std::optional<DropdownItem> NUIDropdown::getSelectedItem() const {
    if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(items_.size())) {
        return items_[selectedIndex_];
    }
    return std::nullopt;
}

void NUIDropdown::setSelectedIndex(int index) {
    if (selectedIndex_ == index) return;

    if (index >= -1 && index < static_cast<int>(items_.size())) {
        selectedIndex_ = index;
        notifySelectionChanged();
        
        if (selectedIndex_ >= 0 && items_[selectedIndex_].callback) {
            items_[selectedIndex_].callback();
        }
        
        setDirty(true);
    }
}

void NUIDropdown::setSelectedByValue(int value) {
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        if (items_[i].value == value) {
            setSelectedIndex(i);
            return;
        }
    }
}

void NUIDropdown::onRender(NUIRenderer& renderer) {
    if (!isVisible()) return;

    auto bounds = getBounds();
    float cornerRadius = 6.0f;
    renderer.fillRoundedRect(bounds, cornerRadius, backgroundColor_);

    std::string displayText = getSelectedText();
    float padding = 10.0f;
    float arrowSpace = 26.0f;

    NUIRect textBounds = bounds;
    textBounds.x += padding;
    textBounds.width -= (padding + arrowSpace);
    textBounds.y += 2.0f;
    textBounds.height -= 4.0f;

    float fontSize = 14.0f;
    if (auto th = getTheme()) {
        fontSize = th->getFontSize("large");
    }

    if (textBounds.width > 20.0f) {
        float maxWidth = textBounds.width - 4.0f;
        NUISize textSize = renderer.measureText(displayText, fontSize);
        
        if (textSize.width > maxWidth) {
            std::string truncated = displayText;
            while (truncated.length() > 3) {
                truncated.pop_back();
                NUISize truncSize = renderer.measureText(truncated + "...", fontSize);
                if (truncSize.width <= maxWidth) break;
            }
            displayText = truncated + "...";
        }
        
        NUISize finalTextSize = renderer.measureText(displayText, fontSize);
        float textY = bounds.y + (bounds.height - finalTextSize.height) * 0.5f;
        renderer.drawText(displayText, NUIPoint(textBounds.x, textY), fontSize, textColor_);
    }

    // Draw chevron arrow
    float arrowSize = 6.0f;
    float centerY = bounds.y + bounds.height / 2;
    float arrowX = bounds.x + bounds.width - padding - arrowSize - 4.0f;
    float arrowCenterX = arrowX + arrowSize / 2;
    
    float rotationRad = chevronRotation_ * 3.14159f / 180.0f;
    float cosR = std::cos(rotationRad);
    float sinR = std::sin(rotationRad);
    
    float halfWidth = arrowSize / 2;
    float halfHeight = arrowSize / 3;
    
    NUIPoint p1(arrowCenterX + (-halfWidth) * cosR - (-halfHeight) * sinR, centerY + (-halfWidth) * sinR + (-halfHeight) * cosR);
    NUIPoint p2(arrowCenterX + halfWidth * cosR - (-halfHeight) * sinR, centerY + halfWidth * sinR + (-halfHeight) * cosR);
    NUIPoint p3(arrowCenterX + 0 * cosR - halfHeight * sinR, centerY + 0 * sinR + halfHeight * cosR);
    
    float lineWidth = 1.5f;
    renderer.drawLine(p1, p3, lineWidth, arrowColor_);
    renderer.drawLine(p2, p3, lineWidth, arrowColor_);

    NUIColor outerBorder = NUIColor(0.0f, 0.0f, 0.0f, 0.6f);
    renderer.strokeRoundedRect(bounds, cornerRadius, 2.0f, outerBorder);
    renderer.strokeRoundedRect(bounds, cornerRadius, 1.0f, borderColor_.withAlpha(0.5f));

    if (isOpen_) {
        renderDropdownList(renderer);
    }
}

bool NUIDropdown::onMouseEvent(const NUIMouseEvent& event) {
    if (!isEnabled()) return false;

    auto bounds = getBounds();

    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (isOpen_) {
            int visibleItems = std::min(maxVisibleItems_, static_cast<int>(items_.size()));
            float listHeight = itemHeight_ * visibleItems;
            NUIRect listBounds(bounds.x, bounds.y + bounds.height + 2.0f, bounds.width, listHeight);
            
            if (listBounds.contains(event.position)) {
                int clickedIndex = getItemUnderMouse(event.position);
                if (clickedIndex >= 0 && clickedIndex < static_cast<int>(items_.size())) {
                    const auto& item = items_[clickedIndex];
                    if (item.enabled && item.visible) {
                        setSelectedIndex(clickedIndex);
                    }
                    closeDropdown();
                }
                return true;
            }
        }

        if (bounds.contains(event.position)) {
            if (!isOpen_ && s_openDropdown != nullptr && s_openDropdown != this) {
                return false;
            }
            bringToFront();
            toggleDropdown();
            return true;
        }

        if (isOpen_) {
            closeDropdown();
            return true;
        }
    }

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
    cacheDirty_ = true;
}

void NUIDropdown::closeDropdown() {
    if (!isOpen_) return;
    isOpen_ = false;
    hoveredIndex_ = -1;
    if (onClose_) onClose_();
    if (s_openDropdown == this) s_openDropdown = nullptr;
    setDirty(true);
}

void NUIDropdown::onUpdate(double deltaTime) {
    NUIComponent::onUpdate(deltaTime);
    
    float targetRotation = isOpen_ ? 180.0f : 0.0f;
    float diff = targetRotation - chevronRotation_;
    if (std::abs(diff) > 0.5f) {
        chevronRotation_ += diff * 0.25f;
        setDirty(true);
    } else {
        chevronRotation_ = targetRotation;
    }
    
    float targetProgress = isOpen_ ? 1.0f : 0.0f;
    dropdownAnimProgress_ += (targetProgress - dropdownAnimProgress_) * 0.15f;
}

void NUIDropdown::renderDropdownListInternal(NUIRenderer& renderer) {
    if (items_.empty()) return;

    auto bounds = getBounds();
    int visibleItems = std::min(maxVisibleItems_, static_cast<int>(items_.size()));
    float listHeight = itemHeight_ * visibleItems;

    NUIRect dropdownBounds(bounds.x, bounds.y + bounds.height + 2.0f, bounds.width, listHeight);

    renderer.setOpacity(1.0f);
    renderer.pushTransform(0, 0, 0, 1.0f);

    if (!itemWidthCacheValid_) {
        itemTextWidthCache_.clear();
        itemTextWidthCache_.reserve(items_.size());
        float fontSize = 14.0f;
        if (auto th = getTheme()) fontSize = th->getFontSize("large");
        for (const auto& it : items_) {
            float w = static_cast<float>(renderer.measureText(it.text, fontSize).width);
            itemTextWidthCache_.push_back(w);
        }
        itemWidthCacheValid_ = true;
    }

    NUIRect shadowBounds = dropdownBounds;
    shadowBounds.y += 2.0f;
    shadowBounds.width += 2.0f;
    shadowBounds.height += 2.0f;
    renderer.fillRoundedRect(shadowBounds, 6.0f, NUIColor(0,0,0,0.4f));

    renderer.fillRoundedRect(dropdownBounds, 6.0f, backgroundColor_);
    renderer.strokeRoundedRect(dropdownBounds, 6.0f, 2.0f, NUIColor(0.0f, 0.0f, 0.0f, 0.6f));
    renderer.strokeRoundedRect(dropdownBounds, 6.0f, 1.0f, borderColor_.withAlpha(0.5f));

    for (int i = 0; i < visibleItems && i < static_cast<int>(items_.size()); ++i) {
        NUIRect itemBounds(dropdownBounds.x, dropdownBounds.y + i * itemHeight_, dropdownBounds.width, itemHeight_);
        bool isSelected = (i == selectedIndex_);
        bool isHovered = (i == hoveredIndex_);
        renderItem(renderer, i, itemBounds, isSelected, isHovered);
        
        if (i < visibleItems - 1) {
            float dividerY = itemBounds.y + itemBounds.height;
            float dividerPadding = 8.0f;
            renderer.drawLine(NUIPoint(itemBounds.x + dividerPadding, dividerY), 
                            NUIPoint(itemBounds.x + itemBounds.width - dividerPadding, dividerY), 
                            1.0f, NUIColor(0.0f, 0.0f, 0.0f, 0.4f));
        }
    }

    renderer.popTransform();
}

void NUIDropdown::renderDropdownList(NUIRenderer& renderer) {
    if (items_.empty()) return;
    NOMAD_ZONE("Dropdown_RenderList");

    auto bounds = getBounds();
    int visibleItems = std::min(maxVisibleItems_, static_cast<int>(items_.size()));
    float listHeight = itemHeight_ * visibleItems;
    NUIRect dropdownBounds(bounds.x, bounds.y + bounds.height + 2.0f, bounds.width, listHeight);

    if (cachedTextureId_ != 0 && !cacheDirty_) {
        renderer.drawTexture(cachedTextureId_, dropdownBounds, NUIRect(0,0,cachedTextureWidth_, cachedTextureHeight_));
        return;
    }

    renderDropdownListInternal(renderer);
}

void NUIDropdown::renderItem(NUIRenderer& renderer, int index, const NUIRect& bounds, bool isSelected, bool isHovered) {
    const auto& item = items_[index];
    if (isSelected) {
        renderer.fillRect(bounds, selectedColor_);
    } else if (isHovered) {
        renderer.fillRect(bounds, hoverColor_);
    }

    NUIColor curText = item.enabled ? textColor_ : textColor_.withAlpha(0.5f);
    float padding = 8.0f;
    
    NUIRect textBounds = bounds;
    textBounds.x += padding;
    textBounds.width -= (padding * 2 + 4.0f);
    textBounds.y += 2.0f;
    textBounds.height -= 4.0f;

    float fontSize = 14.0f;
    if (auto th = getTheme()) fontSize = th->getFontSize("large");

    if (textBounds.width > 2.0f && textBounds.height > 0) {
        float maxWidth = textBounds.width - 10.0f;
        std::string displayText = item.text;
        float measuredFull = itemTextWidthCache_[index];
        
        if (measuredFull > maxWidth) {
            float avgChar = measuredFull / std::max(1u, static_cast<unsigned int>(displayText.length()));
            int allowed = std::max(1, static_cast<int>(maxWidth / avgChar) - 3);
            if (allowed < static_cast<int>(displayText.length())) {
                displayText = displayText.substr(0, allowed) + "...";
            }
            NUISize finalSize = renderer.measureText(displayText, fontSize);
            while (finalSize.width > maxWidth && displayText.length() > 4) {
                displayText = displayText.substr(0, displayText.length() - 4) + "...";
                finalSize = renderer.measureText(displayText, fontSize);
            }
        }
        
        NUISize finalTextSize = renderer.measureText(displayText, fontSize);
        float textY = bounds.y + (bounds.height - finalTextSize.height) * 0.5f;
        renderer.drawText(displayText, NUIPoint(textBounds.x, textY), fontSize, curText);
    }
}

int NUIDropdown::getItemUnderMouse(const NUIPoint& mousePos) const {
    if (!isOpen_) return -1;
    auto bounds = getBounds();
    int visibleItems = std::min(maxVisibleItems_, static_cast<int>(items_.size()));
    NUIRect dropdownBounds(bounds.x, bounds.y + bounds.height + 2.0f, bounds.width, itemHeight_ * visibleItems);
    if (!dropdownBounds.contains(mousePos)) return -1;
    float localY = mousePos.y - dropdownBounds.y;
    int index = static_cast<int>(localY / itemHeight_);
    if (index >= 0 && index < static_cast<int>(items_.size())) return index;
    return -1;
}

void NUIDropdown::notifySelectionChanged() {
    if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(items_.size())) {
        const auto& item = items_[selectedIndex_];
        if (onSelectionChangedIndex_) onSelectionChangedIndex_(selectedIndex_);
        if (onSelectionChanged_) onSelectionChanged_(selectedIndex_, item.value, item.text);
        if (onSelectionChangedEx_) {
            SelectionChangedEvent ev;
            ev.index = static_cast<size_t>(selectedIndex_);
            ev.value = item.value;
            ev.text = item.text;
            onSelectionChangedEx_(ev);
        }
    }
}

} // namespace NomadUI
