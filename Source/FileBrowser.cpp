// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "FileBrowser.h"
#include "../NomadUI/Core/NUIContextMenu.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Core/NUIDragDrop.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "../NomadUI/Core/NUITextInput.h"
#include "../NomadCore/include/NomadLog.h"
#include "../NomadAudio/include/AudioFileValidator.h"
#include "../NomadAudio/include/MiniAudioDecoder.h"
#include "../NomadPlat/include/NomadPlatform.h"
#include "../NomadUI/Platform/NUIPlatformBridge.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <fstream>

#ifdef _WIN32
#include <Windows.h>
#endif

using namespace Nomad;

namespace NomadUI {

namespace {

constexpr float kPreviewPanelHeight = 90.0f;

std::string ellipsizeMiddle(NUIRenderer& renderer, const std::string& text, float fontSize, float maxWidth) {
    constexpr const char* kEllipsis = "...";

    if (text.empty()) return text;
    if (maxWidth <= 0.0f) return "";

    const float ellipsisW = renderer.measureText(kEllipsis, fontSize).width;
    if (ellipsisW >= maxWidth) return std::string(kEllipsis);

    if (renderer.measureText(text, fontSize).width <= maxWidth) {
        return text;
    }

    const size_t extPos = text.find_last_of('.');
    const size_t extLen = (extPos != std::string::npos) ? (text.size() - extPos) : 0;
    const size_t suffixMin = (extLen > 0 && extPos > 0 && extLen <= 8) ? extLen : 1;

    size_t leftKeep = std::max<size_t>(1, text.size() / 2);
    size_t rightKeep = std::max<size_t>(suffixMin, text.size() - leftKeep);

    if (leftKeep + rightKeep >= text.size()) {
        rightKeep = std::min(rightKeep, text.size() - 1);
        leftKeep = std::max<size_t>(1, text.size() - rightKeep);
    }

    for (size_t iter = 0; iter < text.size(); ++iter) {
        std::string candidate = text.substr(0, leftKeep) + kEllipsis + text.substr(text.size() - rightKeep);
        if (renderer.measureText(candidate, fontSize).width <= maxWidth) {
            return candidate;
        }

        const bool canTrimLeft = leftKeep > 1;
        const bool canTrimRight = rightKeep > suffixMin;
        if (!canTrimLeft && !canTrimRight) break;

        // Prefer trimming the longer side while preserving the extension suffix.
        if (canTrimLeft && (!canTrimRight || leftKeep > rightKeep)) {
            --leftKeep;
        } else if (canTrimRight) {
            --rightKeep;
        }
    }

	return std::string(kEllipsis);
}

std::filesystem::path canonicalOrNormalized(const std::filesystem::path& p) {
    try {
        return std::filesystem::weakly_canonical(p);
    } catch (...) {
        return p.lexically_normal();
    }
}

std::string normalizedPathForCompare(const std::filesystem::path& p) {
    std::string s = canonicalOrNormalized(p).generic_string();
#if defined(_WIN32)
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return s;
}

std::string mapKeyForPath(const std::string& path) {
    if (path.empty()) return "";
    std::string s = std::filesystem::path(path).lexically_normal().generic_string();
#if defined(_WIN32)
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return s;
}

bool isPathUnderRoot(const std::filesystem::path& candidatePath, const std::filesystem::path& rootPath) {
    const std::string candidate = normalizedPathForCompare(candidatePath);
    std::string root = normalizedPathForCompare(rootPath);
    if (root.empty()) return true;

    // Allow exact match.
    if (candidate == root) return true;

    // Ensure `root/` prefix match.
    if (root.back() != '/') root.push_back('/');
    if (candidate.size() < root.size()) return false;
    return candidate.compare(0, root.size(), root) == 0;
}

// Generate waveform overview from decoded audio samples
// Downsamples to targetSize bins, computing peak amplitude per bin
std::vector<float> generateWaveformFromAudio(const std::vector<float>& samples, 
                                              uint32_t numChannels, 
                                              size_t targetSize = 256) {
    std::vector<float> waveform(targetSize, 0.0f);
    if (samples.empty() || numChannels == 0) return waveform;
    
    size_t totalFrames = samples.size() / numChannels;
    float framesPerBin = static_cast<float>(totalFrames) / targetSize;
    
    for (size_t bin = 0; bin < targetSize; ++bin) {
        size_t startFrame = static_cast<size_t>(bin * framesPerBin);
        size_t endFrame = static_cast<size_t>((bin + 1) * framesPerBin);
        endFrame = std::min(endFrame, totalFrames);
        
        float maxAmp = 0.0f;
        for (size_t frame = startFrame; frame < endFrame; ++frame) {
            // Mix all channels for mono-sum peak
            float sum = 0.0f;
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                sum += std::abs(samples[frame * numChannels + ch]);
            }
            maxAmp = std::max(maxAmp, sum / numChannels);
        }
        waveform[bin] = std::min(1.0f, maxAmp);  // Clamp to 1.0
    }
    
    return waveform;
}

} // namespace

FileBrowser::FileBrowser()
    : NUIComponent()
    , selectedFile_(nullptr)
    , selectedIndex_(-1)
    , scrollOffset_(0.0f)
    , targetScrollOffset_(0.0f)   // Initialize lerp target
    , scrollVelocity_(0.0f)
    , itemHeight_(36.0f)           // Initialize to default theme value (matches theme)
    , visibleItems_(0)
    , showHiddenFiles_(false)
    , lastCachedWidth_(0.0f)       // Initialize cache width tracker
    , lastRenderedOffset_(0.0f)    // Initialize render tracking
    , effectiveWidth_(0.0f)        // Initialize effective render width
    , scrollbarVisible_(false)
    , scrollbarOpacity_(0.0f)
    , scrollbarWidth_(8.0f)        // Initialize to default theme value
    , scrollbarTrackHeight_(0.0f)
    , scrollbarThumbHeight_(0.0f)
    , scrollbarThumbY_(0.0f)
    , isDraggingScrollbar_(false)
    , scrollbarHovered_(false)
    , dragStartY_(0.0f)
    , dragStartScrollOffset_(0.0f)
    , scrollbarFadeTimer_(0.0f)
    , hoveredIndex_(-1)
    , lastClickedIndex_(-1)
    , lastClickTime_(0.0)
    , sortMode_(SortMode::Name)
    , sortAscending_(true)
    , lastShiftSelectIndex_(-1)
    , isLoadingPlayback_(false)    // Not loading playback initially
    , wasLoadingPlayback_(false)
	    , hoveredBreadcrumbIndex_(-1)
    , navHistoryIndex_(-1)
    , isNavigatingHistory_(false)
{
    // Set default size from theme
    // ... (theme logic handled in onResize)

    // DEFAULT PATH & SANDBOX LOGIC
    // User requested "/Documents/Nomad" as the root.
    std::string userProfile = "";
#if defined(_WIN32)
    const char* profileEnv = std::getenv("USERPROFILE");
    if (profileEnv) userProfile = profileEnv;
#else
    const char* homeEnv = std::getenv("HOME");
    if (homeEnv) userProfile = homeEnv;
#endif

    std::string targetRoot = "";
    if (!userProfile.empty()) {
        targetRoot = (std::filesystem::path(userProfile) / "Documents" / "Nomad").string();
    } else {
        // Fallback for this specific environment if env var fails
        targetRoot = "C:/Users/Current/Documents/Nomad";
    }

    std::filesystem::path docsPath(targetRoot);
    std::error_code ec;
    
    // Attempt creation
    if (!std::filesystem::exists(docsPath, ec)) {
        std::filesystem::create_directories(docsPath, ec);
    }

    // Validation
    if (std::filesystem::exists(docsPath, ec)) {
        rootPath_ = docsPath.string();
        currentPath_ = rootPath_;
        Nomad::Log::info("[FileBrowser] Set root to: " + rootPath_);
    } else {
         // Final Fallback to CWD if creation fails
         rootPath_ = std::filesystem::current_path().string();
         currentPath_ = rootPath_;
         Nomad::Log::warning("[FileBrowser] Failed to set default root, fallback to CWD: " + rootPath_);
    }
    
    // Initial scan happens in onUpdate/onResize or explicit load?
    // We usually wait for first render/update, but let's ensure it's validated.
    // loadState will override this if settings exist.
    
    // Start scan
    // loadDirectoryContents(); // Called in onResize usually or first update?
    // Actually, let's call it here to be safe, assuming thread is ready.
    // Thread worker starts lazily.

    auto& themeManager = NUIThemeManager::getInstance();
    float defaultWidth = themeManager.getLayoutDimension("fileBrowserWidth");
    float defaultHeight = 300.0f; // Default height
    setSize(defaultWidth, defaultHeight);

    // Initialize search input
    // Initialize search input
    searchInput_ = std::make_shared<NUITextInput>();
    searchInput_->setPlaceholderText("Search files...");
    addChild(searchInput_);
    
    // Bind search callback
    searchInput_->setOnTextChange([this](const std::string& text) {
        applyFilter();
    });
    searchInput_->setMaxLength(512); 
    searchInput_->setTextColor(themeManager.getColor("textPrimary")); 

    // Initialize icons with improved visibility for Liminal Dark v2.0
    // Use inline SVG content for reliable icon loading
    // Folder icon (Mac-style smooth)
    folderIcon_ = std::make_shared<NUIIcon>();
    const char* folderSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M20 6h-8l-2-2H4c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2zm-2.06 11L15 10l.94-2H21v9h-3.06z" opacity="0.8"/><path d="M20,6H12L10,4H4A2,2,0,0,0,2,6V18A2,2,0,0,0,4,20H20A2,2,0,0,0,22,18V8A2,2,0,0,0,20,6Z"/></svg>)";
    folderIcon_->loadSVG(folderSvg);
    folderIcon_->setIconSize(20, 20); 
    folderIcon_->setColor(themeManager.getColor("textSecondary"));
    
    // File Icon (Generic) -> unknownFileIcon_
    unknownFileIcon_ = std::make_shared<NUIIcon>();
    const char* fileSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M14 2H6c-1.1 0-1.99.9-1.99 2L4 20c0 1.1.89 2 1.99 2H18c1.1 0 2-.9 2-2V8l-6-6zm2 16H8v-2h8v2zm0-4H8v-2h8v2zm-3-5V3.5L18.5 9H13z"/></svg>)";
    unknownFileIcon_->loadSVG(fileSvg);
    unknownFileIcon_->setIconSize(20, 20);
    unknownFileIcon_->setColor(themeManager.getColor("textSecondary"));

    // Generic Audio Icon -> audioFileIcon_ (Standard Music Note)
    audioFileIcon_ = std::make_shared<NUIIcon>();
    const char* audioSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M12 3v10.55c-.59-.34-1.27-.55-2-.55-2.21 0-4 1.79-4 4s1.79 4 4 4 4-1.79 4-4V7h4V3h-6z"/></svg>)";
    audioFileIcon_->loadSVG(audioSvg);
    audioFileIcon_->setIconSize(20, 20);
    audioFileIcon_->setColor(themeManager.getColor("textSecondary"));

    // WAV Icon (Waveform visual)
    wavFileIcon_ = std::make_shared<NUIIcon>();
    const char* wavSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M6 9.25a.75.75 0 0 1 .75.75v4a.75.75 0 0 1-1.5 0v-4a.75.75 0 0 1 .75-.75Zm3-3a.75.75 0 0 1 .75.75v10a.75.75 0 0 1-1.5 0v-10A.75.75 0 0 1 9 6.25Zm3 2.5a.75.75 0 0 1 .75.75v6.5a.75.75 0 0 1-1.5 0v-6.5a.75.75 0 0 1 .75-.75Zm3-1.5a.75.75 0 0 1 .75.75v9.5a.75.75 0 0 1-1.5 0v-9.5A.75.75 0 0 1 15 7.25Zm3 3.5a.75.75 0 0 1 .75.75v3a.75.75 0 0 1-1.5 0v-3a.75.75 0 0 1 .75-.75Z"/></svg>)";
    wavFileIcon_->loadSVG(wavSvg);
    wavFileIcon_->setIconSize(20, 20);
    wavFileIcon_->setColor(themeManager.getColor("textSecondary"));

    // MP3 Icon (Music Note Circle)
    mp3FileIcon_ = std::make_shared<NUIIcon>();
    const char* mp3Svg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm0 14.5c-2.49 0-4.5-2.01-4.5-4.5S9.51 7.5 12 7.5s4.5 2.01 4.5 4.5-2.01 4.5-4.5 4.5zm0-5.5c-.55 0-1 .45-1 1s.45 1 1 1 1-.45 1-1-.45-1-1-1z"/></svg>)";
    mp3FileIcon_->loadSVG(mp3Svg);
    mp3FileIcon_->setIconSize(20, 20);
    mp3FileIcon_->setColor(themeManager.getColor("textSecondary"));

    // FLAC Icon (HQ High fidelity box)
    flacFileIcon_ = std::make_shared<NUIIcon>();
    const char* flacSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M19 3H5c-1.1 0-2 .9-2 2v14c0 1.1.9 2 2 2h14c1.1 0 2-.9 2-2V5c0-1.1-.9-2-2-2zm-7 14h-2v-2h2v2zm0-4h-2V7h2v6zm4 4h-2v-6h2v6zm0-8h-2V7h2v2z"/></svg>)";
    flacFileIcon_->loadSVG(flacSvg);
    flacFileIcon_->setIconSize(20, 20);
    flacFileIcon_->setColor(themeManager.getColor("textSecondary"));

    // Project Icon (Nomad Diamond)
    projectFileIcon_ = std::make_shared<NUIIcon>();
    const char* projectSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M12 2L4 12l8 10 8-10-8-10zm0 3.75l5 6.25-5 6.25-5-6.25 5-6.25z"/></svg>)";
    projectFileIcon_->loadSVG(projectSvg);
    projectFileIcon_->setIconSize(20, 20);
    projectFileIcon_->setColor(themeManager.getColor("accentPrimary"));

    // Music File Icon (Default for other audio)
    musicFileIcon_ = std::make_shared<NUIIcon>();
    musicFileIcon_->loadSVG(audioSvg);
    musicFileIcon_->setIconSize(20, 20);
    musicFileIcon_->setColor(themeManager.getColor("textSecondary"));

    // Chevron Right (Collapsed)
    chevronIcon_ = std::make_shared<NUIIcon>();
    const char* chevronSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M10 6L8.59 7.41 13.17 12l-4.58 4.59L10 18l6-6z"/></svg>)";
    chevronIcon_->loadSVG(chevronSvg);
    chevronIcon_->setIconSize(16, 16);
    chevronIcon_->setColor(themeManager.getColor("textSecondary"));

    // Chevron Down (Expanded)
    chevronDownIcon_ = std::make_shared<NUIIcon>();
    const char* chevronDownSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M16.59 8.59L12 13.17 7.41 8.59 6 10l6 6 6-6z"/></svg>)";
    chevronDownIcon_->loadSVG(chevronDownSvg);
    chevronDownIcon_->setIconSize(16, 16);
    chevronDownIcon_->setColor(themeManager.getColor("textSecondary"));

    // Refresh Icon
    refreshIcon_ = std::make_shared<NUIIcon>();
    const char* refreshSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M17.65 6.35C16.2 4.9 14.21 4 12 4c-4.42 0-7.99 3.58-7.99 8s3.57 8 7.99 8c3.73 0 6.84-2.55 7.73-6h-2.08c-.82 2.33-3.04 4-5.65 4-3.31 0-6-2.69-6-6s2.69-6 6-6c1.66 0 3.14.69 4.22 1.78L13 11h7V4l-2.35 2.35z"/></svg>)";
    refreshIcon_->loadSVG(refreshSvg);
    refreshIcon_->setIconSize(16, 16);
    refreshIcon_->setColor(themeManager.getColor("textSecondary"));

    // Star Icon (Favorites - Empty)
    starIcon_ = std::make_shared<NUIIcon>();
    const char* starSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M22 9.24l-7.19-.62L12 2 9.19 8.63 2 9.24l5.46 4.73L5.82 21 12 17.27 18.18 21l-1.63-7.03L22 9.24zM12 15.4l-3.76 2.27 1-4.28-3.32-2.88 4.38-.38L12 6.1l1.71 4.04 4.38.38-3.32 2.88 1 4.28L12 15.4z"/></svg>)";
    starIcon_->loadSVG(starSvg);
    starIcon_->setIconSize(16, 16);
    starIcon_->setColor(themeManager.getColor("textSecondary"));

    // Star Icon (Favorites - Filled)
    starFilledIcon_ = std::make_shared<NUIIcon>();
    const char* starFilledSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M12 17.27L18.18 21l-1.64-7.03L22 9.24l-7.19-.61L12 2 9.19 8.63 2 9.24l5.46 4.73L5.82 21z"/></svg>)";
    starFilledIcon_->loadSVG(starFilledSvg);
    starFilledIcon_->setIconSize(16, 16);
    starFilledIcon_->setColor(themeManager.getColor("accentPrimary"));
    popupMenu_ = std::make_shared<NUIContextMenu>();
    popupMenu_->hide();
    addChild(popupMenu_);
    
    // Initialize navigation history with the resolved root path (from top of constructor)
    navHistory_.clear();
    navHistory_.push_back(currentPath_);
    navHistoryIndex_ = 0;
    
    // Load Liminal Dark v2.0 theme colors
    backgroundColor_ = themeManager.getColor("backgroundSecondary");  // #1b1b1f - Panels, sidebars, file browser
    textColor_ = themeManager.getColor("textPrimary");                // #e6e6eb - Soft white
    
    // Use a refined purple accent to match the icons
    selectedColor_ = NUIColor(0.733f, 0.525f, 0.988f, 1.0f);          // #bb86fc - Matching the folder icons
    
    hoverColor_ = NUIColor(1.0f, 1.0f, 1.0f, 0.02f); // Glass hover (ultra-clear)
    borderColor_ = themeManager.getColor("interfaceBorder");          // #2e2e35 - Subtle separation lines
    
    // Perform initial layout now that all members (icons, search input) are initialized
    // This initializes scrollbarTrackHeight_ and other layout vars needed by updateScrollbarVisibility
    onResize(static_cast<int>(getWidth()), static_cast<int>(getHeight()));

    // NOW start the scan, strictly after layout is ready
    loadDirectoryContents();
    // Nomad::Log::info("[FileBrowser] Constructor complete.");
}

