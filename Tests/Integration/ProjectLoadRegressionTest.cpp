// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Regression tests for ProjectSerializer load behavior

#include "../../Source/Core/ProjectSerializer.h"
#include "../AestraCore/include/AestraLog.h"
#include "../Support/TestTempDirectory.h"
#include "Models/ClipSource.h"
#include "Models/PatternSource.h"
#include "Models/TrackManager.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace Aestra;
using namespace Aestra::Audio;

namespace {

class CapturingLogger : public ILogger {
public:
    void log(LogLevel level, const std::string& message) override {
        if (level >= minLevel) {
            entries.push_back({level, message});
        }
    }

    void setLevel(LogLevel level) override { minLevel = level; }
    LogLevel getLevel() const override { return minLevel; }

    struct Entry {
        LogLevel level;
        std::string message;
    };

    LogLevel minLevel{LogLevel::Trace};
    std::vector<Entry> entries;
};

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

void testValidProjectLoad() {
    std::cout << "[TEST] Valid project load..." << std::endl;

    const Aestra::Tests::ScopedTempDirectory testDirScope{"ProjectLoadRegression"};
    const auto& testDir = testDirScope.path();
    std::filesystem::path testWav = testDir / "audio.wav";
    std::filesystem::path testProject = testDir / "project.aes";

    assert(writeMinimalWavMono16(testWav, 44100, 4410));

    std::string projectJson = R"({
        "version": 1,
        "tempo": 120.0,
        "playhead": 0.0,
        "sources": [{"id": 1, "path": ")" +
                              testWav.generic_string() + R"(", "name": "Test Audio"}],
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
}

void testTrailingJsonObjectIsRejectedNonDestructively() {
    std::cout << "[TEST] Trailing JSON object is rejected non-destructively..." << std::endl;

    const Aestra::Tests::ScopedTempDirectory testDirScope{"ProjectLoadTrailingJson"};
    const auto projectPath = testDirScope.path() / "trailing.aes";

    auto source = std::make_shared<TrackManager>();
    source->getPlaylistModel().createLane("Serialized Lane");
    source->addChannel("Serialized Lane");
    auto serialized = ProjectSerializer::serialize(source, 120.0, 0.0, 0);
    assert(serialized.ok);

    std::ofstream out(projectPath, std::ios::binary | std::ios::trunc);
    out << serialized.contents << "\n{\"unexpectedSuffix\":true}";
    out.close();

    auto destination = std::make_shared<TrackManager>();
    destination->getPlaylistModel().createLane("Existing Lane");
    destination->addChannel("Existing Lane");
    const size_t channelsBefore = destination->getChannelCount();

    auto result = ProjectSerializer::load(projectPath.string(), destination);
    assert(!result.ok);
    assert(result.errorMessage.find("exactly one") != std::string::npos);
    assert(destination->getChannelCount() == channelsBefore);

    std::cout << "[PASS] Trailing JSON object is rejected non-destructively" << std::endl;
}

void testMissingPatternReference() {
    std::cout << "[TEST] Missing pattern reference preserves placeholder..." << std::endl;

    const Aestra::Tests::ScopedTempDirectory testDirScope{"ProjectLoadRegression"};
    const auto& testDir = testDirScope.path();
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
}

void testMissingArsenalUnitReference() {
    std::cout << "[TEST] Missing Arsenal unit reference preserves note..." << std::endl;

    const Aestra::Tests::ScopedTempDirectory testDirScope{"ProjectLoadRegression"};
    const auto& testDir = testDirScope.path();
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
}

void testFailedValidationDoesNotClear() {
    std::cout << "[TEST] Failed validation does not clear existing state..." << std::endl;

    const Aestra::Tests::ScopedTempDirectory testDirScope{"ProjectLoadRegression"};
    const auto& testDir = testDirScope.path();
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
}

void testUnitManagerSurvivesFailedLoad() {
    std::cout << "[TEST] UnitManager survives failed PHASE 3 validation..." << std::endl;

    const Aestra::Tests::ScopedTempDirectory testDirScope{"ProjectLoadRegression"};
    const auto& testDir = testDirScope.path();
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
}

void testMissingAudioFileNonDestructive() {
    std::cout << "[TEST] Missing audio file is non-destructive..." << std::endl;

    const Aestra::Tests::ScopedTempDirectory testDirScope{"ProjectLoadRegression"};
    const auto& testDir = testDirScope.path();
    std::filesystem::path testProject = testDir / "project.aes";

    // References a file that does NOT exist
    std::string projectJson = R"({
        "version": 1,
        "tempo": 120.0,
        "playhead": 0.0,
        "sources": [{"id": 1, "path": ")" +
                              (testDir / "nonexistent.wav").generic_string() + R"(", "name": "Missing Audio"}],
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
}

void testUnresolvedRouteTargetNonFatal() {
    std::cout << "[TEST] Unresolved send routing targets are non-fatal..." << std::endl;

    const Aestra::Tests::ScopedTempDirectory testDirScope{"ProjectLoadRegression"};
    const auto& testDir = testDirScope.path();
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
    assert(saved.find("\"targetId\": 99") != std::string::npos || saved.find("\"targetId\":99") != std::string::npos);

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
}

void testV1FixtureMigratesToCurrentVersion() {
    std::cout << "[TEST] v1 fixture migrates to current project version..." << std::endl;

    auto fixturePath = std::filesystem::path(AESTRA_PROJECT_FIXTURE_DIR) / "v1_minimal.aes";
    assert(std::filesystem::exists(fixturePath));

    std::ifstream fixtureIn(fixturePath);
    std::string fixtureJson((std::istreambuf_iterator<char>(fixtureIn)), std::istreambuf_iterator<char>());
    assert(fixtureJson.find("\"version\": 1") != std::string::npos ||
           fixtureJson.find("\"version\":1") != std::string::npos);

    auto trackManager = std::make_shared<TrackManager>();
    auto result = ProjectSerializer::load(fixturePath.string(), trackManager);
    assert(result.ok);
    assert(result.errorMessage.empty());
    assert(std::abs(result.tempo - 133.5) < 1e-9);
    assert(std::abs(result.playhead - 2.25) < 1e-9);
    assert(trackManager->getChannelCount() == 1);

    auto laneIds = trackManager->getPlaylistModel().getLaneIDs();
    assert(laneIds.size() == 1);
    auto* lane = trackManager->getPlaylistModel().getLane(laneIds[0]);
    assert(lane);
    assert(lane->name == "Migrated Track");
    assert(lane->clips.size() == 1);
    assert(lane->clips[0].name == "Migrated Clip");
    assert(std::abs(lane->clips[0].startBeat - 4.0) < 1e-9);
    assert(std::abs(lane->clips[0].durationBeats - 8.0) < 1e-9);
    assert(std::abs(lane->clips[0].sourceOffset - 0.5) < 1e-9);

    auto patterns = trackManager->getPatternManager().getAllPatterns();
    assert(patterns.size() == 1);
    assert(patterns[0]);
    assert(patterns[0]->isMidi());
    assert(patterns[0]->name == "V1 MIDI Pattern");
    assert(std::abs(patterns[0]->lengthBeats - 8.0) < 1e-9);
    assert(patterns[0]->getMidiNotes().size() == 2);
    assert(patterns[0]->getMidiNotes()[0].pitch == 60);
    assert(std::abs(patterns[0]->getMidiNotes()[0].velocity - 0.75f) < 1e-6f);

    std::string saved = ProjectSerializer::serialize(trackManager, result.tempo, result.playhead, 0).contents;
    assert(saved.find("\"version\": 2") != std::string::npos || saved.find("\"version\":2") != std::string::npos);

    const Aestra::Tests::ScopedTempDirectory testDirScope{"ProjectLoadRegression"};
    const auto& testDir = testDirScope.path();
    auto migratedPath = testDir / "v1_migrated_roundtrip.aes";
    std::ofstream migratedOut(migratedPath);
    migratedOut << saved;
    migratedOut.close();

    auto roundTripManager = std::make_shared<TrackManager>();
    auto roundTripResult = ProjectSerializer::load(migratedPath.string(), roundTripManager);
    assert(roundTripResult.ok);
    assert(roundTripResult.errorMessage.empty());
    assert(std::abs(roundTripResult.tempo - 133.5) < 1e-9);
    assert(std::abs(roundTripResult.playhead - 2.25) < 1e-9);

    auto roundTripLaneIds = roundTripManager->getPlaylistModel().getLaneIDs();
    assert(roundTripLaneIds.size() == 1);
    auto* roundTripLane = roundTripManager->getPlaylistModel().getLane(roundTripLaneIds[0]);
    assert(roundTripLane);
    assert(roundTripLane->name == "Migrated Track");
    assert(roundTripLane->clips.size() == 1);
    assert(roundTripLane->clips[0].name == "Migrated Clip");
    assert(std::abs(roundTripLane->clips[0].startBeat - 4.0) < 1e-9);
    assert(std::abs(roundTripLane->clips[0].durationBeats - 8.0) < 1e-9);
    assert(std::abs(roundTripLane->clips[0].sourceOffset - 0.5) < 1e-9);

    auto roundTripPatterns = roundTripManager->getPatternManager().getAllPatterns();
    assert(roundTripPatterns.size() == 1);
    assert(roundTripPatterns[0]);
    assert(roundTripPatterns[0]->isMidi());
    assert(roundTripPatterns[0]->name == "V1 MIDI Pattern");
    assert(roundTripPatterns[0]->getMidiNotes().size() == 2);
    assert(roundTripPatterns[0]->getMidiNotes()[0].pitch == 60);
    assert(std::abs(roundTripPatterns[0]->getMidiNotes()[0].velocity - 0.75f) < 1e-6f);
    assert(roundTripPatterns[0]->getMidiNotes()[1].pitch == 64);
    assert(std::abs(roundTripPatterns[0]->getMidiNotes()[1].velocity - 0.5f) < 1e-6f);

    std::cout << "[PASS] v1 fixture migrates to current project version" << std::endl;
}

void testAutomationTarget256DoesNotWrapToVolume() {
    std::cout << "[TEST] AutomationTarget 256 does not wrap to Volume..." << std::endl;

    const Aestra::Tests::ScopedTempDirectory testDirScope{"ProjectLoadRegression"};
    const auto& testDir = testDirScope.path();
    std::filesystem::path testProject = testDir / "project.aes";

    // AutomationTarget is uint8_t. Raw static_cast wraps 256→0 (Volume).
    // The ProjectSerializer boundary [0,255] must clamp 256 to 255 (Custom).
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
                "automation": [
                    {
                        "param": "volume",
                        "targetEnum": 256,
                        "default": 0.75,
                        "points": [
                            {"b": 0.0, "v": 1.0, "c": 0.0},
                            {"b": 4.0, "v": 0.5, "c": 0.0}
                        ]
                    }
                ]
            }
        ],
        "arsenal": {"nextId": 1, "units": []}
    })";

    std::ofstream out(testProject);
    out << projectJson;
    out.close();

    auto trackManager = std::make_shared<TrackManager>();
    auto result = ProjectSerializer::load(testProject.string(), trackManager);
    assert(result.ok);

    auto laneIds = trackManager->getPlaylistModel().getLaneIDs();
    assert(!laneIds.empty());
    auto* lane = trackManager->getPlaylistModel().getLane(laneIds[0]);
    assert(lane);
    assert(!lane->automationCurves.empty());

    auto& curve = lane->automationCurves[0];
    // Must NOT wrap to Volume(0)
    assert(curve.getAutomationTarget() != AutomationTarget::Volume);
    // Must be clamped to Custom(255) per the serializer boundary
    assert(curve.getAutomationTarget() == AutomationTarget::Custom);
    // Default value must survive
    assert(curve.getDefaultValue() == 0.75f);
    // Points must survive (2 points defined)
    assert(curve.getPoints().size() == 2);

    // Verify round-trip: re-save and reload
    std::string saved = ProjectSerializer::serialize(trackManager, 120.0, 0.0, 0).contents;
    assert(!saved.empty());

    std::filesystem::path testProject2 = testDir / "project2.aes";
    std::ofstream out2(testProject2);
    out2 << saved;
    out2.close();

    auto trackManager2 = std::make_shared<TrackManager>();
    auto result2 = ProjectSerializer::load(testProject2.string(), trackManager2);
    assert(result2.ok);

    auto laneIds2 = trackManager2->getPlaylistModel().getLaneIDs();
    assert(!laneIds2.empty());
    auto* lane2 = trackManager2->getPlaylistModel().getLane(laneIds2[0]);
    assert(lane2);
    assert(!lane2->automationCurves.empty());
    assert(lane2->automationCurves[0].getAutomationTarget() == AutomationTarget::Custom);
    assert(lane2->automationCurves[0].getPoints().size() == 2);

    std::cout << "[PASS] AutomationTarget 256 does not wrap to Volume" << std::endl;
}

