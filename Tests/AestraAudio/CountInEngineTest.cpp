// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// CountInEngineTest — engine-side count-in lifecycle through the command queue.
//
// The count-in / recording sweep: AestraContent pushes MetronomeCountInStart via
// the audio command queue, the engine renders the configured beats headless, and
// the count-in clears when done — that clearing is what the app polls to finish
// the count-in and start playback/recording. This test pins the engine half.

#include "Core/AudioEngine.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kFrames = 256;
constexpr uint32_t kChannels = 2;

int g_failures = 0;

bool require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
    return condition;
}

std::unique_ptr<AudioEngine> makeEngine(float bpm = 120.0f) {
    auto engine = std::make_unique<AudioEngine>();
    engine->setSampleRate(kSampleRate);
    engine->setBufferConfig(kFrames, kChannels);
    engine->setSafetyLimiterEnabled(false);
    engine->setAuditionModeEnabled(false);
    engine->setMetronomeEnabled(false); // Count-in must work even when off.
    engine->setBPM(bpm);
    require(engine->initialize(), "AudioEngine initialization failed");
    return engine;
}

std::vector<float> renderBlock(AudioEngine& engine) {
    std::vector<float> output(static_cast<size_t>(kFrames) * kChannels, 0.0f);
    engine.processBlock(output.data(), nullptr, kFrames, 0.0);
    return output;
}

void testCountInActivatesAndCompletes() {
    std::cout << "  [1/3] Count-in activates and completes via command queue... ";
    auto engine = makeEngine(); // 120 bpm → 24000 samples/beat.

    AudioQueueCommand start{};
    start.type = AudioQueueCommandType::MetronomeCountInStart;
    start.value1 = 4.0f; // 4 beats
    require(engine->commandQueue().push(start), "count-in command queued");

    // The command is applied on the RT drain (a block or two later), then the
    // count-in stays active for the configured beats.
    renderBlock(*engine);
    renderBlock(*engine);
    require(engine->isMetronomeCountInActive(), "count-in activates from the queued command");

    // 4 beats × 24000 = 96000 samples = 375 blocks. Render until it clears.
    constexpr uint32_t kMaxBlocks = 420; // margin past 375
    for (uint32_t i = 0; i < kMaxBlocks && engine->isMetronomeCountInActive(); ++i) {
        renderBlock(*engine);
    }
    require(!engine->isMetronomeCountInActive(), "count-in clears after the configured beats");

    std::cout << "PASSED\n";
}

void testCountInStopClearsImmediately() {
    std::cout << "  [2/3] Count-in can be stopped (cancel path)... ";
    auto engine = makeEngine();
    AudioQueueCommand start{};
    start.type = AudioQueueCommandType::MetronomeCountInStart;
    start.value1 = 8.0f;
    require(engine->commandQueue().push(start), "count-in command queued");
    renderBlock(*engine);
    renderBlock(*engine); // the RT drain applies the start
    require(engine->isMetronomeCountInActive(), "count-in active");

    AudioQueueCommand stop{};
    stop.type = AudioQueueCommandType::MetronomeCountInStop;
    require(engine->commandQueue().push(stop), "count-in stop queued");
    renderBlock(*engine); // pump so the RT drains the stop
    renderBlock(*engine); // stop latches immediately once drained
    require(!engine->isMetronomeCountInActive(), "count-in cleared by the stop command");

    std::cout << "PASSED\n";
}

void testZeroBeatRequestClampsToOneBeat() {
    std::cout << "  [3/3] Zero-beat request degrades to one beat, then clears... ";
    auto engine = makeEngine();
    AudioQueueCommand start{};
    start.type = AudioQueueCommandType::MetronomeCountInStart;
    start.value1 = 0.0f; // clamped to 1
    require(engine->commandQueue().push(start), "zero-beat command queued");
    renderBlock(*engine);
    renderBlock(*engine); // the RT drain applies the clamped request
    require(engine->isMetronomeCountInActive(), "count-in active after clamped request");

    constexpr uint32_t kOneBeatBlocks = 24000 / kFrames + 4; // 94 + margin
    for (uint32_t i = 0; i < kOneBeatBlocks && engine->isMetronomeCountInActive(); ++i) {
        renderBlock(*engine);
    }
    require(!engine->isMetronomeCountInActive(), "one-beat count-in completed");

    std::cout << "PASSED\n";
}

} // namespace

int main() {
    std::cout << "=== Count-In Engine Tests ===\n";
    std::cout << "(Command-queue count-in lifecycle, no audio hardware)\n\n";

    testCountInActivatesAndCompletes();
    testCountInStopClearsImmediately();
    testZeroBeatRequestClampsToOneBeat();

    std::cout << "\n";
    if (g_failures > 0) {
        std::cout << "FAILED: " << g_failures << " assertion(s).\n";
        return 1;
    }
    std::cout << "All count-in engine tests passed.\n";
    return 0;
}
