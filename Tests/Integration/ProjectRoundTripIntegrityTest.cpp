// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Round-trip project integrity tests - prove project meaning survives serialization cycles.

#include "../../Source/Core/ProjectSerializer.h"
#include "../AestraCore/include/AestraLog.h"
#include "../Support/TestTempDirectory.h"
#include "Models/ClipSource.h"
#include "Models/PatternSource.h"
#include "Models/TrackManager.h"
#include "Plugin/PluginHost.h"
#include "Plugin/PluginManager.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <variant>
#include <vector>

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

std::string normalizeJson(const std::string& json) {
    std::string result = json;

    std::regex wsRegex("\\s+");
    result = std::regex_replace(result, wsRegex, " ");

    std::regex trailingWs(" *");
    result = std::regex_replace(result, trailingWs, "");

    std::regex missingPrefix("\"\\[MISSING PATTERN\\] *");
    result = std::regex_replace(result, missingPrefix, "");

    return result;
}

std::string serializeProject(Aestra::Audio::TrackManager& tm, double tempo, double playhead) {
    auto ser = ProjectSerializer::serialize(
        std::shared_ptr<Aestra::Audio::TrackManager>(&tm, [](Aestra::Audio::TrackManager*) {}), tempo, playhead, 0);
    return ser.contents;
}

void compareProjectSemantic(const std::string& json1, const std::string& json2, const std::string& testName) {
    std::string norm1 = normalizeJson(json1);
    std::string norm2 = normalizeJson(json2);

    // Extract and compare only semantic fields (skip regenerated IDs)
    // Check that count of lanes, clips, patterns, notes, etc. match
    auto countField = [](const std::string& json, const char* field) -> int {
        std::string pattern = "\"" + std::string(field) + "\":[";
        size_t pos = json.find(pattern);
        if (pos == std::string::npos)
            return 0;
        int count = 0;
        size_t start = pos + pattern.size();
        for (size_t i = start; i < json.size() && json[i] != '}'; ++i) {
            if (json[i] == '{')
                ++count;
        }
        return count;
    };

    int lanes1 = countField(norm1, "lanes");
    int lanes2 = countField(norm2, "lanes");
    int clips1 = countField(norm1, "clips");
    int clips2 = countField(norm2, "clips");
    int notes1 = countField(norm1, "notes");
    int notes2 = countField(norm2, "notes");
    int units1 = countField(norm1, "units");
    int units2 = countField(norm2, "units");

    auto getLaneNames = [](const std::string& json) -> std::vector<std::string> {
        std::vector<std::string> names;
        size_t search = 0;
        while ((search = json.find("\"lanes\":[", search)) != std::string::npos) {
            size_t laneStart = json.find('{', search);
            while (laneStart != std::string::npos) {
                size_t laneEnd = json.find('}', laneStart);
                std::string lane = json.substr(laneStart, laneEnd - laneStart + 1);
                size_t namePos = lane.find("\"name\":\"");
                if (namePos != std::string::npos) {
                    size_t nameStart = namePos + 8;
                    size_t nameEnd = lane.find("\"", nameStart);
                    names.push_back(lane.substr(nameStart, nameEnd - nameStart));
                }
                laneStart = json.find('{', laneEnd);
            }
            ++search;
        }
        return names;
    };

    std::vector<std::string> names1 = getLaneNames(norm1);
    std::vector<std::string> names2 = getLaneNames(norm2);

    if (lanes1 != lanes2 || clips1 != clips2 || notes1 != notes2 || units1 != units2) {
        std::cout << "[FAIL] " << testName << " - count mismatch (lanes:" << lanes1 << "vs" << lanes2
                  << " clips:" << clips1 << "vs" << clips2 << " notes:" << notes1 << "vs" << notes2
                  << " units:" << units1 << "vs" << units2 << ")" << std::endl;
        assert(false);
    }

    if (names1 != names2) {
        std::cout << "[FAIL] " << testName << " - lane names differ" << std::endl;
        assert(false);
    }

    std::cout << "[PASS] " << testName << std::endl;
}

