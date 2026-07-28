// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUIDropdown.h"
#include "../Graphics/NUIRenderer.h"
#include "../../AestraCore/include/AestraProfiler.h"
#include "NUIThemeSystem.h"
#include "NUITheme.h"
#include <algorithm>
#include <cmath>

namespace AestraUI {

// Keep one shared dropdown open at a time.
static NUIDropdown* s_openDropdown = nullptr;
constexpr float kDropdownGap = 6.0f;

NUIDropdown::NUIDropdown()
    : NUIComponent()
    , selectedIndex_(-1)
    , isOpen_(false)
    , dropdownAnimProgress_(0.0f)
    , maxVisibleItems_(5)
    , itemHeight_(NUIThemeManager::getInstance().getLayoutDimension("standardMenuRowHeight"))
    , placeholderText_("Select an option...")
    , hoveredIndex_(-1)
{
    setLayer(NUILayer::Dropdown);
    
    // Initialize colors from the active theme
    try {
        auto& mgr = NUIThemeManager::getInstance();
        const auto& props = mgr.getCurrentTheme();
        backgroundColor_ = props.surfaceTertiary; // keep in sync with refreshThemeColors()
        hoverColor_ = props.buttonBgHover;
        selectedColor_ = props.selected;
        borderColor_ = props.border;
        textColor_ = props.textPrimary;
        arrowColor_ = props.textSecondary;
    } catch (const std::exception& e) {
        // Fallback to defaults if theme retrieval fails
        backgroundColor_ = NUIColor(0.15f, 0.15f, 0.15f, 1.0f);
        hoverColor_ = NUIColor(0.22f, 0.24f, 0.30f, 1.0f);
        selectedColor_ = NUIColor(0.25f, 0.35f, 0.45f, 1.0f);
        borderColor_ = NUIColor(0.3f, 0.3f, 0.3f, 1.0f);
        textColor_ = NUIColor(0.9f, 0.9f, 0.9f, 1.0f);
        arrowColor_ = NUIColor(0.7f, 0.7f, 0.7f, 1.0f);
    }
}

void NUIDropdown::refreshThemeColors() {
    const auto& props = NUIThemeManager::getInstance().getCurrentTheme();
    auto assignIfChanged = [this](NUIColor& target, const NUIColor& value, bool custom) {
        if (custom) return;
        if (target.r != value.r || target.g != value.g || target.b != value.b || target.a != value.a) {
            target = value;
            cacheDirty_ = true;
            setDirty(true);
        }
    };
    assignIfChanged(backgroundColor_, props.surfaceTertiary, customBackground_);
    assignIfChanged(hoverColor_, props.buttonBgHover, customHover_);
    assignIfChanged(selectedColor_, props.selected, customSelected_);
    assignIfChanged(borderColor_, props.borderSubtle, customBorder_);
    assignIfChanged(textColor_, props.textPrimary, customText_);
    assignIfChanged(arrowColor_, props.textSecondary, customArrow_);
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
        if (items_[index].visible == visible)
            return;
        items_[index].visible = visible;
        if (!visible && selectedIndex_ == index) {
            selectedIndex_ = -1;
        }
        if (!visible && hoveredIndex_ == index) {
            hoveredIndex_ = -1;
        }
        clampScrollOffset();
        if (isOpen_ && getVisibleItemCount() == 0) {
            closeDropdown();
        }
        setDirty(true);
        itemWidthCacheValid_ = false;
        cacheDirty_ = true;
    }
}

void NUIDropdown::setItemEnabled(int index, bool enabled) {
    if (index >= 0 && index < static_cast<int>(items_.size())) {
        if (items_[index].enabled == enabled)
            return;
        items_[index].enabled = enabled;
        if (!enabled && hoveredIndex_ == index) {
            hoveredIndex_ = getNextSelectableIndex(index, 1);
            ensureItemVisible(hoveredIndex_);
        }
        setDirty(true);
        cacheDirty_ = true;
    }
}

void NUIDropdown::clearItems() {
    closeDropdown();
    items_.clear();
    selectedIndex_ = -1;
    hoveredIndex_ = -1;
    scrollOffset_ = 0;
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
    if (selectedIndex_ == index)
        return;

    if (index >= -1 && index < static_cast<int>(items_.size()) && (index == -1 || items_[index].visible)) {
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
        if (items_[i].visible && items_[i].value == value) {
            setSelectedIndex(i);
            return;
        }
    }
    setSelectedIndex(-1);
}

