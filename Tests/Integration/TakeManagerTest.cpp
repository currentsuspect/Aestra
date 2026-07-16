// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "../../Source/Core/TakeManager.h"

#include "../../Source/Core/ProjectSerializer.h"
#include "../Support/TestTempDirectory.h"
#include "Models/TrackManager.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

namespace {

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
    const Aestra::Tests::ScopedTempDirectory tempDirScope{"TakeManager"};
    const auto& tempDir = tempDirScope.path();
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

    // --- Rename: name changes persist, snapshot contents stay untouched ------
    auto renamed = TakeManager::renameTake(projectPath.string(), created.take.id, "  Drum Bus V2  ");
    require(renamed.ok, "Failed to rename Take");
    require(renamed.take.name == "Drum Bus V2", "Rename should trim whitespace");
    auto renamedManifest = TakeManager::loadManifest(projectPath.string());
    require(renamedManifest.ok, "Failed to reload manifest after rename");
    const auto* renamedTake = renamedManifest.findTake(created.take.id);
    require(renamedTake != nullptr && renamedTake->name == "Drum Bus V2", "Rename did not persist");
    require(loadFirstLaneName(TakeManager::resolveSnapshotPath(projectPath.string(), *renamedTake)) ==
                "Variation Saved Lane",
            "Rename must not modify the take snapshot");
    require(!TakeManager::renameTake(projectPath.string(), created.take.id, "   ").ok,
            "Blank take names must be rejected");
    require(!TakeManager::renameTake(projectPath.string(), "no_such_take", "X").ok,
            "Renaming a missing take must fail");

    // --- Duplicate: non-destructive copy, records lineage, stays inactive ----
    auto duplicated = TakeManager::duplicateTake(projectPath.string(), created.take.id);
    require(duplicated.ok, "Failed to duplicate Take");
    require(duplicated.take.parentId == created.take.id, "Duplicate should record its source as parent");
    require(duplicated.take.name == "Drum Bus V2 Copy", "Duplicate should derive a Copy name");
    require(duplicated.manifest.activeTakeId == "main", "Duplicating must not change the active take");
    require(duplicated.manifest.takes.size() == 3, "Duplicate should add a manifest entry");
    require(loadFirstLaneName(TakeManager::resolveSnapshotPath(projectPath.string(), duplicated.take)) ==
                "Variation Saved Lane",
            "Duplicate snapshot should be a copy of its source");
    require(loadFirstLaneName(TakeManager::resolveSnapshotPath(projectPath.string(), *renamedTake)) ==
                "Variation Saved Lane",
            "Duplicating must not modify the source snapshot");
    require(!TakeManager::duplicateTake(projectPath.string(), "no_such_take").ok,
            "Duplicating a missing take must fail");

    // --- Branch flow: duplicate + activate leaves every prior take intact ----
    auto branch = TakeManager::duplicateTake(projectPath.string(), "main", "Main Branch");
    require(branch.ok, "Failed to create branch duplicate");
    auto branchActive = TakeManager::setActiveTake(projectPath.string(), branch.take.id);
    require(branchActive.ok, "Failed to activate branch take");
    auto branchedManifest = TakeManager::loadManifest(projectPath.string());
    require(branchedManifest.ok, "Failed to reload manifest after branching");
    require(branchedManifest.activeTakeId == branch.take.id, "Branch take should be active");
    const auto* branchEntry = branchedManifest.findTake(branch.take.id);
    require(branchEntry != nullptr && branchEntry->parentId == "main", "Branch should descend from Main");
    const auto* mainAfterBranch = branchedManifest.findTake("main");
    require(mainAfterBranch != nullptr, "Main take must survive branching");
    require(loadFirstLaneName(TakeManager::resolveSnapshotPath(projectPath.string(), *mainAfterBranch)) ==
                "Main Lane",
            "Branching must not modify the Main snapshot");
    require(loadFirstLaneName(TakeManager::resolveSnapshotPath(projectPath.string(), *branchEntry)) == "Main Lane",
            "Branch snapshot should start from its source state");

    std::cout << "[PASS] TakeManagerTest\n";
    return 0;
}
