// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// The Audition hub's "Bounce the timeline now" tile: says "Bouncing..." before the render runs, says what happened
// after, and never promises a new version when a click would replace the open version's bounce (owner, 2026-11-08).
#include "../../Source/Panels/AuditionBounceTile.h"

#include <iostream>

using Aestra::AuditionBounceTile;
using Aestra::BounceOutcome;

namespace {
int g_failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::cout << "[FAIL] " << what << '\n';
        ++g_failures;
    }
}
} // namespace

int main() {
    // Words.
    check(AuditionBounceTile::label("") == "Bounce the timeline now", "an unbounced open version: bounce now");
    check(AuditionBounceTile::sub("", 0) == "Adds v1 to this project", "and it adds the next version");
    check(AuditionBounceTile::label("v1") == "Bounce again", "a bounced open version: bounce again");
    check(AuditionBounceTile::sub("v1", 1) == "Replaces v1 with the timeline as it is now", "and it says it replaces, not adds v2");

    // "Bouncing..." is on screen before the render starts.
    AuditionBounceTile tile;
    int bounces = 0;
    const auto ok = [&]() { ++bounces; return BounceOutcome{true, ""}; };
    const auto word = []() { return std::string("Bounced v1. Press play on it to listen."); };
    tile.press();
    check(tile.busy() && tile.message == "Bouncing the timeline...", "a press shows Bouncing... at once");
    check(!tile.step(0.016, ok, word) && bounces == 0, "nothing renders until Bouncing... has been painted");
    tile.notePainted();
    tile.press();
    check(tile.painted, "a second press while waiting changes nothing");
    check(tile.step(0.016, ok, word) && bounces == 1, "after a paint, the next frame bounces");
    check(tile.state == AuditionBounceTile::State::Done && tile.message == "Bounced v1. Press play on it to listen.", "and says what it made");
    check(!tile.step(0.016, ok, word) && bounces == 1, "once");
    tile.step(AuditionBounceTile::kMessageSeconds, ok, word);
    check(tile.state == AuditionBounceTile::State::Idle && tile.message.empty(), "the line goes after a while");

    // A failure says why.
    tile.press();
    tile.notePainted();
    tile.step(0.016, []() { return BounceOutcome{false, "Save the project first."}; }, word);
    check(tile.state == AuditionBounceTile::State::Failed && tile.message == "Save the project first.", "a failed bounce says why");
    tile.press();
    tile.notePainted();
    tile.step(0.016, []() { return BounceOutcome{false, ""}; }, word);
    check(!tile.message.empty(), "even a failure with no reason says something");

    if (g_failures == 0) std::cout << "[PASS] the bounce tile says Bouncing... first, then what happened\n";
    return g_failures == 0 ? 0 : 1;
}
