// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#ifndef NUIDROPDOWN_H
#define NUIDROPDOWN_H

#include "NUIComponent.h"
#include "NUILabel.h"
#include <vector>
#include <string>
#include <functional>

namespace NomadUI {

    class NUIDropdown : public NUIComponent {
    public:
        NUIDropdown();
        virtual ~NUIDropdown();

    // Add an item to the dropdown
    void addItem(const std::string& text);
    void addItem(const std::string& text, const std::function<void()>& callback);
    // Add an item with an associated integer value (used by AudioSettingsDialog)
    void addItem(const std::string& text, int value);
    void addItem(const std::string& text, int value, const std::function<void()>& callback);

        // Remove an item at the specified index
        void removeItem(size_t index);

    // Clear all items
    void clear();
    // Backwards-compatible name used in UI code
    void clearItems();

    // Set/get the selected item
    void setSelectedIndex(size_t index);
    // Backwards-compatible overload accepting int
    void setSelectedIndex(int index);
    size_t getSelectedIndex() const;
    std::string getSelectedText() const;

    // Count of items
    int getItemCount() const;

    // Placeholder text when no selection
    void setPlaceholderText(const std::string& text);

        // Event handlers
    void setOnSelectionChanged(const std::function<void(size_t)>& callback);
    // Full signature used by dialogs: (index, value, text)
    void setOnSelectionChanged(const std::function<void(int,int,const std::string&)>& callback);

        // Overridden from NUIComponent
        void onMouseEnter() override;
        void onMouseLeave() override;
        bool onMouseEvent(const NUIMouseEvent& event) override;
        void onRender(NUIRenderer& renderer) override;

    private:
        struct DropdownItem {
            std::string text;
            std::function<void()> callback;
            int value = 0;
        };

        std::vector<DropdownItem> items;
        size_t selectedIndex;
        bool isOpen;
        NUILabel currentSelectionLabel;
    std::function<void(size_t)> onSelectionChangedCallback;
    std::function<void(int,int,const std::string&)> onSelectionChangedFullCallback;

        void toggleDropdown();
        void closeDropdown();
        bool isPointInDropdownList(const NUIPoint& point) const;
        size_t getItemIndexAtPoint(const NUIPoint& point) const;
        void updateCurrentSelectionLabel();
    };

} // namespace NomadUI

#endif // NUIDROPDOWN_H