FileBrowser::~FileBrowser() {
    stopScanWorker();
}

void FileBrowser::ensureScanWorker() {
    if (scanWorkerStarted_) return;
    scanStop_.store(false, std::memory_order_release);
    scanWorker_ = std::thread([this]() { scanWorkerLoop(); });
    scanWorkerStarted_ = true;
}

void FileBrowser::stopScanWorker() {
    if (!scanWorkerStarted_) return;

    scanStop_.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(scanMutex_);
        scanTasks_.clear();
        scanResults_.clear();
    }
    scanCv_.notify_all();

    if (scanWorker_.joinable()) {
        scanWorker_.join();
    }

    scanWorkerStarted_ = false;
}

void FileBrowser::enqueueScan(ScanKind kind, const std::string& path, int depth) {
    ensureScanWorker();

    ScanTask task;
    task.kind = kind;
    task.path = path;
    task.depth = depth;
    task.showHidden = showHiddenFiles_;
    task.generation = scanGeneration_.load(std::memory_order_acquire);

    {
        std::lock_guard<std::mutex> lock(scanMutex_);
        scanTasks_.push_back(std::move(task));
    }
    scanCv_.notify_one();
}

void FileBrowser::scanWorkerLoop() {
    while (true) {
        ScanTask task;
        {
            std::unique_lock<std::mutex> lock(scanMutex_);
            scanCv_.wait(lock, [&]() {
                return scanStop_.load(std::memory_order_acquire) || !scanTasks_.empty();
            });

            if (scanStop_.load(std::memory_order_acquire) && scanTasks_.empty()) {
                return;
            }

            task = std::move(scanTasks_.front());
            scanTasks_.pop_front();
        }

        const uint64_t currentGen = scanGeneration_.load(std::memory_order_acquire);
        if (task.generation != currentGen) {
            continue;
        }

        ScanResult result;
        result.kind = task.kind;
        result.path = task.path;
        result.depth = task.depth;
        result.generation = task.generation;
        result.items = scanDirectory(task.path, task.depth, task.showHidden, task.generation);

        {
            std::lock_guard<std::mutex> lock(scanMutex_);
            scanResults_.push_back(std::move(result));
        }
    }
}

const std::unordered_set<std::string> FileFilter::audioExtensions = {
    ".wav", ".aif", ".aiff", ".mp3", ".flac", ".ogg", ".mp4", ".m4a"
};

const std::unordered_set<std::string> FileFilter::projectExtensions = {
    ".madproj", ".nomad"
};

bool FileFilter::isAllowed(const std::string& path) {
    if (path.empty()) return false;
    
    // Always allow visible directories (caller handles hidden check)
    if (std::filesystem::is_directory(path)) return true;

    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (audioExtensions.count(ext)) return true;
    if (projectExtensions.count(ext)) return true;

    return false;
}

FileType FileFilter::getType(const std::string& path, bool isDir) {
    if (isDir) return FileType::Folder;

    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".wav") return FileType::WavFile;
    if (ext == ".mp3") return FileType::Mp3File;
    if (ext == ".flac") return FileType::FlacFile;
    if (ext == ".ogg") return FileType::MusicFile;
    if (ext == ".aif" || ext == ".aiff") return FileType::AudioFile;
    if (projectExtensions.count(ext)) return FileType::ProjectFile;

    return FileType::Unknown;
}

std::vector<FileItem> FileBrowser::scanDirectory(const std::string& path, int depth, bool showHidden, uint64_t generation) const {
    std::vector<FileItem> items;
    try {
        const std::filesystem::path dir(path);
        const auto options = std::filesystem::directory_options::skip_permission_denied;

        for (const auto& entry : std::filesystem::directory_iterator(dir, options)) {
            if (scanStop_.load(std::memory_order_acquire) ||
                generation != scanGeneration_.load(std::memory_order_acquire)) {
                break;
            }

            const std::string name = entry.path().filename().string();
            if (!showHidden && !name.empty() && name[0] == '.') {
                continue;
            }

            const std::string entryPath = entry.path().string();
            
            // --- SMART FILTER APPLIED HERE ---
            // If it's not a directory and not in our whitelist, skip it.
            bool isDir = false;
            try { isDir = entry.is_directory(); } catch(...) { continue; }

            if (!isDir && !FileFilter::isAllowed(entryPath)) {
                continue; // Whitelist filter
            }

            FileType type = FileFilter::getType(entryPath, isDir);
            size_t size = 0;
            std::string lastModified;

            if (!isDir) {
                 try {
                    size = entry.file_size();
                } catch (...) {
                    size = 0;
                }
            }

            FileItem item(name, entryPath, type, isDir, size, lastModified);
            item.depth = depth;
            items.push_back(std::move(item));
        }
    } catch (const std::exception& e) {
        Log::warning(std::string("[FileBrowser] Scan failed for ") + path + ": " + e.what());
    }

    return items;
}

FileItem* FileBrowser::findItemByPath(const std::string& path) {
    std::function<FileItem*(std::vector<FileItem>&)> findRecursive = [&](std::vector<FileItem>& items) -> FileItem* {
        for (auto& item : items) {
            if (item.path == path) return &item;
            if (!item.children.empty()) {
                if (auto* found = findRecursive(item.children)) return found;
            }
        }
        return nullptr;
    };

    return findRecursive(rootItems_);
}

void FileBrowser::processScanResults() {
    std::deque<ScanResult> results;
    {
        std::lock_guard<std::mutex> lock(scanMutex_);
        if (scanResults_.empty()) return;
        results.swap(scanResults_);
    }

    const uint64_t currentGen = scanGeneration_.load(std::memory_order_acquire);
    bool didUpdate = false;

    for (auto& result : results) {
        if (result.generation != currentGen) continue;

        if (result.kind == ScanKind::Root) {
            scanningRoot_ = false;

            rootItems_ = std::move(result.items);
            sortFiles();
            updateDisplayList();

            if (isFilterActive()) {
                applyFilter();
            } else {
                filteredFiles_.clear();
                viewDirty_ = true;
                if (!displayItems_.empty()) {
                    selectedIndex_ = 0;
                    selectedFile_ = displayItems_[0];
                    selectedIndices_.clear();
                    selectedIndices_.push_back(0);
                    lastShiftSelectIndex_ = 0;
                } else {
                    clearSelection();
                }
                updateScrollbarVisibility();
                setDirty(true);
            }

            didUpdate = true;
            continue;
        }

        if (result.kind == ScanKind::Folder) {
            if (FileItem* folder = findItemByPath(result.path)) {
                folder->children = std::move(result.items);
                folder->hasLoadedChildren = true;
                folder->isLoadingChildren = false;

                std::stable_sort(folder->children.begin(), folder->children.end(),
                                 [this](const FileItem& a, const FileItem& b) { return compareFileItems(a, b); });

                updateDisplayList();
                if (isFilterActive()) {
                    applyFilter();
                } else {
                    updateScrollbarVisibility();
                    setDirty(true);
                }
                didUpdate = true;
            }
        }
    }

    if (didUpdate) {
        updateScrollbarVisibility();
    }
}

void FileBrowser::onRender(NUIRenderer& renderer) {
    NUIRect bounds = getBounds();
    if (bounds.isEmpty()) return;
    
    // Adjust bounds if preview panel is visible (bottom panel)
    float fileBrowserHeight = bounds.height;
    // Preview panel logic removed - FileBrowser now fills entire bounds
    
    effectiveWidth_ = bounds.width; // Full width for file list
    
    // Update scrollbar track height to match current visible area
    // This ensures drag and scroll clamping work correctly
    auto& themeManager = NUIThemeManager::getInstance();
    // Re-calculate header height (must match onMouseEvent/onResize logic)
    const float buttonsRowHeight = 40.0f;
    const float breadcrumbRowHeight = 32.0f;
    const float searchRowHeight = 36.0f; 
    const float rowSpacing = 8.0f;
    float totalHeaderH = buttonsRowHeight + breadcrumbRowHeight + rowSpacing + searchRowHeight + rowSpacing;
    
    // Store for other methods to use
    scrollbarTrackHeight_ = fileBrowserHeight - totalHeaderH;
    
    NUIRect fileBrowserBounds(bounds.x, bounds.y, bounds.width, fileBrowserHeight);
    
    // Render background with enhanced borders
    renderer.fillRoundedRect(fileBrowserBounds, 8, backgroundColor_);
    
    // Main border
    renderer.strokeRoundedRect(fileBrowserBounds, 8, 1, borderColor_);
    
    // Inner black border for cleaner look
    NUIRect innerBounds(fileBrowserBounds.x + 1, fileBrowserBounds.y + 1, fileBrowserBounds.width - 2, fileBrowserBounds.height - 2);
    renderer.strokeRoundedRect(innerBounds, 7, 1, NUIColor(0.0f, 0.0f, 0.0f, 0.4f));
    
    // Render toolbar with search box
    renderToolbar(renderer);

    // Render file list
    renderFileList(renderer);
    
    // Render scrollbar
    renderScrollbar(renderer);
    


	    // Popup menus / overlays
	    renderChildren(renderer);
	    
	    // Loading spinner removed - now handled by FilePreviewPanel
	}

void FileBrowser::onUpdate(double deltaTime) {
	    NUIComponent::onUpdate(deltaTime);

    // Apply any completed async directory scans (keeps UI responsive on huge folders).
    processScanResults();
    
    // Loading animation removed - now handled by FilePreviewPanel
	    
    // Smooth scrolling with lerp
    float lerpSpeed = 12.0f;
    float snapThreshold = 0.5f;

    float scrollDelta = targetScrollOffset_ - scrollOffset_;
    if (std::abs(scrollDelta) > snapThreshold) {
        float step = std::min(1.0f, static_cast<float>(deltaTime * lerpSpeed));
        scrollOffset_ += scrollDelta * step;
        // Keep scrollbar visible while scrolling
        scrollbarFadeTimer_ = 0.0f;
        scrollbarOpacity_ = 1.0f;
        setDirty(true);
    } else {
        scrollOffset_ = targetScrollOffset_;
    }
    scrollVelocity_ = scrollDelta;
    
    // ALWAYS repaint if scroll position changed at all
    if (std::abs(scrollOffset_ - lastRenderedOffset_) > 0.01f) {
        lastRenderedOffset_ = scrollOffset_;
        setDirty(true);
    }
    
    // Update scrollbar thumb position based on current scroll
    const auto& view = getActiveView();
    float maxScroll = std::max(0.0f, view.size() * itemHeight_ - scrollbarTrackHeight_);
    if (maxScroll > 0.0f) {
        scrollbarThumbY_ = (scrollOffset_ / maxScroll) * (scrollbarTrackHeight_ - scrollbarThumbHeight_);
    }

	    // Auto-hide scrollbar when idle
	    if (scrollbarVisible_) {
	        if (isDraggingScrollbar_) {
	            scrollbarFadeTimer_ = 0.0f;
	            scrollbarOpacity_ = 1.0f;
        } else {
            scrollbarFadeTimer_ += static_cast<float>(deltaTime);
            if (scrollbarFadeTimer_ > SCROLLBAR_FADE_DELAY) {
                float t = (scrollbarFadeTimer_ - SCROLLBAR_FADE_DELAY) / SCROLLBAR_FADE_DURATION;
                float newOpacity = std::max(0.0f, 1.0f - std::min(1.0f, t));
                if (std::abs(newOpacity - scrollbarOpacity_) > 0.001f) {
                    scrollbarOpacity_ = newOpacity;
                    setDirty(true);
	            }
	        }
	    }

    // Search caret blink handled by NUITextInput now
}
}

void FileBrowser::onResize(int width, int height) {
    NUIComponent::onResize(width, height);

    auto& themeManager = NUIThemeManager::getInstance();
    // Ignore legacy headerHeight, define our own stack
    const float buttonsRowHeight = 40.0f;     // Increased from 36
    const float breadcrumbRowHeight = 32.0f;  // Increased from 28
    const float searchRowHeight = 36.0f;      // Increased from 32
    const float innerPad = 8.0f;
    const float rowSpacing = 8.0f;            // Increased from 4
    
    // 1. Header background area (Buttons + Breadcrumbs)
    // We don't set bounds for this render pass, but renderToolbar uses getBounds() top area.
    
    // 2. Search Input Position
    // Position search input below Buttons and Breadcrumbs
    const float searchY = getBounds().y + buttonsRowHeight + breadcrumbRowHeight + rowSpacing;
    
    if (searchInput_) {
        // Full width minus padding
        NUIRect searchBounds(
            getBounds().x + innerPad, 
            searchY, 
            static_cast<float>(width) - innerPad * 2.0f, 
            searchRowHeight
        );
        searchInput_->setBounds(searchBounds);
        
        searchInput_->setTextColor(textColor_);
        searchInput_->setBackgroundColor(themeManager.getColor("inputBgDefault"));
        searchInput_->setBorderColor(borderColor_.withAlpha(0.5f));
        searchInput_->setBorderRadius(themeManager.getRadius("s"));
    }
    
    // 3. File List
    itemHeight_ = themeManager.getComponentDimension("fileBrowser", "itemHeight");
    
    // FIX: Calculate list height taking preview panel into account, consistent with onMouseEvent
    float availableHeight = height;

    const float listYOffset = (searchY - getBounds().y) + searchRowHeight + rowSpacing;
    float listHeight = availableHeight - listYOffset;
    
    visibleItems_ = static_cast<int>(listHeight / itemHeight_);
    visibleItems_ = std::max(1, visibleItems_);

    // Update scrollbar dimensions
    float scrollbarWidth = themeManager.getComponentDimension("fileBrowser", "scrollbarWidth");
    scrollbarTrackHeight_ = listHeight; 
    scrollbarWidth_ = scrollbarWidth;

    // Update caches
    updateScrollPosition();
    updateBreadcrumbs();
    updateScrollbarVisibility(); 
    invalidateAllItemCaches(); // Force text re-layout on resize
}

void FileBrowser::invalidateAllItemCaches() {
    std::vector<NomadUI::FileItem*> stack;
    for (auto& item : rootItems_) {
        stack.push_back(&item);
    }
    
    while (!stack.empty()) {
        NomadUI::FileItem* item = stack.back();
        stack.pop_back();
        
        item->cacheValid = false;
        item->cachedDisplayName.clear();
        item->cachedSizeStr.clear();
        
        for (auto& child : item->children) {
            stack.push_back(&child);
        }
    }
}

