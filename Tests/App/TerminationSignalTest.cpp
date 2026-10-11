// © 2026 Aestra Studios — All Rights Reserved.
//
// SPEC 3 §6.7 (owner ruling A): SIGTERM / SIGINT must not end in the interactive
// "Unsaved Changes" dialog. TerminationSignal records the signal so the main loop can
// autosave and exit; this pins the part a unit test can reach: with the handler
// installed a signal is RECORDED and the process lives on to act on it, and without
// it the same signal kills the process (so a pass is not the default behaviour).
// Each case runs in a forked child so a failure cannot take the test runner down.

#include "TerminationSignal.h"

#include <iostream>

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
int g_failures = 0;

void check(bool ok, const char* what) {
    std::cout << (ok ? "PASS: " : "FAIL: ") << what << "\n";
    if (!ok) {
        ++g_failures;
    }
}

#if !defined(_WIN32)
// Runs body() in a child; returns the raw wait status.
template <typename Body>
int inChild(Body body) {
    const pid_t pid = fork();
    if (pid == 0) {
        _exit(body());
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return status;
}
#endif
} // namespace

int main() {
#if defined(_WIN32)
    check(!Aestra::TerminationSignal::install(), "Windows: install is a documented no-op");
#else
    check(Aestra::TerminationSignal::pending() == 0, "nothing pending before any signal");

    // Control: no handler, and SIGTERM ends the process. Proves the next case is not vacuous.
    const int bare = inChild([] {
        std::raise(SIGTERM);
        return 0;
    });
    check(WIFSIGNALED(bare) && WTERMSIG(bare) == SIGTERM, "control: without the handler SIGTERM kills the process");

    // With the handler: each signal is recorded, and the process is still here to act on it.
    const int handled = inChild([] {
        if (!Aestra::TerminationSignal::install()) {
            return 10;
        }
        std::raise(SIGTERM);
        if (Aestra::TerminationSignal::pending() != SIGTERM) {
            return 11;
        }
        Aestra::TerminationSignal::resetForTest();
        std::raise(SIGINT);
        if (Aestra::TerminationSignal::pending() != SIGINT) {
            return 12;
        }
        return 0;
    });
    check(WIFEXITED(handled), "with the handler, SIGTERM and SIGINT do not kill the process");
    check(WIFEXITED(handled) && WEXITSTATUS(handled) == 0,
          "and each is recorded for the main loop (child exit 10 = install failed, 11 = SIGTERM, 12 = SIGINT)");
#endif

    if (g_failures == 0) {
        std::cout << "Termination signal tests passed\n";
        return 0;
    }
    std::cout << g_failures << " termination signal test(s) failed\n";
    return 1;
}
