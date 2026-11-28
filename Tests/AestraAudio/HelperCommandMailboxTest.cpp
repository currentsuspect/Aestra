// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// HelperCommandMailboxTest — the hand-over between the helper's pipe thread and its main thread keeps the app's order.
//
// The app sends one command at a time and waits for each answer. The pipe thread must therefore not read the next command
// until the main thread has finished the last one, and the main thread must see commands in the order they arrived.

#include "Plugin/HelperCommandMailbox.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace AestraHelper;
using namespace std::chrono_literals;

namespace {
int g_failures = 0;

void check(bool condition, const std::string& label) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << label << std::endl;
    if (!condition) {
        ++g_failures;
    }
}

// The main thread's side: take whatever is posted, waiting up to a second.
bool takeWithin(CommandMailbox& box, std::string& out, std::chrono::milliseconds limit = 1000ms) {
    const auto end = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < end) {
        if (box.take(out)) return true;
        std::this_thread::sleep_for(1ms);
    }
    return false;
}
} // namespace

int main() {
    {
        CommandMailbox box;
        std::atomic<bool> returned{false};
        std::thread poster([&] {
            box.postAndWait("LOAD a");
            returned = true;
        });
        std::string got;
        check(takeWithin(box, got) && got == "LOAD a", "the main thread gets the posted command");
        std::this_thread::sleep_for(50ms);
        check(!returned.load(), "the pipe thread is held while the command runs");
        box.finish();
        poster.join();
        check(returned.load(), "and released when it is finished");
    }

    {
        // Order: three commands, each posted only after the one before has finished.
        CommandMailbox box;
        std::thread poster([&] {
            for (const char* line : {"ONE", "TWO", "THREE"}) {
                if (!box.postAndWait(line)) break;
            }
            box.close();
        });
        std::vector<std::string> seen;
        std::string got;
        while (seen.size() < 3 && takeWithin(box, got)) {
            seen.push_back(got);
            box.finish();
        }
        poster.join();
        check(seen == std::vector<std::string>({"ONE", "TWO", "THREE"}), "commands arrive in the order they were posted");
        check(box.closed(), "after the last one, a closed mailbox reads as closed");
    }

    {
        // Nothing posted: take says so, and does not block.
        CommandMailbox box;
        std::string got;
        check(!box.take(got), "an empty mailbox has nothing to take");
        check(!box.closed(), "and is not closed");
    }

    {
        // Closing with a command still waiting: the main thread still gets it before it sees the end.
        CommandMailbox box;
        std::thread poster([&] { box.postAndWait("LAST"); });
        std::this_thread::sleep_for(50ms);
        box.close();
        std::string got;
        check(!box.closed(), "a posted command is not lost to the close");
        check(box.take(got) && got == "LAST", "and is still handed over");
        box.finish();
        poster.join();
        check(box.closed(), "then the mailbox reads as closed");
        check(!box.postAndWait("TOO LATE"), "posting after the close is refused");
    }

    {
        // The wake descriptor (POSIX) becomes readable when something is posted, so a poll on it wakes the main thread.
#ifndef _WIN32
        CommandMailbox box;
        std::thread poster([&] { box.postAndWait("WAKE"); });
        bool woke = false;
        const auto end = std::chrono::steady_clock::now() + 1000ms;
        while (std::chrono::steady_clock::now() < end && !woke) {
            std::string got;
            if (box.take(got)) {
                woke = true;
                box.finish();
            } else {
                std::this_thread::sleep_for(1ms);
            }
        }
        poster.join();
        check(woke && box.wakeFd() >= 0, "a descriptor is provided for the main thread's poll");
        box.drainWake();
#endif
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "helper command mailbox passed\n";
    return 0;
}
