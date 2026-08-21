// © 2026 Aestra Studios — All Rights Reserved.
// Long-session resource growth harness (#830).
//
// Simulates hours of compressed production activity on the pattern/Arsenal
// path — step placement/deletion through the real CommandHistory, undo/redo
// bursts, pattern switching, transport restarts — while rendering audio, and
// answers one question: does consumption grow progressively?
//
// Unlike AudioEngineSoakTest (timeline graph path), this exercises the object
// churn surfaces where leak-class bugs historically lived: the pattern
// scheduler's instance table, the command history, and edit notifications.
//
// Growth verdict: RSS samples are collected over the run; after discarding a
// warmup prefix, a linear slope is fitted. The run fails if RSS climbs faster
// than --max-slope-mb-per-min, if absolute post-warmup growth exceeds
// --max-growth-mb, or if engine counters (scheduler overflow, queue drops,
// instance-table bound) regress.

#include "Core/AudioEngine.h"
#include "Core/AudioGraph.h"
#include "Core/AudioGraphBuilder.h"
#include "Models/TrackManager.h"
#include "Commands/AddNoteCommand.h"
#include "Commands/RemoveNoteCommand.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <psapi.h>
#include <windows.h>
#endif

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kBlockSize = 256;
constexpr int kEditBatchBlocks = 512;   // one production action per N blocks
constexpr uint32_t kPatterns = 3;
constexpr uint32_t kStepsPerPattern = 16;
constexpr size_t kMaxLiveInstances = 8; // slot reuse keeps this tiny

struct Sample {
    double minutes;
    double rssMB;
    size_t undoDepth;
    double undoBytesMB;
    size_t liveInstances;
};

#if defined(__linux__) || defined(_WIN32)
// Returns false when RSS telemetry is unavailable or the read fails — callers
// must treat that as "no verdict possible", never as zero growth.
bool getRSSBytes(uint64_t& out) {
#if defined(__linux__)
    std::FILE* f = std::fopen("/proc/self/statm", "r");
    if (!f)
        return false;
    long residentPages = 0;
    const int ok = std::fscanf(f, "%*s %ld", &residentPages);
    std::fclose(f);
    if (ok != 1 || residentPages < 0)
        return false;
    const long pageSize = ::sysconf(_SC_PAGESIZE);
    if (pageSize <= 0)
        return false;
    out = static_cast<uint64_t>(residentPages) * static_cast<uint64_t>(pageSize);
    return true;
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return false;
    out = static_cast<uint64_t>(pmc.WorkingSetSize);
    return true;
#endif
}
#endif

double slopeMBPerMinute(const std::vector<Sample>& samples) {
    // Least-squares fit of RSS vs elapsed minutes.
    const double n = static_cast<double>(samples.size());
    if (n < 2.0)
        return 0.0;
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    for (const auto& s : samples) {
        sx += s.minutes;
        sy += s.rssMB;
        sxx += s.minutes * s.minutes;
        sxy += s.minutes * s.rssMB;
    }
    const double denom = n * sxx - sx * sx;
    if (std::fabs(denom) < 1e-9)
        return 0.0;
    return (n * sxy - sx * sy) / denom;
}

} // namespace

