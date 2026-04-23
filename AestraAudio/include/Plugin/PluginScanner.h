// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PluginHost.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Plugin scanner for discovering installed plugins
 *
 * Scans filesystem paths for VST3 and CLAP plugins, extracting metadata
 * and building a searchable plugin database. Supports async scanning
 * with progress callbacks and caching for fast startup.
 *
 * Usage:
 * @code
 *   PluginScanner scanner;
 *   scanner.addSearchPath("C:/Program Files/Common Files/VST3");
 *   scanner.addSearchPath("C:/Program Files/Common Files/CLAP");
 *
 *   scanner.scanAsync([](const std::string& path, int current, int total) {
 *       std::cout << "Scanning: " << path << " (" << current << "/" << total << ")\n";
 *   });
 * @endcode
 */
class PluginScanner {
public:
    /**
     * @brief Progress callback signature
     * @param currentPath Path currently being scanned
     * @param currentIndex Current plugin index (1-based)
     * @param totalCount Total plugins to scan
     */
    using ScanProgressCallback = std::function<void(const std::string& currentPath, int currentIndex, int totalCount)>;

    /**
     * @brief Completion callback signature
     * @param plugins List of discovered plugins
     * @param success true if scan completed, false if cancelled
     */
    using ScanCompleteCallback = std::function<void(const std::vector<PluginInfo>& plugins, bool success)>;

    PluginScanner();
    ~PluginScanner();

    // Non-copyable
    PluginScanner(const PluginScanner&) = delete;
    PluginScanner& operator=(const PluginScanner&) = delete;

    // ==============================
    // Search Paths
    // ==============================

    /**
     * @brief Add a directory to search for plugins
     * @param path Directory path (e.g., "C:/Program Files/Common Files/VST3")
     */
    void addSearchPath(const std::filesystem::path& path);

    /**
     * @brief Remove a search path
     */
    void removeSearchPath(const std::filesystem::path& path);

    /**
     * @brief Clear all search paths
     */
    void clearSearchPaths();

    /**
     * @brief Get all configured search paths
     */
    std::vector<std::filesystem::path> getSearchPaths() const;

    /**
     * @brief Add default platform search paths
     *
     * Windows:
     *   - C:\Program Files\Common Files\VST3
     *   - C:\Program Files\Common Files\CLAP
     *
     * macOS:
     *   - /Library/Audio/Plug-Ins/VST3
     *   - ~/Library/Audio/Plug-Ins/VST3
     *   - /Library/Audio/Plug-Ins/CLAP
     *   - ~/Library/Audio/Plug-Ins/CLAP
     *
     * Linux:
     *   - /usr/lib/vst3
     *   - ~/.vst3
     *   - /usr/lib/clap
     *   - ~/.clap
     */
    void addDefaultSearchPaths();

    // ==============================
    // Scanning
    // ==============================

    /**
     * @brief Start async plugin scan
     *
     * Launches a background thread to scan all search paths.
     * Progress is reported via callback. Results available via
     * getScannedPlugins() or the completion callback.
     *
     * @param progressCallback Called for each plugin scanned (can be null)
     * @param completeCallback Called when scan finishes (can be null)
     */
    void scanAsync(ScanProgressCallback progressCallback = nullptr, ScanCompleteCallback completeCallback = nullptr);

    /**
     * @brief Cancel ongoing async scan
     */
    void cancelScan();

    /**
     * @brief Check if scan is in progress
     */
    bool isScanning() const;

    /**
     * @brief Perform blocking scan
     * @return List of discovered plugins
     */
    std::vector<PluginInfo> scanBlocking();

    /**
     * @brief Rescan a single plugin file
     * @param path Path to plugin file
     * @return Plugin info if valid, empty if failed
     */
    std::vector<PluginInfo> rescanPlugin(const std::filesystem::path& path);

    // ==============================
    // Results
    // ==============================

    /**
     * @brief Get all scanned plugins
     * @return Vector of plugin info (may be empty if not scanned)
     */
    const std::vector<PluginInfo>& getScannedPlugins() const;

    /**
     * @brief Get plugins filtered by type
     */
    std::vector<PluginInfo> getPluginsByType(PluginType type) const;

    /**
     * @brief Get plugins filtered by format
     */
    std::vector<PluginInfo> getPluginsByFormat(PluginFormat format) const;

