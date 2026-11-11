// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "ConfirmationDialog.h"
#include "../AestraUI/Widgets/AestraPanelWindow.h"
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
    , m_saveAsBranchHovered(false)
    , m_cancelHovered(false)
{
}

void ConfirmationDialog::show(const std::string& title, const std::string& message, ResponseCallback callback) {
    m_title = title;
    m_message = message;
    m_callback = callback;
    m_response = DialogResponse::None;
    m_isConfirmMode = false;
    m_isVersionMode = false;
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
    m_isVersionMode = false;
    m_confirmLabel = confirmLabel.empty() ? "Confirm" : confirmLabel;
    m_focusIndex = 1; // default focus on the primary action (Confirm)
    m_pressedButton = DialogResponse::None;
    m_isVisible = true;
    setVisible(true);

    Log::info("[ConfirmationDialog] Showing (confirm): " + title);
}

void ConfirmationDialog::showVersionSwitch(const std::string& title, const std::string& message, ResponseCallback callback) {
    m_title = title;
    m_message = message;
    m_callback = callback;
    m_response = DialogResponse::None;
    m_isConfirmMode = false;
    m_isVersionMode = true;
    m_focusIndex = 3; // default focus on the primary action (Save)
    m_pressedButton = DialogResponse::None;
    m_isVisible = true;
    setVisible(true);

    Log::info("[ConfirmationDialog] Showing (version switch): " + title);
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
        case DialogResponse::SaveAsBranch: responseStr = "Save as an alt"; break;
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
    if (m_isVersionMode) {
        switch (index) {
            case 0: return DialogResponse::Cancel;
            case 1: return DialogResponse::DontSave;
            case 2: return DialogResponse::SaveAsBranch;
            case 3: return DialogResponse::Save;
            default: return DialogResponse::Save;
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
    const float dialogWidth = m_isVersionMode ? 500.0f : 400.0f;
    const float buttonHeight = theme.layout.dialogActionHeight;
    const float buttonSpacing = theme.spacingS;
    const float margin = theme.spacingL;
    // Sized to its content (SPEC 3 §3.2): title bar, the one-line message band, the divider
    // and the buttons. It was a fixed 172 + title bar: 172 was sized when the title was drawn
    // inside the body, so once the title moved into the shared title bar the message sat at
    // the top of an empty band.
    constexpr float kMessageBandH = 24.0f;
    constexpr float kDividerGap = 16.0f; // divider sits this far above the buttons
    const float dialogHeight =
        AestraUI::AestraPanelWindow::TITLE_BAR_H + margin + kMessageBandH + margin + kDividerGap + buttonHeight + margin;

    // Center dialog in parent (rounded so 1px strokes/text stay crisp).
    m_dialogRect.x = std::round(parentBounds.x + (parentBounds.width - dialogWidth) / 2.0f);
    m_dialogRect.y = std::round(parentBounds.y + (parentBounds.height - dialogHeight) / 2.0f);
    m_dialogRect.width = dialogWidth;
    m_dialogRect.height = dialogHeight;
    m_messageCentreY = m_dialogRect.y + AestraUI::AestraPanelWindow::TITLE_BAR_H + margin + kMessageBandH * 0.5f;

    // Right-aligned button row, primary rightmost — modern convention:
    // Save flow: [ Cancel ] [ Don't Save ] [ Save ].
    // Confirm flow: [ Cancel ] [ Confirm ].
    // Version switch: [ Cancel ] [ Discard ] [ Save as an alt ] [ Save ].
    const float cancelWidth = 84.0f;
    const float confirmWidth = 110.0f;
    const float saveWidth = 96.0f;
    const float dontSaveWidth = 104.0f;
    const float saveAsBranchWidth = 132.0f;
    const float totalWidth = m_isConfirmMode ? cancelWidth + confirmWidth + buttonSpacing
                             : m_isVersionMode
                                 ? cancelWidth + dontSaveWidth + saveAsBranchWidth + saveWidth + buttonSpacing * 3.0f
                                 : cancelWidth + dontSaveWidth + saveWidth + buttonSpacing * 2.0f;

    const float buttonY = m_dialogRect.y + dialogHeight - margin - buttonHeight;
    const float startX = m_dialogRect.right() - margin - totalWidth;

    m_cancelButtonRect = {startX, buttonY, cancelWidth, buttonHeight};
    if (m_isConfirmMode) {
        // The Don't Save slot does not exist in confirm mode; keep it empty so
        // no stale rect aliases a live button.
        m_dontSaveButtonRect = AestraUI::NUIRect{};
        m_saveAsBranchButtonRect = AestraUI::NUIRect{};
        m_saveButtonRect = {m_cancelButtonRect.right() + buttonSpacing, buttonY, confirmWidth, buttonHeight};
    } else {
        m_dontSaveButtonRect = {m_cancelButtonRect.right() + buttonSpacing, buttonY, dontSaveWidth, buttonHeight};
        if (m_isVersionMode) {
            m_saveAsBranchButtonRect = {m_dontSaveButtonRect.right() + buttonSpacing, buttonY, saveAsBranchWidth, buttonHeight};
            m_saveButtonRect = {m_saveAsBranchButtonRect.right() + buttonSpacing, buttonY, saveWidth, buttonHeight};
        } else {
            m_saveAsBranchButtonRect = AestraUI::NUIRect{};
            m_saveButtonRect = {m_dontSaveButtonRect.right() + buttonSpacing, buttonY, saveWidth, buttonHeight};
        }
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

    // Surface and title bar come from the shared dialog chrome (spec 2 §5), so
    // this reads as an Aestra panel rather than as a generic alert box. The
    // title moves into the title bar; the "unsaved" dot stays with the message,
    // which is where the state it marks actually is.
    AestraUI::DialogChrome chrome;
    chrome.panel = m_dialogRect;
    chrome.title = m_title;
    chrome.showClose = false; // Cancel is the dismissal; two would be ambiguous.
    const AestraUI::NUIRect content = AestraUI::drawDialogChrome(renderer, chrome);

    const float padX = content.x + theme.spacingL;
    // Message and its dot share one centre line: the text optically by cap height, the dot
    // geometrically (it used a hand-rolled messageY + 5).
    const float messageY = std::round(renderer.calculateOpticalTextY(m_messageCentreY, theme.fontSizeM));
    const float dotR = 4.0f;
    if (!m_isConfirmMode) {
        renderer.fillCircle({padX + dotR, m_messageCentreY}, dotR, theme.warning);
    }
    renderer.drawText(m_message,
                      AestraUI::NUIPoint(m_isConfirmMode ? padX : padX + dotR * 2.0f + 10.0f, messageY),
                      theme.fontSizeM, theme.textSecondary);

    // Divider above the button row.
    const float dividerY = std::round(m_saveButtonRect.y - 16.0f); // kDividerGap in calculateLayout
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
        // Don't Save / Discard: subtle filled + border. In the version switch the same slot is Discard.
        const bool dontSavePressed = m_pressedButton == DialogResponse::DontSave;
        renderer.fillRoundedRect(m_dontSaveButtonRect, theme.radiusM,
                                 dontSavePressed ? theme.pressed
                                                 : (m_dontSaveHovered ? theme.hover : theme.buttonBgDefault));
        renderer.strokeRoundedRect(m_dontSaveButtonRect, theme.radiusM, theme.layout.dividerWidth,
                                   theme.borderStrong);
        renderer.drawTextCentered(m_isVersionMode ? "Discard" : "Don't Save", m_dontSaveButtonRect, theme.fontSizeM,
                                  theme.error);

        if (m_isVersionMode) {
            // Save as an alt: the third answer, between discarding and saving into the open version.
            const bool branchPressed = m_pressedButton == DialogResponse::SaveAsBranch;
            renderer.fillRoundedRect(m_saveAsBranchButtonRect, theme.radiusM,
                                     branchPressed ? theme.pressed
                                                   : (m_saveAsBranchHovered ? theme.hover : theme.buttonBgDefault));
            renderer.strokeRoundedRect(m_saveAsBranchButtonRect, theme.radiusM, theme.layout.dividerWidth,
                                       theme.borderStrong);
            renderer.drawTextCentered("Save as an alt", m_saveAsBranchButtonRect, theme.fontSizeM, theme.textSecondary);
        }

        const bool savePressed = m_pressedButton == DialogResponse::Save;
        renderer.fillRoundedRect(m_saveButtonRect, theme.radiusM,
                                 savePressed ? theme.primaryPressed : (m_saveHovered ? theme.primaryHover : theme.primary));
        renderer.drawTextCentered("Save", m_saveButtonRect, theme.fontSizeM, theme.textOnPrimary);
    }

    // Keyboard focus highlight: a faint accent ring around the focused button
    // (Left/Right move it, Enter activates it).
    const AestraUI::NUIRect focusRect = m_isConfirmMode
        ? (m_focusIndex == 0 ? m_cancelButtonRect : m_saveButtonRect)
        : m_isVersionMode
            ? (m_focusIndex == 0 ? m_cancelButtonRect
               : m_focusIndex == 1 ? m_dontSaveButtonRect
               : m_focusIndex == 2 ? m_saveAsBranchButtonRect
                                   : m_saveButtonRect)
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
    m_saveAsBranchHovered = m_isVersionMode && m_saveAsBranchButtonRect.contains(mouseX, mouseY);
    m_saveHovered = m_saveButtonRect.contains(mouseX, mouseY);
    if (m_isConfirmMode) {
        if (m_cancelHovered) m_focusIndex = 0;
        else if (m_saveHovered) m_focusIndex = 1;
    } else if (m_cancelHovered) m_focusIndex = 0;
    else if (m_dontSaveHovered) m_focusIndex = 1;
    else if (m_saveAsBranchHovered) m_focusIndex = 2;
    else if (m_saveHovered) m_focusIndex = m_isVersionMode ? 3 : 2;

    // Arm on press, fire on release. Consuming BOTH the press and the release while
    // the dialog is visible prevents the release from clicking through to the app
    // behind once the dialog hides (the old code responded on press, so the release
    // reached whatever was underneath).
    if (event.button == AestraUI::NUIMouseButton::Left) {
        if (event.pressed) {
            if (m_saveHovered) m_pressedButton = m_isConfirmMode ? DialogResponse::Confirm : DialogResponse::Save;
            else if (m_saveAsBranchHovered) m_pressedButton = DialogResponse::SaveAsBranch;
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
                      (armed == DialogResponse::SaveAsBranch && m_saveAsBranchHovered) ||
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
            const int buttonCount = m_isConfirmMode ? 2 : (m_isVersionMode ? 4 : 3);
            m_focusIndex = (m_focusIndex - 1 + buttonCount) % buttonCount; // wrap left
            return true;
        }
        if (event.keyCode == AestraUI::NUIKeyCode::Right) {
            const int buttonCount = m_isConfirmMode ? 2 : (m_isVersionMode ? 4 : 3);
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
