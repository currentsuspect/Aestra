// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// AestraHeadless --check, end to end, on a project built here: a real project
// file (ProjectSerializer::save), loaded the way the app loads it, rendered
// through AudioExporter the way Export renders it, read back and judged.
//
//   1. a project with one audio clip of a known tone passes, and is not silent
//   2. rendering it again reproduces the golden it just wrote, hash included:
//      the render is deterministic on this machine
//   3. a golden one dB off fails, and says by how much
//   4. the same project with its sample deleted fails as a missing asset
//
// What it does NOT pin: absolute levels across platforms. Those depend on the
// master stage and on float math that differs between compilers, so a
// cross-platform golden omits the hash and pins levels only (RenderExpectation).

#include "../../Source/App/HeadlessCheck.h"
#include "../../Source/Core/ProjectSerializer.h"
#include "../Support/TestTempDirectory.h"
#include "Models/ClipSource.h"
#include "Models/PatternSource.h"
#include "Models/TrackManager.h"

#include "../AestraCore/include/AestraJSON.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

int g_failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        ++g_failures;
    }
}

constexpr int kRate = 48000;

// 16-bit mono WAV of a 440 Hz sine at half scale: a sound whose level is known.
bool writeToneWav(const std::filesystem::path& path, int numSamples) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t dataSize = static_cast<uint32_t>(numSamples) * 2u;
    out.write("RIFF", 4);
    u32(36u + dataSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    u32(16);
    u16(1);
    u16(1);
    u32(kRate);
    u32(kRate * 2);
    u16(2);
    u16(16);
    out.write("data", 4);
    u32(dataSize);
    for (int i = 0; i < numSamples; ++i) {
        const double x = 0.5 * std::sin(2.0 * 3.14159265358979323846 * 440.0 * i / kRate);
        const auto s = static_cast<int16_t>(std::lround(x * 32767.0));
        out.write(reinterpret_cast<const char*>(&s), sizeof(s));
    }
    return static_cast<bool>(out);
}

// One lane, one audio clip of the whole tone, saved as a real project file.
bool writeProject(const std::filesystem::path& wav, const std::filesystem::path& project) {
    using namespace Aestra::Audio;
    auto tm = std::make_shared<TrackManager>();
    tm->getPlaylistModel().setPatternManager(&tm->getPatternManager());
    ClipSourceID src = tm->getSourceManager().getOrCreateSource(wav.string());
    if (src.value == 0)
        return false;
    AudioSlicePayload payload;
    payload.audioSourceId = src;
    payload.slices.push_back({0.0, static_cast<double>(kRate)});
    PatternID pat = tm->getPatternManager().createAudioPattern("Tone", 2.0, payload);
    if (pat.value == 0)
        return false;
    auto& playlist = tm->getPlaylistModel();
    playlist.setBPM(120.0);
    PlaylistLaneID lane = playlist.createLane("Tone");
    if (!lane.isValid() || tm->addChannel("Tone") == nullptr)
        return false;
    ClipInstanceID clip = playlist.addClipFromPattern(lane, pat, 0.0, 2.0);
    if (!clip.isValid())
        return false;
    return ProjectSerializer::save(project.string(), tm, 120.0, 0.0);
}

Aestra::JSON readJson(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return Aestra::JSON::parse(ss.str());
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const Aestra::Tests::ScopedTempDirectory dir{"HeadlessCheckE2E"};
    const fs::path wav = dir.path() / "tone.wav";
    const fs::path project = dir.path() / "tone.aes";
    const fs::path golden = dir.path() / "tone.golden.json";
    const fs::path report = dir.path() / "report.json";

    check(writeToneWav(wav, kRate), "write the tone sample");
    check(writeProject(wav, project), "build and save the tone project");
    if (g_failures > 0)
        return 1;

    // 1. Passes, writes a golden, and the summary describes a real sound.
    Aestra::HeadlessCheckOptions first;
    first.projectPath = project.string();
    first.writeExpectPath = golden.string();
    first.reportPath = report.string();
    check(Aestra::runHeadlessCheck(first) == 0, "the tone project passes its check");
    const Aestra::JSON r = readJson(report);
    const auto& s = r["summary"];
    check(!s["silent"].asBool(), "the render is not silent");
    check(s["nonFiniteSamples"].asNumber() == 0.0, "no NaN/Inf in the render");
    check(s["peakDbfs"].asNumber() < 0.0 && s["peakDbfs"].asNumber() > -30.0,
          "a half-scale tone renders at a plausible level (-30..0 dBFS)");
    check(s["durationSeconds"].asNumber() >= 1.0, "the render covers the clip");

    // 2. Determinism: a second render reproduces the golden, hash included.
    Aestra::HeadlessCheckOptions again;
    again.projectPath = project.string();
    again.expectPath = golden.string();
    check(Aestra::runHeadlessCheck(again) == 0, "a second render matches the golden bit for bit");

    // 3. A golden one dB off fails.
    {
        Aestra::JSON g = readJson(golden);
        g.set("peakDbfs", Aestra::JSON(g["peakDbfs"].asNumber() + 1.0));
        std::ofstream(golden, std::ios::trunc) << g.toString(2);
    }
    check(Aestra::runHeadlessCheck(again) == 1, "a golden 1 dB off fails the check");

    // 4. A deleted sample is a missing asset, and a failure.
    fs::remove(wav);
    Aestra::HeadlessCheckOptions missing;
    missing.projectPath = project.string();
    missing.allowSilence = true; // the missing sample also silences it; isolate the asset failure
    missing.reportPath = report.string();
    check(Aestra::runHeadlessCheck(missing) == 1, "a missing sample fails the check");
    const Aestra::JSON m = readJson(report);
    check(m["load"]["missingAssets"].asArray().size() == 1, "and is reported as one missing asset");

    if (g_failures == 0) {
        std::cout << "HeadlessCheckEndToEndTest: all checks passed\n";
        return 0;
    }
    std::cerr << "HeadlessCheckEndToEndTest: " << g_failures << " check(s) failed\n";
    return 1;
}
