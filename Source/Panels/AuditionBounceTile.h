// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The "Bounce the timeline now" tile on the Audition hub, as a small state machine the hub drives and a test can.
//
// Owner, 2026-11-08: "the audition's bounce timeline either doesn't work or is just terrible at UX feedback". It did
// work, but a click froze the window while the mix rendered, then a tile appeared with nothing said, a failure (an
// unsaved project, a failed save) did nothing at all, and the tile promised "Adds v2" when a second click re-bounced
// the open version over its own bounce. Now the tile says "Bouncing..." on screen BEFORE the render starts (the render
// runs on the UI thread), says what happened when it is done, and says "Bounce again" when it would replace a bounce.

#include <functional>
#include <string>

namespace Aestra {

/// What a bounce request came back with: ok, or why not, in words for the user.
struct BounceOutcome {
    bool ok = false;
    std::string message; ///< why it failed; empty on success (the hub words the success itself)
};

struct AuditionBounceTile {
    enum class State { Idle, Waiting, Done, Failed };
    State state = State::Idle;
    std::string message;
    double shownFor = 0.0;
    bool painted = false;

    static constexpr double kMessageSeconds = 10.0;

    /// The tile's own words. `replaces` is the badge of the open version's bounce ("v1"), empty when it has none.
    static std::string label(const std::string& replaces) { return replaces.empty() ? "Bounce the timeline now" : "Bounce again"; }
    static std::string sub(const std::string& replaces, size_t bouncedVersions) {
        if (!replaces.empty()) return "Replaces " + replaces + " with the timeline as it is now";
        return "Adds v" + std::to_string(bouncedVersions + 1) + " to this project";
    }

    bool busy() const { return state == State::Waiting; }

    void press() {
        if (busy()) return;
        state = State::Waiting;
        painted = false;
        message = "Bouncing the timeline...";
        shownFor = 0.0;
    }

    /// The hub painted the tile this frame: a waiting bounce has now been seen as "Bouncing...".
    void notePainted() {
        if (state == State::Waiting) painted = true;
    }

    /// Once a frame. Runs a waiting bounce only after "Bouncing..." has been painted; returns true when it ran one.
    /// `wordSuccess` turns a finished bounce into the line shown under the shelf (it knows the new badge).
    bool step(double dt, const std::function<BounceOutcome()>& bounce, const std::function<std::string()>& wordSuccess) {
        if (state == State::Waiting) {
            if (!painted) return false;
            BounceOutcome outcome = bounce ? bounce() : BounceOutcome{false, "Bouncing isn't available here."};
            state = outcome.ok ? State::Done : State::Failed;
            message = outcome.ok ? (wordSuccess ? wordSuccess() : std::string("Bounced.")) : outcome.message;
            if (message.empty()) message = outcome.ok ? "Bounced." : "The bounce didn't work. Nothing was changed.";
            shownFor = 0.0;
            return true;
        }
        if (state == State::Done || state == State::Failed) {
            shownFor += dt;
            if (shownFor >= kMessageSeconds) {
                state = State::Idle;
                message.clear();
            }
        }
        return false;
    }
};

} // namespace Aestra