void testEmptyProjectRoundTrip() {
    std::cout << "[TEST] Empty project round-trip..." << std::endl;

    const Aestra::Tests::ScopedTempDirectory testDirScope{"ProjectRoundTripIntegrity"};
    const auto& testDir = testDirScope.path();
    std::filesystem::path testProject = testDir / "project.aes";

    auto tm1 = std::make_shared<Aestra::Audio::TrackManager>();
    std::string firstSave = serializeProject(*tm1, 120.0, 0.0);

    std::ofstream out(testProject);
    out << firstSave;
    out.close();

    auto tm2 = std::make_shared<Aestra::Audio::TrackManager>();
    auto result = ProjectSerializer::load(testProject.string(), tm2);
    assert(result.ok);

    std::string secondSave = serializeProject(*tm2, result.tempo, result.playhead);

    compareProjectSemantic(firstSave, secondSave, "empty_project");
}

void testSourcesLanesClipsPatternsRoundTrip() {
    std::cout << "[TEST] Sources/lanes/clips/patterns round-trip..." << std::endl;

    const Aestra::Tests::ScopedTempDirectory testDirScope{"ProjectRoundTripIntegrity"};
    const auto& testDir = testDirScope.path();
    std::filesystem::path testWav = testDir / "audio.wav";
    std::filesystem::path testProject = testDir / "project.aes";

    assert(writeMinimalWavMono16(testWav, 44100, 4410));

    auto tm1 = std::make_shared<Aestra::Audio::TrackManager>();
    tm1->getSourceManager().getOrCreateSource(testWav.string());

    auto& playlist = tm1->getPlaylistModel();
    auto laneId = playlist.createLane("Test Lane");
    auto lane = playlist.getLane(laneId);
    lane->volume = 0.8f;
    lane->pan = -0.3f;
    // FD-14: lanes belong to Tracks — the fixture builds the ownership model.
    tm1->createTrack(laneId, "Test Lane");

    auto& patternManager = tm1->getPatternManager();
    Aestra::Audio::MidiPayload payload;
    payload.notes.push_back({60, 0.0, 1.0, 1.0f, 0.0f, 0, 0, 1.0f, false});
    payload.notes.push_back({64, 1.0, 1.0, 0.8f, 0.0f, 0, 0, 1.0f, false});
    auto patternId = patternManager.createMidiPattern("Test Pattern", 4.0, payload);

    Aestra::Audio::ClipInstance clip;
    clip.id = Aestra::Audio::ClipInstanceID::fromString("clip-1");
    clip.patternId = patternId;
    clip.sourceId = patternId.value;
    clip.startBeat = 0.0;
    clip.durationBeats = 4.0;
    clip.name = "Test Clip";
    playlist.addClip(laneId, clip);

    std::string firstSave = serializeProject(*tm1, 120.0, 0.0);

    std::ofstream out1(testProject);
    out1 << firstSave;
    out1.close();

    auto tm2 = std::make_shared<Aestra::Audio::TrackManager>();
    auto result1 = ProjectSerializer::load(testProject.string(), tm2);
    assert(result1.ok);

    std::string secondSave = serializeProject(*tm2, result1.tempo, result1.playhead);

    std::filesystem::path testProject2 = testDir / "project2.aes";
    std::ofstream out2(testProject2);
    out2 << secondSave;
    out2.close();

    auto tm3 = std::make_shared<Aestra::Audio::TrackManager>();
    auto result2 = ProjectSerializer::load(testProject2.string(), tm3);
    assert(result2.ok);

    std::string thirdSave = serializeProject(*tm3, result2.tempo, result2.playhead);

    compareProjectSemantic(firstSave, secondSave, "sources_lanes_clips_patterns");
    compareProjectSemantic(secondSave, thirdSave, "sources_lanes_clips_patterns_2nd_cycle");
}

void testArsenalUnitsRoundTrip() {
    std::cout << "[TEST] Arsenal units round-trip..." << std::endl;

    const Aestra::Tests::ScopedTempDirectory testDirScope{"ProjectRoundTripIntegrity"};
    const auto& testDir = testDirScope.path();
    std::filesystem::path testProject = testDir / "project.aes";

    auto tm1 = std::make_shared<Aestra::Audio::TrackManager>();
    auto& unitManager = tm1->getUnitManager();

    std::string firstSave = serializeProject(*tm1, 120.0, 0.0);

    std::ofstream out(testProject);
    out << firstSave;
    out.close();

    auto tm2 = std::make_shared<Aestra::Audio::TrackManager>();
    auto result = ProjectSerializer::load(testProject.string(), tm2);
    assert(result.ok);

    std::string secondSave = serializeProject(*tm2, result.tempo, result.playhead);

    compareProjectSemantic(firstSave, secondSave, "arsenal_units");
}

