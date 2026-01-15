// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
// Long-session soak harness for AudioEngine (no audio device required).

#include "AudioEngine.h"
#include "AudioGraph.h"
#include "SamplePool.h"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
    #include <unistd.h>
    #include <cstdio>
#elif defined(_WIN32)
    // Windows-specific includes (only in .cpp file)
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <windows.h>
    #include <psapi.h>
#endif

using namespace Nomad::Audio;

namespace {

struct Options {
    uint32_t sampleRate = 48000;
    uint32_t bufferFrames = 256;
    uint32_t tracks = 32;
    uint32_t timelineSeconds = 10;
    uint32_t durationSeconds = 2 * 60 * 60; // 2 hours
    uint32_t commandHz = 500;
    uint32_t graphSwapHz = 10;
    bool realtime = true;
};

uint64_t getRSSBytes() {
#if defined(__linux__)
    std::FILE* f = std::fopen("/proc/self/statm", "r");
    if (!f) return 0;
    long residentPages = 0;
    const int ok = std::fscanf(f, "%*s %ld", &residentPages);
    std::fclose(f);
    if (ok != 1 || residentPages < 0) return 0;
    const long pageSize = ::sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) return 0;
    return static_cast<uint64_t>(residentPages) * static_cast<uint64_t>(pageSize);
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                              sizeof(pmc))) {
        return 0;
    }
    return static_cast<uint64_t>(pmc.WorkingSetSize);
#else
    return 0;
#endif
}

std::shared_ptr<AudioBuffer> makeSineBuffer(uint32_t sampleRate, uint32_t seconds, double frequencyHz) {
    auto buffer = std::make_shared<AudioBuffer>();
    buffer->channels = 2;
    buffer->sampleRate = sampleRate;
    buffer->numFrames = static_cast<uint64_t>(sampleRate) * seconds;
    buffer->data.resize(static_cast<size_t>(buffer->numFrames) * buffer->channels);

    const double twoPi = 6.28318530717958647693;
    const double invSR = 1.0 / static_cast<double>(sampleRate);

    for (uint64_t i = 0; i < buffer->numFrames; ++i) {
        const double t = static_cast<double>(i) * invSR;
        const float s = static_cast<float>(0.2 * std::sin(twoPi * frequencyHz * t));
        buffer->data[static_cast<size_t>(i) * 2] = s;
        buffer->data[static_cast<size_t>(i) * 2 + 1] = s;
    }

    buffer->ready.store(true, std::memory_order_release);
    return buffer;
}

AudioGraph buildLoopGraph(const std::shared_ptr<AudioBuffer>& source,
                          uint32_t engineSampleRate,
                          uint32_t tracks,
                          uint32_t timelineSeconds) {
    AudioGraph graph;
    graph.tracks.reserve(tracks);

    const uint64_t timelineEnd = static_cast<uint64_t>(engineSampleRate) * timelineSeconds;
    graph.timelineEndSample = timelineEnd;

    for (uint32_t i = 0; i < tracks; ++i) {
        TrackRenderState tr;
        tr.trackId = i + 1;
        tr.trackIndex = i;
        tr.volume = 1.0f;
        tr.pan = 0.0f;
        tr.mute = false;
        tr.solo = false;

        ClipRenderState clip;
        clip.buffer = source;
        clip.audioData = source->data.data();
        clip.startSample = 0;
        clip.endSample = timelineEnd;
        clip.sampleOffset = 0;
        clip.totalFrames = source->numFrames;
        clip.sourceSampleRate = static_cast<double>(source->sampleRate);
        clip.gain = 1.0f;
        clip.pan = 0.0f;

        tr.clips.push_back(clip);
        graph.tracks.push_back(std::move(tr));
    }

    return graph;
}

Options parseArgs(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto nextU32 = [&](uint32_t& dst) {
            if (i + 1 >= argc) return;
            dst = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        };

        if (a == "--sr") nextU32(opt.sampleRate);
        else if (a == "--frames") nextU32(opt.bufferFrames);
        else if (a == "--tracks") nextU32(opt.tracks);
        else if (a == "--timeline-sec") nextU32(opt.timelineSeconds);
        else if (a == "--duration-sec") nextU32(opt.durationSeconds);
        else if (a == "--cmd-hz") nextU32(opt.commandHz);
        else if (a == "--graph-hz") nextU32(opt.graphSwapHz);
        else if (a == "--no-realtime") opt.realtime = false;
    }
    return opt;
}

} // namespace