bool FileBrowser::onMouseEvent(const NUIMouseEvent& event) {
    lastMousePos_ = event.position;
    NUIRect bounds = getBounds();
    const auto& view = getActiveView();
    auto& themeManager = NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
	    float headerHeight = themeManager.getComponentDimension("fileBrowser", "headerHeight");
	    float itemHeight = themeManager.getComponentDimension("fileBrowser", "itemHeight");

    // NEW STACK LAYOUT LOGIC (Shared with onResize and renderToolbar)
    const float buttonsRowHeight = 40.0f;
    const float breadcrumbRowHeight = 32.0f;
    const float searchRowHeight = 36.0f; 
    const float rowSpacing = 8.0f;
    
    // Calculate header total height
    float totalHeaderH = buttonsRowHeight + breadcrumbRowHeight + rowSpacing + searchRowHeight + rowSpacing;
    
    // FIX: Calculate list height taking preview panel into account
    float availableHeight = bounds.height;

    
    float listY = bounds.y + totalHeaderH; 
    float listHeight = availableHeight - totalHeaderH;
	    float effectiveW = effectiveWidth_ > 0 ? effectiveWidth_ : bounds.width;
        
    // Update scrollbar track height ensuring it is fresh for this event
    scrollbarTrackHeight_ = listHeight;

    // If a click happens outside the search input, drop focus so shortcuts/navigation work normally.
    if (searchInput_ && event.pressed && event.button == NUIMouseButton::Left) {
        if (searchInput_->isFocused() && !searchInput_->getBounds().contains(event.position)) {
            searchInput_->setFocused(false);
        }
    }

    // Claim keyboard focus when interacting with the file browser (so it can own arrow-key navigation).
    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (bounds.contains(event.position)) {
            // If the click is on the search input, it will take focus itself.
            if (!searchInput_ || !searchInput_->getBounds().contains(event.position)) {
                setFocused(true);
            }
        }
    }
    
    // === DRAG AND DROP HANDLING ===
    auto& dragManager = NUIDragDropManager::getInstance();
    
    // If global drag is active, update it with mouse movement
    if (dragManager.isDragging()) {
        // Always update drag position on any mouse event
        dragManager.updateDrag(event.position);
        
        if (!event.pressed && event.button == NUIMouseButton::Left) {
            dragManager.endDrag(event.position);
            dragPotential_ = false;
            isDraggingFile_ = false;
            dragSourceIndex_ = -1;
            return true;
        }
        return true;  // Consume all events while dragging
    }

    // Popup menu handling (context + dropdowns)
    if (NUIComponent::onMouseEvent(event)) {
        return true;
    }
    if (popupMenu_ && popupMenu_->isVisible() &&
        event.pressed && (event.button == NUIMouseButton::Left || event.button == NUIMouseButton::Right) &&
        !popupMenu_->getBounds().contains(event.position)) {
        hidePopupMenu();
        // Continue processing the click (e.g., select item) after closing.
    }
    
    // Check for potential drag initiation (mouse moved while button held)
    if (dragPotential_ && dragSourceIndex_ >= 0 && dragSourceIndex_ < static_cast<int>(view.size())) {
        float dx = event.position.x - dragStartPos_.x;
        float dy = event.position.y - dragStartPos_.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        
	        if (dist >= dragManager.getDragThreshold()) {
	            // Start the drag!
	            const FileItem* dragFile = view[dragSourceIndex_];
	            
	            // Only drag allowed files (using Smart Filter whitelist)
	            if (!dragFile->isDirectory && FileFilter::isAllowed(dragFile->path)) {
	                NomadUI::DragData dragData;
	                dragData.type = NomadUI::DragDataType::File;
	                dragData.filePath = dragFile->path;
	                dragData.displayName = dragFile->name;
	                dragData.accentColor = NUIColor(0.733f, 0.525f, 0.988f, 1.0f); // Purple accent
                dragData.previewWidth = 150.0f;
                dragData.previewHeight = 30.0f;
	                
	                dragManager.beginDrag(dragData, dragStartPos_, this);
	                isDraggingFile_ = true;
	                dragPotential_ = false;
	                return true;
	            }
	            dragPotential_ = false;
	            dragSourceIndex_ = -1;
	            return true;
	        }
	    }
    
    // Cancel drag potential on mouse release
    if (!event.pressed && event.button == NUIMouseButton::Left) {
        dragPotential_ = false;
        dragSourceIndex_ = -1;
    }
    
    // If we're dragging the scrollbar, handle mouse events even outside bounds
    if (isDraggingScrollbar_) {
        // Always check scrollbar events if we're dragging
        if (handleScrollbarMouseEvent(event)) {
            return true;
        }
    }
    
    // Check if mouse is within bounds
    bool mouseInside = bounds.contains(event.position.x, event.position.y);

    // === MOUSE WHEEL SCROLLING (handle before bounds check so scrolling works on hover) ===
    if (mouseInside && event.wheelDelta != 0) {
        float contentHeight = view.size() * itemHeight;
        float maxScroll = std::max(0.0f, contentHeight - scrollbarTrackHeight_);
        bool needsScrollbar = maxScroll > 0.0f;
        
        if (needsScrollbar) {
            scrollbarFadeTimer_ = 0.0f;
            scrollbarOpacity_ = 1.0f;
        }
        float scrollSpeed = 3.0f; // Scroll 3 items per wheel step
        float scrollDelta = event.wheelDelta * scrollSpeed * itemHeight;
        
        targetScrollOffset_ -= scrollDelta;

        // Clamp target scroll offset
        targetScrollOffset_ = std::max(0.0f, std::min(targetScrollOffset_, maxScroll));

        setDirty(true);
        return true;  // Consume the wheel event
    }

    // Clear hover if mouse leaves the file browser entirely (but allow scrollbar dragging)
    if (!mouseInside && !isDraggingScrollbar_) {
        bool dirty = false;
        if (hoveredIndex_ != -1) {
            hoveredIndex_ = -1;
            dirty = true;
        }
        if (refreshHovered_ || favoritesHovered_ || tagsHovered_ || sortHovered_) {
            refreshHovered_ = false;
            favoritesHovered_ = false;
            tagsHovered_ = false;
            sortHovered_ = false;
            dirty = true;
        }
        if (dirty) setDirty(true);
        return false;
    }

    // Update toolbar hover states
    const bool newRefreshHovered = !refreshButtonBounds_.isEmpty() && refreshButtonBounds_.contains(event.position);
    const bool newFavoritesHovered = !favoritesButtonBounds_.isEmpty() && favoritesButtonBounds_.contains(event.position);
    const bool newTagsHovered = !tagsButtonBounds_.isEmpty() && tagsButtonBounds_.contains(event.position);
    const bool newSortHovered = !sortButtonBounds_.isEmpty() && sortButtonBounds_.contains(event.position);
    if (newRefreshHovered != refreshHovered_ || newFavoritesHovered != favoritesHovered_ ||
        newTagsHovered != tagsHovered_ || newSortHovered != sortHovered_) {
        refreshHovered_ = newRefreshHovered;
        favoritesHovered_ = newFavoritesHovered;
        tagsHovered_ = newTagsHovered;
        sortHovered_ = newSortHovered;
        setDirty(true);
    }
    
    // Handle scrollbar mouse events first - check if scrollbar should be visible
	    float contentHeight = view.size() * itemHeight;
	    float maxScroll = std::max(0.0f, contentHeight - scrollbarTrackHeight_);
	    bool needsScrollbar = maxScroll > 0.0f;
	    const float scrollbarGutter = needsScrollbar ? (scrollbarWidth_ + themeManager.getSpacing("xs")) : 0.0f;
	    const float listX = bounds.x + layout.panelMargin + scrollbarGutter;
	    const float listW = effectiveW - 2 * layout.panelMargin - scrollbarGutter;
    
    // Wheel handling moved earlier in function (before bounds check)
    // Toolbar controls
    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (!refreshButtonBounds_.isEmpty() && refreshButtonBounds_.contains(event.position)) {
            refresh();
            return true;
        }
        if (!favoritesButtonBounds_.isEmpty() && favoritesButtonBounds_.contains(event.position)) {
            showFavoritesMenu();
            return true;
        }
        if (!tagsButtonBounds_.isEmpty() && tagsButtonBounds_.contains(event.position)) {
            showTagFilterMenu();
            return true;
        }
        if (!sortButtonBounds_.isEmpty() && sortButtonBounds_.contains(event.position)) {
            showSortMenu();
            return true;
        }
    }

    // Breadcrumb hover (for chip highlight)
    if (!breadcrumbs_.empty() && breadcrumbBounds_.contains(event.position)) {
        int newHovered = -1;
        for (size_t i = 0; i < breadcrumbs_.size(); ++i) {
            const auto& crumb = breadcrumbs_[i];
            if (event.position.x >= crumb.x && event.position.x <= crumb.x + crumb.width) {
                newHovered = static_cast<int>(i);
                break;
            }
        }
        if (newHovered != hoveredBreadcrumbIndex_) {
            hoveredBreadcrumbIndex_ = newHovered;
            setDirty(true);
        }
    } else if (hoveredBreadcrumbIndex_ != -1) {
        hoveredBreadcrumbIndex_ = -1;
        setDirty(true);
    }

    // Breadcrumb interaction
    if (handleBreadcrumbMouseEvent(event)) {
        return true;
    }
    
    // Check scrollbar events if scrollbar is needed (but not dragging - handled above)
    if (needsScrollbar && !view.empty() && !isDraggingScrollbar_) {
        if (handleScrollbarMouseEvent(event)) {
            return true;
        }
    }
    
	    // Check if click is in file list area
	    if (event.position.x >= listX && event.position.x <= listX + listW &&
	        event.position.y >= listY && event.position.y <= listY + listHeight) {

        // Calculate which item is being hovered
        float relativeY = event.position.y - listY;
        int itemIndex = static_cast<int>((relativeY + scrollOffset_) / itemHeight);

	        // Update hover state
	        int newHoveredIndex = (itemIndex >= 0 && itemIndex < static_cast<int>(view.size())) ? itemIndex : -1;
	        if (newHoveredIndex != hoveredIndex_) {
	            hoveredIndex_ = newHoveredIndex;
	            setDirty(true); // Trigger redraw when hover state changes
	        }

	        // Context menu (right-click)
	        if (event.pressed && event.button == NUIMouseButton::Right) {
	            if (itemIndex >= 0 && itemIndex < static_cast<int>(view.size())) {
	                const FileItem* clickedFile = view[itemIndex];
	                if (clickedFile) {
	                    if (clickedFile->isPlaceholder) {
	                        return true;
	                    }
	                    // Keep multi-select if the right-clicked item is already selected; otherwise select it.
	                    const bool alreadySelected =
	                        (std::find(selectedIndices_.begin(), selectedIndices_.end(), itemIndex) != selectedIndices_.end());
		                    if (!alreadySelected) {
		                        toggleFileSelection(itemIndex, false, false);
		                        const auto& activeView = getActiveView();
		                        if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(activeView.size())) {
		                            selectedFile_ = activeView[selectedIndex_];
		                            if (onFileSelected_) {
		                                onFileSelected_(*selectedFile_);
	                            }
	                        }
	                    }

	                    dragPotential_ = false;
	                    dragSourceIndex_ = -1;
	                    showItemContextMenu(*clickedFile, event.position);
	                    return true;
	                }
	            }
	        }

	        if (event.pressed && event.button == NUIMouseButton::Left) {
	            // Calculate which file was clicked
	            float relativeY = event.position.y - listY;
	            int itemIndex = static_cast<int>((relativeY + scrollOffset_) / itemHeight);
	            
	            if (itemIndex >= 0 && itemIndex < static_cast<int>(view.size())) {
	                const FileItem* clickedFile = view[itemIndex];
	                if (!clickedFile || clickedFile->isPlaceholder) {
	                    return true;
	                }
	                
		                // Check for expander click (match renderFileList layout)
		                if (clickedFile->isDirectory) {
		                    const float indentStep = 18.0f;
		                    const float maxIndent = std::min(72.0f, listW * 0.35f);
		                    const float indent = std::min(static_cast<float>(clickedFile->depth) * indentStep, maxIndent);
		                    const float scrollbarGutter = needsScrollbar ? (scrollbarWidth_ + themeManager.getSpacing("xs")) : 0.0f;
		                    const float listX = bounds.x + layout.panelMargin + scrollbarGutter;
		                    const float contentX = listX + layout.panelMargin + indent;
		                    const float arrowSize = 12.0f;
		                    const float itemY = listY + (itemIndex * itemHeight) - scrollOffset_;
		                    const NUIRect arrowRect(contentX - 6.0f, itemY + (itemHeight - arrowSize) * 0.5f, arrowSize, arrowSize);

	                    if (arrowRect.contains(event.position)) {
	                        toggleFolder(const_cast<FileItem*>(clickedFile));
	                        return true;
	                    }
	                }

		                // Store drag potential state for allowed files
		                if (!clickedFile->isDirectory && FileFilter::isAllowed(clickedFile->path)) {
		                    dragPotential_ = true;
	                    dragSourceIndex_ = itemIndex;
	                    dragStartPos_ = event.position;
	                }
                
                // Get current time for double-click detection
                double currentTime = std::chrono::duration<double>(
                    std::chrono::steady_clock::now().time_since_epoch()
                ).count();
                
                // Check for double-click: same item clicked within time window
                bool isDoubleClick = (itemIndex == lastClickedIndex_) && 
                                    ((currentTime - lastClickTime_) < DOUBLE_CLICK_TIME);
                
                // Update click tracking
                lastClickedIndex_ = itemIndex;
                lastClickTime_ = currentTime;
                
                // Update selection with multi-select support
                bool ctrl = event.modifiers & NUIModifiers::Ctrl;
                bool shift = event.modifiers & NUIModifiers::Shift;
                toggleFileSelection(itemIndex, ctrl, shift);
	                // Update selectedFile_ from active view
	                const auto& activeView = getActiveView();
	                if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(activeView.size())) {
	                    selectedFile_ = activeView[selectedIndex_];
	                    
	                    // Waveform generation moved to FilePreviewPanel
	                    // waveformData_.clear();
	                    if (selectedFile_ && !selectedFile_->isDirectory) {
	                        std::string ext = std::filesystem::path(selectedFile_->path).extension().string();
	                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
	                        
	                        if (ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".ogg" || 
	                            ext == ".aif" || ext == ".aiff" || ext == ".m4a" || ext == ".mp4") {
	                            // Preview loading moved to FilePreviewPanel
	                            // isLoadingPreview_ = true;
	                            // loadingAnimationTime_ = 0.0f;
	                            
	                            // Capture path for async decode
	                            std::string filePath = selectedFile_->path;
	                            
	                            // Launch async decode thread
	                            std::thread([this, filePath]() {
	                                std::vector<float> audioData;
	                                uint32_t sampleRate = 0;
	                                uint32_t numChannels = 0;
	                                
	                                // Decode audio file
	                                bool success = Nomad::Audio::decodeAudioFile(
	                                    filePath, audioData, sampleRate, numChannels);
	                                
	                                // Generate waveform from decoded data
	                                std::vector<float> waveform;
	                                if (success && !audioData.empty()) {
	                                    waveform = generateWaveformFromAudio(audioData, numChannels, 256);
	                                }
	                                
	                                // Preview update moved to FilePreviewPanel
	                                // waveformData_ = std::move(waveform);
	                                // isLoadingPreview_ = false;
	                                
	                            }).detach();
	                        }
	                    }
	                    
	                    if (onFileSelected_) {
	                        onFileSelected_(*selectedFile_);
	                    }
	                }
                
                // Handle double-click: open folders or files
                if (isDoubleClick && selectedFile_) {
                    // Cancel drag potential on double-click
                    dragPotential_ = false;
                    dragSourceIndex_ = -1;
                    
                    if (selectedFile_->isDirectory) {
                        // Double-click on folder: toggle it
                        toggleFolder(const_cast<FileItem*>(selectedFile_));
                        // Clear double-click tracking so subsequent clicks start fresh
                        lastClickedIndex_ = -1;
                        lastClickTime_ = 0.0;
                    }
                    // Double-click on files now only previews; loading is Enter/drag-drop
                    if (onSoundPreview_ && !selectedFile_->isDirectory) {
                        FileType type = selectedFile_->type;
                        if (type == FileType::AudioFile || type == FileType::MusicFile ||
                            type == FileType::WavFile || type == FileType::Mp3File ||
                            type == FileType::FlacFile) {
                            // PreviewEngine now handles async decode internally
                            // No need for wrapper thread - just call directly
                            onSoundPreview_(*selectedFile_);
                        }
                    }
                } else {
                    // Single click: trigger sound preview for audio files
                    if (onSoundPreview_ && selectedFile_ && !selectedFile_->isDirectory) {
                        FileType type = selectedFile_->type;
                        if (type == FileType::AudioFile || type == FileType::MusicFile ||
                            type == FileType::WavFile || type == FileType::Mp3File ||
                            type == FileType::FlacFile) {
                            // PreviewEngine now handles async decode internally
                            // No need for wrapper thread - just call directly
                            onSoundPreview_(*selectedFile_);
                        }
                    }
                }
                
                setDirty(true);
                return true;
            }
        }
    }
    
    return false;
}

bool FileBrowser::onKeyEvent(const NUIKeyEvent& event) {
    if (!isVisible() || !isEnabled()) return false;
    
    // DOMINANT ROUTING: If search is focused, give it the event and STOP.
    // Do NOT let NUIComponent::onKeyEvent run if search handles it, to prevent double-handling or parent overrides.
    if (searchInput_ && searchInput_->isFocused()) {
        if (searchInput_->onKeyEvent(event)) return true;
        
        // If search didn't consume it (e.g. random key), we might want to let parents handle shortcuts like Ctrl+S?
        // But for typing safety, let's just fall through ONLY if it wasn't a typing key.
    }

    // Only handle navigation/shortcuts when the file browser itself owns focus.
    if (!isFocused()) return false;
    
    // Pass to children (if check above failed or wasn't focused)
    if (NUIComponent::onKeyEvent(event)) return true;

    if (event.pressed) {
        // Ctrl+F -> Focus Search
        if (event.keyCode == NUIKeyCode::F && (event.modifiers & NUIModifiers::Ctrl)) {
            if (searchInput_) {
                searchInput_->setFocused(true);
                return true;
            }
        }
        
        // Esc -> Clear search or blur
        if (event.keyCode == NUIKeyCode::Escape) {
            if (searchInput_ && searchInput_->isFocused()) {
                if (searchInput_->getText().empty()) {
                    searchInput_->setFocused(false);
                } else {
                    searchInput_->setText(""); // Will trigger onTextChange -> applyFilter
                }
                return true;
            }
        }
    }

    // Handle navigation/activation on KeyDown only. KeyUp previously re-triggered actions ("moves twice").
    if (!event.pressed) {
        switch (event.keyCode) {
            case NUIKeyCode::Up:
            case NUIKeyCode::Down:
            case NUIKeyCode::Left:
            case NUIKeyCode::Right:
            case NUIKeyCode::Enter:
            case NUIKeyCode::Backspace:
                return true; // consume
            default:
                return false;
        }
    }
    
    const auto& view = getActiveView();

    switch (event.keyCode) {
        case NUIKeyCode::Up:
            if (selectedIndex_ > 0 && !view.empty()) {
                selectedIndex_--;
                selectedFile_ = view[selectedIndex_];
                selectedIndices_.clear();
                selectedIndices_.push_back(selectedIndex_);
                updateScrollPosition();
                if (onFileSelected_) {
                    onFileSelected_(*selectedFile_);
                }
                // Trigger sound preview for audio files
                if (onSoundPreview_ && selectedFile_ && !selectedFile_->isDirectory) {
                    FileType type = selectedFile_->type;
                    if (type == FileType::AudioFile || type == FileType::MusicFile ||
                        type == FileType::WavFile || type == FileType::Mp3File ||
                        type == FileType::FlacFile) {
                        onSoundPreview_(*selectedFile_);
                    }
                }
                setDirty(true);
                return true;
            }
            break;
            
        case NUIKeyCode::Down:
            if (!view.empty() && selectedIndex_ < static_cast<int>(view.size()) - 1) {
                selectedIndex_++;
                selectedFile_ = view[selectedIndex_];
                selectedIndices_.clear();
                selectedIndices_.push_back(selectedIndex_);
                updateScrollPosition();
                if (onFileSelected_) {
                    onFileSelected_(*selectedFile_);
                }
                // Trigger sound preview for audio files
                if (onSoundPreview_ && selectedFile_ && !selectedFile_->isDirectory) {
                    FileType type = selectedFile_->type;
                    if (type == FileType::AudioFile || type == FileType::MusicFile ||
                        type == FileType::WavFile || type == FileType::Mp3File ||
                        type == FileType::FlacFile) {
                        onSoundPreview_(*selectedFile_);
                    }
                }
                setDirty(true);
                return true;
            }
            break;
            
        case NUIKeyCode::Right:
            if (selectedFile_ && selectedFile_->isDirectory) {
                if (!selectedFile_->isExpanded) {
                    toggleFolder(const_cast<FileItem*>(selectedFile_));
                }
                return true;
            }
            break;

        case NUIKeyCode::Left:
            if (selectedFile_ && selectedFile_->isDirectory) {
                if (selectedFile_->isExpanded) {
                    toggleFolder(const_cast<FileItem*>(selectedFile_));
                }
                return true;
            }
            break;
            
	        case NUIKeyCode::Enter:
	            if (selectedFile_) {
	                if (selectedFile_->isPlaceholder) {
	                    return true;
	                }
	                if (selectedFile_->isDirectory) {
	                    toggleFolder(const_cast<FileItem*>(selectedFile_));
	                } else {
	                    if (onFileOpened_) {
	                        onFileOpened_(*selectedFile_);
	                    }
	                }
	                return true;
	            }
            break;
            
        case NUIKeyCode::Backspace:
            navigateUp();
            return true;
    }
    
    return false;
}