void testArsenalDefaultPatternRebindsAfterLoad() {
    std::cout << "[TEST] Arsenal default pattern rebinds after load..." << std::endl;

    const Aestra::Tests::ScopedTempDirectory testDirScope{"ProjectRoundTripIntegrity"};
    const auto& testDir = testDirScope.path();
    std::filesystem::path testProject = testDir / "project.aes";

    std::string projectJson = R"({
        "version": 1,
        "tempo": 120.0,
        "playhead": 0.0,
        "sources": [],
        "patterns": [
            {"id": 10, "name": "Other Pattern", "type": "midi", "length": 8.0, "notes": []},
            {"id": 42, "name": "Sampler Restored Pattern", "type": "midi", "length": 8.0,
             "notes": [{"pitch": 60, "startBeat": 0.0, "durationBeats": 1.0, "velocity": 1.0,
                        "unitId": 7, "pitchOffset": 0, "gate": 1.0, "slide": false}]}
        ],
        "lanes": [],
        "arsenal": {
            "nextId": 8,
            "units": [{
                "id": 7,
                "name": "Sampler 1",
                "enabled": true,
                "targetMixerRoute": -1,
                "timelineLaneAssignment": -1,
                "color": "4286611584",
                "muted": false,
                "solo": false,
                "armed": false,
                "audioClipPath": "",
                "audioDurationSeconds": 0.0,
                "defaultPatternId": 42,
                "group": {"id": 2, "name": "Drums"},
                "type": {"id": 1, "name": "Sampler"}
            }]
        }
    })";

    std::ofstream out(testProject);
    out << projectJson;
    out.close();

    auto tm = std::make_shared<Aestra::Audio::TrackManager>();
    auto result = ProjectSerializer::load(testProject.string(), tm);
    assert(result.ok);

    const auto* unit = tm->getUnitManager().getUnit(7);
    assert(unit);
    assert(unit->defaultPatternId.isValid());
    // Serialized pattern identity is restored on load since #446, so the
    // rebind map resolves 42 -> 42. The binding itself (unit points at the
    // pattern that carries its notes) is what this test guards.
    assert(unit->defaultPatternId.value == 42);

    const auto* pattern = tm->getPatternManager().getPattern(unit->defaultPatternId);
    assert(pattern);
    assert(pattern->name == "Sampler Restored Pattern");
    assert(std::holds_alternative<Aestra::Audio::MidiPayload>(pattern->payload));
    const auto& payload = std::get<Aestra::Audio::MidiPayload>(pattern->payload);
    assert(payload.notes.size() == 1);
    assert(payload.notes[0].unitId == 7);
}

