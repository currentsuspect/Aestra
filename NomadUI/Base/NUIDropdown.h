// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITypes.h"
#include "NUIRenderer.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <optional>

namespace NomadUI {


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
    
    void setItemVisible(int index, bool visible);
    void setItemEnabled(int index, bool enabled);
    void clearItems();

    // Visual configuration
    void setPlaceholderText(const std::string& text) { placeholderText_ = text; setDirty(true); }
    void setMaxVisibleItems(int count) { maxVisibleItems_ = count; setDirty(true); }
    void setItemHeight(float height) { itemHeight_ = height; setDirty(true); }
    
    // Render dropdown list separately for proper z-order
    void renderDropdownList(NUIRenderer& renderer);
    void setBackgroundColor(const NUIColor& color) { backgroundColor_ = color; setDirty(true); }
    void setHoverColor(const NUIColor& color) { hoverColor_ = color; setDirty(true); }
    void setSelectedColor(const NUIColor& color) { selectedColor_ = color; setDirty(true); }
    void setBorderColor(const NUIColor& color) { borderColor_ = color; setDirty(true); }
    void setTextColor(const NUIColor& color) { textColor_ = color; setDirty(true); }
    void setArrowColor(const NUIColor& color) { arrowColor_ = color; setDirty(true); }

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

    // Colors
    NUIColor backgroundColor_;
    NUIColor hoverColor_;
    NUIColor selectedColor_;
    NUIColor borderColor_;
    NUIColor textColor_;
    NUIColor arrowColor_;

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
};


} // namespace NomadUI