void FileBrowser::setCurrentPath(const std::string& path) {
    std::string targetPath = path;
    if (!rootPath_.empty()) {
        const std::filesystem::path root(rootPath_);
        const std::filesystem::path candidate(path);
        if (!isPathUnderRoot(candidate, root)) {
            targetPath = rootPath_;
        }
    }

    if (currentPath_ == targetPath) {
        return;
    }

    currentPath_ = targetPath;
    viewDirty_ = true;
    
    if (!isNavigatingHistory_) {
        pushToHistory(currentPath_);
    }

    loadDirectoryContents();
    updateBreadcrumbs();
    
    // Reset scroll on folder change
    targetScrollOffset_ = 0.0f;
    scrollOffset_ = 0.0f;
    scrollVelocity_ = 0.0f;

    if (onPathChanged_) {
        onPathChanged_(currentPath_);
    }
    setDirty(true);
}

void FileBrowser::pushToHistory(const std::string& path) {
    // Trim forward history if we branched
    if (navHistoryIndex_ >= 0 && navHistoryIndex_ < static_cast<int>(navHistory_.size()) - 1) {
        navHistory_.erase(navHistory_.begin() + navHistoryIndex_ + 1, navHistory_.end());
    }

    navHistory_.push_back(path);
    navHistoryIndex_ = static_cast<int>(navHistory_.size()) - 1;
}

void FileBrowser::navigateBack() {
    if (navHistoryIndex_ > 0) {
        isNavigatingHistory_ = true;
        navHistoryIndex_--;
        setCurrentPath(navHistory_[navHistoryIndex_]);
        isNavigatingHistory_ = false;
    }
}

void FileBrowser::navigateForward() {
    if (navHistoryIndex_ >= 0 && navHistoryIndex_ < static_cast<int>(navHistory_.size()) - 1) {
        isNavigatingHistory_ = true;
        navHistoryIndex_++;
        setCurrentPath(navHistory_[navHistoryIndex_]);
        isNavigatingHistory_ = false;
    }
}

void FileBrowser::refresh() {
    loadDirectoryContents();
    setDirty(true);
}

void FileBrowser::navigateUp() {
    std::filesystem::path current(currentPath_);
    std::filesystem::path parent = current.parent_path();
    if (parent.empty() || parent == current) return;

    if (!rootPath_.empty()) {
        const std::filesystem::path root(rootPath_);
        if (!isPathUnderRoot(parent, root)) {
            setCurrentPath(root.string());
            return;
        }
    }

    setCurrentPath(parent.string());
}

void FileBrowser::navigateTo(const std::string& path) {
    if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path)) return;
    if (!rootPath_.empty()) {
        const std::filesystem::path root(rootPath_);
        const std::filesystem::path candidate(path);
        if (!isPathUnderRoot(candidate, root)) return;
    }
    setCurrentPath(path);
}

void FileBrowser::selectFile(const std::string& path) {
    const auto& view = getActiveView();
    for (int i = 0; i < static_cast<int>(view.size()); ++i) {
        if (view[i] && view[i]->path == path) {
            selectedIndex_ = i;
            selectedIndices_.clear();
            selectedIndices_.push_back(i);
            lastShiftSelectIndex_ = i;
            selectedFile_ = view[i];
            updateScrollPosition();
            
            if (selectedFile_ && !selectedFile_->isDirectory) {
                // Waveform generation moved to FilePreviewPanel
            }
            
            if (onFileSelected_) {
                onFileSelected_(*selectedFile_);
            }
            setDirty(true);
            return;
        }
    }

    // Not in active view (e.g. tag filter active). Still update selectedFile_ if we can find it.
    for (const auto* item : displayItems_) {
        if (item && item->path == path) {
            selectedFile_ = item;
            selectedIndex_ = -1;
            selectedIndices_.clear();
            lastShiftSelectIndex_ = -1;
            if (onFileSelected_) {
                onFileSelected_(*selectedFile_);
            }
            setDirty(true);
            return;
        }
    }
}

void FileBrowser::openFile(const std::string& path) {
    try {
        const std::filesystem::path p(path);
        const std::filesystem::path parent = p.parent_path();
        if (!parent.empty() && parent.string() != currentPath_) {
            setCurrentPath(parent.string());
        }
    } catch (...) {
    }

    selectFile(path);
    if (selectedFile_ && onFileOpened_) {
        onFileOpened_(*selectedFile_);
    }
}

void FileBrowser::openFolder(const std::string& path) {
    navigateTo(path);
}

void FileBrowser::addToFavorites(const std::string& path) {
    const std::string key = mapKeyForPath(path);
    if (key.empty()) return;
    if (std::find(favoritesPaths_.begin(), favoritesPaths_.end(), key) != favoritesPaths_.end()) return;
    favoritesPaths_.push_back(key);
}

void FileBrowser::removeFromFavorites(const std::string& path) {
    const std::string key = mapKeyForPath(path);
    auto it = std::remove(favoritesPaths_.begin(), favoritesPaths_.end(), key);
    favoritesPaths_.erase(it, favoritesPaths_.end());
}

bool FileBrowser::isFavorite(const std::string& path) const {
    const std::string key = mapKeyForPath(path);
    return !key.empty() && (std::find(favoritesPaths_.begin(), favoritesPaths_.end(), key) != favoritesPaths_.end());
}

void FileBrowser::toggleFavorite(const std::string& path) {
    if (isFavorite(path)) {
        removeFromFavorites(path);
    } else {
        addToFavorites(path);
    }
    setDirty(true);
}

void FileBrowser::setSortMode(SortMode mode) {
    if (sortMode_ == mode) return;

    std::vector<std::string> selectedPaths;
    {
        const auto& view = getActiveView();
        for (int idx : selectedIndices_) {
            if (idx >= 0 && idx < static_cast<int>(view.size()) && view[idx]) {
                selectedPaths.push_back(view[idx]->path);
            }
        }
        if (selectedPaths.empty() && selectedFile_) {
            selectedPaths.push_back(selectedFile_->path);
        }
    }

    sortMode_ = mode;
    sortFiles();
    updateDisplayList();
    viewDirty_ = true;

    if (isFilterActive()) {
        applyFilter(); // rebuilds filtered pointers (also clears selection)
    }

    if (!selectedPaths.empty()) {
        const auto& view = getActiveView();
        selectedIndices_.clear();
        for (int i = 0; i < static_cast<int>(view.size()); ++i) {
            const FileItem* item = view[i];
            if (!item) continue;
            if (std::find(selectedPaths.begin(), selectedPaths.end(), item->path) != selectedPaths.end()) {
                selectedIndices_.push_back(i);
            }
        }

        if (!selectedIndices_.empty()) {
            selectedIndex_ = selectedIndices_.back();
            selectedFile_ = view[selectedIndex_];
            updateScrollPosition();
            if (onFileSelected_ && selectedFile_) {
                onFileSelected_(*selectedFile_);
            }
        } else {
            clearSelection();
        }
    }

    setDirty(true);
}

void FileBrowser::setSortAscending(bool ascending) {
    if (sortAscending_ == ascending) return;

    std::vector<std::string> selectedPaths;
    {
        const auto& view = getActiveView();
        for (int idx : selectedIndices_) {
            if (idx >= 0 && idx < static_cast<int>(view.size()) && view[idx]) {
                selectedPaths.push_back(view[idx]->path);
            }
        }
        if (selectedPaths.empty() && selectedFile_) {
            selectedPaths.push_back(selectedFile_->path);
        }
    }

    sortAscending_ = ascending;
    sortFiles();
    updateDisplayList();
    viewDirty_ = true;

    if (isFilterActive()) {
        applyFilter(); // rebuilds filtered pointers (also clears selection)
    }

    if (!selectedPaths.empty()) {
        const auto& view = getActiveView();
        selectedIndices_.clear();
        for (int i = 0; i < static_cast<int>(view.size()); ++i) {
            const FileItem* item = view[i];
            if (!item) continue;
            if (std::find(selectedPaths.begin(), selectedPaths.end(), item->path) != selectedPaths.end()) {
                selectedIndices_.push_back(i);
            }
        }

        if (!selectedIndices_.empty()) {
            selectedIndex_ = selectedIndices_.back();
            selectedFile_ = view[selectedIndex_];
            updateScrollPosition();
            if (onFileSelected_ && selectedFile_) {
                onFileSelected_(*selectedFile_);
            }
        } else {
            clearSelection();
        }
    }

    setDirty(true);
}

void FileBrowser::loadDirectoryContents() {
    rootItems_.clear();
    displayItems_.clear();
    cachedView_.clear(); // Prevent dangling pointers
    filteredFiles_.clear();
    selectedFile_ = nullptr;
    selectedIndex_ = -1;
    selectedIndices_.clear();
    lastShiftSelectIndex_ = -1;
    hoveredIndex_ = -1;
    dragPotential_ = false;
    dragSourceIndex_ = -1;

    // Bump generation to invalidate any in-flight scans for the previous directory.
    scanGeneration_.fetch_add(1, std::memory_order_acq_rel);
    {
        std::lock_guard<std::mutex> lock(scanMutex_);
        scanTasks_.clear();
        scanResults_.clear();
    }

    scanningRoot_ = true;
    enqueueScan(ScanKind::Root, currentPath_, 0);
    updateScrollbarVisibility();
    viewDirty_ = true;
    setDirty(true);
}

void FileBrowser::loadFolderContents(FileItem* item) {
    if (!item || !item->isDirectory) return;
    if (item->hasLoadedChildren || item->isLoadingChildren) return;

    item->isLoadingChildren = true;
    item->children.clear();

    // Minimal placeholder so expanded folders don't appear empty while scanning.
    FileItem placeholder("Loading...", "", FileType::Unknown, false, 0, "");
    placeholder.depth = item->depth + 1;
    placeholder.isPlaceholder = true;
    item->children.push_back(std::move(placeholder));

    enqueueScan(ScanKind::Folder, item->path, item->depth + 1);
    setDirty(true);
}

void FileBrowser::updateDisplayList() {
    displayItems_.clear();
    for (auto& item : rootItems_) {
        displayItems_.push_back(&item);
        if (item.isExpanded) {
            updateDisplayListRecursive(item, displayItems_);
        }
    }
    viewDirty_ = true;
}

void FileBrowser::updateDisplayListRecursive(FileItem& item, std::vector<const FileItem*>& list) {
    for (auto& child : item.children) {
        list.push_back(&child);
        if (child.isExpanded) {
            updateDisplayListRecursive(child, list);
        }
    }
}

void FileBrowser::toggleFolder(const FileItem* item) {
    if (!item->isDirectory) return;
    
    // We need to modify the item, so cast away const (safe in this context)
    FileItem* nonConstItem = const_cast<FileItem*>(item);
    
    if (nonConstItem->isExpanded) {
        nonConstItem->isExpanded = false;
    } else {
        if (!nonConstItem->hasLoadedChildren) {
            loadFolderContents(nonConstItem);
        }
        nonConstItem->isExpanded = true;
    }
    updateDisplayList();
	    setDirty(true);
	}

	bool FileBrowser::compareFileItems(const FileItem& a, const FileItem& b) const {
    // Priority: Search Score (descending) -> Folders First -> Name/etc (stable tie-break)
    
    if (searchInput_ && !searchInput_->getText().empty()) {
        if (a.searchScore != b.searchScore) {
            return a.searchScore > b.searchScore; // Higher score first
        }
        // If scores equal, fall through to standard sort for stability
    }

    if (a.isDirectory != b.isDirectory) {
        return a.isDirectory > b.isDirectory; // folders first
    }

	    const auto tieBreak = [&]() {
	        if (a.name != b.name) {
	            return sortAscending_ ? (a.name < b.name) : (a.name > b.name);
	        }
	        return a.path < b.path;
	    };

	    switch (sortMode_) {
	        case SortMode::Name:
	            return tieBreak();
	        case SortMode::Type:
	            if (a.type != b.type) return sortAscending_ ? (a.type < b.type) : (a.type > b.type);
	            return tieBreak();
	        case SortMode::Size:
	            if (a.size != b.size) return sortAscending_ ? (a.size < b.size) : (a.size > b.size);
	            return tieBreak();
	        case SortMode::Modified:
	            if (a.lastModified != b.lastModified) {
	                return sortAscending_ ? (a.lastModified < b.lastModified) : (a.lastModified > b.lastModified);
	            }
	            return tieBreak();
	    }
	    return tieBreak();
	}

	void FileBrowser::sortFiles() {
    bool hasSearch = searchInput_ && !searchInput_->getText().empty();

    std::function<void(std::vector<FileItem>&)> sortRecursive = [&](std::vector<FileItem>& items) {
        // Use stable_sort to keep the list from "jiggling" during fuzzy search updates
        std::stable_sort(items.begin(), items.end(),
                  [this](const FileItem& a, const FileItem& b) { return compareFileItems(a, b); });
        
        for (auto& item : items) {
            if (item.isDirectory && item.hasLoadedChildren && !item.children.empty()) {
                sortRecursive(item.children); // Recurse
            }
        }
    };

    sortRecursive(rootItems_);
    
    // Also stable_sort the filtered View if it's active
    if (!filteredFiles_.empty()) {
        std::stable_sort(filteredFiles_.begin(), filteredFiles_.end(),
             [this](const FileItem* a, const FileItem* b) { return compareFileItems(*a, *b); });
    }
}

FileType FileBrowser::getFileTypeFromExtension(const std::string& extension) const {
    if (extension == ".wav") return FileType::WavFile;
    if (extension == ".mp3") return FileType::Mp3File;
    if (extension == ".flac") return FileType::FlacFile;
    if (extension == ".aiff" || extension == ".aif") return FileType::AudioFile;
    if (extension == ".nomad" || extension == ".nmd") return FileType::ProjectFile;
    if (extension == ".mid" || extension == ".midi") return FileType::MusicFile;
    
    return FileType::Unknown;
}

std::shared_ptr<NUIIcon> FileBrowser::getIconForFileType(FileType type) {
    switch (type) {
        case FileType::Folder:
            return folderIcon_;
        case FileType::AudioFile:
            return audioFileIcon_;
        case FileType::MusicFile:
            return musicFileIcon_;
        case FileType::ProjectFile:
            return projectFileIcon_;
        case FileType::WavFile:
            return wavFileIcon_;
        case FileType::Mp3File:
            return mp3FileIcon_;
        case FileType::FlacFile:
            return flacFileIcon_;
        default:
            return unknownFileIcon_;
    }
}

