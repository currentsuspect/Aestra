// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "RumbleVariation.h"

#include <algorithm>
#include <cmath>

namespace Aestra {
namespace Plugins {
namespace RumbleVariation {

namespace {

// The sketch's numbers (SPEC-025 sketch source, child()). Changing them changes what Find offers.
constexpr double kSpread[3] = {0.12, 0.24, 0.40};
constexpr double kWobble = 0.3;
constexpr double kSurpriseReach = 1.6;
constexpr double kDetailReach = 1.3;
// Which way each of the first five is pushed: Punch, Tail, Weight, Tone, Dirt. The sixth goes anywhere.
constexpr double kDirections[5][kQualityCount] = {
    {0.0, 1.0, 0.4, 0.0, 0.0},   // longer and deeper
    {0.8, -1.0, 0.0, 0.0, 0.0},  // shorter, harder at the front
    {0.0, 0.0, 0.0, 0.6, 1.0},   // dirtier and brighter
    {0.0, 0.0, 0.5, -0.7, -1.0}, // cleaner, darker and deeper
    {1.0, -0.3, 0.0, 0.0, 0.5},  // harder and dirtier
};
// The settings that drift with every child: everything but the five qualities and Slide, which is how the
// sound is played, not what it is.
constexpr uint32_t kDetails[] = {RumbleSound::DropTime, RumbleSound::Hold,     RumbleSound::Curve,
                                 RumbleSound::Second,   RumbleSound::Third,    RumbleSound::ClipType,
                                 RumbleSound::Click,    RumbleSound::ClickLength, RumbleSound::ClickTone};

struct Rng {
    uint64_t state;
    uint64_t next() {
        state += 0x9E3779B97F4A7C15ull;
        uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double around() { return static_cast<double>(next() >> 11) * (2.0 / 9007199254740992.0) - 1.0; } // -1 to 1
};

double clamp01(double value) {
    return std::isfinite(value) ? std::min(1.0, std::max(0.0, value)) : 0.0;
}

} // namespace

uint32_t settingOf(Quality quality) {
    switch (quality) {
    case Quality::Punch:
        return RumbleSound::Punch;
    case Quality::Tail:
        return RumbleSound::Tail;
    case Quality::Weight:
        return RumbleSound::Weight;
    case Quality::Tone:
        return RumbleSound::Tone;
    case Quality::Dirt:
        return RumbleSound::Dirt;
    }
    return RumbleSound::Tail;
}

std::array<RumbleSound, kRoundSize> makeRound(const Request& request) {
    std::array<RumbleSound, kRoundSize> round{};
    const double spread = kSpread[std::min<size_t>(2, static_cast<size_t>(request.distance))];
    for (size_t child = 0; child < kRoundSize; ++child) {
        RumbleSound sound = request.parent;
        Rng rng{request.seed ^ (0xD1B54A32D192ED03ull * (child + 1))};
        // Every number is drawn whether or not its quality is held, so holding one quality leaves what
        // happens to the others exactly as it was.
        for (size_t quality = 0; quality < kQualityCount; ++quality) {
            const double push =
                child < 5 ? kDirections[child][quality] * spread : rng.around() * spread * kSurpriseReach;
            const double wobble = rng.around() * spread * kWobble;
            if ((request.held & (1u << quality)) == 0) {
                const uint32_t id = settingOf(static_cast<Quality>(quality));
                sound[id] = clamp01(request.parent[id] + push + wobble);
            }
        }
        for (uint32_t id : kDetails) {
            sound[id] = clamp01(request.parent[id] + rng.around() * spread * kDetailReach);
        }
        sound.seed = static_cast<uint32_t>(rng.next());
        round[child] = sound;
    }
    return round;
}

std::string nameFor(const RumbleSound& sound) {
    static constexpr const char* kWords[kQualityCount][2] = {
        {"soft", "knock"}, {"short", "long"}, {"light", "deep"}, {"dark", "bright"}, {"clean", "dirty"}};
    int order[kQualityCount] = {0, 1, 2, 3, 4};
    auto away = [&](int quality) { return sound[settingOf(static_cast<Quality>(quality))] - 0.5; };
    std::stable_sort(order, order + kQualityCount,
                     [&](int a, int b) { return std::fabs(away(a)) > std::fabs(away(b)); });
    int first = order[0];
    int second = order[1];
    // Length reads best first: "Long dirty", not "Dirty long".
    if (second == static_cast<int>(Quality::Tail)) {
        std::swap(first, second);
    }
    std::string name = kWords[first][away(first) < 0.0 ? 0 : 1];
    name += ' ';
    name += kWords[second][away(second) < 0.0 ? 0 : 1];
    name[0] = static_cast<char>(name[0] - 'a' + 'A');
    return name;
}

} // namespace RumbleVariation
} // namespace Plugins
} // namespace Aestra
