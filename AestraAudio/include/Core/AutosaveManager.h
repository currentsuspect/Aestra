// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <string>
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <mutex>
#include <vector>
#include <memory>
#include <condition_variable>

namespace Aestra {
namespace Audio {

/**
 * @brief Manages automatic project saving and crash recovery (v1.0)
 * 
 * Design principles:
 * - Non-destructive: Autosave never overwrites the canonical project file
 * - Dirty-tracking: Only saves when project has actually changed
 * - Thread-safe: Background autosave doesn't block the main thread
 * - Recovery: Detects and offers recovery from crashes
 * - Rotation: Maintains timestamped backups for safety
 * 
 * Usage:
 *   // In application layer (where ProjectSerializer is available):
 *   autosaveManager.initialize(projectPath, trackManager, {
 *       .serializer = [](std::string& outData) -> bool {
 *           auto result = ProjectSerializer::serialize(trackManager, tempo, position, 0);
 *           if (result.ok) { outData = result.contents; return true; }
 *           return false;
 *       }
 *   });
 */
class AutosaveManager {
public:
    // =============================================================================
    // Types
    // =============================================================================
    
    /**
     * @brief Serialization callback - called on background thread to get project data
     * @param outData Output string to fill with serialized project data
     * @return true if serialization succeeded
     */
    using SerializerFunc = std::function<bool(std::string& outData)>;
    
    struct Config {
        // Autosave interval (default: 30 seconds)
        std::chrono::seconds autosaveInterval{30};
        
        // Minimum time between dirty mark and autosave (debounce)
        std::chrono::seconds minDirtyDelay{5};
        
        // Maximum number of rotated backups to keep
        size_t maxBackupFiles{5};
        
        // Enable/disable autosave entirely
        bool enabled{true};
        
        // Serialization callback (REQUIRED)
        SerializerFunc serializer;
    };
    
    struct RecoveryInfo {
        std::string autosavePath;
        std::string originalPath;
        std::chrono::system_clock::time_point timestamp;
        bool valid{false};
        
        std::string getAgeString() const;
    };
    
    // =============================================================================
    // Lifecycle
    // =============================================================================
    
    AutosaveManager();
    ~AutosaveManager();
    
    AutosaveManager(const AutosaveManager&) = delete;
    AutosaveManager& operator=(const AutosaveManager&) = delete;
    
    // =============================================================================
    // Setup
    // =============================================================================
    
    /**
     * @brief Initialize the autosave manager
     * @param projectPath The canonical project file path (e.g., "~/Projects/song.aes")
     * @param config Configuration including required serializer callback
     */
    void initialize(const std::string& projectPath, Config config);
    
    /**
     * @brief Shutdown the autosave manager (stops background thread)
     */
    void shutdown();
    
    // =============================================================================
    // Dirty Tracking
    // =============================================================================
    
    /**
     * @brief Mark the project as "dirty" (modified)
     * Call this after any user edit that should trigger an autosave.
     * Thread-safe, can be called from any thread.
     */
    void markDirty();
    
    /**
     * @brief Mark the project as "clean" (saved)
     * Call this after successful manual save.
     */
    void markClean();
    
    bool isDirty() const { return m_isDirty.load(std::memory_order_acquire); }
    
    // =============================================================================
    // Autosave Control
    // =============================================================================
    
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_config.enabled; }
    
    /**
     * @brief Force an immediate autosave (even if not dirty)
     * @return true if successful
     */
    bool forceAutosave();
    
    std::string getAutosavePath() const;
    std::string getBackupDirectory() const;
    
    // =============================================================================
    // Recovery (Static)
    // =============================================================================
    
    static RecoveryInfo checkForRecovery(const std::string& projectPath);
    static std::vector<std::string> listBackups(const std::string& projectPath);
    static void cleanupAutosaves(const std::string& projectPath);
    static bool recoverTo(const RecoveryInfo& recoveryInfo, const std::string& targetPath);
    
    // =============================================================================
    // Status & Callbacks
    // =============================================================================
    
    using StatusCallback = std::function<void(const std::string& message)>;
    using ErrorCallback = std::function<void(const std::string& error)>;
    
    void setStatusCallback(StatusCallback cb) { m_onStatus = cb; }
    void setErrorCallback(ErrorCallback cb) { m_onError = cb; }
    
    std::chrono::seconds getTimeSinceLastAutosave() const;
    bool isAutosaving() const { return m_isAutosaving.load(std::memory_order_acquire); }
    
private:
    void autosaveThreadFunc();
    bool performAutosave();
    void rotateBackups();
    void notifyStatus(const std::string& msg);
    void notifyError(const std::string& err);
    
    static std::string getAutosavePathForProject(const std::string& projectPath);
    static std::string getBackupDirForProject(const std::string& projectPath);
    
    Config m_config;
    std::string m_projectPath;
    
    std::atomic<bool> m_isDirty{false};
    std::atomic<bool> m_isAutosaving{false};
    std::atomic<bool> m_shouldStop{false};
    std::atomic<bool> m_initialized{false};
    
    std::chrono::steady_clock::time_point m_lastDirtyTime;
    std::chrono::steady_clock::time_point m_lastAutosaveTime;
    
    std::unique_ptr<std::thread> m_autosaveThread;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    
    StatusCallback m_onStatus;
    ErrorCallback m_onError;
};

} // namespace Audio
} // namespace Aestra
