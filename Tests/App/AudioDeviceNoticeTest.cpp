// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// #650 (FD-64): the words the user sees when the audio interface drops out and returns, and that each is said once.
#include "App/AudioDeviceNotice.h"

#include <iostream>

namespace {
int g_failures = 0;
void check(bool cond, const std::string& label) {
    std::cout << (cond ? "PASS: " : "FAIL: ") << label << "\n";
    if (!cond) ++g_failures;
}
} // namespace

int main() {
    using Aestra::AudioDeviceNoticeState;
    {
        AudioDeviceNoticeState s;
        const auto lost = s.onLost();
        check(lost.show, "a lost interface is announced");
        check(lost.text == "Your audio interface disconnected. Aestra keeps running without sound until it's back.",
              "lost wording is plain and says what happens next");
        check(lost.seconds >= 8.0, "the lost message stays up long enough to read");
        check(!s.onLost().show, "a loss is announced once, not on every report");
        const auto back = s.onRestored("Scarlett 2i2");
        check(back.show && back.text == "Your audio interface (Scarlett 2i2) is back.", "restore names the device");
        check(!s.substituted(), "state is clear after the restore");
    }
    {
        AudioDeviceNoticeState s;
        check(!s.onRestored("X").show, "a restore with nothing announced stays quiet");
        s.onLost();
        check(s.onRestored("").text == "Your audio interface is back.", "an unnamed device is still reported");
        check(s.onLost().show, "a second loss after a restore is announced again");
    }
    std::cout << (g_failures == 0 ? "ALL PASSED\n" : "FAILURES: " + std::to_string(g_failures) + "\n");
    return g_failures == 0 ? 0 : 1;
}