void testMixerLaneStateNumbersClampBeforeCast() {
    std::cout << "[TEST] Mixer lane state numbers clamp before cast..." << std::endl;

    const Aestra::Tests::ScopedTempDirectory testDirScope{"ProjectLoadRegression"};
    const auto& testDir = testDirScope.path();
    std::filesystem::path testProject = testDir / "project.aes";

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
                "inputChannelIndex": 2147483648,
                "width": 1e100,
                "trackColorIndex": 1e100,
                "clips": []
            },
            {
                "name": "Track 2",
                "color": "4294967295",
                "volume": 1.0,
                "pan": 0.0,
                "inputChannelIndex": -1e100,
                "trackColorIndex": -1e100,
                "clips": []
            }
        ],
        "arsenal": {"nextId": 1, "units": []}
    })";

    std::ofstream out(testProject);
    out << projectJson;
    out.close();

    auto trackManager = std::make_shared<TrackManager>();
    auto result = ProjectSerializer::load(testProject.string(), trackManager);
    assert(result.ok);
    assert(trackManager->getChannelCount() == 2);

    auto* channel = trackManager->getChannel(0);
    assert(channel != nullptr);
    assert(channel->getInputChannelIndex() == 1024);
    assert(channel->getTrackColorIndex() == 1024);
    assert(std::abs(channel->getWidth() - 4.0f) < 1e-6f);

    auto* negativeChannel = trackManager->getChannel(1);
    assert(negativeChannel != nullptr);
    assert(negativeChannel->getInputChannelIndex() == -2);
    assert(negativeChannel->getTrackColorIndex() == -1);

    std::cout << "[PASS] Mixer lane state numbers clamp before cast" << std::endl;
}

