// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "PluginScanner.h"

#include "Plugin/BuiltInPlugins.h"
#include "AestraLog.h"
#include "AestraPlatform.h"

#include <algorithm>
#include <cctype>
#include <fstream>

#ifdef AESTRA_HAS_VST3
#include "Plugin/VST3Host.h"
#endif

#ifdef AESTRA_HAS_CLAP
#include "Plugin/CLAPHost.h"
#endif

namespace Aestra {
namespace Audio {

namespace {
void mergeBuiltInPlugins(std::vector<PluginInfo>& plugins) {
    for (auto& builtIn : BuiltInPlugins::all()) {
        const auto it = std::find_if(plugins.begin(), plugins.end(), [&](const PluginInfo& existing) {
            return existing.id == builtIn.id;
        });
        if (it == plugins.end()) {
            plugins.push_back(std::move(builtIn));
        }
    }
}
} // namespace

PluginScanner::PluginScanner() {
    loadTrustedPaths();  // [SEC-RTM-005 Part B] Load user-configured trusted paths
    mergeBuiltInPlugins(m_scannedPlugins);
}

PluginScanner::~PluginScanner() {
    cancelScan();
    if (m_scanThread.joinable()) {
        m_scanThread.join();
    }
}

// ==============================
// Search Paths
// ==============================

void PluginScanner::addSearchPath(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_searchPaths.insert(path);
}

void PluginScanner::removeSearchPath(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_searchPaths.erase(path);
}

void PluginScanner::clearSearchPaths() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_searchPaths.clear();
}

std::vector<std::filesystem::path> PluginScanner::getSearchPaths() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::vector<std::filesystem::path>(m_searchPaths.begin(), m_searchPaths.end());
}

void PluginScanner::addDefaultSearchPaths() {
#ifdef _WIN32
    // Windows default paths
    addSearchPath("C:/Program Files/Common Files/VST3");
    addSearchPath("C:/Program Files/Common Files/CLAP");

    // User-specific paths
    if (const char* appdata = std::getenv("LOCALAPPDATA")) {
        addSearchPath(std::filesystem::path(appdata) / "Programs" / "Common" / "VST3");
        addSearchPath(std::filesystem::path(appdata) / "Programs" / "Common" / "CLAP");
    }
#elif __APPLE__
    // macOS default paths
    addSearchPath("/Library/Audio/Plug-Ins/VST3");
    addSearchPath("/Library/Audio/Plug-Ins/CLAP");

    if (const char* home = std::getenv("HOME")) {
        addSearchPath(std::filesystem::path(home) / "Library" / "Audio" / "Plug-Ins" / "VST3");
        addSearchPath(std::filesystem::path(home) / "Library" / "Audio" / "Plug-Ins" / "CLAP");
    }
#else
    // Linux default paths
    addSearchPath("/usr/lib/vst3");
    addSearchPath("/usr/local/lib/vst3");
    addSearchPath("/usr/lib/clap");
    addSearchPath("/usr/local/lib/clap");

    if (const char* home = std::getenv("HOME")) {
        addSearchPath(std::filesystem::path(home) / ".vst3");
        addSearchPath(std::filesystem::path(home) / ".clap");
    }
#endif
}

// ==============================
// Scanning
// ==============================

void PluginScanner::scanAsync(ScanProgressCallback progressCallback, ScanCompleteCallback completeCallback) {
    // Cancel any existing scan
    cancelScan();
    if (m_scanThread.joinable()) {
        m_scanThread.join();
    }

    m_scanning.store(true);
    m_cancelRequested.store(false);

    m_scanThread = std::thread([this, progressCallback, completeCallback]() {
        std::vector<PluginInfo> results;
        bool success = true;

        try {
            // Count total plugins first
            int totalCount = countPluginFiles();
            int currentIndex = 0;

            // Get search paths (thread-safe copy)
            std::vector<std::filesystem::path> paths;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                paths = std::vector<std::filesystem::path>(m_searchPaths.begin(), m_searchPaths.end());
            }

            // Scan each path
            for (const auto& path : paths) {
                if (m_cancelRequested.load()) {
                    success = false;
                    break;
                }

                if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
                    scanDirectory(path, results, progressCallback, currentIndex, totalCount);
                }
            }

            // Store results
            if (success) {
                mergeBuiltInPlugins(results);
                std::lock_guard<std::mutex> lock(m_mutex);
                m_scannedPlugins = std::move(results);
            }
        } catch (...) {
            success = false;
        }

        m_scanning.store(false);

        // Call completion callback OUTSIDE the mutex lock to prevent deadlock
        // when the callback tries to update UI that has its own mutex
        std::vector<PluginInfo> resultsCopy;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            resultsCopy = m_scannedPlugins;
        }

        if (completeCallback) {
            completeCallback(resultsCopy, success);
        }
    });
}

