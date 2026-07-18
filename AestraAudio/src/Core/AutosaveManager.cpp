// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "AutosaveManager.h"
#include "AestraFile.h"
#include "AestraLog.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace fs = std::filesystem;

namespace Aestra {
namespace Audio {

namespace {
// Monotonic milliseconds since the steady_clock epoch. Used for the dirty-time
// stamp so it can live in an atomic and be compared without the mutex.
int64_t steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
} // namespace

//==============================================================================
// RecoveryInfo
//==============================================================================

std::string AutosaveManager::RecoveryInfo::getAgeString() const {
    if (!valid) return "invalid";
    
    auto now = std::chrono::system_clock::now();
    auto age = now - timestamp;
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(age).count();
    auto hours = minutes / 60;
    auto days = hours / 24;
    
    if (days > 0) {
        return std::to_string(days) + " day" + (days > 1 ? "s" : "") + " ago";
    } else if (hours > 0) {
        return std::to_string(hours) + " hour" + (hours > 1 ? "s" : "") + " ago";
    } else if (minutes > 0) {
        return std::to_string(minutes) + " minute" + (minutes > 1 ? "s" : "") + " ago";
    } else {
        return "just now";
    }
}

//==============================================================================
// Lifecycle
//==============================================================================

AutosaveManager::AutosaveManager() = default;

AutosaveManager::~AutosaveManager() {
    shutdown();
}

void AutosaveManager::initialize(const std::string& projectPath, Config config) {
    // Check if already initialized (without lock first to avoid deadlock with shutdown)
    if (m_initialized.load(std::memory_order_acquire)) {
        Log::warning("AutosaveManager already initialized, shutting down first");
        shutdown();
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!config.serializer) {
        Log::error("AutosaveManager initialized without serializer callback");
        return;
    }
    
    m_projectPath = projectPath;
    m_config = std::move(config);
    
    m_isDirty = false;
    m_isAutosaving = false;
    m_shouldStop = false;
    m_enabled.store(m_config.enabled, std::memory_order_release);
    m_nextSnapshotGeneration.store(1, std::memory_order_release);
    m_pendingSnapshot.clear();
    m_pendingSnapshotGeneration = 0;
    m_lastCommittedGeneration = 0;
    m_hasPendingSnapshot = false;
    m_lastAutosaveTime = std::chrono::steady_clock::now();
    m_lastDirtyTimeMs.store(steadyNowMs(), std::memory_order_release);
    
    // Ensure backup directory exists
    if (!m_projectPath.empty()) {
        fs::path backupDir = getBackupDirForProject(m_projectPath);
        std::error_code ec;
        fs::create_directories(backupDir, ec);
        if (ec) {
            Log::error("Failed to create autosave backup directory: " + backupDir.string());
        }
    }
    
    // Start background thread if enabled
    if (m_enabled.load(std::memory_order_acquire)) {
        m_autosaveThread = std::make_unique<std::thread>(
            &AutosaveManager::autosaveThreadFunc, this);
        Log::info("AutosaveManager initialized for: " + projectPath);
        notifyStatus("Autosave enabled");
    } else {
        Log::info("AutosaveManager initialized (disabled) for: " + projectPath);
    }
    
    m_initialized = true;
}

void AutosaveManager::shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_initialized) return;
        
        m_shouldStop = true;
        m_cv.notify_all();
    }
    
    if (m_autosaveThread && m_autosaveThread->joinable()) {
        m_autosaveThread->join();
        m_autosaveThread.reset();
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);
    m_initialized = false;
    m_pendingSnapshot.clear();
    m_pendingSnapshotGeneration = 0;
    m_hasPendingSnapshot = false;
    
    Log::info("AutosaveManager shutdown");
}

//==============================================================================
// Dirty Tracking
//==============================================================================

void AutosaveManager::markDirty() {
    m_isDirty.store(true, std::memory_order_release);
    m_lastDirtyTimeMs.store(steadyNowMs(), std::memory_order_release);
    m_cv.notify_all();
}

void AutosaveManager::markClean() {
    bool wasDirty = m_isDirty.exchange(false, std::memory_order_acq_rel);
    if (wasDirty) {
        notifyStatus("Project saved");
    }
}

