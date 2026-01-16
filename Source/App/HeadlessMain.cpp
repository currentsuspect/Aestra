// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "AudioCommandQueue.h"
#include "AudioEngine.h"
#include "AudioGraph.h"
#include "AudioGraphBuilder.h"
#include "SamplePool.h"
#include "TrackManager.h"

#include "../Core/ProjectSerializer.h"

#include "../AestraCore/include/AestraJSON.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace Aestra;
using namespace Aestra::Audio;

namespace {

constexpr uint32_t kChannels = 2;

enum class ScenarioType {
    Project,
    Tone,
    Stress,
};

struct Scenario {
    std::string name;
    ScenarioType type{ScenarioType::Project};

    uint32_t sampleRate{44100};
    uint32_t bufferFrames{256};
    uint32_t seconds{10};
    float minPeak{1.0e-6f};

    // Project
    std::string projectPath;

    // Tone
    double frequencyHz{440.0};
    double amplitude{0.2};

    // Stress
    uint32_t tracks{32};
    uint32_t timelineSeconds{10};
    uint32_t commandHz{500};
    uint32_t graphSwapHz{10};
};

struct RunMetrics {
    bool ok{false};
    std::string failure;
    float maxPeak{0.0f};
    float rms{0.0f};
    uint64_t nanOrInfCount{0};
    uint64_t hash64{0};
    double renderMs{0.0};
    double audioSeconds{0.0};
    double cpuRatio{0.0};
};

static bool isBadSample(float v) {
    return std::isnan(v) || !std::isfinite(v);
}

static uint64_t fnv1a64_append_u32(uint64_t h, uint32_t v) {
    constexpr uint64_t prime = 1099511628211ull;
    for (int i = 0; i < 4; ++i) {
        const uint8_t b = static_cast<uint8_t>((v >> (i * 8)) & 0xFFu);
        h ^= static_cast<uint64_t>(b);
        h *= prime;
    }
    return h;
}

static uint64_t fnv1a64_init() {
    return 14695981039346656037ull;
}

static bool readTextFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

static std::string resolvePathRelativeToCwd(const std::string& maybeRelative) {
    if (maybeRelative.empty()) return {};
    namespace fs = std::filesystem;
    fs::path p(maybeRelative);
    if (p.is_absolute()) return p.string();
    return (fs::current_path() / p).string();
}

static bool jsonGetU32(const JSON& obj, const char* key, uint32_t& out) {
    if (!obj.isObject() || !obj.has(key)) return false;
    const auto& v = obj[key];
    if (!v.isNumber()) return false;
    const double n = v.asNumber();
    if (n < 0.0) return false;
    out = static_cast<uint32_t>(n);
    return true;
}

static bool jsonGetDouble(const JSON& obj, const char* key, double& out) {
    if (!obj.isObject() || !obj.has(key)) return false;
    const auto& v = obj[key];
    if (!v.isNumber()) return false;
    out = v.asNumber();
    return true;
}

static bool jsonGetString(const JSON& obj, const char* key, std::string& out) {
    if (!obj.isObject() || !obj.has(key)) return false;
    const auto& v = obj[key];
    if (!v.isString()) return false;
    out = v.asString();
    return true;
}

static ScenarioType parseScenarioType(const std::string& s, bool& ok) {
    ok = true;
    if (s == "project") return ScenarioType::Project;
    if (s == "tone") return ScenarioType::Tone;
    if (s == "stress") return ScenarioType::Stress;
    ok = false;
    return ScenarioType::Project;
}

static std::vector<Scenario> loadScenarioFile(const std::string& path) {
    std::vector<Scenario> scenarios;
    std::string text;
    if (!readTextFile(path, text)) return scenarios;

    JSON root = JSON::parse(text);
    if (!root.isObject() || !root.has("scenarios") || !root["scenarios"].isArray()) return scenarios;

    const auto& arr = root["scenarios"].asArray();
    scenarios.reserve(arr.size());

    for (const auto& item : arr) {
        if (!item.isObject()) continue;

        Scenario sc;
        jsonGetString(item, "name", sc.name);

        std::string typeStr;
        if (jsonGetString(item, "type", typeStr)) {
            bool ok = false;
            sc.type = parseScenarioType(typeStr, ok);
            if (!ok) continue;
        }

        jsonGetU32(item, "sr", sc.sampleRate);
        jsonGetU32(item, "frames", sc.bufferFrames);
        jsonGetU32(item, "seconds", sc.seconds);

        double minPeak = static_cast<double>(sc.minPeak);
        if (jsonGetDouble(item, "minPeak", minPeak)) sc.minPeak = static_cast<float>(minPeak);

        // Project
        jsonGetString(item, "project", sc.projectPath);

        // Tone
        jsonGetDouble(item, "frequencyHz", sc.frequencyHz);
        jsonGetDouble(item, "amplitude", sc.amplitude);

        // Stress
        jsonGetU32(item, "tracks", sc.tracks);
        jsonGetU32(item, "timelineSeconds", sc.timelineSeconds);
        jsonGetU32(item, "commandHz", sc.commandHz);
        jsonGetU32(item, "graphSwapHz", sc.graphSwapHz);

        if (sc.name.empty()) continue;
        scenarios.push_back(std::move(sc));
    }

    return scenarios;
}

static std::shared_ptr<AudioBuffer> makeSineBuffer(uint32_t sampleRate, uint32_t seconds, double frequencyHz, double amplitude) {
    auto buffer = std::make_shared<AudioBuffer>();
    buffer->channels = 2;
    buffer->sampleRate = sampleRate;
    buffer->numFrames = static_cast<uint64_t>(sampleRate) * seconds;
    buffer->data.resize(static_cast<size_t>(buffer->numFrames) * buffer->channels);

    const double twoPi = 6.28318530717958647693;
    const double invSR = 1.0 / static_cast<double>(sampleRate);

    for (uint64_t i = 0; i < buffer->numFrames; ++i) {
        const double t = static_cast<double>(i) * invSR;
        const float s = static_cast<float>(amplitude * std::sin(twoPi * frequencyHz * t));
        buffer->data[static_cast<size_t>(i) * 2] = s;
        buffer->data[static_cast<size_t>(i) * 2 + 1] = s;
    }

    buffer->ready.store(true, std::memory_order_release);
    return buffer;
}

static AudioGraph buildLoopGraph(const std::shared_ptr<AudioBuffer>& source,
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
        clip.channels = static_cast<uint32_t>(source->channels);
        clip.gain = 1.0f;
        clip.pan = 0.0f;

        tr.clips.push_back(std::move(clip));
        graph.tracks.push_back(std::move(tr));
    }

