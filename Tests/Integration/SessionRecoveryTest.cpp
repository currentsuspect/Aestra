// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "../../Source/Core/ProjectSerializer.h"
#include "../AestraCore/include/AestraLog.h"
#include "../Support/TestTempDirectory.h"
#include "Models/ClipSource.h"
#include "Models/PatternSource.h"
#include "Models/TrackManager.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

bool writeMinimalWavMono16(const std::filesystem::path& path, int sampleRate, int numSamples) {
    if (sampleRate <= 0 || numSamples <= 0)
        return false;

    const int numChannels = 1;
    const int bitsPerSample = 16;
    const int bytesPerSample = bitsPerSample / 8;
    const int blockAlign = numChannels * bytesPerSample;
    const int byteRate = sampleRate * blockAlign;
    const std::uint32_t dataSize = static_cast<std::uint32_t>(numSamples * blockAlign);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;

    auto writeU32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto writeU16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };

    out.write("RIFF", 4);
    writeU32(36u + dataSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    writeU32(16);
    writeU16(1);
    writeU16(numChannels);
    writeU32(static_cast<std::uint32_t>(sampleRate));
    writeU32(static_cast<std::uint32_t>(byteRate));
    writeU16(static_cast<std::uint16_t>(blockAlign));
    writeU16(static_cast<std::uint16_t>(bitsPerSample));
    out.write("data", 4);
    writeU32(dataSize);

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

// Mimics AestraApp::writeCrashFlag — always written at session start
time_t writeCrashFlag(const std::filesystem::path& path) {
    auto now = std::chrono::system_clock::now();
    auto tp = now.time_since_epoch().count();
    std::ofstream out(path, std::ios::trunc);
    if (out) {
        out << tp;
        out.close();
    }
    return std::chrono::system_clock::to_time_t(now);
}

// Mimics AestraApp::clearCrashFlag — cleared only on clean shutdown
void clearCrashFlag(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        std::filesystem::remove(path, ec);
    }
}

// Mimics AestraApp::isCrashedSession
bool isCrashedSession(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

// Mimics RecoveryDialog::detectAutosave — just checks file existence
bool detectAutosave(const std::filesystem::path& autosavePath, const std::filesystem::path& recoveryMarkerPath,
                    const std::string& expectedSessionToken, std::string& outTimestamp) {
    std::error_code ec;
    if (!std::filesystem::exists(autosavePath, ec) || ec) {
        return false;
    }
    if (expectedSessionToken.empty()) {
        return false;
    }
    std::ifstream marker(recoveryMarkerPath, std::ios::binary);
    if (!marker.good()) {
        outTimestamp = "detected";
        return true;
    }
    std::string markerToken;
    std::getline(marker, markerToken);
    outTimestamp = "detected";
    return true;
}

} // namespace

