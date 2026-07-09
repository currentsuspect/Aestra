// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// ProjectIntegrityCheckTest — corrupt project files must not load silently (#263).
//
// The serializer stamps a keyless FNV-1a-64 checksum over the canonical
// compact serialization (corruption detection, NOT tamper-proofing). This test
// pins the contract:
//   1. save -> load: Verified, no report issues.
//   2. a flipped value in the file: loads non-destructively, Mismatch, and the
//      structured report carries an "integrity" warning.
//   3. legacy files without the field (v1_rich.aes fixture): Unchecked, silent.
//   4. the autosave path (serialize + writeAtomically) carries the checksum too.

#include "../../Source/Core/ProjectSerializer.h"
#include "Models/TrackManager.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace {

int g_failures = 0;

void require(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        ++g_failures;
    }
}

std::filesystem::path makeTempDir() {
    auto base = std::filesystem::temp_directory_path() / "Aestra_tests";
    std::filesystem::create_directories(base);
    for (int i = 0; i < 1000; ++i) {
        auto candidate = base / ("IntegrityCheck_" + std::to_string(i));
        if (!std::filesystem::exists(candidate)) {
            std::filesystem::create_directories(candidate);
            return candidate;
        }
    }
    auto fallback = base / "IntegrityCheck_fallback";
    std::filesystem::create_directories(fallback);
    return fallback;
}

std::shared_ptr<Aestra::Audio::TrackManager> makeFreshManager() {
    auto tm = std::make_shared<Aestra::Audio::TrackManager>();
    tm->getPlaylistModel().setPatternManager(&tm->getPatternManager());
    return tm;
}

bool reportHasIntegrityIssue(const ProjectSerializer::LoadResult& load) {
    if (!load.report)
        return false;
    for (const auto& issue : load.report->issues) {
        if (issue.category == "integrity")
            return true;
    }
    return false;
}

} // namespace

int main() {
    using namespace Aestra::Audio;

    const auto tempDir = makeTempDir();
    std::cout << "[INFO] TempDir: " << tempDir.string() << "\n";

    // Build a small project.
    auto tm1 = makeFreshManager();
    tm1->getPlaylistModel().setBPM(133.25);
    tm1->getPlaylistModel().createLane("Integrity Lane");
    tm1->addChannel("Integrity Lane");

    // ---------------- 1. Clean save -> load: Verified, no report.
    const auto cleanPath = tempDir / "clean.aes";
    require(ProjectSerializer::save(cleanPath.string(), tm1, 133.25, 0.5), "save failed");
    {
        auto tm2 = makeFreshManager();
        auto load = ProjectSerializer::load(cleanPath.string(), tm2);
        require(load.ok, "clean load failed");
        require(load.integrity == ProjectSerializer::LoadIntegrity::Verified,
                "clean file did not verify (expected Verified)");
        require(!reportHasIntegrityIssue(load), "clean load raised an integrity issue");
    }

    // ---------------- 2. Corrupted value: loads, Mismatch, report issue.
    {
        std::ifstream in(cleanPath);
        std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        // Flip the tempo value in-place (structure stays valid, content lies).
        const auto pos = contents.find("133.25");
        require(pos != std::string::npos, "test setup: tempo literal not found in file");
        contents.replace(pos, 6, "233.25");

        const auto corruptPath = tempDir / "corrupt.aes";
        std::ofstream(corruptPath) << contents;

        auto tm3 = makeFreshManager();
        auto load = ProjectSerializer::load(corruptPath.string(), tm3);
        require(load.ok, "corrupted-value file must still load non-destructively");
        require(load.integrity == ProjectSerializer::LoadIntegrity::Mismatch,
                "corrupted file did not report Mismatch");
        require(reportHasIntegrityIssue(load), "mismatch missing from the structured load report");
    }

    // ---------------- 3. Legacy file without integrity field: Unchecked, silent.
    {
        const auto legacyPath = std::filesystem::path(AESTRA_PROJECT_FIXTURE_DIR) / "v1_rich.aes";
        require(std::filesystem::exists(legacyPath), "v1_rich.aes fixture missing");
        auto tm4 = makeFreshManager();
        auto load = ProjectSerializer::load(legacyPath.string(), tm4);
        require(load.ok, "legacy fixture load failed");
        require(load.integrity == ProjectSerializer::LoadIntegrity::Unchecked,
                "legacy file without integrity field must load Unchecked");
        require(!reportHasIntegrityIssue(load), "legacy load raised an integrity issue");
    }

    // ---------------- 4. Autosave path (serialize + writeAtomically) verifies too.
    {
        auto ser = ProjectSerializer::serialize(tm1, 133.25, 0.5, 0);
        require(ser.ok, "serialize failed");
        require(ser.contents.find("\"integrity\"") != std::string::npos,
                "autosave-style serialization missing integrity field");
        const auto autosavePath = tempDir / "autosave.aes";
        require(ProjectSerializer::writeAtomically(autosavePath.string(), ser.contents), "atomic write failed");
        auto tm5 = makeFreshManager();
        auto load = ProjectSerializer::load(autosavePath.string(), tm5);
        require(load.ok, "autosave load failed");
        require(load.integrity == ProjectSerializer::LoadIntegrity::Verified,
                "autosave did not verify (expected Verified)");
    }

    if (g_failures != 0) {
        std::cerr << "[FAIL] ProjectIntegrityCheckTest: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "[PASS] ProjectIntegrityCheckTest\n";
    return 0;
}