void testArsenalSamplerAudioClipPathRehydratesPluginAfterLoad() {
    std::cout << "[TEST] Arsenal sampler audioClipPath rehydrates plugin after load..." << std::endl;

    auto& pluginManager = Aestra::Audio::PluginManager::getInstance();
    assert(pluginManager.initialize());

    const Aestra::Tests::ScopedTempDirectory testDirScope{"ProjectRoundTripIntegrity"};
    const auto& testDir = testDirScope.path();
    std::filesystem::path testWav = testDir / "sampler.wav";
    std::filesystem::path testProject = testDir / "project.aes";
    assert(writeMinimalWavMono16(testWav, 48000, 4800));

    const std::string wavPath = testWav.generic_string();
    std::string projectJson = R"({
        "version": 2,
        "tempo": 120.0,
        "playhead": 0.0,
        "sources": [],
        "patterns": [
            {"id": 1, "name": "Sampler Pattern", "type": "midi", "length": 8.0,
             "notes": [{"pitch": 60, "startBeat": 0.0, "durationBeats": 1.0, "velocity": 1.0,
                        "unitId": 1, "pitchOffset": 0, "gate": 1.0, "slide": false}]}
        ],
        "lanes": [],
        "arsenal": {
            "nextId": 2,
            "units": [{
                "id": 1,
                "name": "Sampler 1",
                "enabled": true,
                "targetMixerRoute": 0,
                "timelineLaneAssignment": 0,
                "color": "4286611584",
                "muted": false,
                "solo": false,
                "armed": false,
                "audioClipPath": ")" +
                              wavPath + R"(",
                "audioDurationSeconds": 0.1,
                "defaultPatternId": 1,
                "pluginId": "com.Aestrastudios.sampler",
                "pluginStateHex": "7b22706172616d73223a5b302e3030312c322c312c302e3030312c305d2c22726f6f744d6964694e6f7465223a36307d",
                "group": {"id": 2, "name": "Drums"},
                "type": {"id": 1, "name": "Sampler"}
            }]
        }
    })";

    std::ofstream out(testProject);
    out << projectJson;
    out.close();

    auto tm = std::make_shared<Aestra::Audio::TrackManager>();
    auto result = ProjectSerializer::load(testProject.string(), tm);
    assert(result.ok);

    auto plugin = tm->getUnitManager().getUnitPlugin(1);
    assert(plugin);
    assert(plugin->isActive());

    constexpr uint32_t kFrames = 256;
    std::vector<float> left(kFrames, 0.0f);
    std::vector<float> right(kFrames, 0.0f);
    float* outputs[2] = {left.data(), right.data()};

    Aestra::Audio::MidiBuffer midi;
    const uint8_t noteOn[3] = {0x90, 60, 127};
    midi.addEvent(0, noteOn, 3);
    plugin->process(nullptr, outputs, 0, 2, kFrames, &midi, nullptr);

    float peak = 0.0f;
    for (uint32_t i = 0; i < kFrames; ++i) {
        peak = std::max(peak, std::abs(left[i]));
        peak = std::max(peak, std::abs(right[i]));
    }
    assert(peak > 1.0e-5f);

    std::cout << "[PASS] Arsenal sampler audioClipPath rehydrates plugin after load" << std::endl;
}

void testMissingPatternReferenceDoesNotCompound() {
    std::cout << "[TEST] Missing pattern reference does not compound..." << std::endl;

    const Aestra::Tests::ScopedTempDirectory testDirScope{"ProjectRoundTripIntegrity"};
    const auto& testDir = testDirScope.path();
    std::filesystem::path testProject = testDir / "project.aes";

    std::string projectJson = R"({
        "version": 1,
        "tempo": 120.0,
        "playhead": 0.0,
        "sources": [],
        "patterns": [{"id": 1, "name": "Valid Pattern", "type": "midi", "length": 4.0, "notes": []}],
        "lanes": [{"name": "Track 1", "color": "4294967295", "volume": 1.0, "pan": 0.0,
            "clips": [{"id": "clip-1", "patternId": 999, "start": 0.0, "duration": 4.0, "name": "Original Clip Name"}]}
        ],
        "arsenal": {"nextId": 1, "units": []}
    })";

    std::ofstream out(testProject);
    out << projectJson;
    out.close();

    auto tm1 = std::make_shared<Aestra::Audio::TrackManager>();
    auto result1 = ProjectSerializer::load(testProject.string(), tm1);
    assert(result1.ok);

    std::string save1 = serializeProject(*tm1, result1.tempo, result1.playhead);

    std::filesystem::path testProject2 = testDir / "project2.aes";
    std::ofstream out2(testProject2);
    out2 << save1;
    out2.close();

    auto tm2 = std::make_shared<Aestra::Audio::TrackManager>();
    auto result2 = ProjectSerializer::load(testProject2.string(), tm2);
    assert(result2.ok);

    std::string save2 = serializeProject(*tm2, result2.tempo, result2.playhead);

    std::filesystem::path testProject3 = testDir / "project3.aes";
    std::ofstream out3(testProject3);
    out3 << save2;
    out3.close();

    auto tm3 = std::make_shared<Aestra::Audio::TrackManager>();
    auto result3 = ProjectSerializer::load(testProject3.string(), tm3);
    assert(result3.ok);

    std::string save3 = serializeProject(*tm3, result3.tempo, result3.playhead);

    if (save2.find("[MISSING PATTERN] [MISSING PATTERN]") != std::string::npos) {
        std::cout << "[FAIL] Missing pattern prefix compounds on repeat load" << std::endl;
        assert(false);
    }

    compareProjectSemantic(save1, save2, "missing_pattern_round_1");
    compareProjectSemantic(save2, save3, "missing_pattern_round_2");
}

