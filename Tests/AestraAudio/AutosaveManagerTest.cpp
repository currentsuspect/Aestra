// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Core/AutosaveManager.h"

#include "../Support/TestTempDirectory.h"
#include "AestraLog.h"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
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
    // forceAutosave() runs performAutosave() synchronously on the calling thread and
    // does not depend on the background worker, so we leave the worker disabled here.
    // Running it would only spin up a thread whose sole activity during this test is a
    // startup-banner log; its heap allocation races this thread's rotateBackups() free
    // under TSan (different mutexes → no happens-before) even though the production
    // write+rotate path is itself serialized by m_commitMutex (issue #564).
    {
        AutosaveManager manager;

        AutosaveManager::Config config;
        config.enabled = false;
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
    // Driven synchronously through forceAutosave() (see Test 3): the background worker
    // is left disabled so its startup-banner log cannot race these rotateBackups() frees
    // under TSan (issue #564). Rotation itself is fully exercised on the calling thread.
    {
        AutosaveManager manager;

        AutosaveManager::Config config;
        config.enabled = false;
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

        auto backupDir = tempDir / "override.autosave.autosave";
        int backupCount = 0;
        if (std::filesystem::exists(backupDir)) {
            for (const auto& entry : std::filesystem::directory_iterator(backupDir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".aes") {
                    ++backupCount;
                }
            }
        }
        require(backupCount <= 2, "Backup rotation should limit to maxBackupFiles");
        const auto recoveryBackups = AutosaveManager::listBackupsForAutosavePath(autosavePath.string());
        require(!recoveryBackups.empty(), "rotated backups must be discoverable from the explicit autosave path");
        require(static_cast<int>(recoveryBackups.size()) == backupCount,
                "recovery backup discovery should match the files retained by rotation");
        std::cout << "[INFO] Backup rotation passed (" << backupCount << " backups).\n";
    }

    // --- Test 5: a change made during the save must not be lost ---
    // The autosave path claims the dirty flag before the (slow) serialize. If a
    // markDirty() lands mid-save, the project must stay dirty so the next cycle
    // catches it — rather than being cleared by an unconditional markClean().
    {
        const auto concurrentPath = tempDir / "concurrent.autosave.aes";
        AutosaveManager manager;
        bool markedDuringSave = false;

        AutosaveManager::Config config;
        config.enabled = false; // drive synchronously; no background thread
        config.minDirtyDelay = std::chrono::seconds(0);
        config.autosavePathOverride = concurrentPath.string();
        config.serializer = [&](std::string& outData) -> bool {
            outData = "concurrent data";
            // Simulate a user edit arriving while the save is in flight.
            if (!markedDuringSave) {
                markedDuringSave = true;
                manager.markDirty();
            }
            return true;
        };

        manager.initialize((tempDir / "project5.aes").string(), std::move(config));
        manager.markDirty();
        require(manager.autosaveIfDue(), "autosave should run for a dirty project");
        require(markedDuringSave, "serializer should have observed the save in progress");
        require(manager.isDirty(), "a change made during the save must keep the project dirty");
        manager.shutdown();
        std::cout << "[INFO] concurrent-dirty preservation passed.\n";
    }

    // --- Test 6: mutable model capture stays on its owner thread ---
    // Aestra's ProjectSerializer traverses TrackManager and related mutable
    // models. The autosave worker may write the captured string, but it must
    // never invoke that traversal concurrently with active edits.
    {
        const auto ownerCapturePath = tempDir / "owner-capture.autosave.aes";
        const auto ownerThread = std::this_thread::get_id();
        std::thread::id serializerThread;
        std::thread::id commitThread;
        std::mutex commitMutex;
        std::condition_variable commitCv;
        bool committed = false;
        bool markedDuringCapture = false;

        AutosaveManager manager;
        AutosaveManager::Config config;
        config.enabled = true;
        config.autosaveInterval = std::chrono::seconds(60);
        config.minDirtyDelay = std::chrono::seconds(0);
        config.autosavePathOverride = ownerCapturePath.string();
        config.captureSnapshotOnCallingThread = true;
        config.serializer = [&](std::string& outData) -> bool {
            serializerThread = std::this_thread::get_id();
            outData = "immutable owner-thread snapshot";
            markedDuringCapture = true;
            manager.markDirty();
            return true;
        };
        config.onAutosaveCommitted = [&](const std::string&) {
            {
                std::lock_guard<std::mutex> lock(commitMutex);
                commitThread = std::this_thread::get_id();
                committed = true;
            }
            commitCv.notify_one();
        };

        manager.initialize((tempDir / "project6.aes").string(), std::move(config));
        manager.markDirty();
        require(manager.captureSnapshotIfDue(), "owner thread should capture a due autosave snapshot");
        require(serializerThread == ownerThread, "serializer must run on the model-owner thread");
        require(markedDuringCapture && manager.isDirty(), "an edit during snapshot capture must remain pending");

        {
            std::unique_lock<std::mutex> lock(commitMutex);
            require(commitCv.wait_for(lock, std::chrono::seconds(5), [&] { return committed; }),
                    "background autosave commit timed out");
        }
        manager.shutdown();

        require(commitThread != ownerThread, "immutable snapshot file commit should run on the worker thread");
        std::ifstream in(ownerCapturePath, std::ios::binary);
        std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        require(contents == "immutable owner-thread snapshot", "owner-thread snapshot contents mismatch");
        std::cout << "[INFO] owner-thread snapshot capture passed.\n";
    }

    std::cout << "[PASS] AutosaveManagerTest\n";
    return 0;
}