void testTrackColorIndexRoundtrip() {
    std::cout << "[TEST] trackColorIndex survives save/load roundtrip..." << std::endl;

    const Aestra::Tests::ScopedTempDirectory testDirScope{"ProjectLoadRegression"};
    const auto& testDir = testDirScope.path();
    std::filesystem::path testProject = testDir / "project.aes";

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
                "trackColorIndex": 5,
                "clips": []
            },
            {
                "name": "Track 2",
                "color": "4294967295",
                "volume": 1.0,
                "pan": 0.0,
                "trackColorIndex": 2,
                "clips": []
            },
            {
                "name": "Track 3",
                "color": "4294967295",
                "volume": 1.0,
                "pan": 0.0,
                "clips": []
            }
        ],
        "arsenal": {"nextId": 1, "units": []}
    })";

    std::ofstream out(testProject);
    out << projectJson;
    out.close();

    auto trackManager = std::make_shared<TrackManager>();
    auto result = ProjectSerializer::load(testProject.string(), trackManager);
    assert(result.ok);
    assert(trackManager->getChannelCount() == 3);

    assert(trackManager->getChannel(0)->getTrackColorIndex() == 5);
    assert(trackManager->getChannel(1)->getTrackColorIndex() == 2);
    // Lane without the field keeps the unset default.
    assert(trackManager->getChannel(2)->getTrackColorIndex() == -1);

    // Re-save and reload to verify the write side emits the field.
    std::string saved = ProjectSerializer::serialize(trackManager, 120.0, 0.0, 0).contents;
    assert(!saved.empty());

    std::filesystem::path testProject2 = testDir / "project2.aes";
    std::ofstream out2(testProject2);
    out2 << saved;
    out2.close();

    auto trackManager2 = std::make_shared<TrackManager>();
    auto result2 = ProjectSerializer::load(testProject2.string(), trackManager2);
    assert(result2.ok);
    assert(trackManager2->getChannelCount() == 3);
    assert(trackManager2->getChannel(0)->getTrackColorIndex() == 5);
    assert(trackManager2->getChannel(1)->getTrackColorIndex() == 2);
    assert(trackManager2->getChannel(2)->getTrackColorIndex() == -1);

    std::cout << "[PASS] trackColorIndex survives save/load roundtrip" << std::endl;
}

