// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

namespace AestraUI {

/**
 * @brief How much a mixer strip shows (V81-M1, FD-28 point 4).
 *
 *  - Wide (120 px): every insert name and each send's level; about 11 channels at 1440.
 *  - Normal (84 px): the default; 16 channels at 1920 with the master and inspector.
 *  - Narrow (44 px): meter, fader, M S and the output's colour; 30+ channels at 1920.
 */
enum class MixerDensity { Wide, Normal, Narrow };

inline constexpr float mixerStripWidth(MixerDensity d) {
    return d == MixerDensity::Wide ? 120.0f : d == MixerDensity::Narrow ? 44.0f : 84.0f;
}

inline constexpr const char* mixerDensityName(MixerDensity d) {
    return d == MixerDensity::Wide ? "wide" : d == MixerDensity::Narrow ? "narrow" : "normal";
}

/// Unknown or empty text means Normal, so a damaged preference never leaves the mixer unusable.
inline MixerDensity mixerDensityFromName(const char* name) {
    if (!name) return MixerDensity::Normal;
    if (name[0] == 'w') return MixerDensity::Wide;
    if (name[0] == 'n' && name[1] == 'a') return MixerDensity::Narrow;
    return MixerDensity::Normal;
}

} // namespace AestraUI
