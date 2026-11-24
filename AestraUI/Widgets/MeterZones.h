// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

namespace AestraUI {
namespace MeterZones {

/**
 * @brief Which colour band a meter level sits in (Design Spec, Mixer, board p-mixer2 locked).
 *
 * The zones are fractions of the lane's height, not dB thresholds: green below 70%, amber from 70% to 88%, red
 * from 88% up. Feed it the level's scale position (MixerScale::norm), so on the board scale the amber starts near
 * -2 dB and the red near +3 dB.
 */
enum class Zone { Green, Amber, Red };

inline constexpr float kAmberFrom = 0.70f;
inline constexpr float kRedFrom = 0.88f;

inline Zone meterZone(float fraction) {
    if (fraction >= kRedFrom)
        return Zone::Red;
    if (fraction >= kAmberFrom)
        return Zone::Amber;
    return Zone::Green;
}

} // namespace MeterZones
} // namespace AestraUI
