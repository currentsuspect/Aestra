// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Contract for a round of six Rumble sounds (SPEC-025, slice R1). Settings only: no engine, no audio.

#include "RumblePresetBank.h"
#include "RumbleVariation.h"
#include "RumbleVoice.h"

#include <cmath>
#include <limits>
#include <cstdint>
#include <iostream>
#include <set>
#include <string>

using namespace Aestra::Plugins;
using RumbleVariation::Distance;
using RumbleVariation::Quality;

namespace {
int failures = 0;
void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "failed: " << message << '\n';
    }
}

RumbleSound middle() {
    RumbleSound sound;
    sound.v.fill(0.5);
    sound[RumbleSound::Slide] = 0.0;
    sound[RumbleSound::ClipType] = 0.1;
    return sound;
}

const Distance kDistances[] = {Distance::Close, Distance::Further, Distance::Far};
const Quality kQualities[] = {Quality::Punch, Quality::Tail, Quality::Weight, Quality::Tone, Quality::Dirt};

bool sameSound(const RumbleSound& a, const RumbleSound& b) {
    return a.v == b.v && a.seed == b.seed;
}

void sameRequestGivesSameSix() {
    const RumbleVariation::Request request{rumbleDefaultSound(), Distance::Further, 0, 42};
    const auto first = RumbleVariation::makeRound(request);
    const auto again = RumbleVariation::makeRound(request);
    bool same = true;
    for (size_t i = 0; i < first.size(); ++i) {
        same = same && sameSound(first[i], again[i]);
    }
    check(same, "the same request gives the same six");
    auto other = request;
    other.seed = 43;
    check(!sameSound(RumbleVariation::makeRound(other)[0], first[0]), "another seed gives other sounds");
}

void everySoundIsUsable() {
    int made = 0;
    for (const auto& preset : kRumbleFactoryPresets) {
        for (Distance distance : kDistances) {
            for (uint64_t seed = 1; seed <= 40; ++seed) {
                const RumbleVariation::Request request{rumbleSoundOf(preset), distance, 0, seed * 7919};
                const auto round = RumbleVariation::makeRound(request);
                std::set<std::array<double, RumbleSound::Count>> different;
                different.insert(request.parent.v);
                std::set<uint32_t> seeds;
                for (const RumbleSound& sound : round) {
                    ++made;
                    different.insert(sound.v);
                    seeds.insert(sound.seed);
                    for (size_t id = 0; id < RumbleSound::Count; ++id) {
                        check(std::isfinite(sound[id]) && sound[id] >= 0.0 && sound[id] <= 1.0, "every setting in range");
                    }
                    check(sound[RumbleSound::Slide] == request.parent[RumbleSound::Slide], "Slide is how it is played: never changed");
                }
                check(different.size() == RumbleVariation::kRoundSize + 1, "the six differ from each other and from their parent");
                check(seeds.size() == RumbleVariation::kRoundSize, "each of the six has its own click grain");
            }
        }
    }
    std::cout << made << " sounds made\n";
}

void heldQualitiesAreKeptExactly() {
    const RumbleSound parent = rumbleSoundOf(kRumbleFactoryPresets[4]);
    for (Quality quality : kQualities) {
        for (Distance distance : kDistances) {
            const auto free = RumbleVariation::makeRound({parent, distance, 0, 9001});
            const auto round = RumbleVariation::makeRound({parent, distance, RumbleVariation::holdBit(quality), 9001});
            for (size_t child = 0; child < round.size(); ++child) {
                for (size_t id = 0; id < RumbleSound::Count; ++id) {
                    if (id == RumbleVariation::settingOf(quality)) {
                        check(round[child][id] == parent[id], "a held quality is kept exactly");
                    } else {
                        check(round[child][id] == free[child][id], "holding one quality leaves everything else as it was");
                    }
                }
            }
        }
    }
}

void eachSoundGoesItsOwnWay() {
    const RumbleSound parent = middle();
    double sum[6][5] = {};
    const int rounds = 300;
    for (int seed = 0; seed < rounds; ++seed) {
        const auto round = RumbleVariation::makeRound({parent, Distance::Further, 0, static_cast<uint64_t>(seed) * 104729 + 1});
        for (size_t child = 0; child < 6; ++child) {
            for (size_t q = 0; q < 5; ++q) {
                sum[child][q] += round[child][RumbleVariation::settingOf(kQualities[q])] - 0.5;
            }
        }
    }
    const double clear = 0.1 * rounds;
    enum { P, T, W, To, D };
    check(sum[0][T] > clear && sum[0][W] > 0, "the first is longer and deeper");
    check(sum[1][T] < -clear && sum[1][P] > clear, "the second is shorter and hits harder");
    check(sum[2][D] > clear && sum[2][To] > clear * 0.5, "the third is dirtier and brighter");
    check(sum[3][D] < -clear && sum[3][To] < -clear * 0.5 && sum[3][W] > 0, "the fourth is cleaner, darker and deeper");
    check(sum[4][P] > clear && sum[4][D] > clear * 0.4, "the fifth is harder and dirtier");
}

void furtherIsFurther() {
    const RumbleSound parent = middle();
    double moved[3] = {};
    for (size_t d = 0; d < 3; ++d) {
        for (uint64_t seed = 1; seed <= 200; ++seed) {
            for (const RumbleSound& sound : RumbleVariation::makeRound({parent, kDistances[d], 0, seed})) {
                for (Quality q : kQualities) {
                    moved[d] += std::fabs(sound[RumbleVariation::settingOf(q)] - 0.5);
                }
            }
        }
    }
    check(moved[0] < moved[1] && moved[1] < moved[2], "Close moves less than Further, Further less than Far");
}

void namesSayWhatIsHeard() {
    RumbleSound sound = middle();
    sound[RumbleSound::Tail] = 0.9;
    sound[RumbleSound::Dirt] = 0.85;
    check(RumbleVariation::nameFor(sound) == "Long dirty", "long and driven reads Long dirty, got " + RumbleVariation::nameFor(sound));
    sound = middle();
    sound[RumbleSound::Tail] = 0.1;
    sound[RumbleSound::Punch] = 0.95;
    check(RumbleVariation::nameFor(sound) == "Short knock", "short with a big drop reads Short knock, got " + RumbleVariation::nameFor(sound));
}
} // namespace

// Review finding on #1365: a damaged parent never makes unplayable children, held qualities included.
void aDamagedParentStillGivesPlayableSounds() {
    RumbleVariation::Request request;
    request.parent = rumbleSoundOf(kRumbleFactoryPresets[0]);
    request.parent[RumbleSound::Punch] = std::nan("");
    request.parent[RumbleSound::Hold] = std::numeric_limits<double>::infinity();
    request.held = RumbleVariation::holdBit(RumbleVariation::Quality::Punch);
    request.seed = 3;
    bool finite = true;
    for (const auto& sound : RumbleVariation::makeRound(request)) {
        for (size_t id = 0; id < RumbleSound::Count; ++id) finite = finite && std::isfinite(sound[id]) && sound[id] >= 0.0 && sound[id] <= 1.0;
    }
    check(finite, "every setting of every child is a number in range");
}

int main() {
    sameRequestGivesSameSix();
    everySoundIsUsable();
    heldQualitiesAreKeptExactly();
    eachSoundGoesItsOwnWay();
    furtherIsFurther();
    namesSayWhatIsHeard();
    aDamagedParentStillGivesPlayableSounds();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "RumbleVariationTest passed\n";
    return 0;
}
