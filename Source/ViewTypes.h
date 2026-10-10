// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

namespace Aestra {
namespace Audio {

/**
 * @brief Identifiers for major DAW view panels
 */
enum class ViewType {
    Mixer,
    Sequencer,
    PianoRoll,
    Playlist,
    History,
    Takes,
    /// Not a view. The number of views, so kAllViewTypes (FloatingPanelDescriptors.h)
    /// can static_assert that it lists every one. Keep it last.
    Count
};

} // namespace Audio
} // namespace Aestra
