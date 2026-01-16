// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "RecoveryDialog.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../AestraCore/include/AestraLog.h"
#include <chrono>
#include <ctime>
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

void RecoveryDialog::show(const std::string& autosavePath, ResponseCallback callback) {
    m_autosavePath = autosavePath;
    m_callback = callback;
    m_response = RecoveryResponse::None;
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
    
    // Dialog size
    float dialogWidth = 450.0f;
    float dialogHeight = 180.0f;
    
    // Center the dialog
    float dialogX = (bounds.width - dialogWidth) * 0.5f;
    float dialogY = (bounds.height - dialogHeight) * 0.5f;
    
    m_dialogRect = AestraUI::NUIRect(dialogX, dialogY, dialogWidth, dialogHeight);
    
    // Button layout
    float buttonWidth = 120.0f;
    float buttonHeight = 36.0f;
    float buttonSpacing = 20.0f;
    float buttonY = dialogY + dialogHeight - buttonHeight - 20.0f;
    
    float totalButtonWidth = buttonWidth * 2 + buttonSpacing;
    float buttonStartX = dialogX + (dialogWidth - totalButtonWidth) * 0.5f;
    
    m_recoverButtonRect = AestraUI::NUIRect(buttonStartX, buttonY, buttonWidth, buttonHeight);
    m_discardButtonRect = AestraUI::NUIRect(buttonStartX + buttonWidth + buttonSpacing, buttonY, buttonWidth, buttonHeight);
}

void RecoveryDialog::onRender(AestraUI::NUIRenderer& renderer) {
    if (!m_isVisible) return;
    
    calculateLayout();
    
    auto bounds = getBounds();
    
    // Semi-transparent overlay
    renderer.fillRect(AestraUI::NUIRect(0, 0, bounds.width, bounds.height), 
                     AestraUI::NUIColor(0.0f, 0.0f, 0.0f, 0.6f));
    
    // Dialog background
    renderer.fillRoundedRect(m_dialogRect, 8.0f, AestraUI::NUIColor(0.15f, 0.15f, 0.18f, 1.0f));
    renderer.strokeRoundedRect(m_dialogRect, 8.0f, 1.0f, AestraUI::NUIColor(0.3f, 0.3f, 0.35f, 1.0f));
    
    // Title
    float titleY = m_dialogRect.y + 25.0f;
    renderer.drawText("Recover Unsaved Work?", 
                     AestraUI::NUIPoint(m_dialogRect.x + 20.0f, titleY), 
                     18.0f, AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 1.0f));
    
    // Message
    float messageY = titleY + 35.0f;
    renderer.drawText("An autosave was found from:", 
                     AestraUI::NUIPoint(m_dialogRect.x + 20.0f, messageY), 
                     14.0f, AestraUI::NUIColor(0.8f, 0.8f, 0.8f, 1.0f));
    
    // Timestamp
    float timestampY = messageY + 25.0f;
    renderer.drawText(m_autosaveTimestamp, 
                     AestraUI::NUIPoint(m_dialogRect.x + 20.0f, timestampY), 
                     14.0f, AestraUI::NUIColor(0.6f, 0.8f, 1.0f, 1.0f));
    
    // Recover button (primary - green)
    AestraUI::NUIColor recoverBg = m_recoverHovered ? 
        AestraUI::NUIColor(0.2f, 0.6f, 0.3f, 1.0f) : 
        AestraUI::NUIColor(0.15f, 0.5f, 0.25f, 1.0f);
    renderer.fillRoundedRect(m_recoverButtonRect, 4.0f, recoverBg);
    
    auto recoverTextSize = renderer.measureText("Recover", 14.0f);
    float recoverTextX = m_recoverButtonRect.x + (m_recoverButtonRect.width - recoverTextSize.width) * 0.5f;
    float recoverTextY = m_recoverButtonRect.y + (m_recoverButtonRect.height - recoverTextSize.height) * 0.5f;
    renderer.drawText("Recover", AestraUI::NUIPoint(recoverTextX, recoverTextY), 
                     14.0f, AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 1.0f));
    
    // Discard button (secondary - red-ish)
    AestraUI::NUIColor discardBg = m_discardHovered ? 
        AestraUI::NUIColor(0.5f, 0.25f, 0.25f, 1.0f) : 
        AestraUI::NUIColor(0.4f, 0.2f, 0.2f, 1.0f);
    renderer.fillRoundedRect(m_discardButtonRect, 4.0f, discardBg);
    
    auto discardTextSize = renderer.measureText("Discard", 14.0f);
    float discardTextX = m_discardButtonRect.x + (m_discardButtonRect.width - discardTextSize.width) * 0.5f;
    float discardTextY = m_discardButtonRect.y + (m_discardButtonRect.height - discardTextSize.height) * 0.5f;
    renderer.drawText("Discard", AestraUI::NUIPoint(discardTextX, discardTextY), 
                     14.0f, AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 1.0f));
}

bool RecoveryDialog::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    if (!m_isVisible) return false;
    
    float mx = event.position.x;
    float my = event.position.y;
    
    // Update hover states
    m_recoverHovered = m_recoverButtonRect.contains(mx, my);
    m_discardHovered = m_discardButtonRect.contains(mx, my);
    
    if (event.pressed && event.button == AestraUI::NUIMouseButton::Left) {
        if (m_recoverHovered) {
            handleResponse(RecoveryResponse::Recover);
            return true;
        }
        if (m_discardHovered) {
            handleResponse(RecoveryResponse::Discard);
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