void FileBrowser::renderFileList(NUIRenderer& renderer) {
    // Get component dimensions from theme ONCE outside the loop
    auto& themeManager = NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();

    NUIRect bounds = getBounds();
    // Use effective width to account for preview panel
    // Use effective width to account for preview panel
    float effectiveW = effectiveWidth_ > 0 ? effectiveWidth_ : bounds.width;
    
    // NEW STACK LAYOUT LOGIC (Shared with onResize and renderToolbar)
    const float buttonsRowHeight = 40.0f;
    const float breadcrumbRowHeight = 32.0f;
    const float searchRowHeight = 36.0f; 
    const float rowSpacing = 8.0f;
    
    // Restore itemHeight which acts as the row height for file list items
    float itemHeight = themeManager.getComponentDimension("fileBrowser", "itemHeight");
    
    // Calculate header total height
    float totalHeaderH = buttonsRowHeight + breadcrumbRowHeight + rowSpacing + searchRowHeight + rowSpacing;
    
    // FIX: Subtract preview panel height from list height for correct clipping
    float availableHeight = bounds.height;
    // Preview panel moved to FilePreviewPanel
    // if (previewPanelVisible_ && selectedFile_ && !selectedFile_->isDirectory) {
    //     availableHeight -= kPreviewPanelHeight + 4;
    // }
    
    float listY = bounds.y + totalHeaderH; 
    float listHeight = availableHeight - totalHeaderH;

    const auto& view = getActiveView();
    const float contentHeight = static_cast<float>(view.size()) * itemHeight;
    const bool needsScrollbar = contentHeight > listHeight;

    // Reserve a left gutter when scrollbar is needed (scrollbar is drawn on the left)
    const float scrollbarGutter = needsScrollbar ? (scrollbarWidth_ + themeManager.getSpacing("xs")) : 0.0f;
    const float listX = bounds.x + layout.panelMargin + scrollbarGutter;
    const float listW = effectiveW - 2 * layout.panelMargin - scrollbarGutter;
    NUIRect listClip(listX, listY, listW, listHeight);

    // CRITICAL: Cache invalidation moved to onResize to prevent frame spikes.
    // if (std::abs(lastCachedWidth_ - listW) > 1.0f) ...

    if (scanningRoot_ && view.empty()) {
        renderer.setClipRect(listClip);
        renderer.drawTextCentered("Loading...", listClip, 14.0f, textColor_.withAlpha(0.6f));
        renderer.clearClipRect();
        return;
    }

    // OPTIMIZATION: Only render visible items (virtualization)
    int firstVisibleIndex = std::max(0, static_cast<int>(scrollOffset_ / itemHeight));
    int lastVisibleIndex = std::min(static_cast<int>(view.size()), 
                                     static_cast<int>((scrollOffset_ + listHeight) / itemHeight) + 1);

    // Lambda for selection check - Optimize by checking index directly if possible
    auto isSelected = [this](int idx) {
        if (idx == selectedIndex_) return true;
        // Only scan vector if we have multi-selection
        if (selectedIndices_.size() <= 1) return false; 
        return std::find(selectedIndices_.begin(), selectedIndices_.end(), idx) != selectedIndices_.end();
    };

    // Clip file items to the list area to prevent bleed
    renderer.setClipRect(listClip);

    const float labelFont = 14.0f;   // +3px for better legibility
    const float metaFont = 12.0f;    // +2px for metadata
    const float rowIndentStep = themeManager.getComponentDimension("fileBrowser", "indentSize");
    const float maxIndent = std::min(72.0f, listW * 0.35f);
    const int maxGuideDepth = static_cast<int>(std::floor(maxIndent / rowIndentStep));

    // Hoist loop invariants
    const float iconSize = themeManager.getComponentDimension("fileBrowser", "iconSize");
    // Pre-calculate text vertical offsets to avoid repeated renderer calls/math
    // Assuming simple centering: y + (height - lineHeight) / 2 is common, or renderer specific logic.
    // Since we can't easily replicate renderer.calculateTextY exactly without its code, 
    // we will calculate it ONCE for a dummy rect at (0,0) and apply the delta.
    NUIRect dummyRect(0, 0, listW, itemHeight);
    float labelYOffset = std::round(renderer.calculateTextY(dummyRect, labelFont));
    float metaYOffset = std::round(renderer.calculateTextY(dummyRect, metaFont));

    // Render file items (ONLY VISIBLE ONES!) - NO CLIPPING, manual bounds check instead
    for (int i = firstVisibleIndex; i < lastVisibleIndex; ++i) {
        // Calculate item position: each item is positioned at listY + (i * itemHeight) - scrollOffset
        float itemY = listY + (i * itemHeight) - scrollOffset_;

        // CRITICAL: Skip rendering if item would bleed above the list area (into path bar)
        if (itemY < listY) {
            continue;
        }

        // Create item rect with proper dimensions (reserve left gutter for scrollbar)
        NUIRect itemRect(listX, itemY, listW, itemHeight);
        bool selected = isSelected(i);
        bool hovered = (i == hoveredIndex_);
        
        const FileItem* item = view[i];

        // Background styling
        if (selected) {
            // Selected state: Richer background + Left accent bar
            renderer.fillRoundedRect(itemRect, 4, selectedColor_.withAlpha(0.2f));
            // Left accent bar - slightly thicker and more rounded
            NUIRect accentRect(itemRect.x, itemRect.y + 3, 4.0f, itemRect.height - 6);
            renderer.fillRoundedRect(accentRect, 2.0f, selectedColor_);
        } else if (hovered) {
            // Hover state: Glass effect (Light overlay + subtle border from theme)
            renderer.fillRoundedRect(itemRect, 4, themeManager.getColor("glassHover")); 
            renderer.strokeRoundedRect(itemRect, 4, 1.0f, themeManager.getColor("glassBorder"));
        } else if (i % 2 == 1) {
            // Alternating rows: Extremely subtle
            renderer.fillRect(itemRect, NUIColor(1.0f, 1.0f, 1.0f, 0.02f));
        }
        
        // Indentation & Tree Lines (clamped so deep trees don't destroy name readability)
        const int depth = item->depth;
        const float indent = std::min(static_cast<float>(depth) * rowIndentStep, maxIndent);
        float contentX = itemRect.x + layout.panelMargin + indent;
        const int guideDepth = std::min(depth, maxGuideDepth);
        
	        // Draw vertical guide lines for tree structure
	        if (guideDepth > 0) {
	            float lineX = std::round(itemRect.x + layout.panelMargin + rowIndentStep * 0.5f) + 0.5f;
	            const NUIColor guideColor = borderColor_.withAlpha(0.18f);
	            const float yPad = 1.0f;
	            for (int d = 0; d < guideDepth; ++d) {
	                // Draw vertical line segment
	                renderer.drawLine(NUIPoint(lineX, itemRect.y + yPad),
	                                  NUIPoint(lineX, itemRect.y + itemRect.height - yPad),
	                                  1.0f, guideColor);
	                lineX += rowIndentStep;
	            }
	        }

        // Expander arrow
        if (item->isDirectory) {
            float arrowSize = 12.0f;
            NUIRect arrowRect(contentX - 6.0f, itemY + (itemHeight - arrowSize) * 0.5f, arrowSize, arrowSize);
            
            auto& icon = item->isExpanded ? chevronDownIcon_ : chevronIcon_;
            if (icon) {
                icon->setBounds(arrowRect);
                // Make arrows slightly more visible
                icon->setColor(selected ? selectedColor_ : textColor_.withAlpha(0.6f));
                icon->onRender(renderer);
            }
        }
        
        contentX += 16.0f; // Space for arrow

        // Render icon
        auto icon = getIconForFileType(item->type);
        if (icon) {
            NUIRect iconRect(contentX, itemY + (itemHeight - iconSize) * 0.5f, iconSize, iconSize);
            icon->setBounds(iconRect);
            // Tint folder icons if selected
            if (item->isDirectory && selected) {
                // Keep original color or tint? Let's keep original for now but maybe brighten
            }
            icon->onRender(renderer);
            contentX += iconSize + 8.0f;
        } else {
            contentX += iconSize + 8.0f;
        }

        // Render file name with proper vertical alignment and bounds checking to prevent bleeding
        float textX = contentX;
        float nameTextY = itemY + labelYOffset; // Use hoisted offset!
        
        // OPTIMIZATION: Cache file size string and display name
        if (!item->cacheValid) {
            // Calculate file size string (cache it)
            item->cachedSizeStr.clear();
            bool hasSize = !item->isDirectory && item->size > 0;
            
            if (hasSize) {
                if (item->size < 1024) {
                    item->cachedSizeStr = std::to_string(item->size) + " B";
                } else if (item->size < 1024 * 1024) {
                    item->cachedSizeStr = std::to_string(item->size / 1024) + " KB";
                } else {
                    item->cachedSizeStr = std::to_string(item->size / (1024 * 1024)) + " MB";
                }
            }
            
            // Calculate display name with truncation (cache it)
            float minGap = 20.0f;
            float rightMargin = 12.0f;
            float actualSizeWidth = hasSize ? renderer.measureText(item->cachedSizeStr, metaFont).width : 0.0f;
            float reservedForSize = hasSize ? (actualSizeWidth + minGap + rightMargin) : rightMargin;
            float maxTextWidth = itemRect.width - (textX - itemRect.x) - reservedForSize;

            // If the list is narrow, drop the size column to preserve name readability.
            if (hasSize && maxTextWidth < 90.0f) {
                item->cachedSizeStr.clear();
                hasSize = false;
                reservedForSize = rightMargin;
                maxTextWidth = itemRect.width - (textX - itemRect.x) - reservedForSize;
            }
            
            item->cachedDisplayName = item->name;
            auto nameTextSize = renderer.measureText(item->cachedDisplayName, labelFont);
            
            if (nameTextSize.width > maxTextWidth) {
                // Truncate with middle ellipsis (keeps extensions / suffixes readable)
                item->cachedDisplayName = ellipsizeMiddle(renderer, item->name, labelFont, maxTextWidth);
            }
            
            item->cacheValid = true;
        }
        
        // Add hover effect
        NUIColor nameColor = textColor_;
        if (selected) {
            nameColor = NUIColor::white(); // Bright white for selected
        } else if (hovered) {
            nameColor = textColor_.lightened(0.2f);
        }
        
        // USE CACHED display name
        renderer.drawText(item->cachedDisplayName, NUIPoint(textX, nameTextY), labelFont, nameColor);
        
        // Render file size (only if cached size exists)
        if (!item->cachedSizeStr.empty()) {
            auto sizeText = renderer.measureText(item->cachedSizeStr, metaFont);
            float sizeTextY = itemY + metaYOffset; // Use hoisted offset
            
            // Calculate position from the right
            float rightMargin = 12.0f;
            float sizeX = itemRect.x + itemRect.width - sizeText.width - rightMargin;
            
            // Render size
            renderer.drawText(item->cachedSizeStr, NUIPoint(sizeX, sizeTextY), metaFont, themeManager.getColor("textSecondary"));
        }
    }

    renderer.clearClipRect();
}

void FileBrowser::renderToolbar(NUIRenderer& renderer) {
    // Get layout dimensions from theme
    auto& themeManager = NUIThemeManager::getInstance();
    // Use fixed heights matching onResize logic
    const float buttonsRowHeight = 40.0f;
    const float breadcrumbRowHeight = 32.0f;
    float totalHeaderHeight = buttonsRowHeight + breadcrumbRowHeight;
    const float innerPad = 8.0f;

    NUIRect bounds = getBounds();
    // Use effective width to account for preview panel
    float effectiveW = effectiveWidth_ > 0 ? effectiveWidth_ : bounds.width;

    NUIRect toolbarRect(bounds.x, bounds.y, effectiveW, totalHeaderHeight);
    
    // Background
    // Use component background color to ensure it matches the dark theme and isn't accent-colored
    // Background
    // Use component background color for the header area
    renderer.fillRoundedRect(toolbarRect, 0.0f, backgroundColor_); 
    
    // Draw separator
    float sepY = toolbarRect.bottom(); 
    renderer.drawLine(NUIPoint(bounds.x, sepY), NUIPoint(bounds.x + effectiveW, sepY), 1.0f, borderColor_.withAlpha(0.5f));

    // Common sizes
    // Common sizes
    const float toolbarFont = themeManager.getFontSize("s");
    const float buttonRadius = 14.0f; // Pill shape radius
    const float buttonPadX = 10.0f;    // Balanced padding
    const float buttonH = 28.0f; 
    
    // Center buttons in the TOP row (0 to buttonsRowHeight)
    const float buttonY = toolbarRect.y + (buttonsRowHeight - buttonH) / 2.0f;
    const float iconSize = 14.0f;     
    const float iconGap = 8.0f;       
    const float clusterGap = 12.0f;   // Standard gap (restored from 20)

    // === MEASURE layout from edges ===
    
    // 1. Right Layout (Sort <- Tags <- Favorites)
    float currentRightX = toolbarRect.right() - innerPad;

    // === PREPARE ===
    
    // Helper lambda for button drawing
    auto drawButton = [&](const NUIRect& rect, const std::string& text, bool hovered, bool active = false) {
        NUIColor bg = themeManager.getColor("surfaceRaised").withAlpha(hovered ? 0.32f : 0.20f);
        if (active) bg = themeManager.getColor("accentPrimary").withAlpha(0.2f);
        
        renderer.fillRoundedRect(rect, buttonRadius, bg);
        renderer.strokeRoundedRect(rect, buttonRadius, 1.0f, 
                                   active ? themeManager.getColor("accentPrimary").withAlpha(0.5f) : 
                                   borderColor_.withAlpha(hovered ? 0.45f : 0.25f));
        
        if (!text.empty()) {
            float tY = std::round(renderer.calculateTextY(rect, toolbarFont));
            renderer.drawText(text, NUIPoint(rect.x + buttonPadX, tY), 
                              toolbarFont, textColor_.withAlpha(hovered ? 0.95f : 0.85f));
        }
    };

    // Sort Button (Compact)
    std::string sortText = "Sort";
    
    auto sortTextSize = renderer.measureText(sortText, toolbarFont);
    float sortButtonW = sortTextSize.width + buttonPadX * 2.0f + iconGap + iconSize; 
    
    sortButtonBounds_ = NUIRect(currentRightX - sortButtonW, buttonY, sortButtonW, buttonH);
    if (sortButtonBounds_.x < toolbarRect.x) sortButtonBounds_.x = toolbarRect.x;
    currentRightX = sortButtonBounds_.x - clusterGap;
    
    // RENDER SORT
    drawButton(sortButtonBounds_, sortText, sortHovered_);
    if (chevronDownIcon_) {
        const NUIRect chevronRect(sortButtonBounds_.right() - buttonPadX - iconSize,
                                  sortButtonBounds_.y + (sortButtonBounds_.height - iconSize) * 0.5f,
                                  iconSize, iconSize);
        chevronDownIcon_->setBounds(chevronRect);
        chevronDownIcon_->setColor(themeManager.getColor("textSecondary").withAlpha(sortHovered_ ? 0.9f : 0.7f));
        chevronDownIcon_->onRender(renderer);
    }

    // Tags Button (Compact)
    std::string tagsText = "Tags";
    
    auto tagsTextSize = renderer.measureText(tagsText, toolbarFont);
    float tagsButtonW = tagsTextSize.width + buttonPadX * 2.0f + iconGap + iconSize;

    tagsButtonBounds_ = NUIRect(currentRightX - tagsButtonW, buttonY, tagsButtonW, buttonH);
    if (tagsButtonBounds_.x < toolbarRect.x) tagsButtonBounds_.x = toolbarRect.x;
    currentRightX = tagsButtonBounds_.x - clusterGap;

    // RENDER TAGS
    bool isActive = !activeTagFilter_.empty();
    drawButton(tagsButtonBounds_, tagsText, tagsHovered_, isActive);
    if (chevronDownIcon_) {
         const NUIRect chevronRect(tagsButtonBounds_.right() - buttonPadX - iconSize,
                                   tagsButtonBounds_.y + (tagsButtonBounds_.height - iconSize) * 0.5f,
                                   iconSize, iconSize);
         chevronDownIcon_->setBounds(chevronRect);
         chevronDownIcon_->setColor(themeManager.getColor("textSecondary").withAlpha(tagsHovered_ ? 0.9f : 0.7f));
         chevronDownIcon_->onRender(renderer);
    }

    // Favorites Button (Star only if no text needed, or text?)
    // Just icon for favorites is cleaner
    const float starSize = 14.0f;
    const float starButtonW = starSize + buttonPadX * 2.0f; // Square-ish or pill?
    
    favoritesButtonBounds_ = NUIRect(currentRightX - starButtonW, buttonY, starButtonW, buttonH);
    if (favoritesButtonBounds_.x < toolbarRect.x) favoritesButtonBounds_.x = toolbarRect.x;
    currentRightX = favoritesButtonBounds_.x - clusterGap;
    
    // Favorites RENDER
    // Check for overlap with Refresh button (Left Cluster)
    float leftLimit = 0; // Will be set after refresh is calcualted, but wait... circular dependency?
    // Refresh is left aligned, Favorites is right aligned.
    // We already moved Favorites X.
    
    // Refresh Button - compact (Icon only)
    float currentLeftX = toolbarRect.x + innerPad;
    float refreshButtonW = buttonH; // Square button
    
    refreshButtonBounds_ = NUIRect(currentLeftX, buttonY, refreshButtonW, buttonH);
    currentLeftX = refreshButtonBounds_.right() + clusterGap;
    
    // NOW render Favorites
    bool isFav = isFavorite(currentPath_);
    // Use standard drawButton for consistent styling (borders, hovers)
    drawButton(favoritesButtonBounds_, "", favoritesHovered_, false); 
        
    auto icon = isFav ? starFilledIcon_ : starIcon_;
    if (icon) {
        float iconX = favoritesButtonBounds_.x + (favoritesButtonBounds_.width - starSize) * 0.5f;
        float iconY = favoritesButtonBounds_.y + (favoritesButtonBounds_.height - starSize) * 0.5f;
            
        icon->setBounds(NUIRect(iconX, iconY, starSize, starSize));
        icon->setColor(isFav ? themeManager.getColor("accentPrimary") : textColor_.withAlpha(0.6f));
        icon->onRender(renderer);
    }


    // 3. Middle (Breadcrumbs)
    const float breadcrumbRowY = toolbarRect.y + 40.0f;
    const float breadcrumbRowH = 32.0f;
    
    float breadcrumbX = toolbarRect.x + innerPad;
    float breadcrumbW = toolbarRect.width - innerPad * 2.0f;
    
    if (breadcrumbW > 10.0f) {
        breadcrumbBounds_ = NUIRect(breadcrumbX, breadcrumbRowY, breadcrumbW, breadcrumbRowH);
        renderInteractiveBreadcrumbs(renderer);
    } else {
        breadcrumbBounds_ = NUIRect(0,0,0,0);
    }

    // Refresh RENDER
    drawButton(refreshButtonBounds_, "", refreshHovered_);
    if (refreshIcon_) {
         float iconX = refreshButtonBounds_.x + (refreshButtonBounds_.width - iconSize) * 0.5f;
         float iconY = refreshButtonBounds_.y + (refreshButtonBounds_.height - iconSize) * 0.5f;
         refreshIcon_->setBounds(NUIRect(iconX, iconY, iconSize, iconSize));
         refreshIcon_->setColor(themeManager.getColor("textSecondary").withAlpha(refreshHovered_ ? 1.0f : 0.7f));
         refreshIcon_->onRender(renderer);
    }
}

