// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Aestra {
namespace Audio {

/**
 * @brief Metadata-only ownership bridge model between Arsenal and Timeline.
 *
 * This enum does not control current render routing. Route/audio behavior stays
 * owned by ArsenalRouteMode + routeId compatibility until a later phase.
 */
enum class ArsenalBridgeMode : uint8_t {
    DraftOnly = 0,
    PreviewToMaster = 1,
    LinkedRack = 2,
    LocalCopy = 3,
    RenderedAudio = 4,
    FrozenAudio = 5,
};

/**
 * @brief Stable string representation for persistence/docs/tests.
 */
inline std::string toString(ArsenalBridgeMode mode) {
    switch (mode) {
    case ArsenalBridgeMode::DraftOnly: return "DraftOnly";
    case ArsenalBridgeMode::PreviewToMaster: return "PreviewToMaster";
    case ArsenalBridgeMode::LinkedRack: return "LinkedRack";
    case ArsenalBridgeMode::LocalCopy: return "LocalCopy";
    case ArsenalBridgeMode::RenderedAudio: return "RenderedAudio";
    case ArsenalBridgeMode::FrozenAudio: return "FrozenAudio";
    default:
        return "InvalidArsenalBridgeMode(" + std::to_string(static_cast<int>(mode)) + ")";
    }
}

/**
 * @brief Parse bridge mode from stable string token.
 * @return Parsed mode, or std::nullopt for unknown/invalid input.
 */
inline std::optional<ArsenalBridgeMode> arsenalBridgeModeFromString(std::string_view token) noexcept {
    if (token == "DraftOnly") return ArsenalBridgeMode::DraftOnly;
    if (token == "PreviewToMaster") return ArsenalBridgeMode::PreviewToMaster;
    if (token == "LinkedRack") return ArsenalBridgeMode::LinkedRack;
    if (token == "LocalCopy") return ArsenalBridgeMode::LocalCopy;
    if (token == "RenderedAudio") return ArsenalBridgeMode::RenderedAudio;
    if (token == "FrozenAudio") return ArsenalBridgeMode::FrozenAudio;
    return std::nullopt;
}

} // namespace Audio
} // namespace Aestra
