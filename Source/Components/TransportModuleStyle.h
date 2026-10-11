// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file TransportModuleStyle.h
 * @brief One visual grammar for every module in the transport row.
 *
 * The transport reads as a row of labelled modules, like the front panel of a
 * piece of hardware: a small caps label names each module, its content sits on
 * one shared line below, and hairline dividers set the modules apart. Position,
 * tempo, record, panels, keys and output all use these numbers, so a module
 * never drifts a pixel from its neighbours.
 */

#pragma once

#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"

#include <cmath>

namespace Aestra::TransportModule {

// Vertical rhythm inside the 56 px transport row: label, then content.
constexpr float kLabelFontSize = 9.0f;
constexpr float kLabelTop = 8.0f;
constexpr float kLabelHeight = 12.0f;
constexpr float kContentTop = 22.0f;
constexpr float kContentHeight = 28.0f;

// Horizontal rhythm: every module pads its content by the same amount.
constexpr float kPadX = 14.0f;
constexpr float kDividerInset = 10.0f;

/** @brief Draw a module's caps label at the module's content x. */
inline void drawLabel(AestraUI::NUIRenderer& renderer, const char* text, float x, float barTop) {
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    const AestraUI::NUIRect labelRect(x, barTop + kLabelTop, 240.0f, kLabelHeight);
    renderer.drawText(text, {x, renderer.calculateTextY(labelRect, kLabelFontSize)}, kLabelFontSize,
                      theme.getColor("textMuted"));
}

/** @brief Hairline between two modules, inset from the row's top and bottom. */
inline void drawDivider(AestraUI::NUIRenderer& renderer, float x, float barTop, float barHeight) {
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    const float px = std::round(x) + 0.5f;
    renderer.drawLine({px, barTop + kDividerInset}, {px, barTop + barHeight - kDividerInset}, 1.0f,
                      theme.getColor("divider"));
}

/** @brief The shared content line of a module that starts at x. */
inline AestraUI::NUIRect contentRect(float x, float width, float barTop) {
    return AestraUI::NUIRect(x, barTop + kContentTop, width, kContentHeight);
}

} // namespace Aestra::TransportModule
