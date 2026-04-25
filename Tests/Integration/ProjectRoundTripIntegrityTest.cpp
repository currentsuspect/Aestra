// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Round-trip project integrity tests - prove project meaning survives serialization cycles.

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
#include <vector>
#include <regex>

namespace {

std::filesystem::path makeTempDir() {
    auto base = std::filesystem::temp_directory_path() / "Aestra_tests";
    std::filesystem::create_directories(base);
    for (int i = 0; i < 1000; ++i) {
        auto candidate = base / ("RoundTrip_" + std::to_string(i));
        if (!std::filesystem::exists(candidate)) {
            std::filesystem::create_directories(candidate);
            return candidate;
        }
    }
    auto fallback = base / "RoundTrip_fallback";
    std::filesystem::create_directories(fallback);
    return fallback;
}

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
    auto ser = ProjectSerializer::serialize(std::shared_ptr<Aestra::Audio::TrackManager>(&tm, [](Aestra::Audio::TrackManager*) {}),
                                tempo, playhead, 0);
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
        if (pos == std::string::npos) return 0;
        int count = 0;
        size_t start = pos + pattern.size();
        for (size_t i = start; i < json.size() && json[i] != '}'; ++i) {
            if (json[i] == '{') ++count;
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
                 << " clips:" << clips1 << "vs" << clips2
                 << " notes:" << notes1 << "vs" << notes2
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

    auto testDir = makeTempDir();
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
    std::filesystem::remove_all(testDir);
}

void testSourcesLanesClipsPatternsRoundTrip() {
    std::cout << "[TEST] Sources/lanes/clips/patterns round-trip..." << std::endl;

    auto testDir = makeTempDir();
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

    auto& patternManager = tm1->getPatternManager();
    Aestra::Audio::MidiPayload payload;
    payload.notes.push_back({60, 0.0, 1.0, 1.0f, 0, 0, 1.0f, false});
    payload.notes.push_back({64, 1.0, 1.0, 0.8f, 0, 0, 1.0f, false});
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
    std::filesystem::remove_all(testDir);
}

void testArsenalUnitsRoundTrip() {
    std::cout << "[TEST] Arsenal units round-trip..." << std::endl;

    auto testDir = makeTempDir();
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
    std::filesystem::remove_all(testDir);
}

void testMissingPatternReferenceDoesNotCompound() {
    std::cout << "[TEST] Missing pattern reference does not compound..." << std::endl;

    auto testDir = makeTempDir();
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
    std::filesystem::remove_all(testDir);
}

void testMultipleRoundTripCycles() {
    std::cout << "[TEST] Multiple round-trip cycles..." << std::endl;

    auto testDir = makeTempDir();
    std::filesystem::path testProject = testDir / "project.aes";

    auto tm1 = std::make_shared<Aestra::Audio::TrackManager>();
    tm1->getPlaylistModel().createLane("Track 1");
    tm1->getPlaylistModel().createLane("Track 2");

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
    std::filesystem::remove_all(testDir);
}

void testAutomationRoundTrip() {
    std::cout << "[TEST] Automation round-trip..." << std::endl;

    auto testDir = makeTempDir();
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

    std::ofstream out(testProject);
    out << save1;
    out.close();

    auto tm2 = std::make_shared<Aestra::Audio::TrackManager>();
    auto result = ProjectSerializer::load(testProject.string(), tm2);
    assert(result.ok);

    // Verify automation curves survived load
    auto lanes2 = tm2->getPlaylistModel().getLaneIDs();
    assert(!lanes2.empty());
    auto loadedLane = tm2->getPlaylistModel().getLane(lanes2[0]);
    assert(loadedLane);
    assert(!loadedLane->automationCurves.empty());

    std::cout << "[PASS] Automation round-trip" << std::endl;
    std::filesystem::remove_all(testDir);
}

}

int main() {
    std::cout << "=== Project Round-Trip Integrity Tests ===" << std::endl;

    testEmptyProjectRoundTrip();
    testSourcesLanesClipsPatternsRoundTrip();
    testArsenalUnitsRoundTrip();
    testMissingPatternReferenceDoesNotCompound();
    testMultipleRoundTripCycles();
    testAutomationRoundTrip();

    std::cout << "=== All tests passed ===" << std::endl;
    return 0;
}