int main() {
    using namespace Aestra::Audio;

    const Aestra::Tests::ScopedTempDirectory tempDirScope{"SessionRecovery"};
    const auto& tempDir = tempDirScope.path();
    const auto wavPath = tempDir / "test.wav";
    const auto autosavePath = tempDir / "autosave.aes";
    const auto recoveryMarkerPath = tempDir / "autosave.aes.recovery";
    const auto crashFlagPath = tempDir / "crash_flag";

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

    PlaylistLaneID lane1Id = playlist1.createLane("Lane 1");
    require(lane1Id.isValid(), "Failed to create lane 1");
    auto* channel1 = tm1->addChannel("Lane 1");
    require(channel1 != nullptr, "Failed to create channel 1");

    PlaylistLaneID lane2Id = playlist1.createLane("Lane 2");
    require(lane2Id.isValid(), "Failed to create lane 2");
    auto* channel2Src = tm1->addChannel("Lane 2");
    require(channel2Src != nullptr, "Failed to create channel 2");

    if (auto* lane = playlist1.getLane(lane1Id)) {
        lane->volume = 0.75f;
        lane->pan = -0.25f;
    }
    channel1->setVolume(0.75f);
    channel1->setPan(-0.25f);
    channel1->setMainOutputId(channel2Src->getChannelId());

    ClipInstanceID clipId = playlist1.addClipFromPattern(lane1Id, patId, 0.0, 4.0);
    require(clipId.isValid(), "Failed to add clip from pattern");

    // --- Simulate crash flag write at session start (AestraApp::initialize)
    time_t crashTime = writeCrashFlag(crashFlagPath);
    std::ifstream crashFlagIn(crashFlagPath, std::ios::binary);
    std::string recoverySessionToken;
    std::getline(crashFlagIn, recoverySessionToken);
    crashFlagIn.close();
    require(isCrashedSession(crashFlagPath), "Crash flag should exist after write");
    require(!recoverySessionToken.empty(), "Crash flag should contain recovery session token");
    std::cout << "[INFO] Crash flag written: " << crashFlagPath.string() << "\n";

    // --- Simulate autosave (what AutosaveManager::performAutosave does)
    {
        std::cout << "[INFO] Simulating autosave (serialize + atomic write)...\n";
        auto ser = ProjectSerializer::serialize(tm1, 128.0, 1.234, 0);
        require(ser.ok, "ProjectSerializer::serialize failed");
        require(!ser.contents.empty(), "ProjectSerializer::serialize produced empty output");
        require(ProjectSerializer::writeAtomically(autosavePath.string(), ser.contents),
                "ProjectSerializer::writeAtomically failed for autosave");
        std::ofstream marker(recoveryMarkerPath, std::ios::binary | std::ios::trunc);
        marker << recoverySessionToken << "\n";
        std::cout << "[INFO] Autosave written: " << autosavePath.string() << "\n";
    }

    {
        const auto plantedPath = tempDir / "planted_autosave.aes";
        std::ofstream planted(plantedPath, std::ios::binary | std::ios::trunc);
        planted << "planted";
        std::string timestamp;
        require(detectAutosave(plantedPath, recoveryMarkerPath, "wrong-session", timestamp),
                "Recovery should fall back to legacy autosave detection on marker mismatch");
        require(!detectAutosave(plantedPath, recoveryMarkerPath, "", timestamp),
                "Recovery must reject autosave when current session token is empty");
    }

    // --- Recovery: detect autosave
    {
        std::string timestamp;
        bool detected = detectAutosave(autosavePath, recoveryMarkerPath, recoverySessionToken, timestamp);
        require(detected, "Recovery should detect autosave");
        std::cout << "[INFO] Autosave detected for recovery.\n";
    }

    // --- Load the autosave into a fresh TrackManager
    auto tm2 = std::make_shared<TrackManager>();
    tm2->getPlaylistModel().setPatternManager(&tm2->getPatternManager());

    std::cout << "[INFO] Loading autosave project...\n";
    ProjectSerializer::LoadResult autosaveLoadResult = ProjectSerializer::load(autosavePath.string(), tm2);
    require(autosaveLoadResult.ok, "ProjectSerializer::load failed for autosave path");
    std::cout << "[INFO] Autosave project loaded.\n";

    // --- Validate recovered data
    require(std::abs(autosaveLoadResult.tempo - 128.0) < 1e-9, "Autosave tempo did not roundtrip");
    require(std::abs(autosaveLoadResult.playhead - 1.234) < 1e-9, "Autosave playhead did not roundtrip");
    require(tm2->getChannelCount() == 2, "Autosave channel count mismatch");

    const auto* recoveredChannel1 = tm2->getChannel(0);
    const auto* recoveredChannel2 = tm2->getChannel(1);
    require(recoveredChannel1 != nullptr && recoveredChannel2 != nullptr, "Recovered channels missing");
    require(recoveredChannel1->getMainOutputId() == recoveredChannel2->getChannelId(),
            "Recovered main output destination mismatch");
    require(std::abs(recoveredChannel1->getVolume() - 0.75f) < 1e-6f, "Recovered channel 1 volume mismatch");
    require(std::abs(recoveredChannel1->getPan() - (-0.25f)) < 1e-6f, "Recovered channel 1 pan mismatch");

    // --- Clean up
    clearCrashFlag(crashFlagPath);
    require(!isCrashedSession(crashFlagPath), "Crash flag should be cleared");

    std::cout << "[PASS] SessionRecoveryTest\n";
    return 0;
}
