// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Folio baseline collector.
//
// Produces RAW, APPEND-ONLY measurement records. It computes no summaries, no
// percentiles and no verdicts: a collector that interprets is a collector you
// have to re-run when the interpretation changes. Every record is one JSON
// object on one line (JSONL), written with O_APPEND, flushed per line.
//
// WHAT THIS DELIBERATELY DOES NOT REPORT
//
// Callback PERCENTILES do not exist in AudioTelemetry. It carries a running
// total, a timed-callback count, and a max — from which only an average and a
// max are recoverable. Reconstructing p95/p99 from those is arithmetic fiction,
// so the collector emits count/avg/max/budget and stops there. If percentiles
// are wanted, the engine has to grow a histogram first.
//
// COUNTER ISOLATION
//
// AudioTelemetry counters are cumulative for the life of the engine. A window's
// numbers are therefore recorded as an explicit (start, end) pair plus the
// delta, never as a bare end-of-run reading — otherwise device-open and
// warm-up callbacks would be silently attributed to the measured window.
// timedCallbackCount is emitted as the denominator so any later analysis can
// tell "no misses in 40000 callbacks" from "no misses in 3".
//
// INVALIDATION IS DATA
//
// A run that cannot be trusted is still recorded, with `valid: false` and a
// machine-readable `invalidationReason`. Silently dropping bad runs is how a
// series ends up with survivorship bias.

#include "Core/AudioDeviceManager.h"
#include "Core/AudioEngine.h"
#include "Core/AudioRT.h"
#include "Core/AudioGraphBuilder.h"
#include "Core/ProjectSerializer.h"
#include "Models/TrackManager.h"
#include "Plugin/BuiltInPlugins.h"
#include "Plugin/PluginManager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace Aestra::Audio;
namespace fs = std::filesystem;

namespace {

// Frozen collection parameters. Changing any of these changes the series and
// requires a new collection-tool SHA.
constexpr int kSampleIntervalMs = 250;
constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kBlockFrames = 512;
constexpr uint32_t kChannels = 2;
constexpr double kTempoBPM = 120.0;

std::string nowIso8601() {
    const auto now = std::chrono::system_clock::now();
    const auto secs = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm tm{};
    gmtime_r(&secs, &tm);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms));
    return buf;
}

uint64_t monotonicNs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

/// TSC frequency estimate, mirroring AestraAudioController::estimateCycleHz.
///
/// AudioTelemetry records callback DURATIONS only when cycleHz is non-zero —
/// recordCallbackDuration is driven by the host's callback wrapper, and the
/// wrapper needs this calibration to convert cycles to nanoseconds. Without it
/// the engine still counts blocksProcessed, so audio looks healthy while
/// timedCallbackCount stays at zero. Calibrating here is not inventing a
/// measurement: it is the same instrumentation the shipping app installs.
uint64_t estimateCycleHz() {
#if defined(__i386__) || defined(__x86_64__)
    const auto t0 = std::chrono::steady_clock::now();
    const uint64_t c0 = Aestra::Audio::RT::readCycleCounter();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto t1 = std::chrono::steady_clock::now();
    const uint64_t c1 = Aestra::Audio::RT::readCycleCounter();
    const double sec = std::chrono::duration_cast<std::chrono::duration<double>>(t1 - t0).count();
    if (sec <= 0.0 || c1 <= c0) {
        return 0;
    }
    return static_cast<uint64_t>(static_cast<double>(c1 - c0) / sec);
#else
    return 0;
#endif
}

std::string readFileTrimmed(const fs::path& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    std::string s;
    std::getline(in, s);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) {
        s.pop_back();
    }
    return s;
}

std::string jsonEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// --------------------------------------------------------------------------
// /proc and sysfs sampling
// --------------------------------------------------------------------------

