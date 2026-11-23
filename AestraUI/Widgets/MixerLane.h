// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cmath>

namespace AestraUI {
namespace MixerLane {

/**
 * @brief The lane of a strip: where the printed scale, the fader and the meters sit (Design Spec, Mixer, board
 *        p-mixer2 locked, variant F). Left to right: a 16px label column, 5px, a 38px track with the slot centred
 *        in it and ticks 8px long on both sides, 5px, then the 18px meter pair. Pure functions.
 */
inline constexpr float kLabelW = 16.0f;
inline constexpr float kGap = 5.0f;
inline constexpr float kTrackW = 38.0f;
inline constexpr float kMeterW = 18.0f;
inline constexpr float kTickLen = 8.0f;
inline constexpr float kTickInset = 1.0f; // ticks sit 1px in from the track's edge
inline constexpr float kFaderW = kLabelW + kGap + kTrackW; // the fader widget spans labels and track: 59
inline constexpr float kGroupW = kFaderW + kGap + kMeterW; // 82

/// Left edge of the lane group, centred in the strip's content column (whole pixels).
inline float groupX(float contentX, float contentW) { return std::floor(contentX + (contentW - kGroupW) * 0.5f); }

/// The fader widget's left edge, the track's and the meter's, from the group's left edge.
inline float faderX(float groupLeft) { return groupLeft; }
inline float trackX(float groupLeft) { return groupLeft + kLabelW + kGap; }
inline float meterX(float groupLeft) { return groupLeft + kFaderW + kGap; }

/// Slot centre, measured from the fader widget's left edge.
inline float slotCentre() { return kLabelW + kGap + kTrackW * 0.5f; }

/// Left tick's left edge and right tick's left edge, measured from the fader widget's left edge.
inline float leftTickX() { return kLabelW + kGap + kTickInset; }
inline float rightTickX() { return kLabelW + kGap + kTrackW - kTickInset - kTickLen; }

} // namespace MixerLane
} // namespace AestraUI