    return graph;
}

static RunMetrics renderEngine(AudioEngine& engine,
                               uint32_t sampleRate,
                               uint32_t bufferFrames,
                               uint32_t seconds,
                               float minPeak,
                               std::function<void(uint64_t /*blockIndex*/)> onBlock = {}) {
    RunMetrics m;
    m.audioSeconds = static_cast<double>(seconds);

    const uint64_t totalFrames = static_cast<uint64_t>(sampleRate) * seconds;
    uint64_t framesProcessed = 0;
    uint64_t blockIndex = 0;

    std::vector<float> out(static_cast<size_t>(bufferFrames) * kChannels);

    float maxPeak = 0.0f;
    long double sumSquares = 0.0L;
    uint64_t sampleCount = 0;
    uint64_t nanCount = 0;

    uint64_t hash = fnv1a64_init();

    const auto start = std::chrono::steady_clock::now();
    double streamTime = 0.0;

    while (framesProcessed < totalFrames) {
        if (onBlock) onBlock(blockIndex);

        const uint32_t framesThisBlock = static_cast<uint32_t>(
            std::min<uint64_t>(bufferFrames, totalFrames - framesProcessed));

        engine.processBlock(out.data(), nullptr, framesThisBlock, streamTime);

        for (uint32_t i = 0; i < framesThisBlock * kChannels; ++i) {
            const float s = out[i];
            if (isBadSample(s)) {
                nanCount++;
                continue;
            }

            maxPeak = std::max(maxPeak, std::fabs(s));
            const long double sd = static_cast<long double>(s);
            sumSquares += sd * sd;
            sampleCount++;

            uint32_t bits = 0;
            static_assert(sizeof(float) == sizeof(uint32_t));
            std::memcpy(&bits, &s, sizeof(bits));
            hash = fnv1a64_append_u32(hash, bits);
        }

        framesProcessed += framesThisBlock;
        streamTime += static_cast<double>(framesThisBlock) / static_cast<double>(sampleRate);
        blockIndex++;
    }

    const auto end = std::chrono::steady_clock::now();
    const std::chrono::duration<double, std::milli> durMs = end - start;
    m.renderMs = durMs.count();
    m.cpuRatio = (m.audioSeconds > 0.0) ? (m.renderMs / (m.audioSeconds * 1000.0)) : 0.0;
    m.maxPeak = maxPeak;
    m.nanOrInfCount = nanCount;
    m.hash64 = hash;
    m.rms = (sampleCount > 0) ? static_cast<float>(std::sqrt(static_cast<double>(sumSquares / static_cast<long double>(sampleCount)))) : 0.0f;

    if (nanCount > 0) {
        m.ok = false;
        m.failure = "nan_or_inf";
        return m;
    }
    if (isBadSample(maxPeak)) {
        m.ok = false;
        m.failure = "invalid_peak";
        return m;
    }
    if (maxPeak < minPeak) {
        m.ok = false;
        m.failure = "near_silence";
        return m;
    }

    m.ok = true;
    return m;
}

