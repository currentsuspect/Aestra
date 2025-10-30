// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUIDropdown.h"
#include "../Graphics/NUIRenderer.h"
#include "NUITheme.h"

namespace NomadUI {

    NUIDropdown::NUIDropdown() 
        : selectedIndex(0)
        , isOpen(false)
    {
        addChild(std::make_shared<NUILabel>(currentSelectionLabel));
        updateCurrentSelectionLabel();
    }

    NUIDropdown::~NUIDropdown() {
        items.clear();
    }

    void NUIDropdown::addItem(const std::string& text) {
        addItem(text, nullptr);
    }

    void NUIDropdown::addItem(const std::string& text, const std::function<void()>& callback) {
        DropdownItem it;
        it.text = text;
        it.callback = callback;
        it.value = static_cast<int>(items.size());
        items.push_back(it);
        
        if (items.size() == 1) {
            selectedIndex = 0;
            updateCurrentSelectionLabel();
        }
    }

    void NUIDropdown::addItem(const std::string& text, int value) {
        addItem(text, value, nullptr);
    }

    void NUIDropdown::addItem(const std::string& text, int value, const std::function<void()>& callback) {
        DropdownItem it;
        it.text = text;
        it.callback = callback;
        it.value = value;
        items.push_back(it);

        if (items.size() == 1) {
            selectedIndex = 0;
            updateCurrentSelectionLabel();
        }
    }

    void NUIDropdown::removeItem(size_t index) {
        if (index < items.size()) {
            items.erase(items.begin() + index);
            
            if (selectedIndex >= items.size()) {
                selectedIndex = items.empty() ? 0 : items.size() - 1;
                updateCurrentSelectionLabel();
            }
        }
    }

    void NUIDropdown::clear() {
        items.clear();
        selectedIndex = 0;
        updateCurrentSelectionLabel();
    }

    void NUIDropdown::clearItems() {
        clear();
    }

    void NUIDropdown::setSelectedIndex(size_t index) {
        if (index < items.size() && index != selectedIndex) {
            selectedIndex = index;
            updateCurrentSelectionLabel();
            
            if (onSelectionChangedCallback) {
                onSelectionChangedCallback(selectedIndex);
            }

            if (onSelectionChangedFullCallback) {
                onSelectionChangedFullCallback(static_cast<int>(selectedIndex), items[selectedIndex].value, items[selectedIndex].text);
            }

            if (items[selectedIndex].callback) {
                items[selectedIndex].callback();
            }
        }
    }

