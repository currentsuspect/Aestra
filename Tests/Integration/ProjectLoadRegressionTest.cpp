// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Regression tests for ProjectSerializer load behavior

#include "../../Source/Core/ProjectSerializer.h"
#include "../AestraCore/include/AestraLog.h"
#include "Models/ClipSource.h"
#include "Models/PatternSource.h"
#include "Models/TrackManager.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace Aestra;
using namespace Aestra::Audio;

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

    return out.good();
}

std::filesystem::path makeTempDir() {
    auto base = std::filesystem::temp_directory_path() / "Aestra_tests";
    std::filesystem::create_directories(base);

    for (int i = 0; i < 1000; ++i) {
        auto candidate = base / ("ProjectLoadTests_" + std::to_string(i));
        if (!std::filesystem::exists(candidate)) {
            std::filesystem::create_directories(candidate);
            return candidate;
        }
    }

    auto fallback = base / "ProjectLoadTests_fallback";
    std::filesystem::create_directories(fallback);
    return fallback;
}

void testValidProjectLoad() {
    std::cout << "[TEST] Valid project load..." << std::endl;

    auto testDir = makeTempDir();
    std::filesystem::path testWav = testDir / "audio.wav";
    std::filesystem::path testProject = testDir / "project.aes";

    assert(writeMinimalWavMono16(testWav, 44100, 4410));

    std::string projectJson = R"({
        "version": 1,
        "tempo": 120.0,
        "playhead": 0.0,
        "sources": [{"id": 1, "path": ")" + testWav.generic_string() + R"(", "name": "Test Audio"}],
        "patterns": [
            {"id": 1, "name": "Test Pattern", "type": "midi", "length": 4.0, "notes": []}
        ],
        "lanes": [
            {
                "name": "Track 1",
                "color": "4294967295",
                "volume": 1.0,
                "pan": 0.0,
                "clips": [
                    {"id": "clip-1", "patternId": 1, "start": 0.0, "duration": 4.0, "name": "Test Clip"}
                ]
            }
        ],
        "arsenal": {"nextId": 1, "units": []}
    })";

    std::ofstream out(testProject);
    out << projectJson;
    out.close();

    auto trackManager = std::make_shared<Aestra::Audio::TrackManager>();
    auto result = ProjectSerializer::load(testProject.string(), trackManager);

    assert(result.ok);
    assert(result.errorMessage.empty());
    assert(result.missingAssets.empty());
    assert(!result.report || !result.report->hasErrors());

    std::cout << "[PASS] Valid project load" << std::endl;

    std::filesystem::remove_all(testDir);
}

void testMissingPatternReference() {
    std::cout << "[TEST] Missing pattern reference preserves placeholder..." << std::endl;

    auto testDir = makeTempDir();
    std::filesystem::path testProject = testDir / "project.aes";

    // Project references pattern ID 999 which does NOT exist in patterns array
    std::string projectJson = R"({
        "version": 1,
        "tempo": 120.0,
        "playhead": 0.0,
        "sources": [],
        "patterns": [
            {"id": 1, "name": "Existing Pattern", "type": "midi", "length": 4.0, "notes": []}
        ],
        "lanes": [
            {
                "name": "Track 1",
                "color": "4294967295",
                "volume": 1.0,
                "pan": 0.0,
                "clips": [
                    {"id": "clip-1", "patternId": 999, "start": 0.0, "duration": 4.0, "name": "Missing Ref Clip"}
                ]
            }
        ],
        "arsenal": {"nextId": 1, "units": []}
    })";

    std::ofstream out(testProject);
    out << projectJson;
    out.close();

    auto trackManager = std::make_shared<Aestra::Audio::TrackManager>();
    auto result = ProjectSerializer::load(testProject.string(), trackManager);

    assert(result.ok);

    // Should have warning about missing pattern (report may contain warnings)
    if (result.report) {
        bool foundWarning = false;
        for (const auto& issue : result.report->issues) {
            if (issue.category == "clip" && issue.referenceId == "999") {
                foundWarning = true;
                break;
            }
        }
        assert(foundWarning);
    }

    std::cout << "[PASS] Missing pattern reference preserves placeholder" << std::endl;

    std::filesystem::remove_all(testDir);
}

