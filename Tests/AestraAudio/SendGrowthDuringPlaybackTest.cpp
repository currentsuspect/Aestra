// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Adding sends while the audio thread renders. Each track's send-gain smoothers live in a
// vector the audio thread writes through by pointer; prepareTrackStateForGraph() used to
// grow it with a plain resize on the UI thread, which reallocates (at 1, 2, 4, 8 sends) while
// the audio thread is still writing into the old storage: a use-after-free that AddressSanitizer
// and ThreadSanitizer flag here. Capacity is now reserved before the first send is published.
// Without a sanitizer this still pins that playback stays finite and audible throughout.

#include "GoldenAudio/GoldenAudioHarness.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

using namespace Aestra::Audio;
using namespace GoldenAudio;

namespace {
constexpr double kTau = 6.28318530717958647692;
constexpr int kSources = 6;
constexpr int kBuses = 8;
} // namespace

int main() {
    std::printf("SendGrowthDuringPlaybackTest\n");
    SessionConfig cfg;
    cfg.blockSize = 128;
    const uint32_t frames = cfg.sampleRate * 4;
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(cfg.sampleRate));
    std::vector<float> tone(static_cast<size_t>(frames) * 2);
    for (uint32_t i = 0; i < frames; ++i) {
        tone[i * 2] = tone[i * 2 + 1] = 0.2f * static_cast<float>(std::sin(kTau * 330.0 * i / cfg.sampleRate));
    }
    for (int s = 0; s < kSources; ++s) {
        addAudioTrack(*tm, "src" + std::to_string(s), tone, frames, cfg);
    }
    std::vector<MixerChannel*> buses;
    for (int b = 0; b < kBuses; ++b) {
        buses.push_back(tm->addChannel("bus" + std::to_string(b)));
    }
    AudioEngine engine;
    prepareEngine(engine, tm, cfg);
    engine.setTransportPlaying(true);

    std::atomic<bool> stop{false};
    std::atomic<uint64_t> blocks{0};
    std::atomic<int> nonFinite{0};
    double peak = 0.0;
    std::thread audio([&] {
        std::vector<float> out(static_cast<size_t>(cfg.blockSize) * 2);
        while (!stop.load(std::memory_order_relaxed)) {
            engine.processBlock(out.data(), nullptr, cfg.blockSize, 0.0);
            for (float v : out) {
                if (!std::isfinite(v)) {
                    nonFinite.fetch_add(1);
                }
                peak = std::max(peak, static_cast<double>(std::fabs(v)));
            }
            blocks.fetch_add(1, std::memory_order_relaxed);
        }
    });

    // The UI thread: every source gains its sends one at a time, a graph published per send.
    for (int s = 0; s < kSources; ++s) {
        MixerChannel* source = tm->getChannel(static_cast<size_t>(s));
        for (int b = 0; b < kBuses; ++b) {
            AudioRoute send;
            send.targetChannelId = buses[static_cast<size_t>(b)]->getChannelId();
            send.gain = 0.5f;
            source->addSend(send);
            engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*tm));
            const uint64_t seen = blocks.load();
            while (blocks.load() < seen + 2) {
                std::this_thread::yield(); // let the audio thread render the new graph
            }
        }
    }
    stop.store(true);
    audio.join();

    int failures = 0;
    const auto check = [&](bool cond, const char* what) {
        std::printf("  %s %s\n", cond ? "PASS" : "FAIL", what);
        failures += cond ? 0 : 1;
    };
    check(nonFinite.load() == 0, "output stays finite while sends are added mid-playback");
    check(peak > 0.05, "and audible");
    std::printf("  blocks=%llu graphs=%d peak=%.3f\n", static_cast<unsigned long long>(blocks.load()),
                kSources * kBuses, peak);
    return failures == 0 ? 0 : 1;
}