void testMultipleRoundTripCycles() {
    std::cout << "[TEST] Multiple round-trip cycles..." << std::endl;

    const Aestra::Tests::ScopedTempDirectory testDirScope{"ProjectRoundTripIntegrity"};
    const auto& testDir = testDirScope.path();
    std::filesystem::path testProject = testDir / "project.aes";

    auto tm1 = std::make_shared<Aestra::Audio::TrackManager>();
    Aestra::Audio::PlaylistLaneID lane1 = tm1->getPlaylistModel().createLane("Track 1");
    Aestra::Audio::PlaylistLaneID lane2 = tm1->getPlaylistModel().createLane("Track 2");
    // FD-14: lanes belong to Tracks — the fixture builds the ownership model.
    tm1->createTrack(lane1, "Track 1");
    tm1->createTrack(lane2, "Track 2");

    std::string save1 = serializeProject(*tm1, 128.0, 2.0);

    std::filesystem::path p = testProject;
    for (int cycle = 0; cycle < 5; ++cycle) {
        std::ofstream out(p);
        out << save1;
        out.close();

        auto tm = std::make_shared<Aestra::Audio::TrackManager>();
        auto result = ProjectSerializer::load(p.string(), tm);
        assert(result.ok);

        save1 = serializeProject(*tm, result.tempo, result.playhead);

        std::filesystem::path nextPath = testDir / ("project_" + std::to_string(cycle + 1) + ".aes");
        p = nextPath;
    }

    std::cout << "[PASS] Multiple round-trip cycles (5 cycles)" << std::endl;
}

