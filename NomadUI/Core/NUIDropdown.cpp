// © 2025 Nomad Studios – All Rights Reserved. Licensed for personal & educational use only.
#include "NUIDropdown.h"
#include "../Graphics/NUIRenderer.h"
#include "NUITheme.h"
#include <algorithm>
#include <cmath>

namespace NomadUI {

    NUIDropdown::NUIDropdown() 
        : selectedIndex(0)
        , isOpen(false)
        , lastMousePos(0, 0)
        , hoveredIndex(std::nullopt)
        , itemHeight(30.0f)
        , maxVisibleItems(10)
        , dropdownAnimationProgress(0.0f)
        , animationEnabled(true)
        , scrollOffset(0)
        , scrollbarWidth(8.0f)
        , needsScrollbar(false)
        , searchEnabled(true)
        , searchTimeout(0.0f)
        , placeholderText("Select an option...")
    {
        currentSelectionLabel = std::make_shared<NUILabel>();
        updateCurrentSelectionLabel();
    }

    NUIDropdown::~NUIDropdown() {
        items.clear();
    }

    // ============================================================
    // Item Management
    // ============================================================

    void NUIDropdown::addItem(const std::string& text) {
        addItem(text, static_cast<int>(items.size()), nullptr);
    }

    void NUIDropdown::addItem(const std::string& text, const std::function<void()>& callback) {
        addItem(text, static_cast<int>(items.size()), callback);
    }

    void NUIDropdown::addItem(const std::string& text, int value) {
        addItem(text, value, nullptr);
    }

    void NUIDropdown::addItem(const std::string& text, int value, const std::function<void()>& callback) {
        DropdownItem item(text, value);
        item.callback = callback;
        items.push_back(item);
        
        if (items.size() == 1) {
            selectedIndex = 0;
            updateCurrentSelectionLabel();
        }
        
        needsScrollbar = items.size() > maxVisibleItems;
    }

    void NUIDropdown::addItem(const DropdownItem& item) {
        items.push_back(item);
        
        if (items.size() == 1) {
            selectedIndex = 0;
            updateCurrentSelectionLabel();
        }
        
        needsScrollbar = items.size() > maxVisibleItems;
    }

    void NUIDropdown::removeItem(size_t index) {
        if (index < items.size()) {
            items.erase(items.begin() + index);
            
            if (selectedIndex >= items.size()) {
                selectedIndex = items.empty() ? 0 : items.size() - 1;
                updateCurrentSelectionLabel();
            }
            
            needsScrollbar = items.size() > maxVisibleItems;
        }
    }

    void NUIDropdown::clear() {
        items.clear();
        selectedIndex = 0;
        scrollOffset = 0;
        updateCurrentSelectionLabel();
        needsScrollbar = false;
    }

    void NUIDropdown::clearItems() {
        clear();
    }

    // ============================================================
    // Selection Management
    // ============================================================

    void NUIDropdown::setSelectedIndex(size_t index) {
        if (items.empty() || index >= items.size() || index == selectedIndex) {
            return;
        }
        
        if (!items[index].enabled) {
            return;  // Cannot select disabled items
        }
        
        selectedIndex = index;
        updateCurrentSelectionLabel();
        notifySelectionChanged();
        
        if (items[selectedIndex].callback) {
            items[selectedIndex].callback();
        }
    }

    void NUIDropdown::setSelectedIndex(int index) {
        if (index < 0) return;
        setSelectedIndex(static_cast<size_t>(index));
    }