static RunMetrics runScenarioProject(const Scenario& sc) {
    const std::string projectPath = resolvePathRelativeToCwd(sc.projectPath);
    if (projectPath.empty()) {
        RunMetrics m;
        m.ok = false;
        m.failure = "missing_project";
        return m;
    }

    auto trackManager = std::make_shared<TrackManager>();
    trackManager->setOutputSampleRate(static_cast<double>(sc.sampleRate));
    trackManager->setInputSampleRate(static_cast<double>(sc.sampleRate));
    trackManager->setInputChannelCount(0);

    ProjectSerializer::LoadResult loadRes = ProjectSerializer::load(projectPath, trackManager);
    if (!loadRes.ok) {
        RunMetrics m;
        m.ok = false;
        m.failure = "project_load_failed";
        return m;
    }

    AudioEngine engine;
    engine.setSampleRate(sc.sampleRate);
    engine.setBufferConfig(sc.bufferFrames, kChannels);

    // Mirror app wiring for metering + routing.
    auto meterBuffer = std::make_shared<MeterSnapshotBuffer>();
    engine.setMeterSnapshots(meterBuffer);
    trackManager->setMeterSnapshots(meterBuffer);
    if (auto slotMap = trackManager->getChannelSlotMapShared()) {
        engine.setChannelSlotMap(slotMap);
    }

    engine.setBPM(static_cast<float>(loadRes.tempo));
    trackManager->setPosition(loadRes.playhead);
    engine.setGlobalSamplePos(static_cast<uint64_t>(loadRes.playhead * static_cast<double>(sc.sampleRate)));

    auto graph = AudioGraphBuilder::buildFromTrackManager(*trackManager, static_cast<double>(sc.sampleRate));
    engine.setGraph(graph);

    engine.setTransportPlaying(true);
    {
        AudioQueueCommand cmd{};
        cmd.type = AudioQueueCommandType::SetTransportState;
        cmd.value1 = 1.0f;
        cmd.samplePos = engine.getGlobalSamplePos();
        engine.commandQueue().push(cmd);
    }

    return renderEngine(engine, sc.sampleRate, sc.bufferFrames, sc.seconds, sc.minPeak);
}

static RunMetrics runScenarioTone(const Scenario& sc) {
    AudioEngine engine;
    engine.setSampleRate(sc.sampleRate);
    engine.setBufferConfig(sc.bufferFrames, kChannels);

    auto source = makeSineBuffer(sc.sampleRate, sc.seconds, sc.frequencyHz, sc.amplitude);
    engine.setGraph(buildLoopGraph(source, sc.sampleRate, 1, sc.seconds));

    engine.setTransportPlaying(true);
    {
        AudioQueueCommand cmd{};
        cmd.type = AudioQueueCommandType::SetTransportState;
        cmd.value1 = 1.0f;
        cmd.samplePos = 0;
        engine.commandQueue().push(cmd);
    }

    return renderEngine(engine, sc.sampleRate, sc.bufferFrames, sc.seconds, sc.minPeak);
}