void PluginScanner::cancelScan() {
    m_cancelRequested.store(true);
}

bool PluginScanner::isScanning() const {
    return m_scanning.load();
}

std::vector<PluginInfo> PluginScanner::scanBlocking() {
    // Get paths
    std::vector<std::filesystem::path> paths;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        paths = std::vector<std::filesystem::path>(m_searchPaths.begin(), m_searchPaths.end());
    }

    std::vector<PluginInfo> results;
    int totalCount = countPluginFiles();
    int currentIndex = 0;

    for (const auto& path : paths) {
        if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
            scanDirectory(path, results, nullptr, currentIndex, totalCount);
        }
    }

    mergeBuiltInPlugins(results);

    // Store results
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_scannedPlugins = results;
    }

    return results;
}

std::vector<PluginInfo> PluginScanner::rescanPlugin(const std::filesystem::path& path) {
    if (path.extension() == ".vst3") {
        return scanVST3Plugin(path);
    } else if (path.extension() == ".clap") {
        return scanCLAPPlugin(path);
    }
    return {};
}

// ==============================
// Results
// ==============================

const std::vector<PluginInfo>& PluginScanner::getScannedPlugins() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_scannedPlugins;
}

std::vector<PluginInfo> PluginScanner::getPluginsByType(PluginType type) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PluginInfo> result;
    for (const auto& p : m_scannedPlugins) {
        if (p.type == type) {
            result.push_back(p);
        }
    }
    return result;
}

std::vector<PluginInfo> PluginScanner::getPluginsByFormat(PluginFormat format) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PluginInfo> result;
    for (const auto& p : m_scannedPlugins) {
        if (p.format == format) {
            result.push_back(p);
        }
    }
    return result;
}

const PluginInfo* PluginScanner::findPlugin(const std::string& id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& p : m_scannedPlugins) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

std::vector<PluginInfo> PluginScanner::searchPlugins(const std::string& query) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PluginInfo> result;

    // Convert query to lowercase
    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    for (const auto& p : m_scannedPlugins) {
        // Convert name and vendor to lowercase for comparison
        std::string lowerName = p.name;
        std::string lowerVendor = p.vendor;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        std::transform(lowerVendor.begin(), lowerVendor.end(), lowerVendor.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (lowerName.find(lowerQuery) != std::string::npos || lowerVendor.find(lowerQuery) != std::string::npos) {
            result.push_back(p);
        }
    }

    return result;
}

// ==============================
// Cache Persistence
// ==============================

