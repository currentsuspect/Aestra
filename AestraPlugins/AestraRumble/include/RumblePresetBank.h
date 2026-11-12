// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Rumble's starting sounds: the seven the founder played in the approved sketch (FD-80, SPEC-025), with the
// sketch's values. Settings in RumbleSound order: Punch, Tail, Weight, Tone, Dirt, Slide, Drop time, Hold,
// Curve, 2nd, 3rd, Clip type, Click, Click length, Click tone.
#pragma once

#include "RumbleVoice.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Aestra {
namespace Plugins {

constexpr size_t kRumblePresetParameterCount = RumbleSound::Count;

struct RumbleFactoryPreset {
    std::string_view name;
    std::string_view category;
    std::string_view description;
    std::array<float, kRumblePresetParameterCount> values;
    uint32_t seed;
};

inline constexpr std::array<RumbleFactoryPreset, 7> kRumbleFactoryPresets = {{
    {"Balanced 808", "Start from", "The all-round 808 every sound in the sketch began from.",
     {0.42f, 0.55f, 0.5f, 0.5f, 0.3f, 0.0f, 0.35f, 0.3f, 0.5f, 0.3f, 0.2f, 0.1f, 0.35f, 0.3f, 0.4f}, 1000u},
    {"Pure sub", "Start from", "Almost all clean sine, a gentle drop and barely any click.",
     {0.15f, 0.6f, 0.88f, 0.25f, 0.04f, 0.0f, 0.35f, 0.3f, 0.5f, 0.1f, 0.05f, 0.1f, 0.08f, 0.3f, 0.4f}, 1001u},
    {"Tight punch", "Start from", "Short, hard at the front, with a clear click.",
     {0.75f, 0.2f, 0.4f, 0.6f, 0.35f, 0.0f, 0.2f, 0.3f, 0.5f, 0.3f, 0.2f, 0.1f, 0.7f, 0.3f, 0.4f}, 1002u},
    {"Long tail", "Start from", "Rings long, with a rounder decay.",
     {0.3f, 0.9f, 0.62f, 0.4f, 0.2f, 0.0f, 0.35f, 0.3f, 0.7f, 0.3f, 0.2f, 0.1f, 0.35f, 0.3f, 0.4f}, 1003u},
    {"Dark trap", "Start from", "Long and low, with a slow pitch drop and some grit.",
     {0.5f, 0.76f, 0.7f, 0.2f, 0.46f, 0.0f, 0.6f, 0.3f, 0.5f, 0.3f, 0.2f, 0.1f, 0.35f, 0.3f, 0.4f}, 1004u},
    {"Hard clip", "Start from", "Driven into hard clipping, bright and loud on small speakers.",
     {0.55f, 0.6f, 0.3f, 0.76f, 0.86f, 0.0f, 0.35f, 0.3f, 0.5f, 0.3f, 0.5f, 0.5f, 0.35f, 0.3f, 0.4f}, 1005u},
    {"Glide lead", "Start from", "Bright and driven, and slides between overlapping notes.",
     {0.25f, 0.66f, 0.35f, 0.7f, 0.5f, 0.5f, 0.35f, 0.3f, 0.5f, 0.5f, 0.2f, 0.1f, 0.35f, 0.3f, 0.4f}, 1006u},
}};

inline RumbleSound rumbleSoundOf(const RumbleFactoryPreset& preset) {
    RumbleSound sound;
    for (size_t id = 0; id < RumbleSound::Count; ++id) {
        sound[id] = preset.values[id];
    }
    sound.seed = preset.seed;
    return sound;
}

// A new Rumble opens on Balanced 808.
inline RumbleSound rumbleDefaultSound() {
    return rumbleSoundOf(kRumbleFactoryPresets[0]);
}

} // namespace Plugins
} // namespace Aestra
