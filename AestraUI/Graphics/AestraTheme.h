#pragma once

#include <glm/vec4.hpp>

namespace Aestra::UI::Graphics {

/**
 * @brief Semantic color palette for Aestra UI.
 * All colors are defined in Linear RGB space (0.0 - 1.0).
 * "The Synthetic Frontier" design system — void-black foundation, neon accents.
 */
struct AestraTheme {
    // -------------------------------------------------------------------------
    // Panel Surfaces
    // -------------------------------------------------------------------------

    // Void black base (#0e0e0e) — deep dark for long studio sessions.
    static constexpr glm::vec4 PanelBackground = glm::vec4(0.055f, 0.055f, 0.055f, 1.0f);

    // Ghost border at 15% white — hairline suggestion, not hard line.
    static constexpr glm::vec4 PanelBorder = glm::vec4(1.0f, 1.0f, 1.0f, 0.15f);

    // -------------------------------------------------------------------------
    // Accents & Typography
    // -------------------------------------------------------------------------

    // Neon purple (#cc97ff) — primary accent, active state, time-cursor.
    static constexpr glm::vec4 AccentPrimary = glm::vec4(0.800f, 0.592f, 1.000f, 1.0f);

    // Soft white (#e6e6eb) — never use pure white for text.
    static constexpr glm::vec4 TextPrimary = glm::vec4(0.902f, 0.902f, 0.922f, 1.0f);

    // Muted gray (#adaaaa) — secondary labels, per design doc.
    static constexpr glm::vec4 TextSecondary = glm::vec4(0.678f, 0.678f, 0.667f, 1.0f);

    // -------------------------------------------------------------------------
    // Semantic Status Colors
    // -------------------------------------------------------------------------

    // Success — saturated green.
    static constexpr glm::vec4 StatusSuccess = glm::vec4(0.25f, 0.75f, 0.35f, 1.0f);

    // Warning — amber.
    static constexpr glm::vec4 StatusWarning = glm::vec4(1.00f, 0.816f, 0.376f, 1.0f);

    // Critical — coral/pink (#ff6b9b).
    static constexpr glm::vec4 StatusCritical = glm::vec4(1.00f, 0.420f, 0.608f, 1.0f);
};

} // namespace Aestra::UI::Graphics
