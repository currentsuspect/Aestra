// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "../../Source/Core/ProjectSerializer.h"
#include "Models/TrackManager.h"
#include "Models/UnitManager.h"
#include "Plugin/AestraEQ.h"
#include "Plugin/BuiltInPlugins.h"
#include "Plugin/PluginManager.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include "../Support/TestTempDirectory.h"

namespace {
void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}

std::filesystem::path makeTempDir() {
    return Aestra::Tests::makeUniqueTempDirectory("ArsenalRouteModeRoundTrip");
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
    tm1->getPlaylistModel().createLane("Track 1");
    tm1->getPlaylistModel().createLane("Arrangement only 2");
    tm1->getPlaylistModel().createLane("Arrangement only 3");
    auto* mixerChannel = tm1->addChannelWithId("Track 1", 42);
    require(mixerChannel && mixerChannel->getChannelId() == 42, "Failed to create stable mixer channel");

    AudioSlicePayload audioPayload;
    audioPayload.audioSourceId = tm1->getSourceManager().getOrCreateSource("missing-routing-test.wav");
    const PatternID audioPattern = tm1->getPatternManager().createAudioPattern("Routed audio", 4.0, audioPayload);
    require(tm1->getPatternManager().setPatternMixerChannel(audioPattern, 42),
            "Failed to route audio pattern before save");

    UnitManager& um1 = tm1->getUnitManager();
    const UnitID timelineUnit = um1.createUnit("Timeline", UnitType::Sampler);
    const UnitID previewUnit = um1.createUnit("Preview", UnitType::Sampler);
    um1.assignUnitToTimelineLane(timelineUnit, 0);
    um1.setUnitMixerChannel(timelineUnit, 42);
    um1.clearUnitTimelineLane(previewUnit);

    require(ProjectSerializer::save(path.string(), tm1, 120.0, 0.0), "Failed to save project");

    const std::string saved = readFile(path);
    require(!saved.empty(), "Saved project is empty");
    require(saved.find("\"routeMode\"") != std::string::npos, "routeMode field should be serialized for arsenal units");
    require(saved.find("\"targetMixerChannelId\"") != std::string::npos,
            "Stable unit mixer destination should be serialized");
    require(saved.find("\"mixerChannelId\"") != std::string::npos,
            "Stable mixer channel identity should be serialized");

    auto tm2 = std::make_shared<TrackManager>();
    tm2->getPlaylistModel().setPatternManager(&tm2->getPatternManager());
    const auto loadResult = ProjectSerializer::load(path.string(), tm2);
    require(loadResult.ok, "Failed to load saved project");

    UnitManager& um2 = tm2->getUnitManager();
    const UnitInfo* loadedTimeline = um2.getUnit(timelineUnit);
    const UnitInfo* loadedPreview = um2.getUnit(previewUnit);
    require(loadedTimeline != nullptr, "Timeline unit missing after load");
    require(loadedPreview != nullptr, "Preview unit missing after load");
    require(tm2->getChannelCount() == 1 && tm2->getChannel(0)->getChannelId() == 42,
            "Mixer channel identity mismatch after load");
    require(tm2->getPlaylistModel().getLaneCount() == 3, "Independent Playlist lane count mismatch after load");
    const auto* loadedAudioPattern = tm2->getPatternManager().getPattern(audioPattern);
    require(loadedAudioPattern && loadedAudioPattern->getMixerChannelId() == 42,
            "Audio-pattern mixer destination mismatch after load");
    require(loadedTimeline->targetMixerChannelId == 42, "Unit mixer destination mismatch after load");
    require(loadedPreview->targetMixerChannelId == MASTER_MIXER_CHANNEL_ID,
            "Preview unit should remain routed to Master");
    require(loadedTimeline->targetMixerRoute == 0, "Timeline routeId mismatch after load");
    require(loadedPreview->targetMixerRoute < 0, "Preview routeId mismatch after load");
    require(loadedTimeline->routeMode == ArsenalRouteMode::RoutedToTimelineTrack,
            "Timeline routeMode mismatch after load");
    require(loadedPreview->routeMode == ArsenalRouteMode::PreviewToMaster, "Preview routeMode mismatch after load");

    const auto secondPath = path.parent_path() / "current_roundtrip_2.aes";
    require(ProjectSerializer::save(secondPath.string(), tm2, 120.0, 0.0), "Failed to save second project");
    const std::string secondSaved = readFile(secondPath);
    require(countSubstring(saved, "\"routeMode\"") == countSubstring(secondSaved, "\"routeMode\""),
            "routeMode field drifted across repeated round-trip saves");
}

