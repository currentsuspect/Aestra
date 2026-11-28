// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// ProjectSubfolderTest — renders and recordings go beside the project file, whatever the file is called.
//
// Reported by the founder: Bounce in Place failed with "Could not create render directory: .../Projects/EROS/Renders". Aestra saves
// projects without an extension, so Projects/EROS is a FILE; the folder rule asked only "does it have an extension?" and so took an
// extensionless file for a project folder, and tried to make a directory inside a file. The same helper decides where recordings go
// (recordingRootDirectory is private, so this tests it through renderRootDirectory).

#include "../Support/TestTempDirectory.h"
#include "Models/TrackManager.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace fs = std::filesystem;

namespace {
int g_failures = 0;

void check(bool condition, const std::string& label) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << label << std::endl;
    if (!condition) {
        ++g_failures;
    }
}
} // namespace

int main() {
    using namespace Aestra::Audio;
    const Aestra::Tests::ScopedTempDirectory scope{"ProjectSubfolder"};
    const fs::path dir = scope.path();

    {
        // The founder's case: a project saved with no extension is a file.
        const fs::path project = dir / "EROS";
        std::ofstream(project) << "{}";
        auto tm = std::make_shared<TrackManager>();
        tm->setRecordingProjectPath(project.string());
        check(fs::path(tm->renderRootDirectory()) == dir / "Renders", "an extensionless project FILE puts renders beside it");
        std::error_code ec;
        fs::create_directories(tm->renderRootDirectory(), ec);
        check(!ec, "and that folder can be created (it was a directory inside a file before)");
    }
    {
        // Unchanged: a project with an extension is a file beside which things go.
        auto tm = std::make_shared<TrackManager>();
        tm->setRecordingProjectPath((dir / "session.aes").string());
        check(fs::path(tm->renderRootDirectory()) == dir / "Renders", "a .aes project still puts renders beside it");
    }
    {
        // Unchanged: a project that is a folder holds them inside.
        const fs::path folder = dir / "FolderProject";
        fs::create_directories(folder);
        auto tm = std::make_shared<TrackManager>();
        tm->setRecordingProjectPath(folder.string());
        check(fs::path(tm->renderRootDirectory()) == folder / "Renders", "a project folder still holds its own renders");
    }
    {
        // Unchanged: an unsaved project, before it has a path, falls back under Documents/Aestra.
        auto tm = std::make_shared<TrackManager>();
        check(fs::path(tm->renderRootDirectory()).filename() == "Renders", "an unsaved project still gets a Renders folder");
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "project subfolder passed\n";
    return 0;
}
