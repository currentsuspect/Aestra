// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// The Audition hub's "Bounce the timeline now" tile: a click starts a background job and returns at once, the tile
// follows the job's progress, says what happened after, and never promises a new version when a click would replace
// the open version's bounce (owner, 2026-11-08).
#include "../../Source/Panels/AuditionBounceTile.h"

#include <iostream>

using Aestra::AuditionBounceTile;
using Aestra::BounceJob;
using Aestra::BounceOutcome;

namespace {
int g_failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::cout << "[FAIL] " << what << '\n';
        ++g_failures;
    }
}
constexpr double kFrame = 1.0 / 60.0;
} // namespace

int main() {
    // Words.
    check(AuditionBounceTile::label("") == "Bounce the timeline now", "an unbounced open version: bounce now");
    check(AuditionBounceTile::sub("", 0) == "Adds v1 to this project", "and it adds the next version");
    check(AuditionBounceTile::label("v1") == "Bounce again", "a bounced open version: bounce again");
    check(AuditionBounceTile::sub("v1", 1) == "Replaces v1 with the timeline as it is now", "and it says it replaces, not adds v2");

    // A press starts a job and returns; the tile runs until the job is finished.
    AuditionBounceTile tile;
    int starts = 0;
    std::shared_ptr<BounceJob> running;
    const auto start = [&]() { ++starts; running = std::make_shared<BounceJob>(); return running; };
    const auto word = []() { return std::string("Bounced v1. Press play on it to listen."); };
    tile.press(start);
    check(starts == 1 && tile.busy() && tile.message == "Bouncing the timeline...", "a press starts one job and shows Bouncing...");
    tile.press(start);
    check(starts == 1, "a second press while it runs starts nothing");
    running->progress = 0.4f;
    check(!tile.step(kFrame, word) && tile.busy() && tile.progress() > 0.39f, "while the job runs the tile follows its progress");
    running->finished = true;
    running->outcome = {true, ""};
    check(tile.step(kFrame, word), "a finished job is picked up on the next frame");
    check(tile.state == AuditionBounceTile::State::Done && tile.message == "Bounced v1. Press play on it to listen.", "and says what it made");
    check(!tile.step(kFrame, word), "once");
    tile.step(AuditionBounceTile::kMessageSeconds, word);
    check(tile.state == AuditionBounceTile::State::Idle && tile.message.empty(), "the line goes after a while");

    // A job that could not start says why on the next frame.
    tile.press([]() { return BounceJob::failed("Save the project first."); });
    tile.step(kFrame, word);
    check(tile.state == AuditionBounceTile::State::Failed && tile.message == "Save the project first.", "a failed bounce says why");
    tile.step(AuditionBounceTile::kMessageSeconds, word);
    tile.press([]() { return std::shared_ptr<BounceJob>(); });
    tile.step(kFrame, word);
    check(tile.state == AuditionBounceTile::State::Failed && !tile.message.empty(), "even no job at all says something");

    if (g_failures == 0) std::cout << "[PASS] the bounce tile starts a job, follows it, then says what happened\n";
    return g_failures == 0 ? 0 : 1;
}