void FileBrowser::renderSearchBox(NUIRenderer& renderer) {
    (void)renderer;
    // DEPRECATED: Handled by searchInput_ child component
}

		void FileBrowser::hidePopupMenu() {
		    if (popupMenu_ && popupMenu_->isVisible()) {
		        popupMenu_->hide();
		        popupMenuTargetPath_.clear();
		        popupMenuTargetIsDirectory_ = false;
		        setDirty(true);
		    }
		}

		bool FileBrowser::hasTag(const std::string& path, const std::string& tag) const {
		    if (tag.empty()) return false;
		    const std::string key = mapKeyForPath(path);
		    if (key.empty()) return false;
		    auto it = tagsByPath_.find(key);
		    if (it == tagsByPath_.end()) return false;
		    const auto& tags = it->second;
		    return std::find(tags.begin(), tags.end(), tag) != tags.end();
		}

		void FileBrowser::toggleTag(const std::string& path, const std::string& tag) {
		    if (tag.empty()) return;
		    const std::string key = mapKeyForPath(path);
		    if (key.empty()) return;

		    auto& tags = tagsByPath_[key];
		    auto it = std::find(tags.begin(), tags.end(), tag);
		    if (it != tags.end()) {
		        tags.erase(it);
		    } else {
		        tags.push_back(tag);
		    }

		    if (tags.empty()) {
		        tagsByPath_.erase(key);
		    }

		    // Refresh filtered view if active.
		    if (isFilterActive()) {
		        applyFilter();
		    } else {
		        setDirty(true);
		    }
		}

		std::vector<std::string> FileBrowser::getAllTagsSorted() const {
		    std::vector<std::string> all;
		    for (const auto& [_, tags] : tagsByPath_) {
		        for (const auto& t : tags) {
		            if (t.empty()) continue;
		            if (std::find(all.begin(), all.end(), t) == all.end()) {
		                all.push_back(t);
		            }
		        }
		    }
		    std::sort(all.begin(), all.end());
		    return all;
		}

		void FileBrowser::showFavoritesMenu() {
		    if (!popupMenu_) return;

		    popupMenu_->clear();
		    popupMenuTargetPath_.clear();
		    popupMenuTargetIsDirectory_ = false;

		    const bool currentFav = isFavorite(currentPath_);
		    popupMenu_->addItem(currentFav ? "Unfavorite Current Folder" : "Favorite Current Folder",
		                        [this]() { toggleFavorite(currentPath_); });

		    popupMenu_->addSeparator();

		    if (favoritesPaths_.empty()) {
		        auto emptyItem = std::make_shared<NUIContextMenuItem>("No favorites");
		        emptyItem->setEnabled(false);
		        popupMenu_->addItem(emptyItem);
		    } else {
		        // Stable order (path string)
		        std::vector<std::string> favorites = favoritesPaths_;
		        std::sort(favorites.begin(), favorites.end());

		        for (const auto& favPath : favorites) {
		            std::string label = favPath;
		            try {
		                std::filesystem::path p(favPath);
		                const std::string name = p.filename().string();
		                if (!name.empty()) label = name;
		            } catch (...) {
		            }

		            bool isDir = false;
		            try {
		                isDir = std::filesystem::exists(favPath) && std::filesystem::is_directory(favPath);
		            } catch (...) {
		                isDir = false;
		            }

		            if (isDir) {
		                popupMenu_->addItem(label, [this, path = favPath]() { openFolder(path); });
		            } else {
		                popupMenu_->addItem(label, [this, path = favPath]() { openFile(path); });
		            }
		        }

		        popupMenu_->addSeparator();
		        popupMenu_->addItem("Clear Favorites", [this]() {
		            favoritesPaths_.clear();
		            setDirty(true);
		        });
		    }

		    const float menuX = favoritesButtonBounds_.x;
		    const float menuY = favoritesButtonBounds_.bottom() + 6.0f;
		    popupMenu_->showAt(static_cast<int>(menuX), static_cast<int>(menuY));
		    setDirty(true);
		}

		void FileBrowser::showSortMenu() {
		    if (!popupMenu_) return;

	    popupMenu_->clear();
	    popupMenuTargetPath_.clear();
	    popupMenuTargetIsDirectory_ = false;

	    popupMenu_->addRadioItem("Name", "sort_mode", sortMode_ == SortMode::Name, [this]() { setSortMode(SortMode::Name); });
	    popupMenu_->addRadioItem("Type", "sort_mode", sortMode_ == SortMode::Type, [this]() { setSortMode(SortMode::Type); });
	    popupMenu_->addRadioItem("Size", "sort_mode", sortMode_ == SortMode::Size, [this]() { setSortMode(SortMode::Size); });
	    popupMenu_->addRadioItem("Modified", "sort_mode", sortMode_ == SortMode::Modified, [this]() { setSortMode(SortMode::Modified); });
	    popupMenu_->addSeparator();
	    popupMenu_->addCheckbox("Ascending", sortAscending_, [this](bool checked) { setSortAscending(checked); });

	    const float menuX = sortButtonBounds_.x;
	    const float menuY = sortButtonBounds_.bottom() + 6.0f;
		    popupMenu_->showAt(static_cast<int>(menuX), static_cast<int>(menuY));
		    setDirty(true);
		}

		void FileBrowser::showTagFilterMenu() {
		    if (!popupMenu_) return;

		    popupMenu_->clear();
		    popupMenuTargetPath_.clear();
		    popupMenuTargetIsDirectory_ = false;

		    popupMenu_->addRadioItem("All", "tag_filter", activeTagFilter_.empty(), [this]() {
		        activeTagFilter_.clear();
		        applyFilter();
		    });

		    auto tags = getAllTagsSorted();
		    if (!tags.empty()) {
		        popupMenu_->addSeparator();
		        for (const auto& t : tags) {
		            popupMenu_->addRadioItem(t, "tag_filter", activeTagFilter_ == t, [this, tag = t]() {
		                activeTagFilter_ = tag;
		                applyFilter();
		            });
		        }
		    }

		    const float menuX = tagsButtonBounds_.isEmpty() ? (sortButtonBounds_.x - 150.0f) : tagsButtonBounds_.x;
		    const float menuY = (tagsButtonBounds_.isEmpty() ? sortButtonBounds_.bottom() : tagsButtonBounds_.bottom()) + 6.0f;
		    popupMenu_->showAt(static_cast<int>(menuX), static_cast<int>(menuY));
		    setDirty(true);
		}

		void FileBrowser::showItemContextMenu(const FileItem& item, const NUIPoint& position) {
		    if (!popupMenu_) return;

	    popupMenu_->clear();
	    popupMenuTargetPath_ = item.path;
	    popupMenuTargetIsDirectory_ = item.isDirectory;

	    const auto copyToClipboard = [](const std::string& text) {
	        if (auto* utils = Nomad::Platform::getUtils()) {
	            utils->setClipboardText(text);
	        }
	    };

		    if (item.isDirectory) {
		        popupMenu_->addItem("Open", [this, path = item.path]() { openFolder(path); });
		        popupMenu_->addItem("Set as Root", [this, path = item.path]() {
		            rootPath_ = canonicalOrNormalized(std::filesystem::path(path)).string();
		            setCurrentPath(rootPath_);
		        });
	        if (!rootPath_.empty()) {
	            popupMenu_->addItem("Clear Root", [this]() {
	                rootPath_.clear();
	                updateBreadcrumbs();
	                setDirty(true);
	            });
	        }
	        popupMenu_->addSeparator();

		        const bool fav = isFavorite(item.path);
		        popupMenu_->addItem(fav ? "Remove from Favorites" : "Add to Favorites",
		                            [this, path = item.path]() { toggleFavorite(path); setDirty(true); });
		        // Tags submenu
		        {
		            auto tagsMenu = std::make_shared<NUIContextMenu>();
		            static const std::vector<std::string> kPresetTags = {
		                "Drums", "Bass", "Vocal", "FX", "Loops", "One-shots", "Synth", "Pads", "Ambience"};
		            for (const auto& tag : kPresetTags) {
		                tagsMenu->addCheckbox(tag, hasTag(item.path, tag),
		                                      [this, path = item.path, tag](bool) { toggleTag(path, tag); });
		            }
		            popupMenu_->addSubmenu("Tags", tagsMenu);
		        }
		        popupMenu_->addSeparator();
		        popupMenu_->addItem("Copy Path", [path = item.path, copyToClipboard]() { copyToClipboard(path); });
		    } else {
	        // Navigate to containing folder
	        popupMenu_->addItem("Show in Browser", [this, path = item.path]() {
	            try {
	                std::filesystem::path p(path);
	                std::filesystem::path parent = p.parent_path();
	                if (!parent.empty()) {
	                    setCurrentPath(parent.string());
	                    selectFile(path);
	                }
	            } catch (...) {
	            }
	        });

	        const bool isAudio =
	            item.type == FileType::AudioFile || item.type == FileType::MusicFile ||
	            item.type == FileType::WavFile || item.type == FileType::Mp3File || item.type == FileType::FlacFile;

		        if (isAudio) {
		            popupMenu_->addSeparator();
		            popupMenu_->addItem("Preview", [this, path = item.path]() {
		                selectFile(path);
		                if (selectedFile_ && onSoundPreview_) {
		                    onSoundPreview_(*selectedFile_);
		                }
		            });
		            popupMenu_->addItem("Load", [this, path = item.path]() { openFile(path); });
		        }

		        // Tags submenu
		        {
		            auto tagsMenu = std::make_shared<NUIContextMenu>();
		            static const std::vector<std::string> kPresetTags = {
		                "Drums", "Bass", "Vocal", "FX", "Loops", "One-shots", "Synth", "Pads", "Ambience"};
		            for (const auto& tag : kPresetTags) {
		                tagsMenu->addCheckbox(tag, hasTag(item.path, tag),
		                                      [this, path = item.path, tag](bool) { toggleTag(path, tag); });
		            }
		            popupMenu_->addSubmenu("Tags", tagsMenu);
		        }

		        popupMenu_->addSeparator();
		        popupMenu_->addItem("Copy Path", [path = item.path, copyToClipboard]() { copyToClipboard(path); });
		    }

	    popupMenu_->showAt(position);
	    setDirty(true);
	}

void FileBrowser::updateScrollPosition() {
    if (selectedIndex_ < 0) return;

    // Get component dimensions from theme
    auto& themeManager = NUIThemeManager::getInstance();

    NUIRect bounds = getBounds();
    
    // USE UNIFIED STACK LAYOUT LOGIC
    const float buttonsRowHeight = 40.0f;
    const float breadcrumbRowHeight = 32.0f;
    const float searchRowHeight = 36.0f; 
    const float rowSpacing = 8.0f;
    float totalHeaderH = buttonsRowHeight + breadcrumbRowHeight + rowSpacing + searchRowHeight + rowSpacing;
    
    float availableHeight = bounds.height;
    // Preview panel moved to FilePreviewPanel
    // if (previewPanelVisible_ && selectedFile_ && !selectedFile_->isDirectory) {
    //     availableHeight -= kPreviewPanelHeight + 4;
    // }
    
    float listY = bounds.y + totalHeaderH;
    float listHeight = availableHeight - totalHeaderH;

    const auto& view = getActiveView();
    float itemY = listY + (selectedIndex_ * itemHeight_) - scrollOffset_;

    // Scroll up if item is above visible area
    if (itemY < listY) {
        scrollOffset_ = selectedIndex_ * itemHeight_;
    }
    // Scroll down if item is below visible area
    else if (itemY + itemHeight_ > listY + listHeight) {
        scrollOffset_ = (selectedIndex_ + 1) * itemHeight_ - listHeight;
    }

    // Clamp scroll offset
    float maxScroll = std::max(0.0f, (view.size() * itemHeight_) - listHeight);
    scrollOffset_ = std::max(0.0f, std::min(scrollOffset_, maxScroll));
    targetScrollOffset_ = scrollOffset_;
    scrollVelocity_ = 0.0f;

    // Update scrollbar visibility and position
    updateScrollbarVisibility();
}

void FileBrowser::renderScrollbar(NUIRenderer& renderer) {
    auto& themeManager = NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();

    const auto& view = getActiveView();
    // Use stored track height which is correctly calculated in onResize/onMouseEvent
    // If it's 0 (unlikely if sized), fall back to list height calc
    if (scrollbarTrackHeight_ <= 0.0f) {
        // Fallback or initialization
        scrollbarTrackHeight_ = getBounds().height; // Rough calc
    }

    float contentHeight = view.size() * itemHeight_;
    float maxScroll = std::max(0.0f, contentHeight - scrollbarTrackHeight_);
    bool needsScrollbar = maxScroll > 0.0f;

    if (!needsScrollbar || view.empty()) return;

    NUIRect bounds = getBounds();
    // Scrollbar is on the left, inside the panel margin.
    float scrollbarX = bounds.x + layout.panelMargin;
    
    // RE-CALCULATE List Y (Unified Stack) - Scrollbar starts at list top
    const float buttonsRowHeight = 40.0f;
    const float breadcrumbRowHeight = 32.0f;
    const float searchRowHeight = 36.0f; 
    const float rowSpacing = 8.0f;
    float totalHeaderH = buttonsRowHeight + breadcrumbRowHeight + rowSpacing + searchRowHeight + rowSpacing;
    
    float scrollbarY = bounds.y + totalHeaderH;
    float scrollbarHeight = scrollbarTrackHeight_;

    float opacity = std::clamp(scrollbarOpacity_, 0.0f, 1.0f);
    if (opacity <= 0.01f) return;

    const float radius = themeManager.getRadius("s");
    const bool hot = isDraggingScrollbar_ || scrollbarHovered_;
    const float hoverGrow = hot ? 2.0f : 0.0f;
    const float trackWidth = scrollbarWidth_ + hoverGrow;

    // Track
    const float trackAlpha = (hot ? 0.08f : 0.05f) * opacity;
    NUIColor trackColor = themeManager.getColor("border").withAlpha(trackAlpha);
    renderer.fillRoundedRect(NUIRect(scrollbarX, scrollbarY, trackWidth, scrollbarHeight),
                             radius, trackColor);

    // Thumb (quiet by default; brighter on hover/drag)
    const float thumbAlpha = (isDraggingScrollbar_ ? 0.55f : (scrollbarHovered_ ? 0.32f : 0.22f)) * opacity;
    NUIColor thumbColor = themeManager.getColor("textSecondary").withAlpha(thumbAlpha);
    float thumbY = scrollbarY + scrollbarThumbY_;
    NUIRect thumbRect(scrollbarX, thumbY, trackWidth, scrollbarThumbHeight_);
    renderer.fillRoundedRect(thumbRect, radius, thumbColor);
    renderer.strokeRoundedRect(thumbRect, radius, 1.0f, themeManager.getColor("border").withAlpha(0.12f * opacity));
}

