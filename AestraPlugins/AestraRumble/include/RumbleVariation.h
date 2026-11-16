// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Makes a round of six Rumble sounds from one (SPEC-025, slice R1), the way the approved sketch did.
//
// Each of the six is pushed a different way so the round is six different answers; the sixth is a surprise.
// The same request always gives the same six, which is what lets the editor show a step again exactly as it
// was heard. Not for the audio thread: this runs when the producer asks for more sounds.
#pragma once

#include "RumbleVoice.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Aestra {
namespace Plugins {
namespace RumbleVariation {

// What the producer hears and can hold. Each is one of Rumble's settings.
enum class Quality : uint8_t { Punch = 0, Tail, Weight, Tone, Dirt };
inline constexpr size_t kQualityCount = 5;

// How far the six stray from the sound they are made from.
enum class Distance : uint8_t { Close = 0, Further, Far };

inline constexpr size_t kRoundSize = 6;

inline constexpr uint8_t holdBit(Quality quality) {
    return static_cast<uint8_t>(1u << static_cast<uint8_t>(quality));
}

// The setting a quality is: Punch, Tail, Weight, Tone, Dirt.
uint32_t settingOf(Quality quality);

struct Request {
    RumbleSound parent{};
    Distance distance = Distance::Further;
    uint8_t held = 0; // holdBit() of every quality the six must keep exactly
    uint64_t seed = 0;
};

std::array<RumbleSound, kRoundSize> makeRound(const Request& request);

// Two plain words from the two qualities furthest from the middle: "Long dirty", "Short knock".
std::string nameFor(const RumbleSound& sound);

} // namespace RumbleVariation
} // namespace Plugins
} // namespace Aestra
