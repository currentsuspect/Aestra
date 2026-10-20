// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V82-H3: the bookkeeping behind a plugin editor's run loop (file descriptors and timers a VST3/CLAP editor asks the
// host for), separated from X11 so the rules can be pinned: when a timer is due, what happens after a stall, a handler
// that cancels itself or a neighbour, descriptors shared by several handlers.

#include "Plugin/EditorLoopPump.h"

#include <iostream>
#include <string>
#include <unistd.h>

using Aestra::Audio::EditorLoopPump;
using namespace std::chrono_literals;

namespace {
int g_failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cout << "[FAIL] " << what << "\n";
        ++g_failures;
    }
}
} // namespace

int main() {
    const auto t0 = EditorLoopPump::Clock::time_point{} + 1000s; // a fixed clock: no sleeping in this test

    // No timers: the poll has no deadline.
    {
        EditorLoopPump pump;
        check(pump.empty() && pump.msUntilNextTimer(t0) == -1, "an empty pump has no deadline");
    }

    // A timer is due one interval after it is added, not before, and fires once per due time.
    {
        EditorLoopPump pump;
        int ticks = 0;
        pump.addTimer(30ms, [&] { ++ticks; }, t0);
        check(pump.msUntilNextTimer(t0) >= 30 && pump.msUntilNextTimer(t0) <= 31, "30 ms from now: the poll waits 30 ms (rounded up)");
        check(pump.fireDueTimers(t0 + 29ms) == 0 && ticks == 0, "not due yet at 29 ms");
        check(pump.fireDueTimers(t0 + 30ms) == 1 && ticks == 1, "due at 30 ms: fires");
        check(pump.fireDueTimers(t0 + 31ms) == 0 && ticks == 1, "and not again straight away");
        check(pump.fireDueTimers(t0 + 60ms) == 1 && ticks == 2, "once per interval");
    }

    // A long stall fires a timer once, not once for every missed interval.
    {
        EditorLoopPump pump;
        int ticks = 0;
        pump.addTimer(10ms, [&] { ++ticks; }, t0);
        check(pump.fireDueTimers(t0 + 5000ms) == 1 && ticks == 1, "after a 5 s stall a 10 ms timer fires once");
        check(pump.msUntilNextTimer(t0 + 5000ms) >= 10, "and is next due an interval after the stall");
    }

    // The soonest of several timers sets the deadline.
    {
        EditorLoopPump pump;
        pump.addTimer(100ms, [] {}, t0);
        pump.addTimer(20ms, [] {}, t0);
        pump.addTimer(50ms, [] {}, t0);
        const int ms = pump.msUntilNextTimer(t0);
        check(ms >= 20 && ms <= 21, "the 20 ms timer decides how long to wait");
        check(pump.msUntilNextTimer(t0 + 500ms) == 0, "already overdue: do not wait");
    }

    // A zero interval is not a busy loop: it is clamped to 1 ms.
    {
        EditorLoopPump pump;
        int ticks = 0;
        pump.addTimer(0ms, [&] { ++ticks; }, t0);
        check(pump.fireDueTimers(t0) == 0, "a 0 ms timer is not due the instant it is added");
        check(pump.fireDueTimers(t0 + 1ms) == 1, "it runs at 1 ms");
    }

    // A timer removed does not fire; one that removes itself in its handler fires exactly once.
    {
        EditorLoopPump pump;
        int a = 0, b = 0;
        const int tokenA = pump.addTimer(10ms, [&] { ++a; }, t0);
        int tokenB = 0;
        tokenB = pump.addTimer(10ms, [&] { ++b; pump.removeTimer(tokenB); }, t0);
        pump.removeTimer(tokenA);
        pump.fireDueTimers(t0 + 10ms);
        pump.fireDueTimers(t0 + 20ms);
        check(a == 0, "a removed timer never fires");
        check(b == 1, "a timer that removes itself fires exactly once");
    }

    // A handler that removes a neighbour due in the same pass stops it from firing.
    {
        EditorLoopPump pump;
        int second = 0;
        int firstToken = 0, secondToken = 0;
        firstToken = pump.addTimer(10ms, [&] { pump.removeTimer(secondToken); }, t0);
        secondToken = pump.addTimer(10ms, [&] { ++second; }, t0);
        (void)firstToken;
        pump.fireDueTimers(t0 + 10ms);
        check(second == 0, "a timer removed by an earlier handler in the same pass does not fire");
    }

    // A handler may add a timer (a plugin starting an animation) without deadlocking.
    {
        EditorLoopPump pump;
        int inner = 0;
        pump.addTimer(10ms, [&] { pump.addTimer(5ms, [&] { ++inner; }, t0 + 10ms); }, t0);
        pump.fireDueTimers(t0 + 10ms);
        pump.fireDueTimers(t0 + 15ms);
        check(inner == 1, "a timer added from a handler works");
    }

    // File descriptors: each is polled once even when shared; dispatch calls every handler on it.
    {
        EditorLoopPump pump;
        int pipefd[2];
        check(pipe(pipefd) == 0, "pipe");
        int one = 0, two = 0;
        const int t1 = pump.addFd(pipefd[0], [&] { ++one; });
        pump.addFd(pipefd[0], [&] { ++two; });
        pump.addFd(42, [] {});
        const auto polled = pump.fds();
        check(polled.size() == 2, "a descriptor shared by two handlers is polled once");
        check(pump.dispatchFd(pipefd[0]) == 2 && one == 1 && two == 1, "dispatch runs every handler on that descriptor");
        pump.removeFd(t1);
        check(pump.dispatchFd(pipefd[0]) == 1 && one == 1 && two == 2, "a removed handler is not called");
        check(pump.dispatchFd(7) == 0, "a descriptor nobody registered runs nothing");
        close(pipefd[0]);
        close(pipefd[1]);
    }

    if (g_failures == 0) {
        std::cout << "EditorLoopPumpTest: all passed\n";
        return 0;
    }
    std::cout << "EditorLoopPumpTest: " << g_failures << " failure(s)\n";
    return 1;
}
