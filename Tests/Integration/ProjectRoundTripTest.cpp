// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "../../Source/Core/ProjectSerializer.h"

#include "Models/TrackManager.h"
#include "Models/ClipSource.h"
#include "Models/PatternSource.h"

#include "../AestraCore/include/AestraLog.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

std::filesystem::path makeTempDir() {
    auto base = std::filesystem::temp_directory_path() / "nomad_tests";
    std::filesystem::create_directories(base);

    // Ensure uniqueness without relying on high-resolution clocks.
    for (int i = 0; i < 1000; ++i) {
        auto candidate = base / ("ProjectRoundTrip_" + std::to_string(i));
        if (!std::filesystem::exists(candidate)) {
            std::filesystem::create_directories(candidate);
            return candidate;
        }
    }

    // Fallback (should not happen).
    auto fallback = base / "ProjectRoundTrip_fallback";
    std::filesystem::create_directories(fallback);
    return fallback;
}

// Minimal PCM 16-bit mono WAV writer (enough to satisfy SourceManager file loading).
bool writeMinimalWavMono16(const std::filesystem::path& path, int sampleRate, int numSamples) {
    if (sampleRate <= 0 || numSamples <= 0) return false;

    const int numChannels = 1;
    const int bitsPerSample = 16;
    const int bytesPerSample = bitsPerSample / 8;
    const int blockAlign = numChannels * bytesPerSample;
    const int byteRate = sampleRate * blockAlign;
    const std::uint32_t dataSize = static_cast<std::uint32_t>(numSamples * blockAlign);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;

    auto writeU32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto writeU16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };

    // RIFF header
    out.write("RIFF", 4);
    writeU32(36u + dataSize);
    out.write("WAVE", 4);

    // fmt chunk
    out.write("fmt ", 4);
    writeU32(16);                 // PCM
    writeU16(1);                  // audio format = PCM
    writeU16(numChannels);
    writeU32(static_cast<std::uint32_t>(sampleRate));
    writeU32(static_cast<std::uint32_t>(byteRate));
    writeU16(static_cast<std::uint16_t>(blockAlign));
    writeU16(static_cast<std::uint16_t>(bitsPerSample));

    // data chunk
    out.write("data", 4);
    writeU32(dataSize);

    // Samples: simple ramp (non-zero)
    for (int i = 0; i < numSamples; ++i) {
        std::int16_t s = static_cast<std::int16_t>((i % 200) * 100);
        out.write(reinterpret_cast<const char*>(&s), sizeof(s));
    }

    return true;
}

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}

} // namespace