void AutosaveManager::setEnabled(bool enabled) {
    m_enabled.store(enabled, std::memory_order_release);
    if (enabled && !m_autosaveThread) {
        // Restart thread if enabling
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_initialized && !m_autosaveThread) {
            m_shouldStop = false;
            m_autosaveThread = std::make_unique<std::thread>(
                &AutosaveManager::autosaveThreadFunc, this);
        }
    }
    m_cv.notify_all();
}

//==============================================================================
// Autosave Control
//==============================================================================

bool AutosaveManager::forceAutosave() {
    return performAutosave();
}

bool AutosaveManager::autosaveIfDue() {
    // Same gate as autosaveThreadFunc(): only save when there are pending changes
    // that have survived the debounce window. markClean() clearing m_isDirty is
    // exactly what suppresses a would-be autosave here.
    if (!m_isDirty.load(std::memory_order_acquire)) {
        return false;
    }
    const int64_t sinceMs = steadyNowMs() - m_lastDirtyTimeMs.load(std::memory_order_acquire);
    const int64_t minDelayMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(m_config.minDirtyDelay).count();
    if (sinceMs < minDelayMs) {
        return false;
    }
    // Claim the dirty state BEFORE the (slow) file I/O. A markDirty() that lands
    // during the save then re-sets the flag and is caught next cycle, instead of
    // being wiped by an unconditional markClean() afterward.
    if (!m_isDirty.exchange(false, std::memory_order_acq_rel)) {
        return false;
    }
    if (!performAutosave()) {
        m_isDirty.store(true, std::memory_order_release); // save failed; stay dirty
        return false;
    }
    notifyStatus("Project saved");
    return true;
}

bool AutosaveManager::claimDirtyIfDue() {
    if (!m_isDirty.load(std::memory_order_acquire)) {
        return false;
    }
    const int64_t sinceMs = steadyNowMs() - m_lastDirtyTimeMs.load(std::memory_order_acquire);
    const int64_t minDelayMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(m_config.minDirtyDelay).count();
    if (sinceMs < minDelayMs) {
        return false;
    }
    return m_isDirty.exchange(false, std::memory_order_acq_rel);
}

bool AutosaveManager::captureSnapshotIfDue() {
    if (!m_config.captureSnapshotOnCallingThread || !m_initialized.load(std::memory_order_acquire) ||
        !m_enabled.load(std::memory_order_acquire) || !claimDirtyIfDue()) {
        return false;
    }

    std::string data;
    if (!m_config.serializer || !m_config.serializer(data)) {
        m_isDirty.store(true, std::memory_order_release);
        notifyError("Autosave failed: serialization error");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_shouldStop.load(std::memory_order_acquire)) {
            m_isDirty.store(true, std::memory_order_release);
            return false;
        }
        // Only the latest immutable snapshot matters if the writer has not yet
        // consumed an older one. Dirty changes made during capture remain set.
        m_pendingSnapshot = std::move(data);
        m_pendingSnapshotGeneration = m_nextSnapshotGeneration.fetch_add(1, std::memory_order_acq_rel);
        m_hasPendingSnapshot = true;
    }
    m_cv.notify_all();
    return true;
}

std::string AutosaveManager::getAutosavePath() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_config.autosavePathOverride.empty()) return m_config.autosavePathOverride;
    if (m_projectPath.empty()) return "";
    return getAutosavePathForProject(m_projectPath);
}

std::string AutosaveManager::getBackupDirectory() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_config.autosavePathOverride.empty()) {
        fs::path p(m_config.autosavePathOverride);
        if (p.has_parent_path()) {
            return (p.parent_path() / (p.stem().string() + ".autosave")).string();
        }
        return "";
    }
    if (m_projectPath.empty()) return "";
    return getBackupDirForProject(m_projectPath);
}

//==============================================================================
// Recovery (Static)
//==============================================================================

