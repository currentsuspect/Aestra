// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Models/TrackManager.h"

namespace Aestra {
namespace Audio {

/// Tempo File > New Project starts at (the same as a fresh launch).
inline constexpr double kNewProjectTempo = 120.0;

/**
 * @brief Empty the whole project: clips, sources, patterns, Arsenal units,
 * channels and tracks, and the undo history (File > New Project).
 *
 * The history goes BEFORE the channels (#611): SetVolume, SetPan, SetMute,
 * SetSolo and the effect/plugin commands hold `MixerChannel&` to strips that
 * clearAllChannels() destroys, so New Project followed by Ctrl+Z undid an edit
 * through freed memory. Patterns, units and tracks are cleared because the reset
 * used to keep them: a new project still had the last project's Arsenal rack and
 * patterns, and every New Project stacked fifty more tracks on the old ones.
 * The caller rebuilds the defaults.
 */
inline void clearProjectContent(TrackManager& tm) {
    tm.getPlaylistModel().clear();
    tm.getSourceManager().clear();
    tm.getCommandHistory().clear();
    tm.getPatternManager().clear();
    tm.getUnitManager().clear();
    tm.clearAllChannels();
    tm.clearAllTracks();
}

} // namespace Audio
} // namespace Aestra
