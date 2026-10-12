// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V81-M2: the master meter slot carries momentary and short-term loudness and the held true peak, written by
// the real engine from real audio. A steady stereo sine has a loudness that follows from its amplitude, so
// the readouts are checked against the signal, and short-term must agree with the engine's integrated value.

#include "GoldenAudio/GoldenAudioHarness.h"

#include "Core/ChannelSlotMap.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace Aestra;
using namespace Aestra::Audio;
using namespace GoldenAudio;

namespace {

constexpr double kTau = 6.28318530717958647692;
int g_failures = 0;
void check(bool cond, const std::string& what) {
    std::cout << "  " << (cond ? "PASS " : "FAIL ") << what << "\n";
    if (!cond)
        ++g_failures;
}

} // namespace

int main() {
    SessionConfig cfg;
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(cfg.sampleRate);
    tm->getPlaylistModel().setBPM(cfg.bpm);
    const uint32_t frames = cfg.sampleRate * 8; // 8 s
    std::vector<float> sine(static_cast<size_t>(frames) * 2);
    const float amp = 0.5f;
    for (uint32_t i = 0; i < frames; ++i) {
        const float v = amp * static_cast<float>(std::sin(kTau * 1000.0 * i / cfg.sampleRate));
        sine[static_cast<size_t>(i) * 2] = v;
        sine[static_cast<size_t>(i) * 2 + 1] = v;
    }
    addAudioTrack(*tm, "Tone", sine, frames, cfg);
    auto enginePtr = std::make_unique<AudioEngine>(); // large: keep it off the stack
    AudioEngine& engine = *enginePtr;
    auto snaps = std::make_shared<MeterSnapshotBuffer>();
    prepareEngine(engine, tm, cfg);
    engine.setMeterSnapshots(snaps);
    engine.setTransportPlaying(true);
    const int masterSlot = static_cast<int>(ChannelSlotMap::MASTER_SLOT_INDEX);

    std::cout << "[1] nothing yet before a full momentary window\n";
    renderBlocks(engine, cfg.sampleRate / 5, cfg); // 200 ms
    {
        const auto r = snaps->readMeter(masterSlot);
        check(r.momentaryLufs <= -100.0f, "200 ms in, momentary reads silence (" + std::to_string(r.momentaryLufs) + ")");
    }

    std::cout << "[2] a steady tone reads steady loudness in all three\n";
    renderBlocks(engine, cfg.sampleRate * 5, cfg);
    // The integrated value is computed by a worker thread; give it a moment, then render one more block to publish it.
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    renderBlocks(engine, cfg.blockSize, cfg);
    const auto r = snaps->readMeter(masterSlot);
    // 1 kHz sine, 0.5 peak on both channels: mean square 0.125 per channel, K-weighting ~ +0.7 dB at 1 kHz.
    const double expected = -0.691 + 10.0 * std::log10(2.0 * 0.125) + 0.7;
    check(std::fabs(r.momentaryLufs - expected) < 1.0, "momentary ~" + std::to_string(expected) + " LUFS: " + std::to_string(r.momentaryLufs));
    check(std::fabs(r.shortTermLufs - expected) < 1.0, "short-term ~" + std::to_string(expected) + " LUFS: " + std::to_string(r.shortTermLufs));
    check(std::fabs(r.shortTermLufs - r.momentaryLufs) < 0.3f, "and the two agree on a steady tone");
    check(r.integratedLufs > -100.0f && std::fabs(r.shortTermLufs - r.integratedLufs) < 1.0f,
          "short-term agrees with the engine's integrated value (" + std::to_string(r.integratedLufs) + ")");

    std::cout << "[3] the held true peak is at least the sample peak\n";
    const float tpDb = 20.0f * std::log10(std::max(r.truePeakL, 1e-9f));
    check(r.truePeakL >= 0.49f && r.truePeakL < 0.6f, "true peak L about 0.5 linear: " + std::to_string(r.truePeakL));
    check(std::fabs(r.truePeakL - r.truePeakR) < 0.01f, "left and right match for a mono tone");
    check(std::fabs(tpDb - (-6.0f)) < 0.6f, "about -6 dBTP: " + std::to_string(tpDb));

    std::cout << "[4] resetting loudness clears the readouts\n";
    engine.resetLoudness();
    renderBlocks(engine, cfg.sampleRate / 10, cfg);
    {
        const auto after = snaps->readMeter(masterSlot);
        check(after.momentaryLufs <= -100.0f && after.shortTermLufs <= -100.0f,
              "momentary and short-term start again from nothing");
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
