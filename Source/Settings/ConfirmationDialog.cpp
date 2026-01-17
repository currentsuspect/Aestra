// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "ConfirmationDialog.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../AestraCore/include/AestraLog.h"

namespace Aestra {

ConfirmationDialog::ConfirmationDialog()
    : m_response(DialogResponse::None)
    , m_isVisible(false)
    , m_saveHovered(false)
    , m_dontSaveHovered(false)
    , m_cancelHovered(false)
{
}

void ConfirmationDialog::show(const std::string& title, const std::string& message, ResponseCallback callback) {
    m_title = title;
    m_message = message;
    m_callback = callback;
    m_response = DialogResponse::None;
    m_isVisible = true;
    setVisible(true);
    
    Log::info("[ConfirmationDialog] Showing: " + title);
}

void ConfirmationDialog::hide() {
    m_isVisible = false;
    setVisible(false);
    
    Log::info("[ConfirmationDialog] Hidden");
}

void ConfirmationDialog::handleResponse(DialogResponse response) {
    m_response = response;
    
    std::string responseStr;
    switch (response) {
        case DialogResponse::Save: responseStr = "Save"; break;
        case DialogResponse::DontSave: responseStr = "Don't Save"; break;
        case DialogResponse::Cancel: responseStr = "Cancel"; break;
        default: responseStr = "Unknown"; break;
    }
    Log::info("[ConfirmationDialog] User selected: " + responseStr);
    
    hide();
    
    if (m_callback) {
        m_callback(response);
    }
}

void ConfirmationDialog::calculateLayout() {
    AestraUI::NUIRect parentBounds = getBounds();
    
	    // Dialog dimensions - wider for better button spacing
	    const float dialogWidth = 420.0f;
	    const float dialogHeight = 160.0f;
	    const float buttonWidth = 110.0f;
	    const float buttonHeight = 34.0f;
	    const float buttonSpacing = 10.0f;
	    const float buttonMargin = 24.0f;
    
    // Center dialog in parent
    m_dialogRect.x = parentBounds.x + (parentBounds.width - dialogWidth) / 2.0f;
    m_dialogRect.y = parentBounds.y + (parentBounds.height - dialogHeight) / 2.0f;
    m_dialogRect.width = dialogWidth;
    m_dialogRect.height = dialogHeight;
    
	    // Calculate button positions (centered at bottom)
	    float totalButtonsWidth = 3 * buttonWidth + 2 * buttonSpacing;
	    float buttonsStartX = m_dialogRect.x + (dialogWidth - totalButtonsWidth) * 0.5f;
	    float buttonY = m_dialogRect.y + dialogHeight - buttonMargin - buttonHeight;
    
    m_saveButtonRect = {buttonsStartX, buttonY, buttonWidth, buttonHeight};
    m_dontSaveButtonRect = {buttonsStartX + buttonWidth + buttonSpacing, buttonY, buttonWidth, buttonHeight};
    m_cancelButtonRect = {buttonsStartX + 2 * (buttonWidth + buttonSpacing), buttonY, buttonWidth, buttonHeight};
}

void ConfirmationDialog::onRender(AestraUI::NUIRenderer& renderer) {
    if (!m_isVisible) {
        return;
    }
    
    calculateLayout();
    
    // Modern dark theme colors
    AestraUI::NUIColor overlayColor(0.0f, 0.0f, 0.0f, 0.7f);
    AestraUI::NUIColor dialogBg(0.12f, 0.12f, 0.14f, 1.0f);       // Dark gray
    AestraUI::NUIColor dialogBorder(0.25f, 0.25f, 0.28f, 1.0f);   // Subtle border
    AestraUI::NUIColor titleColor(1.0f, 1.0f, 1.0f, 1.0f);        // White
    AestraUI::NUIColor messageColor(0.7f, 0.7f, 0.72f, 1.0f);     // Light gray
    
    // Button colors
    AestraUI::NUIColor saveBgNormal(0.4f, 0.6f, 1.0f, 1.0f);      // Blue accent
    AestraUI::NUIColor saveBgHover(0.5f, 0.7f, 1.0f, 1.0f);       // Lighter blue
    AestraUI::NUIColor buttonBgNormal(0.2f, 0.2f, 0.22f, 1.0f);   // Dark button
    AestraUI::NUIColor buttonBgHover(0.3f, 0.3f, 0.33f, 1.0f);    // Hover state
    AestraUI::NUIColor buttonBorder(0.35f, 0.35f, 0.38f, 1.0f);   // Button border
    AestraUI::NUIColor textWhite(1.0f, 1.0f, 1.0f, 1.0f);
    AestraUI::NUIColor textLight(0.9f, 0.9f, 0.92f, 1.0f);
    
    // Draw semi-transparent overlay
    AestraUI::NUIRect parentBounds = getBounds();
    renderer.fillRect(parentBounds, overlayColor);
    
    // Draw dialog shadow (offset dark rectangle)
    AestraUI::NUIRect shadowRect = m_dialogRect;
    shadowRect.x += 4.0f;
    shadowRect.y += 4.0f;
    renderer.fillRoundedRect(shadowRect, 8.0f, AestraUI::NUIColor(0.0f, 0.0f, 0.0f, 0.4f));
    
    // Draw dialog background with rounded corners
    renderer.fillRoundedRect(m_dialogRect, 8.0f, dialogBg);
    renderer.strokeRoundedRect(m_dialogRect, 8.0f, 1.0f, dialogBorder);
    
    // Draw title (larger, bold-ish)
    float titleX = m_dialogRect.x + 24.0f;
    float titleY = m_dialogRect.y + 28.0f;
    renderer.drawText(m_title, AestraUI::NUIPoint(titleX, titleY), 14.0f, titleColor);
    
    // Draw message
    float messageX = m_dialogRect.x + 24.0f;
    float messageY = m_dialogRect.y + 58.0f;
    renderer.drawText(m_message, AestraUI::NUIPoint(messageX, messageY), 13.0f, messageColor);
    
    // === SAVE BUTTON (Primary - Blue) ===
    AestraUI::NUIColor saveBg = m_saveHovered ? saveBgHover : saveBgNormal;
    renderer.fillRoundedRect(m_saveButtonRect, 6.0f, saveBg);
    
    // Center "Save" text using drawTextCentered
    renderer.drawTextCentered("Save", m_saveButtonRect, 13.0f, textWhite);
    
    // === DON'T SAVE BUTTON ===
    AestraUI::NUIColor dontSaveBg = m_dontSaveHovered ? buttonBgHover : buttonBgNormal;
    renderer.fillRoundedRect(m_dontSaveButtonRect, 6.0f, dontSaveBg);
    renderer.strokeRoundedRect(m_dontSaveButtonRect, 6.0f, 1.0f, buttonBorder);
    
    // Center "Don't Save" text using drawTextCentered
    renderer.drawTextCentered("Don't Save", m_dontSaveButtonRect, 13.0f, textLight);
    
    // === CANCEL BUTTON ===
    AestraUI::NUIColor cancelBg = m_cancelHovered ? buttonBgHover : buttonBgNormal;
    renderer.fillRoundedRect(m_cancelButtonRect, 6.0f, cancelBg);
    renderer.strokeRoundedRect(m_cancelButtonRect, 6.0f, 1.0f, buttonBorder);
    
    // Center "Cancel" text using drawTextCentered
    renderer.drawTextCentered("Cancel", m_cancelButtonRect, 13.0f, textLight);
}

bool ConfirmationDialog::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    if (!m_isVisible) {
        return false;
    }
    
