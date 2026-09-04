// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "MissingAssetsDialog.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../AestraCore/include/AestraLog.h"

#include <algorithm>

namespace Aestra {

namespace {
constexpr float DIALOG_WIDTH = 520.0f;
constexpr float TITLE_H = 34.0f;
constexpr float SUMMARY_H = 22.0f;
constexpr float HINT_H = 20.0f;
constexpr float ROW_H = 26.0f;
constexpr float BUTTON_W = 78.0f;
constexpr float FOOTER_H = 48.0f;
constexpr float MAX_ROWS = 6.0f;
} // namespace

void MissingAssetsDialog::show(std::vector<MissingEntry> entries, DismissedCallback onDismissed) {
    m_entries = std::move(entries);
    m_onDismissed = std::move(onDismissed);
    m_lastRequested.clear();
    m_failedPaths.clear();
    // New session: any relink worker from a previous session is stale on
    // arrival, even if it kept the dialog alive via shared_ptr.
    ++m_generation;
    m_hoveredRow = -1;
    m_pressedRow = -1;
    m_dismissHovered = false;
    m_dismissPressed = false;
    m_isVisible = true;
    setVisible(true);
    Log::warning("[MissingAssets] Dialog shown with " + std::to_string(m_entries.size()) + " missing asset(s)");
}

void MissingAssetsDialog::hide() {
    m_isVisible = false;
    setVisible(false);
}

void MissingAssetsDialog::markRelinked(const std::string& storedPath) {
    auto it = std::remove_if(m_entries.begin(), m_entries.end(),
                             [&storedPath](const MissingEntry& e) { return e.storedPath == storedPath; });
    if (it == m_entries.end()) {
        return;
    }
    m_entries.erase(it, m_entries.end());
    m_lastRequested.clear();
    m_failedPaths.erase(storedPath);
    setDirty(true);
    if (m_entries.empty()) {
        Log::info("[MissingAssets] All missing assets relinked; closing dialog");
        handleDismiss();
    }
}

void MissingAssetsDialog::markRelinkFailed(const std::string& storedPath) {
    // Tag the failed row: a failed relink (undecodable pick, path taken,
    // empty decode) leaves the source as the retryable placeholder it was,
    // and lastRequestedPath() names the entry it happened to.
    m_lastRequested = storedPath;
    m_failedPaths.insert(storedPath);
    setDirty(true);
}

void MissingAssetsDialog::calculateLayout() {
    auto bounds = getBounds();
    const auto& theme = AestraUI::NUIThemeManager::getInstance().getCurrentTheme();

    const float visibleRows = std::min<float>(static_cast<float>(m_entries.size()), MAX_ROWS);
    const float dialogHeight =
        TITLE_H + SUMMARY_H + HINT_H + visibleRows * ROW_H + FOOTER_H + theme.spacingM;

    const float dialogX = (bounds.width - DIALOG_WIDTH) * 0.5f;
    const float dialogY = (bounds.height - dialogHeight) * 0.5f;
    m_dialogRect = AestraUI::NUIRect(dialogX, dialogY, DIALOG_WIDTH, dialogHeight);

    const float contentX = dialogX + theme.spacingM;
    const float contentW = DIALOG_WIDTH - theme.spacingM * 2.0f;

    m_rowRects.clear();
    m_rowRects.reserve(m_entries.size());
    float rowY = dialogY + TITLE_H + SUMMARY_H + HINT_H;
    for (size_t i = 0; i < m_entries.size() && static_cast<float>(i) < MAX_ROWS; ++i) {
        m_rowRects.push_back(RowRects{
            AestraUI::NUIRect(contentX, rowY, contentW - BUTTON_W - theme.spacingS, ROW_H - 4.0f),
            AestraUI::NUIRect(contentX + contentW - BUTTON_W, rowY, BUTTON_W, ROW_H - 4.0f),
        });
        rowY += ROW_H;
    }

    const float buttonHeight = theme.layout.dialogActionHeight;
    m_dismissButtonRect = AestraUI::NUIRect(
        dialogX + (DIALOG_WIDTH - 180.0f) * 0.5f, dialogY + dialogHeight - buttonHeight - theme.spacingM, 180.0f,
        buttonHeight);
}

void MissingAssetsDialog::onRender(AestraUI::NUIRenderer& renderer) {
    if (!m_isVisible) return;

    calculateLayout();

    auto bounds = getBounds();
    const auto& theme = AestraUI::NUIThemeManager::getInstance().getCurrentTheme();

    renderer.fillRect(AestraUI::NUIRect(0, 0, bounds.width, bounds.height), theme.overlay);

    renderer.fillRoundedRect(m_dialogRect, theme.radiusL, theme.surfaceTertiary);
    renderer.strokeRoundedRect(m_dialogRect, theme.radiusL, theme.layout.dividerWidth, theme.borderStrong);

    const float contentX = m_dialogRect.x + theme.spacingM;
    const float contentW = DIALOG_WIDTH - theme.spacingM * 2.0f;

    float y = m_dialogRect.y + theme.spacingS;
    renderer.drawText("Missing audio files", AestraUI::NUIPoint(contentX, y), theme.fontSizeXL, theme.textPrimary);
    y = m_dialogRect.y + TITLE_H;
    const std::string summary =
        std::to_string(m_entries.size()) +
        (m_entries.size() == 1 ? " audio file is missing from this project."
                               : " audio files are missing from this project.");
    renderer.drawText(summary, AestraUI::NUIPoint(contentX, y), theme.fontSizeM, theme.textSecondary);
    y += SUMMARY_H;
    renderer.drawText("Affected clips play silent until their file is relinked.",
                      AestraUI::NUIPoint(contentX, y), theme.fontSizeS, theme.textMuted);

    for (size_t i = 0; i < m_entries.size() && static_cast<float>(i) < MAX_ROWS; ++i) {
        const auto& row = m_rowRects[i];
        const bool hovered = (static_cast<int>(i) == m_hoveredRow);
        const bool failed = m_failedPaths.count(m_entries[i].storedPath) != 0;
        if (hovered) {
            renderer.fillRoundedRect(row.pathRect, theme.radiusS, theme.hover.withAlpha(0.35f));
        }
        renderer.drawText(m_entries[i].storedPath, {row.pathRect.x + 4.0f, row.pathRect.y + 3.0f}, theme.fontSizeS,
                          failed ? theme.error : theme.textPrimary);

        const bool pressed = (static_cast<int>(i) == m_pressedRow);
        const auto buttonBg = pressed ? theme.primaryPressed : (hovered ? theme.primaryHover : theme.primary);
        renderer.fillRoundedRect(row.buttonRect, theme.radiusS, buttonBg);
        renderer.drawTextCentered(failed ? "Retry..." : "Relink...", row.buttonRect, theme.fontSizeS,
                                  theme.textOnPrimary);
    }

    if (m_entries.size() > static_cast<std::size_t>(MAX_ROWS)) {
        renderer.drawText("...and " + std::to_string(m_entries.size() - static_cast<std::size_t>(MAX_ROWS)) +
                              " more. Relink the ones above; they share folders.",
                          AestraUI::NUIPoint(contentX, m_dialogRect.bottom() - FOOTER_H - theme.spacingS),
                          theme.fontSizeS, theme.textMuted);
    }

    const auto dismissBg = m_dismissPressed ? theme.pressed : (m_dismissHovered ? theme.hover : theme.buttonBgDefault);
    renderer.fillRoundedRect(m_dismissButtonRect, theme.radiusM, dismissBg);
    renderer.strokeRoundedRect(m_dismissButtonRect, theme.radiusM, theme.layout.dividerWidth, theme.borderStrong);
    renderer.drawTextCentered("Keep placeholders", m_dismissButtonRect, theme.fontSizeM, theme.textSecondary);
}

bool MissingAssetsDialog::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    if (!m_isVisible) return false;