    /**
     * @brief Find plugin by ID
     * @return Pointer to plugin info, or nullptr if not found
     */
    const PluginInfo* findPlugin(const std::string& id) const;

    /**
     * @brief Search plugins by name (case-insensitive substring match)
     */
    std::vector<PluginInfo> searchPlugins(const std::string& query) const;

    // ==============================
    // Cache Persistence
    // ==============================

    /**
     * @brief Save scan results to cache file
     * @param cachePath Path to cache file
     * @return true on success
     */
    bool saveScanCache(const std::filesystem::path& cachePath) const;

    /**
     * @brief Load scan results from cache file
     * @param cachePath Path to cache file
     * @return true if cache loaded and is valid
     */
    bool loadScanCache(const std::filesystem::path& cachePath);

    /**
     * @brief Clear scan cache
     */
    void clearCache();

    /**
     * @brief Check if a plugin file has been modified since last scan
     */
    bool isPluginModified(const std::filesystem::path& pluginPath) const;

    // ==============================
    // Security: Trusted Paths (SEC-RTM-005)
    // ==============================

    /**
     * @brief Check if a plugin path is in a trusted system directory.
     *
     * Only checks system paths. For user-configured paths, use checkPathTrusted().
     */
    static bool isTrustedPath(const std::filesystem::path& path);

    /**
     * @brief Check if a plugin path is trusted (system + user-configured).
     *
     * Checks both system paths and user-configured trusted paths (RTM-005 Part B).
     */
    bool checkPathTrusted(const std::filesystem::path& path) const;

    /**
     * @brief Add a user-configured trusted path (RTM-005 Part B).
     *
     * Paths added here are treated as trusted and skip first-load warning.
     * Stored persistently in config/settings file.
     *
     * @param path Directory path to trust
     */
    void addTrustedPath(const std::filesystem::path& path);

    /**
     * @brief Remove a user-configured trusted path (RTM-005 Part B).
     *
     * @param path Directory path to untrust
     */
    void removeTrustedPath(const std::filesystem::path& path);

    /**
     * @brief Get all user-configured trusted paths.
     *
     * @return Vector of trusted directory paths
     */
    std::vector<std::filesystem::path> getTrustedPaths() const;

    /**
     * @brief Load trusted paths from persistent storage.
     * Called during PluginScanner initialization.
     */
    void loadTrustedPaths();

    /**
     * @brief Save trusted paths to persistent storage.
     * Called after user adds/removes a path.
     */
    void saveTrustedPaths() const;

    /**
     * @brief Callback invoked when a plugin from an untrusted path is loaded for the first time.
     * @param pluginPath Path to the plugin file
     * @param pluginName Display name of the plugin
     * @return true to allow loading, false to skip this plugin
     */
    using FirstLoadWarningCallback = std::function<bool(const std::filesystem::path& pluginPath, const std::string& pluginName)>;

    /**
     * @brief Set the callback for first-load warnings from untrusted paths.
     * If not set, untrusted-path plugins are skipped before binary load.
     */
    void setFirstLoadWarningCallback(FirstLoadWarningCallback cb);

private:
    mutable std::mutex m_mutex;
    std::set<std::filesystem::path> m_searchPaths;
    std::vector<PluginInfo> m_scannedPlugins;

    // Async scan state
    std::atomic<bool> m_scanning{false};
    std::atomic<bool> m_cancelRequested{false};
    std::thread m_scanThread;

    // Cache metadata (path -> last modified time)
    std::unordered_map<std::string, std::filesystem::file_time_type> m_fileTimestamps;

    // [SEC-RTM-005] First-load warning for untrusted-path plugins
    FirstLoadWarningCallback m_firstLoadWarningCb;
    std::set<std::string> m_seenUntrustedPlugins;  // path strings already acknowledged

    // [SEC-RTM-005 Part B] User-configured trusted paths (loaded from config)
    std::vector<std::filesystem::path> m_userTrustedPaths;

    // Internal scanning methods
    void scanDirectory(const std::filesystem::path& dir, std::vector<PluginInfo>& results,
                       ScanProgressCallback callback, int& currentIndex, int totalCount);

    std::vector<PluginInfo> scanVST3Plugin(const std::filesystem::path& path);
    std::vector<PluginInfo> scanCLAPPlugin(const std::filesystem::path& path);

    int countPluginFiles() const;
};

} // namespace Audio
} // namespace Aestra
