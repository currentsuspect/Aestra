// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The "Bounce the timeline now" tile on the Audition hub, as a small state machine the hub drives and a test can.
//
// Owner, 2026-11-08: "the audition's bounce timeline either doesn't work or is just terrible at UX feedback", then
// "let it bounce on a worker thread, not with UI, it freezes ... along with an animation filling up the render". It
// did work, but a click froze the window while the mix rendered, a failure (an unsaved project, a failed save) did
// nothing at all, and the tile promised "Adds v2" when a second click re-bounced the open version over its own bounce.
// Now a click starts a BounceJob on a worker (the app renders a copy of the saved version, so the song can still be
// edited and played meanwhile); the tile fills with the job's progress, says what happened when it is done, and says
// "Bounce again" when it would replace a bounce.

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace Aestra {

/// What a bounce request came back with: ok, or why not, in words for the user.
struct BounceOutcome {
    bool ok = false;
    std::string message; ///< why it failed; empty on success (the hub words the success itself)
};

/// One bounce in flight. The worker writes `progress` (0..1) and reads `cancel`; `finished` and `outcome` are set on
/// the UI thread (the worker's completion hops back through the app's main-thread queue), so the hub reads them plainly.
struct BounceJob {
    std::atomic<float> progress{0.0f};
    std::atomic<bool> cancel{false};
    bool finished = false;
    BounceOutcome outcome;

    static std::shared_ptr<BounceJob> failed(std::string why) {
        auto job = std::make_shared<BounceJob>();
        job->finished = true;
        job->outcome = {false, std::move(why)};
        return job;
    }
};

struct AuditionBounceTile {
    enum class State { Idle, Running, Done, Failed };
    State state = State::Idle;
    std::string message;
    double shownFor = 0.0;
    std::shared_ptr<BounceJob> job;

    static constexpr double kMessageSeconds = 10.0;

    /// The tile's own words. `replaces` is the badge of the open version's bounce ("v1"), empty when it has none.
    static std::string label(const std::string& replaces) { return replaces.empty() ? "Bounce the timeline now" : "Bounce again"; }
    static std::string sub(const std::string& replaces, size_t bouncedVersions) {
        if (!replaces.empty()) return "Replaces " + replaces + " with the timeline as it is now";
        return "Adds v" + std::to_string(bouncedVersions + 1) + " to this project";
    }

    bool busy() const { return state == State::Running; }
    /// 0..1 while running; the render's share of the work (loading the song first shows as 0).
    float progress() const { return job ? job->progress.load(std::memory_order_relaxed) : 0.0f; }

    /// Start a bounce. `start` hands back the job at once (already finished when it could not start); nothing renders
    /// on the calling thread.
    void press(const std::function<std::shared_ptr<BounceJob>()>& start) {
        if (busy()) return;
        job = start ? start() : BounceJob::failed("Bouncing isn't available here.");
        if (!job) job = BounceJob::failed("The bounce didn't start. Nothing was changed.");
        state = State::Running;
        message = "Bouncing the timeline...";
        shownFor = 0.0;
    }

    /// Once a frame. Picks up a finished job; returns true when it did (the hub then re-reads the project).
    /// `wordSuccess` turns a finished bounce into the line shown under the tile (it knows the new badge).
    bool step(double dt, const std::function<std::string()>& wordSuccess) {
        if (state == State::Running) {
            if (!job || !job->finished) return false;
            const BounceOutcome outcome = job->outcome;
            state = outcome.ok ? State::Done : State::Failed;
            message = outcome.ok ? (wordSuccess ? wordSuccess() : std::string("Bounced.")) : outcome.message;
            if (message.empty()) message = outcome.ok ? "Bounced." : "The bounce didn't work. Nothing was changed.";
            shownFor = 0.0;
            job.reset();
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
