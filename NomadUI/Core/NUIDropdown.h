// © 2025 Nomad Studios – All Rights Reserved. Licensed for personal & educational use only.
#ifndef NUIDROPDOWN_H
#define NUIDROPDOWN_H

#include "NUIComponent.h"
#include "NUILabel.h"
#include <vector>
#include <string>
#include <functional>
#include <optional>
#include <memory>

namespace NomadUI {

    struct DropdownItem {
        std::string text;
        std::function<void()> callback;
        int value = 0;
        bool enabled = true;
        std::string iconPath;  // Optional icon
        
        DropdownItem() = default;
        DropdownItem(const std::string& t, int v = 0) 
            : text(t), value(v), enabled(true) {}
    };

    struct SelectionChangedEvent {
        size_t index;
        int value;
        std::string text;
    };

    class NUIDropdown : public NUIComponent {
    public:
        NUIDropdown();
        virtual ~NUIDropdown();

        // Item management
        void addItem(const std::string& text);
        void addItem(const std::string& text, const std::function<void()>& callback);
        void addItem(const std::string& text, int value);
        void addItem(const std::string& text, int value, const std::function<void()>& callback);
        void addItem(const DropdownItem& item);
        
        void removeItem(size_t index);
        void clear();
        void clearItems();  // Backwards-compatible alias

        // Selection management
        void setSelectedIndex(size_t index);
        void setSelectedIndex(int index);
        void setSelectedByValue(int value);
        
        size_t getSelectedIndex() const;
        std::string getSelectedText() const;
        int getSelectedValue() const;
        std::optional<DropdownItem> getSelectedItem() const;

        // Item access
        int getItemCount() const;
        const std::vector<DropdownItem>& getItems() const;
        std::optional<DropdownItem> getItem(size_t index) const;
        
        // Configuration
        void setPlaceholderText(const std::string& text);
        void setItemHeight(float height);
        void setMaxVisibleItems(size_t count);
        void setSearchEnabled(bool enabled);
        void setAnimationEnabled(bool enabled);
        
        float getItemHeight() const { return itemHeight; }
        size_t getMaxVisibleItems() const { return maxVisibleItems; }
        bool isSearchEnabled() const { return searchEnabled; }

        // Dropdown state
        bool isDropdownOpen() const { return isOpen; }
        void openDropdown();
        void closeDropdown();
        void toggleDropdown();

        // Event callbacks
        void setOnSelectionChanged(const std::function<void(size_t)>& callback);
        void setOnSelectionChanged(const std::function<void(int, int, const std::string&)>& callback);
        void setOnSelectionChangedEx(const std::function<void(const SelectionChangedEvent&)>& callback);
        void setOnDropdownOpened(const std::function<void()>& callback);
        void setOnDropdownClosed(const std::function<void()>& callback);

        // Overridden from NUIComponent
        void onMouseEnter() override;
        void onMouseLeave() override;
        bool onMouseEvent(const NUIMouseEvent& event) override;
        void onRender(NUIRenderer& renderer) override;
        
        // Additional virtual methods (remove override if not in base class)
        virtual bool onKeyEvent(const NUIKeyEvent& event);
        void onUpdate(double deltaTime) override;

    private:
        // Items and selection
        std::vector<DropdownItem> items;
        size_t selectedIndex;
        std::string placeholderText;
        
        // Dropdown state
        bool isOpen;
        NUIPoint lastMousePos;
        std::optional<size_t> hoveredIndex;
        
        // Visual settings
        float itemHeight;
        size_t maxVisibleItems;
        float dropdownAnimationProgress;  // 0.0 = closed, 1.0 = open
        bool animationEnabled;
        
        // Scrolling
        size_t scrollOffset;
        float scrollbarWidth;
        bool needsScrollbar;
        
        // Search functionality
        bool searchEnabled;
        std::string searchBuffer;
        float searchTimeout;
        const float SEARCH_TIMEOUT_DURATION = 1.0f;
        
        // Labels
        std::shared_ptr<NUILabel> currentSelectionLabel;
        
        // Callbacks
        std::function<void(size_t)> onSelectionChangedCallback;
        std::function<void(int, int, const std::string&)> onSelectionChangedFullCallback;
        std::function<void(const SelectionChangedEvent&)> onSelectionChangedExCallback;
        std::function<void()> onDropdownOpenedCallback;
        std::function<void()> onDropdownClosedCallback;

        // Helper methods
        void updateCurrentSelectionLabel();
        void notifySelectionChanged();
        bool isPointInDropdownList(const NUIPoint& point) const;
        bool isPointInMainButton(const NUIPoint& point) const;
        std::optional<size_t> getItemIndexAtPoint(const NUIPoint& point) const;
        
        // Rendering helpers
        void renderMainButton(NUIRenderer& renderer);
        void renderDropdownList(NUIRenderer& renderer);
        void renderItem(NUIRenderer& renderer, size_t index, float y);
        void renderScrollbar(NUIRenderer& renderer);
        
        // Keyboard navigation
        void selectPreviousItem();
        void selectNextItem();
        void selectFirstItem();
        void selectLastItem();
        void handleSearchCharacter(char c);
        
        // Scrolling
        void updateScrollOffset();
        void scrollToItem(size_t index);
        size_t getVisibleItemCount() const;
        float getDropdownHeight() const;
        
        // Animation
        void updateAnimation(float deltaTime);
    };

} // namespace NomadUI

#endif // NUIDROPDOWN_H