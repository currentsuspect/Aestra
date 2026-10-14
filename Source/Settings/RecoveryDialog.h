// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../AestraUI/Core/NUIComponent.h"
#include "../AestraUI/Widgets/NUICoreWidgets.h"
#include "../Core/RecoveryInfo.h"
#include <functional>
#include <string>
#include <filesystem>

namespace Aestra {

/**
 * @brief Response options for the recovery dialog
 */
enum class RecoveryResponse {
    None,       // Dialog not yet answered
    Recover,        // User chose to recover the newest autosave
    RecoverAsAlt,   // Recover it and keep it as an alt, leaving the saved branch as it was
    OpenLastSaved,  // Open the last saved version; the autosave is kept as a rescued session, not thrown away
    Discard,        // Delete the autosave and start fresh (no longer offered by the card; kept for callers)
    Cancel          // User cancelled (continue with autosave loaded silently)
};

/**
 * @brief Recovery dialog shown on startup when autosave is detected
 * 
 * Prompts the user with options to:
 * - Recover: Load the autosave file
 * - Discard: Delete autosave and start with empty project
 */
class RecoveryDialog : public AestraUI::NUIComponent {
public:
    using ResponseCallback = std::function<void(RecoveryResponse)>;
    
    RecoveryDialog();
    ~RecoveryDialog() override = default;
    
    // NUIComponent interface
    void onRender(AestraUI::NUIRenderer& renderer) override;
    bool onMouseEvent(const AestraUI::NUIMouseEvent& event) override;
    bool onKeyEvent(const AestraUI::NUIKeyEvent& event) override;
    
    /**
     * @brief Show the dialog with autosave info
     * @param autosavePath Path to the detected autosave file
     * @param callback Function called with user's response
     */
    void show(const std::string& autosavePath, ResponseCallback callback, const RecoveryInfo::Info& info = {});
    
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
    static bool detectAutosave(const std::string& autosavePath,
                               const std::string& recoveryMarkerPath,
                               const std::string& expectedSessionToken,
                               std::string& outTimestamp);
    
private:
    std::string m_autosavePath;
    std::string m_autosaveTimestamp;
    ResponseCallback m_callback;
    RecoveryResponse m_response;
    bool m_isVisible;
    
    // Button hover states
    bool m_recoverHovered;
    bool m_discardHovered;
    bool m_altHovered{false};
    RecoveryInfo::Info m_info;
    RecoveryResponse m_pressedResponse{RecoveryResponse::None};
    
    // Button rectangles (calculated during render)
    AestraUI::NUIRect m_recoverButtonRect;
    AestraUI::NUIRect m_discardButtonRect;  // "Open last saved version"
    AestraUI::NUIRect m_altButtonRect;
    AestraUI::NUIRect m_dialogRect;
    
    void handleResponse(RecoveryResponse response);
    void calculateLayout();
};

} // namespace Aestra
