// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "HeadlessCheck.h"

#include "../Core/ProjectSerializer.h"
#include "Analysis/AudioAnalysis.h"
#include "AudioEngine.h"
#include "Headless/OfflineProjectRender.h"
#include "Headless/RenderSummary.h"
#include "MiniAudioDecoder.h"
#include "TrackManager.h"

#include "../AestraCore/include/AestraJSON.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

namespace Aestra {

using namespace Aestra::Audio;

namespace {

constexpr int kPass = 0;
constexpr int kFailed = 1;
constexpr int kCouldNotCheck = 2;

std::string absolutePath(const std::string& p) {
    namespace fs = std::filesystem;
    if (p.empty()) {
        return {};
    }
    const fs::path path(p);
    return path.is_absolute() ? path.string() : (fs::current_path() / path).string();
}

JSON stringArray(const std::vector<std::string>& items) {
    JSON arr = JSON::array();
    for (const auto& item : items) {
        arr.push(JSON(item));
    }
    return arr;
}

JSON pluginArray(const std::vector<ProjectSerializer::MissingPlugin>& plugins) {
    JSON arr = JSON::array();
    for (const auto& p : plugins) {
        JSON o = JSON::object();
        o.set("pluginId", JSON(p.pluginId));
        o.set("location", JSON(p.location));
        arr.push(o);
    }
    return arr;
}

JSON summaryJson(const RenderSummary& s, double truePeakDbtp) {
    JSON o = JSON::object();
    o.set("frames", JSON(static_cast<double>(s.frames)));
    o.set("sampleRate", JSON(static_cast<double>(s.sampleRate)));
    o.set("channels", JSON(static_cast<double>(s.channels)));
    o.set("durationSeconds", JSON(s.durationSeconds));
    o.set("peakDbfs", JSON(s.peakDbfs));
    o.set("truePeakDbtp", JSON(truePeakDbtp));
    o.set("rmsDbfs", JSON(s.rmsDbfs));
    JSON dc = JSON::array();
    for (const double d : s.dcOffset) {
        dc.push(JSON(d));
    }
    o.set("dcOffset", dc);
    o.set("clippedSamples", JSON(static_cast<double>(s.clippedSamples)));
    o.set("nonFiniteSamples", JSON(static_cast<double>(s.nonFiniteSamples)));
    o.set("silent", JSON(s.silent));
    o.set("leadingSilenceSeconds", JSON(s.leadingSilenceSeconds));
    o.set("trailingSilenceSeconds", JSON(s.trailingSilenceSeconds));
    o.set("hash", JSON(hashToHex(s.hash64)));
    return o;
}

JSON analysisJson(const AudioAnalysis& a) {
    JSON o = JSON::object();
    o.set("integratedLufs", JSON(a.integratedLufs));
    o.set("maxMomentaryLufs", JSON(a.maxMomentaryLufs));
    o.set("maxShortTermLufs", JSON(a.maxShortTermLufs));
    o.set("loudnessRangeLu", JSON(a.loudnessRangeLu));
    o.set("samplePeakDbfs", JSON(a.samplePeakDbfs));
    o.set("truePeakDbtp", JSON(a.truePeakDbtp));
    o.set("correlation", JSON(a.correlation));
    static constexpr const char* kBandNames[AudioAnalysis::kBandCount] = {"low", "lowMid", "mid", "highMid", "high"};
    JSON bands = JSON::object();
    for (size_t b = 0; b < AudioAnalysis::kBandCount; ++b) {
        JSON band = JSON::object();
        band.set("rmsDbfs", JSON(a.bandRmsDbfs[b]));
        band.set("share", JSON(a.bandShare[b]));
        bands.set(kBandNames[b], band);
    }
    o.set("bands", bands);
    return o;
}

bool readText(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool writeText(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << text << "\n";
    return static_cast<bool>(out);
}

bool readExpectation(const std::string& path, RenderExpectation& out, std::string& error) {
    std::string text;
    if (!readText(path, text)) {
        error = "cannot read " + path;
        return false;
    }
    bool consumedAll = false;
    const JSON root = JSON::parseStrict(text, consumedAll);
    if (!root.isObject() || !consumedAll || !root.has("frames") || !root.has("peakDbfs") || !root.has("rmsDbfs")) {
        error = path + " is not a render golden (want frames, peakDbfs, rmsDbfs, and optionally hash)";
        return false;
    }
    // No "hash": a cross-platform golden, levels only (see RenderExpectation).
    out.hasHash = root.has("hash");
    if (out.hasHash && !hashFromHex(root["hash"].asString(), out.hash64)) {
        error = path + ": hash must be 16 lower-case hex digits";
        return false;
    }
    out.frames = static_cast<uint64_t>(root["frames"].asNumber());
    out.peakDbfs = root["peakDbfs"].asNumber();
    out.rmsDbfs = root["rmsDbfs"].asNumber();
    // Loudness pins (V8-S8) are optional: a golden from before them still checks.
    out.hasLoudness = root.has("integratedLufs") && root.has("truePeakDbtp");
    if (out.hasLoudness) {
        out.integratedLufs = root["integratedLufs"].asNumber();
        out.truePeakDbtp = root["truePeakDbtp"].asNumber();
    }
    return true;
}

JSON expectationJson(const RenderExpectation& e) {
    JSON o = JSON::object();
    o.set("frames", JSON(static_cast<double>(e.frames)));
    o.set("hash", JSON(hashToHex(e.hash64)));
    o.set("peakDbfs", JSON(e.peakDbfs));
    o.set("rmsDbfs", JSON(e.rmsDbfs));
    if (e.hasLoudness) {
        o.set("integratedLufs", JSON(e.integratedLufs));
        o.set("truePeakDbtp", JSON(e.truePeakDbtp));
    }
    return o;
}

// Prints the report, writes it if asked, and returns the exit code.
int finish(JSON& report, const std::vector<std::string>& failures, const HeadlessCheckOptions& options, int code) {
    report.set("ok", JSON(code == kPass));
    report.set("failures", stringArray(failures));
    const std::string text = report.toString(2);
    std::cout << text << "\n";
    if (!options.reportPath.empty() && !writeText(options.reportPath, text)) {
        std::cerr << "AestraHeadless --check: cannot write report " << options.reportPath << "\n";
        return kCouldNotCheck;
    }
    for (const auto& f : failures) {
        std::cerr << "FAIL " << f << "\n";
    }
    return code;
}

} // namespace

int runHeadlessCheck(const HeadlessCheckOptions& options) {
    namespace fs = std::filesystem;
    JSON report = JSON::object();
    std::vector<std::string> failures;
    const std::string projectPath = absolutePath(options.projectPath);
    report.set("project", JSON(projectPath));

    // --- Load, the way the app does ------------------------------------------
    auto trackManager = std::make_shared<TrackManager>();
    trackManager->setOutputSampleRate(static_cast<double>(options.sampleRate));
    trackManager->setInputSampleRate(static_cast<double>(options.sampleRate));
    trackManager->setInputChannelCount(0);
    ProjectSerializer::LoadResult load = ProjectSerializer::load(projectPath, trackManager);

    JSON loadJson = JSON::object();
    loadJson.set("ok", JSON(load.ok));
    loadJson.set("tempo", JSON(load.tempo));
    loadJson.set("missingAssets", stringArray(load.missingAssets));
    loadJson.set("missingPlugins", pluginArray(load.missingPlugins));
    loadJson.set("unreadablePluginState", pluginArray(load.unreadablePluginState));
    if (!load.errorMessage.empty()) {
        loadJson.set("error", JSON(load.errorMessage));
    }
    report.set("load", loadJson);
    if (!load.ok) {
        failures.push_back("project did not load: " + load.errorMessage);
        return finish(report, failures, options, kCouldNotCheck);
    }
    // A missing asset or plugin renders as silence where it should sound, so
    // the render would be checked against the wrong music. Fail unless asked.
    if (!options.allowMissing) {
        if (!load.missingAssets.empty()) {
            failures.push_back(std::to_string(load.missingAssets.size()) + " missing asset(s)");
        }
        if (!load.missingPlugins.empty()) {
            failures.push_back(std::to_string(load.missingPlugins.size()) + " missing plugin(s)");
        }
        if (!load.unreadablePluginState.empty()) {
            failures.push_back(std::to_string(load.unreadablePluginState.size()) +
                               " plugin(s) rejected their saved state");
        }
    }

    // --- Render, the way Export does -----------------------------------------
    // Float_32: no dither, so the samples on disk are the samples rendered and
    // the hash can be bit-exact across runs.
    const bool keep = !options.outPath.empty();
    const std::string wavPath =
        keep ? absolutePath(options.outPath)
             : (fs::temp_directory_path() / ("aestra_check_" + std::to_string(std::hash<std::string>{}(projectPath)) +
                                             ".wav"))
                   .string();
    struct TempCleanup {
        std::string path;
        ~TempCleanup() {
            if (!path.empty()) {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        }
    } cleanup{keep ? std::string() : wavPath};

    AudioEngine engine;
    OfflineRenderRequest request;
    request.outputPath = wavPath;
    request.sampleRate = options.sampleRate;
    request.bitDepth = AudioExporter::BitDepth::Float_32;
    request.tempoBpm = load.tempo;
    const AudioExporter::Result render = renderProjectOffline(engine, *trackManager, request);
    if (!render.ok()) {
        failures.push_back("render failed: " + render.errorMessage);
        return finish(report, failures, options, kCouldNotCheck);
    }
    if (keep) {
        report.set("output", JSON(wavPath));
    }

    // --- Read back and summarise ---------------------------------------------
    std::vector<float> samples;
    uint32_t fileRate = 0;
    uint32_t fileChannels = 0;
    if (!decodeAudioFile(wavPath, samples, fileRate, fileChannels) || fileChannels == 0) {
        failures.push_back("cannot read back the render: " + wavPath);
        return finish(report, failures, options, kCouldNotCheck);
    }
    const RenderSummary summary = summarizeRender(samples, fileChannels, fileRate);
    report.set("summary", summaryJson(summary, render.maxTruePeakdBTP));
    const AudioAnalysis analysis = analyzeAudio(samples, fileChannels, fileRate);
    report.set("analysis", analysisJson(analysis));

    if (summary.nonFiniteSamples > 0) {
        failures.push_back(std::to_string(summary.nonFiniteSamples) + " NaN/Inf sample(s)");
    }
    if (summary.silent && !options.allowSilence) {
        failures.push_back("the render is silent");
    }

    // --- Golden ----------------------------------------------------------------
    if (!options.writeExpectPath.empty()) {
        if (!writeText(options.writeExpectPath, expectationJson(expectationFrom(summary, analysis)).toString(2))) {
            failures.push_back("cannot write golden " + options.writeExpectPath);
            return finish(report, failures, options, kCouldNotCheck);
        }
        report.set("wroteExpect", JSON(options.writeExpectPath));
    }
    if (!options.expectPath.empty()) {
        RenderExpectation expected;
        std::string error;
        if (!readExpectation(options.expectPath, expected, error)) {
            failures.push_back(error);
            return finish(report, failures, options, kCouldNotCheck);
        }
        const auto diffs = compareToExpectation(summary, analysis, expected);
        report.set("expect", JSON(options.expectPath));
        report.set("expectDiffs", stringArray(diffs));
        for (const auto& d : diffs) {
            failures.push_back("golden: " + d);
        }
    }

    return finish(report, failures, options, failures.empty() ? kPass : kFailed);
}

} // namespace Aestra