AutosaveManager::RecoveryInfo AutosaveManager::checkForRecovery(const std::string& projectPath) {
    RecoveryInfo info;
    info.originalPath = projectPath;
    
    if (projectPath.empty()) {
        return info;
    }
    
    std::string autosavePath = getAutosavePathForProject(projectPath);
    
    std::error_code ec;
    if (!fs::exists(autosavePath, ec) || ec) {
        return info;
    }
    
    // Get timestamp
    auto lastWrite = fs::last_write_time(autosavePath, ec);
    if (!ec) {
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            lastWrite - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        info.timestamp = sctp;
    }
    
    info.autosavePath = autosavePath;
    info.valid = true;
    
    return info;
}

std::vector<std::string> AutosaveManager::listBackups(const std::string& projectPath) {
    std::vector<std::string> backups;
    
    if (projectPath.empty()) {
        return backups;
    }
    
    std::string backupDir = getBackupDirForProject(projectPath);
    std::error_code ec;
    
    if (!fs::exists(backupDir, ec) || ec) {
        return backups;
    }
    
    for (const auto& entry : fs::directory_iterator(backupDir, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".aes") {
            backups.push_back(entry.path().string());
        }
    }
    
    std::sort(backups.begin(), backups.end(), 
        [](const std::string& a, const std::string& b) {
            std::error_code ec;
            auto timeA = fs::last_write_time(a, ec);
            auto timeB = fs::last_write_time(b, ec);
            return timeA > timeB;
        });
    
    return backups;
}

void AutosaveManager::cleanupAutosaves(const std::string& projectPath) {
    if (projectPath.empty()) return;
    
    std::error_code ec;
    
    std::string autosavePath = getAutosavePathForProject(projectPath);
    if (fs::exists(autosavePath, ec)) {
        fs::remove(autosavePath, ec);
        if (!ec) {
            Log::info("Cleaned up autosave: " + autosavePath);
        }
    }
    
    std::string backupDir = getBackupDirForProject(projectPath);
    if (fs::exists(backupDir, ec)) {
        fs::remove_all(backupDir, ec);
        if (!ec) {
            Log::info("Cleaned up backup directory: " + backupDir);
        }
    }
}

bool AutosaveManager::recoverTo(const RecoveryInfo& recoveryInfo, const std::string& targetPath) {
    if (!recoveryInfo.valid || recoveryInfo.autosavePath.empty()) {
        Log::error("Recovery failed: invalid recovery info");
        return false;
    }
    
    std::error_code ec;
    
    if (!fs::exists(recoveryInfo.autosavePath, ec)) {
        Log::error("Recovery failed: autosave file not found");
        return false;
    }
    
    fs::path target(targetPath);
    if (target.has_parent_path()) {
        fs::create_directories(target.parent_path(), ec);
    }
    
    fs::copy_file(recoveryInfo.autosavePath, targetPath, 
                  fs::copy_options::overwrite_existing, ec);
    
    if (ec) {
        Log::error("Recovery failed: " + ec.message());
        return false;
    }
    
    Log::info("Recovered project from autosave: " + recoveryInfo.autosavePath + " -> " + targetPath);
    return true;
}

//==============================================================================
// Status
//==============================================================================

std::chrono::seconds AutosaveManager::getTimeSinceLastAutosave() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - m_lastAutosaveTime;
    return std::chrono::duration_cast<std::chrono::seconds>(elapsed);
}

//==============================================================================
// Internal
//==============================================================================