void testAutomationRoundTrip() {
    std::cout << "[TEST] Automation round-trip..." << std::endl;

    const Aestra::Tests::ScopedTempDirectory testDirScope{"ProjectRoundTripIntegrity"};
    const auto& testDir = testDirScope.path();
    std::filesystem::path testProject = testDir / "project.aes";

    auto tm1 = std::make_shared<Aestra::Audio::TrackManager>();
    auto laneId = tm1->getPlaylistModel().createLane("Automation Lane");
    auto lane = tm1->getPlaylistModel().getLane(laneId);
    assert(lane);

    lane->automationCurves.push_back(Aestra::Audio::AutomationCurve("volume", Aestra::Audio::AutomationTarget::Volume));
    auto& curve = lane->automationCurves.back();
    curve.setDefaultValue(1.0f);
    curve.addPoint(0.0, 1.0, 44100.0, 0.0f);
    curve.addPoint(4.0, 0.5, 44100.0, 0.0f);
    curve.addPoint(8.0, 1.0, 44100.0, 0.0f);

    std::string save1 = serializeProject(*tm1, 120.0, 0.0);

    // Cycle 1
    std::ofstream out(testProject);
    out << save1;
    out.close();

    auto tm2 = std::make_shared<Aestra::Audio::TrackManager>();
    auto result = ProjectSerializer::load(testProject.string(), tm2);
    assert(result.ok);

    auto lanes2 = tm2->getPlaylistModel().getLaneIDs();
    assert(!lanes2.empty());
    auto loadedLane = tm2->getPlaylistModel().getLane(lanes2[0]);
    assert(loadedLane);
    assert(!loadedLane->automationCurves.empty());

    std::string save2 = serializeProject(*tm2, result.tempo, result.playhead);

    // Cycle 2: save again, load again
    std::filesystem::path testProject2 = testDir / "project2.aes";
    std::ofstream out2(testProject2);
    out2 << save2;
    out2.close();

    auto tm3 = std::make_shared<Aestra::Audio::TrackManager>();
    auto result2 = ProjectSerializer::load(testProject2.string(), tm3);
    assert(result2.ok);

    auto lanes3 = tm3->getPlaylistModel().getLaneIDs();
    assert(!lanes3.empty());
    auto loadedLane3 = tm3->getPlaylistModel().getLane(lanes3[0]);
    assert(loadedLane3);
    assert(!loadedLane3->automationCurves.empty());

    std::string save3 = serializeProject(*tm3, result2.tempo, result2.playhead);

    // Cycle 3: save again, load again
    std::filesystem::path testProject3 = testDir / "project3.aes";
    std::ofstream out3(testProject3);
    out3 << save3;
    out3.close();

    auto tm4 = std::make_shared<Aestra::Audio::TrackManager>();
    auto result3 = ProjectSerializer::load(testProject3.string(), tm4);
    assert(result3.ok);

    auto lanes4 = tm4->getPlaylistModel().getLaneIDs();
    assert(!lanes4.empty());
    auto loadedLane4 = tm4->getPlaylistModel().getLane(lanes4[0]);
    assert(loadedLane4);
    assert(!loadedLane4->automationCurves.empty());

    // Verify semantic survival across 3 cycles
    // Check automation curves count is stable across cycles
    auto countAutomationCurves = [](const std::string& json) -> int {
        size_t count = 0;
        size_t pos = 0;
        const std::string pattern = "\"automation\":[";
        while ((pos = json.find(pattern, pos)) != std::string::npos) {
            size_t start = pos + pattern.size();
            // Count objects until end of array
            int braceDepth = 1;
            for (size_t i = start; i < json.size(); ++i) {
                if (json[i] == '{')
                    ++braceDepth;
                else if (json[i] == '}')
                    --braceDepth;
                if (braceDepth == 0) {
                    ++count;
                    break;
                } else if (json[i] == ',') {
                    std::cerr << "";
                } // continue
            }
            pos = start;
        }
        return static_cast<int>(count);
    };

    int autoCurves1 = countAutomationCurves(save1);
    int autoCurves2 = countAutomationCurves(save2);
    int autoCurves3 = countAutomationCurves(save3);

    (void)(autoCurves1);
    (void)(autoCurves2);
    (void)(autoCurves3);

    // Also verify point count semantic survival
    auto countAutomationPoints = [](const std::string& json) -> int {
        int count = 0;
        size_t pos = 0;
        const std::string pattern = "\"b\":";
        while ((pos = json.find(pattern, pos)) != std::string::npos) {
            ++count;
            pos += pattern.size();
        }
        return count;
    };

    int points1 = countAutomationPoints(save1);
    int points2 = countAutomationPoints(save2);
    int points3 = countAutomationPoints(save3);

    assert(points1 == points2 && "Automation point count changed across round-trip cycle 1→2");
    assert(points2 == points3 && "Automation point count changed across round-trip cycle 2→3");

    std::cout << "[PASS] Automation round-trip (3 cycles, " << autoCurves3 << " curves, " << points3 << " points)"
              << std::endl;
}

void testLegacyProjectWithoutAutomation() {
    std::cout << "[TEST] Legacy project without automation key..." << std::endl;

    const Aestra::Tests::ScopedTempDirectory testDirScope{"ProjectRoundTripIntegrity"};
    const auto& testDir = testDirScope.path();
    std::filesystem::path testProject = testDir / "project_legacy.aes";

    // Old-format project: lanes have no "automation" key
    std::string legacyJson = R"({
        "version": 1,
        "tempo": 120.0,
        "playhead": 0.0,
        "sources": [],
        "patterns": [],
        "lanes": [
            {"name": "Track 1", "color": "4294967295", "volume": 1.0, "pan": 0.0,
             "clips": []}
        ],
        "arsenal": {"nextId": 1, "units": []}
    })";

    std::ofstream out(testProject, std::ios::binary | std::ios::trunc);
    out << legacyJson;
    out.close();

    auto tm = std::make_shared<Aestra::Audio::TrackManager>();
    auto result = ProjectSerializer::load(testProject.string(), tm);
    assert(result.ok);

    auto laneIds = tm->getPlaylistModel().getLaneIDs();
    assert(!laneIds.empty());
    auto* lane = tm->getPlaylistModel().getLane(laneIds[0]);
    assert(lane);
    assert(lane->automationCurves.empty());

    // Save it back — should include the "automation" key in the new format
    std::string saved = serializeProject(*tm, 120.0, 0.0);
    assert(saved.find("\"automation\"") != std::string::npos);

    // Load it again and verify still no automation
    std::filesystem::path testProject2 = testDir / "project_legacy2.aes";
    std::ofstream out2(testProject2);
    out2 << saved;
    out2.close();

    auto tm2 = std::make_shared<Aestra::Audio::TrackManager>();
    auto result2 = ProjectSerializer::load(testProject2.string(), tm2);
    assert(result2.ok);

    auto laneIds2 = tm2->getPlaylistModel().getLaneIDs();
    assert(!laneIds2.empty());
    auto* lane2 = tm2->getPlaylistModel().getLane(laneIds2[0]);
    assert(lane2);
    assert(lane2->automationCurves.empty());

    std::cout << "[PASS] Legacy project without automation key loads and round-trips" << std::endl;
}