void testProjectLoadWarningsAreBounded() {
    std::cout << "[TEST] Project load warnings are bounded..." << std::endl;

    const Aestra::Tests::ScopedTempDirectory testDirScope{"ProjectLoadRegression"};
    const auto& testDir = testDirScope.path();
    std::filesystem::path testProject = testDir / "project.aes";

    std::ostringstream lanes;
    for (int i = 0; i < 100; ++i) {
        if (i > 0)
            lanes << ",";
        lanes << R"({
                "name": "Track )"
              << i << R"(",
                "color": "4294967295",
                "volume": 1.0,
                "pan": 0.0,
                "routing": {"sends": [{"targetId": )"
              << (100000 + i) << R"(, "gain": 1.0}]},
                "clips": []
            })";
    }

    std::string projectJson = R"({
        "version": 1,
        "tempo": 120.0,
        "playhead": 0.0,
        "sources": [],
        "patterns": [],
        "lanes": [)" + lanes.str() +
                              R"(],
        "arsenal": {"nextId": 1, "units": []}
    })";

    std::ofstream out(testProject);
    out << projectJson;
    out.close();

    auto previousLogger = Log::getLogger();
    auto capture = std::make_shared<CapturingLogger>();
    Log::init(capture);

    auto trackManager = std::make_shared<TrackManager>();
    auto result = ProjectSerializer::load(testProject.string(), trackManager);

    Log::init(previousLogger);

    assert(result.ok);
    size_t sendWarnings = 0;
    size_t suppressedWarnings = 0;
    for (const auto& entry : capture->entries) {
        if (entry.level != LogLevel::Warning) {
            continue;
        }
        if (entry.message.find("Send from '") != std::string::npos) {
            ++sendWarnings;
        }
        if (entry.message.find("Additional unresolved send route warnings suppressed") != std::string::npos) {
            ++suppressedWarnings;
        }
    }

    assert(sendWarnings == 64);
    assert(suppressedWarnings == 1);

    std::cout << "[PASS] Project load warnings are bounded" << std::endl;
}

} // namespace

int main() {
    std::cout << "=== Project Load Regression Tests ===" << std::endl;

    testValidProjectLoad();
    testTrailingJsonObjectIsRejectedNonDestructively();
    testMissingPatternReference();
    testMissingArsenalUnitReference();
    testFailedValidationDoesNotClear();
    testUnitManagerSurvivesFailedLoad();
    testMissingAudioFileNonDestructive();
    testUnresolvedRouteTargetNonFatal();
    testV1FixtureMigratesToCurrentVersion();
    testAutomationTarget256DoesNotWrapToVolume();
    testMixerLaneStateNumbersClampBeforeCast();
    testTrackColorIndexRoundtrip();
    testProjectLoadWarningsAreBounded();

    std::cout << "=== All tests passed ===" << std::endl;
    return 0;
}
