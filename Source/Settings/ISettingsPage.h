// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../AestraUI/Core/NUIComponent.h"
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
     * @brief Called when the page becomes visible/active
     */
    virtual void onShow() {}
    
    /**
     * @brief Called when the page is hidden/inactive
     */
    virtual void onHide() {}
};

} // namespace Aestra
