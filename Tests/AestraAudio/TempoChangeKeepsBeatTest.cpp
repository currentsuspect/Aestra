// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Changing the tempo must keep the playhead on its beat. Positions are held in samples, so a
// tempo edit that only swapped the BPM left the playhead where it was in time: bar 9 at
// 120 BPM became bar ~10.3 at 140. setTempoKeepingBeat() rescales the playhead — in the same
// audio block as the switch while playing, directly while stopped.

#include "Core/AudioEngine.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kRate = 48000;
constexpr uint32_t kBlock = 256;

double beatAt(uint64_t sample, double bpm) { return static_cast<double>(sample) * bpm / (60.0 * kRate); }

void configure(AudioEngine& engine) {
    engine.setSampleRate(kRate);
    assert(engine.setBufferConfig(kBlock, 2));
    engine.setBPM(120.0f);
}

void testWhilePlaying() {
    AudioEngine engine;
    configure(engine);
    std::vector<float> out(kBlock * 2, 0.0f);
    engine.setGlobalSamplePos(static_cast<uint64_t>(32.0 * 60.0 / 120.0 * kRate)); // beat 32
    engine.setTransportPlaying(true);
    engine.processBlock(out.data(), nullptr, kBlock, 0.0);
    const double beatBefore = beatAt(engine.getGlobalSamplePos(), 120.0);

    const double factor = engine.setTempoKeepingBeat(140.0f);
    assert(std::fabs(factor - 120.0 / 140.0) < 1e-12);
    engine.processBlock(out.data(), nullptr, kBlock, 0.0);
    // One block later, at the new tempo.
    const double expected = beatBefore + beatAt(kBlock, 140.0);
    const double beatAfter = beatAt(engine.getGlobalSamplePos(), 140.0);
    assert(engine.getBPM() == 140.0f);
    assert(std::fabs(beatAfter - expected) < 1e-3 && "the playhead stays on its beat across the switch");
    std::printf("[PASS] playing: beat %.4f -> %.4f at 140 BPM (expected %.4f)\n", beatBefore, beatAfter, expected);
}

void testWhileStopped() {
    AudioEngine engine;
    configure(engine);
    engine.setGlobalSamplePos(static_cast<uint64_t>(16.0 * 60.0 / 120.0 * kRate)); // beat 16
    engine.setTempoKeepingBeat(90.0f);
    assert(engine.getBPM() == 90.0f);
    assert(std::fabs(beatAt(engine.getGlobalSamplePos(), 90.0) - 16.0) < 1e-4);
    std::printf("[PASS] stopped: the cued playhead stays on beat 16\n");
}

void testInvalidTempoIsIgnored() {
    AudioEngine engine;
    configure(engine);
    engine.setGlobalSamplePos(1000);
    assert(engine.setTempoKeepingBeat(0.0f) == 1.0);
    assert(engine.setTempoKeepingBeat(std::nanf("")) == 1.0);
    assert(engine.getBPM() == 120.0f && engine.getGlobalSamplePos() == 1000);
    std::printf("[PASS] a zero or NaN tempo changes nothing\n");
}

} // namespace

int main() {
    testWhilePlaying();
    testWhileStopped();
    testInvalidTempoIsIgnored();
    return 0;
}
