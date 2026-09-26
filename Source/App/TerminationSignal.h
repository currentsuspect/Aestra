// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// SIGTERM / SIGINT: autosave and exit, never a prompt (SPEC 3 §6.7, owner ruling A).
//
// SDL converts these signals into SDL_QUIT, which the platform layer routes to the
// same close callback as the window's close button, so a signal got the interactive
// "Unsaved Changes" dialog and the process waited on it forever: a logout or a
// scripted `kill` never finished. Installing this handler replaces SDL's (SDL only
// installs its own over SIG_DFL, and on quit only removes a handler that is still
// its own), so the close button keeps its prompt and a signal takes this path.
//
// The handler only records which signal arrived. The main loop polls pending() and
// does the actual work (autosave, keep the session recoverable, exit) on the main
// thread, where it is allowed to.
//
// Deliberately dependency-free, like CrashFlagPath.h, so it is unit-testable while
// Source/ has no linkable library target (#666).

#include <atomic>
#include <csignal> // on POSIX this also declares sigaction

namespace Aestra {

class TerminationSignal {
public:
    /// Route SIGTERM and SIGINT to the flag. True if both were installed. A no-op
    /// returning false on Windows, where the OS does not deliver SIGTERM to a GUI app.
    static bool install() {
#if defined(_WIN32)
        return false;
#else
        struct sigaction action {};
        action.sa_handler = &TerminationSignal::handle;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;
        const bool term = sigaction(SIGTERM, &action, nullptr) == 0;
        const bool interrupt = sigaction(SIGINT, &action, nullptr) == 0;
        return term && interrupt;
#endif
    }

    /// The signal that asked the app to exit, or 0 if none has.
    static int pending() { return s_pending.load(std::memory_order_relaxed); }

    /// Tests only: forget a delivered signal.
    static void resetForTest() { s_pending.store(0, std::memory_order_relaxed); }

private:
    // Async-signal-safe: one lock-free store, nothing else.
    static void handle(int signalNumber) { s_pending.store(signalNumber, std::memory_order_relaxed); }

    static_assert(std::atomic<int>::is_always_lock_free, "the signal handler needs a lock-free flag");
    // Constant-initialized, so the handler never runs a guarded static initialization.
    inline static std::atomic<int> s_pending{0};
};

} // namespace Aestra
