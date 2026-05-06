#include "AudioEngine.h"
#include "Models/TrackManager.h"

#include <cassert>
#include <iostream>
#include <memory>

using namespace Aestra::Audio;

int main() {
    constexpr uint32_t kSampleRate = 48000;
    constexpr uint32_t kFrames = 256;
    constexpr uint32_t kChannels = 2;

    auto trackManager = std::make_shared<TrackManager>();
    auto* channel = trackManager->addChannel("Prepare Snapshot Track");
    assert(channel != nullptr);

    // Regression guard: default chain snapshot is null until prepare() runs.
    assert(channel->getEffectChain().getSnapshot() == nullptr);

    AudioEngine engine;
    engine.setTrackManager(trackManager);
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kFrames, kChannels);

    auto snapshot = channel->getEffectChain().getSnapshot();
    assert(snapshot != nullptr);
    assert(snapshot->getActiveSlotCount() == 0);

    std::cout << "PASS: AudioEngine setBufferConfig prepares effect chains and publishes snapshots\n";
    return 0;
}