struct ProcSample {
    uint64_t utimeTicks = 0;
    uint64_t stimeTicks = 0;
    uint64_t rssKb = 0;
    uint64_t peakRssKb = 0; // VmHWM — kernel-maintained high-water mark
    long numThreads = 0;
    bool ok = false;
};

/// Parses /proc/<pid>/stat. Field 2 (comm) may contain spaces and parentheses,
/// so parsing starts after the LAST ')' rather than tokenising from the front —
/// a process named "a b) c" breaks the naive split.
ProcSample sampleProc(pid_t pid) {
    ProcSample s;
    const fs::path base = "/proc/" + std::to_string(pid);

    std::ifstream statFile(base / "stat");
    if (!statFile) {
        return s;
    }
    std::string content((std::istreambuf_iterator<char>(statFile)), std::istreambuf_iterator<char>());
    const auto close = content.rfind(')');
    if (close == std::string::npos) {
        return s;
    }
    std::istringstream rest(content.substr(close + 1));
    std::vector<std::string> fields;
    std::string tok;
    while (rest >> tok) {
        fields.push_back(tok);
    }
    // After "pid (comm)" the next token is state = index 0 here, so stat field N
    // maps to index N-3. utime = field 14 -> 11, stime = 15 -> 12, threads = 20 -> 17.
    if (fields.size() > 17) {
        s.utimeTicks = std::strtoull(fields[11].c_str(), nullptr, 10);
        s.stimeTicks = std::strtoull(fields[12].c_str(), nullptr, 10);
        s.numThreads = std::strtol(fields[17].c_str(), nullptr, 10);
    }

    std::ifstream statusFile(base / "status");
    std::string line;
    while (std::getline(statusFile, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            s.rssKb = std::strtoull(line.c_str() + 6, nullptr, 10);
        } else if (line.rfind("VmHWM:", 0) == 0) {
            s.peakRssKb = std::strtoull(line.c_str() + 6, nullptr, 10);
        }
    }
    s.ok = true;
    return s;
}

struct ThermalZone {
    std::string type;
    fs::path tempPath;
};

std::vector<ThermalZone> discoverThermalZones() {
    std::vector<ThermalZone> zones;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator("/sys/class/thermal", ec)) {
        const auto name = entry.path().filename().string();
        if (name.rfind("thermal_zone", 0) != 0) {
            continue;
        }
        ThermalZone z;
        z.type = readFileTrimmed(entry.path() / "type");
        z.tempPath = entry.path() / "temp";
        if (fs::exists(z.tempPath)) {
            zones.push_back(std::move(z));
        }
    }
    std::sort(zones.begin(), zones.end(), [](const ThermalZone& a, const ThermalZone& b) { return a.type < b.type; });
    return zones;
}

/// Kernel thermal-throttle counters, where the platform exposes them. These are
/// cumulative, so like the audio counters they are meaningful only as a
/// (start, end) pair.
uint64_t readThrottleCount() {
    uint64_t total = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator("/sys/devices/system/cpu", ec)) {
        const auto name = entry.path().filename().string();
        if (name.rfind("cpu", 0) != 0 || name.size() < 4 || !std::isdigit(static_cast<unsigned char>(name[3]))) {
            continue;
        }
        const auto path = entry.path() / "thermal_throttle" / "core_throttle_count";
        const auto value = readFileTrimmed(path);
        if (!value.empty()) {
            total += std::strtoull(value.c_str(), nullptr, 10);
        }
    }
    return total;
}

// --------------------------------------------------------------------------
// Append-only JSONL sink
// --------------------------------------------------------------------------

class Sink {
public:
    explicit Sink(const fs::path& path) : m_out(path, std::ios::app) {
        if (!m_out) {
            std::cerr << "[collect] cannot open output for append: " << path << "\n";
            std::exit(2);
        }
    }
    void write(const std::string& line) {
        m_out << line << "\n";
        m_out.flush(); // per-line flush: a killed run keeps every sample it took
    }

private:
    std::ofstream m_out;
};

