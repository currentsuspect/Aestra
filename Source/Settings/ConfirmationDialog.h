// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Widgets/NUICoreWidgets.h"
#include <functional>
#include <string>

namespace Nomad {

/**
 * @brief Dialog response options for user confirmation
 */
enum class DialogResponse {
    None,       // Dialog not yet answered
    Save,       // User chose to save
    DontSave,   // User chose to discard changes
    Cancel      // User cancelled the action
};

/**
 * @brief Confirmation dialog for unsaved changes prompt
 * 
 * Displays a modal dialog with three options:
 * - Save: Save changes and proceed
 * - Don't Save: Discard changes and proceed
 * - Cancel: Return to the application
 */
class ConfirmationDialog : public NomadUI::NUIComponent {
public:
    using ResponseCallback = std::function<void(DialogResponse)>;
    
    ConfirmationDialog();
    ~ConfirmationDialog() override = default;
    
    // NUIComponent interface
    void onRender(NomadUI::NUIRenderer& renderer) override;
    bool onMouseEvent(const NomadUI::NUIMouseEvent& event) override;
    bool onKeyEvent(const NomadUI::NUIKeyEvent& event) override;
    
    /**
     * @brief Show the dialog with a custom message
     * @param title Dialog title (e.g., "Unsaved Changes")
     * @param message Dialog message (e.g., "Do you want to save before closing?")
     * @param callback Function called with user's response
     */
    void show(const std::string& title, const std::string& message, ResponseCallback callback);
    
    /**
     * @brief Hide the dialog
     */
    void hide();
    
    /**
     * @brief Check if dialog is currently visible
     */
    bool isDialogVisible() const { return m_isVisible; }
    
    /**
     * @brief Get the last response (None if dialog still open)
     */
    DialogResponse getResponse() const { return m_response; }
    
private:
    std::string m_title;
    std::string m_message;
    ResponseCallback m_callback;
    DialogResponse m_response;
    bool m_isVisible;
    
    // Button hover states
    bool m_saveHovered;
    bool m_dontSaveHovered;
    bool m_cancelHovered;
    
    // Button rectangles (calculated during render)
    NomadUI::NUIRect m_saveButtonRect;
    NomadUI::NUIRect m_dontSaveButtonRect;
    NomadUI::NUIRect m_cancelButtonRect;
    NomadUI::NUIRect m_dialogRect;
    
    void handleResponse(DialogResponse response);
    void calculateLayout();
};

} // namespace Nomad
