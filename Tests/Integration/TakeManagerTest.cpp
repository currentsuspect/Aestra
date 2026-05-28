// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "../../Source/Core/TakeManager.h"

#include "../../Source/Core/ProjectSerializer.h"
#include "Models/TrackManager.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::filesystem::path makeTempDir() {
    static std::atomic<uint64_t> counter{0};
    auto base = std::filesystem::temp_directory_path() / "Aestra_tests";
    std::filesystem::create_directories(base);

    const auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
    for (int attempt = 0; attempt < 100; ++attempt) {
        auto candidate = base / ("TakeManager_" + std::to_string(tid) + "_" + std::to_string(counter.fetch_add(1)));
        std::error_code ec;
        if (std::filesystem::create_directories(candidate, ec)) {
            return candidate;
        }
    }

    auto fallback = base / ("TakeManager_fallback_" + std::to_string(counter.fetch_add(1)));
    std::filesystem::create_directories(fallback);
    return fallback;
}

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}

std::shared_ptr<Aestra::Audio::TrackManager> makeProject(const std::string& laneName) {
    auto trackManager = std::make_shared<Aestra::Audio::TrackManager>();
    auto& playlist = trackManager->getPlaylistModel();
    playlist.setPatternManager(&trackManager->getPatternManager());

    auto laneId = playlist.createLane(laneName);
    require(laneId.isValid(), "Failed to create playlist lane");
    auto* channel = trackManager->addChannel(laneName);
    require(channel != nullptr, "Failed to create mixer channel");
    return trackManager;
}

void renameProjectLane(const std::shared_ptr<Aestra::Audio::TrackManager>& trackManager, const std::string& laneName) {
    auto& playlist = trackManager->getPlaylistModel();
    require(playlist.getLaneCount() == 1, "Expected a single lane before rename");

    auto laneId = playlist.getLaneId(0);
    auto* lane = playlist.getLane(laneId);
    require(lane != nullptr, "Lane missing before rename");
    lane->name = laneName;

    auto* channel = trackManager->getChannel(0);
    require(channel != nullptr, "Channel missing before rename");
    channel->setName(laneName);
}

std::string serializeProject(const std::shared_ptr<Aestra::Audio::TrackManager>& trackManager) {
    auto ser = ProjectSerializer::serialize(trackManager, 120.0, 0.0, 2);
    require(ser.ok, "Project serialization failed");
    require(!ser.contents.empty(), "Project serialization returned empty contents");
    return ser.contents;
}

std::string loadFirstLaneName(const std::string& projectPath) {
    auto trackManager = std::make_shared<Aestra::Audio::TrackManager>();
    trackManager->getPlaylistModel().setPatternManager(&trackManager->getPatternManager());

    auto result = ProjectSerializer::load(projectPath, trackManager);
    require(result.ok, "Project snapshot failed to load");
    require(trackManager->getPlaylistModel().getLaneCount() == 1, "Loaded snapshot lane count mismatch");

    const auto laneId = trackManager->getPlaylistModel().getLaneId(0);
    const auto* lane = trackManager->getPlaylistModel().getLane(laneId);
    require(lane != nullptr, "Loaded snapshot lane missing");
    return lane->name;
}

} // namespace

int main() {
    const auto tempDir = makeTempDir();
    const auto projectPath = tempDir / "takes_project.aes";

    auto trackManager = makeProject("Main Lane");
    const std::string mainContents = serializeProject(trackManager);
    require(ProjectSerializer::writeAtomically(projectPath.string(), mainContents),
            "Failed to write canonical project");

    auto init = TakeManager::ensureManifest(projectPath.string(), mainContents, "Main");
    if (!init.ok) {
        std::cerr << "[FAIL] Failed to initialize Takes manifest: " << init.errorMessage << "\n";
        std::exit(1);
    }
    require(init.manifest.takes.size() == 1, "Expected initial manifest to contain one take");
    require(init.manifest.activeTakeId == "main", "Expected Main take to be active");
    require(std::filesystem::exists(TakeManager::resolveSnapshotPath(projectPath.string(), init.take)),
            "Initial take snapshot was not written");

    TakeManager::TakeEntry malicious = init.take;
    malicious.snapshotPath = (tempDir / "outside.aes").string();
    require(TakeManager::resolveSnapshotPath(projectPath.string(), malicious).empty(),
            "Absolute take snapshot paths must be rejected");
    malicious.snapshotPath = "../outside.aes";
    require(TakeManager::resolveSnapshotPath(projectPath.string(), malicious).empty(),
            "Parent-relative take snapshot paths must be rejected");
    malicious.snapshotPath = ".";
    require(TakeManager::resolveSnapshotPath(projectPath.string(), malicious).empty(),
            "Directory-only take snapshot paths must be rejected");
#ifndef _WIN32
    malicious.snapshotPath = "../takes_project.takes\\outside.aes";
    require(TakeManager::resolveSnapshotPath(projectPath.string(), malicious).empty(),
            "POSIX backslash-prefixed take path escapes must be rejected");
#endif

    renameProjectLane(trackManager, "Variation Lane");
    const std::string variationContents = serializeProject(trackManager);
    auto created = TakeManager::createTake(projectPath.string(), variationContents, "Drum Bus Experiment");
    require(created.ok, "Failed to create variation Take");
    require(created.manifest.takes.size() == 2, "Expected manifest to contain two takes");
    require(created.manifest.activeTakeId == created.take.id, "Newly created take should be active");
    require(created.take.parentId == "main", "New take should record Main as parent");

    renameProjectLane(trackManager, "Variation Saved Lane");
    const std::string savedVariationContents = serializeProject(trackManager);
    auto saved = TakeManager::saveActiveTake(projectPath.string(), savedVariationContents);
    require(saved.ok, "Failed to save active Take");

    auto manifest = TakeManager::loadManifest(projectPath.string());
    require(manifest.ok, "Failed to reload Takes manifest");
    const auto* mainTake = manifest.findTake("main");
    const auto* variationTake = manifest.findTake(created.take.id);
    require(mainTake != nullptr, "Main take missing after reload");
    require(variationTake != nullptr, "Variation take missing after reload");

    require(loadFirstLaneName(TakeManager::resolveSnapshotPath(projectPath.string(), *mainTake)) == "Main Lane",
            "Main take snapshot did not preserve the original lane");
    require(loadFirstLaneName(TakeManager::resolveSnapshotPath(projectPath.string(), *variationTake)) ==
                "Variation Saved Lane",
            "Variation take snapshot did not preserve the saved variation lane");

    auto switched = TakeManager::setActiveTake(projectPath.string(), "main");
    require(switched.ok, "Failed to set active Take back to Main");
    auto switchedManifest = TakeManager::loadManifest(projectPath.string());
    require(switchedManifest.ok, "Failed to reload switched manifest");
    require(switchedManifest.activeTakeId == "main", "Active take id did not persist after switching");
    require(switchedManifest.activeTake() && switchedManifest.activeTake()->active,
            "Active take marker was not restored");

    std::filesystem::remove_all(tempDir);
    std::cout << "[PASS] TakeManagerTest\n";
    return 0;
}
