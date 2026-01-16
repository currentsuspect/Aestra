// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
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
    void scanAsync(ScanProgressCallback progressCallback = nullptr,
                   ScanCompleteCallback completeCallback = nullptr);
    
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
    
    // Internal scanning methods
    void scanDirectory(const std::filesystem::path& dir, 
                      std::vector<PluginInfo>& results,
                      ScanProgressCallback callback,
                      int& currentIndex,
                      int totalCount);
    
    std::vector<PluginInfo> scanVST3Plugin(const std::filesystem::path& path);
    std::vector<PluginInfo> scanCLAPPlugin(const std::filesystem::path& path);
    
    int countPluginFiles() const;
};

} // namespace Audio
} // namespace Aestra
