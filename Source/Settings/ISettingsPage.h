// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../AestraUI/Core/NUIComponent.h"

#include <memory>
#include <string>

namespace Aestra {

/**
 * @brief Interface for a settings page in the unified Settings Dialog
 */
class ISettingsPage : public AestraUI::NUIComponent {
public:
    virtual ~ISettingsPage() = default;

    /**
     * @brief Get the unique ID of the page (e.g., "audio", "general")
     */
    virtual std::string getPageID() const = 0;

    /**
     * @brief Get the display title of the page
     */
    virtual std::string getTitle() const = 0;
    
    /**
     * @brief Get the icon name (if supported by sidebar)
     */
    virtual std::string getIcon() const { return ""; }

    /**
     * @brief Called when changes are applied
     */
    virtual void applyChanges() = 0;

    /**
     * @brief Called when changes are cancelled/reverted
     */
    virtual void cancelChanges() = 0;

    /**
     * @brief Check if there are unsaved changes
     */
    virtual bool hasUnsavedChanges() const = 0;

    /**
     * @brief How many settings on this page differ from what is saved (the sidebar badge and the
     * "N changes not saved" note). Pages that only know yes/no report 1.
     */
    virtual int pendingChangeCount() const { return hasUnsavedChanges() ? 1 : 0; }

    /**
     * @brief A page's own action for the dialog footer's left side (Audio: Test sound), and the
     * muted note beside it. Null when the page has none.
     */
    virtual std::shared_ptr<AestraUI::NUIComponent> footerAction() { return nullptr; }
    virtual std::string footerNote() const { return ""; }

    /**
     * @brief Called when the page becomes visible/active
     */
    virtual void onShow() {}
    
    /**
     * @brief Called when the page is hidden/inactive
     */
    virtual void onHide() {}
};

} // namespace Aestra
