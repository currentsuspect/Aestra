// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Core/AutosaveManager.h"
#include "AestraLog.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include "../Support/TestTempDirectory.h"

namespace {

std::filesystem::path makeTempDir() {
    return Aestra::Tests::makeUniqueTempDirectory("AutosaveManager");
}

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}

} // namespace

int main() {
    using namespace Aestra::Audio;

    const auto tempDir = makeTempDir();
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

        manager.initialize("some_project.aes", std::move(config));
        require(manager.getAutosavePath() == autosavePath.string(),
                "autosavePathOverride should be respected");

        manager.markDirty();
        std::this_thread::sleep_for(std::chrono::seconds(2));
        manager.shutdown();

        require(std::filesystem::exists(autosavePath), "Autosave file should exist after background save");
        std::ifstream in(autosavePath, std::ios::binary);
        std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        require(contents == "test autosave data", "Autosave contents mismatch");
        std::cout << "[INFO] Autosave with override passed.\n";
    }

    // --- Test 2: markClean suppresses further autosaves ---
    {
        AutosaveManager manager;
        int serializeCount = 0;

        AutosaveManager::Config config;
        config.enabled = true;
        config.autosaveInterval = std::chrono::seconds(1);
        config.minDirtyDelay = std::chrono::seconds(0);
        config.autosavePathOverride = autosavePath.string();
        config.serializer = [&](std::string& outData) -> bool {
            outData = "updated data";
            ++serializeCount;
            return true;
        };

        manager.initialize("project2.aes", std::move(config));
        manager.markDirty();
        manager.markClean();
        std::this_thread::sleep_for(std::chrono::seconds(2));
        manager.shutdown();

        // The existing file from test 1 should still be there; test 2 should NOT have
        // overwritten it because markClean() was called before the thread woke.
        std::ifstream in(autosavePath, std::ios::binary);
        std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        require(contents == "test autosave data", "markClean should have suppressed autosave");
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

        manager.initialize("project3.aes", std::move(config));
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

        manager.initialize("project4.aes", std::move(config));
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