void verifyLegacyRouteIdOnlyProjectLoad(const std::filesystem::path& path) {
    using namespace Aestra::Audio;
    const std::string legacy = "{\n"
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

void verifyLegacySharedAudioPatternMigration(const std::filesystem::path& path) {
    using namespace Aestra::Audio;
    const std::string legacy = R"JSON({
  "version": 2,
  "tempo": 120,
  "playhead": 0,
  "sources": [{"id": 1, "path": "missing-legacy.wav", "name": "Legacy source"}],
  "patterns": [{
    "id": 7, "name": "Shared audio", "length": 4, "type": "audio",
    "sourceId": 1, "durationSeconds": 2, "slices": [{"start": 0, "length": 88200}]
  }],
  "lanes": [
    {"name": "Lane A", "mixerChannelId": 11, "clips": [{"patternId": 7, "start": 0, "duration": 4}]},
    {"name": "Lane B", "mixerChannelId": 42, "clips": [{"patternId": 7, "start": 8, "duration": 4}]}
  ]
})JSON";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << legacy;
    }

    auto tm = std::make_shared<TrackManager>();
    const auto loadResult = ProjectSerializer::load(path.string(), tm);
    require(loadResult.ok, "Legacy shared-audio project failed to load");
    require(tm->getChannelCount() == 2, "Legacy lane inserts were not restored");
    require(tm->getPlaylistModel().getLaneCount() == 2, "Legacy lanes were not restored");

    const auto* laneA = tm->getPlaylistModel().getLane(tm->getPlaylistModel().getLaneId(0));
    const auto* laneB = tm->getPlaylistModel().getLane(tm->getPlaylistModel().getLaneId(1));
    require(laneA && laneB && laneA->clips.size() == 1 && laneB->clips.size() == 1,
            "Legacy shared clips were not restored");
    require(laneA->clips[0].patternId != laneB->clips[0].patternId,
            "Legacy source was not cloned to preserve different lane destinations");
    const auto* sourceA = tm->getPatternManager().getPattern(laneA->clips[0].patternId);
    const auto* sourceB = tm->getPatternManager().getPattern(laneB->clips[0].patternId);
    require(sourceA && sourceB && sourceA->getMixerChannelId() == 11 && sourceB->getMixerChannelId() == 42,
            "Legacy lane routes were not preserved as source destinations");
}