int main(int argc, char** argv) {
    uint32_t durationSeconds = 60;
    double maxSlopeMBPerMin = 6.0;
    double maxGrowthMB = 48.0;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--duration-sec" && i + 1 < argc) {
            // Strict parse: reject signs, garbage, and values beyond uint32_t —
            // strtoul("-1") would otherwise schedule a 136-year session.
            const char* token = argv[++i];
            if (*token == '\0' || *token == '-' || *token == '+') {
                std::cerr << "Invalid --duration-sec: " << token << "\n";
                return 2;
            }
            char* end = nullptr;
            const unsigned long long parsed = std::strtoull(token, &end, 10);
            if (end == token || *end != '\0' || parsed > 0xFFFFFFFFull) {
                std::cerr << "Invalid --duration-sec: " << token << "\n";
                return 2;
            }
            durationSeconds = static_cast<uint32_t>(parsed);
        } else if (a == "--max-slope-mb-per-min" && i + 1 < argc)
            maxSlopeMBPerMin = std::strtod(argv[++i], nullptr);
        else if (a == "--max-growth-mb" && i + 1 < argc)
            maxGrowthMB = std::strtod(argv[++i], nullptr);
    }

    std::cout << "LongSessionResourceGrowthTest\n";
    std::cout << "  durationSec=" << durationSeconds << " maxSlope=" << maxSlopeMBPerMin << "MB/min"
              << " maxGrowth=" << maxGrowthMB << "MB\n";

    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(kSampleRate));

    AudioEngine engine;
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kBlockSize, 2);
    engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*tm));
    if (auto slotMap = tm->getChannelSlotMapShared())
        engine.setChannelSlotMap(slotMap);
    engine.setUnitManager(&tm->getUnitManager());
    engine.setPatternPlaybackEngine(&tm->getPatternPlaybackEngine());

    auto& patterns = tm->getPatternManager();
    std::vector<PatternID> patternIds;
    for (uint32_t p = 0; p < kPatterns; ++p) {
        MidiPayload empty;
        patternIds.push_back(patterns.createMidiPattern("growth" + std::to_string(p), 4.0, empty));
    }

    std::mt19937 rng(0x5EEDu);
    std::uniform_int_distribution<int> stepDist(0, static_cast<int>(kStepsPerPattern) - 1);
    std::uniform_int_distribution<size_t> patternDist(0, kPatterns - 1);

    engine.setPatternPlaybackMode(true, 4.0);
    engine.setGlobalSamplePos(0);
    engine.setMetronomeEnabled(false);
    engine.setAuditionModeEnabled(false);
    engine.setTransportPlaying(true);

    const auto startWall = std::chrono::steady_clock::now();
    const auto deadline = startWall + std::chrono::seconds(durationSeconds);

    std::vector<float> out(static_cast<size_t>(kBlockSize) * 2);
    std::vector<Sample> samples;

    uint64_t blocks = 0;
    uint64_t edits = 0;
    uint64_t undoRedos = 0;
    uint64_t transportRestarts = 0;
    uint64_t overflowAtStart = tm->getPatternPlaybackEngine().getOverflowCount();
    uint64_t overflowMax = overflowAtStart;
    uint64_t queueDropsMax = 0;
    uint64_t xruns = 0;
    uint64_t maxCallbackUs = 0;

    auto sampleNow = [&](double minutes) {
        uint64_t rss = 0;
        const bool haveRss = getRSSBytes(rss);
        if (!haveRss) {
            std::cerr << "\nRESULT: FAIL — RSS telemetry unavailable; no resource verdict possible.\n";
            std::exit(3);
        }
        const auto& undoStack = tm->getCommandHistory().getUndoStack();
        size_t undoBytes = 0;
        for (const auto& cmd : undoStack)
            undoBytes += cmd ? cmd->getSizeInBytes() : 0;
        samples.push_back({minutes,
                           static_cast<double>(rss) / (1024.0 * 1024.0),
                           undoStack.size(),
                           static_cast<double>(undoBytes) / (1024.0 * 1024.0),
                           tm->getPatternPlaybackEngine().getActiveInstanceCount()});
    };

    // No telemetry at startup means the growth checks below would be fiction.
    {
        uint64_t probe = 0;
        if (!getRSSBytes(probe)) {
            std::cerr << "RESULT: SKIP — RSS telemetry unavailable on this platform.\n";
            return 2;
        }
    }

    sampleNow(0.0);
    auto nextSample = startWall + std::chrono::seconds(5);

    while (std::chrono::steady_clock::now() < deadline) {
        const auto t0 = std::chrono::high_resolution_clock::now();
        engine.processBlock(out.data(), nullptr, kBlockSize, 0.0);
        const auto t1 = std::chrono::high_resolution_clock::now();
        const uint64_t cbUs =
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
        ++blocks;
        if (cbUs > maxCallbackUs)
            maxCallbackUs = cbUs;
        if (cbUs > (kBlockSize * 1000000ULL) / kSampleRate)
            ++xruns;

        // One production action every kEditBatchBlocks.
        if (blocks % kEditBatchBlocks == 0) {
            const PatternID pid = patternIds[patternDist(rng)];
            auto& history = tm->getCommandHistory();

            switch (rng() % 5u) {
            case 0:
            case 1: { // place a step
                MidiNote note;
                note.pitch = 60 + stepDist(rng) % 12;
                note.startBeat = static_cast<double>(stepDist(rng)) * 0.25;
                note.durationBeats = 0.25;
                note.velocity = 100.0f / 127.0f;
                note.unitId = 0;
                history.pushAndExecute(std::make_shared<AddNoteCommand>(patterns, pid, note));
                break;
            }
            case 2: { // delete a random step if any exist
                const auto* pat = patterns.getPattern(pid);
                if (pat && pat->isMidi()) {
                    const auto& notes = std::get<MidiPayload>(pat->payload).notes;
                    if (!notes.empty()) {
                        const MidiNote victim = notes[rng() % notes.size()];
                        history.pushAndExecute(std::make_shared<RemoveNoteCommand>(patterns, pid, victim));
                    }
                }
                break;
            }
            case 3: // undo burst
                for (int u = 0; u < 8 && history.canUndo(); ++u) {
                    history.undo();
                    ++undoRedos;
                }
                break;
            default: // redo burst
                for (int r = 0; r < 8 && history.canRedo(); ++r) {
                    history.redo();
                    ++undoRedos;
                }
                break;
            }

            // The editor-side notification path as it exists on develop today.
            // NOTE: when PR #840 (patternContentEdited) merges, switch this to
            // tm->patternContentEdited() so the harness exercises post-fix semantics.
            tm->preparePatternForArsenal(pid);
            ++edits;

            // Rotate which pattern is armed: slot re-arm must REPLACE, never stack up.
            if (edits % 16 == 0) {
                tm->getPatternPlaybackEngine().schedulePatternInstance(
                    patternIds[patternDist(rng)], 0.0, 1);
            }
            // Transport restart churn exercises rewind/catch-up paths.
            if (edits % 64 == 0) {
                engine.setTransportPlaying(false);
                tm->getPatternPlaybackEngine().rewindScheduledInstances();
                engine.setTransportPlaying(true);
                ++transportRestarts;
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= nextSample) {
            nextSample += std::chrono::seconds(5);
            const double minutes =
                std::chrono::duration<double>(now - startWall).count() / 60.0;
            sampleNow(minutes);
            overflowMax = std::max(overflowMax,
                                   static_cast<uint64_t>(tm->getPatternPlaybackEngine().getOverflowCount()));
            queueDropsMax = std::max(queueDropsMax,
                                     static_cast<uint64_t>(engine.commandQueue().droppedCount()));
        }
    }

    const double wallMin = std::chrono::duration<double>(std::chrono::steady_clock::now() - startWall).count() / 60.0;
    sampleNow(wallMin);
    // Close the measurement window: drops during the final interval count too.
    queueDropsMax = std::max(queueDropsMax, static_cast<uint64_t>(engine.commandQueue().droppedCount()));

    // --- Analysis ---
    const size_t warmup = samples.size() / 4; // discard ramp-up allocations
    const std::vector<Sample> steady(samples.begin() + static_cast<long>(warmup), samples.end());

    const double slope = slopeMBPerMinute(steady);
    const double firstRSS = steady.front().rssMB;
    const double lastRSS = steady.back().rssMB;
    const double growthMB = lastRSS - firstRSS;
    size_t instancesPeak = 0;
    size_t undoDepthPeak = 0;
    double undoBytesPeakMB = 0.0;
    for (const auto& s : samples) {
        instancesPeak = std::max(instancesPeak, s.liveInstances);
        undoDepthPeak = std::max(undoDepthPeak, s.undoDepth);
        undoBytesPeakMB = std::max(undoBytesPeakMB, s.undoBytesMB);
    }
    const uint64_t overflowDelta =
        tm->getPatternPlaybackEngine().getOverflowCount() - overflowAtStart;

    std::cout << "\n=== Session ===\n";
    std::cout << "wallMin=" << wallMin << " blocks=" << blocks << " edits=" << edits
              << " undoRedos=" << undoRedos << " restarts=" << transportRestarts << "\n";
    std::cout << "maxCallbackUs=" << maxCallbackUs << " xruns=" << xruns << "\n";

    std::cout << "\n=== Samples ===\n";
    for (const auto& s : samples) {
        std::cout << "t=" << s.minutes << "min rss=" << s.rssMB << "MB undoDepth=" << s.undoDepth
                  << " undoBytes=" << s.undoBytesMB << "MB instances=" << s.liveInstances << "\n";
    }

    std::cout << "\n=== Growth ===\n";
    std::cout << "steadySlope=" << slope << "MB/min (limit " << maxSlopeMBPerMin << ")\n";
    std::cout << "steadyGrowth=" << growthMB << "MB (limit " << maxGrowthMB << ")\n";
    std::cout << "instancesPeak=" << instancesPeak << " (limit " << kMaxLiveInstances << ")\n";
    std::cout << "undoDepthPeak=" << undoDepthPeak << " undoBytesPeak=" << undoBytesPeakMB << "MB\n";
    std::cout << "overflowDelta=" << overflowDelta << " queueDrops=" << queueDropsMax << "\n";

    const bool passSlope = slope <= maxSlopeMBPerMin;
    const bool passGrowth = growthMB <= maxGrowthMB;
    const bool passInstances = instancesPeak <= kMaxLiveInstances;
    const bool passOverflow = overflowDelta == 0 && queueDropsMax == 0;

    std::cout << "\n=== Verdict ===\n";
    std::cout << "slope: " << (passSlope ? "PASS" : "FAIL") << "\n";
    std::cout << "growth: " << (passGrowth ? "PASS" : "FAIL") << "\n";
    std::cout << "instances: " << (passInstances ? "PASS" : "FAIL") << "\n";
    std::cout << "counters: " << (passOverflow ? "PASS" : "FAIL") << "\n";

    const bool pass = passSlope && passGrowth && passInstances && passOverflow;
    std::cout << (pass ? "\nRESULT: PASS\n" : "\nRESULT: FAIL\n");
    return pass ? 0 : 1;
}