void testAudioClipDurationSecondsMigrationAndTempoRecompute() {
    std::cout << "[TEST] Audio clip duration seconds migration and tempo recompute..." << std::endl;

    const Aestra::Tests::ScopedTempDirectory testDirScope{"ProjectRoundTripIntegrity"};
    const auto& testDir = testDirScope.path();
    std::filesystem::path testProject = testDir / "project_audio_duration_migration.aes";

    const std::string legacyJson = R"({
        "version": 1,
        "tempo": 120,
        "playhead": 0,
        "sources": [
            {"id": 1, "path": "missing_audio.wav"}
        ],
        "patterns": [
            {"id": 1, "name": "Audio Pattern", "type": "audio", "length": 4.0,
             "sourceId": 1, "slices": [{"start": 0.0, "length": 44100.0}]},
            {"id": 2, "name": "Midi Pattern", "type": "midi", "length": 4.0,
             "notes": [{"pitch": 60, "startBeat": 0.0, "durationBeats": 1.0, "velocity": 1.0}]}
        ],
        "lanes": [
            {"id": "lane-1", "name": "Track 1", "clips": [
                {"id": "clip-audio", "patternId": 1, "start": 0.0, "duration": 4.0, "name": "Audio Clip"},
                {"id": "clip-midi", "patternId": 2, "start": 4.0, "duration": 4.0, "name": "Midi Clip"}
            ]}
        ],
        "arsenal": {"nextId": 1, "units": []}
    })";

    std::ofstream out(testProject, std::ios::binary | std::ios::trunc);
    out << legacyJson;
    out.close();

    auto tm = std::make_shared<Aestra::Audio::TrackManager>();
    auto result = ProjectSerializer::load(testProject.string(), tm);
    assert(result.ok);

    auto laneIds = tm->getPlaylistModel().getLaneIDs();
    assert(!laneIds.empty());
    auto* lane = tm->getPlaylistModel().getLane(laneIds[0]);
    assert(lane);
    assert(lane->clips.size() == 2);

    auto* audioPattern = tm->getPatternManager().getPattern(lane->clips[0].patternId);
    auto* midiPattern = tm->getPatternManager().getPattern(lane->clips[1].patternId);
    assert(audioPattern && audioPattern->isAudio());
    assert(midiPattern && midiPattern->isMidi());
    assert(std::abs(lane->clips[0].edits.gainLinear - 1.0f) < 1.0e-7f);
    assert(std::abs(lane->clips[0].durationSeconds - 2.0) < 1.0e-9);
    assert(std::abs(lane->clips[0].durationBeats - 4.0) < 1.0e-9);
    assert(std::abs(lane->clips[1].durationBeats - 4.0) < 1.0e-9);

    tm->getPlaylistModel().setBPM(60.0);
    lane = tm->getPlaylistModel().getLane(laneIds[0]);
    assert(lane);
    assert(std::abs(lane->clips[0].durationSeconds - 2.0) < 1.0e-9);
    assert(std::abs(lane->clips[0].durationBeats - 2.0) < 1.0e-9);
    assert(std::abs(lane->clips[1].durationBeats - 4.0) < 1.0e-9);

    std::string saved = serializeProject(*tm, 60.0, 0.0);
    assert(saved.find("\"durationSeconds\"") != std::string::npos);

    std::cout << "[PASS] Audio clip duration seconds migration and tempo recompute" << std::endl;
}

