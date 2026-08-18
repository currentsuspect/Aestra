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
    m_isConfirmMode = false;
    m_focusIndex = 2; // default focus on the primary action (Save)
    m_pressedButton = DialogResponse::None;
    m_isVisible = true;
    setVisible(true);
    
    Log::info("[ConfirmationDialog] Showing: " + title);
}

void ConfirmationDialog::showConfirm(const std::string& title, const std::string& message,
                                     const std::string& confirmLabel, ResponseCallback callback) {
    m_title = title;
    m_message = message;
    m_callback = callback;
    m_response = DialogResponse::None;
    m_isConfirmMode = true;
    m_confirmLabel = confirmLabel.empty() ? "Confirm" : confirmLabel;
    m_focusIndex = 1; // default focus on the primary action (Confirm)
    m_pressedButton = DialogResponse::None;
    m_isVisible = true;
    setVisible(true);

    Log::info("[ConfirmationDialog] Showing (confirm): " + title);
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
        case DialogResponse::Confirm: responseStr = m_confirmLabel; break;
        default: responseStr = "Unknown"; break;
    }
    Log::info("[ConfirmationDialog] User selected: " + responseStr);
    
    hide();
    
    if (m_callback) {
        m_callback(response);
    }
}

DialogResponse ConfirmationDialog::responseForFocus(int index) const {
    if (m_isConfirmMode) {
        switch (index) {
            case 0: return DialogResponse::Cancel;
            case 1: return DialogResponse::Confirm;
            default: return DialogResponse::Confirm;
        }
    }
    switch (index) {
        case 0: return DialogResponse::Cancel;
        case 1: return DialogResponse::DontSave;
        case 2: return DialogResponse::Save;
        default: return DialogResponse::Save;
    }
}

void ConfirmationDialog::calculateLayout() {
    AestraUI::NUIRect parentBounds = getBounds();
    const auto& theme = AestraUI::NUIThemeManager::getInstance().getCurrentTheme();

    // Dialog dimensions
    const float dialogWidth = 400.0f;
    const float dialogHeight = 172.0f;
    const float buttonHeight = theme.layout.dialogActionHeight;
    const float buttonSpacing = theme.spacingS;
    const float margin = theme.spacingL;

    // Center dialog in parent (rounded so 1px strokes/text stay crisp).
    m_dialogRect.x = std::round(parentBounds.x + (parentBounds.width - dialogWidth) / 2.0f);
    m_dialogRect.y = std::round(parentBounds.y + (parentBounds.height - dialogHeight) / 2.0f);
    m_dialogRect.width = dialogWidth;
    m_dialogRect.height = dialogHeight;

    // Right-aligned button row, primary rightmost — modern convention:
    // Save flow: [ Cancel ] [ Don't Save ] [ Save ].
    // Confirm flow: [ Cancel ] [ Confirm ].
    const float cancelWidth = 84.0f;
    const float confirmWidth = 110.0f;
    const float saveWidth = 96.0f;
    const float dontSaveWidth = 104.0f;
    const float totalWidth = m_isConfirmMode
        ? cancelWidth + confirmWidth + buttonSpacing
        : cancelWidth + dontSaveWidth + saveWidth + buttonSpacing * 2.0f;

    const float buttonY = m_dialogRect.y + dialogHeight - margin - buttonHeight;
    const float startX = m_dialogRect.right() - margin - totalWidth;

    m_cancelButtonRect = {startX, buttonY, cancelWidth, buttonHeight};
    if (m_isConfirmMode) {
        // The Don't Save slot does not exist in confirm mode; keep it empty so
        // no stale rect aliases a live button.
        m_dontSaveButtonRect = AestraUI::NUIRect{};
        m_saveButtonRect = {m_cancelButtonRect.right() + buttonSpacing, buttonY, confirmWidth, buttonHeight};
    } else {
        m_dontSaveButtonRect = {m_cancelButtonRect.right() + buttonSpacing, buttonY, dontSaveWidth, buttonHeight};
        m_saveButtonRect = {m_dontSaveButtonRect.right() + buttonSpacing, buttonY, saveWidth, buttonHeight};
    }
}