void AutosaveManager::autosaveThreadFunc() {
    Log::info("Autosave thread started");

    if (m_config.captureSnapshotOnCallingThread) {
        while (!m_shouldStop.load(std::memory_order_acquire)) {
            std::string snapshot;
            uint64_t snapshotGeneration = 0;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this] {
                    return m_shouldStop.load(std::memory_order_acquire) ||
                           (m_enabled.load(std::memory_order_acquire) && m_hasPendingSnapshot);
                });

                if (m_shouldStop.load(std::memory_order_acquire)) {
                    break;
                }
                if (!m_enabled.load(std::memory_order_acquire) || !m_hasPendingSnapshot) {
                    continue;
                }

                snapshot = std::move(m_pendingSnapshot);
                snapshotGeneration = m_pendingSnapshotGeneration;
                m_pendingSnapshot.clear();
                m_pendingSnapshotGeneration = 0;
                m_hasPendingSnapshot = false;
            }

            if (performAutosaveWithData(std::move(snapshot), snapshotGeneration)) {
                notifyStatus("Project saved");
            } else {
                // The immutable snapshot could not be committed. Capture a new
                // current snapshot on the owner thread on the next cycle.
                m_isDirty.store(true, std::memory_order_release);
            }
        }

        Log::info("Autosave thread stopped");
        return;
    }
    
    while (!m_shouldStop.load(std::memory_order_acquire)) {
        std::unique_lock<std::mutex> lock(m_mutex);
        
        m_cv.wait_for(lock, m_config.autosaveInterval, [this] {
            return m_shouldStop.load(std::memory_order_acquire) || 
                   (m_enabled.load(std::memory_order_acquire) && m_isDirty.load(std::memory_order_acquire));
        });
        
        if (m_shouldStop.load(std::memory_order_acquire)) {
            break;
        }
        
        if (!m_enabled.load(std::memory_order_acquire)) {
            continue;
        }
        
        if (!m_isDirty.load(std::memory_order_acquire)) {
            continue;
        }

        const int64_t sinceMs = steadyNowMs() - m_lastDirtyTimeMs.load(std::memory_order_acquire);
        const int64_t minDelayMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(m_config.minDirtyDelay).count();
        if (sinceMs < minDelayMs) {
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(minDelayMs - sinceMs));
            continue;
        }

        lock.unlock();

        // Claim the dirty state before the save so a concurrent markDirty() during
        // the file I/O keeps the project dirty for the next cycle rather than being
        // cleared by an unconditional markClean().
        if (!m_isDirty.exchange(false, std::memory_order_acq_rel)) {
            continue;
        }
        if (performAutosave()) {
            notifyStatus("Project saved");
        } else {
            m_isDirty.store(true, std::memory_order_release); // save failed; stay dirty
        }
    }
    
    Log::info("Autosave thread stopped");
}

bool AutosaveManager::performAutosave() {
    std::lock_guard<std::mutex> commitLock(m_commitMutex);
    m_isAutosaving.store(true, std::memory_order_release);

    struct AutosavingGuard {
        std::atomic<bool>& flag;
        explicit AutosavingGuard(std::atomic<bool>& f) : flag(f) {}
        ~AutosavingGuard() { flag.store(false, std::memory_order_release); }
    } guard(m_isAutosaving);

    if (!m_config.serializer) {
        notifyError("Autosave failed: no serializer callback");
        return false;
    }

    std::string data;
    if (!m_config.serializer(data)) {
        notifyError("Autosave failed: serialization error");
        return false;
    }

    const uint64_t generation = m_nextSnapshotGeneration.fetch_add(1, std::memory_order_acq_rel);
    return writeAutosaveData(data, generation);
}

bool AutosaveManager::performAutosaveWithData(std::string data, uint64_t generation) {
    std::lock_guard<std::mutex> commitLock(m_commitMutex);
    m_isAutosaving.store(true, std::memory_order_release);

    struct AutosavingGuard {
        std::atomic<bool>& flag;
        explicit AutosavingGuard(std::atomic<bool>& f) : flag(f) {}
        ~AutosavingGuard() { flag.store(false, std::memory_order_release); }
    } guard(m_isAutosaving);

    return writeAutosaveData(data, generation);
}

bool AutosaveManager::writeAutosaveData(const std::string& data, uint64_t generation) {
    if (generation <= m_lastCommittedGeneration) {
        return true;
    }

    std::string autosavePath = getAutosavePath();
    if (autosavePath.empty()) {
        notifyError("Autosave failed: no autosave path");
        return false;
    }

    notifyStatus("Autosaving...");
    rotateBackups();

    // Write atomically
    fs::path target(autosavePath);
    fs::path tmp = target;
    tmp += ".tmp";
    
    std::error_code ec;
    if (target.has_parent_path()) {
        fs::create_directories(target.parent_path(), ec);
    }
    
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            notifyError("Autosave failed: cannot open temp file");
            return false;
        }
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.flush();
        if (!out) {
            notifyError("Autosave failed: write error");
            fs::remove(tmp, ec);
            return false;
        }
        // Sync to disk before atomic rename to prevent data loss on crash
        if (!Aestra::syncOfstream(out, tmp.string())) {
            notifyError("Autosave failed: sync error");
            out.close();
            fs::remove(tmp, ec);
            return false;
        }
    }
    
    // Atomic replace: on POSIX, rename() atomically replaces the target.
    // On Windows, we must remove first (no atomic rename-over-existing in std::filesystem).