bool FileBrowser::handleScrollbarMouseEvent(const NUIMouseEvent& event) {
    // Get component dimensions from theme
    auto& themeManager = NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();

    NUIRect bounds = getBounds();
    float headerHeight = themeManager.getComponentDimension("fileBrowser", "headerHeight");
    float scrollbarX = bounds.x + layout.panelMargin; // Left-side scrollbar
    const float pathBarHeight = 30.0f;
    float scrollbarY = bounds.y + headerHeight + 8 + pathBarHeight; // After path bar
    
    // Use the member variable scrollbarTrackHeight_ for consistency
    // It's set in onResize() and used for thumb calculation

    const auto& view = getActiveView();

    // If we're dragging, continue dragging regardless of mouse position
    if (isDraggingScrollbar_) {
        scrollbarFadeTimer_ = 0.0f;
        scrollbarOpacity_ = 1.0f;
        // Stop dragging on mouse release (anywhere, not just in scrollbar area)
        if (!event.pressed && event.button == NUIMouseButton::Left) {
            isDraggingScrollbar_ = false;
            return true;
        }

        // Continue dragging even if mouse is outside scrollbar area
        float deltaY = event.position.y - dragStartY_;
        float scrollRatio = deltaY / scrollbarTrackHeight_;
        float itemHeight = themeManager.getComponentDimension("fileBrowser", "itemHeight");
        float maxScroll = std::max(0.0f, (view.size() * itemHeight) - scrollbarTrackHeight_);

        // Direct scrolling for responsive dragging - set BOTH for instant response
        targetScrollOffset_ = dragStartScrollOffset_ + (scrollRatio * maxScroll);
        scrollOffset_ = targetScrollOffset_; // Instant during drag, no lerp

        // Clamp scroll offset
        scrollOffset_ = std::max(0.0f, std::min(scrollOffset_, maxScroll));
        targetScrollOffset_ = scrollOffset_;
        
        // Let onUpdate handle thumb position and dirty flag
        return true;
    }

    // Check if mouse is over scrollbar area (with padding for easier clicking)
    bool inScrollbarArea = (event.position.x >= scrollbarX - 10 && 
                             event.position.x <= scrollbarX + scrollbarWidth_ + 10 &&
                             event.position.y >= scrollbarY - 10 && 
                             event.position.y <= scrollbarY + scrollbarTrackHeight_ + 10);

    if (scrollbarHovered_ != inScrollbarArea) {
        scrollbarHovered_ = inScrollbarArea;
        setDirty(true);
    }

    // Hovering the scrollbar should reveal it (auto-hide UX)
    if (inScrollbarArea) {
        scrollbarFadeTimer_ = 0.0f;
        if (scrollbarOpacity_ < 1.0f) {
            scrollbarOpacity_ = 1.0f;
            setDirty(true);
        }
    }

    if (!inScrollbarArea) {
        return false;
    }


    if (event.pressed && event.button == NUIMouseButton::Left) {
        scrollbarFadeTimer_ = 0.0f;
        scrollbarOpacity_ = 1.0f;
        // Check if clicking on thumb or track (with padding)
        float thumbAbsoluteY = scrollbarY + scrollbarThumbY_;
        if (event.position.y >= thumbAbsoluteY - 10 &&
            event.position.y <= thumbAbsoluteY + scrollbarThumbHeight_ + 10) {
            // Start dragging thumb
            isDraggingScrollbar_ = true;
            dragStartY_ = event.position.y;
            dragStartScrollOffset_ = scrollOffset_;
        } else {
            // Click on track - jump to position
            float relativeY = event.position.y - scrollbarY;
            float scrollRatio = relativeY / scrollbarTrackHeight_;
            float itemHeight = themeManager.getComponentDimension("fileBrowser", "itemHeight");
            float maxScroll = std::max(0.0f, (view.size() * itemHeight) - scrollbarTrackHeight_);
            // Direct jump to clicked position - set both for instant response
            targetScrollOffset_ = scrollRatio * maxScroll;
            scrollOffset_ = targetScrollOffset_;

            // Clamp scroll offset
            scrollOffset_ = std::max(0.0f, std::min(scrollOffset_, maxScroll));
            targetScrollOffset_ = scrollOffset_;
            
            // Let onUpdate handle thumb position and dirty flag
        }
        return true;
    } else if (!event.pressed && event.button == NUIMouseButton::Left) {
        // Stop dragging
        isDraggingScrollbar_ = false;
        return true;
    }

    return false;
}

void FileBrowser::updateScrollbarVisibility() {
    // Get component dimensions from theme
    auto& themeManager = NUIThemeManager::getInstance();
    float itemHeight = themeManager.getComponentDimension("fileBrowser", "itemHeight");
    const auto& view = getActiveView();

    // Calculate if we need a scrollbar
    float contentHeight = view.size() * itemHeight;
    float maxScroll = std::max(0.0f, contentHeight - scrollbarTrackHeight_);
    bool needsScrollbar = maxScroll > 0.0f;

    if (needsScrollbar) {
        scrollbarVisible_ = true;
        scrollbarFadeTimer_ = 0.0f;
        scrollbarOpacity_ = 1.0f;

        // Calculate thumb height (proportional to visible area)
        float minThumbSize = themeManager.getComponentDimension("scrollbar", "minThumbSize");
        scrollbarThumbHeight_ = std::max(minThumbSize, (scrollbarTrackHeight_ / contentHeight) * scrollbarTrackHeight_);

        // Calculate thumb position based on scroll offset
        if (maxScroll > 0.0f) {
            scrollbarThumbY_ = (scrollOffset_ / maxScroll) * (scrollbarTrackHeight_ - scrollbarThumbHeight_);
        } else {
            scrollbarThumbY_ = 0.0f;
        }

    } else {
        scrollbarVisible_ = false;
        scrollbarOpacity_ = 0.0f;
        scrollbarFadeTimer_ = 0.0f;
        scrollbarHovered_ = false;
    }
}

// ========================================================================
// Selection / Filtering / Breadcrumb helpers
// ========================================================================

bool FileBrowser::isFilterActive() const {
    return (searchInput_ && !searchInput_->getText().empty()) || !activeTagFilter_.empty();
}

const std::vector<const FileItem*>& FileBrowser::getActiveView() const {
    if (viewDirty_) {
        cachedView_ = isFilterActive() ? filteredFiles_ : displayItems_;
        viewDirty_ = false;
    }
    return cachedView_;
}

void FileBrowser::toggleFileSelection(int index, bool ctrlPressed, bool shiftPressed) {
    const auto& view = getActiveView();
    if (index < 0 || index >= static_cast<int>(view.size())) {
        clearSelection();
        return;
    }

    // Shift-select range
    if (shiftPressed && lastShiftSelectIndex_ >= 0 && lastShiftSelectIndex_ < static_cast<int>(view.size())) {
        int start = std::min(lastShiftSelectIndex_, index);
        int end = std::max(lastShiftSelectIndex_, index);
        selectedIndices_.clear();
        for (int i = start; i <= end; ++i) selectedIndices_.push_back(i);
        selectedIndex_ = index;
    } else if (ctrlPressed) {
        // Toggle membership
        auto it = std::find(selectedIndices_.begin(), selectedIndices_.end(), index);
        if (it != selectedIndices_.end()) {
            selectedIndices_.erase(it);
            if (selectedIndices_.empty()) selectedIndex_ = -1;
        } else {
            selectedIndices_.push_back(index);
            selectedIndex_ = index;
            lastShiftSelectIndex_ = index;
        }
    } else {
        // Single select
        selectedIndices_.clear();
        selectedIndices_.push_back(index);
        selectedIndex_ = index;
        lastShiftSelectIndex_ = index;
    }
}

void FileBrowser::clearSelection() {
    selectedIndices_.clear();
    selectedIndex_ = -1;
    selectedFile_ = nullptr;
    lastShiftSelectIndex_ = -1;
}

void FileBrowser::setSearchQuery(const std::string& query) {
    if (searchInput_) {
        searchInput_->setText(query);
    }
}

void FileBrowser::applyFilter() {
    filteredFiles_.clear();
    std::string query = searchInput_ ? searchInput_->getText() : "";
    const bool hasNameFilter = !query.empty();
    const bool hasTagFilter = !activeTagFilter_.empty();
    
    if (!hasNameFilter && !hasTagFilter) {
        // No filter active, display all root items
        updateDisplayList();
        selectedFile_ = nullptr;
        selectedIndex_ = -1;
        selectedIndices_.clear();
        updateScrollbarVisibility();
        setDirty(true);
        return;
    }

    // Prepare search query
    std::string needle = query;
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c){ return std::tolower(c); });
    
    // Hybrid Search Rules
    bool isExtensionSearch = !needle.empty() && needle.front() == '.';
    bool isSubstringSearch = !isExtensionSearch && needle.find('.') != std::string::npos;
    bool isFuzzySearch = !isExtensionSearch && !isSubstringSearch;

    // Flatten all items (including children) for comprehensive search
    std::vector<const FileItem*> allItems;
    std::function<void(const std::vector<FileItem>&)> gatherItems = 
        [&](const std::vector<FileItem>& items) {
        for (const auto& item : items) {
            allItems.push_back(&item);
            if (item.isDirectory && item.hasLoadedChildren) {
                gatherItems(item.children); // Recurse
            }
        }
    };
    gatherItems(rootItems_);

    for (const auto* item : allItems) {
        bool matchesSearch = true;
        int score = 0;

        if (hasNameFilter) {
            std::string hay = item->name; // Search against name (basename)
            std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c){ return std::tolower(c); });

            if (isExtensionSearch) {
                // Rule 1: Extension Match (ends_with)
                if (hay.length() >= needle.length()) {
                    matchesSearch = (hay.compare(hay.length() - needle.length(), needle.length(), needle) == 0);
                    score = 1000; // High score for exact extension
                } else {
                    matchesSearch = false;
                }
            } 
            else if (isSubstringSearch) {
                // Rule 2: Substring-ish (all chars in order, contiguous ideally)
                // For "kick.wav" seeking "kick.wav" -> exact substring
                size_t foundPos = hay.find(needle);
                matchesSearch = (foundPos != std::string::npos);
                if (matchesSearch) {
                    score = 500 - static_cast<int>(foundPos); // Prefer earlier matches
                }
            } 
            else {
                // Rule 3: Fuzzy Subsequence with Scoring
                // -1 gap, +10 start, +5 start of word, +5 contiguous
                // Penalty: -len/10
                
                size_t nIdx = 0;
                size_t hIdx = 0;
                int gapPenalty = 0;
                int bonuses = 0;
                int contiguousRun = 0;
                bool firstCharMatched = false;
                
                // Track start of match for scoring
                int firstMatchIdx = -1;

                while (nIdx < needle.length() && hIdx < hay.length()) {
                    if (needle[nIdx] == hay[hIdx]) {
                        if (firstMatchIdx == -1) firstMatchIdx = static_cast<int>(hIdx);
                        
                        // Start of string bonus
                        if (hIdx == 0) bonuses += 10;
                        
                        // Start of word bonus (check prev char for separator)
                        if (hIdx > 0) {
                            char prev = hay[hIdx - 1];
                            if (prev == '_' || prev == '-' || prev == ' ' || prev == '.') {
                                bonuses += 5;
                            }
                        }
                        
                        // Contiguous bonus
                        if (nIdx > 0 && hIdx > 0 && needle[nIdx-1] == hay[hIdx-1]) { // Logic check: actually just checking if we matched prev loop
                            // This logic is slightly flawed for "contiguous in haystack", simplistic approach:
                            contiguousRun++; 
                            if (contiguousRun > 0) bonuses += 5;
                        } else {
                            contiguousRun = 0;
                        }

                        nIdx++;
                    } else {
                         // Gap
                         if (firstMatchIdx != -1) gapPenalty -= 1; // Only penalize gaps inside the match span? 
                         // Or simple: penalize every skipped char
                    }
                    hIdx++; // Always advance haystack
                }

                matchesSearch = (nIdx == needle.length()); // Found all chars
                
                if (matchesSearch) {
                    // Simple fuzzy score calculation re-pass or simplification
                    // Let's refine the score based on the successful match
                    // Since the above verification loop is greedy, it might not find optimal alignment.
                    // For UI responsiveness, greedy is usually fine.
                    
                    // Add penalty for total length to prefer shorter files
                    int lengthPenalty = static_cast<int>(hay.length()) / 10;
                    
                    score = bonuses + gapPenalty - lengthPenalty;
                }
            }
        }

        bool matchesTag = true;
        if (matchesTag && hasTagFilter) {
            matchesTag = hasTag(item->path, activeTagFilter_);
        }

        if (matchesSearch && matchesTag) {
            item->searchScore = score;
            filteredFiles_.push_back(item);
        }
    }

    sortFiles(); // Will use searchScore if query is active

    selectedFile_ = nullptr;
    selectedIndex_ = -1;
    selectedIndices_.clear();
    updateScrollbarVisibility();
    viewDirty_ = true;
    setDirty(true);
}

void FileBrowser::updateBreadcrumbs() {
    breadcrumbs_.clear();
    if (currentPath_.empty()) return;

    std::filesystem::path p(currentPath_);
    std::filesystem::path accum;
    float x = getBounds().x + 10.0f;
    float spacing = 6.0f;

    for (const auto& part : p) {
        accum /= part;
        std::string name = part.string();
        if (!name.empty() && name.back() == std::filesystem::path::preferred_separator) {
            name.pop_back();
        }
        float approxWidth = static_cast<float>(name.size()) * 7.0f; // refined during render
        breadcrumbs_.push_back({name, accum.string(), {}, x, approxWidth});
        x += approxWidth + spacing + 12.0f; // include chevron spacing
    }
}

void FileBrowser::navigateToBreadcrumb(int index) {
    if (index < 0 || index >= static_cast<int>(breadcrumbs_.size())) return;
    navigateTo(breadcrumbs_[index].path);
}