static RunMetrics runScenarioStress(const Scenario& sc) {
    AudioEngine engine;
    engine.setSampleRate(sc.sampleRate);
    engine.setBufferConfig(sc.bufferFrames, kChannels);

    // Force some SRC activity by using mismatched source SR.
    auto source = makeSineBuffer(44100, sc.timelineSeconds, 997.0, 0.2);
    auto graph = buildLoopGraph(source, sc.sampleRate, std::max<uint32_t>(1, sc.tracks), sc.timelineSeconds);
    engine.setGraph(graph);

    engine.setTransportPlaying(true);
    {
        AudioQueueCommand cmd{};
        cmd.type = AudioQueueCommandType::SetTransportState;
        cmd.value1 = 1.0f;
        cmd.samplePos = 0;
        engine.commandQueue().push(cmd);
    }

    // Deterministic interleaving: push commands / swap graph based on block index.
    const uint64_t blocksPerSecond = (sc.sampleRate > 0 && sc.bufferFrames > 0)
        ? static_cast<uint64_t>(std::max<double>(1.0, static_cast<double>(sc.sampleRate) / static_cast<double>(sc.bufferFrames)))
        : 1;

    const uint64_t cmdEvery = (sc.commandHz > 0)
        ? std::max<uint64_t>(1, blocksPerSecond / sc.commandHz)
        : std::numeric_limits<uint64_t>::max();

    const uint64_t graphEvery = (sc.graphSwapHz > 0)
        ? std::max<uint64_t>(1, blocksPerSecond / sc.graphSwapHz)
        : std::numeric_limits<uint64_t>::max();

    std::mt19937 rng(0xC0FFEEu);
    std::uniform_int_distribution<uint32_t> trackDist(0, std::max<uint32_t>(1, sc.tracks) - 1);
    std::uniform_real_distribution<float> volDist(0.0f, 1.2f);
    std::uniform_real_distribution<float> panDist(-1.0f, 1.0f);
    std::uniform_int_distribution<int> boolDist(0, 1);

    auto onBlock = [&](uint64_t blockIndex) {
        if (blockIndex % cmdEvery == 0) {
            AudioQueueCommand cmd{};
            const uint32_t trackIndex = trackDist(rng);
            cmd.trackIndex = trackIndex;
            const int which = static_cast<int>(rng() % 4u);
            switch (which) {
                case 0:
                    cmd.type = AudioQueueCommandType::SetTrackVolume;
                    cmd.value1 = volDist(rng);
                    break;
                case 1:
                    cmd.type = AudioQueueCommandType::SetTrackPan;
                    cmd.value1 = panDist(rng);
                    break;
                case 2:
                    cmd.type = AudioQueueCommandType::SetTrackMute;
                    cmd.value1 = boolDist(rng) ? 1.0f : 0.0f;
                    break;
                default:
                    cmd.type = AudioQueueCommandType::SetTrackSolo;
                    cmd.value1 = boolDist(rng) ? 1.0f : 0.0f;
                    break;
            }
            engine.commandQueue().push(cmd);
        }

        if (blockIndex % graphEvery == 0) {
            // Mutate a few clip gains and swap.
            for (uint32_t i = 0; i < std::min<uint32_t>(sc.tracks, 8); ++i) {
                const uint32_t trackIndex = trackDist(rng);
                if (trackIndex >= graph.tracks.size()) continue;
                if (graph.tracks[trackIndex].clips.empty()) continue;
                graph.tracks[trackIndex].clips[0].gain = 0.6f + 0.4f * volDist(rng);
            }
            engine.setGraph(graph);
        }
    };

    return renderEngine(engine, sc.sampleRate, sc.bufferFrames, sc.seconds, sc.minPeak, onBlock);
}

static RunMetrics runScenario(const Scenario& sc) {
    switch (sc.type) {
        case ScenarioType::Project: return runScenarioProject(sc);
        case ScenarioType::Tone: return runScenarioTone(sc);
        case ScenarioType::Stress: return runScenarioStress(sc);
    }
    RunMetrics m;
    m.ok = false;
    m.failure = "unknown_type";
    return m;
}