bool PluginScanner::saveScanCache(const std::filesystem::path& cachePath) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    try {
        std::ofstream file(cachePath, std::ios::binary);
        if (!file.is_open())
            return false;

        // Write header
        const char magic[4] = {'N', 'P', 'S', 'C'}; // Aestra Plugin Scan Cache
        file.write(magic, 4);

        uint32_t version = 2;  // v2: includes file mtime for integrity (SEC-RTM-006)
        file.write(reinterpret_cast<const char*>(&version), sizeof(version));

        // Write plugin count
        uint32_t count = static_cast<uint32_t>(m_scannedPlugins.size());
        file.write(reinterpret_cast<const char*>(&count), sizeof(count));

        // Write each plugin
        for (const auto& p : m_scannedPlugins) {
            // Write strings (length-prefixed)
            auto writeString = [&file](const std::string& s) {
                uint32_t len = static_cast<uint32_t>(s.size());
                file.write(reinterpret_cast<const char*>(&len), sizeof(len));
                file.write(s.data(), len);
            };

            writeString(p.id);
            writeString(p.name);
            writeString(p.vendor);
            writeString(p.version);
            writeString(p.category);
            writeString(p.path.string());

            file.write(reinterpret_cast<const char*>(&p.format), sizeof(p.format));
            file.write(reinterpret_cast<const char*>(&p.type), sizeof(p.type));
            file.write(reinterpret_cast<const char*>(&p.numAudioInputs), sizeof(p.numAudioInputs));
            file.write(reinterpret_cast<const char*>(&p.numAudioOutputs), sizeof(p.numAudioOutputs));
            file.write(reinterpret_cast<const char*>(&p.hasMidiInput), sizeof(p.hasMidiInput));
            file.write(reinterpret_cast<const char*>(&p.hasMidiOutput), sizeof(p.hasMidiOutput));
            file.write(reinterpret_cast<const char*>(&p.hasEditor), sizeof(p.hasEditor));

            // [SEC-RTM-006] Cache the file modification time for integrity verification
            std::error_code ec;
            auto mtime = std::filesystem::last_write_time(p.path, ec);
            uint64_t mtimeBits = ec ? 0 : mtime.time_since_epoch().count();
            file.write(reinterpret_cast<const char*>(&mtimeBits), sizeof(mtimeBits));
        }

        return true;
    } catch (...) {
        return false;
    }
}

bool PluginScanner::loadScanCache(const std::filesystem::path& cachePath) {
    std::lock_guard<std::mutex> lock(m_mutex);

    try {
        std::ifstream file(cachePath, std::ios::binary);
        if (!file.is_open())
            return false;

        // Read header
        char magic[4];
        file.read(magic, 4);
        if (magic[0] != 'N' || magic[1] != 'P' || magic[2] != 'S' || magic[3] != 'C') {
            return false;
        }

        uint32_t version;
        file.read(reinterpret_cast<char*>(&version), sizeof(version));
        if (version != 1 && version != 2)
            return false;

        // Read plugin count
        uint32_t count;
        file.read(reinterpret_cast<char*>(&count), sizeof(count));

        // [SEC-RTM-010] Guard against crafted cache files with massive counts
        constexpr uint32_t kMaxCachedPlugins = 10000;
        if (count > kMaxCachedPlugins) {
            return false;
        }

        std::vector<PluginInfo> plugins;
        plugins.reserve(count);

        // Read each plugin
        // [SEC-RTM-010] String length cap: no single metadata string > 64 KB
        constexpr uint32_t kMaxStringLen = 65536;
        auto readString = [&file, kMaxStringLen]() -> std::string {
            uint32_t len;
            file.read(reinterpret_cast<char*>(&len), sizeof(len));
            if (!file.good()) return "";
            if (len > kMaxStringLen) return "";
            std::string s(len, '\0');
            file.read(s.data(), len);
            if (!file.good()) return "";
            return s;
        };

        for (uint32_t i = 0; i < count; ++i) {
            PluginInfo p;
            p.id = readString();
            p.name = readString();
            p.vendor = readString();
            p.version = readString();
            p.category = readString();
            p.path = readString();

            file.read(reinterpret_cast<char*>(&p.format), sizeof(p.format));
            file.read(reinterpret_cast<char*>(&p.type), sizeof(p.type));
            file.read(reinterpret_cast<char*>(&p.numAudioInputs), sizeof(p.numAudioInputs));
            file.read(reinterpret_cast<char*>(&p.numAudioOutputs), sizeof(p.numAudioOutputs));
            file.read(reinterpret_cast<char*>(&p.hasMidiInput), sizeof(p.hasMidiInput));
            file.read(reinterpret_cast<char*>(&p.hasMidiOutput), sizeof(p.hasMidiOutput));
            file.read(reinterpret_cast<char*>(&p.hasEditor), sizeof(p.hasEditor));

            // [SEC-RTM-006] Verify file integrity: compare cached mtime with current mtime
            bool integrityOk = true;
            if (version >= 2) {
                uint64_t cachedMtimeBits = 0;
                file.read(reinterpret_cast<char*>(&cachedMtimeBits), sizeof(cachedMtimeBits));
                if (cachedMtimeBits != 0) {
                    std::error_code ec;
                    auto currentMtime = std::filesystem::last_write_time(p.path, ec);
                    if (!ec) {
                        uint64_t currentMtimeBits = currentMtime.time_since_epoch().count();
                        if (currentMtimeBits != cachedMtimeBits) {
                            integrityOk = false;  // File modified — needs rescan
                        }
                    }
                }
            }

            if (!p.isValid() || !integrityOk) {
                // Skip invalid or tampered entries
                continue;
            }
            plugins.push_back(std::move(p));
        }

        mergeBuiltInPlugins(plugins);
        m_scannedPlugins = std::move(plugins);
        return true;
    } catch (...) {
        return false;
    }
}

