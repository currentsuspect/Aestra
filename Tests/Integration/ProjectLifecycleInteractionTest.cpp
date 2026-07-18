// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Core/AutosaveManager.h"
#include "Models/TrackManager.h"
#include "ProjectDocumentState.h"
#include "ProjectSerializer.h"
#include "Support/TestTempDirectory.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        std::exit(1);
    }
}

std::shared_ptr<Aestra::Audio::TrackManager> makeProject(const std::string& name) {
    auto tracks = std::make_shared<Aestra::Audio::TrackManager>();
    tracks->getPlaylistModel().setPatternManager(&tracks->getPatternManager());
    tracks->getPlaylistModel().createLane(name);
    tracks->addChannel(name);
    return tracks;
}

} // namespace

int main() {
    using Aestra::Audio::AutosaveManager;

    const Aestra::Tests::ScopedTempDirectory tempDir{"ProjectLifecycleInteraction"};
    const auto canonicalPath = tempDir.path() / "song.aes";
    const auto reviewedPath = tempDir.path() / "song-reviewed.aes";
    const auto autosavePath = tempDir.path() / "autosave.aes";
    const auto backupDir = tempDir.path() / "autosave.autosave";
    const auto backupPath = backupDir / "20260718_120000.aes";

    auto source = makeProject("Lifecycle Lane");
    const auto serialized = ProjectSerializer::serialize(source, 126.0, 2.5, 0);
    require(serialized.ok, "production serializer failed to create the fixture project");
    require(ProjectSerializer::writeAtomically(canonicalPath.string(), serialized.contents),
            "canonical project write failed");

    Aestra::ProjectDocumentState document;
    document.startUntitled(autosavePath.string());
    require(document.requiresSaveAs(), "untitled Save must route through Save As");
    require(document.windowTitle() == "Untitled - Aestra", "untitled title is not user-visible");

    document.adoptCanonical(canonicalPath.string());
    require(!document.requiresSaveAs(), "successful Save As did not establish direct Save routing");
    require(document.windowTitle() == canonicalPath.string() + " - Aestra", "canonical title lost its path");

    std::filesystem::create_directories(backupDir);
    std::filesystem::copy_file(canonicalPath, backupPath, std::filesystem::copy_options::overwrite_existing);
    {
        std::ofstream primary(autosavePath, std::ios::binary | std::ios::trunc);
        primary << "{\"version\":";
    }

    std::vector<std::string> candidates{autosavePath.string()};
    const auto backups = AutosaveManager::listBackupsForAutosavePath(autosavePath.string());
    candidates.insert(candidates.end(), backups.begin(), backups.end());

    auto recoveredTracks = makeProject("State That Must Be Replaced");
    auto selected = ProjectSerializer::loadFirstValid(candidates, recoveredTracks, canonicalPath.string());
    require(selected.result.ok, "recovery rejected every candidate");
    require(selected.loadedPath == backupPath.string(), "recovery did not fall back from corrupt primary");
    require(recoveredTracks->getChannelCount() == 1, "recovery did not replace the active project model");
    require(recoveredTracks->getChannel(0) && recoveredTracks->getChannel(0)->getName() == "Lifecycle Lane",
            "recovery left the pre-existing session model in place");

    document.recoverFrom(selected.loadedPath, canonicalPath.string());
    require(!document.requiresSaveAs(), "named recovery did not retain direct Save routing");
    require(document.windowTitle() == "Recovered - " + canonicalPath.string() + " - Aestra",
            "recovery state is not visible in the title");

    std::string mismatchedContents = serialized.contents;
    const auto namePos = mismatchedContents.find("Lifecycle Lane");
    require(namePos != std::string::npos, "fixture project did not contain its channel name");
    mismatchedContents.replace(namePos, std::string("Lifecycle Lane").size(), "Lifecycle Lame");
    require(ProjectSerializer::writeAtomically(canonicalPath.string(), mismatchedContents),
            "integrity-mismatch fixture write failed");

    auto integrityTracks = makeProject("State That Must Be Replaced");
    const auto mismatch = ProjectSerializer::load(canonicalPath.string(), integrityTracks);
    require(mismatch.ok, "recoverable integrity mismatch should remain loadable");
    require(mismatch.integrity == ProjectSerializer::LoadIntegrity::Mismatch,
            "production serializer did not expose the integrity mismatch");

    document.openCanonical(canonicalPath.string());
    document.protectCanonicalFromOverwrite();
    require(document.requiresSaveAs(), "integrity mismatch did not block ordinary overwrite");
    require(document.windowTitle() == "Integrity Warning - " + canonicalPath.string() + " - Aestra",
            "integrity warning is not visible in the application title");

    document.adoptCanonical(reviewedPath.string());
    require(!document.requiresSaveAs(), "reviewed Save As target did not restore direct Save routing");
    require(!document.isOverwriteProtected(), "reviewed Save As did not clear overwrite protection");

    std::cout << "[PASS] ProjectLifecycleInteractionTest\n";
    return 0;
}
