// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The shared modal-dialog chrome (spec 2 §5). Declared in AestraPanelWindow.h,
// beside the class whose look it mirrors and whose TITLE_BAR_H/kRadius it uses —
// but defined here rather than in AestraPanelWindow.cpp on purpose.
//
// AestraPanelWindow.cpp references NUIPlatformBridge for its knob cursor
// capture. A static library only pulls in an object file when a symbol from it
// is needed, so putting these two functions there made every dialog that paints
// its own chrome drag the platform layer in behind them — which broke the link
// of the light UI test targets that deliberately do not link it.
#include "AestraPanelWindow.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"

#include <algorithm>

namespace AestraUI {

NUIRect dialogCloseButtonRect(const NUIRect& panel)
{
    return NUIRect{panel.right() - 28.0f, panel.y + 4.0f, 24.0f, 24.0f};
}

NUIRect drawDialogChrome(NUIRenderer& renderer, const DialogChrome& chrome)
{
    const NUIRect& b = chrome.panel;
    if (b.isEmpty()) return NUIRect();

    auto& theme = NUIThemeManager::getInstance();
    const NUIColor border = theme.getColor("borderSubtle");

    // Same surface, radius, border and title-bar height as AestraPanelWindow, so
    // a dialog and a plugin editor read as the same object family.
    renderer.fillRoundedRect(b, AestraPanelWindow::kRadius, theme.getColor("elevatedPanel"));

    renderer.drawLine({b.x, b.y + AestraPanelWindow::TITLE_BAR_H - 0.5f},
                      {b.right(), b.y + AestraPanelWindow::TITLE_BAR_H - 0.5f}, 0.5f, border);

    if (!chrome.title.empty()) {
        const NUIRect textRect{b.x, b.y, b.width, AestraPanelWindow::TITLE_BAR_H};
        renderer.drawText(chrome.title, {b.x + 12.0f, renderer.calculateTextY(textRect, 12.0f)}, 12.0f,
                          theme.getColor("textSecondary"));
    }

    if (chrome.showClose) {
        const NUIRect closeBtn = dialogCloseButtonRect(b);
        if (chrome.closeHovered) {
            renderer.fillRoundedRect(closeBtn, 6.0f, theme.getColor("error").withAlpha(0.22f));
        }
        const float cx = closeBtn.x + closeBtn.width * 0.5f;
        const float cy = closeBtn.y + closeBtn.height * 0.5f;
        constexpr float d = 5.0f;
        const NUIColor xColor =
            chrome.closeHovered ? theme.getColor("error") : theme.getColor("textDisabled");
        renderer.drawLine({cx - d, cy - d}, {cx + d, cy + d}, 1.5f, xColor);
        renderer.drawLine({cx + d, cy - d}, {cx - d, cy + d}, 1.5f, xColor);
    }

    renderer.strokeRoundedRect(b, AestraPanelWindow::kRadius, 1.0f, border);

    return NUIRect{b.x, b.y + AestraPanelWindow::TITLE_BAR_H, b.width,
                   std::max(0.0f, b.height - AestraPanelWindow::TITLE_BAR_H)};
}

} // namespace AestraUI