void PluginScanner::clearCache() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_scannedPlugins.clear();
    mergeBuiltInPlugins(m_scannedPlugins);
    m_fileTimestamps.clear();
}

bool PluginScanner::isPluginModified(const std::filesystem::path& pluginPath) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_fileTimestamps.find(pluginPath.string());
    if (it == m_fileTimestamps.end()) {
        return true; // Not in cache, consider modified
    }

    try {
        auto currentTime = std::filesystem::last_write_time(pluginPath);
        return currentTime != it->second;
    } catch (...) {
        return true;
    }
}

// ==============================
// Internal Scanning Methods
// ==============================

void PluginScanner::scanDirectory(const std::filesystem::path& dir, std::vector<PluginInfo>& results,
                                  ScanProgressCallback callback, int& currentIndex, int totalCount) {
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 dir, std::filesystem::directory_options::skip_permission_denied)) {
            if (m_cancelRequested.load()) {
                return;
            }

            if (!entry.is_regular_file() && !entry.is_directory()) {
                continue;
            }

            const auto& path = entry.path();
            std::vector<PluginInfo> scanned;

            // VST3 plugins are bundles (directories on Windows, .vst3 extension)
            if (path.extension() == ".vst3") {
                scanned = scanVST3Plugin(path);
            }
            // CLAP plugins are DLLs with .clap extension
            else if (path.extension() == ".clap" && entry.is_regular_file()) {
                scanned = scanCLAPPlugin(path);
            }

            if (!scanned.empty()) {
                ++currentIndex;

                if (callback) {
                    callback(path.string(), currentIndex, totalCount);
                }

                for (auto& info : scanned) {
                    // [SEC-RTM-005] First-load warning for untrusted-path plugins
                    // checkPathTrusted() checks both system AND user-configured paths
                    if (!checkPathTrusted(info.path)) {
                        std::unique_lock<std::mutex> cbLock(m_mutex);
                        if (m_seenUntrustedPlugins.find(info.path.string()) == m_seenUntrustedPlugins.end()) {
                            if (m_firstLoadWarningCb) {
                                // Unlock before calling UI callback (may re-enter)
                                cbLock.unlock();
                                bool allowed = m_firstLoadWarningCb(info.path, info.name);
                                cbLock.lock();
                                if (!allowed) {
                                    Log::warning("[PluginSecurity] Skipping untrusted plugin: " + info.name +
                                                 " from " + info.path.string());
                                    continue;  // Skip this plugin
                                }
                            }
                            m_seenUntrustedPlugins.insert(info.path.string());
                        }
                    }
                    results.push_back(std::move(info));
                }

                // Store timestamp
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_fileTimestamps[path.string()] = std::filesystem::last_write_time(path);
                }
            }
        }
    } catch (const std::filesystem::filesystem_error&) {
        // Skip directories we can't access
    }
}

std::vector<PluginInfo> PluginScanner::scanVST3Plugin(const std::filesystem::path& path) {
#ifdef AESTRA_HAS_VST3
    // Use VST3 SDK to properly scan the plugin
    return VST3PluginFactory::scanPlugin(path);
#else
    // Fallback: create a placeholder entry based on filename
    PluginInfo info;
    info.id = path.stem().string();
    info.name = path.stem().string();
    info.vendor = "Unknown";
    info.version = "1.0.0";
    info.category = "Effect";
    info.format = PluginFormat::VST3;
    info.type = PluginType::Effect;
    info.path = path;
    info.hasEditor = true;

    return {info};
#endif
}

