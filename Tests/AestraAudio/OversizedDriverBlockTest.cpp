// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// A driver may deliver more frames per callback than the block size the engine
// was configured for (WASAPI shared mode hands over up to its device buffer).
// processBlock() must render such a block in capacity-sized chunks: the same
// audio as a run of ordinary blocks. Before the split, an oversized block made
// renderGraph() refuse the whole block, so clip audio vanished for it and the
// output stage read past the end of the master buffer.

#include "GoldenAudio/GoldenAudioHarness.h"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <vector>

using namespace Aestra::Audio;
using namespace GoldenAudio;

namespace {

constexpr double kTau = 6.28318530717958647692;
// The engine never shrinks its blocks below the 4096-frame default, so that is
// the capacity a small setBufferConfig() request actually gets.
constexpr uint32_t kCapacity = 4096;
constexpr uint32_t kOversized = 2 * kCapacity + 500; // two full chunks and a tail
constexpr uint32_t kTotalFrames = 3 * kOversized;

int g_failures = 0;
void check(bool cond, const char* what) {
    std::printf("  %s %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        ++g_failures;
    }
}

std::shared_ptr<TrackManager> buildSession(const SessionConfig& cfg) {
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(cfg.sampleRate));
    std::vector<float> sine(static_cast<size_t>(kTotalFrames) * 2, 0.0f);
    for (uint32_t i = 0; i < kTotalFrames; ++i) {
        const float v = 0.3f * static_cast<float>(std::sin(kTau * 440.0 * i / cfg.sampleRate));
        sine[static_cast<size_t>(i) * 2] = v;
        sine[static_cast<size_t>(i) * 2 + 1] = v;
    }
    addAudioTrack(*tm, "A", sine, kTotalFrames, cfg);
    return tm;
}

std::vector<float> render(uint32_t driverBlock) {
    SessionConfig cfg;
    cfg.blockSize = 256; // what the engine is configured with
    auto tm = buildSession(cfg);
    AudioEngine engine;
    prepareEngine(engine, tm, cfg);
    engine.setTransportPlaying(true);
    SessionConfig drive = cfg;
    drive.blockSize = driverBlock; // what the driver actually hands over
    auto out = renderBlocks(engine, kTotalFrames, drive);
    if (driverBlock > kCapacity) {
        check(engine.telemetry().getOversizedBlocks() == 3, "every oversized callback was split");
    } else {
        check(engine.telemetry().getOversizedBlocks() == 0, "an in-capacity callback is never split");
    }
    return out;
}

} // namespace

int main() {
    std::cout << "OversizedDriverBlockTest\n";
    const auto reference = render(256);
    const auto oversized = render(kOversized);

    double peak = 0.0;
    double maxDiff = 0.0;
    for (size_t i = 0; i < reference.size(); ++i) {
        peak = std::max(peak, static_cast<double>(std::fabs(reference[i])));
        maxDiff = std::max(maxDiff, static_cast<double>(std::fabs(reference[i] - oversized[i])));
    }
    check(peak > 0.1, "the clip is audible in the reference render");
    check(oversized.size() == reference.size(), "both renders cover the same frames");
    check(maxDiff < 1e-6, "oversized callbacks render the same audio as ordinary blocks");
    std::printf("  peak=%.4f maxDiff=%.3g\n", peak, maxDiff);
    return g_failures == 0 ? 0 : 1;
}
