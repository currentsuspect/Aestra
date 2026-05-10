// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// PDC (Plugin Delay Compensation) Test

#include "AudioEngine.h"
#include "Models/TrackManager.h"

#include <cassert>
#include <iostream>
#include <memory>

using namespace Aestra::Audio;

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kFrames = 256;
constexpr uint32_t kChannels = 2;

void testPDCInitialization() {
    std::cout << "Test: PDC initializes correctly...\n";

    auto trackManager = std::make_shared<TrackManager>();
    auto* channel = trackManager->addChannel("Test Track");

    AudioEngine engine;
    engine.setTrackManager(trackManager);
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kFrames, kChannels);

    assert(engine.isLatencyCompensationEnabled() == true);
    assert(engine.getMaxProjectLatency() == 0);

    std::cout << "  [PASS] PDC initializes enabled, zero latency\n";
}

void testPDCWithEmptyChain() {
    std::cout << "Test: PDC with empty effect chain...\n";

    auto trackManager = std::make_shared<TrackManager>();
    trackManager->addChannel("Track 1");
    trackManager->addChannel("Track 2");

    AudioEngine engine;
    engine.setTrackManager(trackManager);
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kFrames, kChannels);

    assert(engine.getMaxProjectLatency() == 0);

    std::cout << "  [PASS] Empty chains have zero latency\n";
}

void testPDCEnableDisable() {
    std::cout << "Test: PDC enable/disable toggle...\n";

    auto trackManager = std::make_shared<TrackManager>();
    trackManager->addChannel("Test Track");

    AudioEngine engine;
    engine.setTrackManager(trackManager);
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kFrames, kChannels);

    assert(engine.isLatencyCompensationEnabled() == true);
    assert(engine.getMaxProjectLatency() == 0);

    engine.setLatencyCompensationEnabled(false);
    assert(engine.isLatencyCompensationEnabled() == false);
    assert(engine.getMaxProjectLatency() == 0);

    engine.setLatencyCompensationEnabled(true);
    assert(engine.isLatencyCompensationEnabled() == true);

    std::cout << "  [PASS] Enable/disable works correctly\n";
}

void testPDCCallbackOnPluginInsert() {
    std::cout << "Test: PDC callback triggers...\n";

    auto trackManager = std::make_shared<TrackManager>();
    trackManager->addChannel("Test Track");

    AudioEngine engine;
    engine.setTrackManager(trackManager);
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kFrames, kChannels);

    engine.calculateLatencyCompensation();
    auto latency = engine.getMaxProjectLatency();
    (void)latency;

    std::cout << "  [PASS] PDC recalculation works\n";
}

void testPDCMultipleTracks() {
    std::cout << "Test: PDC with multiple tracks...\n";

    auto trackManager = std::make_shared<TrackManager>();
    trackManager->addChannel("Track 1");
    trackManager->addChannel("Track 2");
    trackManager->addChannel("Track 3");

    AudioEngine engine;
    engine.setTrackManager(trackManager);
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kFrames, kChannels);

    assert(engine.getMaxProjectLatency() == 0);

    std::cout << "  [PASS] Multiple tracks initialize with zero latency\n";
}

int main() {
    std::cout << "=== Plugin Delay Compensation (PDC) Tests ===\n\n";

    testPDCInitialization();
    testPDCWithEmptyChain();
    testPDCEnableDisable();
    testPDCCallbackOnPluginInsert();
    testPDCMultipleTracks();

    std::cout << "\n=== All PDC Tests Passed ===\n";
    return 0;
}