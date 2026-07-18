// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "ProjectDocumentState.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        std::exit(1);
    }
}

} // namespace

int main() {
    using Aestra::ProjectDocumentState;

    ProjectDocumentState state;
    state.startUntitled("/app-data/autosave.aes");
    require(state.canonicalPath().empty(), "untitled project acquired a canonical path");
    require(state.autosavePath() == "/app-data/autosave.aes", "untitled autosave identity was lost");
    require(state.requiresSaveAs(), "ordinary Save must require Save As for an untitled project");

    state.recoverFrom("/app-data/autosave.aes");
    require(state.isRecovered(), "recovery source kind was not retained");
    require(state.recoveryPath() == "/app-data/autosave.aes", "recovery source path was not retained");
    require(state.canonicalPath().empty(), "legacy recovery silently treated autosave as canonical");
    require(state.requiresSaveAs(), "legacy recovery without an original path must require Save As");

    state.recoverFrom("/app-data/autosave.aes", "/projects/song.aes");
    require(state.isRecovered(), "recovered canonical project lost its recovery state");
    require(state.canonicalPath() == "/projects/song.aes", "original canonical path was not restored");
    require(state.recoveryPath() == "/app-data/autosave.aes", "canonical and recovery identities collapsed");
    require(!state.requiresSaveAs(), "recovered named project should save to its original canonical path");
    require(state.autosavePath() == "/app-data/autosave.aes", "recovery changed the autosave destination");

    state.restoreSnapshot("/projects/song.history/snapshot.aes", "/projects/song.aes");
    require(state.isSnapshotRestore(), "snapshot source kind was not retained");
    require(state.snapshotPath() == "/projects/song.history/snapshot.aes", "snapshot path was not retained");
    require(state.canonicalPath() == "/projects/song.aes", "snapshot replaced the canonical identity");

    state.adoptCanonical("/projects/song-recovered.aes");
    require(state.sourceKind() == ProjectDocumentState::SourceKind::Canonical,
            "successful Save As did not establish a canonical document");
    require(state.canonicalPath() == "/projects/song-recovered.aes", "Save As canonical path was not retained");
    require(state.recoveryPath().empty() && state.snapshotPath().empty(),
            "transient recovery or snapshot identity survived canonical save");
    require(!state.requiresSaveAs(), "canonical document still requires Save As");
    require(state.autosavePath() == "/app-data/autosave.aes", "Save As collapsed canonical and autosave identities");

    state.protectCanonicalFromOverwrite();
    require(state.isOverwriteProtected(), "integrity mismatch did not protect the canonical project");
    require(state.requiresSaveAs(), "protected canonical project must not be overwritten by ordinary Save");

    state.adoptCanonical("/projects/song-reviewed.aes");
    require(!state.isOverwriteProtected(), "successful Save As did not clear overwrite protection");
    require(!state.requiresSaveAs(), "reviewed Save As target should become canonical");

    std::cout << "[PASS] ProjectDocumentStateTest\n";
    return 0;
}