static JSON makeReportJson(const Scenario& sc, const RunMetrics& m) {
    auto toHex64 = [](uint64_t v) {
        std::ostringstream oss;
        oss << "0x" << std::hex << v;
        return oss.str();
    };

    JSON r = JSON::object();
    r.set("name", JSON(sc.name));
    r.set("type", JSON(sc.type == ScenarioType::Project ? "project" : sc.type == ScenarioType::Tone ? "tone" : "stress"));
    r.set("ok", JSON(m.ok));
    if (!m.failure.empty()) r.set("failure", JSON(m.failure));

    JSON cfg = JSON::object();
    cfg.set("sr", JSON(static_cast<double>(sc.sampleRate)));
    cfg.set("frames", JSON(static_cast<double>(sc.bufferFrames)));
    cfg.set("seconds", JSON(static_cast<double>(sc.seconds)));
    cfg.set("minPeak", JSON(static_cast<double>(sc.minPeak)));
    if (sc.type == ScenarioType::Project) cfg.set("project", JSON(sc.projectPath));
    if (sc.type == ScenarioType::Tone) {
        cfg.set("frequencyHz", JSON(sc.frequencyHz));
        cfg.set("amplitude", JSON(sc.amplitude));
    }
    if (sc.type == ScenarioType::Stress) {
        cfg.set("tracks", JSON(static_cast<double>(sc.tracks)));
        cfg.set("timelineSeconds", JSON(static_cast<double>(sc.timelineSeconds)));
        cfg.set("commandHz", JSON(static_cast<double>(sc.commandHz)));
        cfg.set("graphSwapHz", JSON(static_cast<double>(sc.graphSwapHz)));
    }
    r.set("config", cfg);

    JSON metrics = JSON::object();
    metrics.set("maxPeak", JSON(static_cast<double>(m.maxPeak)));
    metrics.set("rms", JSON(static_cast<double>(m.rms)));
    metrics.set("nanOrInfCount", JSON(static_cast<double>(m.nanOrInfCount)));
    metrics.set("hash64", JSON(toHex64(m.hash64)));
    metrics.set("renderMs", JSON(m.renderMs));
    metrics.set("audioSeconds", JSON(m.audioSeconds));
    metrics.set("cpuRatio", JSON(m.cpuRatio));
    r.set("metrics", metrics);

    return r;
}

struct Cli {
    // Scenario mode
    std::string scenarioFile;
    std::string scenarioName;
    std::string reportPath;
    bool listScenarios{false};
    bool human{false};

    // Legacy single-run mode (kept for convenience)
    std::string projectPath;
    uint32_t sr{44100};
    uint32_t frames{256};
    uint32_t seconds{10};
    float minPeak{1.0e-6f};
};

static void printHelp() {
    std::cout
        << "AestraHeadless\n"
        << "\n"
        << "Scenario runner:\n"
        << "  --scenario-file <path>     JSON scenario file (default: Tests/headless_scenarios.json if present)\n"
        << "  --scenario <name>          Run a single scenario by name (default: run all)\n"
        << "  --list-scenarios           Print scenario names and exit\n"
        << "  --report <path>            Write JSON report to file (array for multi-run)\n"
        << "  --human                    Print a compact summary to stderr\n"
        << "\n"
        << "Legacy single-run (project render):\n"
        << "  --project <path>           Load .aes/.nomadproj and render\n"
        << "  --sr <hz>                  Sample rate\n"
        << "  --frames <n>               Buffer frames\n"
        << "  --seconds <n>              Duration\n"
        << "  --min-peak <v>              Fail if max peak < v\n";
}

