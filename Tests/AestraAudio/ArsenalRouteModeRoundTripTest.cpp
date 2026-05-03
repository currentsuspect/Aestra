// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "../../Source/Core/ProjectSerializer.h"

#include "Models/TrackManager.h"
#include "Models/UnitManager.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}

std::filesystem::path makeTempDir() {
    auto base = std::filesystem::temp_directory_path() / "Aestra_tests";
    std::filesystem::create_directories(base);

    for (int i = 0; i < 1000; ++i) {
        auto candidate = base / ("ArsenalRouteModeRoundTrip_" + std::to_string(i));
        if (!std::filesystem::exists(candidate)) {
            std::filesystem::create_directories(candidate);
            return candidate;
        }
    }
    auto fallback = base / "ArsenalRouteModeRoundTrip_fallback";
    std::filesystem::create_directories(fallback);
    return fallback;
}

struct TempDir {
    std::filesystem::path path;
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

size_t countSubstring(const std::string& haystack, const std::string& needle) {
    size_t count = 0;
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

void verifyCurrentProjectRoundTrip(const std::filesystem::path& path) {
    using namespace Aestra::Audio;
    auto tm1 = std::make_shared<TrackManager>();
    tm1->getPlaylistModel().setPatternManager(&tm1->getPatternManager());
    tm1->addChannel("Track 1");

    UnitManager& um1 = tm1->getUnitManager();
    const UnitID timelineUnit = um1.createUnit("Timeline", UnitType::Sampler);
    const UnitID previewUnit = um1.createUnit("Preview", UnitType::Sampler);
    um1.assignUnitToTimelineLane(timelineUnit, 0);
    um1.clearUnitTimelineLane(previewUnit);

    require(ProjectSerializer::save(path.string(), tm1, 120.0, 0.0), "Failed to save project");

    const std::string saved = readFile(path);
    require(!saved.empty(), "Saved project is empty");
    require(saved.find("\"routeMode\"") != std::string::npos,
            "routeMode field should be serialized for arsenal units");

    auto tm2 = std::make_shared<TrackManager>();
    tm2->getPlaylistModel().setPatternManager(&tm2->getPatternManager());
    const auto loadResult = ProjectSerializer::load(path.string(), tm2);
    require(loadResult.ok, "Failed to load saved project");

    UnitManager& um2 = tm2->getUnitManager();
    const UnitInfo* loadedTimeline = um2.getUnit(timelineUnit);
    const UnitInfo* loadedPreview = um2.getUnit(previewUnit);
    require(loadedTimeline != nullptr, "Timeline unit missing after load");
    require(loadedPreview != nullptr, "Preview unit missing after load");
    require(loadedTimeline->targetMixerRoute == 0, "Timeline routeId mismatch after load");
    require(loadedPreview->targetMixerRoute < 0, "Preview routeId mismatch after load");
    require(loadedTimeline->routeMode == ArsenalRouteMode::RoutedToTimelineTrack,
            "Timeline routeMode mismatch after load");
    require(loadedPreview->routeMode == ArsenalRouteMode::PreviewToMaster,
            "Preview routeMode mismatch after load");

    const auto secondPath = path.parent_path() / "current_roundtrip_2.aes";
    require(ProjectSerializer::save(secondPath.string(), tm2, 120.0, 0.0), "Failed to save second project");
    const std::string secondSaved = readFile(secondPath);
    require(countSubstring(saved, "\"routeMode\"") == countSubstring(secondSaved, "\"routeMode\""),
            "routeMode field drifted across repeated round-trip saves");
}

void verifyLegacyRouteIdOnlyProjectLoad(const std::filesystem::path& path) {
    using namespace Aestra::Audio;
    const std::string legacy =
        "{\n"
        "  \"version\": 1,\n"
        "  \"tempo\": 120,\n"
        "  \"playhead\": 0,\n"
        "  \"lanes\": [],\n"
        "  \"arsenal\": {\n"
        "    \"nextId\": 3,\n"
        "    \"units\": [\n"
        "      {\n"
        "        \"id\": 1,\n"
        "        \"name\": \"Legacy Preview\",\n"
        "        \"enabled\": true,\n"
        "        \"targetMixerRoute\": -1\n"
        "      },\n"
        "      {\n"
        "        \"id\": 2,\n"
        "        \"name\": \"Legacy Track\",\n"
        "        \"enabled\": true,\n"
        "        \"targetMixerRoute\": 1\n"
        "      }\n"
        "    ]\n"
        "  }\n"
        "}\n";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << legacy;
    }

    auto tm = std::make_shared<TrackManager>();
    tm->getPlaylistModel().setPatternManager(&tm->getPatternManager());
    const auto loadResult = ProjectSerializer::load(path.string(), tm);
    require(loadResult.ok, "Legacy routeId-only project failed to load");

    UnitManager& um = tm->getUnitManager();
    const UnitInfo* preview = um.getUnit(1);
    const UnitInfo* track = um.getUnit(2);
    require(preview != nullptr, "Legacy preview unit missing");
    require(track != nullptr, "Legacy track unit missing");
    require(preview->routeMode == ArsenalRouteMode::PreviewToMaster,
            "Legacy preview unit routeMode should resolve from routeId");
    require(track->routeMode == ArsenalRouteMode::RoutedToTimelineTrack,
            "Legacy track unit routeMode should resolve from routeId");
}
} // namespace

int main() {
    const TempDir tempDir{makeTempDir()};
    verifyCurrentProjectRoundTrip(tempDir.path / "current_roundtrip.aes");
    verifyLegacyRouteIdOnlyProjectLoad(tempDir.path / "legacy_routeid_only.aes");

    std::cout << "[PASS] ArsenalRouteModeRoundTripTest\n";
    return 0;
}
