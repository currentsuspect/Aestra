#pragma once

#include <glm/vec4.hpp>

namespace Aestra::UI::Graphics {

/**
 * @brief Semantic color palette for Aestra UI.
 * All colors are defined in Linear RGB space (0.0 - 1.0).
 * Tokens are strictly mapped to macOS Dark Mode values but adapted for cross-platform rendering.
 */
struct AestraTheme {
    // -------------------------------------------------------------------------
    // Panel Surfaces
    // -------------------------------------------------------------------------

    // Dark, desaturated grey (not pure black) for main panel backgrounds.
    static constexpr glm::vec4 PanelBackground = glm::vec4(0.11f, 0.11f, 0.11f, 1.0f);

    // White with low alpha (~9%) for subtle separation borders.
    static constexpr glm::vec4 PanelBorder = glm::vec4(1.0f, 1.0f, 1.0f, 0.09f);

    // -------------------------------------------------------------------------
    // Accents & Typography
    // -------------------------------------------------------------------------

    // A refined Indigo/Violet for primary interactions (replacing neon purple).
    static constexpr glm::vec4 AccentPrimary = glm::vec4(0.36f, 0.28f, 0.88f, 1.0f);

    // Primary text - 98% White.
    static constexpr glm::vec4 TextPrimary = glm::vec4(0.98f, 0.98f, 0.98f, 1.0f);

    // Secondary text - 60% White (using TextPrimary base with lower alpha).
    static constexpr glm::vec4 TextSecondary = glm::vec4(0.98f, 0.98f, 0.98f, 0.60f);

    // -------------------------------------------------------------------------
    // Semantic Status Colors
    // Desaturated to avoid eye strain ("Pro" application standard).
    // -------------------------------------------------------------------------

    // Success - Desaturated Green.
    static constexpr glm::vec4 StatusSuccess = glm::vec4(0.25f, 0.65f, 0.35f, 1.0f);

    // Warning - Desaturated Yellow.
    static constexpr glm::vec4 StatusWarning = glm::vec4(0.85f, 0.75f, 0.25f, 1.0f);

    // Critical - Desaturated Red.
    static constexpr glm::vec4 StatusCritical = glm::vec4(0.85f, 0.35f, 0.35f, 1.0f);
};

} // namespace Aestra::UI::Graphics