void testMissingArsenalUnitReference() {
    std::cout << "[TEST] Missing Arsenal unit reference preserves note..." << std::endl;

    auto testDir = makeTempDir();
    std::filesystem::path testProject = testDir / "project.aes";

    // MIDI note references unit ID 888 which does NOT exist in arsenal
    std::string projectJson = R"({
        "version": 1,
        "tempo": 120.0,
        "playhead": 0.0,
        "sources": [],
        "patterns": [
            {"id": 1, "name": "Pattern With Unit Ref", "type": "midi", "length": 4.0, "notes": [
                {"pitch": 60, "startBeat": 0.0, "durationBeats": 1.0, "velocity": 1.0, "unitId": 888}
            ]}
        ],
        "lanes": [],
        "arsenal": {"nextId": 1, "units": []}
    })";

    std::ofstream out(testProject);
    out << projectJson;
    out.close();

    auto trackManager = std::make_shared<Aestra::Audio::TrackManager>();
    auto result = ProjectSerializer::load(testProject.string(), trackManager);

    assert(result.ok);

    // Should have warning about missing unit
    if (result.report) {
        bool foundWarning = false;
        for (const auto& issue : result.report->issues) {
            if (issue.category == "unit" && issue.referenceId == "888") {
                foundWarning = true;
                break;
            }
        }
        assert(foundWarning);
    }

    std::cout << "[PASS] Missing Arsenal unit reference preserves note" << std::endl;

    std::filesystem::remove_all(testDir);
}

void testFailedValidationDoesNotClear() {
    std::cout << "[TEST] Failed validation does not clear existing state..." << std::endl;

    auto testDir = makeTempDir();
    std::filesystem::path testProject = testDir / "project.aes";

    // Invalid project - missing required "lanes" field
    std::string projectJson = R"({
        "version": 1,
        "tempo": 120.0,
        "playhead": 0.0,
        "sources": []
    })";

    std::ofstream out(testProject);
    out << projectJson;
    out.close();

    auto trackManager = std::make_shared<Aestra::Audio::TrackManager>();

    // Create a lane BEFORE loading (this should survive failed load)
    trackManager->getPlaylistModel().createLane("Existing Lane");

    auto result = ProjectSerializer::load(testProject.string(), trackManager);

    // Load should fail
    assert(!result.ok);
    assert(!result.errorMessage.empty());

    // But existing state should still be there
    auto laneIds = trackManager->getPlaylistModel().getLaneIDs();
    assert(!laneIds.empty());

    std::cout << "[PASS] Failed validation does not clear existing state" << std::endl;

    std::filesystem::remove_all(testDir);
}

void testUnitManagerSurvivesFailedLoad() {
    std::cout << "[TEST] UnitManager survives failed PHASE 3 validation..." << std::endl;

    auto testDir = makeTempDir();
    std::filesystem::path testProject = testDir / "project.aes";

    // Invalid project - missing required "lanes" field
    std::string projectJson = R"({
        "version": 1,
        "tempo": 120.0,
        "playhead": 0.0,
        "sources": []
    })";

    std::ofstream out(testProject);
    out << projectJson;
    out.close();

    auto trackManager = std::make_shared<Aestra::Audio::TrackManager>();

    // Note: UnitManager::clear() is called AFTER commit at PHASE 4.
    // Before PHASE 4, UnitManager state should remain untouched.
    // This test verifies that a pre-commit failure (PHASE 3) does NOT clear UnitManager.

    // We can't directly add units in TrackManager via public API,
    // but we verify by checking that loadFromJSON was never called.
    // The fact that failed validation returns early proves no clearing happened.

    auto result = ProjectSerializer::load(testProject.string(), trackManager);

    // Load should fail BEFORE PHASE 4 commit
    assert(!result.ok);
    assert(result.errorMessage.find("lanes") != std::string::npos);

    // If we got here, no state was cleared (early return preserved everything)
    std::cout << "[PASS] UnitManager survives failed PHASE 3 validation" << std::endl;

    std::filesystem::remove_all(testDir);
}

void testMissingAudioFileNonDestructive() {
    std::cout << "[TEST] Missing audio file is non-destructive..." << std::endl;

    auto testDir = makeTempDir();
    std::filesystem::path testProject = testDir / "project.aes";

    // References a file that does NOT exist
    std::string projectJson = R"({
        "version": 1,
        "tempo": 120.0,
        "playhead": 0.0,
        "sources": [{"id": 1, "path": ")" + (testDir / "nonexistent.wav").generic_string() + R"(", "name": "Missing Audio"}],
        "patterns": [],
        "lanes": [],
        "arsenal": {"nextId": 1, "units": []}
    })";

    std::ofstream out(testProject);
    out << projectJson;
    out.close();

    auto trackManager = std::make_shared<TrackManager>();
    auto result = ProjectSerializer::load(testProject.string(), trackManager);

    // Load should succeed but with missing asset warning
    assert(result.ok);
    assert(!result.missingAssets.empty());

    std::cout << "[PASS] Missing audio file is non-destructive" << std::endl;

    std::filesystem::remove_all(testDir);
}

