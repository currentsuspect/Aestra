// © 2026 Aestra Studios — All Rights Reserved.
//
// SPEC 3 §4: after input, the adaptive-FPS governor must fall idle once the idle timeout
// passes. It used to latch: the idle timer only advanced while "user active" was false, and
// that flag only cleared once the timer passed the timeout, so after the first mouse move
// getIdleTime() stayed 0 forever and the render gate presented every frame of an untouched
// app. A short timeout keeps this test to a few tens of milliseconds.

#include "Core/NUIAdaptiveFPS.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace {
int g_failures = 0;
void check(bool ok, const char* what) {
    std::cout << (ok ? "PASS: " : "FAIL: ") << what << "\n";
    if (!ok) ++g_failures;
}
void frame(AestraUI::NUIAdaptiveFPS& fps) {
    const auto start = fps.beginFrame();
    fps.endFrame(start, 1.0 / 60.0);
}
} // namespace

int main() {
    using namespace std::chrono_literals;
    AestraUI::NUIAdaptiveFPS::Config config;
    config.idleTimeout = 0.05; // 50 ms
    AestraUI::NUIAdaptiveFPS fps(config);

    fps.signalActivity(AestraUI::NUIAdaptiveFPS::ActivityType::MouseMove);
    frame(fps);
    check(fps.getIdleTime() < 0.05, "right after input the governor is active (idle time below the timeout)");

    // Frames keep running with no further input, as they do at idle.
    for (int i = 0; i < 6; ++i) {
        std::this_thread::sleep_for(15ms);
        frame(fps);
    }
    check(fps.getIdleTime() >= 0.05, "with no input for longer than the timeout, the governor goes idle");

    fps.signalActivity(AestraUI::NUIAdaptiveFPS::ActivityType::KeyPress);
    frame(fps);
    check(fps.getIdleTime() < 0.05, "new input makes it active again");

    // Continuous visuals (playback meters) hold it active without input.
    fps.setAudioVisualizationActive(true);
    for (int i = 0; i < 6; ++i) {
        std::this_thread::sleep_for(15ms);
        frame(fps);
    }
    check(fps.getIdleTime() == 0.0, "while audio visualization runs it never goes idle");
    fps.setAudioVisualizationActive(false);
    for (int i = 0; i < 6; ++i) {
        std::this_thread::sleep_for(15ms);
        frame(fps);
    }
    check(fps.getIdleTime() >= 0.05, "and once it stops, it goes idle again");

    if (g_failures == 0) {
        std::cout << "Adaptive FPS idle tests passed\n";
        return 0;
    }
    std::cout << g_failures << " adaptive FPS idle test(s) failed\n";
    return 1;
}
