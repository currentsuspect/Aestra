// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"
#include "../Core/NUITypes.h"
#include "../Graphics/NUIRenderer.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace NomadUI {


class NUIDropdownItem {
public:
    NUIDropdownItem(const std::string& text, int value)
        : text_(text), value_(value), visible_(true), enabled_(true) {}

    const std::string& getText() const { return text_; }
    int getValue() const { return value_; }
    bool isVisible() const { return visible_; }
    bool isEnabled() const { return enabled_; }
    
    void setVisible(bool visible) { visible_ = visible; }
    void setEnabled(bool enabled) { enabled_ = enabled; }

private:
    std::string text_;
    int value_;
    bool visible_;
    bool enabled_;
};

class NUIDropdown : public NUIComponent {
public:
    NUIDropdown();
    ~NUIDropdown();

    // Item management
    void addItem(const std::string& text, int value);
    void addItem(std::shared_ptr<NUIDropdownItem> item);
    void setItemVisible(int index, bool visible);
    void setItemEnabled(int index, bool enabled);
    void clearItems();

    // Visual configuration
    void setPlaceholderText(const std::string& text) { placeholderText_ = text; setDirty(true); }
    void setMaxVisibleItems(int count) { maxVisibleItems_ = count; setDirty(true); }
    
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
    int getSelectedValue() const;
    std::string getSelectedText() const;
    bool isOpen() const { return isOpen_; }
    size_t getItemCount() const { return items_.size(); }
    void setSelectedIndex(int index);

    // Event callbacks
    void setOnOpen(std::function<void()> callback) { onOpen_ = callback; }
    void setOnClose(std::function<void()> callback) { onClose_ = callback; }
    void setOnSelectionChanged(std::function<void(int, int, const std::string&)> callback) { onSelectionChanged_ = callback; }

    // Overridden methods from NUIComponent
    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    bool onKeyEvent(const NUIKeyEvent& event) override;
    void onFocusGained() override;
    void onFocusLost() override;

protected:
    void toggleDropdown();
    void openDropdown();
    void closeDropdown();
    void updateAnimations();

private:
    void renderItem(NUIRenderer& renderer, int index, const NUIRect& bounds, bool isSelected, bool isHovered);
    int getItemUnderMouse(const NUIPoint& mousePos) const;

    std::vector<std::shared_ptr<NUIDropdownItem>> items_;
    int selectedIndex_ = -1;
    bool isOpen_ = false;
    float dropdownAnimProgress_ = 0.0f;
    std::string placeholderText_ = "Select an item...";
    int maxVisibleItems_ = 5;
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
    std::function<void(int, int, const std::string&)> onSelectionChanged_;
    
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