int main(int argc, char** argv) {
    const Options opt = parseArgs(argc, argv);

    std::cout << "NomadAudioSoakTest\n";
    std::cout << "  sr=" << opt.sampleRate
              << " frames=" << opt.bufferFrames
              << " tracks=" << opt.tracks
              << " timelineSec=" << opt.timelineSeconds
              << " durationSec=" << opt.durationSeconds
              << " cmdHz=" << opt.commandHz
              << " graphHz=" << opt.graphSwapHz
              << " realtime=" << (opt.realtime ? "yes" : "no")
              << "\n";

    AudioEngine engine;
    engine.setSampleRate(opt.sampleRate);
    engine.setBufferConfig(opt.bufferFrames, 2);

    // Force SRC activity by using a mismatched source sample rate.
    auto source = makeSineBuffer(44100, opt.timelineSeconds, 997.0);
    auto graph = buildLoopGraph(source, opt.sampleRate, opt.tracks, opt.timelineSeconds);
    engine.setGraph(graph);

    // Start transport via command queue to exercise that path.
    {
        AudioQueueCommand cmd;
        cmd.type = AudioQueueCommandType::SetTransportState;
        cmd.value1 = 1.0f;
        cmd.samplePos = 0;
        engine.commandQueue().push(cmd);
    }

    std::vector<float> out(static_cast<size_t>(opt.bufferFrames) * 2);

    std::atomic<bool> running{true};

    // UI stress thread: spam param changes + periodic graph swaps.
    std::thread stress([&] {
        std::mt19937 rng(0xC0FFEEu);
        std::uniform_int_distribution<uint32_t> trackDist(0, opt.tracks ? (opt.tracks - 1) : 0);
        std::uniform_real_distribution<float> volDist(0.0f, 1.2f);
        std::uniform_real_distribution<float> panDist(-1.0f, 1.0f);
        std::uniform_int_distribution<int> boolDist(0, 1);

        const auto cmdPeriod = (opt.commandHz > 0) ? std::chrono::microseconds(1000000 / opt.commandHz)
                                                   : std::chrono::microseconds(1000000);
        const auto graphPeriod = (opt.graphSwapHz > 0) ? std::chrono::microseconds(1000000 / opt.graphSwapHz)
                                                       : std::chrono::microseconds(1000000);

        auto nextCmd = std::chrono::steady_clock::now();
        auto nextGraph = std::chrono::steady_clock::now();

        while (running.load(std::memory_order_acquire)) {
            const auto now = std::chrono::steady_clock::now();

            if (now >= nextCmd) {
                nextCmd += cmdPeriod;
                const uint32_t trackIndex = trackDist(rng);

                AudioQueueCommand cmd{};
                const int which = static_cast<int>(rng() % 4u);
                switch (which) {
                    case 0:
                        cmd.type = AudioQueueCommandType::SetTrackVolume;
                        cmd.trackIndex = trackIndex;
                        cmd.value1 = volDist(rng);
                        break;
                    case 1:
                        cmd.type = AudioQueueCommandType::SetTrackPan;
                        cmd.trackIndex = trackIndex;
                        cmd.value1 = panDist(rng);
                        break;
                    case 2:
                        cmd.type = AudioQueueCommandType::SetTrackMute;
                        cmd.trackIndex = trackIndex;
                        cmd.value1 = boolDist(rng) ? 1.0f : 0.0f;
                        break;
                    default:
                        cmd.type = AudioQueueCommandType::SetTrackSolo;
                        cmd.trackIndex = trackIndex;
                        cmd.value1 = boolDist(rng) ? 1.0f : 0.0f;
                        break;
                }
                engine.commandQueue().push(cmd);
            }

            if (now >= nextGraph) {
                nextGraph += graphPeriod;

                // Mutate a few clip gains and swap the snapshot (sizes stable => no alloc churn).
                for (uint32_t i = 0; i < std::min<uint32_t>(opt.tracks, 8); ++i) {
                    const uint32_t trackIndex = trackDist(rng);
                    if (trackIndex >= graph.tracks.size()) continue;
                    if (graph.tracks[trackIndex].clips.empty()) continue;
                    graph.tracks[trackIndex].clips[0].gain = 0.6f + 0.4f * volDist(rng);
                }

                engine.setGraph(graph);
            }

            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });

    const uint64_t bufferDurationNs =
        static_cast<uint64_t>((static_cast<double>(opt.bufferFrames) / static_cast<double>(opt.sampleRate)) * 1e9);

    const auto startWall = std::chrono::steady_clock::now();
    auto nextReport = startWall + std::chrono::seconds(5);
    auto nextMemSample = startWall + std::chrono::seconds(10);

    uint64_t blocks = 0;
    uint64_t xruns = 0;
    uint64_t maxCallbackNs = 0;
    long double sumCallbackNs = 0.0;

    const uint64_t rssStart = getRSSBytes();
    uint64_t rssMax = rssStart;

    while (true) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startWall).count();
        if (elapsed >= static_cast<long long>(opt.durationSeconds)) {
            break;
        }

        const auto t0 = std::chrono::high_resolution_clock::now();
        engine.processBlock(out.data(), nullptr, opt.bufferFrames, 0.0);
        const auto t1 = std::chrono::high_resolution_clock::now();

        const uint64_t cbNs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());

        ++blocks;
        sumCallbackNs += static_cast<long double>(cbNs);
        if (cbNs > maxCallbackNs) maxCallbackNs = cbNs;
        if (cbNs > bufferDurationNs) ++xruns;

        if (opt.realtime) {
            const uint64_t sleepNs = (cbNs < bufferDurationNs) ? (bufferDurationNs - cbNs) : 0;
            if (sleepNs > 0) {
                std::this_thread::sleep_for(std::chrono::nanoseconds(sleepNs));
            }
        }

        if (now >= nextMemSample) {
            nextMemSample += std::chrono::seconds(10);
            const uint64_t rss = getRSSBytes();
            if (rss > rssMax) rssMax = rss;
        }

        if (now >= nextReport) {
            nextReport += std::chrono::seconds(5);
            const long double avgNs = (blocks > 0) ? (sumCallbackNs / static_cast<long double>(blocks)) : 0.0;
            const double loadPct = (bufferDurationNs > 0)
                                       ? (static_cast<double>(maxCallbackNs) / static_cast<double>(bufferDurationNs)) * 100.0
                                       : 0.0;

            std::cout << "t=" << elapsed << "s"
                      << " blocks=" << blocks
                      << " avg=" << (static_cast<double>(avgNs) / 1e6) << "ms"
                      << " max=" << (static_cast<double>(maxCallbackNs) / 1e6) << "ms"
                      << " xruns=" << xruns
                      << " qDepthMax=" << engine.commandQueue().maxDepth()
                      << " qDrops=" << engine.commandQueue().droppedCount()
                      << " rssMB=" << (rssMax / (1024.0 * 1024.0))
                      << " peakLoad=" << loadPct << "%"
                      << "\n";
        }
    }

    running.store(false, std::memory_order_release);
    stress.join();

    const auto endWall = std::chrono::steady_clock::now();
    const double wallSec = std::chrono::duration_cast<std::chrono::duration<double>>(endWall - startWall).count();
    const double avgNs = (blocks > 0) ? static_cast<double>(sumCallbackNs / static_cast<long double>(blocks)) : 0.0;

    // Drift: engine increments global sample pos while transport is playing.
    const uint64_t expectedSamples = blocks * static_cast<uint64_t>(opt.bufferFrames);
    const uint64_t actualSamples = engine.getGlobalSamplePos();
    const uint64_t loopLen = graph.timelineEndSample;
    const uint64_t expectedPos = (loopLen > 0) ? (expectedSamples % loopLen) : expectedSamples;
    const int64_t driftSamples = static_cast<int64_t>(actualSamples) - static_cast<int64_t>(expectedPos);

    std::cout << "\n=== Summary ===\n";
    std::cout << "wallSec=" << wallSec << "\n";
    std::cout << "blocks=" << blocks << "\n";
    std::cout << "avgCallbackMs=" << (avgNs / 1e6) << "\n";
    std::cout << "maxCallbackMs=" << (static_cast<double>(maxCallbackNs) / 1e6) << "\n";
    std::cout << "bufferMs=" << (static_cast<double>(bufferDurationNs) / 1e6) << "\n";
    std::cout << "xruns=" << xruns << "\n";
    std::cout << "queueDrops=" << engine.commandQueue().droppedCount() << "\n";
    std::cout << "queueDepthMax=" << engine.commandQueue().maxDepth() << "\n";
    std::cout << "driftSamples=" << driftSamples << "\n";
    std::cout << "rssStartMB=" << (rssStart / (1024.0 * 1024.0)) << "\n";
    std::cout << "rssMaxMB=" << (rssMax / (1024.0 * 1024.0)) << "\n";

    // Pass/fail thresholds (tune as we collect baselines).
    const bool passXruns = (xruns == 0);
    const bool passDrift = (driftSamples == 0);
    const bool passQueueDrops = (engine.commandQueue().droppedCount() == 0);

    const double headroomPct =
        (bufferDurationNs > 0) ? (static_cast<double>(maxCallbackNs) / static_cast<double>(bufferDurationNs)) * 100.0 : 0.0;
    const bool passHeadroom = (headroomPct < 80.0); // max callback must be <80% of buffer budget

    std::cout << "\n=== Thresholds ===\n";
    std::cout << "xruns==0: " << (passXruns ? "PASS" : "FAIL") << "\n";
    std::cout << "drift==0: " << (passDrift ? "PASS" : "FAIL") << "\n";
    std::cout << "queueDrops==0: " << (passQueueDrops ? "PASS" : "FAIL") << "\n";
    std::cout << "max<80% budget: " << (passHeadroom ? "PASS" : "FAIL") << " (" << headroomPct << "%)\n";

    const bool pass = passXruns && passDrift && passQueueDrops && passHeadroom;
    return pass ? 0 : 1;
}