void NUIDropdown::setMaxVisibleItems(int count) {
    const int clamped = std::max(1, count);
    if (maxVisibleItems_ == clamped)
        return;
    maxVisibleItems_ = clamped;
    clampScrollOffset();
    ensureItemVisible(hoveredIndex_ >= 0 ? hoveredIndex_ : selectedIndex_);
    setDirty(true);
    cacheDirty_ = true;
}

void NUIDropdown::setItemHeight(float height) {
    const float clamped = std::max(1.0f, height);
    if (itemHeight_ == clamped)
        return;
    itemHeight_ = clamped;
    setDirty(true);
    cacheDirty_ = true;
}

void NUIDropdown::onRender(NUIRenderer& renderer) {
    if (!isVisible()) return;

    refreshThemeColors();
    auto& themeManager = NUIThemeManager::getInstance();
    const auto& props = themeManager.getCurrentTheme();
    auto bounds = getBounds();
    const float cornerRadius = props.radiusM;
    const bool hovered = isEnabled() && isHovered();
    const NUIColor bg = hovered ? backgroundColor_.withAlpha(0.98f) : backgroundColor_.withAlpha(0.94f);
    const NUIColor mainBorder = !isEnabled() ? borderColor_.withAlpha(0.42f)
                                : isFocused() ? themeManager.getColor("focusRing")
                                : hovered     ? themeManager.getColor("borderStrong")
                                              : borderColor_;

    renderer.fillRoundedRect(bounds, cornerRadius, bg);
    renderer.strokeRoundedRect(bounds, cornerRadius, isFocused() && isEnabled() ? 1.5f : 1.0f, mainBorder);

    std::string displayText = getSelectedText();
    const float padding = props.spacingS;
    const float arrowSpace = props.layout.standardControlHeight;

    NUIRect textBounds = bounds;
    textBounds.x += padding;
    textBounds.width -= (padding + arrowSpace);
    textBounds.y += 2.0f;
    textBounds.height -= 4.0f;

    float fontSize = props.fontSizeM;
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
        renderer.drawText(displayText, NUIPoint(textBounds.x, textY), fontSize,
                          isEnabled() ? textColor_ : props.textDisabled);
    }

    const NUIRect arrowWell{
        bounds.right() - arrowSpace,
        bounds.y + 2.0f,
        arrowSpace - 4.0f,
        bounds.height - 4.0f
    };
    renderer.fillRoundedRect(arrowWell, props.radiusS,
                             hovered ? themeManager.getColor("controlHover") : NUIColor::transparent());

    // Draw chevron arrow
    float arrowSize = 6.0f;
    float centerY = bounds.y + bounds.height / 2;
    float arrowX = arrowWell.x + (arrowWell.width - arrowSize) * 0.5f;
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

    if (isOpen_) {
        renderDropdownList(renderer);
    }
}

bool NUIDropdown::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible() || !isEnabled())
        return false;

    auto bounds = getBounds();

    if (isOpen_ && event.wheelDelta != 0.0f) {
        const NUIRect listBounds = getDropdownBounds();
        if (listBounds.contains(event.position)) {
            const int previousOffset = scrollOffset_;
            scrollOffset_ += event.wheelDelta > 0.0f ? -1 : 1;
            clampScrollOffset();
            hoveredIndex_ = getItemUnderMouse(event.position);
            if (scrollOffset_ != previousOffset) {
                setDirty(true);
                cacheDirty_ = true;
            }
            return true;
        }
    }

    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (isOpen_) {
            const NUIRect listBounds = getDropdownBounds();

            if (listBounds.contains(event.position)) {
                int clickedIndex = getItemUnderMouse(event.position);
                if (clickedIndex >= 0 && clickedIndex < static_cast<int>(items_.size())) {
                    const auto& item = items_[clickedIndex];
                    if (item.enabled && item.visible) {
                        setSelectedIndex(clickedIndex);
                        closeDropdown();
                    }
                }
                return true;
            }
        }

        if (bounds.contains(event.position)) {
            if (!isOpen_ && s_openDropdown != nullptr && s_openDropdown != this) {
                s_openDropdown->closeDropdown();
            }
            bringToFront();
            setFocused(true);
            toggleDropdown();
            return true;
        }

        if (isOpen_) {
            closeDropdown();
            // Let the same press continue through the parent dispatch so a
            // sibling control can activate without requiring a second click.
            return false;
        }
    }

    if (isOpen_) {
        hoveredIndex_ = getItemUnderMouse(event.position);
        setDirty(true);
    }

    return false;
}