#ifdef _WIN32
    if (fs::exists(target, ec)) {
        fs::remove(target, ec);
    }
#endif

    fs::rename(tmp, target, ec);
    if (ec) {
        notifyError("Autosave failed: cannot replace target file");
        fs::remove(tmp, ec);
        return false;
    }

#ifndef _WIN32
    if (!Aestra::fsyncParentDirectory(target.string())) {
        notifyError("Autosave failed: directory sync error");
        return false;
    }
#endif
    
    m_lastAutosaveTime = std::chrono::steady_clock::now();
    
    size_t sizeKB = data.size() / 1024;
    std::string sizeStr = sizeKB > 0 ? std::to_string(sizeKB) + " KB" : 
                          std::to_string(data.size()) + " bytes";
    
    notifyStatus("Autosaved (" + sizeStr + ")");
    Log::info("Autosaved to: " + autosavePath + " (" + sizeStr + ")");

    if (m_config.onAutosaveCommitted) {
        m_config.onAutosaveCommitted(autosavePath);
    }

    m_lastCommittedGeneration = generation;
    
    return true;
}

void AutosaveManager::rotateBackups() {
    std::string autosavePath = getAutosavePath();
    std::string backupDir = getBackupDirectory();
    if (autosavePath.empty() || backupDir.empty() || m_config.maxBackupFiles == 0) return;

    std::error_code ec;

    if (!fs::exists(autosavePath, ec) || ec) {
        return;
    }

    fs::create_directories(backupDir, ec);
    if (ec) return;

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << backupDir << "/";
    ss << std::put_time(std::localtime(&time), "%Y%m%d_%H%M%S");
    ss << ".aes";
    std::string backupPath = ss.str();

    fs::copy_file(autosavePath, backupPath, ec);
    if (ec) {
        Log::warning("Failed to create backup: " + ec.message());
        return;
    }

    // Prune oldest backups
    std::vector<std::string> backups;
    for (const auto& entry : fs::directory_iterator(backupDir, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".aes") {
            backups.push_back(entry.path().string());
        }
    }
    std::sort(backups.begin(), backups.end(),
        [](const std::string& a, const std::string& b) {
            std::error_code aec, bec;
            auto timeA = fs::last_write_time(a, aec);
            auto timeB = fs::last_write_time(b, bec);
            return timeA > timeB;
        });

    while (backups.size() > m_config.maxBackupFiles) {
        std::string oldest = backups.back();
        backups.pop_back();

        fs::remove(oldest, ec);
        if (!ec) {
            Log::info("Removed old backup: " + oldest);
        }
    }
}

void AutosaveManager::notifyStatus(const std::string& msg) {
    if (m_onStatus) {
        m_onStatus(msg);
    }
}

void AutosaveManager::notifyError(const std::string& err) {
    Log::error("Autosave: " + err);
    if (m_onError) {
        m_onError(err);
    }
}

//==============================================================================
// Path Helpers
//==============================================================================

std::string AutosaveManager::getAutosavePathForProject(const std::string& projectPath) {
    if (projectPath.empty()) return "";
    
    fs::path project(projectPath);
    fs::path parent = project.parent_path();
    std::string stem = project.stem().string();
    
    return (parent / (stem + ".autosave.aes")).string();
}

std::string AutosaveManager::getBackupDirForProject(const std::string& projectPath) {
    if (projectPath.empty()) return "";
    
    fs::path project(projectPath);
    fs::path parent = project.parent_path();
    std::string stem = project.stem().string();
    
    return (parent / (stem + ".autosave")).string();
}

} // namespace Audio
} // namespace Aestra