struct TelemetrySnapshot {
    uint64_t blocksProcessed = 0;
    uint64_t timedCallbackCount = 0;
    uint64_t totalCallbackNs = 0;
    uint64_t maxCallbackNs = 0;
    uint64_t budgetNs = 0;
    uint64_t xruns = 0;
    uint64_t underruns = 0;
    uint64_t overruns = 0; // deadline misses: callback ns > budget ns
    uint64_t rtAllocationViolations = 0;
    uint64_t rtLockViolations = 0;
    uint64_t rtLogViolations = 0;
    uint64_t recoveryModeActivations = 0;
    uint64_t srcActiveBlocks = 0;
    uint64_t cycleHz = 0;
    uint32_t threadPriorityStatus = 0;
    int32_t linuxRtPriorityErrno = 0;
};

TelemetrySnapshot snapTelemetry(const AudioTelemetry& t) {
    TelemetrySnapshot s;
    s.blocksProcessed = t.getBlocksProcessed();
    s.timedCallbackCount = t.getTimedCallbackCount();
    s.totalCallbackNs = t.totalCallbackNs.load(std::memory_order_relaxed);
    s.maxCallbackNs = t.getMaxCallbackNs();
    s.budgetNs = t.getCallbackBudgetNs();
    s.xruns = t.getXruns();
    s.underruns = t.getUnderruns();
    s.overruns = t.getOverruns();
    s.rtAllocationViolations = t.getRtAllocationViolations();
    s.rtLockViolations = t.getRtLockViolations();
    s.rtLogViolations = t.getRtLogViolations();
    s.recoveryModeActivations = t.getRecoveryModeActivations();
    s.srcActiveBlocks = t.getSrcActiveBlocks();
    s.cycleHz = t.getCycleHz();
    s.threadPriorityStatus = t.getThreadPriorityStatus();
    s.linuxRtPriorityErrno = t.getLinuxRtPriorityErrno();
    return s;
}

std::string telemetryJson(const char* label, const TelemetrySnapshot& s) {
    std::ostringstream o;
    o << "\"" << label << "\":{"
      << "\"blocksProcessed\":" << s.blocksProcessed << ",\"timedCallbackCount\":" << s.timedCallbackCount
      << ",\"totalCallbackNs\":" << s.totalCallbackNs << ",\"maxCallbackNs\":" << s.maxCallbackNs
      << ",\"budgetNs\":" << s.budgetNs << ",\"xruns\":" << s.xruns << ",\"underruns\":" << s.underruns
      << ",\"deadlineMisses\":" << s.overruns << ",\"rtAllocationViolations\":" << s.rtAllocationViolations
      << ",\"rtLockViolations\":" << s.rtLockViolations << ",\"rtLogViolations\":" << s.rtLogViolations
      << ",\"recoveryModeActivations\":" << s.recoveryModeActivations << ",\"srcActiveBlocks\":" << s.srcActiveBlocks
      << ",\"cycleHz\":" << s.cycleHz << ",\"threadPriorityStatus\":" << s.threadPriorityStatus
      << ",\"linuxRtPriorityErrno\":" << s.linuxRtPriorityErrno << "}";
    return o.str();
}

std::string environmentManifest(const std::string& fixtureRoot, const std::string& engineSha,
                                const std::string& fixtureHash) {
    std::ostringstream o;
    char host[256] = {0};
    gethostname(host, sizeof(host) - 1);

    o << "\"environment\":{"
      << "\"host\":\"" << jsonEscape(host) << "\""
      << ",\"kernel\":\"" << jsonEscape(readFileTrimmed("/proc/sys/kernel/osrelease")) << "\""
      << ",\"clockTicksPerSec\":" << sysconf(_SC_CLK_TCK) << ",\"pageSizeBytes\":" << sysconf(_SC_PAGESIZE)
      << ",\"onlineCpus\":" << sysconf(_SC_NPROCESSORS_ONLN)
      << ",\"sampleIntervalMs\":" << kSampleIntervalMs << ",\"audioSampleRate\":" << kSampleRate
      << ",\"audioBlockFrames\":" << kBlockFrames
      << ",\"engineSha\":\"" << jsonEscape(engineSha) << "\""
      << ",\"fixtureTreeHash\":\"" << jsonEscape(fixtureHash) << "\""
      << ",\"fixtureRoot\":\"" << jsonEscape(fixtureRoot) << "\""
      << ",\"governor\":\""
      << jsonEscape(readFileTrimmed("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor")) << "\""
      << "}";
    return o.str();
}