bool NUIDropdown::onKeyEvent(const NUIKeyEvent& event) {
    if (!isEnabled() || !isFocused())
        return false;

    if (event.pressed) {
        switch (event.keyCode) {
        case NUIKeyCode::Enter:
        case NUIKeyCode::Space:
            if (!isOpen_) {
                openDropdown();
            } else {
                if (hoveredIndex_ >= 0 && items_[hoveredIndex_].visible && items_[hoveredIndex_].enabled) {
                    setSelectedIndex(hoveredIndex_);
                }
                closeDropdown();
            }
            return true;
        case NUIKeyCode::Escape:
            if (isOpen_) {
                closeDropdown();
                return true;
            }
            break;
        case NUIKeyCode::Up:
            if (isOpen_) {
                hoveredIndex_ = getNextSelectableIndex(hoveredIndex_ >= 0 ? hoveredIndex_ : selectedIndex_, -1);
                ensureItemVisible(hoveredIndex_);
                setDirty(true);
                return true;
            }
            break;
        case NUIKeyCode::Down:
            if (isOpen_) {
                hoveredIndex_ = getNextSelectableIndex(hoveredIndex_ >= 0 ? hoveredIndex_ : selectedIndex_, 1);
                ensureItemVisible(hoveredIndex_);
                setDirty(true);
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
    if (isOpen_ || getVisibleItemCount() == 0)
        return;
    if (s_openDropdown && s_openDropdown != this) {
        s_openDropdown->closeDropdown();
    }
    NUIComponent::hideRemoteTooltip();
    isOpen_ = true;
    hoveredIndex_ =
        selectedIndex_ >= 0 && items_[selectedIndex_].enabled ? selectedIndex_ : getNextSelectableIndex(-1, 1);
    ensureItemVisible(hoveredIndex_);
    if (onOpen_)
        onOpen_();
    s_openDropdown = this;
    setDirty(true);
    cacheDirty_ = true;
}

void NUIDropdown::closeDropdown() {
    if (!isOpen_)
        return;
    isOpen_ = false;
    hoveredIndex_ = -1;
    scrollOffset_ = 0;
    if (onClose_)
        onClose_();
    if (s_openDropdown == this)
        s_openDropdown = nullptr;
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
    if (getVisibleItemCount() == 0)
        return;

    refreshThemeColors();
    const auto& props = NUIThemeManager::getInstance().getCurrentTheme();
    const NUIRect dropdownBounds = getDropdownBounds();
    const int displayedRows = getDisplayedRowCount();

    renderer.setOpacity(1.0f);
    renderer.pushTransform(0, 0, 0, 1.0f);

    if (!itemWidthCacheValid_) {
        itemTextWidthCache_.clear();
        itemTextWidthCache_.reserve(items_.size());
        float fontSize = props.fontSizeM;
        if (auto th = getTheme()) fontSize = th->getFontSize("large");
        for (const auto& it : items_) {
            float w = static_cast<float>(renderer.measureText(it.text, fontSize).width);
            itemTextWidthCache_.push_back(w);
        }
        itemWidthCacheValid_ = true;
    }

    renderer.drawShadow(dropdownBounds, 0.0f, 8.0f, 18.0f, NUIColor(0,0,0,0.18f));
    renderer.fillRoundedRect(dropdownBounds, props.radiusL, backgroundColor_.withAlpha(0.985f));
    renderer.strokeRoundedRect(dropdownBounds, props.radiusL, 1.0f, borderColor_);

    for (int row = 0; row < displayedRows; ++row) {
        const int itemIndex = getItemIndexForVisibleRow(scrollOffset_ + row);
        if (itemIndex < 0)
            break;
        NUIRect itemBounds(dropdownBounds.x, dropdownBounds.y + row * itemHeight_, dropdownBounds.width, itemHeight_);
        bool isSelected = (itemIndex == selectedIndex_);
        bool isHovered = (itemIndex == hoveredIndex_);
        renderItem(renderer, itemIndex, itemBounds, isSelected, isHovered);
        
        if (row < displayedRows - 1) {
            float dividerY = itemBounds.y + itemBounds.height;
            float dividerPadding = props.spacingS;
            renderer.drawLine(NUIPoint(itemBounds.x + dividerPadding, dividerY), 
                            NUIPoint(itemBounds.x + itemBounds.width - dividerPadding, dividerY), 
                            props.layout.dividerWidth, props.divider);
        }
    }

    renderer.popTransform();
}

void NUIDropdown::renderDropdownList(NUIRenderer& renderer) {
    if (getVisibleItemCount() == 0)
        return;
    AESTRA_ZONE("Dropdown_RenderList");

    const NUIRect dropdownBounds = getDropdownBounds();

    if (cachedTextureId_ != 0 && !cacheDirty_) {
        renderer.drawTexture(cachedTextureId_, dropdownBounds, NUIRect(0,0,cachedTextureWidth_, cachedTextureHeight_));
        return;
    }

    renderDropdownListInternal(renderer);
}

void NUIDropdown::renderItem(NUIRenderer& renderer, int index, const NUIRect& bounds, bool isSelected, bool isHovered) {
    const auto& item = items_[index];
    const auto& props = NUIThemeManager::getInstance().getCurrentTheme();
    if (isSelected) {
        renderer.fillRoundedRect({bounds.x + 4.0f, bounds.y + 2.0f, bounds.width - 8.0f, bounds.height - 4.0f},
                                 props.radiusS,
                                 selectedColor_);
    } else if (isHovered) {
        renderer.fillRoundedRect({bounds.x + 4.0f, bounds.y + 2.0f, bounds.width - 8.0f, bounds.height - 4.0f},
                                 props.radiusS,
                                 hoverColor_);
    }

    NUIColor curText = item.enabled ? textColor_ : props.textDisabled;
    float padding = props.spacingS;
    
    NUIRect textBounds = bounds;
    textBounds.x += padding;
    textBounds.width -= (padding * 2 + 4.0f);
    textBounds.y += 2.0f;
    textBounds.height -= 4.0f;

    float fontSize = props.fontSizeM;
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
    if (!isOpen_)
        return -1;
    const NUIRect dropdownBounds = getDropdownBounds();
    if (!dropdownBounds.contains(mousePos))
        return -1;
    float localY = mousePos.y - dropdownBounds.y;
    const int row = static_cast<int>(localY / itemHeight_);
    return getItemIndexForVisibleRow(scrollOffset_ + row);
}

int NUIDropdown::getVisibleItemCount() const {
    return static_cast<int>(
        std::count_if(items_.begin(), items_.end(), [](const DropdownItem& item) { return item.visible; }));
}

int NUIDropdown::getItemIndexForVisibleRow(int row) const {
    if (row < 0)
        return -1;
    int visibleRow = 0;
    for (int index = 0; index < static_cast<int>(items_.size()); ++index) {
        if (!items_[index].visible)
            continue;
        if (visibleRow == row)
            return index;
        ++visibleRow;
    }
    return -1;
}

int NUIDropdown::getVisibleRowForItem(int index) const {
    if (index < 0 || index >= static_cast<int>(items_.size()) || !items_[index].visible)
        return -1;
    int visibleRow = 0;
    for (int current = 0; current < index; ++current) {
        if (items_[current].visible)
            ++visibleRow;
    }
    return visibleRow;
}

int NUIDropdown::getDisplayedRowCount() const {
    return std::min(maxVisibleItems_, getVisibleItemCount());
}

int NUIDropdown::getNextSelectableIndex(int currentIndex, int direction) const {
    if (items_.empty() || direction == 0)
        return -1;
    const int itemCount = static_cast<int>(items_.size());
    int index = currentIndex;
    for (int attempt = 0; attempt < itemCount; ++attempt) {
        index += direction > 0 ? 1 : -1;
        if (index >= itemCount)
            index = 0;
        if (index < 0)
            index = itemCount - 1;
        if (items_[index].visible && items_[index].enabled)
            return index;
    }
    return -1;
}

NUIRect NUIDropdown::getDropdownBounds() const {
    const NUIRect bounds = getBounds();
    const float listHeight = itemHeight_ * getDisplayedRowCount();
    float y = bounds.bottom() + kDropdownGap;

    const NUIComponent* root = this;
    while (root->getParent()) {
        root = root->getParent();
    }
    const NUIRect viewport = root->getBounds();
    const float aboveY = bounds.y - kDropdownGap - listHeight;
    if (viewport.height > 0.0f && y + listHeight > viewport.bottom() && aboveY >= viewport.y) {
        y = aboveY;
    }

    return {bounds.x, y, bounds.width, listHeight};
}

void NUIDropdown::clampScrollOffset() {
    const int maxOffset = std::max(0, getVisibleItemCount() - getDisplayedRowCount());
    scrollOffset_ = std::clamp(scrollOffset_, 0, maxOffset);
}

void NUIDropdown::ensureItemVisible(int index) {
    const int row = getVisibleRowForItem(index);
    if (row < 0) {
        clampScrollOffset();
        return;
    }

    const int displayedRows = getDisplayedRowCount();
    if (row < scrollOffset_) {
        scrollOffset_ = row;
    } else if (row >= scrollOffset_ + displayedRows) {
        scrollOffset_ = row - displayedRows + 1;
    }
    clampScrollOffset();
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

} // namespace AestraUI