std::vector<PluginInfo> PluginScanner::scanCLAPPlugin(const std::filesystem::path& path) {
#ifdef AESTRA_HAS_CLAP
    // Use CLAP SDK to properly scan the plugin
    return CLAPPluginFactory::scanPlugin(path);
#else
    // Fallback: create a placeholder entry based on filename
    PluginInfo info;
    info.id = path.stem().string();
    info.name = path.stem().string();
    info.vendor = "Unknown";
    info.version = "1.0.0";
    info.category = "Effect";
    info.format = PluginFormat::CLAP;
    info.type = PluginType::Effect;
    info.path = path;
    info.hasEditor = true;

    return {info};
#endif
}

int PluginScanner::countPluginFiles() const {
    int count = 0;

    std::vector<std::filesystem::path> paths;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        paths = std::vector<std::filesystem::path>(m_searchPaths.begin(), m_searchPaths.end());
    }

    for (const auto& path : paths) {
        try {
            if (!std::filesystem::exists(path))
                continue;

            for (const auto& entry : std::filesystem::recursive_directory_iterator(
                     path, std::filesystem::directory_options::skip_permission_denied)) {
                if (entry.path().extension() == ".vst3" || entry.path().extension() == ".clap") {
                    ++count;
                }
            }
        } catch (...) {
            // Skip inaccessible directories
        }
    }

    return count;
}

// ==============================
// Security: Trusted Paths (SEC-RTM-005)
// ==============================

// Static method: only checks system paths (no member access)
bool PluginScanner::isTrustedPath(const std::filesystem::path& path) {
    std::string p = path.lexically_normal().generic_string();
    std::replace(p.begin(), p.end(), '\\', '/');

    // Linux: system-wide paths are trusted
    if (p.find("/usr/lib/vst3") == 0 ||
        p.find("/usr/lib/clap") == 0 ||
        p.find("/usr/local/lib/vst3") == 0 ||
        p.find("/usr/local/lib/clap") == 0) {
        return true;
    }

    // Windows: Program Files paths are trusted
    if (p.find("C:/Program Files/Common Files/VST3") == 0 ||
        p.find("C:/Program Files/Common Files/CLAP") == 0) {
        return true;
    }

    // macOS: system Library paths are trusted
    if (p.find("/Library/Audio/Plug-Ins/VST3") == 0 ||
        p.find("/Library/Audio/Plug-Ins/CLAP") == 0) {
        return true;
    }

    // Everything else (user home directories, custom paths) is untrusted
    // Note: User-configured paths checked in non-static checkPath() below
    return false;
}

// Non-static method: checks both system AND user-configured paths (RTM-005 Part B)
bool PluginScanner::checkPathTrusted(const std::filesystem::path& path) const {
    std::string p = path.lexically_normal().string();

    // First check system paths (static method)
    if (isTrustedPath(path)) {
        return true;
    }

    // Then check user-configured paths
    for (const auto& trusted : m_userTrustedPaths) {
        std::string tp = trusted.lexically_normal().string();
        if (p.find(tp) == 0) {
            return true;
        }
    }

    return false;
}

// [SEC-RTM-005 Part B] User-configured trusted paths
void PluginScanner::addTrustedPath(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_userTrustedPaths.push_back(path.lexically_normal());
    saveTrustedPaths();
}

void PluginScanner::removeTrustedPath(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto normalized = path.lexically_normal();
    m_userTrustedPaths.erase(
        std::remove_if(m_userTrustedPaths.begin(), m_userTrustedPaths.end(),
            [&normalized](const std::filesystem::path& p) {
                return p.lexically_normal() == normalized;
            }),
        m_userTrustedPaths.end()
    );
    saveTrustedPaths();
}

std::vector<std::filesystem::path> PluginScanner::getTrustedPaths() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_userTrustedPaths;
}