void ConfirmationDialog::onRender(AestraUI::NUIRenderer& renderer) {
    if (!m_isVisible) {
        return;
    }
    
    calculateLayout();
    const auto& theme = AestraUI::NUIThemeManager::getInstance().getCurrentTheme();

    // Dim the app behind the modal.
    const AestraUI::NUIRect parentBounds = getBounds();
    renderer.fillRect(parentBounds, theme.overlay);

    // Soft drop shadow (offsetX, offsetY, blur, color).
    renderer.drawShadow(m_dialogRect, 0.0f, theme.spacingS, theme.spacingL, theme.shadow);

    // Dialog surface.
    renderer.fillRoundedRect(m_dialogRect, theme.radiusL, theme.surfaceTertiary);
    renderer.strokeRoundedRect(m_dialogRect, theme.radiusL, theme.layout.dividerWidth, theme.borderStrong);

    // Header: an accent "unsaved" dot, then the title on its baseline.
    const float padX = m_dialogRect.x + theme.spacingL;
    const float titleBaselineY = m_dialogRect.y + 40.0f;
    const float dotR = 4.0f;
    renderer.fillCircle({padX + dotR, titleBaselineY - 5.0f}, dotR, theme.warning);
    renderer.drawText(m_title, AestraUI::NUIPoint(padX + dotR * 2.0f + 12.0f,
                                                   std::round(titleBaselineY - 13.0f)),
                      theme.fontSizeXL, theme.textPrimary);

    // Message.
    renderer.drawText(m_message, AestraUI::NUIPoint(padX, std::round(m_dialogRect.y + 66.0f)),
                      theme.fontSizeM, theme.textSecondary);

    // Divider above the button row.
    const float dividerY = std::round(m_saveButtonRect.y - 16.0f);
    renderer.drawLine({m_dialogRect.x + theme.spacingM, dividerY},
                      {m_dialogRect.right() - theme.spacingM, dividerY},
                      theme.layout.dividerWidth, theme.borderSubtle);

    // --- Buttons ---
    // Cancel: ghost (border only).
    const bool cancelPressed = m_pressedButton == DialogResponse::Cancel;
    renderer.fillRoundedRect(m_cancelButtonRect, theme.radiusM,
                             cancelPressed ? theme.pressed : (m_cancelHovered ? theme.hover : theme.buttonBgDefault));
    renderer.strokeRoundedRect(m_cancelButtonRect, theme.radiusM, theme.layout.dividerWidth,
                               theme.borderStrong);
    renderer.drawTextCentered("Cancel", m_cancelButtonRect, theme.fontSizeM,
                              m_cancelHovered ? theme.textPrimary : theme.textSecondary);

    if (m_isConfirmMode) {
        // Confirm: primary fill, mirrors the Save slot.
        const bool confirmPressed = m_pressedButton == DialogResponse::Confirm;
        renderer.fillRoundedRect(m_saveButtonRect, theme.radiusM,
                                 confirmPressed ? theme.primaryPressed
                                                : (m_saveHovered ? theme.primaryHover : theme.primary));
        renderer.drawTextCentered(m_confirmLabel, m_saveButtonRect, theme.fontSizeM, theme.textOnPrimary);
    } else {
        // Don't Save: subtle filled + border.
        const bool dontSavePressed = m_pressedButton == DialogResponse::DontSave;
        renderer.fillRoundedRect(m_dontSaveButtonRect, theme.radiusM,
                                 dontSavePressed ? theme.pressed
                                                 : (m_dontSaveHovered ? theme.hover : theme.buttonBgDefault));
        renderer.strokeRoundedRect(m_dontSaveButtonRect, theme.radiusM, theme.layout.dividerWidth,
                                   theme.borderStrong);
        renderer.drawTextCentered("Don't Save", m_dontSaveButtonRect, theme.fontSizeM, theme.error);

        const bool savePressed = m_pressedButton == DialogResponse::Save;
        renderer.fillRoundedRect(m_saveButtonRect, theme.radiusM,
                                 savePressed ? theme.primaryPressed : (m_saveHovered ? theme.primaryHover : theme.primary));
        renderer.drawTextCentered("Save", m_saveButtonRect, theme.fontSizeM, theme.textOnPrimary);
    }

    // Keyboard focus highlight: a faint accent ring around the focused button
    // (Left/Right move it, Enter activates it).
    const AestraUI::NUIRect focusRect = m_isConfirmMode
        ? (m_focusIndex == 0 ? m_cancelButtonRect : m_saveButtonRect)
        : (m_focusIndex == 0) ? m_cancelButtonRect : (m_focusIndex == 1) ? m_dontSaveButtonRect : m_saveButtonRect;
    AestraUI::NUIRect ring = focusRect;
    ring.x -= 2.0f;
    ring.y -= 2.0f;
    ring.width += 4.0f;
    ring.height += 4.0f;
    renderer.strokeRoundedRect(ring, theme.radiusL, 1.5f, theme.focusRing);
}

