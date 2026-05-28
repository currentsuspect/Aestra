// © 2025 Aestra Studios — All Rights Reserved.
// Transport stress test - validates TimelineClock edge cases

#include "../../AestraAudio/include/Playback/TimelineClock.h"

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

void test_tempo_setting() {
    TimelineClock clock(120.0);

    if (std::abs(clock.getCurrentTempo() - 120.0) > 0.01) { FAIL("initial tempo"); return; }
    PASS("Initial tempo - OK");

    clock.setTempo(140.0);
    if (std::abs(clock.getCurrentTempo() - 140.0) > 0.01) { FAIL("tempo change"); return; }
    PASS("Tempo change - OK");
}

void test_beat_to_seconds_conversion() {
    TimelineClock clock(120.0);

    // At 120 BPM, 1 beat = 0.5 seconds
    double seconds = clock.secondsAtBeat(1.0);
    if (std::abs(seconds - 0.5) > 0.01) { FAIL("beat to seconds"); return; }
    PASS("Beat to seconds conversion - OK");

    // 2 beats = 1 second
    seconds = clock.secondsAtBeat(2.0);
    if (std::abs(seconds - 1.0) > 0.01) { FAIL("2 beats to seconds"); return; }
    PASS("2 beats to seconds - OK");
}

void test_beat_to_sample_conversion() {
    TimelineClock clock(120.0);
    int sampleRate = 48000;

    // At 120 BPM, 1 beat = 0.5 seconds = 24000 samples
    uint64_t samples = clock.sampleFrameAtBeat(1.0, sampleRate);
    if (samples != 24000) { FAIL("beat to samples"); return; }
    PASS("Beat to sample conversion - OK");
}

void test_sample_to_beat_conversion() {
    TimelineClock clock(120.0);
    int sampleRate = 48000;

    // 24000 samples at 48000 Hz = 0.5 seconds = 1 beat at 120 BPM
    double beat = clock.beatAtSampleFrame(24000, sampleRate);
    if (std::abs(beat - 1.0) > 0.01) { FAIL("sample to beat"); return; }
    PASS("Sample to beat conversion - OK");
}

void test_tempo_map() {
    TimelineClock clock(120.0);

    std::vector<TempoChange> map = {
        {0.0, 120.0},
        {4.0, 140.0},
        {8.0, 100.0}
    };
    clock.setTempoMap(map);

    // Tempo at beat 0 should be 120
    double tempo0 = clock.getTempoAtBeat(0.0);
    if (std::abs(tempo0 - 120.0) > 0.01) { FAIL("tempo map beat 0"); return; }
    PASS("Tempo map beat 0 - OK");

    // Tempo at beat 5 should be 140
    double tempo5 = clock.getTempoAtBeat(5.0);
    if (std::abs(tempo5 - 140.0) > 0.01) { FAIL("tempo map beat 5"); return; }
    PASS("Tempo map beat 5 - OK");

    // Tempo at beat 10 should be 100
    double tempo10 = clock.getTempoAtBeat(10.0);
    if (std::abs(tempo10 - 100.0) > 0.01) { FAIL("tempo map beat 10"); return; }
    PASS("Tempo map beat 10 - OK");
}

int main() {
    printf("=========================================\n");
    printf("  Transport Stress Tests\n");
    printf("=========================================\n");

    test_tempo_setting();
    test_beat_to_seconds_conversion();
    test_beat_to_sample_conversion();
    test_sample_to_beat_conversion();
    test_tempo_map();

    printf("\n=========================================\n");
    printf("  Test Summary\n");
    printf("=========================================\n");
    printf("  Passed: %d\n", testsPassed);
    printf("  Failed: %d\n", testsFailed);
    printf("  Total:  %d\n", testsPassed + testsFailed);
    printf("=========================================\n");

    return testsFailed > 0 ? 1 : 0;
}