// --------------------------------------------------------------------------

struct RunContext {
    std::string runId;
    std::string mode;
    int sequence = 0;
    bool official = false;
    std::string fixtureRoot;
    std::string engineSha;
    std::string fixtureHash;
    Sink* sink = nullptr;
    std::vector<ThermalZone> zones;
};

void emitSample(const RunContext& ctx, int index, uint64_t startNs, const ProcSample& proc, uint64_t throttleCount) {
    std::ostringstream o;
    o << "{\"record\":\"sample\""
      << ",\"runId\":\"" << ctx.runId << "\""
      << ",\"sequence\":" << ctx.sequence << ",\"sampleIndex\":" << index
      << ",\"wallClock\":\"" << nowIso8601() << "\""
      << ",\"monotonicNs\":" << (monotonicNs() - startNs) << ",\"pid\":" << getpid()
      << ",\"utimeTicks\":" << proc.utimeTicks << ",\"stimeTicks\":" << proc.stimeTicks
      << ",\"rssKb\":" << proc.rssKb << ",\"peakRssKb\":" << proc.peakRssKb
      << ",\"threads\":" << proc.numThreads << ",\"throttleCount\":" << throttleCount << ",\"thermal\":{";
    bool first = true;
    for (const auto& z : ctx.zones) {
        const auto raw = readFileTrimmed(z.tempPath);
        if (raw.empty()) {
            continue;
        }
        if (!first) {
            o << ",";
        }
        first = false;
        o << "\"" << jsonEscape(z.type) << "\":" << raw; // millidegrees C, raw
    }
    o << "}}";
    ctx.sink->write(o.str());
}

std::string commandLine(int argc, char* argv[]) {
    std::string s;
    for (int i = 0; i < argc; ++i) {
        if (i) s += " ";
        s += argv[i];
    }
    return s;
}

} // namespace