void verifyEQCurrentProjectRoundTrip(const std::filesystem::path& path) {
    using namespace Aestra::Audio;
    BuiltInPlugins::registerCoreBuiltIns();
    require(PluginManager::getInstance().initialize(), "Failed to initialize PluginManager");

    auto tm1 = std::make_shared<TrackManager>();
    tm1->getPlaylistModel().setPatternManager(&tm1->getPatternManager());

    UnitManager& um1 = tm1->getUnitManager();
    const UnitID eqUnit = um1.createUnit("Project EQ", UnitType::Audio);
    auto eq = std::make_shared<Plugins::AestraEQ>();
    eq->setInfo(BuiltInPlugins::eqInfo());
    require(eq->initialize(48000.0, 512), "Failed to initialize project EQ");
    eq->setParameter(Plugins::AestraEQ::kParamBell1Enable, 1.0f);
    eq->setParameter(Plugins::AestraEQ::kParamBell1Freq, 0.43f);
    eq->setParameter(Plugins::AestraEQ::kParamBell1Q, 0.35f);
    eq->setParameter(Plugins::AestraEQ::kParamBell1Type, 1.0f / 3.0f);
    eq->setParameter(Plugins::AestraEQ::kParamBell2Enable, 1.0f);
    eq->setParameter(Plugins::AestraEQ::kParamBell2Freq, 0.66f);
    eq->setParameter(Plugins::AestraEQ::kParamBell2Q, 0.50f);
    eq->setParameter(Plugins::AestraEQ::kParamBell2Type, 2.0f / 3.0f);
    eq->setParameter(Plugins::AestraEQ::kParamOutputGain, 0.75f);
    eq->setParameter(Plugins::AestraEQ::kParamPolarityInvert, 1.0f);
    require(eq->setDynamicBandSlot(Plugins::AestraEQ::kLegacyBandCount,
                                   {/* enabled */ true,
                                    /* type */ Plugins::FilterType::Notch,
                                    /* stereoMode */ Plugins::AestraEQ::StereoMode::Mid,
                                    /* frequencyNorm */ 0.62f,
                                    /* gainNorm */ 0.40f,
                                    /* qOrSlopeNorm */ 0.35f,
                                    /* usesSlope */ false,
                                    /* dynamicEnabled */ true,
                                    /* targetGainNorm */ 0.72f,
                                    /* thresholdNorm */ 0.44f,
                                    /* kneeNorm */ 0.18f,
                                    /* attackNorm */ 0.12f,
                                    /* releaseNorm */ 0.60f,
                                    /* sidechainLinked */ false,
                                    /* sidechainType */ Plugins::FilterType::BandPass,
                                    /* sidechainFrequencyNorm */ 0.31f,
                                    /* sidechainQNorm */ 0.44f}),
            "Failed to configure dynamic project EQ slot");

    um1.attachPlugin(eqUnit, BuiltInPlugins::eqInfo().id, eq);
    um1.captureUnitPluginState(eqUnit);

    require(ProjectSerializer::save(path.string(), tm1, 120.0, 0.0), "Failed to save EQ project");
    const std::string saved = readFile(path);
    require(saved.find("\"pluginStateHex\"") != std::string::npos, "EQ plugin state should be serialized in project");

    auto tm2 = std::make_shared<TrackManager>();
    tm2->getPlaylistModel().setPatternManager(&tm2->getPatternManager());
    const auto loadResult = ProjectSerializer::load(path.string(), tm2);
    require(loadResult.ok, "Failed to load EQ project");

    UnitManager& um2 = tm2->getUnitManager();
    require(um2.getUnitPluginId(eqUnit) == BuiltInPlugins::eqInfo().id, "EQ plugin id mismatch after project load");
    auto restoredEq = std::dynamic_pointer_cast<Plugins::AestraEQ>(um2.getUnitPlugin(eqUnit));
    require(restoredEq != nullptr, "EQ plugin instance missing after project load");
    require(std::abs(restoredEq->getParameter(Plugins::AestraEQ::kParamBell1Type) - (1.0f / 3.0f)) < 0.001f,
            "Bell 1 type should round-trip as Notch through project state");
    require(std::abs(restoredEq->getParameter(Plugins::AestraEQ::kParamBell2Type) - (2.0f / 3.0f)) < 0.001f,
            "Bell 2 type should round-trip as Band Pass through project state");
    require(std::abs(restoredEq->getParameter(Plugins::AestraEQ::kParamOutputGain) - 0.75f) < 0.001f,
            "Output gain should round-trip through project state");
    require(restoredEq->getParameter(Plugins::AestraEQ::kParamPolarityInvert) == 1.0f,
            "Polarity invert should round-trip through project state");
    const auto restoredDynamic = restoredEq->getDynamicBandSlotSnapshot(Plugins::AestraEQ::kLegacyBandCount);
    require(restoredDynamic.enabled, "Dynamic EQ slot should round-trip enabled through project state");
    require(restoredDynamic.dynamicEnabled, "Dynamic EQ mode should round-trip through project state");
    require(restoredDynamic.stereoMode == Plugins::AestraEQ::StereoMode::Mid,
            "Dynamic EQ stereo mode should round-trip through project state");
    require(!restoredDynamic.sidechainLinked,
            "Dynamic EQ sidechain link state should round-trip through project state");
    require(std::abs(restoredDynamic.sidechainFrequencyNorm - 0.31f) < 0.001f,
            "Dynamic EQ sidechain frequency should round-trip through project state");

    const auto secondPath = path.parent_path() / "eq_current_roundtrip_2.aes";
    require(ProjectSerializer::save(secondPath.string(), tm2, 120.0, 0.0), "Failed to save reloaded EQ project");

    auto tm3 = std::make_shared<TrackManager>();
    tm3->getPlaylistModel().setPatternManager(&tm3->getPatternManager());
    const auto secondLoadResult = ProjectSerializer::load(secondPath.string(), tm3);
    require(secondLoadResult.ok, "Failed to reload EQ project");

    auto secondRestoredEq = std::dynamic_pointer_cast<Plugins::AestraEQ>(tm3->getUnitManager().getUnitPlugin(eqUnit));
    require(secondRestoredEq != nullptr, "EQ plugin instance missing after second project load");
    require(std::abs(secondRestoredEq->getParameter(Plugins::AestraEQ::kParamBell1Type) - (1.0f / 3.0f)) < 0.001f,
            "Bell 1 type drifted across repeated project round-trip saves");
    require(std::abs(secondRestoredEq->getParameter(Plugins::AestraEQ::kParamBell2Type) - (2.0f / 3.0f)) < 0.001f,
            "Bell 2 type drifted across repeated project round-trip saves");
    require(std::abs(secondRestoredEq->getParameter(Plugins::AestraEQ::kParamOutputGain) - 0.75f) < 0.001f,
            "Output gain drifted across repeated project round-trip saves");
    require(secondRestoredEq->getParameter(Plugins::AestraEQ::kParamPolarityInvert) == 1.0f,
            "Polarity invert drifted across repeated project round-trip saves");
    const auto secondDynamic = secondRestoredEq->getDynamicBandSlotSnapshot(Plugins::AestraEQ::kLegacyBandCount);
    require(secondDynamic.enabled && secondDynamic.dynamicEnabled && !secondDynamic.sidechainLinked,
            "Dynamic EQ slot drifted across repeated project round-trip saves");
}
} // namespace

int main() {
    const TempDir tempDir{makeTempDir()};
    verifyCurrentProjectRoundTrip(tempDir.path / "current_roundtrip.aes");
    verifyLegacyRouteIdOnlyProjectLoad(tempDir.path / "legacy_routeid_only.aes");
    verifyLegacySharedAudioPatternMigration(tempDir.path / "legacy_shared_audio.aes");
    verifyEQCurrentProjectRoundTrip(tempDir.path / "eq_current_roundtrip.aes");

    std::cout << "[PASS] ArsenalRouteModeRoundTripTest\n";
    return 0;
}