    calculateLayout();
    
    float mouseX = event.position.x;
    float mouseY = event.position.y;
    
    // Update hover states
    m_saveHovered = m_saveButtonRect.contains(mouseX, mouseY);
    m_dontSaveHovered = m_dontSaveButtonRect.contains(mouseX, mouseY);
    m_cancelHovered = m_cancelButtonRect.contains(mouseX, mouseY);
    
    // Handle clicks (pressed == true and button is Left)
    if (event.pressed && event.button == AestraUI::NUIMouseButton::Left) {
        if (m_saveHovered) {
            handleResponse(DialogResponse::Save);
            return true;
        }
        if (m_dontSaveHovered) {
            handleResponse(DialogResponse::DontSave);
            return true;
        }
        if (m_cancelHovered) {
            handleResponse(DialogResponse::Cancel);
            return true;
        }
        
        // Click outside dialog = cancel
        if (!m_dialogRect.contains(mouseX, mouseY)) {
            handleResponse(DialogResponse::Cancel);
            return true;
        }
    }
    
    // Consume all mouse events while dialog is visible
    return true;
}

bool ConfirmationDialog::onKeyEvent(const AestraUI::NUIKeyEvent& event) {
    if (!m_isVisible) {
        return false;
    }
    
    if (event.pressed) {
        // Escape = Cancel
        if (event.keyCode == AestraUI::NUIKeyCode::Escape) {
            handleResponse(DialogResponse::Cancel);
            return true;
        }
        
        // Enter = Save (primary action)
        if (event.keyCode == AestraUI::NUIKeyCode::Enter) {
            handleResponse(DialogResponse::Save);
            return true;
        }
    }
    
    // Consume all key events while dialog is visible
    return true;
}

} // namespace Aestra
