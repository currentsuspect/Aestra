// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "PluginScanner.h"
#include <algorithm>
#include <cctype>
#include <fstream>

#ifdef NOMAD_HAS_VST3
#include "Plugin/VST3Host.h"
#endif

#ifdef NOMAD_HAS_CLAP
#include "Plugin/CLAPHost.h"
#endif

namespace Nomad {
namespace Audio {

PluginScanner::PluginScanner() = default;

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

void PluginScanner::scanAsync(ScanProgressCallback progressCallback,
                              ScanCompleteCallback completeCallback) {
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
                std::lock_guard<std::mutex> lock(m_mutex);
                m_scannedPlugins = std::move(results);
            }
        } catch (...) {
            success = false;
        }
        
        m_scanning.store(false);
        
        // Call completion callback
        if (completeCallback) {
            std::lock_guard<std::mutex> lock(m_mutex);
            completeCallback(m_scannedPlugins, success);
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
        
        if (lowerName.find(lowerQuery) != std::string::npos ||
            lowerVendor.find(lowerQuery) != std::string::npos) {
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
        if (!file.is_open()) return false;
        
        // Write header
        const char magic[4] = {'N', 'P', 'S', 'C'}; // Nomad Plugin Scan Cache
        file.write(magic, 4);
        
        uint32_t version = 1;
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
        if (!file.is_open()) return false;
        
        // Read header
        char magic[4];
        file.read(magic, 4);
        if (magic[0] != 'N' || magic[1] != 'P' || magic[2] != 'S' || magic[3] != 'C') {
            return false;
        }
        
        uint32_t version;
        file.read(reinterpret_cast<char*>(&version), sizeof(version));
        if (version != 1) return false;
        
        // Read plugin count
        uint32_t count;
        file.read(reinterpret_cast<char*>(&count), sizeof(count));
        
        std::vector<PluginInfo> plugins;
        plugins.reserve(count);
        
        // Read each plugin
        auto readString = [&file]() -> std::string {
            uint32_t len;
            file.read(reinterpret_cast<char*>(&len), sizeof(len));
            std::string s(len, '\0');
            file.read(s.data(), len);
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
            
            plugins.push_back(std::move(p));
        }
        
        m_scannedPlugins = std::move(plugins);
        return true;
    } catch (...) {
        return false;
    }
}

void PluginScanner::clearCache() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_scannedPlugins.clear();
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

void PluginScanner::scanDirectory(const std::filesystem::path& dir,
                                  std::vector<PluginInfo>& results,
                                  ScanProgressCallback callback,
                                  int& currentIndex,
                                  int totalCount) {
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir,
                std::filesystem::directory_options::skip_permission_denied)) {
            
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
#ifdef NOMAD_HAS_VST3
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
#ifdef NOMAD_HAS_CLAP
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
            if (!std::filesystem::exists(path)) continue;
            
            for (const auto& entry : std::filesystem::recursive_directory_iterator(path,
                    std::filesystem::directory_options::skip_permission_denied)) {
                if (entry.path().extension() == ".vst3" ||
                    entry.path().extension() == ".clap") {
                    ++count;
                }
            }
        } catch (...) {
            // Skip inaccessible directories
        }
    }
    
    return count;
}

} // namespace Audio
} // namespace Nomad
