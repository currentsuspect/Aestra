// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Spec 1 item 2: the transport clock toggles between time and musical display
// with one tap. Pins both representations (time preserved verbatim, musical in
// the existing bar:beat.centibeats convention) and the toggle round-trip.

#include "../../Source/Components/TransportInfoContainer.h"

#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        ++g_failures;
    }
}

} // namespace

int main() {
    using Aestra::TimerDisplay;

    expect(TimerDisplay::formatTime(0.0) == "0:00.00", "time zero formats");
    expect(TimerDisplay::formatTime(4.33) == "0:04.33", "time seconds format");
    expect(TimerDisplay::formatTime(65.5) == "1:05.50", "time minute rollover");
    expect(TimerDisplay::formatTime(600.0) == "10:00.00", "time double digits");

    expect(TimerDisplay::formatMusical(0.0, 4) == "1:1.00", "musical downbeat");
    expect(TimerDisplay::formatMusical(3.5, 4) == "1:4.50", "musical fractional beat");
    expect(TimerDisplay::formatMusical(4.0, 4) == "2:1.00", "musical bar rollover");
    expect(TimerDisplay::formatMusical(7.25, 3) == "3:2.25", "musical triple meter");
    expect(TimerDisplay::formatMusical(-2.0, 4) == "1:1.00", "musical clamps negative");
    expect(TimerDisplay::formatMusical(5.0, 0) == "6:1.00", "musical guards zero meter");

    TimerDisplay clock;
    expect(clock.getDisplayMode() == TimerDisplay::DisplayMode::Time, "clock defaults to time mode");
    clock.setTime(4.33);
    clock.setMusicalPosition(8.66, 4);
    clock.toggleDisplayMode();
    expect(clock.getDisplayMode() == TimerDisplay::DisplayMode::Musical, "one toggle reaches musical mode");
    clock.toggleDisplayMode();
    expect(clock.getDisplayMode() == TimerDisplay::DisplayMode::Time, "two toggles return to time mode");
    expect(clock.getTime() == 4.33, "toggling never moves the position");

    if (g_failures == 0) {
        std::cout << "All TransportClockFormat tests passed.\n";
        return 0;
    }
    std::cerr << g_failures << " TransportClockFormat test(s) failed.\n";
    return 1;
}