void PluginScanner::loadTrustedPaths() {
    // Load user-configured trusted paths from JSON file
    std::lock_guard<std::mutex> lock(m_mutex);
    m_userTrustedPaths.clear();

    try {
        // Try to get app data directory from platform utils
        std::filesystem::path configPath;
        if (Aestra::Platform::isInitialized()) {
            auto* utils = Aestra::Platform::getUtils();
            if (!utils) {
                return;
            }
            std::error_code ec;
            std::filesystem::path appDataDir(utils->getAppDataPath("Aestra"));
            if (!appDataDir.empty()) {
                std::filesystem::create_directories(appDataDir, ec);
                configPath = appDataDir / "trusted_paths.json";
            }
        } else {
            return;
        }

        if (configPath.empty() || !std::filesystem::exists(configPath)) {
            return;  // No config location available yet
        }

        // Read JSON file
        std::ifstream file(configPath);
        if (!file.is_open()) {
            return;
        }

        // Simple JSON parse: look for "paths": [...]
        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());

        // Find the paths array
        auto pathsStart = content.find("\"paths\":");
        if (pathsStart == std::string::npos) {
            return;
        }

        // Extract paths between [ and ]
        auto arrayStart = content.find("[", pathsStart);
        auto arrayEnd = content.find("]", arrayStart);
        if (arrayStart == std::string::npos || arrayEnd == std::string::npos) {
            return;
        }

        // Extract each string between quotes
        size_t pos = arrayStart + 1;
        while (pos < arrayEnd) {
            auto quoteStart = content.find("\"", pos);
            if (quoteStart == std::string::npos || quoteStart >= arrayEnd) break;
            auto quoteEnd = content.find("\"", quoteStart + 1);
            if (quoteEnd == std::string::npos || quoteEnd >= arrayEnd) break;

            std::string pathStr = content.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
            if (!pathStr.empty()) {
                m_userTrustedPaths.push_back(std::filesystem::path(pathStr));
            }
            pos = quoteEnd + 1;
        }

        Log::info("[PluginScanner] Loaded " + std::to_string(m_userTrustedPaths.size()) +
                  " trusted paths from config");

    } catch (const std::exception& e) {
        Log::warning("[PluginScanner] Failed to load trusted paths: " + std::string(e.what()));
    }
}

void PluginScanner::saveTrustedPaths() const {
    // Save user-configured trusted paths to JSON file
    try {
        std::filesystem::path configPath;

        // Try to get app data directory from platform utils
        if (Aestra::Platform::isInitialized()) {
            auto* utils = Aestra::Platform::getUtils();
            if (!utils) {
                return;
            }
            std::error_code ec;
            std::filesystem::path appDataDir(utils->getAppDataPath("Aestra"));
            if (!appDataDir.empty()) {
                std::filesystem::create_directories(appDataDir, ec);
                configPath = appDataDir / "trusted_paths.json";
            }
        } else {
            return;
        }

        if (configPath.empty()) {
            Log::warning("[PluginScanner] No config path available to save trusted paths");
            return;
        }

        // Build JSON
        std::string json = "{\n  \"paths\": [\n";
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (size_t i = 0; i < m_userTrustedPaths.size(); ++i) {
                json += "    \"" + m_userTrustedPaths[i].lexically_normal().string() + "\"";
                if (i < m_userTrustedPaths.size() - 1) json += ",";
                json += "\n";
            }
        }
        json += "  ]\n}\n";

        // Write file
        std::ofstream file(configPath);
        if (!file.is_open()) {
            Log::warning("[PluginScanner] Failed to open config file for trusted paths");
            return;
        }
        file << json;
        file.close();

        Log::info("[PluginScanner] Saved " + std::to_string(m_userTrustedPaths.size()) +
                  " trusted paths to config");

    } catch (const std::exception& e) {
        Log::warning("[PluginScanner] Failed to save trusted paths: " + std::string(e.what()));
    }
}

void PluginScanner::setFirstLoadWarningCallback(FirstLoadWarningCallback cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_firstLoadWarningCb = std::move(cb);
}

} // namespace Audio
} // namespace Aestra