    void NUIDropdown::setSelectedIndex(int index) {
        if (index < 0) return;
        setSelectedIndex(static_cast<size_t>(index));
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

    int NUIDropdown::getItemCount() const {
        return static_cast<int>(items.size());
    }

    void NUIDropdown::setPlaceholderText(const std::string& text) {
        // For now, forward to the internal label if empty selection
        if (items.empty()) {
            currentSelectionLabel.setText(text);
        }
    }

    void NUIDropdown::setOnSelectionChanged(const std::function<void(size_t)>& callback) {
        onSelectionChangedCallback = callback;
    }

    void NUIDropdown::setOnSelectionChanged(const std::function<void(int,int,const std::string&)>& callback) {
        onSelectionChangedFullCallback = callback;
    }

    void NUIDropdown::onMouseEnter() {
        NUIComponent::onMouseEnter();
    }

    void NUIDropdown::onMouseLeave() {
        NUIComponent::onMouseLeave();
        if (!isPointInDropdownList(NUIPoint())) {
            closeDropdown();
        }
    }

    bool NUIDropdown::onMouseEvent(const NUIMouseEvent& event) {
        if (event.pressed && event.button == NUIMouseButton::Left) {
            if (!isOpen) {
                toggleDropdown();
                return true;
            } else {
                size_t clickedIndex = getItemIndexAtPoint(event.position);
                if (clickedIndex < items.size()) {
                    setSelectedIndex(clickedIndex);
                }
                closeDropdown();
                return true;
            }
        }
        return NUIComponent::onMouseEvent(event);
    }

    void NUIDropdown::onRender(NUIRenderer& renderer) {
        // Draw main dropdown box
        NUIRect rect = {getX(), getY(), getWidth(), getHeight()};
        NUIColor defaultBg(0.2f, 0.2f, 0.2f, 1.0f);
        NUIColor defaultBorder(0.4f, 0.4f, 0.4f, 1.0f);
        renderer.fillRect(rect, getTheme()->getColor("dropdown.background", defaultBg));
        renderer.strokeRect(rect, 1.0f, getTheme()->getColor("dropdown.border", defaultBorder));

        // Draw current selection
        currentSelectionLabel.setPosition(getX() + 5, getY() + (getHeight() - currentSelectionLabel.getHeight()) / 2);
        currentSelectionLabel.onRender(renderer);

        // Draw dropdown arrow
        int arrowSize = 8;
        int arrowX = getX() + getWidth() - arrowSize - 5;
        int arrowY = getY() + (getHeight() - arrowSize) / 2;
        
        NUIPoint p1(arrowX, arrowY);
        NUIPoint p2(arrowX + arrowSize, arrowY);
        NUIPoint p3(arrowX + arrowSize/2, arrowY + arrowSize);
        NUIColor defaultArrow(0.8f, 0.8f, 0.8f, 1.0f);
        renderer.drawLine(p1, p2, 1.0f, getTheme()->getColor("dropdown.arrow", defaultArrow));
        renderer.drawLine(p2, p3, 1.0f, getTheme()->getColor("dropdown.arrow", defaultArrow));
        renderer.drawLine(p3, p1, 1.0f, getTheme()->getColor("dropdown.arrow", defaultArrow));

        // Draw dropdown list if open
        if (isOpen && !items.empty()) {
            float itemHeight = getHeight();
            float dropdownHeight = items.size() * itemHeight;
            float dropdownY = getY() + getHeight();

            // Draw dropdown background
            NUIRect listRect = {getX(), dropdownY, getWidth(), dropdownHeight};
            NUIColor defaultListBg(0.15f, 0.15f, 0.15f, 1.0f);
            NUIColor defaultListBorder(0.3f, 0.3f, 0.3f, 1.0f);
            renderer.fillRect(listRect, getTheme()->getColor("dropdown.list.background", defaultListBg));
            renderer.strokeRect(listRect, 1.0f, getTheme()->getColor("dropdown.list.border", defaultListBorder));

            // Draw items
            NUIPoint mousePos = {0, 0}; // TODO: Get actual mouse position from event system
            for (size_t i = 0; i < items.size(); i++) {
                float itemY = dropdownY + i * itemHeight;
                NUIRect itemRect = {getX(), itemY, getWidth(), itemHeight};
                
                // Highlight selected or hovered item
                if (i == selectedIndex || (isPointInDropdownList(mousePos) && i == getItemIndexAtPoint(mousePos))) {
                    NUIColor defaultHover(0.25f, 0.25f, 0.25f, 1.0f);
                    renderer.fillRect(itemRect, getTheme()->getColor("dropdown.item.hover", defaultHover));
                }

                // Draw item text
                renderer.drawText(items[i].text, 
                                NUIPoint(getX() + 5, itemY + (itemHeight - getTheme()->getFontSizeNormal()) / 2),
                                getTheme()->getFontSizeNormal(),
                                getTheme()->getText());
            }
        }
    }

    void NUIDropdown::toggleDropdown() {
        isOpen = !isOpen;
    }

    void NUIDropdown::closeDropdown() {
        isOpen = false;
    }

    bool NUIDropdown::isPointInDropdownList(const NUIPoint& point) const {
        if (!isOpen) return false;

        float dropdownY = getY() + getHeight();
        float dropdownHeight = items.size() * getHeight();

        return point.x >= getX() && point.x <= getX() + getWidth() &&
               point.y >= dropdownY && point.y <= dropdownY + dropdownHeight;
    }

    size_t NUIDropdown::getItemIndexAtPoint(const NUIPoint& point) const {
        if (!isPointInDropdownList(point)) return (size_t)-1;

        float dropdownY = getY() + getHeight();
        float relativeY = point.y - dropdownY;
        return (size_t)(relativeY / getHeight());
    }

    void NUIDropdown::updateCurrentSelectionLabel() {
        std::string text = items.empty() ? "" : items[selectedIndex].text;
        currentSelectionLabel.setText(text);
    }

} // namespace NomadUI