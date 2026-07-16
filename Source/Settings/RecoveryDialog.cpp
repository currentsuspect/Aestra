// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "RecoveryDialog.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../AestraCore/include/AestraLog.h"
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Aestra {

RecoveryDialog::RecoveryDialog()
    : m_response(RecoveryResponse::None)
    , m_isVisible(false)
    , m_recoverHovered(false)
    , m_discardHovered(false)
{
}

bool RecoveryDialog::detectAutosave(const std::string& autosavePath, std::string& outTimestamp) {
    if (!std::filesystem::exists(autosavePath)) {
        return false;
    }
    
    // Get file modification time
    try {
        auto ftime = std::filesystem::last_write_time(autosavePath);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
        );
        auto time = std::chrono::system_clock::to_time_t(sctp);
        
        std::tm tm;
#ifdef _WIN32
        localtime_s(&tm, &time);
#else
        localtime_r(&time, &tm);
#endif
        
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        outTimestamp = oss.str();
        
        // Check if file is not empty
        auto fileSize = std::filesystem::file_size(autosavePath);
        if (fileSize == 0) {
            return false;
        }
        
        Log::info("[Recovery] Detected autosave: " + autosavePath + " (" + outTimestamp + ")");
        return true;
    } catch (const std::exception& e) {
        Log::warning("[Recovery] Error checking autosave: " + std::string(e.what()));
        return false;
    }
}

bool RecoveryDialog::detectAutosave(const std::string& autosavePath,
                                    const std::string& recoveryMarkerPath,
                                    const std::string& expectedSessionToken,
                                    std::string& outTimestamp) {
    if (expectedSessionToken.empty()) {
        return false;
    }

    std::ifstream marker(recoveryMarkerPath, std::ios::binary);
    if (!marker.good()) {
        Log::warning("[Recovery] Recovery marker missing; falling back to legacy autosave detection");
        return detectAutosave(autosavePath, outTimestamp);
    }

    std::string markerToken;
    std::getline(marker, markerToken);
    if (marker.fail() && !marker.eof()) {
        Log::warning("[Recovery] Recovery marker unreadable; falling back to legacy autosave detection");
        return detectAutosave(autosavePath, outTimestamp);
    }

    if (markerToken != expectedSessionToken) {
        Log::warning("[Recovery] Recovery marker does not match current session; falling back to legacy autosave detection");
        return detectAutosave(autosavePath, outTimestamp);
    }

    return detectAutosave(autosavePath, outTimestamp);
}

void RecoveryDialog::show(const std::string& autosavePath, ResponseCallback callback) {
    m_autosavePath = autosavePath;
    m_callback = callback;
    m_response = RecoveryResponse::None;
    m_pressedResponse = RecoveryResponse::None;
    m_isVisible = true;
    
    // Get timestamp
    detectAutosave(autosavePath, m_autosaveTimestamp);
    
    setVisible(true);
    Log::info("[Recovery] Showing recovery dialog");
}

void RecoveryDialog::hide() {
    m_isVisible = false;
    setVisible(false);
}

void RecoveryDialog::calculateLayout() {
    auto bounds = getBounds();
    const auto& theme = AestraUI::NUIThemeManager::getInstance().getCurrentTheme();
    
    // Dialog size
    float dialogWidth = 450.0f;
    float dialogHeight = 180.0f;
    
    // Center the dialog
    float dialogX = (bounds.width - dialogWidth) * 0.5f;
    float dialogY = (bounds.height - dialogHeight) * 0.5f;
    
    m_dialogRect = AestraUI::NUIRect(dialogX, dialogY, dialogWidth, dialogHeight);
    
    // Button layout
    float buttonWidth = 120.0f;
    float buttonHeight = theme.layout.dialogActionHeight;
    float buttonSpacing = theme.spacingM;
    float buttonY = dialogY + dialogHeight - buttonHeight - theme.spacingM;
    
    float totalButtonWidth = buttonWidth * 2 + buttonSpacing;
    float buttonStartX = dialogX + (dialogWidth - totalButtonWidth) * 0.5f;
    
    m_recoverButtonRect = AestraUI::NUIRect(buttonStartX, buttonY, buttonWidth, buttonHeight);
    m_discardButtonRect = AestraUI::NUIRect(buttonStartX + buttonWidth + buttonSpacing, buttonY, buttonWidth, buttonHeight);
}

