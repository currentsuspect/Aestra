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
#include "../Graphics/NUIDepth.h"
#include "NUIThemeSystem.h"

#include <algorithm>

namespace AestraUI {

namespace {
constexpr float kDialogRadius = 12.0f; // Design Spec, Dialog
constexpr float kDialogPad = 18.0f;
} // namespace

NUIRect dialogCloseButtonRect(const NUIRect& panel)
{
    return NUIRect{panel.right() - 34.0f, panel.y + 8.0f, 24.0f, 24.0f};
}

NUIRect drawDialogChrome(NUIRenderer& renderer, const DialogChrome& chrome)
{
    const NUIRect& b = chrome.panel;
    if (b.isEmpty()) return NUIRect();

    auto& theme = NUIThemeManager::getInstance();

    // Design Spec, Dialog (locked 12 Nov 2026): a floating card of backgroundSecondary, 12px round, on a deep soft
    // shadow, with no outline and no rule under the heading. The question is the heading, in the panel size.
    Depth::floatingCard(renderer, b, kDialogRadius, theme.getColor("backgroundSecondary"), 2.0f);

    if (!chrome.title.empty()) {
        const NUIRect textRect{b.x, b.y + 4.0f, b.width, AestraPanelWindow::TITLE_BAR_H};
        renderer.drawText(chrome.title, {b.x + kDialogPad, renderer.calculateTextY(textRect, 16.0f)}, 16.0f,
                          theme.getColor("textPrimary"));
    }

    if (chrome.showClose) {
        // Esc and Cancel are the way out; the close mark stays quiet, never a red alarm.
        const NUIRect closeBtn = dialogCloseButtonRect(b);
        if (chrome.closeHovered)
            renderer.fillRoundedRect(closeBtn, Depth::Radius::pill(closeBtn.height), theme.getColor("hover"));
        const float cx = closeBtn.x + closeBtn.width * 0.5f;
        const float cy = closeBtn.y + closeBtn.height * 0.5f;
        constexpr float d = 4.5f;
        const NUIColor xColor = chrome.closeHovered ? theme.getColor("textPrimary") : theme.getColor("textMuted");
        renderer.drawLine({cx - d, cy - d}, {cx + d, cy + d}, 1.5f, xColor);
        renderer.drawLine({cx + d, cy - d}, {cx - d, cy + d}, 1.5f, xColor);
    }

    return NUIRect{b.x, b.y + AestraPanelWindow::TITLE_BAR_H, b.width,
                   std::max(0.0f, b.height - AestraPanelWindow::TITLE_BAR_H)};
}

} // namespace AestraUI
