// © 2026 Aestra Studios — All Rights Reserved.
// .aes project load hardening: malformed files fail before destructive state changes.

#include "ProjectSerializer.h"
#include "TrackManager.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace fs = std::filesystem;
using Aestra::Audio::TrackManager;

namespace {

fs::path tempDir() {
    const fs::path dir = fs::temp_directory_path() / "aestra_project_load_hardening";
    fs::create_directories(dir);
    return dir;
}

void writeText(const fs::path& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << contents;
}

std::shared_ptr<TrackManager> makeNonEmptyTrackManager() {
    auto tm = std::make_shared<TrackManager>();
    tm->getPlaylistModel().createLane("Keep Me");
    tm->addChannel("Keep Me");
    return tm;
}

bool assertPreserved(const std::shared_ptr<TrackManager>& tm, const char* label) {
    if (tm->getPlaylistModel().getLaneCount() != 1 || tm->getChannelCount() != 1) {
        std::cerr << "  [FAIL] " << label << " cleared existing project state\n";
        return false;
    }
    return true;
}

bool expectRejectPreserves(const fs::path& path, const std::string& contents, const char* label) {
    writeText(path, contents);
    auto tm = makeNonEmptyTrackManager();
    const auto result = ProjectSerializer::load(path.string(), tm);
    if (result.ok) {
        std::cerr << "  [FAIL] " << label << " unexpectedly loaded\n";
        return false;
    }
    if (!assertPreserved(tm, label)) {
        return false;
    }
    std::cout << "  [PASS] " << label << " rejected without state loss: " << result.errorMessage << "\n";
    return true;
}

} // namespace

int main() {
    std::cout << "=== .aes project load hardening ===\n";

    const fs::path dir = tempDir();
    const fs::path projectPath = dir / "case.aes";
    bool ok = true;

    ok &= expectRejectPreserves(projectPath, "[]", "non-object root");
    ok &= expectRejectPreserves(projectPath,
        "{\"version\":1,\"tempo\":120,\"playhead\":0,\"lanes\":{}}",
        "lanes object instead of array");
    ok &= expectRejectPreserves(projectPath,
        "{\"version\":1,\"tempo\":1e999,\"playhead\":0,\"lanes\":[]}",
        "non-finite tempo");
    ok &= expectRejectPreserves(projectPath,
        "{\"version\":1,\"tempo\":120,\"playhead\":0,\"patterns\":[{\"id\":1,\"name\":\"x\",\"length\":4,\"type\":\"alien\"}],\"lanes\":[]}",
        "unsupported pattern type");
    ok &= expectRejectPreserves(projectPath,
        "{\"version\":1,\"tempo\":120,\"playhead\":0,\"lanes\":[{\"name\":\"x\",\"clips\":{}}]}",
        "clips object instead of array");

    std::string tooManyLanes = "{\"version\":1,\"tempo\":120,\"playhead\":0,\"lanes\":[";
    for (int i = 0; i < 2049; ++i) {
        if (i != 0) tooManyLanes += ",";
        tooManyLanes += "{\"name\":\"x\",\"clips\":[]}";
    }
    tooManyLanes += "]}";
    ok &= expectRejectPreserves(projectPath, tooManyLanes, "lane count cap");

    writeText(projectPath, "{\"version\":1,\"tempo\":120,\"playhead\":0,\"lanes\":[]}");
    auto emptyTarget = makeNonEmptyTrackManager();
    const auto valid = ProjectSerializer::load(projectPath.string(), emptyTarget);
    if (!valid.ok || emptyTarget->getPlaylistModel().getLaneCount() != 0) {
        std::cerr << "  [FAIL] valid minimal project did not load cleanly\n";
        ok = false;
    } else {
        std::cout << "  [PASS] valid minimal project still loads and commits\n";
    }

    fs::remove_all(dir);
    std::cout << "\n[" << (ok ? "PASS" : "FAIL") << "] .aes load hardening verified.\n";
    return ok ? 0 : 1;
}
