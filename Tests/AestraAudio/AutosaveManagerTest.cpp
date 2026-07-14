// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Core/AutosaveManager.h"

#include "../Support/TestTempDirectory.h"
#include "AestraLog.h"

#include <chrono>
#include <filesystem>
#include <fstream>
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

} // namespace

int main() {
    using namespace Aestra::Audio;

    const Aestra::Tests::ScopedTempDirectory tempDirScope{"AutosaveManager"};
    const auto& tempDir = tempDirScope.path();
    const auto autosavePath = tempDir / "override.autosave.aes";

    std::cout << "[INFO] TempDir: " << tempDir.string() << "\n";

    // --- Test 1: Autosave with path override ---
    {
        AutosaveManager manager;
        int serializeCount = 0;

        AutosaveManager::Config config;
        config.enabled = true;
        config.autosaveInterval = std::chrono::seconds(1);
        config.minDirtyDelay = std::chrono::seconds(0);
        config.autosavePathOverride = autosavePath.string();
        config.serializer = [&](std::string& outData) -> bool {
            outData = "test autosave data";
            ++serializeCount;
            return true;
        };

        manager.initialize((tempDir / "some_project.aes").string(), std::move(config));
        require(manager.getAutosavePath() == autosavePath.string(), "autosavePathOverride should be respected");

        manager.markDirty();
        std::this_thread::sleep_for(std::chrono::seconds(2));
        manager.shutdown();

        require(std::filesystem::exists(autosavePath), "Autosave file should exist after background save");
        std::ifstream in(autosavePath, std::ios::binary);
        std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        require(contents == "test autosave data", "Autosave contents mismatch");
        std::cout << "[INFO] Autosave with override passed.\n";
    }

    // --- Test 2: markClean suppresses a due autosave ---
    // Driven synchronously via autosaveIfDue() rather than sleeping against the
    // background thread: the previous version raced markClean() vs. the 1s autosave
    // timer, which flaked under ThreadSanitizer's 5-15x slowdown (issue #437). Here
    // there is no timing dependency at all. A dedicated path keeps Test 1's file
    // untouched.
    {
        const auto suppressPath = tempDir / "suppress.autosave.aes";
        AutosaveManager manager;
        int serializeCount = 0;

        AutosaveManager::Config config;
        config.enabled = false; // no background thread; we drive the gate explicitly
        config.autosaveInterval = std::chrono::seconds(1);
        config.minDirtyDelay = std::chrono::seconds(0);
        config.autosavePathOverride = suppressPath.string();
        config.serializer = [&](std::string& outData) -> bool {
            outData = "updated data";
            ++serializeCount;
            return true;
        };

        manager.initialize((tempDir / "project2.aes").string(), std::move(config));

        // markClean() after markDirty() must clear the pending-change state so a
        // due autosave is suppressed.
        manager.markDirty();
        manager.markClean();
        require(!manager.autosaveIfDue(), "markClean should have suppressed autosave");
        require(serializeCount == 0, "suppressed autosave must not serialize");
        require(!std::filesystem::exists(suppressPath), "suppressed autosave must not write a file");

        // Sanity: with changes genuinely pending, the same gate does autosave —
        // proving the suppression above was due to markClean(), not a dead path.
        manager.markDirty();
        require(manager.autosaveIfDue(), "a dirty project should autosave when due");
        require(serializeCount == 1, "exactly one serialize after the due autosave");
        std::ifstream in(suppressPath, std::ios::binary);
        std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        require(contents == "updated data", "due autosave should write serialized data");

        manager.shutdown();
        std::cout << "[INFO] markClean suppression passed.\n";
    }

    // --- Test 3: forceAutosave writes immediately ---
    {
        AutosaveManager manager;

        AutosaveManager::Config config;
        config.enabled = true;
        config.autosaveInterval = std::chrono::seconds(60);
        config.minDirtyDelay = std::chrono::seconds(5);
        config.autosavePathOverride = autosavePath.string();
        config.serializer = [&](std::string& outData) -> bool {
            outData = "forced data";
            return true;
        };

        manager.initialize((tempDir / "project3.aes").string(), std::move(config));
        bool ok = manager.forceAutosave();
        require(ok, "forceAutosave should succeed");
        manager.shutdown();

        std::ifstream in(autosavePath, std::ios::binary);
        std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        require(contents == "forced data", "forceAutosave should write immediately");
        std::cout << "[INFO] forceAutosave passed.\n";
    }

    // --- Test 4: Backup rotation ---
    {
        AutosaveManager manager;

        AutosaveManager::Config config;
        config.enabled = true;
        config.autosaveInterval = std::chrono::seconds(60);
        config.minDirtyDelay = std::chrono::seconds(0);
        config.maxBackupFiles = 2;
        config.autosavePathOverride = autosavePath.string();
        config.serializer = [&](std::string& outData) -> bool {
            outData = "rotation data";
            return true;
        };

        manager.initialize((tempDir / "project4.aes").string(), std::move(config));
        manager.forceAutosave();
        manager.forceAutosave();
        manager.forceAutosave();
        manager.shutdown();

        auto backupDir = tempDir / "override.autosave";
        int backupCount = 0;
        if (std::filesystem::exists(backupDir)) {
            for (const auto& entry : std::filesystem::directory_iterator(backupDir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".aes") {
                    ++backupCount;
                }
            }
        }
        require(backupCount <= 2, "Backup rotation should limit to maxBackupFiles");
        std::cout << "[INFO] Backup rotation passed (" << backupCount << " backups).\n";
    }

    std::cout << "[PASS] AutosaveManagerTest\n";
    return 0;
}