int main(int argc, char* argv[]) {
    std::string fixtureRoot;
    std::string outPath;
    std::string mode = "offline";
    std::string engineSha = "e81fd95a";
    std::string fixtureHash;
    std::string runId;
    double seconds = 20.0;
    int sequence = 0;
    bool official = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : std::string{}; };
        if (a == "--fixture") fixtureRoot = next();
        else if (a == "--out") outPath = next();
        else if (a == "--mode") mode = next();
        else if (a == "--seconds") seconds = std::strtod(next().c_str(), nullptr);
        else if (a == "--sequence") sequence = std::atoi(next().c_str());
        else if (a == "--run-id") runId = next();
        else if (a == "--engine-sha") engineSha = next();
        else if (a == "--fixture-hash") fixtureHash = next();
        else if (a == "--official") official = true;
    }

    if (fixtureRoot.empty() || outPath.empty()) {
        std::cerr << "Usage: collect --fixture <root> --out <file.jsonl> [--mode offline|playback|idle]\n"
                  << "               [--seconds N] [--sequence N] [--run-id ID] [--official]\n"
                  << "\nRuns are NON-OFFICIAL unless --official is passed.\n";
        return 2;
    }

    const std::string command = commandLine(argc, argv);
    const fs::path absOut = fs::absolute(outPath);
    const fs::path absFixture = fs::absolute(fixtureRoot);

    if (runId.empty()) {
        runId = "run-" + std::to_string(monotonicNs());
    }

    std::error_code ec;
    fs::create_directories(absOut.parent_path(), ec);

    // The fixture's MIDI instrument resolves its sample against the process CWD.
    fs::current_path(absFixture, ec);
    if (ec) {
        std::cerr << "[collect] cannot chdir to fixture root\n";
        return 2;
    }

    Sink sink(absOut);
    RunContext ctx;
    ctx.runId = runId;
    ctx.mode = mode;
    ctx.sequence = sequence;
    ctx.official = official;
    ctx.fixtureRoot = absFixture.string();
    ctx.engineSha = engineSha;
    ctx.fixtureHash = fixtureHash;
    ctx.sink = &sink;
    ctx.zones = discoverThermalZones();

    const uint64_t startNs = monotonicNs();

    {
        std::ostringstream o;
        o << "{\"record\":\"run_start\""
          << ",\"runId\":\"" << ctx.runId << "\",\"sequence\":" << sequence << ",\"mode\":\"" << jsonEscape(mode)
          << "\",\"official\":" << (official ? "true" : "false") << ",\"wallClock\":\"" << nowIso8601() << "\""
          << ",\"pid\":" << getpid() << ",\"command\":\"" << jsonEscape(command) << "\""
          << ",\"targetSeconds\":" << seconds << "," << environmentManifest(ctx.fixtureRoot, engineSha, fixtureHash)
          << "}";
        sink.write(o.str());
    }

    std::string invalidation;
    bool valid = true;

    BuiltInPlugins::registerCoreBuiltIns();
    auto& pluginManager = PluginManager::getInstance();
    if (!pluginManager.initialize()) {
        valid = false;
        invalidation = "plugin_manager_init_failed";
    }

    // ---- Load phase (also the subject of load-time measurement) ----
    const uint64_t loadStartNs = monotonicNs();
    auto tm = std::make_shared<TrackManager>();
    auto load = ProjectSerializer::load("folio-baseline.aes", tm);
    const uint64_t loadEndNs = monotonicNs();
    if (!load.ok) {
        valid = false;
        invalidation = "project_load_failed";
    }

    AudioEngine engine;
    AudioDeviceManager deviceManager;
    bool streamOpen = false;

    TelemetrySnapshot windowStart;
    TelemetrySnapshot windowEnd;
    uint64_t throttleStart = readThrottleCount();
    uint64_t windowStartNs = 0;
    uint64_t windowEndNs = 0;

    if (valid) {
        engine.setSampleRate(kSampleRate);
        engine.setBufferConfig(kBlockFrames, kChannels);
        engine.setBPM(static_cast<float>(kTempoBPM));
        tm->setOutputSampleRate(static_cast<double>(kSampleRate));
        engine.setTrackManager(tm);
        engine.setUnitManager(&tm->getUnitManager());
        engine.setPatternPlaybackEngine(&tm->getPatternPlaybackEngine());
        engine.initialize();
        tm->buildAndShareSlotMap();
        if (auto slotMap = tm->getChannelSlotMapShared()) {
            engine.setChannelSlotMap(slotMap);
        }
        engine.setMetronomeEnabled(false);
        engine.setAuditionModeEnabled(false);
        engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*tm));
        engine.setGlobalSamplePos(0);
        tm->getPatternPlaybackEngine().flush();
        tm->scheduleTimelineForOfflineRender(0.0);
    }

    if (valid && mode == "playback") {
        // Real device: the ONLY way xruns, underruns and callback timings
        // become non-zero, because they are recorded by the driver callback
        // wrapper. An offline render never touches that path.
        if (!deviceManager.initialize()) {
            valid = false;
            invalidation = "audio_device_init_failed";
        } else {
            // Device id 0 is not a valid default — it must be resolved from the
            // backend, or openStream rejects the config outright.
            const AudioDeviceInfo defaultOut = deviceManager.getDefaultOutputDevice();
            AudioStreamConfig config;
            config.deviceId = defaultOut.id;
            config.sampleRate = kSampleRate;
            config.bufferSize = kBlockFrames;
            config.numOutputChannels = kChannels;
            config.telemetry = &engine.telemetry();

            {
                std::ostringstream o;
                o << "{\"record\":\"device\",\"runId\":\"" << ctx.runId << "\",\"deviceId\":" << defaultOut.id
                  << ",\"name\":\"" << jsonEscape(defaultOut.name) << "\""
                  << ",\"maxOutputChannels\":" << defaultOut.maxOutputChannels
                  << ",\"preferredSampleRate\":" << defaultOut.preferredSampleRate << "}";
                sink.write(o.str());
            }
            // Calibrate before the stream starts so the very first callbacks are
            // already timed rather than silently dropped from the average.
            engine.telemetry().updateCycleHz(estimateCycleHz());

            // Non-capturing: AudioCallback is a plain function pointer, and the
            // engine arrives through userData. The timing around processBlock
            // mirrors AestraAudioController's wrapper and stays RT-safe — a
            // cycle-counter read and relaxed atomics, no allocation, lock or
            // syscall.
            AudioCallback callback = [](float* out, const float* in, uint32_t frames, double streamTime,
                                        void* user) -> int {
                auto* e = static_cast<AudioEngine*>(user);
                const uint64_t c0 = Aestra::Audio::RT::readCycleCounter();
                e->processBlock(out, in, frames, streamTime);
                const uint64_t c1 = Aestra::Audio::RT::readCycleCounter();
                auto& tel = e->telemetry();
                const uint64_t hz = tel.cycleHz.load(std::memory_order_relaxed);
                if (hz > 0 && c1 > c0) {
                    tel.recordCallbackDuration(((c1 - c0) * 1000000000ull) / hz, frames, kSampleRate);
                }
                return 0;
            };
            if (!deviceManager.openStream(config, callback, &engine) || !deviceManager.startStream()) {
                valid = false;
                invalidation = "audio_stream_start_failed";
            } else {
                streamOpen = true;
                engine.setTransportPlaying(true);
            }
        }
    }

    // ---- Measured window ----
    windowStart = snapTelemetry(engine.telemetry());
    windowStartNs = monotonicNs();

    const int totalSamples = std::max(1, static_cast<int>((seconds * 1000.0) / kSampleIntervalMs));
    uint64_t offlineFramesRendered = 0;

    if (valid && mode == "offline") {
        // Offline render on this thread, sampled between blocks. This is the
        // export-time subject: no device, so callback telemetry stays zero by
        // construction and is reported as such rather than faked.
        std::vector<float> block(static_cast<size_t>(kBlockFrames) * kChannels, 0.0f);
        const auto totalFrames = static_cast<uint64_t>(seconds * kSampleRate);
        uint64_t nextSampleAt = 0;
        int sampleIndex = 0;
        while (offlineFramesRendered < totalFrames) {
            const auto frames =
                static_cast<uint32_t>(std::min<uint64_t>(kBlockFrames, totalFrames - offlineFramesRendered));
            std::fill(block.begin(), block.end(), 0.0f);
            engine.processBlock(block.data(), nullptr, frames,
                                static_cast<double>(offlineFramesRendered) / kSampleRate);
            engine.performNonRealtimeMaintenance();
            offlineFramesRendered += frames;
            if (offlineFramesRendered >= nextSampleAt) {
                emitSample(ctx, sampleIndex++, startNs, sampleProc(getpid()), readThrottleCount());
                nextSampleAt += static_cast<uint64_t>(kSampleRate * kSampleIntervalMs / 1000.0);
            }
        }
    } else {
        // playback (device-driven) and idle: sample on a wall-clock cadence
        // while the audio thread, if any, runs independently.
        for (int i = 0; i < totalSamples; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kSampleIntervalMs));
            emitSample(ctx, i, startNs, sampleProc(getpid()), readThrottleCount());
        }
    }

    windowEndNs = monotonicNs();
    windowEnd = snapTelemetry(engine.telemetry());
    const uint64_t throttleEnd = readThrottleCount();

    // ---- Teardown (cleanup is part of the contract, not an afterthought) ----
    if (streamOpen) {
        engine.setTransportPlaying(false);
        deviceManager.stopStream();
        deviceManager.closeStream();
        deviceManager.shutdown();
        streamOpen = false;
    }
    if (valid) {
        tm->getPatternPlaybackEngine().flush();
    }

    // Callback timing is only meaningful when the cycle counter was calibrated.
    if (valid && mode == "playback") {
        if (windowEnd.timedCallbackCount == windowStart.timedCallbackCount) {
            valid = false;
            invalidation = "no_callbacks_observed_in_window";
        } else if (windowEnd.cycleHz == 0) {
            valid = false;
            invalidation = "cycle_counter_uncalibrated_callback_timing_unavailable";
        }
    }

    {
        const uint64_t dCount = windowEnd.timedCallbackCount - windowStart.timedCallbackCount;
        const uint64_t dTotalNs = windowEnd.totalCallbackNs - windowStart.totalCallbackNs;
        std::ostringstream o;
        o << "{\"record\":\"run_end\""
          << ",\"runId\":\"" << ctx.runId << "\",\"sequence\":" << sequence << ",\"mode\":\"" << jsonEscape(mode)
          << "\",\"official\":" << (official ? "true" : "false") << ",\"wallClock\":\"" << nowIso8601() << "\""
          << ",\"pid\":" << getpid()
          << ",\"projectLoadNs\":" << (loadEndNs - loadStartNs)
          << ",\"windowNs\":" << (windowEndNs - windowStartNs)
          << ",\"offlineFramesRendered\":" << offlineFramesRendered
          << ",\"throttleCountStart\":" << throttleStart << ",\"throttleCountEnd\":" << throttleEnd
          << ",\"finalProcRssKb\":" << sampleProc(getpid()).rssKb
          << ",\"finalProcPeakRssKb\":" << sampleProc(getpid()).peakRssKb << ","
          << telemetryJson("telemetryWindowStart", windowStart) << ","
          << telemetryJson("telemetryWindowEnd", windowEnd)
          // Deltas: the isolated window. Emitted alongside the raw pair so the
          // raw pair remains auditable rather than replaced by a derived value.
          << ",\"windowDelta\":{"
          << "\"timedCallbackCount\":" << dCount
          << ",\"totalCallbackNs\":" << dTotalNs
          << ",\"blocksProcessed\":" << (windowEnd.blocksProcessed - windowStart.blocksProcessed)
          << ",\"xruns\":" << (windowEnd.xruns - windowStart.xruns)
          << ",\"underruns\":" << (windowEnd.underruns - windowStart.underruns)
          << ",\"deadlineMisses\":" << (windowEnd.overruns - windowStart.overruns)
          << ",\"meanCallbackNs\":" << (dCount ? dTotalNs / dCount : 0)
          << "},"
          << "\"callbackPercentiles\":null"
          << ",\"callbackPercentilesNote\":\"AudioTelemetry exposes only a running total, a count and a max; "
             "percentiles are not derivable and are deliberately not approximated\""
          << ",\"valid\":" << (valid ? "true" : "false") << ",\"invalidationReason\":"
          << (invalidation.empty() ? std::string("null") : "\"" + jsonEscape(invalidation) + "\"")
          << ",\"exitStatus\":" << (valid ? 0 : 1) << "}";
        sink.write(o.str());
    }

    std::cout << "[collect] run " << ctx.runId << " mode=" << mode << " official=" << (official ? "yes" : "no")
              << " valid=" << (valid ? "yes" : "no")
              << (invalidation.empty() ? "" : (" reason=" + invalidation)) << " -> " << absOut << "\n";
    return valid ? 0 : 1;
}
