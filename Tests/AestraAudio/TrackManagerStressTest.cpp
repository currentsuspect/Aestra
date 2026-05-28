// © 2025 Aestra Studios — All Rights Reserved.
// TrackManager stress test - validates channel creation/removal edge cases

#include "../../AestraAudio/include/Models/TrackManager.h"

#include <cstdio>
#include <cstdint>
#include <string>
#include <thread>
#include <atomic>

using namespace Aestra::Audio;

static int testsPassed = 0;
static int testsFailed = 0;

#define PASS(msg) do { printf("PASS: %s\n", msg); testsPassed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); testsFailed++; } while(0)

void test_add_remove_channels() {
    TrackManager tm;

    // Get initial count (should have master channel)
    size_t initialCount = tm.getChannelCount();

    // Add a channel
    auto* channel = tm.addChannel("Test Channel");
    if (!channel) { FAIL("add channel returned null"); return; }
    PASS("Add channel - OK");

    // Channel count should increase
    size_t newCount = tm.getChannelCount();
    if (newCount != initialCount + 1) { FAIL("channel count not increased"); return; }
    PASS("Channel count increased after add - OK");

    // Remove last channel
    bool removed = tm.removeLastChannel();
    if (!removed) { FAIL("removeLastChannel failed"); return; }
    PASS("Remove last channel - OK");
}

void test_rapid_add_remove() {
    TrackManager tm;

    for (int i = 0; i < 50; i++) {
        auto* channel = tm.addChannel("Channel " + std::to_string(i));
        if (!channel) { FAIL("rapid add returned null"); return; }
        tm.removeLastChannel();
    }
    PASS("Rapid add/remove cycles - OK");
}

void test_channel_name_preservation() {
    TrackManager tm;

    auto* channel = tm.addChannel("My Channel");
    if (!channel) { FAIL("channel not found"); return; }

    if (channel->getName() != "My Channel") { FAIL("channel name mismatch"); return; }
    PASS("Channel name preserved - OK");
}

void test_get_channel_by_index() {
    TrackManager tm;

    auto* channel = tm.addChannel("Test");
    if (!channel) { FAIL("add channel failed"); return; }

    // Get by index (should be the last one before master)
    size_t count = tm.getChannelCount();
    auto* retrieved = tm.getChannel(count - 1);
    if (!retrieved) { FAIL("getChannel by index failed"); return; }
    PASS("Get channel by index - OK");

    tm.removeLastChannel();
}

void test_channel_count_consistency() {
    TrackManager tm;

    size_t initialCount = tm.getChannelCount();

    tm.addChannel("Channel 1");
    tm.addChannel("Channel 2");

    if (tm.getChannelCount() != initialCount + 2) { FAIL("channel count mismatch"); return; }
    PASS("Channel count consistent - OK");

    tm.removeLastChannel();
    tm.removeLastChannel();
}

int main() {
    printf("=========================================\n");
    printf("  TrackManager Stress Tests\n");
    printf("=========================================\n");

    test_add_remove_channels();
    test_rapid_add_remove();
    test_channel_name_preservation();
    test_get_channel_by_index();
    test_channel_count_consistency();

    printf("\n=========================================\n");
    printf("  Test Summary\n");
    printf("=========================================\n");
    printf("  Passed: %d\n", testsPassed);
    printf("  Failed: %d\n", testsFailed);
    printf("  Total:  %d\n", testsPassed + testsFailed);
    printf("=========================================\n");

    return testsFailed > 0 ? 1 : 0;
}