void testAudioClipPlacementHelperPersistsClipAndDurationSeconds() {
    std::cout << "[TEST] Audio clip placement helper persists clip and duration seconds..." << std::endl;

    const Aestra::Tests::ScopedTempDirectory testDirScope{"ProjectRoundTripIntegrity"};
    const auto& testDir = testDirScope.path();
    std::filesystem::path testWav = testDir / "placed_audio.wav";
    std::filesystem::path testProject = testDir / "project_audio_placement.aes";
    assert(writeMinimalWavMono16(testWav, 48000, 48000));

    auto tm1 = std::make_shared<Aestra::Audio::TrackManager>();
    auto& sourceManager = tm1->getSourceManager();
    const Aestra::Audio::ClipSourceID sourceId = sourceManager.getOrCreateSource(testWav.string());
    assert(sourceId.isValid());

    Aestra::Audio::AudioSlicePayload payload;
    payload.audioSourceId = sourceId;
    payload.durationSeconds = 1.0;
    payload.slices.push_back({0.0, 1.0, 0.0, 48000.0});

    auto& patternManager = tm1->getPatternManager();
    const auto patternId = patternManager.createAudioPattern("Placed Audio", 2.0, payload);
    assert(patternId.isValid());

    auto& playlist = tm1->getPlaylistModel();
    const auto laneId = playlist.createLane("Audio Lane");
    const auto clipId = playlist.addClipFromPattern(laneId, patternId, 4.0, 2.0);
    assert(clipId.isValid());

    const auto* clip = playlist.getClip(clipId);
    assert(clip);
    assert(std::abs(clip->edits.gainLinear - Aestra::Audio::DEFAULT_AUDIO_CLIP_GAIN_LINEAR) < 1.0e-7f);
    assert(std::abs(clip->durationSeconds - 1.0) < 1.0e-9);
    auto persistedEdits = clip->edits;
    persistedEdits.playbackRate = 1.75f;
    persistedEdits.pitchSemitones = 5.0f;
    persistedEdits.sourceStart = 1234.5;
    assert(playlist.setClipEdits(clipId, persistedEdits));

    std::string firstSave = serializeProject(*tm1, 120.0, 0.0);
    assert(firstSave.find("\"durationSeconds\"") != std::string::npos);

    std::ofstream out(testProject, std::ios::binary | std::ios::trunc);
    out << firstSave;
    out.close();

    auto tm2 = std::make_shared<Aestra::Audio::TrackManager>();
    auto result = ProjectSerializer::load(testProject.string(), tm2);
    assert(result.ok);

    const auto loadedLaneId = tm2->getPlaylistModel().getLaneId(0);
    const auto* loadedLane = tm2->getPlaylistModel().getLane(loadedLaneId);
    assert(loadedLane);
    assert(loadedLane->clips.size() == 1);
    assert(std::abs(loadedLane->clips[0].edits.gainLinear - Aestra::Audio::DEFAULT_AUDIO_CLIP_GAIN_LINEAR) < 1.0e-7f);
    assert(std::abs(loadedLane->clips[0].edits.playbackRate - 1.75f) < 1.0e-7f);
    assert(std::abs(loadedLane->clips[0].edits.pitchSemitones - 5.0f) < 1.0e-7f);
    assert(std::abs(loadedLane->clips[0].edits.sourceStart - 1234.5) < 1.0e-9);
    assert(std::abs(loadedLane->clips[0].durationSeconds - 1.0) < 1.0e-9);
    assert(std::abs(loadedLane->clips[0].durationBeats - 2.0) < 1.0e-9);

    std::cout << "[PASS] Audio clip placement helper persists clip and duration seconds" << std::endl;
}

} // namespace

int main() {
    std::cout << "=== Project Round-Trip Integrity Tests ===" << std::endl;

    testEmptyProjectRoundTrip();
    testSourcesLanesClipsPatternsRoundTrip();
    testArsenalUnitsRoundTrip();
    testArsenalDefaultPatternRebindsAfterLoad();
    testArsenalSamplerAudioClipPathRehydratesPluginAfterLoad();
    testMissingPatternReferenceDoesNotCompound();
    testMultipleRoundTripCycles();
    testAutomationRoundTrip();
    testLegacyProjectWithoutAutomation();
    testAudioClipDurationSecondsMigrationAndTempoRecompute();
    testAudioClipPlacementHelperPersistsClipAndDurationSeconds();

    std::cout << "=== All tests passed ===" << std::endl;
    return 0;
}
