// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "AutosaveManager.h"
#include "AestraLog.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace fs = std::filesystem;

namespace Aestra {
namespace Audio {

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
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_initialized) {
        Log::warning("AutosaveManager already initialized, shutting down first");
        shutdown();
    }
    
    if (!config.serializer) {
        Log::error("AutosaveManager initialized without serializer callback");
        return;
    }
    
    m_projectPath = projectPath;
    m_config = std::move(config);
    
    m_isDirty = false;
    m_isAutosaving = false;
    m_shouldStop = false;
    m_lastAutosaveTime = std::chrono::steady_clock::now();
    m_lastDirtyTime = std::chrono::steady_clock::now();
    
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
    if (m_config.enabled) {
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
    
    Log::info("AutosaveManager shutdown");
}

//==============================================================================
// Dirty Tracking
//==============================================================================

void AutosaveManager::markDirty() {
    m_isDirty.store(true, std::memory_order_release);
    m_lastDirtyTime = std::chrono::steady_clock::now();
    m_cv.notify_all();
}

void AutosaveManager::markClean() {
    bool wasDirty = m_isDirty.exchange(false, std::memory_order_acq_rel);
    if (wasDirty) {
        notifyStatus("Project saved");
    }
}

void AutosaveManager::setEnabled(bool enabled) {
    m_config.enabled = enabled;
    if (enabled && !m_autosaveThread) {
        // Restart thread if enabling
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_initialized && !m_autosaveThread) {
            m_shouldStop = false;
            m_autosaveThread = std::make_unique<std::thread>(
                &AutosaveManager::autosaveThreadFunc, this);
        }
    }
}

//==============================================================================
// Autosave Control
//==============================================================================

bool AutosaveManager::forceAutosave() {
    return performAutosave();
}

std::string AutosaveManager::getAutosavePath() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_projectPath.empty()) return "";
    return getAutosavePathForProject(m_projectPath);
}

std::string AutosaveManager::getBackupDirectory() const {
    std::lock_guard<std::mutex> lock(m_mutex);
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
    
    while (!m_shouldStop.load(std::memory_order_acquire)) {
        std::unique_lock<std::mutex> lock(m_mutex);
        
        auto waitResult = m_cv.wait_for(lock, m_config.autosaveInterval, [this] {
            return m_shouldStop.load(std::memory_order_acquire) || 
                   (m_config.enabled && m_isDirty.load(std::memory_order_acquire));
        });
        
        if (m_shouldStop.load(std::memory_order_acquire)) {
            break;
        }
        
        if (!m_config.enabled) {
            continue;
        }
        
        if (!m_isDirty.load(std::memory_order_acquire)) {
            continue;
        }
        
        auto now = std::chrono::steady_clock::now();
        auto timeSinceDirty = now - m_lastDirtyTime;
        if (timeSinceDirty < m_config.minDirtyDelay) {
            auto remaining = m_config.minDirtyDelay - timeSinceDirty;
            lock.unlock();
            std::this_thread::sleep_for(remaining);
            continue;
        }
        
        lock.unlock();
        
        if (performAutosave()) {
            markClean();
        }
    }
    
    Log::info("Autosave thread stopped");
}

bool AutosaveManager::performAutosave() {
    bool expected = false;
    if (!m_isAutosaving.compare_exchange_strong(expected, true)) {
        return false;
    }
    
    struct AutosavingGuard {
        std::atomic<bool>& flag;
        explicit AutosavingGuard(std::atomic<bool>& f) : flag(f) {}
        ~AutosavingGuard() { flag.store(false, std::memory_order_release); }
    } guard(m_isAutosaving);
    
    std::string projectPath;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        projectPath = m_projectPath;
    }
    
    if (projectPath.empty()) {
        notifyError("Autosave failed: no project path");
        return false;
    }
    
    if (!m_config.serializer) {
        notifyError("Autosave failed: no serializer callback");
        return false;
    }
    
    notifyStatus("Autosaving...");
    
    rotateBackups();
    
    std::string autosavePath = getAutosavePathForProject(projectPath);
    
    // Call the serializer callback to get project data
    std::string data;
    if (!m_config.serializer(data)) {
        notifyError("Autosave failed: serialization error");
        return false;
    }
    
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
    }
    
    if (fs::exists(target, ec)) {
        fs::remove(target, ec);
    }
    
    fs::rename(tmp, target, ec);
    if (ec) {
        notifyError("Autosave failed: cannot replace target file");
        fs::remove(tmp, ec);
        return false;
    }
    
    m_lastAutosaveTime = std::chrono::steady_clock::now();
    
    size_t sizeKB = data.size() / 1024;
    std::string sizeStr = sizeKB > 0 ? std::to_string(sizeKB) + " KB" : 
                          std::to_string(data.size()) + " bytes";
    
    notifyStatus("Autosaved (" + sizeStr + ")");
    Log::info("Autosaved to: " + autosavePath + " (" + sizeStr + ")");
    
    return true;
}

void AutosaveManager::rotateBackups() {
    if (m_projectPath.empty() || m_config.maxBackupFiles == 0) return;
    
    std::string autosavePath = getAutosavePathForProject(m_projectPath);
    std::string backupDir = getBackupDirForProject(m_projectPath);
    
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
    
    auto backups = listBackups(m_projectPath);
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