bool ConfirmationDialog::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    if (!m_isVisible) {
        return false;
    }
    
    calculateLayout();
    
    float mouseX = event.position.x;
    float mouseY = event.position.y;
    
    // Update hover states; hovering also moves keyboard focus so the two stay in sync.
    m_cancelHovered = m_cancelButtonRect.contains(mouseX, mouseY);
    m_dontSaveHovered = !m_isConfirmMode && m_dontSaveButtonRect.contains(mouseX, mouseY);
    m_saveHovered = m_saveButtonRect.contains(mouseX, mouseY);
    if (m_isConfirmMode) {
        if (m_cancelHovered) m_focusIndex = 0;
        else if (m_saveHovered) m_focusIndex = 1;
    } else if (m_cancelHovered) m_focusIndex = 0;
    else if (m_dontSaveHovered) m_focusIndex = 1;
    else if (m_saveHovered) m_focusIndex = 2;

    // Arm on press, fire on release. Consuming BOTH the press and the release while
    // the dialog is visible prevents the release from clicking through to the app
    // behind once the dialog hides (the old code responded on press, so the release
    // reached whatever was underneath).
    if (event.button == AestraUI::NUIMouseButton::Left) {
        if (event.pressed) {
            if (m_saveHovered) m_pressedButton = m_isConfirmMode ? DialogResponse::Confirm : DialogResponse::Save;
            else if (m_dontSaveHovered) m_pressedButton = DialogResponse::DontSave;
            else if (m_cancelHovered) m_pressedButton = DialogResponse::Cancel;
            else if (!m_dialogRect.contains(mouseX, mouseY)) m_pressedButton = DialogResponse::Cancel; // click-outside
            else m_pressedButton = DialogResponse::None;
            return true;
        }
        if (event.released) {
            const DialogResponse armed = m_pressedButton;
            m_pressedButton = DialogResponse::None;
            // Only fire if the release lands on the same button that was pressed.
            const bool overArmed = m_isConfirmMode
                ? (armed == DialogResponse::Confirm && m_saveHovered) ||
                      (armed == DialogResponse::Cancel &&
                       (m_cancelHovered || !m_dialogRect.contains(mouseX, mouseY)))
                : (armed == DialogResponse::Save && m_saveHovered) ||
                      (armed == DialogResponse::DontSave && m_dontSaveHovered) ||
                      (armed == DialogResponse::Cancel &&
                       (m_cancelHovered || !m_dialogRect.contains(mouseX, mouseY)));
            if (armed != DialogResponse::None && overArmed) {
                handleResponse(armed);
            }
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
        // Escape = Cancel (regardless of focus).
        if (event.keyCode == AestraUI::NUIKeyCode::Escape) {
            handleResponse(DialogResponse::Cancel);
            return true;
        }

        // Left/Right move the focus highlight across the button row.
        if (event.keyCode == AestraUI::NUIKeyCode::Left) {
            const int buttonCount = m_isConfirmMode ? 2 : 3;
            m_focusIndex = (m_focusIndex - 1 + buttonCount) % buttonCount; // wrap left
            return true;
        }
        if (event.keyCode == AestraUI::NUIKeyCode::Right) {
            const int buttonCount = m_isConfirmMode ? 2 : 3;
            m_focusIndex = (m_focusIndex + 1) % buttonCount; // wrap right
            return true;
        }

        // Enter/Tab-less activate: fire the focused button.
        if (event.keyCode == AestraUI::NUIKeyCode::Enter) {
            handleResponse(responseForFocus(m_focusIndex));
            return true;
        }
    }

    // Consume all key events while dialog is visible
    return true;
}

} // namespace Aestra
