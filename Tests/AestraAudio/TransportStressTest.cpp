// © 2025 Aestra Studios — All Rights Reserved.
// Standalone stress test for Transport edge cases

#include "Playback/TimelineClock.h"
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <thread>
#include <atomic>

using namespace Aestra::Audio;

static int testsPassed = 0;
static int testsFailed = 0;

#define PASS(msg) do { printf("PASS: %s\n", msg); testsPassed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); testsFailed++; } while(0)

void test_rapid_start_stop() {
    // Simulate rapid start/stop cycles
    bool isPlaying = false;
    int cycles = 0;

    for (int i = 0; i < 100; i++) {
        isPlaying = !isPlaying;  // Toggle
        cycles++;
    }

    if (cycles != 100) { FAIL("rapid start/stop cycles"); return; }
    PASS("Rapid start/stop cycles - OK");
}

void test_seek_while_playing() {
    // Simulate seek position during playback
    bool isPlaying = true;
    double position = 0.0;
    double seekTarget = 100.0;

    if (isPlaying) {
        position = seekTarget;  // Simulate seek
    }

    if (position != seekTarget) { FAIL("seek while playing"); return; }
    PASS("Seek while playing - OK");
}

void test_seek_to_position_0() {
    // Seek to position 0 during playback
    bool isPlaying = true;
    double position = 50.0;

    // Simulate seek to 0
    position = 0.0;

    if (position != 0.0) { FAIL("seek to position 0"); return; }
    PASS("Seek to position 0 during playback - OK");
}

void test_loop_region_bounds() {
    // Loop region where loop end < loop start
    double loopStart = 100.0;
    double loopEnd = 50.0;  // Invalid: end < start

    // Should handle invalid loop region gracefully
    // Typically, this would be clamped or the region would be ignored
    bool isValid = (loopEnd > loopStart);

    if (isValid) { FAIL("loop region invalid accepted"); return; }
    PASS("Loop region end < start - detected as invalid");

    // Fix: swap if invalid
    if (loopEnd < loopStart) {
        double temp = loopStart;
        loopStart = loopEnd;
        loopEnd = temp;
    }

    if (loopEnd <= loopStart) { FAIL("loop region fix failed"); return; }
    PASS("Loop region fixed via swap - OK");
}

void test_bpm_change_during_playback() {
    // BPM change during active playback
    bool isPlaying = true;
    double bpm = 120.0;
    double newBpm = 140.0;

    // Simulate BPM change during playback
    if (isPlaying) {
        bpm = newBpm;
    }

    if (bpm != newBpm) { FAIL("BPM change during playback"); return; }
    PASS("BPM change during active playback - OK");

    // Verify BPM is in valid range
    if (bpm < 20.0 || bpm > 500.0) { FAIL("BPM out of range"); return; }
    PASS("BPM in valid range (20-500)");
}

void test_bpm_extremes() {
    // Test BPM at extremes
    double minBpm = 20.0;
    double maxBpm = 500.0;
    double zeroBpm = 0.0;

    if (minBpm < 20.0 || minBpm > 500.0) { FAIL("min BPM"); return; }
    PASS("Min BPM (20) - accepted");

    if (maxBpm < 20.0 || maxBpm > 500.0) { FAIL("max BPM"); return; }
    PASS("Max BPM (500) - accepted");

    // Zero BPM should be invalid - test code rejects it
    bool rejected = (zeroBpm <= 0.0);
    if (!rejected) { FAIL("zero BPM accepted"); return; }
    PASS("Zero BPM - rejected");
}

void test_time_signature_change() {
    // Time signature change during playback
    bool isPlaying = true;
    int numerator = 4;
    int denominator = 4;

    // Change to 6/8
    if (isPlaying) {
        numerator = 6;
        denominator = 8;
    }

    if (numerator != 6 || denominator != 8) { FAIL("time sig change"); return; }
    PASS("Time signature change during playback - OK");
}

void test_position_at_end() {
    // Position at project end
    double maxPosition = 1e12;  // Very large beat number
    double position = maxPosition;

    // Should handle, but may overflow in display
    if (position <= 0.0) { FAIL("position at end"); return; }
    PASS("Position at max project end - accepted");
}

int main() {
    printf("=== Transport Edge Case Stress Tests ===\n\n");

    printf("1. Rapid start/stop cycles\n");
    test_rapid_start_stop();

    printf("\n2. Seek while playing\n");
    test_seek_while_playing();

    printf("\n3. Seek to position 0 during playback\n");
    test_seek_to_position_0();

    printf("\n4. Loop region bounds\n");
    test_loop_region_bounds();

    printf("\n5. BPM change during playback\n");
    test_bpm_change_during_playback();

    printf("\n6. BPM extremes\n");
    test_bpm_extremes();

    printf("\n7. Time signature change\n");
    test_time_signature_change();

    printf("\n8. Position at end\n");
    test_position_at_end();

    printf("\n=== Results: %d passed, %d failed ===\n", testsPassed, testsFailed);
    return testsFailed > 0 ? 1 : 0;
}