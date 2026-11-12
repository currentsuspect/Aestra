// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// The moves of finding a sound (SPEC-025 R5): pick, six more like it, back to the same six, holds, distance,
// shaping that is carried forward and can be put back, and a sentence for every move.

#include "RumbleFindSession.h"
#include "RumblePresetBank.h"

#include <cmath>
#include <iostream>
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

bool same(const RumbleSound& a, const RumbleSound& b) {
    return a.v == b.v && a.seed == b.seed;
}

RumbleFindSession fresh(uint64_t seed = 7) {
    return RumbleFindSession(rumbleSoundOf(kRumbleFactoryPresets[0]), "Balanced 808", seed);
}

void startsWithSixAndTheFirstPicked() {
    auto s = fresh();
    check(s.stepCount() == 1 && s.currentStep() == 0, "one step to start");
    // Opening Rumble must not change the sound on the track: the start itself is picked, not one of the six.
    check(s.selected() == RumbleFindSession::kParent, "the starting sound stays picked");
    check(same(s.current(), s.step(0).parent), "and it is what plays");
    check(same(s.step(0).parent, rumbleSoundOf(kRumbleFactoryPresets[0])), "the six come from the starting sound");
    check(s.nameOf(RumbleFindSession::kParent) == "Balanced 808", "the start keeps its own name");
    check(s.soundsMade() == 6, "six sounds made");
    check(!s.said().empty(), "the screen says something from the start");
}

void moreMakesSixFromThePickedSound() {
    auto s = fresh();
    s.select(3);
    const RumbleSound picked = s.current();
    const std::string name = s.currentName();
    s.more();
    check(s.stepCount() == 2 && s.currentStep() == 1, "a second step");
    check(same(s.step(1).parent, picked), "made from the picked sound");
    check(s.nameOf(RumbleFindSession::kParent) == name, "which keeps its name as the parent");
    check(s.selected() == 0, "the first of the new six is picked");
    check(s.said().find(name) != std::string::npos, "the sentence names what they came from: " + s.said());
    check(s.soundsMade() == 12, "twelve made");
}

void backShowsTheSameSix() {
    auto s = fresh();
    s.select(2);
    const auto first = s.step(0).six;
    s.more();
    check(s.back(), "back works from the second step");
    check(s.currentStep() == 0, "at the first step");
    bool sameSix = true;
    for (size_t i = 0; i < RumbleFindSession::kSix; ++i) sameSix = sameSix && same(s.step(0).six[i], first[i]);
    check(sameSix, "the same six as before, not new ones");
    check(s.selected() == 2, "with the same one picked");
    check(!s.back(), "there is nothing before the start");
    check(s.goToStep(1) && s.currentStep() == 1, "the trail still has the later step");
    s.goToStep(0);
    s.select(4);
    s.more();
    check(s.stepCount() == 2 && same(s.step(1).parent, s.step(0).six[4]), "asking again from an earlier step replaces what came after");
}

void theSameSeedGivesTheSameRounds() {
    auto a = fresh(99), b = fresh(99), c = fresh(100);
    a.select(1); b.select(1); c.select(1);
    a.more(); b.more(); c.more();
    bool ab = true, ac = true;
    for (size_t i = 0; i < RumbleFindSession::kSix; ++i) {
        ab = ab && same(a.step(1).six[i], b.step(1).six[i]);
        ac = ac && same(a.step(1).six[i], c.step(1).six[i]);
    }
    check(ab, "the same session seed makes the same rounds");
    check(!ac, "another seed makes other rounds");
}

void heldQualitiesAreKeptExactly() {
    auto s = fresh();
    s.toggleHold(Quality::Tail);
    check(s.held(Quality::Tail) && !s.held(Quality::Dirt), "Tail is held");
    const double tail = s.current()[RumbleSound::Tail];
    s.more();
    bool kept = true;
    for (const auto& sound : s.step(1).six) kept = kept && sound[RumbleSound::Tail] == tail;
    check(kept, "all six keep the held tail exactly");
    s.toggleHold(Quality::Tail);
    check(!s.held(Quality::Tail), "and it lets go");
}

double meanDistance(Distance distance) {
    auto s = fresh();
    s.setDistance(distance);
    double total = 0.0;
    int n = 0;
    for (int round = 0; round < 6; ++round) {
        s.more();
        const auto& st = s.step(s.currentStep());
        for (const auto& sound : st.six) {
            for (uint32_t q : {RumbleSound::Punch, RumbleSound::Tail, RumbleSound::Weight, RumbleSound::Tone, RumbleSound::Dirt}) {
                total += std::fabs(sound[q] - st.parent[q]);
                ++n;
            }
        }
    }
    return total / n;
}

void distanceSetsHowFar() {
    const double close = meanDistance(Distance::Close), far = meanDistance(Distance::Far);
    check(close < far, "Close strays less than Far (" + std::to_string(close) + " vs " + std::to_string(far) + ")");
    auto s = fresh();
    s.setDistance(Distance::Far);
    check(s.distance() == Distance::Far, "the distance is kept");
}

void arrowsWrapThroughTheParent() {
    auto s = fresh();
    s.select(5);
    s.moveSelection(1);
    check(s.selected() == RumbleFindSession::kParent, "right from the last goes to the parent");
    check(same(s.current(), s.step(0).parent), "which then plays");
    s.moveSelection(1);
    check(s.selected() == 0, "and on to the first");
    s.moveSelection(-1);
    s.moveSelection(-1);
    check(s.selected() == 5, "left from the parent goes to the last");
}

void shapingIsCarriedAndCanBePutBack() {
    auto s = fresh();
    s.select(1);
    const double arrived = s.current()[RumbleSound::Tail];
    s.shape(RumbleSound::Tail, 0.9);
    check(s.current()[RumbleSound::Tail] == 0.9 && s.shaped(1), "the shaped setting holds and the sound is marked");
    check(!s.shaped(0), "the others are not");
    check(!s.said().empty() && s.said().find("Tail") != std::string::npos, "the sentence says what changed: " + s.said());
    s.more();
    check(s.step(1).parent[RumbleSound::Tail] == 0.9, "shaping is carried into the next six");
    s.back();
    check(s.current()[RumbleSound::Tail] == 0.9 && s.shaped(1), "and is still there going back");
    s.putBack(RumbleSound::Tail);
    check(s.current()[RumbleSound::Tail] == arrived && !s.shaped(1), "put back: as it arrived, no longer marked");
    s.shape(RumbleSound::Punch, 2.0);
    check(s.current()[RumbleSound::Punch] == 1.0, "settings stay in range");
}

void everyMoveSaysSomething() {
    auto s = fresh();
    std::string last = s.said();
    s.select(2);
    check(s.said() != last, "picking says something new");
    last = s.said();
    s.more();
    check(s.said() != last, "more says something new");
    last = s.said();
    s.back();
    check(s.said() != last, "back says something new");
    s.back();
    check(!s.said().empty(), "and at the start it says so");
}
} // namespace

int main() {
    startsWithSixAndTheFirstPicked();
    moreMakesSixFromThePickedSound();
    backShowsTheSameSix();
    theSameSeedGivesTheSameRounds();
    heldQualitiesAreKeptExactly();
    distanceSetsHowFar();
    arrowsWrapThroughTheParent();
    shapingIsCarriedAndCanBePutBack();
    everyMoveSaysSomething();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "RumbleFindSessionTest passed\n";
    return 0;
}