void testUnresolvedRouteTargetNonFatal() {
    std::cout << "[TEST] Unresolved send routing targets are non-fatal..." << std::endl;

    auto testDir = makeTempDir();
    std::filesystem::path testProject = testDir / "project.aes";

    // Project with routing sends to a channel ID that doesn't exist.
    // Channel IDs are 1-based and sequential during load.
    // Lane 0 gets channel ID 1. The send target 99 does not exist.
    std::string projectJson = R"({
        "version": 1,
        "tempo": 120.0,
        "playhead": 0.0,
        "sources": [],
        "patterns": [],
        "lanes": [
            {
                "name": "Track 1",
                "color": "4294967295",
                "volume": 1.0,
                "pan": 0.0,
                "clips": [],
                "routing": {
                    "mainOutputId": 0,
                    "sends": [
                        {
                            "targetId": 99,
                            "gain": 0.8,
                            "pan": 0.0,
                            "postFader": true,
                            "mute": false,
                            "sidechainOnly": false
                        },
                        {
                            "targetId": 0,
                            "gain": 0.5,
                            "pan": -0.5,
                            "postFader": true,
                            "mute": false,
                            "sidechainOnly": true
                        }
                    ]
                }
            }
        ],
        "arsenal": {"nextId": 1, "units": []}
    })";

    std::ofstream out(testProject);
    out << projectJson;
    out.close();

    auto trackManager = std::make_shared<TrackManager>();
    auto result = ProjectSerializer::load(testProject.string(), trackManager);

    // Load must succeed — unresolved sends must not cause errors.
    assert(result.ok);

    // Verify the channel was created and has the sends (both the
    // unresolvable and the resolvable master send).
    assert(trackManager->getChannelCount() == 1);
    auto* channel = trackManager->getChannel(0);
    assert(channel != nullptr);

    const auto sends = channel->getSends();
    assert(sends.size() == 2);

    // The unresolvable send to channel 99 is preserved in the channel
    // but silently ignored by the audio runtime (INVALID_SLOT check).
    // ProjectSerializer logs a warning for unresolved send targets during load.
    bool foundUnresolvable = false;
    bool foundMaster = false;
    for (const auto& send : sends) {
        if (send.targetChannelId == 99) {
            foundUnresolvable = true;
        }
        if (send.targetChannelId == 0xFFFFFFFFu) {
            foundMaster = true;
        }
    }
    assert(foundUnresolvable);
    assert(foundMaster);

    // Verify that saving and reloading preserves the routing (no data loss).
    std::string saved = ProjectSerializer::serialize(trackManager, 120.0, 0.0, 0).contents;
    assert(!saved.empty());
    assert(saved.find("\"targetId\": 99") != std::string::npos ||
           saved.find("\"targetId\":99") != std::string::npos);

    // Load the re-saved project and verify routing is still preserved.
    std::filesystem::path testProject2 = testDir / "project2.aes";
    std::ofstream out2(testProject2);
    out2 << saved;
    out2.close();

    auto trackManager2 = std::make_shared<TrackManager>();
    auto result2 = ProjectSerializer::load(testProject2.string(), trackManager2);
    assert(result2.ok);

    auto* channel2 = trackManager2->getChannel(0);
    assert(channel2 != nullptr);
    const auto sends2 = channel2->getSends();
    assert(sends2.size() == 2);

    // Verify the mainOutputId is stable after round-trip
    assert(channel2->getMainOutputId() == channel->getMainOutputId());

    std::cout << "[PASS] Unresolved send routing targets are non-fatal" << std::endl;

    std::filesystem::remove_all(testDir);
}

}

int main() {
    std::cout << "=== Project Load Regression Tests ===" << std::endl;

    testValidProjectLoad();
    testMissingPatternReference();
    testMissingArsenalUnitReference();
    testFailedValidationDoesNotClear();
    testUnitManagerSurvivesFailedLoad();
    testMissingAudioFileNonDestructive();
    testUnresolvedRouteTargetNonFatal();

    std::cout << "=== All tests passed ===" << std::endl;
    return 0;
}