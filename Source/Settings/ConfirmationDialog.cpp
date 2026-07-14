// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "ConfirmationDialog.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../AestraCore/include/AestraLog.h"

#include <cmath>

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

    // Dialog dimensions
    const float dialogWidth = 400.0f;
    const float dialogHeight = 172.0f;
    const float buttonHeight = 36.0f;
    const float buttonSpacing = 10.0f;
    const float margin = 22.0f;

    // Center dialog in parent (rounded so 1px strokes/text stay crisp).
    m_dialogRect.x = std::round(parentBounds.x + (parentBounds.width - dialogWidth) / 2.0f);
    m_dialogRect.y = std::round(parentBounds.y + (parentBounds.height - dialogHeight) / 2.0f);
    m_dialogRect.width = dialogWidth;
    m_dialogRect.height = dialogHeight;

    // Right-aligned button row, primary (Save) rightmost — modern convention:
    // [ Cancel ] [ Don't Save ] [ Save ].
    const float cancelWidth = 84.0f;
    const float dontSaveWidth = 104.0f;
    const float saveWidth = 96.0f;
    const float totalWidth = cancelWidth + dontSaveWidth + saveWidth + buttonSpacing * 2.0f;

    const float buttonY = m_dialogRect.y + dialogHeight - margin - buttonHeight;
    const float startX = m_dialogRect.right() - margin - totalWidth;

    m_cancelButtonRect = {startX, buttonY, cancelWidth, buttonHeight};
    m_dontSaveButtonRect = {m_cancelButtonRect.right() + buttonSpacing, buttonY, dontSaveWidth, buttonHeight};
    m_saveButtonRect = {m_dontSaveButtonRect.right() + buttonSpacing, buttonY, saveWidth, buttonHeight};
}

void ConfirmationDialog::onRender(AestraUI::NUIRenderer& renderer) {
    if (!m_isVisible) {
        return;
    }
    
    calculateLayout();

    // --- Palette (Aestra dark + brand purple) ---
    const AestraUI::NUIColor overlayColor(0.0f, 0.0f, 0.0f, 0.55f);
    const AestraUI::NUIColor dialogBg(0.078f, 0.078f, 0.090f, 1.0f);
    const AestraUI::NUIColor dialogBorder(1.0f, 1.0f, 1.0f, 0.10f);
    const AestraUI::NUIColor titleColor(0.96f, 0.96f, 0.98f, 1.0f);
    const AestraUI::NUIColor messageColor(0.62f, 0.62f, 0.66f, 1.0f);
    const AestraUI::NUIColor divider(1.0f, 1.0f, 1.0f, 0.07f);

    // Brand purple (matches AestraVerb / accentPrimary).
    const AestraUI::NUIColor accent(0.498f, 0.353f, 0.941f, 1.0f);
    const AestraUI::NUIColor accentHover(0.576f, 0.443f, 0.973f, 1.0f);
    const AestraUI::NUIColor textWhite(1.0f, 1.0f, 1.0f, 1.0f);
    const AestraUI::NUIColor textLight(0.86f, 0.86f, 0.90f, 1.0f);
    const AestraUI::NUIColor textMuted(0.66f, 0.66f, 0.70f, 1.0f);

    // Dim the app behind the modal.
    const AestraUI::NUIRect parentBounds = getBounds();
    renderer.fillRect(parentBounds, overlayColor);

    // Soft drop shadow (offsetX, offsetY, blur, color).
    renderer.drawShadow(m_dialogRect, 0.0f, 10.0f, 22.0f, AestraUI::NUIColor(0.0f, 0.0f, 0.0f, 0.55f));

    // Dialog surface.
    renderer.fillRoundedRect(m_dialogRect, 12.0f, dialogBg);
    renderer.strokeRoundedRect(m_dialogRect, 12.0f, 1.0f, dialogBorder);

    // Header: an accent "unsaved" dot, then the title on its baseline.
    const float padX = m_dialogRect.x + 24.0f;
    const float titleBaselineY = m_dialogRect.y + 40.0f;
    const float dotR = 4.0f;
    renderer.fillCircle({padX + dotR, titleBaselineY - 5.0f}, dotR, accent);
    renderer.fillCircle({padX + dotR, titleBaselineY - 5.0f}, dotR + 3.0f, accent.withAlpha(0.18f));
    renderer.drawText(m_title, AestraUI::NUIPoint(padX + dotR * 2.0f + 12.0f,
                                                   std::round(titleBaselineY - 13.0f)),
                      15.0f, titleColor);

    // Message.
    renderer.drawText(m_message, AestraUI::NUIPoint(padX, std::round(m_dialogRect.y + 66.0f)), 12.5f, messageColor);

    // Divider above the button row.
    const float dividerY = std::round(m_saveButtonRect.y - 16.0f);
    renderer.drawLine({m_dialogRect.x + 20.0f, dividerY}, {m_dialogRect.right() - 20.0f, dividerY}, 1.0f, divider);

    // --- Buttons ---
    // Cancel: ghost (border only).
    if (m_cancelHovered)
        renderer.fillRoundedRect(m_cancelButtonRect, 7.0f, AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.06f));
    renderer.strokeRoundedRect(m_cancelButtonRect, 7.0f, 1.0f, AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.14f));
    renderer.drawTextCentered("Cancel", m_cancelButtonRect, 13.0f, m_cancelHovered ? textLight : textMuted);

    // Don't Save: subtle filled + border.
    renderer.fillRoundedRect(m_dontSaveButtonRect, 7.0f,
                             m_dontSaveHovered ? AestraUI::NUIColor(0.20f, 0.20f, 0.23f, 1.0f)
                                               : AestraUI::NUIColor(0.145f, 0.145f, 0.165f, 1.0f));
    renderer.strokeRoundedRect(m_dontSaveButtonRect, 7.0f, 1.0f, AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.12f));
    renderer.drawTextCentered("Don't Save", m_dontSaveButtonRect, 13.0f, textLight);

    // Save: primary, filled accent with a soft hover glow.
    if (m_saveHovered) {
        AestraUI::NUIRect glow = m_saveButtonRect;
        glow.x -= 3.0f;
        glow.y -= 3.0f;
        glow.width += 6.0f;
        glow.height += 6.0f;
        renderer.fillRoundedRect(glow, 9.0f, accent.withAlpha(0.22f));
    }
    renderer.fillRoundedRect(m_saveButtonRect, 7.0f, m_saveHovered ? accentHover : accent);
    renderer.drawTextCentered("Save", m_saveButtonRect, 13.0f, textWhite);
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