    calculateLayout();

    const float mx = event.position.x;
    const float my = event.position.y;

    int hoveredRow = -1;
    for (size_t i = 0; i < m_rowRects.size(); ++i) {
        if (m_rowRects[i].buttonRect.contains(mx, my) || m_rowRects[i].pathRect.contains(mx, my)) {
            hoveredRow = static_cast<int>(i);
            break;
        }
    }
    const bool dismissHovered = m_dismissButtonRect.contains(mx, my);
    if (hoveredRow != m_hoveredRow || dismissHovered != m_dismissHovered) {
        m_hoveredRow = hoveredRow;
        m_dismissHovered = dismissHovered;
        setDirty(true);
    }

    if (event.button == AestraUI::NUIMouseButton::Left) {
        if (event.pressed) {
            m_pressedRow = (hoveredRow >= 0 && m_rowRects[static_cast<std::size_t>(hoveredRow)].buttonRect.contains(mx, my))
                               ? hoveredRow
                               : -1;
            m_dismissPressed = dismissHovered;
            setDirty(true);
            return true;
        }
        if (event.released) {
            const int armedRow = m_pressedRow;
            const bool armedDismiss = m_dismissPressed;
            m_pressedRow = -1;
            m_dismissPressed = false;
            setDirty(true);
            if (armedRow >= 0 && armedRow < static_cast<int>(m_entries.size()) &&
                m_rowRects[static_cast<std::size_t>(armedRow)].buttonRect.contains(mx, my)) {
                m_lastRequested = m_entries[static_cast<std::size_t>(armedRow)].storedPath;
                // A new attempt clears the failed tag; markRelinkFailed
                // re-tags it if this try also fails.
                m_failedPaths.erase(m_lastRequested);
                if (m_relinkRequested) {
                    m_relinkRequested(m_entries[static_cast<std::size_t>(armedRow)]);
                }
            } else if (armedDismiss && dismissHovered) {
                handleDismiss();
            }
            return true;
        }
    }

    // Modal: block everything else while visible.
    return true;
}

bool MissingAssetsDialog::onKeyEvent(const AestraUI::NUIKeyEvent& event) {
    if (!m_isVisible) return false;

    if (event.pressed && event.keyCode == AestraUI::NUIKeyCode::Escape) {
        // Escape = keep placeholders (the non-destructive default).
        handleDismiss();
    }
    // Block all key events while visible.
    return true;
}

void MissingAssetsDialog::handleDismiss() {
    const std::size_t stillMissing = m_entries.size();
    // Dismissal ends the session: in-flight relink workers must drop their
    // completions on arrival (Escape = keep placeholders, and a later show()
    // starts a session whose same-path rows must not be touched by them).
    ++m_generation;
    hide();
    if (m_onDismissed) {
        m_onDismissed(stillMissing);
    }
}

} // namespace Aestra
