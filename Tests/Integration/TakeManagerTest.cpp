// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "../../Source/Core/TakeManager.h"
#include "../../Source/Core/VersionBounce.h"
#include "../../Source/Core/VersionBringOver.h"

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

    // --- Delete: refuses the active take, reparents children, removes files --
    require(!TakeManager::deleteTake(projectPath.string(), branch.take.id).ok,
            "Deleting the active take must be refused");
    require(!TakeManager::deleteTake(projectPath.string(), "no_such_take").ok,
            "Deleting a missing take must fail");

    // duplicated (parent = renamed variation) must survive its parent's
    // deletion by moving up to the variation's parent ("main").
    const std::string dupSnapshotPath = TakeManager::resolveSnapshotPath(projectPath.string(), duplicated.take);
    const auto beforeDeleteManifest = TakeManager::loadManifest(projectPath.string());
    require(beforeDeleteManifest.ok, "Manifest should load before deletion");
    const auto* renamedBeforeDelete = beforeDeleteManifest.findTake(created.take.id);
    require(renamedBeforeDelete != nullptr, "Renamed take should exist before deletion");
    const std::string renamedSnapshotPath =
        TakeManager::resolveSnapshotPath(projectPath.string(), *renamedBeforeDelete);
    require(std::filesystem::exists(renamedSnapshotPath), "Renamed take snapshot should exist before deletion");

    auto deleted = TakeManager::deleteTake(projectPath.string(), created.take.id);
    require(deleted.ok, "Failed to delete a non-active take");
    auto afterDelete = TakeManager::loadManifest(projectPath.string());
    require(afterDelete.ok, "Manifest should reload after deletion");
    require(afterDelete.findTake(created.take.id) == nullptr, "Deleted take must leave the manifest");
    require(!std::filesystem::exists(renamedSnapshotPath), "Deleted take snapshot must be removed from disk");
    const auto* orphan = afterDelete.findTake(duplicated.take.id);
    require(orphan != nullptr, "Child of a deleted take must survive");
    require(orphan->parentId == "main", "Child of a deleted take must reparent to its grandparent");
    require(std::filesystem::exists(dupSnapshotPath), "Deleting a take must not touch other snapshots");
    require(afterDelete.activeTakeId == branch.take.id, "Deletion must not change the active take");

    std::cout << "[PASS] TakeManagerTest\n";
    // A bounce: the render is written beside the snapshot, its card info is kept, and the version points at it.
    {
        namespace VB = Aestra::VersionBounce;
        std::vector<float> tone;
        for (int f = 0; f < 8000; ++f) { const float a = f < 4000 ? 0.25f : 0.5f; tone.push_back(a); tone.push_back(-a); }
        const auto bars = VB::peaks(tone, 2, 8);
        require(bars.size() == 8 && bars.back() == 1.0f && bars.front() == 0.5f, "Peaks are scaled so the loudest bar is 1");
        require(VB::peaks({}, 2, 4) == std::vector<float>(4, 0.0f), "An empty render has flat peaks");
        require(VB::lengthLabel(161.4) == "2:41", "Length reads minutes and seconds");

        const std::string id = TakeManager::createVersion(projectPath.string(), mainContents, "Bounced", "", "", TakeManager::TakeEntry::Kind::Saved, {}).take.id;
        require(!id.empty(), "A version to bounce exists");
        const bool ok = VB::bounceVersion(projectPath.string(), id,
            [&](std::vector<float>& a, uint32_t& c, uint32_t& r) { a = tone; c = 2; r = 8000; return true; },
            [](const std::vector<float>&, uint32_t, uint32_t) { return -9.8; });
        require(ok, "A bounce is written");
        const auto manifest = TakeManager::loadManifest(projectPath.string());
        const auto* v = manifest.findTake(id);
        require(v && v->bounce == VB::fileNameFor(id), "The version records its bounce");
        const std::string wav = TakeManager::resolveBouncePath(projectPath.string(), *v);
        require(!wav.empty() && std::filesystem::file_size(wav) == 44 + tone.size() * 2, "The WAV is 16-bit PCM of the whole render");
        Aestra::VersionBounceInfo info;
        require(VB::readInfo(wav, info) && info.peaks.size() == VB::kBars && std::fabs(info.lufs + 9.8) < 0.01 && std::fabs(info.seconds - 1.0) < 1e-6, "The card info round-trips");
        require(!VB::bounceVersion(projectPath.string(), id, [](std::vector<float>&, uint32_t&, uint32_t&) { return false; }, nullptr), "An empty render makes no bounce");
        TakeManager::TakeEntry escape = *v;
        escape.bounce = "../outside.wav";
        require(TakeManager::resolveBouncePath(projectPath.string(), escape).empty(), "A bounce path cannot leave the takes folder");
    }

    // Bring over: choose tracks and patterns from another version; one undo takes them back out.
    {
        namespace BO = Aestra::BringOver;
        auto current = makeProject("Drums");
        auto other = makeProject("Drums");
        auto& op = other->getPatternManager();
        Aestra::Audio::MidiPayload hook;
        hook.notes.push_back({60, 0.0, 1.0, 0.8f});
        hook.notes.push_back({64, 1.0, 1.0, 0.8f});
        const auto hookId = op.createMidiPattern("Hook B", 4.0, hook);
        Aestra::Audio::MidiPayload lonely = hook;
        lonely.notes[0].unitId = 99;  // plays an instrument the open version does not have
        const auto lonelyId = op.createMidiPattern("Lonely", 4.0, lonely);
        auto& playlist = other->getPlaylistModel();
        const auto lane = playlist.createLane("Hook vox");
        other->createTrack(lane, "Hook vox");
        Aestra::Audio::ClipInstance clip;
        clip.patternId = hookId;
        clip.startBeat = 8.0;
        clip.durationBeats = 4.0;
        playlist.addClip(lane, clip);

        const auto plan = BO::plan(*other, *current, "Alt hook");
        require(plan.tracks.size() == 1 && plan.tracks[0].name == "Hook vox" && plan.tracks[0].status == BO::Status::New, "A track the open version lacks is listed as new");
        bool sawHook = false, sawLonely = false;
        for (const auto& it : plan.patterns) {
            if (it.name == "Hook B") sawHook = it.status == BO::Status::New && BO::defaultChecked(it);
            if (it.name == "Lonely") sawLonely = it.status == BO::Status::Blocked && !BO::defaultChecked(it);
        }
        require(sawHook && sawLonely, "New patterns start checked; ones needing a missing instrument are blocked");

        const size_t lanesBefore = current->getPlaylistModel().getLaneCount();
        auto cmd = std::make_shared<BO::Command>(*current, *other, std::vector<uint64_t>{plan.tracks[0].id}, std::vector<uint64_t>{});
        require(cmd->trackCount() == 1 && cmd->patternCount() == 1, "The command takes the track and the pattern its clip plays");
        cmd->execute();
        require(current->getPlaylistModel().getLaneCount() == lanesBefore + 1, "The track arrives as a lane");
        bool found = false;
        for (auto* t : current->getTracks()) found = found || t->name == "Hook vox";
        require(found, "...with its track");
        require(BO::plan(*other, *current, "Alt hook").tracks[0].status == BO::Status::Same, "Once brought over, the track reads as the same as current");
        cmd->undo();
        require(current->getPlaylistModel().getLaneCount() == lanesBefore, "One undo removes the lane");
        found = false;
        for (auto* t : current->getTracks()) found = found || t->name == "Hook vox";
        require(!found, "...and the track");
        bool patternGone = true;
        current->getPatternManager().forEachPattern([&](const Aestra::Audio::PatternSource& p) { patternGone = patternGone && p.name != "Hook B"; });
        require(patternGone, "...and the pattern it brought");
        cmd->redo();
        require(current->getPlaylistModel().getLaneCount() == lanesBefore + 1, "Redo brings it back");
        (void)lonelyId;
    }

    return 0;
}
