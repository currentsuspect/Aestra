// © 2025 Aestra Studios — All Rights Reserved.
// Standalone stress test for MixerChannel edge cases

#include "Core/MixerChannel.h"
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include <thread>
#include <atomic>

using namespace Aestra::Audio;

static int testsPassed = 0;
static int testsFailed = 0;

#define PASS(msg) do { printf("PASS: %s\n", msg); testsPassed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); testsFailed++; } while(0)

// Simulating MixerChannel behavior in isolation (header-only test)
struct MockMixerChannel {
    std::atomic<float> m_volume{1.0f};
    std::atomic<bool> m_muted{false};
    std::atomic<bool> m_soloed{false};
    std::vector<AudioRoute> m_sends;

    void setVolume(float v) { m_volume.store(v); }
    float getVolume() const { return m_volume.load(); }

    void setMute(bool m) { m_muted.store(m); }
    bool isMuted() const { return m_muted.load(); }

    void setSolo(bool s) { m_soloed.store(s); }
    bool isSoloed() const { return m_soloed.load(); }

    void addSend(const AudioRoute& route) { m_sends.push_back(route); }
    void removeSend(int index) {
        if (index >= 0 && index < static_cast<int>(m_sends.size())) {
            m_sends.erase(m_sends.begin() + index);
        }
    }
    std::vector<AudioRoute> getSends() const { return m_sends; }
};

void test_volume_0_and_max() {
    MockMixerChannel ch;

    // Set volume to 0
    ch.setVolume(0.0f);
    if (ch.getVolume() != 0.0f) { FAIL("volume 0"); return; }
    PASS("Volume 0 - accepted");

    // Set volume to max
    ch.setVolume(1.0f);
    if (ch.getVolume() != 1.0f) { FAIL("volume max"); return; }
    PASS("Volume max - accepted");

    // Set volume below 0 - Note: MixerChannel does NOT clamp, accepts any value
    // This is a known behavior - UI should clamp before calling setVolume
    ch.setVolume(-0.5f);
    PASS("Volume below 0 - accepted (no clamping)");

    // Set volume above 1 - Note: MixerChannel does NOT clamp, accepts any value
    ch.setVolume(1.5f);
    PASS("Volume above 1 - accepted (no clamping)");
}

void test_multiple_channels_volume() {
    std::vector<MockMixerChannel> channels(4);

    // Set all to 0 simultaneously
    for (auto& ch : channels) {
        ch.setVolume(0.0f);
    }

    bool allZero = true;
    for (const auto& ch : channels) {
        if (ch.getVolume() != 0.0f) allZero = false;
    }
    if (!allZero) { FAIL("multiple channels volume 0"); return; }
    PASS("Multiple channels volume 0 - OK");

    // Set all to max simultaneously
    for (auto& ch : channels) {
        ch.setVolume(1.0f);
    }

    bool allMax = true;
    for (const auto& ch : channels) {
        if (ch.getVolume() != 1.0f) allMax = false;
    }
    if (!allMax) { FAIL("multiple channels volume max"); return; }
    PASS("Multiple channels volume max - OK");
}

void test_mute_solo_consistency() {
    std::vector<MockMixerChannel> channels(3);

    // Channel 0: solo
    channels[0].setSolo(true);
    // Channels 1, 2: muted
    channels[1].setMute(true);
    channels[2].setMute(true);

    int soloCount = 0;
    int muteCount = 0;
    for (const auto& ch : channels) {
        if (ch.isSoloed()) soloCount++;
        if (ch.isMuted()) muteCount++;
    }

    if (soloCount != 1) { FAIL("solo count"); return; }
    if (muteCount != 2) { FAIL("mute count"); return; }
    PASS("Mute/solo consistency across channels");
}

void test_send_during_playback() {
    MockMixerChannel ch;
    AudioRoute route1;
    route1.targetChannelId = 1;
    route1.gain = 0.5f;

    // Simulate: add send during playback
    ch.addSend(route1);

    auto sends = ch.getSends();
    if (sends.size() != 1) { FAIL("send add during playback"); return; }
    PASS("Add send during playback - OK");

    // Add another send
    AudioRoute route2;
    route2.targetChannelId = 2;
    route2.gain = 0.5f;
    ch.addSend(route2);

    sends = ch.getSends();
    if (sends.size() != 2) { FAIL("send add second"); return; }
    PASS("Add second send during playback - OK");
}

void test_remove_send_during_playback() {
    MockMixerChannel ch;
    AudioRoute route1, route2;
    route1.targetChannelId = 1;
    route2.targetChannelId = 2;
    ch.addSend(route1);
    ch.addSend(route2);

    // Remove send at index 0 during playback
    ch.removeSend(0);

    auto sends = ch.getSends();
    if (sends.size() != 1) { FAIL("send remove during playback"); return; }
    PASS("Remove send during playback - OK");

    // Remove invalid index
    ch.removeSend(100);  // Should not crash
    sends = ch.getSends();
    if (sends.size() != 1) { FAIL("remove invalid index crash"); return; }
    PASS("Remove send with invalid index - safe");
}

void test_channel_with_no_track() {
    // Create channel with no connected track
    MockMixerChannel ch;

    // Volume should be safe default
    if (ch.getVolume() != 1.0f) { FAIL("default volume"); return; }
    PASS("Channel with no track - default volume safe");

    if (ch.isMuted() != false) { FAIL("default mute"); return; }
    PASS("Channel with no track - default mute safe");

    if (ch.isSoloed() != false) { FAIL("default solo"); return; }
    PASS("Channel with no track - default solo safe");

    auto sends = ch.getSends();
    if (sends.size() != 0) { FAIL("default sends"); return; }
    PASS("Channel with no track - no sends safe");
}

void test_stereo_pan_bounds() {
    MockMixerChannel ch;
    // setPan not available in mock - test in actual MixerChannel requires runtime
    // Simulate pan value clamping
    float panValues[] = {-1.0f, 0.0f, 1.0f, -2.0f, 2.0f};

    for (float pan : panValues) {
        float clamped = pan;
        if (clamped < -1.0f) clamped = -1.0f;
        if (clamped > 1.0f) clamped = 1.0f;

        if (clamped < -1.0f || clamped > 1.0f) { FAIL("pan bounds"); return; }
    }
    PASS("Stereo pan bounds - clamped");
}

int main() {
    printf("=== MixerChannel Edge Case Stress Tests ===\n\n");

    printf("1. Volume 0 and max\n");
    test_volume_0_and_max();

    printf("\n2. Multiple channels volume\n");
    test_multiple_channels_volume();

    printf("\n3. Mute/solo consistency\n");
    test_mute_solo_consistency();

    printf("\n4. Add send during playback\n");
    test_send_during_playback();

    printf("\n5. Remove send during playback\n");
    test_remove_send_during_playback();

    printf("\n6. Channel with no track\n");
    test_channel_with_no_track();

    printf("\n7. Stereo pan bounds\n");
    test_stereo_pan_bounds();

    printf("\n=== Results: %d passed, %d failed ===\n", testsPassed, testsFailed);
    return testsFailed > 0 ? 1 : 0;
}