static Cli parseCli(int argc, char** argv) {
    Cli cli;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        auto nextStr = [&](std::string& dst) {
            if (i + 1 >= argc) return;
            dst = argv[++i];
        };
        auto nextU32 = [&](uint32_t& dst) {
            if (i + 1 >= argc) return;
            dst = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        };

        if (std::strcmp(a, "--scenario-file") == 0) nextStr(cli.scenarioFile);
        else if (std::strcmp(a, "--scenario") == 0) nextStr(cli.scenarioName);
        else if (std::strcmp(a, "--report") == 0) nextStr(cli.reportPath);
        else if (std::strcmp(a, "--list-scenarios") == 0) cli.listScenarios = true;
        else if (std::strcmp(a, "--human") == 0) cli.human = true;

        else if (std::strcmp(a, "--project") == 0) nextStr(cli.projectPath);
        else if (std::strcmp(a, "--sr") == 0) nextU32(cli.sr);
        else if (std::strcmp(a, "--frames") == 0) nextU32(cli.frames);
        else if (std::strcmp(a, "--seconds") == 0) nextU32(cli.seconds);
        else if (std::strcmp(a, "--min-peak") == 0) {
            if (i + 1 < argc) cli.minPeak = static_cast<float>(std::strtod(argv[++i], nullptr));
        }

        else if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
            printHelp();
            std::exit(0);
        }
    }

    if (cli.scenarioFile.empty()) {
        namespace fs = std::filesystem;
        const fs::path defaultPath = fs::current_path() / "Tests" / "headless_scenarios.json";
        std::error_code ec;
        if (fs::exists(defaultPath, ec) && !ec) {
            cli.scenarioFile = defaultPath.string();
        }
    }

    if (const char* env = std::getenv("AESTRA_MIN_PEAK")) {
        cli.minPeak = static_cast<float>(std::strtod(env, nullptr));
    }

    return cli;
}

static int runLegacyProjectOnce(const Cli& cli) {
    Scenario sc;
    sc.name = "legacy_project";
    sc.type = ScenarioType::Project;
    sc.sampleRate = cli.sr;
    sc.bufferFrames = cli.frames;
    sc.seconds = cli.seconds;
    sc.minPeak = cli.minPeak;
    sc.projectPath = cli.projectPath;

    const RunMetrics m = runScenario(sc);
    const JSON report = makeReportJson(sc, m);
    std::cout << report.toString(0) << "\n";
    return m.ok ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    const Cli cli = parseCli(argc, argv);

    // Scenario-file mode.
    if (!cli.scenarioFile.empty()) {
        const std::vector<Scenario> scenarios = loadScenarioFile(cli.scenarioFile);
        if (scenarios.empty()) {
            std::cerr << "AestraHeadless: scenario file empty or invalid: " << cli.scenarioFile << "\n";
            return 2;
        }

        if (cli.listScenarios) {
            for (const auto& s : scenarios) std::cout << s.name << "\n";
            return 0;
        }

        JSON allReports = JSON::array();
        bool allOk = true;
        bool ranAny = false;

        for (const auto& sc0 : scenarios) {
            if (!cli.scenarioName.empty() && sc0.name != cli.scenarioName) continue;
            ranAny = true;

            Scenario sc = sc0;
            // Environment override knobs for agents.
            if (const char* env = std::getenv("AESTRA_MIN_PEAK")) {
                sc.minPeak = static_cast<float>(std::strtod(env, nullptr));
            }

            if (sc.type == ScenarioType::Project) {
                // Allow overriding project path via env for container runs.
                if (const char* envProj = std::getenv("AESTRA_PROJECT")) {
                    sc.projectPath = envProj;
                }
            }

            const RunMetrics m = runScenario(sc);
            allOk = allOk && m.ok;
            allReports.push(makeReportJson(sc, m));

            if (cli.human) {
                std::cerr
                    << (m.ok ? "OK" : "FAIL")
                    << " name=" << sc.name
                    << " maxPeak=" << m.maxPeak
                    << " rms=" << m.rms
                    << " nan=" << m.nanOrInfCount
                    << " cpuRatio=" << m.cpuRatio
                    << " renderMs=" << m.renderMs;
                if (!m.ok) std::cerr << " reason=" << m.failure;
                std::cerr << "\n";
            }
        }

        if (!ranAny) {
            std::cerr << "AestraHeadless: scenario not found: " << cli.scenarioName << "\n";
            return 2;
        }

        const std::string outJson = allReports.toString(0);
        if (!cli.reportPath.empty()) {
            std::ofstream out(cli.reportPath, std::ios::binary | std::ios::trunc);
            if (!out) {
                std::cerr << "AestraHeadless: failed to write report: " << cli.reportPath << "\n";
                return 2;
            }
            out << outJson;
        } else {
            std::cout << outJson << "\n";
        }

        return allOk ? 0 : 1;
    }

    // Legacy mode: if no scenario file, allow old CLI to run a project.
    if (!cli.projectPath.empty()) {
        return runLegacyProjectOnce(cli);
    }

    printHelp();
    return 2;
}
