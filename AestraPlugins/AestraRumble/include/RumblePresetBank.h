#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace Aestra {
namespace Plugins {

constexpr size_t kRumblePresetParameterCount = 25;

struct RumbleFactoryPreset {
    std::string_view name;
    std::string_view category;
    std::string_view description;
    std::array<float, kRumblePresetParameterCount> values;
};

inline constexpr std::array<float, kRumblePresetParameterCount> kRumbleDefaultValues = {
    0.45f,          // AmpDecay
    0.10f,          // Drive
    0.30f,          // Tone
    0.50f,          // OutputGain
    0.35f,          // PitchAmount
    0.25f,          // PitchDecay
    0.42f,          // PitchCurve
    0.04081632653f, // AmpAttack
    0.55f,          // Resonance
    0.48f,          // TransientAmount
    0.25f,          // ClickLevel
    0.20f,          // ClickDecay
    0.35f,          // ClickTone
    0.15f,          // GlideTime
    0.50f,          // GlideCurve
    0.0f,           // GlideMode
    0.0f,           // RetriggerMode
    0.50f,          // FilterEnvAmount
    0.0f,           // FilterKeytrack
    0.0f,           // SatMode
    0.75f,          // VelocityToAmp
    0.50f,          // Tune
    0.50f,          // Fine
    0.22f,          // Harmonics
    0.70f,          // SubClean
};

inline constexpr auto kRumbleFactoryPresets = [] {
    std::array<RumbleFactoryPreset, 16> presets{};

    presets[0] = {"Pure Sub", "Essentials", "Clean, centered fundamental with a restrained pitch drop.",
                  kRumbleDefaultValues};
    presets[0].values[0] = 0.55f;
    presets[0].values[1] = 0.0f;
    presets[0].values[2] = 0.52f;
    presets[0].values[3] = 0.46f;
    presets[0].values[4] = 0.12f;
    presets[0].values[5] = 0.12f;
    presets[0].values[8] = 0.25f;
    presets[0].values[9] = 0.32f;
    presets[0].values[10] = 0.04f;
    presets[0].values[23] = 0.0f;
    presets[0].values[24] = 1.0f;

    presets[1] = {"Balanced 808", "Essentials", "Finished default for melodic trap and hip-hop basslines.",
                  kRumbleDefaultValues};
    presets[1].values[0] = 0.58f;
    presets[1].values[1] = 0.14f;
    presets[1].values[2] = 0.46f;
    presets[1].values[3] = 0.47f;
    presets[1].values[4] = 0.30f;
    presets[1].values[9] = 0.58f;
    presets[1].values[23] = 0.30f;
    presets[1].values[24] = 0.82f;

    presets[2] = {"Tight Punch", "Essentials", "Short tail, assertive pitch strike, and defined click.",
                  kRumbleDefaultValues};
    presets[2].values[0] = 0.25f;
    presets[2].values[1] = 0.12f;
    presets[2].values[2] = 0.58f;
    presets[2].values[3] = 0.47f;
    presets[2].values[4] = 0.46f;
    presets[2].values[5] = 0.08f;
    presets[2].values[6] = 0.65f;
    presets[2].values[7] = 0.0f;
    presets[2].values[9] = 0.82f;
    presets[2].values[10] = 0.45f;
    presets[2].values[11] = 0.10f;
    presets[2].values[12] = 0.65f;
    presets[2].values[23] = 0.25f;

    presets[3] = {"Long Tail", "Essentials", "Deep sustained tail with a soft, classic front edge.",
                  kRumbleDefaultValues};
    presets[3].values[0] = 0.82f;
    presets[3].values[1] = 0.07f;
    presets[3].values[2] = 0.40f;
    presets[3].values[3] = 0.44f;
    presets[3].values[4] = 0.18f;
    presets[3].values[5] = 0.22f;
    presets[3].values[7] = 0.02f;
    presets[3].values[9] = 0.42f;
    presets[3].values[10] = 0.08f;
    presets[3].values[23] = 0.18f;
    presets[3].values[24] = 0.94f;

    presets[4] = {"Phone Ready", "Modern", "Strong upper harmonics without abandoning the sub fundamental.",
                  kRumbleDefaultValues};
    presets[4].values[0] = 0.52f;
    presets[4].values[1] = 0.34f;
    presets[4].values[2] = 0.72f;
    presets[4].values[3] = 0.42f;
    presets[4].values[4] = 0.26f;
    presets[4].values[8] = 0.40f;
    presets[4].values[9] = 0.60f;
    presets[4].values[10] = 0.20f;
    presets[4].values[12] = 0.62f;
    presets[4].values[23] = 0.78f;
    presets[4].values[24] = 0.72f;

    presets[5] = {"Clean R&B", "Modern", "Rounded note starts and a polished, nearly clean tail.",
                  kRumbleDefaultValues};
    presets[5].values[0] = 0.67f;
    presets[5].values[1] = 0.04f;
    presets[5].values[2] = 0.50f;
    presets[5].values[3] = 0.45f;
    presets[5].values[4] = 0.14f;
    presets[5].values[7] = 0.10f;
    presets[5].values[9] = 0.34f;
    presets[5].values[10] = 0.03f;
    presets[5].values[13] = 0.24f;
    presets[5].values[15] = 1.0f;
    presets[5].values[23] = 0.24f;
    presets[5].values[24] = 0.96f;

    presets[6] = {"Dark Trap", "Modern", "Low-passed weight with controlled grit and a long pitch gesture.",
                  kRumbleDefaultValues};
    presets[6].values[0] = 0.72f;
    presets[6].values[1] = 0.24f;
    presets[6].values[2] = 0.32f;
    presets[6].values[3] = 0.44f;
    presets[6].values[4] = 0.40f;
    presets[6].values[5] = 0.34f;
    presets[6].values[6] = 0.52f;
    presets[6].values[9] = 0.56f;
    presets[6].values[10] = 0.12f;
    presets[6].values[23] = 0.38f;
    presets[6].values[24] = 0.84f;

    presets[7] = {"Glide Lead", "Modern", "Legato mono glide with enough harmonic information to lead a hook.",
                  kRumbleDefaultValues};
    presets[7].values[0] = 0.76f;
    presets[7].values[1] = 0.22f;
    presets[7].values[2] = 0.66f;
    presets[7].values[3] = 0.41f;
    presets[7].values[4] = 0.10f;
    presets[7].values[7] = 0.05f;
    presets[7].values[9] = 0.28f;
    presets[7].values[10] = 0.0f;
    presets[7].values[13] = 0.42f;
    presets[7].values[14] = 0.45f;
    presets[7].values[15] = 1.0f;
    presets[7].values[16] = 1.0f;
    presets[7].values[18] = 0.55f;
    presets[7].values[23] = 0.58f;
    presets[7].values[24] = 0.80f;

    presets[8] = {"Tape Weight", "Character", "Soft saturation and restrained highs for sampled-record warmth.",
                  kRumbleDefaultValues};
    presets[8].values[0] = 0.62f;
    presets[8].values[1] = 0.40f;
    presets[8].values[2] = 0.50f;
    presets[8].values[3] = 0.42f;
    presets[8].values[4] = 0.22f;
    presets[8].values[8] = 0.48f;
    presets[8].values[9] = 0.48f;
    presets[8].values[10] = 0.12f;
    presets[8].values[19] = 0.0f;
    presets[8].values[23] = 0.46f;
    presets[8].values[24] = 0.84f;

    presets[9] = {"Hard Clip", "Character", "Aggressive hard saturation with deliberate midrange projection.",
                  kRumbleDefaultValues};
    presets[9].values[0] = 0.55f;
    presets[9].values[1] = 0.72f;
    presets[9].values[2] = 0.76f;
    presets[9].values[3] = 0.36f;
    presets[9].values[4] = 0.28f;
    presets[9].values[9] = 0.68f;
    presets[9].values[10] = 0.18f;
    presets[9].values[19] = 1.0f;
    presets[9].values[23] = 0.68f;
    presets[9].values[24] = 0.54f;

    presets[10] = {"Knock", "Character", "Pronounced resonant front with a compact, chest-focused body.",
                   kRumbleDefaultValues};
    presets[10].values[0] = 0.34f;
    presets[10].values[1] = 0.18f;
    presets[10].values[2] = 0.60f;
    presets[10].values[3] = 0.45f;
    presets[10].values[4] = 0.54f;
    presets[10].values[5] = 0.10f;
    presets[10].values[6] = 0.72f;
    presets[10].values[8] = 0.66f;
    presets[10].values[9] = 0.92f;
    presets[10].values[10] = 0.24f;
    presets[10].values[23] = 0.34f;
    presets[10].values[24] = 0.78f;

    presets[11] = {"Hollow", "Character", "Resonant, filtered body with a scooped and synthetic identity.",
                   kRumbleDefaultValues};
    presets[11].values[0] = 0.60f;
    presets[11].values[1] = 0.20f;
    presets[11].values[2] = 0.42f;
    presets[11].values[3] = 0.43f;
    presets[11].values[4] = 0.32f;
    presets[11].values[8] = 0.86f;
    presets[11].values[9] = 0.52f;
    presets[11].values[10] = 0.10f;
    presets[11].values[17] = 0.30f;
    presets[11].values[23] = 0.58f;
    presets[11].values[24] = 0.76f;

    presets[12] = {"Kick Layer", "Utility", "Short percussive strike designed to sit above a separate sub bass.",
                   kRumbleDefaultValues};
    presets[12].values[0] = 0.16f;
    presets[12].values[1] = 0.16f;
    presets[12].values[2] = 0.72f;
    presets[12].values[3] = 0.46f;
    presets[12].values[4] = 0.62f;
    presets[12].values[5] = 0.05f;
    presets[12].values[6] = 0.76f;
    presets[12].values[7] = 0.0f;
    presets[12].values[9] = 1.0f;
    presets[12].values[10] = 0.78f;
    presets[12].values[11] = 0.08f;
    presets[12].values[12] = 0.78f;
    presets[12].values[23] = 0.16f;
    presets[12].values[24] = 0.88f;

    presets[13] = {"Bass Only", "Utility", "Soft onset and no click for layering under a separate kick.",
                   kRumbleDefaultValues};
    presets[13].values[0] = 0.78f;
    presets[13].values[1] = 0.08f;
    presets[13].values[2] = 0.44f;
    presets[13].values[3] = 0.45f;
    presets[13].values[4] = 0.08f;
    presets[13].values[7] = 0.18f;
    presets[13].values[9] = 0.26f;
    presets[13].values[10] = 0.0f;
    presets[13].values[23] = 0.22f;
    presets[13].values[24] = 1.0f;

    presets[14] = {"Mix Safe", "Utility", "Conservative level, moderate translation, and controlled tail length.",
                   kRumbleDefaultValues};
    presets[14].values[0] = 0.44f;
    presets[14].values[1] = 0.12f;
    presets[14].values[2] = 0.56f;
    presets[14].values[3] = 0.38f;
    presets[14].values[4] = 0.24f;
    presets[14].values[8] = 0.38f;
    presets[14].values[9] = 0.52f;
    presets[14].values[10] = 0.14f;
    presets[14].values[23] = 0.36f;
    presets[14].values[24] = 0.92f;

    presets[15] = {"Overdrive 808", "Utility", "Forward soft-drive tone that remains safer than the hard-clip bank.",
                   kRumbleDefaultValues};
    presets[15].values[0] = 0.58f;
    presets[15].values[1] = 0.58f;
    presets[15].values[2] = 0.68f;
    presets[15].values[3] = 0.39f;
    presets[15].values[4] = 0.30f;
    presets[15].values[9] = 0.66f;
    presets[15].values[10] = 0.16f;
    presets[15].values[19] = 0.0f;
    presets[15].values[23] = 0.62f;
    presets[15].values[24] = 0.64f;

    return presets;
}();

} // namespace Plugins
} // namespace Aestra