int main() {
    using namespace Aestra::Audio;

    const auto tempDir = makeTempDir();
    const auto wavPath = tempDir / "test.wav";
    const auto projectPath = tempDir / "project.aes";
    const auto autosavePath = tempDir / "autosave.aes";

    std::cout << "[INFO] TempDir: " << tempDir.string() << "\n";
    std::cout << "[INFO] Writing WAV...\n";
    require(writeMinimalWavMono16(wavPath, 48000, 4800), "Failed to write test WAV");
    std::cout << "[INFO] WAV written: " << wavPath.string() << "\n";

    // --- Arrange: create a tiny project state
    auto tm1 = std::make_shared<TrackManager>();
    tm1->getPlaylistModel().setPatternManager(&tm1->getPatternManager());

    auto& sm1 = tm1->getSourceManager();
    ClipSourceID srcId = sm1.getOrCreateSource(wavPath.string());
    require(srcId.value != 0, "SourceManager failed to create/load source");

    AudioSlicePayload payload;
    payload.audioSourceId = srcId;
    payload.slices.push_back({0.0, 2400.0});

    PatternID patId = tm1->getPatternManager().createAudioPattern("TestPattern", 4.0, payload);
    require(patId.value != 0, "PatternManager failed to create audio pattern");

    auto& playlist1 = tm1->getPlaylistModel();
    playlist1.setBPM(128.0);

    PlaylistLaneID laneId = playlist1.createLane("Lane 1");
    require(laneId.isValid(), "Failed to create lane");

    if (auto* lane = playlist1.getLane(laneId)) {
        lane->volume = 0.75f;
        lane->pan = -0.25f;
        lane->muted = false;
        lane->solo = false;
    }

    ClipInstanceID clipId = playlist1.addClipFromPattern(laneId, patId, 0.0, 4.0);
    require(clipId.isValid(), "Failed to add clip from pattern");

    if (auto* clip = playlist1.getClip(clipId)) {
        clip->edits.gainLinear = 0.9f;
        clip->edits.pan = 0.1f;
        clip->edits.muted = false;
        clip->edits.playbackRate = 1.0f;
        clip->edits.fadeInBeats = 0.0;
        clip->edits.fadeOutBeats = 0.0;
        clip->edits.sourceStart = 0.0;
    }

    // --- Autosave-style path: serialize compact (indent=0) + atomic write
    {
        std::cout << "[INFO] Serializing (indent=0) + atomic write (autosave)...\n";
        auto ser = ProjectSerializer::serialize(tm1, 128.0, 1.234, 0);
        require(ser.ok, "ProjectSerializer::serialize failed");
        require(!ser.contents.empty(), "ProjectSerializer::serialize produced empty output");
        require(ProjectSerializer::writeAtomically(autosavePath.string(), ser.contents), "ProjectSerializer::writeAtomically failed");
        std::cout << "[INFO] Autosave written: " << autosavePath.string() << " (" << ser.contents.size() << " bytes)\n";
    }

    // --- Canonical save path: pretty-printed save (indent=2) + atomic write internally
    std::cout << "[INFO] Saving canonical project...\n";
    require(ProjectSerializer::save(projectPath.string(), tm1, 128.0, 1.234), "ProjectSerializer::save failed");
    std::cout << "[INFO] Canonical project saved: " << projectPath.string() << "\n";

    // --- Act: load into a fresh TrackManager (avoids cross-contamination; current loader clears only some subsystems)
    auto tm2 = std::make_shared<TrackManager>();
    tm2->getPlaylistModel().setPatternManager(&tm2->getPatternManager());

    std::cout << "[INFO] Loading canonical project...\n";
    ProjectSerializer::LoadResult loadResult = ProjectSerializer::load(projectPath.string(), tm2);
    require(loadResult.ok, "ProjectSerializer::load failed");
    std::cout << "[INFO] Canonical project loaded.\n";

    // --- Assert: verify key invariants
    require(std::abs(loadResult.tempo - 128.0) < 1e-9, "Tempo did not roundtrip");
    require(std::abs(loadResult.playhead - 1.234) < 1e-9, "Playhead did not roundtrip");

    auto& playlist2 = tm2->getPlaylistModel();
    require(playlist2.getLaneCount() == 1, "Lane count mismatch after load");

    PlaylistLaneID lane2Id = playlist2.getLaneId(0);
    const auto* lane2 = playlist2.getLane(lane2Id);
    require(lane2 != nullptr, "Loaded lane missing");
    require(lane2->name == "Lane 1", "Lane name mismatch after load");
    require(std::abs(lane2->volume - 0.75f) < 1e-6f, "Lane volume mismatch after load");
    require(std::abs(lane2->pan - (-0.25f)) < 1e-6f, "Lane pan mismatch after load");

    require(lane2->clips.size() == 1, "Clip count mismatch after load");
    const auto& loadedClip = lane2->clips[0];
    require(std::abs(loadedClip.startBeat - 0.0) < 1e-9, "Clip start mismatch after load");
    require(std::abs(loadedClip.durationBeats - 4.0) < 1e-9, "Clip duration mismatch after load");
    require(std::abs(loadedClip.edits.gainLinear - 0.9f) < 1e-6f, "Clip gain mismatch after load");

    auto patterns2 = tm2->getPatternManager().getAllPatterns();
    require(patterns2.size() == 1, "Pattern count mismatch after load");
    require(patterns2[0] != nullptr && patterns2[0]->name == "TestPattern", "Pattern name mismatch after load");

    auto srcIds2 = tm2->getSourceManager().getAllSourceIDs();
    require(srcIds2.size() == 1, "Source count mismatch after load");

    // --- Act/Assert (autosave-style): load compact autosave and validate same invariants
    auto tm3 = std::make_shared<TrackManager>();
    tm3->getPlaylistModel().setPatternManager(&tm3->getPatternManager());

    std::cout << "[INFO] Loading autosave project...\n";
    ProjectSerializer::LoadResult autosaveLoadResult = ProjectSerializer::load(autosavePath.string(), tm3);
    require(autosaveLoadResult.ok, "ProjectSerializer::load failed for autosave path");
    std::cout << "[INFO] Autosave project loaded.\n";
    require(std::abs(autosaveLoadResult.tempo - 128.0) < 1e-9, "Autosave tempo did not roundtrip");
    require(std::abs(autosaveLoadResult.playhead - 1.234) < 1e-9, "Autosave playhead did not roundtrip");

    std::cout << "[PASS] ProjectRoundTripTest\n";
    return 0;
}