void FileBrowser::renderInteractiveBreadcrumbs(NUIRenderer& renderer) {
    if (breadcrumbs_.empty()) {
        updateBreadcrumbs();
    }

    if (breadcrumbBounds_.isEmpty() || currentPath_.empty()) {
        return;
    }

    auto& themeManager = NUIThemeManager::getInstance();
    const float fontSize = themeManager.getFontSize("s");
    const NUIRect breadcrumbRect = breadcrumbBounds_;
    const float chipInsetY = 2.0f;
    const float chipRowH = std::max(0.0f, breadcrumbRect.height - chipInsetY * 2.0f);
    const NUIRect chipRowRect(breadcrumbRect.x, breadcrumbRect.y + chipInsetY, breadcrumbRect.width, chipRowH);
    const float breadcrumbTextY = std::round(renderer.calculateTextY(chipRowRect, fontSize));

    std::filesystem::path p(currentPath_);
    std::vector<std::filesystem::path> parts;
    bool sandboxed = false;

    if (!rootPath_.empty() && isPathUnderRoot(p, rootPath_)) {
        sandboxed = true;
        // Sandbox mode: Root is the first breadcrumb
        std::filesystem::path root(rootPath_);
        if (root.has_filename()) {
             parts.push_back(root.filename());
        } else {
             parts.push_back(root.root_name()); // Handle "C:" case
             if (parts.back().empty()) parts.back() = "Root"; // Fallback
        }
        
        // Relative parts
        // Use lexical relative to avoid disk I/O or symlink confusion in rendering
        std::filesystem::path rel = std::filesystem::relative(p, root);
        if (rel != "." && !rel.empty()) {
            for (auto it = rel.begin(); it != rel.end(); ++it) {
                if (*it != ".") parts.push_back(*it);
            }
        }
    } else {
        // Standard mode: absolute parts
        for (auto it = p.begin(); it != p.end(); ++it) {
            parts.push_back(*it);
        }
    }

    if (parts.empty()) {
        return;
    }

    // Separators: use "/" (no chevrons/arrows)
    const char* separatorText = "/";
    const auto separatorSize = renderer.measureText(separatorText, fontSize);
    const float separatorPad = 8.0f;
    const float separatorW = separatorSize.width + separatorPad;

    // Chip sizing
    const float chipPadX = 10.0f;
    const float chipRadius = 6.0f;

    // Measure parts
    std::vector<float> partWidths;
    std::vector<std::string> partDisplayNames; // Store ellipsized names
    partWidths.reserve(parts.size());
    partDisplayNames.reserve(parts.size());
    
    float totalWidth = 0.0f;
    const float maxChipWidth = 120.0f; // Max width per breadcrumb chip

    for (size_t i = 0; i < parts.size(); ++i) {
        std::string partName = parts[i].string();
        if (!partName.empty() && partName.back() == std::filesystem::path::preferred_separator) {
            partName.pop_back();
        }
        
        // Ellipsize if too long
        std::string displayName = partName;
        float textW = renderer.measureText(partName, fontSize).width;
        
        if (textW > maxChipWidth) {
             displayName = ellipsizeMiddle(renderer, partName, fontSize, maxChipWidth);
             textW = renderer.measureText(displayName, fontSize).width;
        }

        partDisplayNames.push_back(displayName);
        partWidths.push_back(textW);
        totalWidth += textW + chipPadX * 2.0f;
        
        if (i < parts.size() - 1) {
            totalWidth += separatorW;
        }
    }

    // Layout Strategy:
    // 1. Always show Root (index 0).
    // 2. Always show Current (index size-1).
    // 3. Always show Parent (index size-2) if it exists and fits.
    // 4. Fill remaining space from the right (moving backwards from Parent-1).
    // 5. Gap between Root and first visible right item = "...".
    
    const float availableWidth = breadcrumbRect.width;
    const auto ellipsisSize = renderer.measureText("...", fontSize);
    const float ellipsisW = ellipsisSize.width + chipPadX * 2.0f;

    // Calculate fixed widths (Root + Current)
    float fixedWidth = (partWidths[0] + chipPadX * 2.0f);
    if (parts.size() > 1) {
        fixedWidth += (partWidths.back() + chipPadX * 2.0f) + separatorW;
        // Also account for separator after root if > 1 item
        fixedWidth += separatorW; 
    }
    
    // Check if we need ellipsis (if size > 2, we might have a gap)
    bool hasGap = false;
    if (parts.size() > 2) {
         // Assume we might need ellipsis
         fixedWidth += ellipsisW + separatorW;
         hasGap = true;
    }

    float availableForMiddle = availableWidth - fixedWidth;
    
    // We strictly want to show Parent (size-2) if we can.
    size_t rightStartIndex = parts.size() - 1; // Default starts at Current

    if (parts.size() > 2) {
        // Try to fit Parent (size-2)
        int parentIdx = static_cast<int>(parts.size()) - 2;
        float parentW = partWidths[parentIdx] + chipPadX * 2.0f + separatorW;
        
        // If Parent fits, we include it. In fact, user requested "always have 3".
        // We will try our best to fit it. If it doesn't fit, we might have to ellipsize it further?
        // For now, standard fitting logic.
        
        // Start filling from Parent backwards
        rightStartIndex = parts.size() - 1; // Includes Current
        
        float currentRightWidth = 0.0f;
        // Loop from Parent down to 1
        for (int i = static_cast<int>(parts.size()) - 2; i >= 1; --i) {
             float partW = partWidths[static_cast<size_t>(i)] + chipPadX * 2.0f + separatorW;
             
             if (currentRightWidth + partW <= availableForMiddle) {
                 currentRightWidth += partW;
                 rightStartIndex = static_cast<size_t>(i);
             } else {
                 break;
             }
        }
        
    } else if (parts.size() == 2) {
        // Root + Current only. No gap.
        rightStartIndex = 1; 
    } else {
        // Root only
        rightStartIndex = 1;
    }
    
    // RENDERING
    float currentX = breadcrumbRect.x;
    breadcrumbs_.clear();
    std::filesystem::path buildPath;

    // 1. Draw Root
    if (sandboxed) {
         buildPath = std::filesystem::path(rootPath_);
    } else {
         buildPath = parts[0];
    }
    
    {
        std::string partName = partDisplayNames[0];
        const float chipW = partWidths[0] + chipPadX * 2.0f;
        const NUIRect partRect(currentX, chipRowRect.y, chipW, chipRowRect.height);
        
        Breadcrumb b;
        b.name = partName;
        b.path = buildPath.string();
        b.x = currentX;
        b.width = chipW;
        breadcrumbs_.push_back(b);
        
        bool isHovered = (0 == hoveredBreadcrumbIndex_);
        // If it's the only one, it's also the last one
        bool isLast = (parts.size() == 1);

        if (isHovered) {
             renderer.fillRoundedRect(partRect, chipRadius, hoverColor_);
             renderer.strokeRoundedRect(partRect, chipRadius, 1, hoverColor_.lightened(0.2f));
        } else if (isLast) {
             renderer.fillRoundedRect(partRect, chipRadius, selectedColor_.withAlpha(0.15f));
             renderer.strokeRoundedRect(partRect, chipRadius, 1, selectedColor_.withAlpha(0.3f));
        } else {
             renderer.fillRoundedRect(partRect, chipRadius, NUIColor(1.0f, 1.0f, 1.0f, 0.03f));
        }
        
        NUIColor color = isHovered ? NUIColor::white() : (isLast ? selectedColor_ : textColor_);
        renderer.drawText(partName, NUIPoint(currentX + chipPadX, breadcrumbTextY), fontSize, color);
        
        currentX += chipW;
        
        // Draw separator after root if we have more items
        if (parts.size() > 1) {
            renderer.drawText(separatorText, NUIPoint(currentX + separatorPad * 0.5f, breadcrumbTextY), fontSize, textColor_.withAlpha(0.45f));
            currentX += separatorW;
        }
    }

    // 2. Draw Ellipsis if gap exists
    // 2. Draw Ellipsis if gap exists
    // Gap exists if rightStartIndex > 1 (meaning we skipped index 1, 2, etc.)
    if (rightStartIndex > 1) {
        // Create an interactive breadcrumb for the ellipsis
        Breadcrumb b;
        b.name = "...";
        b.x = currentX;
        b.width = ellipsisW;
        
        // Populate hidden paths
        // We start from where buildPath currently is (parts[0]) 
        // and append parts up to rightStartIndex-1.
        auto tempPath = std::filesystem::path(buildPath);
        for (size_t k = 1; k < rightStartIndex; ++k) {
             tempPath /= parts[k];
             b.hiddenPaths.push_back(tempPath.string());
        }
        
        breadcrumbs_.push_back(b);

        // Render ellipsis
        // Check hover state for ellipsis
        int viewIndex = static_cast<int>(breadcrumbs_.size()) - 1;
        bool isHovered = (viewIndex == hoveredBreadcrumbIndex_);
        const NUIRect partRect(currentX, chipRowRect.y, ellipsisW, chipRowRect.height);

        if (isHovered) {
             renderer.fillRoundedRect(partRect, chipRadius, hoverColor_);
             renderer.strokeRoundedRect(partRect, chipRadius, 1, hoverColor_.lightened(0.2f));
        }
        
        renderer.drawText("...", NUIPoint(currentX + chipPadX, breadcrumbTextY), fontSize, textColor_.withAlpha(0.55f));
        currentX += ellipsisW;
        
        // Separator after ellipsis
        renderer.drawText(separatorText, NUIPoint(currentX + separatorPad * 0.5f, breadcrumbTextY), fontSize, textColor_.withAlpha(0.45f));
        currentX += separatorW;
        
        // Update buildPath for the next visible items
        for (size_t k = 1; k < rightStartIndex; ++k) {
             buildPath /= parts[k];
        }
    } else {
        // No gap, buildPath is currently at parts[0]
    }

    // 3. Draw Right Side Items
    for (size_t partIndex = rightStartIndex; partIndex < parts.size(); ++partIndex) {
        std::string partName = partDisplayNames[partIndex];

        const float chipW = partWidths[partIndex] + chipPadX * 2.0f;
        const NUIRect partRect(currentX, chipRowRect.y, chipW, chipRowRect.height);

        buildPath /= parts[partIndex];

        Breadcrumb b;
        b.name = partName;
        b.path = buildPath.string();
        b.x = currentX;
        b.width = chipW;
        breadcrumbs_.push_back(b);

        // Indices in breadcrumbs_ vector: Root is 0. Next visible is 1.
        // We need to match hovered index correctly.
        int viewIndex = static_cast<int>(breadcrumbs_.size()) - 1;
        bool isHovered = (viewIndex == hoveredBreadcrumbIndex_);
        bool isLast = (partIndex == parts.size() - 1);

        if (isHovered) {
            renderer.fillRoundedRect(partRect, chipRadius, hoverColor_);
            renderer.strokeRoundedRect(partRect, chipRadius, 1, hoverColor_.lightened(0.2f));
        } else if (isLast) {
            renderer.fillRoundedRect(partRect, chipRadius, selectedColor_.withAlpha(0.15f));
            renderer.strokeRoundedRect(partRect, chipRadius, 1, selectedColor_.withAlpha(0.3f));
        } else {
            renderer.fillRoundedRect(partRect, chipRadius, NUIColor(1.0f, 1.0f, 1.0f, 0.03f));
        }

        const auto color = isHovered ? NUIColor::white() : (isLast ? selectedColor_ : textColor_);
        renderer.drawText(partName, NUIPoint(currentX + chipPadX, breadcrumbTextY), fontSize, color);

        currentX += chipW;

        if (partIndex < parts.size() - 1) {
            renderer.drawText(separatorText, NUIPoint(currentX + separatorPad * 0.5f, breadcrumbTextY), fontSize, textColor_.withAlpha(0.45f));
            currentX += separatorW;
        }
    }
}
bool FileBrowser::handleBreadcrumbMouseEvent(const NUIMouseEvent& event) {
    if (breadcrumbs_.empty() || breadcrumbBounds_.isEmpty()) return false;
    const float y = breadcrumbBounds_.y;
    const float h = breadcrumbBounds_.height;
    
    int hoveredIndex = -1;

    for (size_t i = 0; i < breadcrumbs_.size(); ++i) {
        const auto& crumb = breadcrumbs_[i];
        const float w = crumb.width > 0.0f ? crumb.width : static_cast<float>(crumb.name.size()) * 7.0f;
        if (event.position.x >= crumb.x && event.position.x <= crumb.x + w &&
            event.position.y >= y && event.position.y <= y + h) {
            
            hoveredIndex = static_cast<int>(i);

            if (event.pressed && event.button == NUIMouseButton::Left) {
                if (crumb.name == "...") {
                     // Show hidden folders menu
                     showHiddenBreadcrumbMenu(crumb.hiddenPaths, event.position);
                } else {
                     navigateToBreadcrumb(static_cast<int>(i));
                }
                return true;
            }
        }
    }
    
    if (hoveredIndex != hoveredBreadcrumbIndex_) {
        hoveredBreadcrumbIndex_ = hoveredIndex;
    }
    
    return false;
}

bool FileBrowser::handleSearchBoxMouseEvent(const NUIMouseEvent& event) {
    // Handle legacy search box input? No, NUITextInput handles it.
    // We return false here so events might propagate if we had other handlers
    return false;
}

// setPreviewPanelVisible removed - functionality moved to FilePreviewPanel
/*
void FileBrowser::setPreviewPanelVisible(bool visible) {
    if (previewPanelVisible_ == visible) return;
    previewPanelVisible_ = visible;
    
    // Trigger layout update and ensuring scrolling is clamped
    NUIRect b = getBounds();
    onResize(b.width, b.height); 
    setDirty(true);
}
*/

// renderPreviewPanel is implemented at the bottom of this file

std::string FileBrowser::getSearchQuery() const {
    return searchInput_ ? searchInput_->getText() : "";
}

bool FileBrowser::isSearchBoxFocused() const {
    return searchInput_ ? searchInput_->isFocused() : false;
}

// === PERSISTENT STATE SAVE/LOAD ===

void FileBrowser::saveState(const std::string& filePath) {
    std::ofstream file(filePath);
    if (!file.is_open()) {
        Nomad::Log::warning("[FileBrowser] Failed to save state to: " + filePath);
        return;
    }
    
    // Save as simple key=value format
    file << "currentPath=" << currentPath_ << "\n";
    file << "rootPath=" << rootPath_ << "\n";
    file << "scrollOffset=" << scrollOffset_ << "\n";
    file << "sortMode=" << static_cast<int>(sortMode_) << "\n";
    file << "sortAscending=" << (sortAscending_ ? "1" : "0") << "\n";
    
    // Save expanded folders
    file << "expandedFolders=";
    bool first = true;
    std::function<void(const FileItem&)> saveExpanded = [&](const FileItem& item) {
        if (item.isDirectory && item.isExpanded) {
            if (!first) file << "|";
            file << item.path;
            first = false;
        }
        for (const auto& child : item.children) {
            saveExpanded(child);
        }
    };
    for (const auto& item : rootItems_) {
        saveExpanded(item);
    }
    file << "\n";
    
    // Save favorites
    file << "favorites=";
    for (size_t i = 0; i < favoritesPaths_.size(); ++i) {
        if (i > 0) file << "|";
        file << favoritesPaths_[i];
    }
    file << "\n";

    // Save tag filter + tags
    file << "tagFilter=" << activeTagFilter_ << "\n";
    file << "tags=";
    bool firstTagEntry = true;
    for (const auto& [pathKey, tags] : tagsByPath_) {
        if (pathKey.empty() || tags.empty()) continue;
        if (!firstTagEntry) file << "|";
        file << pathKey << ">";
        for (size_t i = 0; i < tags.size(); ++i) {
            if (i > 0) file << ",";
            file << tags[i];
        }
        firstTagEntry = false;
    }
    file << "\n";
    
    file.close();
    Nomad::Log::info("[FileBrowser] State saved to: " + filePath);
}

void FileBrowser::loadState(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        Nomad::Log::info("[FileBrowser] No saved state found at: " + filePath);
        return;
    }
    
    std::string line;
    std::string loadedCurrentPath;
    std::string loadedRootPath;
    float loadedScrollOffset = 0.0f;
    bool hasScrollOffset = false;
    SortMode loadedSortMode = sortMode_;
	    bool hasSortMode = false;
	    bool loadedSortAscending = sortAscending_;
	    bool hasSortAscending = false;
	    std::vector<std::string> expandedFolders;
	    std::vector<std::string> loadedFavorites;
	    bool hasFavorites = false;
	    std::string loadedTagFilter;
	    bool hasTagFilter = false;
	    std::unordered_map<std::string, std::vector<std::string>> loadedTagsByPath;
	    bool hasTags = false;
	    
	    while (std::getline(file, line)) {
	        size_t eqPos = line.find('=');
	        if (eqPos == std::string::npos) continue;
        
        std::string key = line.substr(0, eqPos);
        std::string value = line.substr(eqPos + 1);
        
        if (key == "currentPath" && !value.empty()) {
            loadedCurrentPath = value;
        }
        else if (key == "rootPath" && !value.empty()) {
            loadedRootPath = value;
        }
        else if (key == "scrollOffset") {
            try {
                loadedScrollOffset = std::stof(value);
                hasScrollOffset = true;
            } catch (...) {}
        }
        else if (key == "sortMode") {
            try {
                loadedSortMode = static_cast<SortMode>(std::stoi(value));
                hasSortMode = true;
            } catch (...) {}
        }
        else if (key == "sortAscending") {
            loadedSortAscending = (value == "1");
            hasSortAscending = true;
        }
        else if (key == "expandedFolders" && !value.empty()) {
            // Parse pipe-separated paths
            size_t start = 0;
            size_t end;
            while ((end = value.find('|', start)) != std::string::npos) {
                expandedFolders.push_back(value.substr(start, end - start));
                start = end + 1;
            }
            if (start < value.length()) {
                expandedFolders.push_back(value.substr(start));
            }
        }
	        else if (key == "favorites" && !value.empty()) {
	            hasFavorites = true;
	            size_t start = 0;
	            size_t end;
	            while ((end = value.find('|', start)) != std::string::npos) {
	                loadedFavorites.push_back(value.substr(start, end - start));
	                start = end + 1;
	            }
	            if (start < value.length()) {
	                loadedFavorites.push_back(value.substr(start));
	            }
	        }
	        else if (key == "favorites") {
	            // Present but empty => clear favorites.
	            hasFavorites = true;
	        }
	        else if (key == "tagFilter") {
	            hasTagFilter = true;
	            loadedTagFilter = value;
	        }
	        else if (key == "tags") {
	            hasTags = true;
	            loadedTagsByPath.clear();
	            if (!value.empty()) {
	                size_t start = 0;
	                while (start < value.size()) {
	                    size_t end = value.find('|', start);
	                    if (end == std::string::npos) end = value.size();
	                    const std::string entry = value.substr(start, end - start);
	                    const size_t sep = entry.find('>');
	                    if (sep != std::string::npos) {
	                        const std::string pathKey = entry.substr(0, sep);
	                        const std::string tagsCsv = entry.substr(sep + 1);
	                        if (!pathKey.empty() && !tagsCsv.empty()) {
	                            std::vector<std::string> tags;
	                            size_t ts = 0;
	                            while (ts < tagsCsv.size()) {
	                                size_t te = tagsCsv.find(',', ts);
	                                if (te == std::string::npos) te = tagsCsv.size();
	                                std::string tag = tagsCsv.substr(ts, te - ts);
	                                if (!tag.empty()) {
	                                    tags.push_back(std::move(tag));
	                                }
	                                ts = te + 1;
	                            }
	                            if (!tags.empty()) {
	                                loadedTagsByPath[pathKey] = std::move(tags);
	                            }
	                        }
	                    }
	                    start = end + 1;
	                }
	            }
	        }
	    }
	    
	    file.close();

	    // Apply settings in safe order (sort/root before directory load)
    if (!loadedRootPath.empty() && std::filesystem::exists(loadedRootPath) && std::filesystem::is_directory(loadedRootPath)) {
        rootPath_ = canonicalOrNormalized(std::filesystem::path(loadedRootPath)).string();
    } else {
        rootPath_.clear();
    }

	    if (hasSortMode) sortMode_ = loadedSortMode;
	    if (hasSortAscending) sortAscending_ = loadedSortAscending;
	    if (hasFavorites) favoritesPaths_ = std::move(loadedFavorites);
	    if (hasTags) tagsByPath_ = std::move(loadedTagsByPath);
	    if (hasTagFilter) activeTagFilter_ = std::move(loadedTagFilter);

	    if (loadedCurrentPath.empty() && !rootPath_.empty()) {
	        loadedCurrentPath = rootPath_;
	    }
    if (!loadedCurrentPath.empty()) {
        setCurrentPath(loadedCurrentPath);
    } else {
        // Ensure content is loaded with the current sort settings.
        loadDirectoryContents();
        updateBreadcrumbs();
    }
    
    // Re-expand saved folders
    std::function<void(FileItem&)> expandSaved = [&](FileItem& item) {
        if (item.isDirectory) {
            for (const auto& expandedPath : expandedFolders) {
                if (item.path == expandedPath) {
                    // Load children and expand
                    if (!item.hasLoadedChildren) {
                        loadFolderContents(&item);
                    }
                    item.isExpanded = true;
                    break;
                }
            }
            for (auto& child : item.children) {
                expandSaved(child);
            }
        }
    };
    for (auto& item : rootItems_) {
        expandSaved(item);
	    }
	    
	    updateDisplayList();
	    if (isFilterActive()) {
	        applyFilter();
	    }

	    if (hasScrollOffset) {
	        const auto& view = getActiveView();
	        auto& themeManager = NUIThemeManager::getInstance();
	        float itemHeight = themeManager.getComponentDimension("fileBrowser", "itemHeight");
	        float maxScroll = std::max(0.0f, static_cast<float>(view.size()) * itemHeight - scrollbarTrackHeight_);
	        scrollOffset_ = std::max(0.0f, std::min(loadedScrollOffset, maxScroll));
        targetScrollOffset_ = scrollOffset_;
        lastRenderedOffset_ = scrollOffset_;
        updateScrollbarVisibility();
    }
    Nomad::Log::info("[FileBrowser] State loaded from: " + filePath);
}


void FileBrowser::showHiddenBreadcrumbMenu(const std::vector<std::string>& hiddenPaths, const NUIPoint& position) {
    if (!popupMenu_ || hiddenPaths.empty()) return;

    popupMenu_->clear();
    
    // Header
    // popupMenu_->addItem("Hidden Folders", [](){}); 
    // popupMenu_->addSeparator();

    for (const auto& path : hiddenPaths) {
        std::filesystem::path p(path);
        std::string name = p.filename().string();
        if (name.empty()) name = p.string();
        
        popupMenu_->addItem(name, [this, path]() {
            navigateTo(path);
        });
    }

    popupMenu_->showAt(position);
}



} // namespace NomadUI