    void NUIDropdown::setSelectedByValue(int value) {
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i].value == value) {
                setSelectedIndex(i);
                return;
            }
        }
    }

    size_t NUIDropdown::getSelectedIndex() const {
        return selectedIndex;
    }

    std::string NUIDropdown::getSelectedText() const {
        if (!items.empty() && selectedIndex < items.size()) {
            return items[selectedIndex].text;
        }
        return "";
    }

    int NUIDropdown::getSelectedValue() const {
        if (!items.empty() && selectedIndex < items.size()) {
            return items[selectedIndex].value;
        }
        return 0;
    }

    std::optional<DropdownItem> NUIDropdown::getSelectedItem() const {
        if (!items.empty() && selectedIndex < items.size()) {
            return items[selectedIndex];
        }
        return std::nullopt;
    }

    // ============================================================
    // Item Access
    // ============================================================

    int NUIDropdown::getItemCount() const {
        return static_cast<int>(items.size());
    }

    const std::vector<DropdownItem>& NUIDropdown::getItems() const {
        return items;
    }

    std::optional<DropdownItem> NUIDropdown::getItem(size_t index) const {
        if (index < items.size()) {
            return items[index];
        }
        return std::nullopt;
    }

    // ============================================================
    // Configuration
    // ============================================================

    void NUIDropdown::setPlaceholderText(const std::string& text) {
        placeholderText = text;
        if (items.empty()) {
            updateCurrentSelectionLabel();
        }
    }

    void NUIDropdown::setItemHeight(float height) {
        itemHeight = std::max(20.0f, height);
        needsScrollbar = items.size() > maxVisibleItems;
    }

    void NUIDropdown::setMaxVisibleItems(size_t count) {
        maxVisibleItems = std::max(size_t(1), count);
        needsScrollbar = items.size() > maxVisibleItems;
    }

    void NUIDropdown::setSearchEnabled(bool enabled) {
        searchEnabled = enabled;
        if (!enabled) {
            searchBuffer.clear();
            searchTimeout = 0.0f;
        }
    }

    void NUIDropdown::setAnimationEnabled(bool enabled) {
        animationEnabled = enabled;
        if (!enabled) {
            dropdownAnimationProgress = isOpen ? 1.0f : 0.0f;
        }
    }

    // ============================================================
    // Dropdown State
    // ============================================================

    void NUIDropdown::openDropdown() {
        if (!isOpen && !items.empty()) {
            isOpen = true;
            scrollToItem(selectedIndex);
            
            if (onDropdownOpenedCallback) {
                onDropdownOpenedCallback();
            }
        }
    }

    void NUIDropdown::closeDropdown() {
        if (isOpen) {
            isOpen = false;
            hoveredIndex = std::nullopt;
            searchBuffer.clear();
            searchTimeout = 0.0f;
            
            if (onDropdownClosedCallback) {
                onDropdownClosedCallback();
            }
        }
    }

    void NUIDropdown::toggleDropdown() {
        if (isOpen) {
            closeDropdown();
        } else {
            openDropdown();
        }
    }

    // ============================================================
    // Event Callbacks
    // ============================================================

    void NUIDropdown::setOnSelectionChanged(const std::function<void(size_t)>& callback) {
        onSelectionChangedCallback = callback;
    }

    void NUIDropdown::setOnSelectionChanged(const std::function<void(int, int, const std::string&)>& callback) {
        onSelectionChangedFullCallback = callback;
    }

    void NUIDropdown::setOnSelectionChangedEx(const std::function<void(const SelectionChangedEvent&)>& callback) {
        onSelectionChangedExCallback = callback;
    }

    void NUIDropdown::setOnDropdownOpened(const std::function<void()>& callback) {
        onDropdownOpenedCallback = callback;
    }

    void NUIDropdown::setOnDropdownClosed(const std::function<void()>& callback) {
        onDropdownClosedCallback = callback;
    }

    // ============================================================
    // Component Events
    // ============================================================

    void NUIDropdown::onMouseEnter() {
        NUIComponent::onMouseEnter();
    }

    void NUIDropdown::onMouseLeave() {
        NUIComponent::onMouseLeave();
        
        // Close dropdown if mouse leaves both button and list
        if (isOpen && !isPointInDropdownList(lastMousePos) && !isPointInMainButton(lastMousePos)) {
            closeDropdown();
        }
    }

    bool NUIDropdown::onMouseEvent(const NUIMouseEvent& event) {
        lastMousePos = event.position;
        
        // Handle mouse wheel for scrolling
        if (isOpen && event.wheelDelta != 0) {
            if (isPointInDropdownList(event.position)) {
                int newOffset = static_cast<int>(scrollOffset) - event.wheelDelta;
                scrollOffset = std::clamp(newOffset, 0, static_cast<int>(items.size()) - static_cast<int>(maxVisibleItems));
                return true;
            }
        }
        
        // Handle clicks
        if (event.pressed && event.button == NUIMouseButton::Left) {
            if (isPointInMainButton(event.position)) {
                toggleDropdown();
                return true;
            } else if (isOpen && isPointInDropdownList(event.position)) {
                auto clickedIndex = getItemIndexAtPoint(event.position);
                if (clickedIndex.has_value() && items[*clickedIndex].enabled) {
                    setSelectedIndex(*clickedIndex);
                }
                closeDropdown();
                return true;
            } else if (isOpen) {
                // Click outside - close dropdown
                closeDropdown();
                return false;  // Allow event to propagate
            }
        }
        
        // Update hovered item
        if (isOpen) {
            hoveredIndex = getItemIndexAtPoint(event.position);
        }
        
        return NUIComponent::onMouseEvent(event);
    }

    bool NUIDropdown::onKeyEvent(const NUIKeyEvent& event) {
        if (!event.pressed) {
            return NUIComponent::onKeyEvent(event);
        }
        
        // Handle dropdown toggle
        if (event.keyCode == NUIKeyCode::Space || event.keyCode == NUIKeyCode::Enter) {
            if (!isOpen) {
                openDropdown();
                return true;
            } else if (hoveredIndex.has_value()) {
                setSelectedIndex(*hoveredIndex);
                closeDropdown();
                return true;
            }
        }
        
        // Handle escape
        if (event.keyCode == NUIKeyCode::Escape && isOpen) {
            closeDropdown();
            return true;
        }
        
        // Handle arrow keys
        if (event.keyCode == NUIKeyCode::Up) {
            if (!isOpen) {
                openDropdown();
            }
            selectPreviousItem();
            return true;
        }
        
        if (event.keyCode == NUIKeyCode::Down) {
            if (!isOpen) {
                openDropdown();
            }
            selectNextItem();
            return true;
        }
        
        if (event.keyCode == NUIKeyCode::Home) {
            selectFirstItem();
            return true;
        }
        
        if (event.keyCode == NUIKeyCode::End) {
            selectLastItem();
            return true;
        }
        
        // Handle type-to-search
        if (searchEnabled && isOpen && event.character != '\0') {
            handleSearchCharacter(event.character);
            return true;
        }
        
        return NUIComponent::onKeyEvent(event);
    }

    void NUIDropdown::onRender(NUIRenderer& renderer) {
        renderMainButton(renderer);
        
        if (dropdownAnimationProgress > 0.0f) {
            renderDropdownList(renderer);
        }
    }

    void NUIDropdown::onUpdate(double deltaTime) {
        NUIComponent::onUpdate(deltaTime);
        
        // Update animation
        if (animationEnabled) {
            updateAnimation(static_cast<float>(deltaTime));
        }
        
        // Update search timeout
        if (searchTimeout > 0.0f) {
            searchTimeout -= static_cast<float>(deltaTime);
            if (searchTimeout <= 0.0f) {
                searchBuffer.clear();
                searchTimeout = 0.0f;
            }
        }
    }

    // ============================================================
    // Helper Methods
    // ============================================================

    void NUIDropdown::updateCurrentSelectionLabel() {
        std::string text = items.empty() ? placeholderText : 
                          (selectedIndex < items.size() ? items[selectedIndex].text : "");
        currentSelectionLabel->setText(text);
    }

    void NUIDropdown::notifySelectionChanged() {
        if (items.empty() || selectedIndex >= items.size()) {
            return;
        }
        
        const auto& item = items[selectedIndex];
        
        if (onSelectionChangedCallback) {
            onSelectionChangedCallback(selectedIndex);
        }
        
        if (onSelectionChangedFullCallback) {
            onSelectionChangedFullCallback(static_cast<int>(selectedIndex), item.value, item.text);
        }
        
        if (onSelectionChangedExCallback) {
            SelectionChangedEvent event;
            event.index = selectedIndex;
            event.value = item.value;
            event.text = item.text;
            onSelectionChangedExCallback(event);
        }
    }

    bool NUIDropdown::isPointInDropdownList(const NUIPoint& point) const {
        if (!isOpen || items.empty()) return false;

        float dropdownY = getY() + getHeight();
        float dropdownHeight = getDropdownHeight();

        return point.x >= getX() && point.x <= getX() + getWidth() &&
               point.y >= dropdownY && point.y <= dropdownY + dropdownHeight;
    }

    bool NUIDropdown::isPointInMainButton(const NUIPoint& point) const {
        return point.x >= getX() && point.x <= getX() + getWidth() &&
               point.y >= getY() && point.y <= getY() + getHeight();
    }

    std::optional<size_t> NUIDropdown::getItemIndexAtPoint(const NUIPoint& point) const {
        if (!isPointInDropdownList(point)) {
            return std::nullopt;
        }

        float dropdownY = getY() + getHeight();
        float relativeY = point.y - dropdownY;
        size_t index = scrollOffset + static_cast<size_t>(relativeY / itemHeight);
        
        if (index < items.size()) {
            return index;
        }
        
        return std::nullopt;
    }

    // ============================================================
    // Rendering Helpers
    // ============================================================

    void NUIDropdown::renderMainButton(NUIRenderer& renderer) {
        NUIRect rect = {getX(), getY(), getWidth(), getHeight()};
        
        // Determine colors based on state
        NUIColor bgColor = getTheme()->getColor("dropdown.background", NUIColor(0.2f, 0.2f, 0.2f, 1.0f));
        NUIColor borderColor = getTheme()->getColor("dropdown.border", NUIColor(0.4f, 0.4f, 0.4f, 1.0f));
        
        if (isHovered() || isOpen) {
            bgColor = getTheme()->getColor("dropdown.hover", NUIColor(0.25f, 0.25f, 0.25f, 1.0f));
        }
        
        // Draw background and border
        renderer.fillRect(rect, bgColor);
        renderer.strokeRect(rect, 1.0f, borderColor);

        // Draw current selection text with proper baseline alignment
        float textX = getX() + 10.0f;
        float fontSize = getTheme()->getFontSizeNormal();
        
        // Measure text for true vertical centering (top-left Y positioning)
        std::string displayText = items.empty() ? placeholderText : items[selectedIndex].text;
        NUISize textSize = renderer.measureText(displayText, fontSize);
        float textY = getY() + (getHeight() - fontSize) * 0.5f;
        
        NUIColor textColor = items.empty() ? 
            getTheme()->getColor("dropdown.placeholder", NUIColor(0.5f, 0.5f, 0.5f, 1.0f)) :
            getTheme()->getText();
        
        renderer.drawText(displayText, NUIPoint(textX, textY), fontSize, textColor);

        // Draw dropdown arrow
        float arrowSize = 8.0f;
        float arrowX = getX() + getWidth() - arrowSize - 10.0f;
        float arrowY = getY() + (getHeight() - arrowSize) / 2.0f;
        
        float arrowRotation = isOpen ? 180.0f : 0.0f;
        
        NUIPoint p1(arrowX, arrowY);
        NUIPoint p2(arrowX + arrowSize, arrowY);
        NUIPoint p3(arrowX + arrowSize / 2.0f, arrowY + arrowSize);
        
        NUIColor arrowColor = getTheme()->getColor("dropdown.arrow", NUIColor(0.8f, 0.8f, 0.8f, 1.0f));
        renderer.drawLine(p1, p2, 1.5f, arrowColor);
        renderer.drawLine(p2, p3, 1.5f, arrowColor);
        renderer.drawLine(p3, p1, 1.5f, arrowColor);
    }

    void NUIDropdown::renderDropdownList(NUIRenderer& renderer) {
        if (items.empty()) return;

        float dropdownY = getY() + getHeight();
        float dropdownHeight = getDropdownHeight();
        
        // Apply animation
        if (animationEnabled && dropdownAnimationProgress < 1.0f) {
            dropdownHeight *= dropdownAnimationProgress;
        }

        // Draw dropdown background
        NUIRect listRect = {getX(), dropdownY, getWidth(), dropdownHeight};
        NUIColor listBg = getTheme()->getColor("dropdown.list.background", NUIColor(0.15f, 0.15f, 0.15f, 1.0f));
        NUIColor listBorder = getTheme()->getColor("dropdown.list.border", NUIColor(0.3f, 0.3f, 0.3f, 1.0f));
        
        renderer.fillRect(listRect, listBg);
        renderer.strokeRect(listRect, 1.0f, listBorder);

        // Draw visible items
        size_t visibleCount = getVisibleItemCount();
        for (size_t i = 0; i < visibleCount && (scrollOffset + i) < items.size(); ++i) {
            size_t itemIndex = scrollOffset + i;
            float itemY = dropdownY + i * itemHeight;
            renderItem(renderer, itemIndex, itemY);
        }

        // Draw scrollbar if needed
        if (needsScrollbar) {
            renderScrollbar(renderer);
        }
        
        // Draw search indicator with proper baseline alignment
        if (searchEnabled && !searchBuffer.empty()) {
            std::string searchText = "Search: " + searchBuffer;
            float fontSize = getTheme()->getFontSizeSmall();
            
            // Create rect for search text and calculate proper baseline Y
            NUISize searchSize = renderer.measureText(searchText, fontSize);
            float searchY = dropdownY + dropdownHeight + 5.0f + (fontSize + 4.0f - searchSize.height) * 0.5f + searchSize.height;
            
            renderer.drawText(searchText, NUIPoint(getX() + 5.0f, searchY), 
                            fontSize, getTheme()->getText());
        }
    }

    void NUIDropdown::renderItem(NUIRenderer& renderer, size_t index, float y) {
        NUIRect itemRect = {getX(), y, getWidth() - (needsScrollbar ? scrollbarWidth : 0.0f), itemHeight};
        
        const auto& item = items[index];
        
        // Highlight selected or hovered item
        bool isHoveredItem = hoveredIndex.has_value() && *hoveredIndex == index;
        bool isSelectedItem = index == selectedIndex;
        
        if (isHoveredItem && item.enabled) {
            NUIColor hoverColor = getTheme()->getColor("dropdown.item.hover", NUIColor(0.3f, 0.3f, 0.3f, 1.0f));
            renderer.fillRect(itemRect, hoverColor);
        } else if (isSelectedItem) {
            NUIColor selectedColor = getTheme()->getColor("dropdown.item.selected", NUIColor(0.25f, 0.35f, 0.45f, 1.0f));
            renderer.fillRect(itemRect, selectedColor);
        }

        // Draw item text with proper baseline alignment
        NUIColor textColor = item.enabled ? 
            getTheme()->getText() :
            getTheme()->getColor("dropdown.item.disabled", NUIColor(0.4f, 0.4f, 0.4f, 1.0f));
        
        float textX = getX() + 10.0f;
        float fontSize = getTheme()->getFontSizeNormal();
        
        // Measure text for true vertical centering (top-left Y positioning)
        NUISize textSize = renderer.measureText(item.text, fontSize);
        float textY = y + (itemHeight - fontSize) * 0.5f;
        
        renderer.drawText(item.text, NUIPoint(textX, textY), fontSize, textColor);
    }

    void NUIDropdown::renderScrollbar(NUIRenderer& renderer) {
        float dropdownY = getY() + getHeight();
        float dropdownHeight = getDropdownHeight();
        
        float scrollbarX = getX() + getWidth() - scrollbarWidth;
        float scrollbarHeight = dropdownHeight * (static_cast<float>(maxVisibleItems) / items.size());
        float scrollbarY = dropdownY + (dropdownHeight - scrollbarHeight) * 
                           (static_cast<float>(scrollOffset) / (items.size() - maxVisibleItems));
        
        NUIRect scrollbarRect = {scrollbarX, scrollbarY, scrollbarWidth, scrollbarHeight};
        NUIColor scrollbarColor = getTheme()->getColor("dropdown.scrollbar", NUIColor(0.5f, 0.5f, 0.5f, 0.8f));
        
        renderer.fillRect(scrollbarRect, scrollbarColor);
    }

    // ============================================================
    // Keyboard Navigation
    // ============================================================

    void NUIDropdown::selectPreviousItem() {
        if (items.empty()) return;
        
        size_t newIndex = selectedIndex;
        do {
            if (newIndex == 0) {
                newIndex = items.size() - 1;
            } else {
                newIndex--;
            }
            
            if (items[newIndex].enabled) {
                setSelectedIndex(newIndex);
                scrollToItem(newIndex);
                break;
            }
        } while (newIndex != selectedIndex);
    }

    void NUIDropdown::selectNextItem() {
        if (items.empty()) return;
        
        size_t newIndex = selectedIndex;
        do {
            newIndex = (newIndex + 1) % items.size();
            
            if (items[newIndex].enabled) {
                setSelectedIndex(newIndex);
                scrollToItem(newIndex);
                break;
            }
        } while (newIndex != selectedIndex);
    }

    void NUIDropdown::selectFirstItem() {
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i].enabled) {
                setSelectedIndex(i);
                scrollToItem(i);
                break;
            }
        }
    }

    void NUIDropdown::selectLastItem() {
        for (int i = static_cast<int>(items.size()) - 1; i >= 0; --i) {
            if (items[i].enabled) {
                setSelectedIndex(i);
                scrollToItem(i);
                break;
            }
        }
    }

    void NUIDropdown::handleSearchCharacter(char c) {
        searchBuffer += c;
        searchTimeout = SEARCH_TIMEOUT_DURATION;
        
        // Find first item that starts with search buffer
        std::string lowerSearch = searchBuffer;
        std::transform(lowerSearch.begin(), lowerSearch.end(), lowerSearch.begin(), ::tolower);
        
        for (size_t i = 0; i < items.size(); ++i) {
            std::string lowerText = items[i].text;
            std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);
            
            if (lowerText.find(lowerSearch) == 0 && items[i].enabled) {
                hoveredIndex = i;
                scrollToItem(i);
                break;
            }
        }
    }

    // ============================================================
    // Scrolling
    // ============================================================

    void NUIDropdown::updateScrollOffset() {
        if (items.size() <= maxVisibleItems) {
            scrollOffset = 0;
            return;
        }
        
        // Ensure selected item is visible
        if (selectedIndex < scrollOffset) {
            scrollOffset = selectedIndex;
        } else if (selectedIndex >= scrollOffset + maxVisibleItems) {
            scrollOffset = selectedIndex - maxVisibleItems + 1;
        }
    }

    void NUIDropdown::scrollToItem(size_t index) {
        if (items.size() <= maxVisibleItems) {
            scrollOffset = 0;
            return;
        }
        
        if (index < scrollOffset) {
            scrollOffset = index;
        } else if (index >= scrollOffset + maxVisibleItems) {
            scrollOffset = index - maxVisibleItems + 1;
        }
    }

    size_t NUIDropdown::getVisibleItemCount() const {
        return std::min(maxVisibleItems, items.size());
    }

    float NUIDropdown::getDropdownHeight() const {
        return getVisibleItemCount() * itemHeight;
    }

    // ============================================================
    // Animation
    // ============================================================

    void NUIDropdown::updateAnimation(float deltaTime) {
        const float animationSpeed = 8.0f;
        
        float target = isOpen ? 1.0f : 0.0f;
        
        if (dropdownAnimationProgress != target) {
            float diff = target - dropdownAnimationProgress;
            dropdownAnimationProgress += diff * animationSpeed * deltaTime;
            
            // Snap to target if very close
            if (std::abs(dropdownAnimationProgress - target) < 0.01f) {
                dropdownAnimationProgress = target;
            }
        }
    }

} // namespace NomadUI
