// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Widgets/NUICoreWidgets.h"
#include <functional>
#include <string>
#include <filesystem>

namespace Nomad {

/**
 * @brief Response options for the recovery dialog
 */
enum class RecoveryResponse {
    None,       // Dialog not yet answered
    Recover,    // User chose to recover autosave
    Discard,    // User chose to discard autosave and start fresh
    Cancel      // User cancelled (continue with autosave loaded silently)
};

/**
 * @brief Recovery dialog shown on startup when autosave is detected
 * 
 * Prompts the user with options to:
 * - Recover: Load the autosave file
 * - Discard: Delete autosave and start with empty project
 */
class RecoveryDialog : public NomadUI::NUIComponent {
public:
    using ResponseCallback = std::function<void(RecoveryResponse)>;
    
    RecoveryDialog();
    ~RecoveryDialog() override = default;
    
    // NUIComponent interface
    void onRender(NomadUI::NUIRenderer& renderer) override;
    bool onMouseEvent(const NomadUI::NUIMouseEvent& event) override;
    bool onKeyEvent(const NomadUI::NUIKeyEvent& event) override;
    
    /**
     * @brief Show the dialog with autosave info
     * @param autosavePath Path to the detected autosave file
     * @param callback Function called with user's response
     */
    void show(const std::string& autosavePath, ResponseCallback callback);
    
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
    RecoveryResponse getResponse() const { return m_response; }
    
    /**
     * @brief Check if an autosave file exists and return info
     * @param autosavePath Path to check
     * @return True if autosave exists and is valid
     */
    static bool detectAutosave(const std::string& autosavePath, std::string& outTimestamp);
    
private:
    std::string m_autosavePath;
    std::string m_autosaveTimestamp;
    ResponseCallback m_callback;
    RecoveryResponse m_response;
    bool m_isVisible;
    
    // Button hover states
    bool m_recoverHovered;
    bool m_discardHovered;
    
    // Button rectangles (calculated during render)
    NomadUI::NUIRect m_recoverButtonRect;
    NomadUI::NUIRect m_discardButtonRect;
    NomadUI::NUIRect m_dialogRect;
    
    void handleResponse(RecoveryResponse response);
    void calculateLayout();
};

} // namespace Nomad
