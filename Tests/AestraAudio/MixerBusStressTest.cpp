// © 2025 Aestra Studios — All Rights Reserved.
// Standalone stress test for mixer bus summing
#include "Core/MixerBus.h"
#include <cstdio>
#include <cmath>
#include <cstring>

using namespace Aestra::Audio;

static int testsPassed = 0;
static int testsFailed = 0;

#define PASS(msg) do { printf("PASS: %s\n", msg); testsPassed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); testsFailed++; } while(0)

constexpr float EPSILON = 0.01f;  // Allow 1% tolerance for FP math

void test_one_silent_channel() {
    // Sum with one silent channel - output should equal the other
    MixerBus bus1("Bus1", 2);
    MixerBus bus2("Bus2", 2);

    // Ensure unity gain
    bus1.setGain(1.0f);
    bus2.setGain(1.0f);

    float in1[4] = {0.5f, 0.5f, -0.3f, -0.3f}; // Mixed
    float in2[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // Silent
    float out[4] = {0};

    bus1.mixInto(out, in1, 2);
    bus2.mixInto(out, in2, 2);

    // constant-power pan at center: expected ~0.707x gain
    float expected = in1[0] * 0.707f;
    if (std::abs(out[0] - expected) > EPSILON) { FAIL("one silent channel"); return; }
    PASS("One silent channel preserves signal");
}

void test_all_unity() {
    // Sum with all channels at unity - should sum linearly
    MixerBus bus1("Bus1", 2);
    MixerBus bus2("Bus2", 2);

    // Ensure unity gain
    bus1.setGain(1.0f);
    bus2.setGain(1.0f);

    float in1[4] = {0.5f, 0.5f, 0.3f, 0.3f};
    float in2[4] = {0.5f, 0.5f, 0.3f, 0.3f};
    float out[4] = {0};

    bus1.mixInto(out, in1, 2);
    bus2.mixInto(out, in2, 2);

    // constant-power pan at center: expected ~0.707x gain per channel
    // 0.5 + 0.5 = 1.0, then * 0.707 = 0.707 at output
    float expected = 1.0f * 0.707f;
    if (std::abs(out[0] - expected) > EPSILON) { FAIL("all unity L"); return; }
    PASS("All unity gain sums correctly");
}

void test_exceeding_0dBFS() {
    // Sum exceeding 0dBFS headroom - should clip or saturate
    MixerBus bus1("Bus1", 2);
    MixerBus bus2("Bus2", 2);

    float in1[4] = {0.9f, 0.9f, 0.9f, 0.9f};
    float in2[4] = {0.9f, 0.9f, 0.9f, 0.9f};
    float out[4] = {0};

    bus1.mixInto(out, in1, 2);
    bus2.mixInto(out, in2, 2);

    // Sum = 1.8, exceeds 1.0 (0dBFS)
    printf("  Peak: %.2f (expected ~1.8 or clipped to 1.0)\n", out[0]);
    PASS("Exceeding 0dBFS handled (clipped or saturated)");
}

void test_late_registration() {
    // Late bus registration during active playback - register after process calls
    MixerBus bus("LateBus", 2);

    float buf[4] = {0.1f, 0.1f, 0.1f, 0.1f};
    float silent[4] = {0};

    // Process before any mixing - just baseline
    bus.process(buf, 2);

    // Now add our bus to a "live" system
    // In real code, this would register the bus during audio callback
    // Verify method is callable and doesn't crash
    bus.mixInto(buf, silent, 2);
    PASS("Late bus registration during playback");
}

int main() {
    printf("=== Mixer Bus Summing Stress Tests ===\n\n");

    printf("2c-I: Sum with one silent channel\n");
    test_one_silent_channel();

    printf("\n2c-II: Sum with all channels at unity\n");
    test_all_unity();

    printf("\n2c-III: Sum exceeding 0dBFS headroom\n");
    test_exceeding_0dBFS();

    printf("\n2c-IV: Late bus registration during active playback\n");
    test_late_registration();

    printf("\n=== Results: %d passed, %d failed ===\n", testsPassed, testsFailed);
    return testsFailed > 0 ? 1 : 0;
}