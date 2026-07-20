// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Deterministic transport-restart coverage for metronome transient state.

#include "Core/AudioEngine.h"
#include "Playback/MetronomeEngine.h"

#include <algorithm>
#include <cmath>
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
constexpr float kTolerance = 1.0e-6f;

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
    engine->setMetronomeEnabled(true);
    engine->setMetronomeVolume(0.7f);
    engine->setBPM(bpm);
    require(engine->initialize(), "AudioEngine initialization failed");
    return engine;
}

void queueTransport(AudioEngine& engine, bool playing, uint64_t samplePos) {
    AudioQueueCommand command{};
    command.type = AudioQueueCommandType::SetTransportState;
    command.value1 = playing ? 1.0f : 0.0f;
    command.samplePos = samplePos;
    require(engine.commandQueue().push(command), "Transport command queue rejected a command");
}

std::vector<float> renderBlock(AudioEngine& engine) {
    std::vector<float> output(static_cast<size_t>(kFrames) * kChannels, 0.0f);
    engine.processBlock(output.data(), nullptr, kFrames, 0.0);
    return output;
}

std::vector<float> renderBlocks(AudioEngine& engine, uint32_t blockCount) {
    std::vector<float> output;
    output.reserve(static_cast<size_t>(blockCount) * kFrames * kChannels);
    for (uint32_t block = 0; block < blockCount; ++block) {
        auto rendered = renderBlock(engine);
        output.insert(output.end(), rendered.begin(), rendered.end());
    }
    return output;
}

float maxDifference(const std::vector<float>& lhs, const std::vector<float>& rhs) {
    if (lhs.size() != rhs.size()) {
        return INFINITY;
    }

    float difference = 0.0f;
    for (size_t i = 0; i < lhs.size(); ++i) {
        difference = std::max(difference, std::abs(lhs[i] - rhs[i]));
    }
    return difference;
}

float peakMagnitude(const std::vector<float>& samples) {
    float peak = 0.0f;
    for (float sample : samples) {
        peak = std::max(peak, std::abs(sample));
    }
    return peak;
}

std::vector<float> renderFreshStart(uint64_t samplePos, float bpm = 120.0f, uint32_t blocks = 1) {
    auto engine = makeEngine(bpm);
    queueTransport(*engine, true, samplePos);
    return renderBlocks(*engine, blocks);
}

void testRestartAtZeroBeforeFirstCallback() {
    const auto reference = renderFreshStart(0);

    auto engine = makeEngine();
    queueTransport(*engine, false, 0);
    queueTransport(*engine, true, 0);
    const auto restarted = renderBlock(*engine);

    require(maxDifference(reference, restarted) <= kTolerance,
            "restart at zero before the first callback differs from a fresh beat-zero render");
}

void testRestartAtZeroAfterFirstCallback() {
    const auto reference = renderFreshStart(0);

    auto engine = makeEngine();
    queueTransport(*engine, true, 0);
    (void)renderBlock(*engine); // Advance the active click by one callback.

    queueTransport(*engine, false, 0);
    (void)renderBlock(*engine);
    queueTransport(*engine, true, 0);
    const auto restarted = renderBlock(*engine);

    require(maxDifference(reference, restarted) <= kTolerance,
            "restart at zero resumed retained click state instead of starting a fresh click");
}

void testMidBeatRestartClearsActiveTail() {
    constexpr uint64_t kRestartPosition = 1024;
    const auto reference = renderFreshStart(kRestartPosition);

    auto engine = makeEngine();
    queueTransport(*engine, true, 0);
    (void)renderBlock(*engine); // The 100 ms click still has an active tail.

    queueTransport(*engine, false, kFrames);
    (void)renderBlock(*engine);
    queueTransport(*engine, true, kRestartPosition);
    const auto restarted = renderBlock(*engine);

    require(peakMagnitude(reference) <= kTolerance, "fresh mid-beat reference unexpectedly contains a click");
    require(maxDifference(reference, restarted) <= kTolerance,
            "mid-beat restart resumed the click tail from the previous playback run");
}

void testMidBeatRestartRebasesCompletedClickSchedule() {
    constexpr uint64_t kRestartPosition = 12000;
    constexpr uint32_t kComparisonBlocks = 150; // Crosses the 48,000-sample beat at 60 BPM.
    const auto reference = renderFreshStart(kRestartPosition, 60.0f, kComparisonBlocks);

    auto engine = makeEngine(120.0f);
    queueTransport(*engine, true, 0);
    (void)renderBlocks(*engine, 20); // 5,120 samples: the 100 ms click has completed.

    const uint64_t stoppedPosition = engine->getGlobalSamplePos();
    queueTransport(*engine, false, stoppedPosition);
    (void)renderBlock(*engine);

    engine->setBPM(60.0f);
    queueTransport(*engine, true, kRestartPosition);
    const auto restarted = renderBlocks(*engine, kComparisonBlocks);

    require(maxDifference(reference, restarted) <= kTolerance,
            "restart retained next-beat scheduling derived from the previous playback run");
}

void testUninterruptedPlaybackPreservesClickContinuation() {
    auto engine = makeEngine();
    queueTransport(*engine, true, 0);
    const auto uninterrupted = renderBlocks(*engine, 2);

    MetronomeEngine referenceMetronome;
    referenceMetronome.setEnabled(true);
    referenceMetronome.setVolume(0.7f);
    referenceMetronome.setBPM(120.0f);
    referenceMetronome.setSampleRate(kSampleRate);
    std::vector<float> reference(uninterrupted.size(), 0.0f);
    referenceMetronome.process(reference.data(), kFrames, kChannels, 0, kSampleRate, true);
    referenceMetronome.process(reference.data() + static_cast<size_t>(kFrames) * kChannels, kFrames, kChannels, kFrames,
                               kSampleRate, true);

    const std::vector<float> secondBlock(uninterrupted.begin() + static_cast<size_t>(kFrames) * kChannels,
                                         uninterrupted.end());
    require(peakMagnitude(secondBlock) > 0.1f, "uninterrupted playback did not continue the active click tail");
    require(maxDifference(reference, uninterrupted) <= kTolerance,
            "ordinary uninterrupted metronome output changed from the direct reference path");
}

void testExplicitBackwardSeekRebasesMetronome() {
    const auto reference = renderFreshStart(0);

    auto engine = makeEngine();
    queueTransport(*engine, true, 0);
    (void)renderBlocks(*engine, 2);

    queueTransport(*engine, true, 0); // Playing + position change is the production seek path.
    const auto sought = renderBlock(*engine);
    require(maxDifference(reference, sought) <= kTolerance,
            "explicit backward seek to zero did not produce a fresh beat-zero click");
}

} // namespace

int main() {
    testRestartAtZeroBeforeFirstCallback();
    testRestartAtZeroAfterFirstCallback();
    testMidBeatRestartClearsActiveTail();
    testMidBeatRestartRebasesCompletedClickSchedule();
    testUninterruptedPlaybackPreservesClickContinuation();
    testExplicitBackwardSeekRebasesMetronome();

    if (g_failures != 0) {
        std::cerr << g_failures << " metronome restart regression(s) failed\n";
        return 1;
    }

    std::cout << "All metronome restart regressions passed\n";
    return 0;
}
