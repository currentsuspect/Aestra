// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// A loop shorter than the audio block wraps more than once inside that block. The engine
// used to split the block once — the rest of it ran on past the loop end — so a 1/16-note
// loop at a 2048-frame buffer played material from outside the loop. Rendering the same
// session with large blocks must match rendering it with blocks smaller than the loop.

#include "GoldenAudio/GoldenAudioHarness.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace Aestra::Audio;
using namespace GoldenAudio;

namespace {

constexpr double kTau = 6.28318530717958647692;
constexpr uint32_t kTotalFrames = 48000;
// 120 BPM at 48 kHz: one beat is 24000 frames. A 1/32-note loop is 750 frames.
constexpr double kLoopStartBeat = 1.0;
constexpr double kLoopEndBeat = 1.0 + 1.0 / 32.0;

int g_failures = 0;
void check(bool cond, const char* what) {
    std::printf("  %s %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        ++g_failures;
    }
}

std::vector<float> render(uint32_t driverBlock) {
    SessionConfig cfg;
    cfg.blockSize = 256;
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(cfg.sampleRate));
    // A rising ramp makes every frame position distinct: any frame from outside the
    // loop, or a pass that starts at the wrong place, changes the output.
    std::vector<float> ramp(static_cast<size_t>(kTotalFrames) * 2, 0.0f);
    for (uint32_t i = 0; i < kTotalFrames; ++i) {
        const float v = 0.5f * static_cast<float>(i) / static_cast<float>(kTotalFrames) +
                        0.1f * static_cast<float>(std::sin(kTau * 97.0 * i / cfg.sampleRate));
        ramp[static_cast<size_t>(i) * 2] = v;
        ramp[static_cast<size_t>(i) * 2 + 1] = v;
    }
    addAudioTrack(*tm, "ramp", ramp, kTotalFrames, cfg);

    AudioEngine engine;
    prepareEngine(engine, tm, cfg);
    engine.setLoopRegion(kLoopStartBeat, kLoopEndBeat);
    engine.setLoopEnabled(true);
    engine.setGlobalSamplePos(static_cast<uint64_t>(kLoopStartBeat * 24000.0));
    engine.setTransportPlaying(true);
    SessionConfig drive = cfg;
    drive.blockSize = driverBlock;
    return renderBlocks(engine, 8 * 2048, drive);
}

} // namespace

int main() {
    std::printf("ShortLoopMultiWrapTest\n");
    const auto small = render(128); // smaller than the 750-frame loop: at most one wrap per block
    const auto large = render(2048); // nearly three wraps per block

    double maxDiff = 0.0;
    double peak = 0.0;
    for (size_t i = 0; i < small.size(); ++i) {
        maxDiff = std::max(maxDiff, static_cast<double>(std::fabs(small[i] - large[i])));
        peak = std::max(peak, static_cast<double>(std::fabs(small[i])));
    }
    check(peak > 0.05, "the looped clip is audible");
    check(maxDiff < 1e-6, "a block that wraps several times matches blocks that wrap once");
    std::printf("  peak=%.4f maxDiff=%.3g\n", peak, maxDiff);
    return g_failures == 0 ? 0 : 1;
}