void RecoveryDialog::onRender(AestraUI::NUIRenderer& renderer) {
    if (!m_isVisible) return;
    
    calculateLayout();
    
    auto bounds = getBounds();
    const auto& theme = AestraUI::NUIThemeManager::getInstance().getCurrentTheme();
    
    // Semi-transparent overlay
    renderer.fillRect(AestraUI::NUIRect(0, 0, bounds.width, bounds.height), theme.overlay);
    
    // Dialog background
    renderer.fillRoundedRect(m_dialogRect, theme.radiusL, theme.surfaceTertiary);
    renderer.strokeRoundedRect(m_dialogRect, theme.radiusL, theme.layout.dividerWidth, theme.borderStrong);
    
    // Title
    const float contentX = m_dialogRect.x + theme.spacingM;
    float titleY = m_dialogRect.y + theme.spacingL;
    renderer.drawText("Recover Unsaved Work?", 
                     AestraUI::NUIPoint(contentX, titleY),
                     theme.fontSizeXL, theme.textPrimary);
    
    // Message
    float messageY = titleY + theme.spacingXL;
    renderer.drawText("An autosave was found from:", 
                     AestraUI::NUIPoint(contentX, messageY),
                     theme.fontSizeL, theme.textSecondary);
    
    // Timestamp
    float timestampY = messageY + theme.spacingL;
    renderer.drawText(m_autosaveTimestamp, 
                     AestraUI::NUIPoint(contentX, timestampY),
                     theme.fontSizeM, theme.textMuted);

    // Primary recovery action.
    const bool recoverPressed = m_pressedResponse == RecoveryResponse::Recover;
    AestraUI::NUIColor recoverBg =
        recoverPressed ? theme.primaryPressed : (m_recoverHovered ? theme.primaryHover : theme.primary);
    renderer.fillRoundedRect(m_recoverButtonRect, theme.radiusM, recoverBg);
    renderer.drawTextCentered("Recover", m_recoverButtonRect, theme.fontSizeM, theme.textOnPrimary);

    // Destructive secondary action.
    const bool discardPressed = m_pressedResponse == RecoveryResponse::Discard;
    AestraUI::NUIColor discardBg = discardPressed ? theme.pressed : (m_discardHovered ? theme.hover : theme.buttonBgDefault);
    renderer.fillRoundedRect(m_discardButtonRect, theme.radiusM, discardBg);
    renderer.strokeRoundedRect(m_discardButtonRect, theme.radiusM, theme.layout.dividerWidth, theme.borderStrong);
    renderer.drawTextCentered("Discard", m_discardButtonRect, theme.fontSizeM, theme.error);
}

bool RecoveryDialog::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    if (!m_isVisible) return false;
    
    float mx = event.position.x;
    float my = event.position.y;
    
    // Update hover states
    const bool recoverHovered = m_recoverButtonRect.contains(mx, my);
    const bool discardHovered = m_discardButtonRect.contains(mx, my);
    if (recoverHovered != m_recoverHovered || discardHovered != m_discardHovered) {
        m_recoverHovered = recoverHovered;
        m_discardHovered = discardHovered;
        setDirty(true);
    }
    
    if (event.button == AestraUI::NUIMouseButton::Left) {
        if (event.pressed) {
            m_pressedResponse = m_recoverHovered ? RecoveryResponse::Recover
                              : m_discardHovered ? RecoveryResponse::Discard
                                                 : RecoveryResponse::None;
            setDirty(true);
            return true;
        }
        if (event.released) {
            const RecoveryResponse armed = m_pressedResponse;
            m_pressedResponse = RecoveryResponse::None;
            setDirty(true);
            if ((armed == RecoveryResponse::Recover && m_recoverHovered) ||
                (armed == RecoveryResponse::Discard && m_discardHovered)) {
                handleResponse(armed);
            }
            return true;
        }
    }
    
    // Block all mouse events when visible
    return true;
}

bool RecoveryDialog::onKeyEvent(const AestraUI::NUIKeyEvent& event) {
    if (!m_isVisible) return false;
    
    if (event.pressed) {
        if (event.keyCode == AestraUI::NUIKeyCode::Escape) {
            // Escape = Recover (safe default)
            handleResponse(RecoveryResponse::Recover);
            return true;
        }
        if (event.keyCode == AestraUI::NUIKeyCode::Enter) {
            // Enter = Recover (primary action)
            handleResponse(RecoveryResponse::Recover);
            return true;
        }
    }
    
    // Block all key events when visible
    return true;
}

void RecoveryDialog::handleResponse(RecoveryResponse response) {
    m_response = response;
    hide();
    
    if (m_callback) {
        m_callback(response);
    }
}

} // namespace Aestra
