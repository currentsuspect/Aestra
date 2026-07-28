// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITypes.h"
#include "NUIRenderer.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <optional>

namespace AestraUI {


struct SelectionChangedEvent {
    size_t index;
    int value;
    std::string text;
};

struct DropdownItem {
    std::string text;
    int value = 0;
    bool enabled = true;
    bool visible = true;
    std::function<void()> callback;

    DropdownItem() = default;
    DropdownItem(const std::string& t, int v = 0) : text(t), value(v) {}
};

class NUIDropdown : public NUIComponent {
public:
    NUIDropdown();
    ~NUIDropdown();

    // Item management
    void addItem(const std::string& text, int value = 0);
    void addItem(const std::string& text, const std::function<void()>& callback);
    void addItem(const std::string& text, int value, const std::function<void()>& callback);
    void addItem(const DropdownItem& item);
    
    /**
     * @brief Show or hide an item.
     *
     * Hiding the CURRENTLY SELECTED item clears the selection outright — it does
     * not retain a hidden logical selection, and it does not advance to another
     * visible row. Auto-advancing would change the user's choice without asking,
     * which for a settings control is a silent intent change.
     *
     * Consumers must therefore treat "no selection" as a real state. Use
     * getSelectedItem(), which returns std::nullopt; getSelectedValue() returns
     * 0 in that state, and 0 is a legal item value, so it cannot distinguish
     * "nothing selected" from "the item whose value is 0".
     */
    void setItemVisible(int index, bool visible);
    void setItemEnabled(int index, bool enabled);
    void clearItems();

    // Visual configuration
    void setPlaceholderText(const std::string& text) { placeholderText_ = text; setDirty(true); }
    void setMaxVisibleItems(int count);
    void setItemHeight(float height);
    
    // Render dropdown list separately for proper z-order
    void renderDropdownList(NUIRenderer& renderer);
    void setBackgroundColor(const NUIColor& color) { backgroundColor_ = color; customBackground_ = true; setDirty(true); }
    void setHoverColor(const NUIColor& color) { hoverColor_ = color; customHover_ = true; setDirty(true); }
    void setSelectedColor(const NUIColor& color) { selectedColor_ = color; customSelected_ = true; setDirty(true); }
    void setBorderColor(const NUIColor& color) { borderColor_ = color; customBorder_ = true; setDirty(true); }
    void setTextColor(const NUIColor& color) { textColor_ = color; customText_ = true; setDirty(true); }
    void setArrowColor(const NUIColor& color) { arrowColor_ = color; customArrow_ = true; setDirty(true); }

    // Selection state
    int getSelectedIndex() const { return selectedIndex_; }
    void setSelectedByValue(int value);
    int getSelectedValue() const;
    std::string getSelectedText() const;
    std::optional<DropdownItem> getSelectedItem() const;
    
    bool isOpen() const { return isOpen_; }
    size_t getItemCount() const { return items_.size(); }
    void setSelectedIndex(int index);

    // Event callbacks
    void setOnOpen(std::function<void()> callback) { onOpen_ = callback; }
    void setOnClose(std::function<void()> callback) { onClose_ = callback; }
    void setOnSelectionChanged(std::function<void(int)> callback) { onSelectionChangedIndex_ = callback; }
    void setOnSelectionChanged(std::function<void(int, int, const std::string&)> callback) { onSelectionChanged_ = callback; }
    void setOnSelectionChangedEx(std::function<void(const SelectionChangedEvent&)> callback) { onSelectionChangedEx_ = callback; }

    // Overridden methods from NUIComponent
    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    bool onKeyEvent(const NUIKeyEvent& event) override;
    void onFocusGained() override;
    void onFocusLost() override;
    void onUpdate(double deltaTime) override;

protected:
    void toggleDropdown();
    void openDropdown();
    void closeDropdown();
    void updateAnimations();

private:
    void renderItem(NUIRenderer& renderer, int index, const NUIRect& bounds, bool isSelected, bool isHovered);
    int getItemUnderMouse(const NUIPoint& mousePos) const;
    int getVisibleItemCount() const;
    int getItemIndexForVisibleRow(int row) const;
    int getVisibleRowForItem(int index) const;
    int getDisplayedRowCount() const;
    int getNextSelectableIndex(int currentIndex, int direction) const;
    NUIRect getDropdownBounds() const;
    void clampScrollOffset();
    void ensureItemVisible(int index);
    void notifySelectionChanged();

    std::vector<DropdownItem> items_;
    int selectedIndex_ = -1;
    bool isOpen_ = false;
    float dropdownAnimProgress_ = 0.0f;
    float chevronRotation_ = 0.0f;  // Current rotation angle (0 = down, 180 = up)
    std::string placeholderText_ = "Select an item...";
    int maxVisibleItems_ = 5;
    float itemHeight_ = 28.0f;
    int hoveredIndex_ = -1;
    int scrollOffset_ = 0;

    // Colors
    NUIColor backgroundColor_;
    NUIColor hoverColor_;
    NUIColor selectedColor_;
    NUIColor borderColor_;
    NUIColor textColor_;
    NUIColor arrowColor_;
    bool customBackground_ = false;
    bool customHover_ = false;
    bool customSelected_ = false;
    bool customBorder_ = false;
    bool customText_ = false;
    bool customArrow_ = false;

    // Callbacks
    std::function<void()> onOpen_;
    std::function<void()> onClose_;
    std::function<void(int)> onSelectionChangedIndex_;
    std::function<void(int, int, const std::string&)> onSelectionChanged_;
    std::function<void(const SelectionChangedEvent&)> onSelectionChangedEx_;
    
    // Text measurement cache to avoid repeated expensive measureText() calls
    std::vector<float> itemTextWidthCache_;
    bool itemWidthCacheValid_ = false;
    
    // Dropdown rendering cache
    uint32_t cachedTextureId_ = 0;
    int cachedTextureWidth_ = 0;
    int cachedTextureHeight_ = 0;
    bool cacheDirty_ = true; // need to regenerate when open/contents change

    // Internal rendering helper used by cache generation and normal rendering
    void renderDropdownListInternal(NUIRenderer& renderer);
    void refreshThemeColors();
};


} // namespace AestraUI
