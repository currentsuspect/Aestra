// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "FileBrowser.h"
#include "NUIContextMenu.h"
#include "NUIThemeSystem.h"
#include "NUIDragDrop.h"
#include "Graphics/NUIRenderer.h"
#include "Graphics/OpenGL/NUIRenderCache.h"
#include "NUITextInput.h"
#include "../AestraCore/include/AestraLog.h"
#include "AudioFileValidator.h"
#include "MiniAudioDecoder.h"
#include "../AestraPlat/include/AestraPlatform.h"
#include "Platform/NUIPlatformBridge.h"
#include "../AestraCore/include/AestraUnifiedProfiler.h"
#include "../AestraCore/include/AestraJSON.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <fstream>
#include <unordered_map>

#ifdef _WIN32
#include <Windows.h>
#endif

using namespace Aestra;

namespace AestraUI {

namespace {

constexpr float kPreviewPanelHeight = 68.0f;
constexpr float kCompactNavWidth = 52.0f;
constexpr float kCompactNavStartWidth = 320.0f;
constexpr float kExpandedNavStartWidth = 440.0f;
constexpr float BROWSER_SEARCH_ROW_H = 34.0f;
constexpr float BROWSER_TOP_PAD = 7.0f;
constexpr float BROWSER_CONTENT_GAP = 8.0f;
constexpr float BROWSER_NAV_ROW_H = 30.0f;
constexpr float BROWSER_LIST_HEADER_H = 58.0f;
constexpr float BROWSER_LIST_ROW_H = 30.0f;

static std::string getSettingsPath() {
    const char* home = getenv("HOME");
    if (!home) home = "/tmp";
    std::string dir = std::string(home) + "/.config/aestra";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir + "/browser_settings.json";
}

AestraUI::NUIComponent* getRootComponent(AestraUI::NUIComponent* component) {
    AestraUI::NUIComponent* root = component;
    while (root && root->getParent()) {
        root = root->getParent();
    }
    return root;
}

float computeNavigationWidth(float browserWidth) {
    if (browserWidth <= kCompactNavStartWidth) {
        return std::min(browserWidth, kCompactNavWidth);
    }

    const float expandedWidth = std::clamp(browserWidth * 0.34f, 118.0f, 188.0f);
    if (browserWidth >= kExpandedNavStartWidth) {
        return expandedWidth;
    }

    const float expandedAtBreakpoint = kExpandedNavStartWidth * 0.34f;
    const float progress = (browserWidth - kCompactNavStartWidth) / (kExpandedNavStartWidth - kCompactNavStartWidth);
    return kCompactNavWidth + progress * (expandedAtBreakpoint - kCompactNavWidth);
}

void detachPopupMenu(const std::shared_ptr<AestraUI::NUIContextMenu>& menu) {
    if (!menu) return;
    if (auto* parent = menu->getParent()) {
        parent->removeChild(menu);
    }
}

void attachAndShowPopupMenu(AestraUI::NUIComponent* owner,
                            const std::shared_ptr<AestraUI::NUIContextMenu>& menu,
                            const AestraUI::NUIPoint& position) {
    if (!owner || !menu) return;
    AestraUI::NUIComponent* root = getRootComponent(owner);
    if (!root) root = owner;
    root->addChild(menu);
    menu->showAt(position);
    root->repaint();
}

std::string ellipsizeMiddle(NUIRenderer& renderer, const std::string& text, float fontSize, float maxWidth) {
    constexpr const char* kEllipsis = "...";

    if (text.empty()) return text;
    if (maxWidth <= 0.0f) return "";

    // Check full string first (common case)
    if (renderer.measureText(text, fontSize).width <= maxWidth) {
        return text;
    }

    // Measure prefix (first 60%) and suffix (last 40%) to maintain context
    size_t prefixLen = static_cast<size_t>(text.size() * 0.6);
    size_t suffixLen = text.size() - prefixLen;

    std::string prefix = text.substr(0, prefixLen);
    std::string suffix = text.substr(text.size() - suffixLen);

    // Shrink suffix first, then prefix
    while (!suffix.empty() && renderer.measureText(prefix + kEllipsis + suffix, fontSize).width > maxWidth) {
        suffix = suffix.substr(1);
    }
    while (!prefix.empty() && renderer.measureText(prefix + kEllipsis + suffix, fontSize).width > maxWidth) {
        prefix = prefix.substr(0, prefix.size() - 1);
    }

    return prefix + kEllipsis + suffix;
}

static int parseBpmFromFilename(const std::string& name) {
    // Matches: "kick_120bpm", "loop 128 BPM", "90bpm_hat", "140_bpm_loop"
    for (size_t i = 0; i + 2 < name.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(name[i]))) continue;
        size_t start = i;
        while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i]))) ++i;
        size_t end = i;
        if (end - start < 2 || end - start > 3) continue;
        std::string numStr = name.substr(start, end - start);
        int bpm = std::stoi(numStr);
        if (bpm < 60 || bpm > 300) continue;
        // Check for "bpm" nearby (before or after the number)
        std::string context = name.substr(start > 4 ? start - 4 : 0,
                                          std::min(name.size() - start, end + 5));
        std::string lower;
        for (char c : context) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower.find("bpm") != std::string::npos) return bpm;
    }
    return 0;
}

std::string ellipsizeEnd(NUIRenderer& renderer, const std::string& text, float fontSize, float maxWidth) {
    if (text.empty()) return text;
    if (maxWidth <= 0.0f) return "";

    if (renderer.measureText(text, fontSize).width <= maxWidth) return text;

    constexpr const char* kEllipsis = "...";
    const float ellipsisW = renderer.measureText(kEllipsis, fontSize).width;
    if (ellipsisW >= maxWidth) return text;

    static constexpr size_t MIN_VISIBLE = 24;
    const size_t startChars = std::min(MIN_VISIBLE, text.size());
    std::string minCandidate = text.substr(0, startChars) + kEllipsis;
    if (renderer.measureText(minCandidate, fontSize).width > maxWidth) return minCandidate;

    int low = static_cast<int>(startChars);
    int high = static_cast<int>(text.size());
    std::string best = minCandidate;

    while (low <= high) {
        int mid = low + (high - low) / 2;
        std::string candidate = text.substr(0, mid) + kEllipsis;
        if (renderer.measureText(candidate, fontSize).width <= maxWidth) {
            best = candidate;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }

    return best;
}

std::filesystem::path canonicalOrNormalized(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(p, ec);
    return ec ? p.lexically_normal() : canonical;
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

std::string resolveExistingDirectoryPath(const std::string& requestedPath, const std::string& rootPath) {
    namespace fs = std::filesystem;

    std::error_code ec;
    fs::path root = rootPath.empty() ? fs::path() : fs::path(rootPath);
    fs::path candidate = requestedPath.empty() ? root : fs::path(requestedPath);

    if (!root.empty() && !isPathUnderRoot(candidate, root)) {
        candidate = root;
    }

    while (!candidate.empty()) {
        if (fs::exists(candidate, ec) && fs::is_directory(candidate, ec)) {
            if (!root.empty() && !isPathUnderRoot(candidate, root)) {
                break;
            }
            return canonicalOrNormalized(candidate).string();
        }

        const fs::path parent = candidate.parent_path();
        if (parent.empty() || parent == candidate) {
            break;
        }
        candidate = parent;
    }

    if (!root.empty() && fs::exists(root, ec) && fs::is_directory(root, ec)) {
        return canonicalOrNormalized(root).string();
    }

    if (!root.empty()) {
        return {};
    }

    return requestedPath;
}

} // namespace

// =============================================================================
// SECTION: Construction & Initialization
// =============================================================================

FileBrowser::FileBrowser()
    : NUIComponent()
    , selectedFile_(nullptr)
    , selectedIndex_(-1)
    , scrollOffset_(0.0f)
    , targetScrollOffset_(0.0f)   // Initialize lerp target
    , scrollVelocity_(0.0f)
    , itemHeight_(22.0f)           // Reduced for compact look (was 36.0f)
    , visibleItems_(0)
    , showHiddenFiles_(false)
    , lastCachedWidth_(0.0f)       // Initialize cache width tracker
    , lastRenderedOffset_(0.0f)    // Initialize render tracking
    , effectiveWidth_(0.0f)        // Initialize effective render width
    , scrollbarVisible_(false)
    , scrollbarOpacity_(0.0f)
    , scrollbarWidth_(6.0f)        // Slim default scrollbar
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
    , m_cacheId(reinterpret_cast<uint64_t>(this))
    , m_cacheInvalidated(true)
    , m_isRenderingToCache(false)
{
    // Set default size from theme
    // ... (theme logic handled in onResize)

    // DEFAULT PATH & SANDBOX LOGIC
    // User requested "/Documents/Aestra" as the root.
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
        targetRoot = (std::filesystem::path(userProfile) / "Documents" / "Aestra").string();
    } else {
        // Fallback for this specific environment if env var fails
        targetRoot = "C:/Users/Current/Documents/Aestra";
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
        Aestra::Log::info("[FileBrowser] Set root to: " + rootPath_);
    } else {
         // Final Fallback to CWD if creation fails
         rootPath_ = std::filesystem::current_path().string();
         currentPath_ = rootPath_;
         Aestra::Log::warning("[FileBrowser] Failed to set default root, fallback to CWD: " + rootPath_);
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
    searchInput_ = std::make_shared<NUITextInput>();
    searchInput_->setPlaceholderText("Search files and folders");
    addChild(searchInput_);

    // Bind search callback
    searchInput_->setOnTextChange([this](const std::string& text) {
        if (onSearchTextChanged_) {
            onSearchTextChanged_(text);
        }
        if (activeNavAction_ != BrowserNavAction::Plugins && activeNavAction_ != BrowserNavAction::Patterns) {
            applyFilter();
        }
    });
    searchInput_->setOnEscapeKey([this]() {
        searchInput_->clear();
        searchInput_->setFocused(false);
        applyFilter();
    });
    searchInput_->setMaxLength(512);
    searchInput_->setTextColor(themeManager.getColor("textPrimary"));
    searchInput_->setPlaceholderColor(themeManager.getColor("textSecondary").withAlpha(0.56f));
    searchInput_->setPadding(6.0f);
    searchInput_->setBorderRadius(5.0f);
    searchInput_->setBackgroundColor(themeManager.getColor("backgroundSecondary").darkened(0.02f));
    searchInput_->setBorderColor(themeManager.getColor("border"));
    searchInput_->setFocusedBorderColor(themeManager.getColor("focusRing"));
    searchInput_->setBorderWidth(1.0f);

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
    // Increased visibility for dark theme
    unknownFileIcon_->setColor(themeManager.getColor("textSecondary").withAlpha(0.9f));

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

    // Project Icon (Aestra Diamond)
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

    popupMenu_ = std::make_shared<NUIContextMenu>();
    popupMenu_->hide();

    // Initialize navigation history with the resolved root path (from top of constructor)
    navHistory_.clear();
    navHistory_.push_back(currentPath_);
    navHistoryIndex_ = 0;

    // Load Theme Colors (Consistent with AestraTheme)
    backgroundColor_ = themeManager.getColor("backgroundPrimary"); // Deep Void from theme
    textColor_ = themeManager.getColor("textPrimary");

    // Use theme accent for selection (consistent with the rest of the app)
    selectedColor_ = themeManager.getColor("accentPrimary");

    hoverColor_ = themeManager.getColor("buttonBgHover").withAlpha(0.72f);
    borderColor_ = themeManager.getColor("glassBorder");

    // Perform initial layout now that all members (icons, search input) are initialized
    // This initializes scrollbarTrackHeight_ and other layout vars needed by updateScrollbarVisibility
    onResize(static_cast<int>(getWidth()), static_cast<int>(getHeight()));

    // NOW start the scan, strictly after layout is ready
    loadDirectoryContents();
    // Aestra::Log::info("[FileBrowser] Constructor complete.");
}

FileBrowser::~FileBrowser() {
    stopScanWorker();
}

// =============================================================================
// SECTION: Directory Scanning (Background Thread)
// =============================================================================

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
        result.items = scanDirectory(task.path, task.depth, task.showHidden, task.generation, result.error);

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
    ".madproj", ".Aestra"
};

bool FileFilter::isAllowed(const std::string& path) {
    if (path.empty()) return false;

    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) return true;

    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (audioExtensions.count(ext)) return true;
    if (projectExtensions.count(ext)) return true;
    if (ext == ".mid" || ext == ".midi") return true;

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
    if (ext == ".mid" || ext == ".midi") return FileType::MidiFile;
    if (projectExtensions.count(ext)) return FileType::ProjectFile;

    return FileType::Unknown;
}

std::vector<FileItem> FileBrowser::scanDirectory(const std::string& path, int depth, bool showHidden,
                                                 uint64_t generation, std::string& error) const {
    std::vector<FileItem> items;
    error.clear();
    try {
        const std::filesystem::path dir(path);
        const auto options = std::filesystem::directory_options::skip_permission_denied;
        std::error_code iterEc;
        std::filesystem::directory_iterator it(dir, options, iterEc);
        if (iterEc) {
            error = iterEc.message();
            Log::warning(std::string("[FileBrowser] Scan failed for ") + path + ": " + iterEc.message());
            return items;
        }

        for (; it != std::filesystem::directory_iterator(); it.increment(iterEc)) {
            if (scanStop_.load(std::memory_order_acquire) ||
                generation != scanGeneration_.load(std::memory_order_acquire)) {
                break;
            }

            if (iterEc) {
                error = iterEc.message();
                Log::warning(std::string("[FileBrowser] Scan iteration failed for ") + path + ": " + iterEc.message());
                break;
            }

            const auto& entry = *it;

            const std::string name = entry.path().filename().string();
            if (!showHidden && !name.empty() && name[0] == '.') {
                continue;
            }

            const std::string entryPath = entry.path().string();

            // --- SMART FILTER APPLIED HERE ---
            // If it's not a directory and not in our whitelist, skip it.
            std::error_code dirEc;
            const bool isDir = entry.is_directory(dirEc);
            if (dirEc) continue;

            if (!isDir && !FileFilter::isAllowed(entryPath)) {
                continue; // Whitelist filter
            }

            FileType type = FileFilter::getType(entryPath, isDir);
            size_t size = 0;
            std::string lastModified;

            if (!isDir) {
                std::error_code sizeEc;
                size = static_cast<size_t>(entry.file_size(sizeEc));
                if (sizeEc) size = 0;
            }

            FileItem item(name, entryPath, type, isDir, size, lastModified);
            item.depth = depth;
            if (!isDir && (type == FileType::AudioFile || type == FileType::MusicFile ||
                           type == FileType::WavFile || type == FileType::Mp3File ||
                           type == FileType::FlacFile)) {
                item.detectedBpm = parseBpmFromFilename(name);
            }
            items.push_back(std::move(item));
        }
    } catch (const std::exception& e) {
        error = e.what();
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
            scanError_ = std::move(result.error);

            rootItems_ = std::move(result.items);
            sortFiles();
            updateDisplayList();

            if (isFilterActive()) {
                applyFilter();
            } else {
                filteredFiles_.clear();
                viewDirty_ = true;
                if (!pendingSelectionPath_.empty()) {
                    const std::string restoredPath = pendingSelectionPath_;
                    pendingSelectionPath_.clear();
                    selectFile(restoredPath);
                } else if (!displayItems_.empty()) {
                    selectedIndex_ = 0;
                    selectedFile_ = displayItems_[0];
                    selectedIndices_.clear();
                    selectedIndices_.push_back(0);
                    lastShiftSelectIndex_ = 0;
                } else {
                    clearSelection();
                }
                updateScrollbarVisibility();
                invalidateCache();
            }

            didUpdate = true;
            continue;
        }

        if (result.kind == ScanKind::Folder) {
            if (FileItem* folder = findItemByPath(result.path)) {
                folder->children = std::move(result.items);
                folder->hasLoadedChildren = result.error.empty();
                folder->isLoadingChildren = false;

                if (!result.error.empty()) {
                    FileItem placeholder("Folder unavailable — collapse and reopen to retry", "",
                                         FileType::Unknown, false, 0, "");
                    placeholder.depth = folder->depth + 1;
                    placeholder.isPlaceholder = true;
                    folder->children.push_back(std::move(placeholder));
                }

                std::stable_sort(folder->children.begin(), folder->children.end(),
                                 [this](const FileItem& a, const FileItem& b) { return compareFileItems(a, b); });

                updateDisplayList();
                if (isFilterActive()) {
                    applyFilter();
                } else {
                    updateScrollbarVisibility();
                    invalidateCache();
                }
                didUpdate = true;
            }
        }
    }

    if (didUpdate) {
        updateScrollbarVisibility();
    }
}

// =============================================================================
// SECTION: Rendering
// =============================================================================

// =============================================================================
// SECTION: Rendering with FBO Caching
// =============================================================================

FileBrowser::BrowserLayout FileBrowser::computeBrowserLayout() const {
    NUIRect bounds = getBounds();
    const float effectiveW = bounds.width;
    const float searchH = BROWSER_SEARCH_ROW_H;
    const float headerH = searchH;
    const float contentY = bounds.y + headerH;
    const float contentH = std::max(0.0f, bounds.height - headerH);
    const float navW = computeNavigationWidth(effectiveW);
    const float listX = bounds.x + navW;
    const float listW = std::max(0.0f, effectiveW - navW);

    BrowserLayout layout;
    layout.searchBar = NUIRect(bounds.x, bounds.y, std::max(0.0f, effectiveW), searchH);
    layout.search = NUIRect(bounds.x + 30.0f, bounds.y + 4.0f,
                            std::max(0.0f, effectiveW - 64.0f), searchH - 8.0f);
    layout.navPane = NUIRect(bounds.x, contentY, navW, contentH);
    layout.listHeader = NUIRect(listX, contentY, listW, BROWSER_LIST_HEADER_H);
    const float previewH = previewPanelVisible_ ? std::min(kPreviewPanelHeight, contentH) : 0.0f;
    layout.list = NUIRect(listX, contentY + BROWSER_LIST_HEADER_H, listW,
                          std::max(0.0f, contentH - BROWSER_LIST_HEADER_H - previewH));
    const float chromeY = layout.listHeader.y + 3.0f;
    layout.backButton = NUIRect(layout.listHeader.x + 5.0f, chromeY, 22.0f, 24.0f);
    layout.forwardButton = NUIRect(layout.backButton.right() + 2.0f, chromeY, 22.0f, 24.0f);
    layout.upButton = NUIRect(layout.forwardButton.right() + 2.0f, chromeY, 22.0f, 24.0f);
    layout.pathLabel = NUIRect(layout.upButton.right() + 7.0f, chromeY,
                               std::max(0.0f, layout.listHeader.right() - layout.upButton.right() - 14.0f), 24.0f);
    layout.sortButton = NUIRect(layout.listHeader.right() - 31.0f, layout.listHeader.y + 31.0f, 26.0f, 23.0f);
    layout.filterButton = NUIRect(layout.sortButton.x - 29.0f, layout.sortButton.y, 26.0f, 23.0f);
    layout.searchActionButton = NUIRect(layout.searchBar.right() - 30.0f, layout.searchBar.y + 4.0f, 26.0f, 26.0f);
    layout.navWidth = navW;
    return layout;
}

float FileBrowser::getNavPaneWidth() const {
    NUIRect bounds = getBounds();
    const float effectiveW = bounds.width;
    return computeNavigationWidth(effectiveW);
}

bool FileBrowser::usesCompactNavigation() const {
    return getNavPaneWidth() < 112.0f;
}

NUIRect FileBrowser::getContentViewBounds() const {
    const BrowserLayout layout = computeBrowserLayout();
    return NUIRect(layout.listHeader.x, layout.searchBar.bottom(), layout.listHeader.width,
                   std::max(0.0f, layout.list.bottom() - layout.searchBar.bottom()));
}

void FileBrowser::registerContentView(BrowserNavAction action, const std::shared_ptr<NUIComponent>& component) {
    if (!component) {
        return;
    }

    for (auto& view : contentViews_) {
        if (view.action == action) {
            view.component = component;
            updateContentViews();
            return;
        }
    }

    contentViews_.push_back({action, component});
    updateContentViews();
}

void FileBrowser::setContentViewsEnabled(bool enabled) {
    if (contentViewsEnabled_ == enabled) {
        return;
    }
    contentViewsEnabled_ = enabled;
    updateContentViews();
}

void FileBrowser::updateContentViews() {
    const NUIRect contentBounds = getContentViewBounds();
    for (auto& view : contentViews_) {
        if (auto component = view.component.lock()) {
            const bool active = contentViewsEnabled_ && isVisible() && view.action == activeNavAction_;
            component->setBounds(contentBounds);
            component->setEnabled(active);
            component->setVisible(active);
        }
    }
}

void FileBrowser::renderNavigationPane(NUIRenderer& renderer, const BrowserLayout& layout) {
    auto& themeManager = NUIThemeManager::getInstance();
    navHits_.clear();

    const NUIColor paneBg = themeManager.getColor("backgroundSecondary");
    const NUIColor sectionColor = themeManager.getColor("textSecondary").withAlpha(0.58f);  // Stronger section headers
    const NUIColor rowText = themeManager.getColor("textPrimary").withAlpha(0.78f);
    const NUIColor muted = themeManager.getColor("textSecondary").withAlpha(0.48f);
    const NUIColor selectedBg = themeManager.getColor("selection");
    const NUIColor divider = themeManager.getColor("divider");
    const bool compact = layout.navWidth < 112.0f;

    renderer.fillRect(layout.navPane, paneBg);
    renderer.setClipRect(layout.navPane);
    renderer.drawLine({layout.navPane.right(), layout.navPane.y},
                      {layout.navPane.right(), layout.navPane.bottom()},
                      1.0f, divider);

    // Left header matches right listHeader exactly in height and structure
    const float navHeaderH = BROWSER_LIST_HEADER_H;
    const NUIRect navHeader(layout.navPane.x, layout.navPane.y, layout.navPane.width, navHeaderH);
    renderer.fillRect(navHeader, themeManager.getColor("backgroundSecondary").darkened(0.03f));
    renderer.drawLine({navHeader.x, navHeader.bottom()}, {navHeader.right(), navHeader.bottom()},
                      1.0f, themeManager.getColor("border").withAlpha(0.40f));

    // Folder name at top (like breadcrumb on the right)
    const auto& themeProps = themeManager.getCurrentTheme();
    std::string folderName = "Browse";
    if (!currentPath_.empty()) {
        std::filesystem::path p(currentPath_);
        folderName = p.filename().string();
        if (folderName.empty()) folderName = currentPath_;
    }
    if (!compact) {
        renderer.drawText(
            folderName,
            {navHeader.x + themeProps.spacingM,
             std::round(renderer.calculateTextY(NUIRect(navHeader.x, navHeader.y + 5.0f, navHeader.width, 20.0f),
                                                themeProps.fontSizeXS))},
            themeProps.fontSizeXS, themeManager.getColor("textPrimary").withAlpha(0.76f));
    } else {
        const std::string compactLabel = "LIB";
        const auto labelSize = renderer.measureText(compactLabel, 9.0f);
        renderer.drawText(compactLabel,
                          {navHeader.x + (navHeader.width - labelSize.width) * 0.5f,
                           std::round(renderer.calculateTextY(
                               NUIRect(navHeader.x, navHeader.y + 5.0f, navHeader.width, 20.0f), 9.0f))},
                          9.0f, themeManager.getColor("textSecondary").withAlpha(0.58f));
    }

    // Inner divider (like right header)
    renderer.drawLine({navHeader.x, navHeader.y + 27.0f},
                      {navHeader.right(), navHeader.y + 27.0f},
                      1.0f, themeManager.getColor("border").withAlpha(0.65f));

    // Scrollable content region below the fixed folder-name header. Short
    // windows keep the tail rows reachable without moving the header.
    const float navContentTop = layout.navPane.y + 28.0f;
    navViewportHeight_ = std::max(0.0f, layout.navPane.bottom() - navContentTop);
    const float navMaxScroll = std::max(0.0f, navContentHeight_ - navViewportHeight_);
    navScrollOffset_ = std::clamp(navScrollOffset_, 0.0f, navMaxScroll);
    renderer.setClipRect(NUIRect(layout.navPane.x, navContentTop, layout.navPane.width, navViewportHeight_));

    // Start nav content at the same Y as the right column labels
    float y = navContentTop - navScrollOffset_;
    int hitIndex = 0;

    auto collectionCount = [&](const std::string& tag) {
        int count = 0;
        for (const auto& [_, tags] : tagsByPath_) {
            if (std::find(tags.begin(), tags.end(), tag) != tags.end()) {
                ++count;
            }
        }
        return count;
    };

    auto drawSection = [&](const std::string& label) {
        if (compact) {
            y += 5.0f;
            renderer.drawLine({layout.navPane.x + 15.0f, y}, {layout.navPane.right() - 15.0f, y}, 1.0f,
                              divider.withAlpha(0.38f));
            y += 6.0f;
            return;
        }
        std::string upper = label;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        NUIRect labelRect(layout.navPane.x + themeProps.spacingM, y, layout.navPane.width - themeProps.spacingM * 2.0f, 22.0f);
        renderer.drawText(upper, {labelRect.x, std::round(renderer.calculateTextY(labelRect, themeProps.fontSizeXS))}, themeProps.fontSizeXS, sectionColor);
        if (label == "Collections") {
            // Small up-chevron replacing "^" text glyph
            const float upCx = layout.navPane.right() - 14.0f;
            const float upCy = labelRect.y + labelRect.height * 0.5f;
            const float upS = 2.5f;
            renderer.drawLine({upCx - upS, upCy + upS * 0.5f}, {upCx, upCy - upS * 0.5f}, 1.3f, sectionColor.withAlpha(0.72f));
            renderer.drawLine({upCx, upCy - upS * 0.5f}, {upCx + upS, upCy + upS * 0.5f}, 1.3f, sectionColor.withAlpha(0.72f));
        }
        y += 22.0f;
    };

    auto drawIcon = [&](const NUIRect& rect, BrowserNavAction action, bool selected) {
        const NUIColor iconColor = selected ? themeManager.getColor("textPrimary").withAlpha(0.86f) : muted.withAlpha(0.70f);
        const float cx = compact ? rect.x + rect.width * 0.5f : rect.x + 13.0f;
        const float cy = rect.y + rect.height * 0.5f;

        auto drawSvgIcon = [&](const char* svg) {
            static std::unordered_map<int, std::shared_ptr<NUIIcon>> iconCache;
            const int key = static_cast<int>(action);
            auto it = iconCache.find(key);
            if (it == iconCache.end()) {
                auto icon = std::make_shared<NUIIcon>(svg);
                it = iconCache.emplace(key, icon).first;
            }
            auto& icon = it->second;
            icon->setBounds({std::round(cx - 8.0f), std::round(cy - 8.0f), 16.0f, 16.0f});
            icon->setColor(iconColor);
            icon->onRender(renderer);
        };

        switch (action) {
            case BrowserNavAction::Favorites:
                drawSvgIcon(R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M12 3.8l2.25 4.55 5.03.73-3.64 3.55.86 5.01L12 15.28l-4.5 2.36.86-5.01L4.72 9.08l5.03-.73L12 3.8z"/></svg>)");
                break;
            case BrowserNavAction::Purple:
                renderer.fillRoundedRect({cx - 4.0f, cy - 4.0f, 8.0f, 8.0f}, 4.0f,
                                         NUIColor::fromHex(0x7c3aed, selected ? 0.98f : 0.82f));
                break;
            case BrowserNavAction::CollectionDrums:
                renderer.fillRoundedRect({cx - 4.0f, cy - 4.0f, 8.0f, 8.0f}, 4.0f,
                                         NUIColor::fromHex(0xf97316, selected ? 0.98f : 0.82f));
                break;
            case BrowserNavAction::CollectionInstruments:
                renderer.fillRoundedRect({cx - 4.0f, cy - 4.0f, 8.0f, 8.0f}, 4.0f,
                                         NUIColor::fromHex(0x22c55e, selected ? 0.98f : 0.82f));
                break;
            case BrowserNavAction::Vocals:
                renderer.fillRoundedRect({cx - 4.0f, cy - 4.0f, 8.0f, 8.0f}, 4.0f,
                                         NUIColor::fromHex(0x3b82f6, selected ? 0.98f : 0.82f));
                break;
            case BrowserNavAction::Sounds:
                drawSvgIcon(R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M14 5v10.2"/><path d="M14 5l5-1.3v10.2"/><circle cx="10" cy="17" r="3.1"/><circle cx="17" cy="15.7" r="2.4"/></svg>)");
                break;
            case BrowserNavAction::Drums:
                drawSvgIcon(R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><rect x="4" y="5" width="6" height="6" rx="1.6"/><rect x="14" y="5" width="6" height="6" rx="1.6"/><rect x="4" y="15" width="6" height="4" rx="1.4"/><rect x="14" y="15" width="6" height="4" rx="1.4"/></svg>)");
                break;
            case BrowserNavAction::Instruments:
                drawSvgIcon(R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><rect x="3.5" y="6" width="17" height="12" rx="2"/><path d="M3.5 11h17"/><path d="M8 6v12"/><path d="M12 11v7"/><path d="M16 6v12"/></svg>)");
                break;
            case BrowserNavAction::AudioEffects:
                drawSvgIcon(R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M6 4v16"/><path d="M12 4v16"/><path d="M18 4v16"/><circle cx="6" cy="9" r="2"/><circle cx="12" cy="15" r="2"/><circle cx="18" cy="8" r="2"/></svg>)");
                break;
            case BrowserNavAction::Plugins:
                drawSvgIcon(R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><rect x="6" y="6" width="12" height="12" rx="2"/><rect x="9.5" y="9.5" width="5" height="5" rx="1.2"/><path d="M9 2.8V6"/><path d="M15 2.8V6"/><path d="M9 18v3.2"/><path d="M15 18v3.2"/><path d="M2.8 9H6"/><path d="M2.8 15H6"/><path d="M18 9h3.2"/><path d="M18 15h3.2"/></svg>)");
                break;
            case BrowserNavAction::Patterns:
                drawSvgIcon(R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><rect x="4" y="5" width="16" height="14" rx="2"/><path d="M8 9h8"/><path d="M8 13h5"/><circle cx="16" cy="13" r="1.5"/></svg>)");
                break;
            case BrowserNavAction::Clips:
                drawSvgIcon(R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><rect x="4" y="6" width="16" height="12" rx="2"/><path d="M10 9.5l5 2.5-5 2.5v-5z"/></svg>)");
                break;
            case BrowserNavAction::Samples:
                drawSvgIcon(R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><rect x="4" y="7" width="16" height="10" rx="2"/><path d="M7 12h1.4l1.2-3 2.2 6 1.8-5 1.4 2h2"/></svg>)");
                break;
            case BrowserNavAction::Packs:
                drawSvgIcon(R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M7 7.5l5-2.8 5 2.8v9l-5 2.8-5-2.8v-9z"/><path d="M7.3 7.8L12 10.5l4.7-2.7"/><path d="M12 10.5v8.6"/></svg>)");
                break;
            case BrowserNavAction::UserLibrary:
                drawSvgIcon(R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="8" r="3.2"/><path d="M5.5 19c1.2-3.4 3.4-5.1 6.5-5.1s5.3 1.7 6.5 5.1"/></svg>)");
                break;
            case BrowserNavAction::CurrentProject:
            case BrowserNavAction::CustomPlace:
                drawSvgIcon(R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M4 8.2h6l1.6 2H20v7.3a1.8 1.8 0 0 1-1.8 1.8H5.8A1.8 1.8 0 0 1 4 17.5V8.2z"/><path d="M4 8.2V6.7A1.8 1.8 0 0 1 5.8 5h4.4l1.4 1.8H18"/></svg>)");
                break;
            case BrowserNavAction::AddFolder:
                drawSvgIcon(R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M3.5 8.2h6l1.6 2H18v7.1a1.7 1.7 0 0 1-1.7 1.7H5.2a1.7 1.7 0 0 1-1.7-1.7V8.2z"/><path d="M16.5 5.5v6"/><path d="M13.5 8.5h6"/></svg>)");
                break;
            default:
                drawSvgIcon(R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><rect x="5" y="5" width="14" height="14" rx="2"/><path d="M8.5 10h7"/><path d="M8.5 14h7"/></svg>)");
                break;
        }
    };

    auto drawRow = [&](BrowserNavAction action, const std::string& label, int count = -1, std::string path = {}) {
        NUIRect row(layout.navPane.x + 5.0f, y, std::max(0.0f, layout.navPane.width - 10.0f), BROWSER_NAV_ROW_H);
        const bool selected =
            activeNavAction_ == action && (action != BrowserNavAction::CustomPlace || activeNavPath_ == path);
        if (selected) {
            renderer.fillRoundedRect(row, themeProps.radiusS, selectedBg);
            renderer.fillRoundedRect({row.x, row.y + 4.0f, 2.0f, row.height - 8.0f}, 1.0f,
                                     themeManager.getColor("accentPrimary").withAlpha(0.92f));
        }
        // Nav hover wash also lives in renderHoverOverlays(), outside the cache.
        drawIcon(row, action, selected);
        if (!compact) {
            renderer.drawText(label, {row.x + 33.0f, std::round(renderer.calculateTextY(row, themeProps.fontSizeS))},
                              themeProps.fontSizeS,
                              selected ? themeManager.getColor("textPrimary").withAlpha(0.90f) : rowText);
        }
        if (!compact && count > 0) {
            const std::string countText = std::to_string(count);
            const auto countSize = renderer.measureText(countText, themeManager.getFontSize("s"));
            renderer.drawText(countText,
                              {row.right() - countSize.width - 8.0f, std::round(renderer.calculateTextY(row, 11.0f))},
                              11.0f,
                              muted.withAlpha(selected ? 0.82f : 0.54f));
        }
        navHits_.push_back({action, row, label, std::move(path)});
        y += BROWSER_NAV_ROW_H;
        ++hitIndex;
    };

    auto drawDivider = [&]() {
        y += compact ? 3.0f : 5.0f;
        renderer.drawLine({layout.navPane.x + 14.0f, y},
                          {layout.navPane.right() - 14.0f, y},
                          1.0f, divider.withAlpha(0.45f));
        y += compact ? 4.0f : 8.0f;
    };

    drawSection("Collections");
    y += 2.0f; // small gap so first nav row aligns with first file row
    drawRow(BrowserNavAction::Favorites, "Favorites", static_cast<int>(favoritesPaths_.size()));
    drawRow(BrowserNavAction::Purple, "Purple", collectionCount("Purple"));
    drawRow(BrowserNavAction::CollectionDrums, "Drums", collectionCount("Drums"));
    drawRow(BrowserNavAction::CollectionInstruments, "Instruments", collectionCount("Instruments"));
    drawRow(BrowserNavAction::Vocals, "Vocals", collectionCount("Vocals"));
    drawDivider();
    drawSection("Categories");
    drawRow(BrowserNavAction::Sounds, "Sounds");
    drawRow(BrowserNavAction::Drums, "Drums");
    drawRow(BrowserNavAction::Instruments, "Instruments");
    drawRow(BrowserNavAction::AudioEffects, "Effects");
    drawRow(BrowserNavAction::Plugins, "Plugins");
    drawRow(BrowserNavAction::Patterns, "Patterns");
    drawRow(BrowserNavAction::Clips, "Clips");
    drawRow(BrowserNavAction::Samples, "Samples");
    drawDivider();
    drawSection("Places");
    drawRow(BrowserNavAction::Packs, "Packs");
    drawRow(BrowserNavAction::UserLibrary, "User Library");
    drawRow(BrowserNavAction::CurrentProject, "Current Project");
    for (const auto& place : customPlacePaths_) {
        std::filesystem::path p(place);
        std::string label = p.filename().string();
        if (label.empty()) {
            label = place;
        }
        drawRow(BrowserNavAction::CustomPlace, label, -1, place);
    }
    drawRow(BrowserNavAction::AddFolder, "+ Add Folder...");

    // Drag-over visual for Places section
    if (m_isDragOverPlaces) {
        // Find the bounds of the Places section (from Packs to AddFolder)
        float placesTop = 0, placesBottom = 0;
        for (const auto& hit : navHits_) {
            if (hit.action == BrowserNavAction::Packs) placesTop = hit.bounds.y;
            if (hit.action == BrowserNavAction::AddFolder) placesBottom = hit.bounds.bottom();
        }
        if (placesTop > 0 && placesBottom > placesTop) {
            NUIRect placesOverlay = {layout.navPane.x, placesTop, layout.navPane.width, placesBottom - placesTop};
            renderer.fillRect(placesOverlay, themeManager.getColor("dragTarget"));
            renderer.strokeRoundedRect(placesOverlay, themeManager.getRadius("s"), 1.0f,
                                       themeManager.getColor("focusRing"));
        }
    }

    // Measure content for next frame's scroll clamp (y is post-offset screen
    // space; add the offset back to recover the intrinsic content height).
    navContentHeight_ = (y + navScrollOffset_) - navContentTop;

    // Restore the full-pane clip and draw a thin scroll thumb when content
    // overflows the available viewport.
    renderer.setClipRect(layout.navPane);
    const float navOverflow = navContentHeight_ - navViewportHeight_;
    if (navOverflow > 0.5f && navViewportHeight_ > 0.0f) {
        const float sbW = 3.0f;
        const float sbX = layout.navPane.right() - sbW - 2.0f;
        const float thumbH = std::max(24.0f, navViewportHeight_ * (navViewportHeight_ / navContentHeight_));
        const float thumbY = navContentTop + (navScrollOffset_ / navOverflow) * (navViewportHeight_ - thumbH);
        renderer.fillRoundedRect(NUIRect(sbX, thumbY, sbW, thumbH), sbW * 0.5f,
                                 themeManager.getColor("textSecondary").withAlpha(0.30f));
    }

    renderer.clearClipRect();
}

void FileBrowser::renderListHeader(NUIRenderer& renderer, const BrowserLayout& layout) {
    auto& themeManager = NUIThemeManager::getInstance();
    const NUIColor headerBg = themeManager.getColor("backgroundSecondary").darkened(0.03f);
    const NUIColor border = themeManager.getColor("border").withAlpha(0.40f);
    const NUIColor text = themeManager.getColor("textPrimary");
    const NUIColor muted = themeManager.getColor("textSecondary");
    const NUIColor accent = themeManager.getColor("accentPrimary");
    const auto& view = getActiveView();
    renderer.fillRect(layout.listHeader, headerBg);

    const auto& themeProps = themeManager.getCurrentTheme();

    const auto drawChevron = [&](const NUIRect& rect, bool pointsRight, bool enabled) {
        const float cx = rect.x + rect.width * 0.5f;
        const float cy = rect.y + rect.height * 0.5f;
        const float direction = pointsRight ? 1.0f : -1.0f;
        const NUIColor color = muted.withAlpha(enabled ? 0.78f : 0.24f);
        renderer.drawLine({cx - direction * 2.0f, cy - 4.0f}, {cx + direction * 2.0f, cy}, 1.5f, color);
        renderer.drawLine({cx + direction * 2.0f, cy}, {cx - direction * 2.0f, cy + 4.0f}, 1.5f, color);
    };
    drawChevron(layout.backButton, false, navHistoryIndex_ > 0);
    drawChevron(layout.forwardButton, true,
                navHistoryIndex_ >= 0 && navHistoryIndex_ < static_cast<int>(navHistory_.size()) - 1);

    const bool canNavigateUp = !currentPath_.empty() &&
                               (rootPath_.empty() || mapKeyForPath(currentPath_) != mapKeyForPath(rootPath_));
    const float upCx = layout.upButton.x + layout.upButton.width * 0.5f;
    const float upCy = layout.upButton.y + layout.upButton.height * 0.5f;
    const NUIColor upColor = muted.withAlpha(canNavigateUp ? 0.78f : 0.24f);
    renderer.drawLine({upCx - 4.0f, upCy + 2.0f}, {upCx, upCy - 2.0f}, 1.5f, upColor);
    renderer.drawLine({upCx, upCy - 2.0f}, {upCx + 4.0f, upCy + 2.0f}, 1.5f, upColor);
    renderer.drawLine({upCx, upCy - 2.0f}, {upCx, upCy + 5.0f}, 1.5f, upColor);

    std::string location = "Library";
    if (!currentPath_.empty()) {
        std::filesystem::path p(currentPath_);
        location = p.filename().string();
        if (location.empty()) location = currentPath_;
    }
    location = ellipsizeMiddle(renderer, location, themeProps.fontSizeS, layout.pathLabel.width);
    renderer.drawText(location,
                      {layout.pathLabel.x, std::round(renderer.calculateTextY(layout.pathLabel, themeProps.fontSizeS))},
                      themeProps.fontSizeS, text.withAlpha(0.88f));

    renderer.drawLine({layout.listHeader.x, layout.listHeader.y + 29.0f},
                      {layout.listHeader.right(), layout.listHeader.y + 29.0f},
                      1.0f, border.withAlpha(0.65f));
    renderer.drawLine({layout.listHeader.x, layout.listHeader.bottom()},
                      {layout.listHeader.right(), layout.listHeader.bottom()},
                      1.0f, border);
    std::string status;
    if (selectedIndices_.size() > 1) {
        status = std::to_string(selectedIndices_.size()) + " selected";
    } else {
        status = std::to_string(view.size()) + (view.size() == 1 ? " item" : " items");
        const std::string filterLabel = getQuickFilterLabel();
        if (filterLabel != "All") status += "  ·  " + filterLabel;
        if (!activeTagFilter_.empty()) status += "  ·  " + activeTagFilter_;
    }
    const NUIRect statusRect(layout.listHeader.x + 10.0f, layout.listHeader.y + 31.0f,
                             std::max(0.0f, layout.filterButton.x - layout.listHeader.x - 14.0f), 23.0f);
    renderer.drawText(ellipsizeEnd(renderer, status, themeProps.fontSizeXS, statusRect.width),
                      {statusRect.x, std::round(renderer.calculateTextY(statusRect, themeProps.fontSizeXS))},
                      themeProps.fontSizeXS, muted.withAlpha(0.66f));

    const bool filterActive = isFilterActive();
    const NUIColor controlColor = filterActive ? accent.withAlpha(0.94f) : muted.withAlpha(0.62f);
    const float filterCx = layout.filterButton.x + layout.filterButton.width * 0.5f;
    const float filterCy = layout.filterButton.y + layout.filterButton.height * 0.5f;
    renderer.drawLine({filterCx - 5.0f, filterCy - 4.0f}, {filterCx + 5.0f, filterCy - 4.0f}, 1.2f, controlColor);
    renderer.drawLine({filterCx - 3.0f, filterCy}, {filterCx + 3.0f, filterCy}, 1.2f, controlColor);
    renderer.drawLine({filterCx - 1.0f, filterCy + 4.0f}, {filterCx + 1.0f, filterCy + 4.0f}, 1.2f, controlColor);
    if (filterActive) renderer.fillCircle({layout.filterButton.right() - 5.0f, layout.filterButton.y + 5.0f}, 2.0f, accent);

    const NUIColor sortColor = muted.withAlpha(0.62f);
    const float sortCx = layout.sortButton.x + layout.sortButton.width * 0.5f;
    const float sortCy = layout.sortButton.y + layout.sortButton.height * 0.5f;
    renderer.drawLine({sortCx - 5.0f, sortCy - 4.0f}, {sortCx + 2.0f, sortCy - 4.0f}, 1.2f, sortColor);
    renderer.drawLine({sortCx - 5.0f, sortCy}, {sortCx, sortCy}, 1.2f, sortColor);
    renderer.drawLine({sortCx - 5.0f, sortCy + 4.0f}, {sortCx - 2.0f, sortCy + 4.0f}, 1.2f, sortColor);
    renderer.drawLine({sortCx + 5.0f, sortCy - 4.0f}, {sortCx + 5.0f, sortCy + 4.0f}, 1.2f, sortColor);
    const float arrowDirection = sortAscending_ ? -1.0f : 1.0f;
    renderer.drawLine({sortCx + 2.5f, sortCy + arrowDirection * 1.5f},
                      {sortCx + 5.0f, sortCy + arrowDirection * 4.0f}, 1.2f, sortColor);
    renderer.drawLine({sortCx + 7.5f, sortCy + arrowDirection * 1.5f},
                      {sortCx + 5.0f, sortCy + arrowDirection * 4.0f}, 1.2f, sortColor);
}

void FileBrowser::renderStaticContent(NUIRenderer& renderer, const NUIRect& bounds) {
    auto& themeManager = NUIThemeManager::getInstance();

    effectiveWidth_ = bounds.width; // Full width for file list

    const BrowserLayout browserLayout = computeBrowserLayout();
    scrollbarTrackHeight_ = browserLayout.list.height;

    // Keep the search input glued to the panel. onResize only fires on size
    // changes, so a pure move (splitter drag, panel slide) would otherwise
    // leave the input at its old absolute position while the panel renders
    // at the new one. Diff before setting to avoid dirtying every frame.
    if (searchInput_) {
        const NUIRect cur = searchInput_->getBounds();
        const NUIRect& tgt = browserLayout.search;
        if (cur.x != tgt.x || cur.y != tgt.y || cur.width != tgt.width || cur.height != tgt.height) {
            searchInput_->setBounds(tgt);
        }
    }

    NUIRect fileBrowserBounds(bounds.x, bounds.y, bounds.width, bounds.height);

    renderer.fillRect(fileBrowserBounds, themeManager.getColor("backgroundPrimary"));
    renderer.fillRect(browserLayout.searchBar, themeManager.getColor("backgroundSecondary").darkened(0.02f));
    renderer.drawLine({browserLayout.searchBar.x, browserLayout.searchBar.bottom() - 1.0f},
                      {browserLayout.searchBar.right(), browserLayout.searchBar.bottom() - 1.0f},
                      1.0f, themeManager.getColor("border").withAlpha(0.52f));

    const auto& themeProps = themeManager.getCurrentTheme();
    const float cornerRadius = themeProps.radiusM;

    NUIRect topClip = fileBrowserBounds;
    topClip.height -= cornerRadius;
    renderer.setClipRect(topClip);
    renderer.strokeRoundedRect(fileBrowserBounds, cornerRadius, 1.0f, borderColor_);
    renderer.clearClipRect();

    NUIRect bottomClip = fileBrowserBounds;
    bottomClip.y = fileBrowserBounds.bottom() - cornerRadius;
    bottomClip.height = cornerRadius;
    renderer.setClipRect(bottomClip);

    renderer.drawLine(NUIPoint(fileBrowserBounds.x, bottomClip.y), NUIPoint(fileBrowserBounds.x, bottomClip.bottom()), 1.0f, borderColor_);
    renderer.drawLine(NUIPoint(fileBrowserBounds.right(), bottomClip.y), NUIPoint(fileBrowserBounds.right(), bottomClip.bottom()), 1.0f, borderColor_);

    renderer.clearClipRect();

    renderNavigationPane(renderer, browserLayout);
    renderListHeader(renderer, browserLayout);
    renderFileList(renderer);
    renderScrollbar(renderer);
    if (isFocused() && (!searchInput_ || !searchInput_->isFocused())) {
        renderer.strokeRoundedRect(browserLayout.list, 2.0f, 1.0f,
                                   themeManager.getColor("accentPrimary").withAlpha(0.28f));
    }
}

void FileBrowser::renderHoverOverlays(NUIRenderer& renderer) {
    auto& themeManager = NUIThemeManager::getInstance();
    const auto& themeProps = themeManager.getCurrentTheme();

    // File-list hover wash (parity with the old in-cache visual: skipped when
    // the row is selected; translucent, so drawing it over the cached text is
    // visually equivalent to the old under-text fill at this alpha).
    if (hoveredIndex_ >= 0 && hoveredIndex_ != selectedIndex_) {
        const auto& view = getActiveView();
        if (hoveredIndex_ < static_cast<int>(view.size())) {
            const BrowserLayout browserLayout = computeBrowserLayout();
            NUIRect listClip = browserLayout.list;
            const float scrollbarGutter = scrollbarVisible_ ? scrollbarWidth_ + 4.0f : 0.0f;
            listClip.width = std::max(0.0f, listClip.width - scrollbarGutter);
            const float itemY = listClip.y + (hoveredIndex_ * BROWSER_LIST_ROW_H) - scrollOffset_;
            const NUIRect itemRect(listClip.x, itemY, listClip.width, BROWSER_LIST_ROW_H);
            if (itemRect.bottom() > listClip.y && itemRect.y < listClip.bottom()) {
                renderer.setClipRect(listClip);
                renderer.fillRect(itemRect, NUIColor::white().withAlpha(0.045f));
                renderer.clearClipRect();
            }
        }
    }

    // Nav-rail hover wash (skipped when the row is the active selection).
    if (hoveredNavIndex_ >= 0 && hoveredNavIndex_ < static_cast<int>(navHits_.size())) {
        const BrowserNavHit& hit = navHits_[hoveredNavIndex_];
        const bool selected = activeNavAction_ == hit.action &&
                              (hit.action != BrowserNavAction::CustomPlace || activeNavPath_ == hit.path);
        if (!selected) {
            renderer.fillRoundedRect(hit.bounds, themeProps.radiusS, NUIColor::white().withAlpha(0.065f));
        }
    }

    const BrowserLayout layout = computeBrowserLayout();
    NUIRect chromeHover;
    switch (hoveredChromeAction_) {
        case ChromeAction::Back: chromeHover = layout.backButton; break;
        case ChromeAction::Forward: chromeHover = layout.forwardButton; break;
        case ChromeAction::Up: chromeHover = layout.upButton; break;
        case ChromeAction::Filter: chromeHover = layout.filterButton; break;
        case ChromeAction::Sort: chromeHover = layout.sortButton; break;
        case ChromeAction::SearchAction: chromeHover = layout.searchActionButton; break;
        case ChromeAction::None: break;
    }
    if (!chromeHover.isEmpty()) {
        renderer.fillRoundedRect(chromeHover, themeProps.radiusS, NUIColor::white().withAlpha(0.07f));
    }
}

void FileBrowser::onRender(NUIRenderer& renderer) {
    AESTRA_ZONE("FileBrowser_Render");

    if (!isVisible()) return;

    NUIRect bounds = getBounds();
    if (bounds.isEmpty()) return;

    // Specialized views (Plugins, Patterns, future browser modes) share this
    // geometry even when the File Browser's static FBO cache is reused.
    updateContentViews();

    // FBO Caching Logic
    auto* renderCache = renderer.getRenderCache();
    if (!renderCache || !renderCache->isEnabled()) {
        // Fallback: Immediate render
        renderStaticContent(renderer, bounds);
        renderHoverOverlays(renderer);
        // Clip children to prevent search bar spillover during resize
        renderer.setClipRect(bounds);
        renderChildren(renderer);
        renderer.clearClipRect();
        return;
    }

    // Cache size matches the component bounds
    AestraUI::NUISize cacheSize(static_cast<int>(bounds.width), static_cast<int>(bounds.height));

    // Get/Create Cache
    // We use a shared_ptr<void> member to hold the cache reference if NUI supports it,
    // or just look it up. TrackManagerUI uses getOrCreateCache returning SharedRenderCache.
    // Assuming getOrCreateCache returns a shared_ptr we can store (or ignore if internal).
    // Let's rely on m_cacheId lookup for now as TrackManagerUI did.
    auto* cache = renderCache->getOrCreateCache(m_cacheId, cacheSize);
    m_cachedRender = cache;

    // Invalidate if requested
    if (m_cacheInvalidated && cache) {
        renderCache->invalidate(m_cacheId);
        m_cacheInvalidated = false;
    }

    // Render Cache
    if (cache) {
        renderCache->renderCachedOrUpdate(cache, bounds, [&]() {
            m_isRenderingToCache = true;

            // Clear FBO with background color BEFORE any transforms to ensure full coverage
            renderer.clear(backgroundColor_);

            // FBO is 0,0 based, so we must push a transform to negated bounds
            renderer.pushTransform(-bounds.x, -bounds.y);

            // Render content
            renderStaticContent(renderer, bounds);

            renderer.popTransform();
            m_isRenderingToCache = false;
        });
    } else {
        renderStaticContent(renderer, bounds);
    }

    // Hover washes render every frame on top of the cached content — this is
    // what lets hover changes skip cache rebuilds entirely.
    renderHoverOverlays(renderer);

    // Render interactive children (Search Input, Popup Menus) ON TOP of the cache
    // These handle their own dirtiness and shouldn't trigger full cache rebuilds
    // Clip children to prevent search bar spillover during resize
    renderer.setClipRect(bounds);
    renderChildren(renderer);
    renderer.clearClipRect();

    if (searchInput_ && searchInput_->isVisible()) {
        auto& themeManager = NUIThemeManager::getInstance();
        const BrowserLayout layout = computeBrowserLayout();
        const NUIRect search = layout.searchBar;
        const NUIColor iconColor = themeManager.getColor("textSecondary").withAlpha(0.58f);
        const float cx = search.x + 15.0f;
        const float cy = search.y + search.height * 0.5f;
        renderer.strokeCircle({cx, cy - 1.0f}, 4.6f, 1.3f, iconColor);
        renderer.drawLine({cx + 3.7f, cy + 2.7f}, {cx + 7.2f, cy + 6.2f}, 1.3f, iconColor);

        const float fx = layout.searchActionButton.x + layout.searchActionButton.width * 0.5f;
        if (!searchInput_->getText().empty()) {
            renderer.drawLine({fx - 4.0f, cy - 4.0f}, {fx + 4.0f, cy + 4.0f}, 1.3f, iconColor);
            renderer.drawLine({fx + 4.0f, cy - 4.0f}, {fx - 4.0f, cy + 4.0f}, 1.3f, iconColor);
        } else {
            const float lineStartY = cy - 5.0f;
            for (int i = 0; i < 3; ++i) {
                const float y = lineStartY + static_cast<float>(i) * 5.0f;
                renderer.drawLine({fx - 6.0f, y}, {fx + 6.0f, y}, 1.1f, iconColor.withAlpha(0.78f));
                const float knobX = fx + (i == 1 ? -2.5f : 3.0f);
                renderer.fillCircle({knobX, y}, 1.7f, iconColor);
            }
            if (isFilterActive()) {
                renderer.fillCircle({layout.searchActionButton.right() - 4.0f,
                                     layout.searchActionButton.y + 4.0f}, 2.0f,
                                    themeManager.getColor("accentPrimary"));
            }
        }
    }

    // Loading spinner removed - now handled by FilePreviewPanel
}

void FileBrowser::onUpdate(double deltaTime) {
	    NUIComponent::onUpdate(deltaTime);

    // Apply any completed async directory scans (keeps UI responsive on huge folders).
    processScanResults();

    // One-shot recovery for boot races where the initial scan result never lands.
    if (!scanningRoot_ &&
        !bootScanRecoveryAttempted_ &&
        rootItems_.empty() &&
        !currentPath_.empty() &&
        std::filesystem::exists(currentPath_)) {
        bootScanRecoveryAttempted_ = true;
        loadDirectoryContents();
    }

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
        invalidateCache();
    } else {
        scrollOffset_ = targetScrollOffset_;
    }
    scrollVelocity_ = scrollDelta;

    // ALWAYS repaint if scroll position changed at all
    if (std::abs(scrollOffset_ - lastRenderedOffset_) > 0.01f) {
        lastRenderedOffset_ = scrollOffset_;
        invalidateCache();
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
                    invalidateCache();
	            }
	        }
	    }

    // Search caret blink handled by NUITextInput now
}
}

void FileBrowser::onResize(int width, int height) {
    NUIComponent::onResize(width, height);

    auto& themeManager = NUIThemeManager::getInstance();
    const BrowserLayout browserLayout = computeBrowserLayout();

    if (searchInput_) {
        searchInput_->setBounds(browserLayout.search);

        searchInput_->setTextColor(textColor_);
        searchInput_->setBackgroundColor(themeManager.getColor("backgroundSecondary").darkened(0.02f));
        searchInput_->setBorderColor(themeManager.getColor("border"));
        searchInput_->setFocusedBorderColor(themeManager.getColor("focusRing"));
        searchInput_->setPlaceholderColor(themeManager.getColor("textSecondary").withAlpha(0.56f));
        searchInput_->setPadding(6.0f);
        searchInput_->setBorderRadius(5.0f);
        searchInput_->setBorderWidth(1.0f);
    }

    itemHeight_ = BROWSER_LIST_ROW_H;
    float listHeight = browserLayout.list.height;

    visibleItems_ = static_cast<int>(listHeight / itemHeight_);
    visibleItems_ = std::max(1, visibleItems_);

    // Update scrollbar dimensions
    float scrollbarWidth = themeManager.getComponentDimension("fileBrowser", "scrollbarWidth");
    scrollbarTrackHeight_ = std::max(0.0f, listHeight);
    scrollbarWidth_ = std::clamp(scrollbarWidth, 4.0f, 6.0f);

    // Update caches
    updateScrollPosition();
    updateBreadcrumbs();
    updateScrollbarVisibility();
    invalidateAllItemCaches(); // Force text re-layout on resize
    invalidateCache();
    updateContentViews();
}

void FileBrowser::invalidateAllItemCaches() {
    std::vector<AestraUI::FileItem*> stack;
    for (auto& item : rootItems_) {
        stack.push_back(&item);
    }

    while (!stack.empty()) {
        AestraUI::FileItem* item = stack.back();
        stack.pop_back();

        item->cacheValid = false;
        item->cachedDisplayName.clear();
        item->cachedSizeStr.clear();

        for (auto& child : item->children) {
            stack.push_back(&child);
        }
    }
}

// =============================================================================
// SECTION: Event Handling
// =============================================================================

bool FileBrowser::onMouseEvent(const NUIMouseEvent& event) {
    lastMousePos_ = event.position;
    NUIRect bounds = getBounds();
    const auto& view = getActiveView();
    auto& themeManager = NUIThemeManager::getInstance();
	    float itemHeight = BROWSER_LIST_ROW_H;

    const BrowserLayout browserLayout = computeBrowserLayout();
    float listY = browserLayout.list.y;
    float listHeight = browserLayout.list.height;

    // Update scrollbar track height ensuring it is fresh for this event
    scrollbarTrackHeight_ = listHeight;

    // If a click happens outside the search input, drop focus so shortcuts/navigation work normally.
    if (searchInput_ && event.pressed && event.button == NUIMouseButton::Left) {
        if (searchInput_->isFocused() && !searchInput_->getBounds().contains(event.position)) {
            searchInput_->setFocused(false);
        }
    }

    ChromeAction newChromeAction = ChromeAction::None;
    if (!event.cursorCaptured) {
        if (browserLayout.backButton.contains(event.position)) newChromeAction = ChromeAction::Back;
        else if (browserLayout.forwardButton.contains(event.position)) newChromeAction = ChromeAction::Forward;
        else if (browserLayout.upButton.contains(event.position)) newChromeAction = ChromeAction::Up;
        else if (browserLayout.filterButton.contains(event.position)) newChromeAction = ChromeAction::Filter;
        else if (browserLayout.sortButton.contains(event.position)) newChromeAction = ChromeAction::Sort;
        else if (browserLayout.searchActionButton.contains(event.position)) newChromeAction = ChromeAction::SearchAction;
    }
    if (newChromeAction != hoveredChromeAction_) {
        hoveredChromeAction_ = newChromeAction;
        setDirty(true);
    }

    if (event.pressed && event.button == NUIMouseButton::Left && newChromeAction != ChromeAction::None) {
        switch (newChromeAction) {
            case ChromeAction::Back: navigateBack(); break;
            case ChromeAction::Forward: navigateForward(); break;
            case ChromeAction::Up: navigateUp(); break;
            case ChromeAction::Filter: showQuickFilterMenu(); break;
            case ChromeAction::Sort: showSortMenu(); break;
            case ChromeAction::SearchAction:
                if (searchInput_ && !searchInput_->getText().empty()) searchInput_->clear();
                else showQuickFilterMenu();
                break;
            case ChromeAction::None: break;
        }
        return true;
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
        dragManager.updateDrag(event.position);

        // Track drag-over Places section
        bool overPlaces = isPointOverPlacesSection(event.position.x, event.position.y);
        if (overPlaces != m_isDragOverPlaces) {
            m_isDragOverPlaces = overPlaces;
            invalidateCache();
        }

        if (!event.pressed && event.button == NUIMouseButton::Left) {
            // Check if dropped on Places section
            if (m_isDragOverPlaces && dragManager.getDragData().type == AestraUI::DragDataType::File) {
                onDropFileToPlaces(dragManager.getDragData().filePath);
            }
            m_isDragOverPlaces = false;
            dragManager.endDrag(event.position);
            dragPotential_ = false;
            isDraggingFile_ = false;
            dragSourceIndex_ = -1;
            return true;
        }
        return true;  // Consume all events while dragging
    } else {
        // Clear drag-over state when no global drag
        if (m_isDragOverPlaces) {
            m_isDragOverPlaces = false;
            invalidateCache();
        }
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
	            const FileItem* dragFile = view[dragSourceIndex_];

	            if (!dragFile->isDirectory && FileFilter::isAllowed(dragFile->path)) {
	                AestraUI::DragData dragData;
	                if (dragFile->type == FileType::MidiFile) {
	                    dragData.type = AestraUI::DragDataType::MidiClip;
	                } else {
	                    dragData.type = AestraUI::DragDataType::File;
	                }
	                dragData.filePath = dragFile->path;
	                dragData.displayName = dragFile->name;
	                dragData.accentColor = NUIThemeManager::getInstance().getColor("accentPrimary");
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

    if (handleNavigationMouseEvent(event, browserLayout)) {
        return true;
    }

    // === NAV PANE WHEEL (independent of the file list) ===
    // Scroll the collections/categories/places column when it overflows.
    if (event.wheelDelta != 0 && browserLayout.navPane.contains(event.position)) {
        const float navOverflow = std::max(0.0f, navContentHeight_ - navViewportHeight_);
        if (navOverflow > 0.0f) {
            navScrollOffset_ = std::clamp(navScrollOffset_ - event.wheelDelta * 3.0f * BROWSER_NAV_ROW_H,
                                          0.0f, navOverflow);
            invalidateCache();
            return true;
        }
    }

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

        invalidateCache();
        return true;  // Consume the wheel event
    }

    // Clear hover if mouse leaves the file browser entirely (but allow scrollbar dragging)
    if (!mouseInside && !isDraggingScrollbar_) {
        bool dirty = false;
        if (hoveredIndex_ != -1) {
            hoveredIndex_ = -1;
            dirty = true;
        }
        if (dirty)
            setDirty(true); // hover overlay only — no cache rebuild
        return false;
    }

    // Handle scrollbar mouse events first - check if scrollbar should be visible
        const float contentHeight = view.size() * itemHeight;
        const float maxScroll = std::max(0.0f, contentHeight - scrollbarTrackHeight_);
        const bool needsScrollbar = maxScroll > 0.0f;
	    const float scrollbarGutter = scrollbarWidth_ + themeManager.getSpacing("xs");
	    const float listX = browserLayout.list.x;
	    const float listW = std::max(0.0f, browserLayout.list.width - scrollbarGutter);

    // Wheel handling moved earlier in function (before bounds check)

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
            invalidateCache();
        }
    } else if (hoveredBreadcrumbIndex_ != -1) {
        hoveredBreadcrumbIndex_ = -1;
        invalidateCache();
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
        bool isInsideList = (event.position.x >= listX && event.position.x <= listX + listW &&
                             event.position.y >= listY && event.position.y <= listY + listHeight);

        if (!event.cursorCaptured && !isInsideList) {
            // Outside list area - clear hover and tooltip
            if (hoveredIndex_ != -1) {
                hoveredIndex_ = -1;
                setDirty(true); // hover overlay only — no cache rebuild
            }
            if (!browserLayout.navPane.contains(event.position)) {
                AestraUI::NUIComponent::hideRemoteTooltip(this);
            }
        }

        if (!event.cursorCaptured && isInsideList) {


        // Calculate which item is being hovered
        float relativeY = event.position.y - listY;
        int itemIndex = static_cast<int>((relativeY + scrollOffset_) / itemHeight);

	        // Update hover state
	        int newHoveredIndex = (itemIndex >= 0 && itemIndex < static_cast<int>(view.size())) ? itemIndex : -1;
	        if (newHoveredIndex != hoveredIndex_) {
	            hoveredIndex_ = newHoveredIndex;

                // Tooltip Logic for Truncated Items
                if (hoveredIndex_ >= 0 && hoveredIndex_ < static_cast<int>(view.size())) {
                    const FileItem* item = view[hoveredIndex_];
                    if (item && item->isTruncated) {
                        // Position tooltip at the mouse or right of the text
                        // For simply following mouse:
                        NUIPoint tooltipPos = event.position;
                        tooltipPos.x += 16.0f; // Offset
                        tooltipPos.y += 16.0f;

                        NUIComponent::showRemoteTooltip(item->name, tooltipPos, this);
                    } else {
                        NUIComponent::hideRemoteTooltip(this);
                    }
                } else {
                    NUIComponent::hideRemoteTooltip(this);
                }

                setDirty(true); // hover overlay only — no cache rebuild
            }

            // Keep tooltip alive while hovering a *truncated* list item (not only on
            // hover-change events). Fully-legible names need no tooltip — this
            // keep-alive path used to show one unconditionally, overriding the
            // isTruncated check above and tooltipping every item.
            if (hoveredIndex_ >= 0 && hoveredIndex_ < static_cast<int>(view.size())) {
                const FileItem* item = view[hoveredIndex_];
                if (item && item->isTruncated) {
                    AestraUI::NUIComponent::showRemoteTooltip(item->name, event.position, this);
                }
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
                hidePopupMenu();
                return true;
	        }

	        if (event.pressed && event.button == NUIMouseButton::Left) {
	            // Request focus when clicking the list
                if (!isFocused()) setFocused(true);

	            // Calculate which file was clicked
	            float relativeY = event.position.y - listY;
	            int itemIndex = static_cast<int>((relativeY + scrollOffset_) / itemHeight);

	            if (itemIndex >= 0 && itemIndex < static_cast<int>(view.size())) {
	                const FileItem* clickedFile = view[itemIndex];
	                if (!clickedFile || clickedFile->isPlaceholder) {
	                    return true;
	                }

                    if (popupMenu_ && popupMenu_->isVisible() && popupMenuTargetPath_ == clickedFile->path) {
                        hidePopupMenu();
                        return true;
                    }

		                // Check for expander click (match renderFileList layout)
		                if (clickedFile->isDirectory) {
		                    const float indentStep = 18.0f;
			                    const float indent = std::min(static_cast<float>(clickedFile->depth) * indentStep, 68.0f);
		                    const float contentX = listX + 12.0f + indent;
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

                invalidateCache();
                return true;
            }
        }
    }

    // If we reached here, and the click was inside the list area but on no item,
    // we MUST consume it to prevent focus from resetting to Root.
    if (isInsideList && event.pressed &&
        (event.button == NUIMouseButton::Left || event.button == NUIMouseButton::Right)) {
        return true;
    }

    if (mouseInside && event.pressed && event.button == NUIMouseButton::Right) {
        hidePopupMenu();
        return true;
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
        if (event.modifiers & NUIModifiers::Alt) {
            if (event.keyCode == NUIKeyCode::Left) { navigateBack(); return true; }
            if (event.keyCode == NUIKeyCode::Right) { navigateForward(); return true; }
            if (event.keyCode == NUIKeyCode::Up) { navigateUp(); return true; }
        }

        // Quick filter shortcuts (Ctrl+1..4)
        if (event.modifiers & NUIModifiers::Ctrl) {
            if (event.keyCode == NUIKeyCode::Num0) { clearActiveFilters(); return true; }
            if (event.keyCode == NUIKeyCode::Num1) { activeQuickFilter_ = QuickFilter::All; applyFilter(); return true; }
            if (event.keyCode == NUIKeyCode::Num2) { activeQuickFilter_ = QuickFilter::Audio; applyFilter(); return true; }
            if (event.keyCode == NUIKeyCode::Num3) { activeQuickFilter_ = QuickFilter::Projects; applyFilter(); return true; }
            if (event.keyCode == NUIKeyCode::Num4) { activeQuickFilter_ = QuickFilter::Folders; applyFilter(); return true; }
            if (event.keyCode == NUIKeyCode::A) {
                const auto& activeView = getActiveView();
                selectedIndices_.clear();
                selectedIndices_.reserve(activeView.size());
                for (int i = 0; i < static_cast<int>(activeView.size()); ++i) {
                    if (activeView[i] && !activeView[i]->isPlaceholder) selectedIndices_.push_back(i);
                }
                if (!selectedIndices_.empty()) {
                    selectedIndex_ = selectedIndices_.back();
                    selectedFile_ = activeView[selectedIndex_];
                    lastShiftSelectIndex_ = selectedIndices_.front();
                    updateScrollPosition();
                }
                invalidateCache();
                return true;
            }
        }

        // Ctrl+F -> Focus Search
        if (event.keyCode == NUIKeyCode::F && (event.modifiers & NUIModifiers::Ctrl)) {
            if (searchInput_) {
                searchInput_->setFocused(true);
                return true;
            }
        }

        // Esc -> Clear search or blur
        if (event.keyCode == NUIKeyCode::Escape) {
            if (isFilterActive()) {
                clearActiveFilters();
                return true;
            }
        }

        if (event.keyCode == NUIKeyCode::F5) {
            refresh();
            return true;
        }

        if (!(event.modifiers & NUIModifiers::Ctrl) &&
            !(event.modifiers & NUIModifiers::Alt) &&
            !(event.modifiers & NUIModifiers::Super)) {
            char c = event.character;
            if (c == 0) {
                if (event.keyCode >= NUIKeyCode::A && event.keyCode <= NUIKeyCode::Z) {
                    c = 'a' + (static_cast<int>(event.keyCode) - static_cast<int>(NUIKeyCode::A));
                    const bool shift = event.modifiers & NUIModifiers::Shift;
                    const bool caps = event.modifiers & NUIModifiers::CapsLock;
                    if (shift != caps) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                } else if (event.keyCode >= NUIKeyCode::Num0 && event.keyCode <= NUIKeyCode::Num9) {
                    c = '0' + (static_cast<int>(event.keyCode) - static_cast<int>(NUIKeyCode::Num0));
                }
            }
            if (c >= 32 && c <= 126 && searchInput_) {
                searchInput_->setFocused(true);
                NUIKeyEvent resolvedEvent = event;
                resolvedEvent.character = c;
                searchInput_->onKeyEvent(resolvedEvent);
                return true;
            }
        }
    }

    // Handle navigation/activation on key-down only.
    if (!event.pressed) {
        switch (event.keyCode) {
            case NUIKeyCode::Up:
            case NUIKeyCode::Down:
            case NUIKeyCode::Left:
            case NUIKeyCode::Right:
            case NUIKeyCode::Enter:
            case NUIKeyCode::Backspace:
            case NUIKeyCode::Home:
            case NUIKeyCode::End:
            case NUIKeyCode::PageUp:
            case NUIKeyCode::PageDown:
                return true; // consume
            default: return false;
        }
    }

    const auto& view = getActiveView();

    const auto selectFromKeyboard = [&](int index, bool extendRange) {
        if (view.empty()) return false;
        index = std::clamp(index, 0, static_cast<int>(view.size()) - 1);
        if (!view[index] || view[index]->isPlaceholder) return false;
        toggleFileSelection(index, false, extendRange);
        selectedFile_ = view[index];
        updateScrollPosition();
        if (onFileSelected_) onFileSelected_(*selectedFile_);
        tryAutoPreview();
        invalidateCache();
        return true;
    };

    switch (event.keyCode) {
        case NUIKeyCode::Up:
            if (selectFromKeyboard(selectedIndex_ < 0 ? 0 : selectedIndex_ - 1,
                                   event.modifiers & NUIModifiers::Shift)) return true;
            break;

        case NUIKeyCode::Down:
            if (selectFromKeyboard(selectedIndex_ < 0 ? 0 : selectedIndex_ + 1,
                                   event.modifiers & NUIModifiers::Shift)) return true;
            break;

        case NUIKeyCode::Home:
            return selectFromKeyboard(0, event.modifiers & NUIModifiers::Shift);
        case NUIKeyCode::End:
            return selectFromKeyboard(static_cast<int>(view.size()) - 1, event.modifiers & NUIModifiers::Shift);
        case NUIKeyCode::PageUp:
            return selectFromKeyboard(selectedIndex_ - std::max(1, visibleItems_ - 1),
                                      event.modifiers & NUIModifiers::Shift);
        case NUIKeyCode::PageDown:
            return selectFromKeyboard(selectedIndex_ + std::max(1, visibleItems_ - 1),
                                      event.modifiers & NUIModifiers::Shift);

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
                } else {
                    navigateUp();
                }
                return true;
            }
            navigateUp();
            return true;

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

        case NUIKeyCode::Space: {
            // Toggle preview play/pause — consume to prevent transport toggle
            if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(view.size())) {
                const FileItem* item = view[selectedIndex_];
                if (item && !item->isDirectory) {
                    FileType t = item->type;
                    if (t == FileType::AudioFile || t == FileType::MusicFile ||
                        t == FileType::WavFile || t == FileType::Mp3File ||
                        t == FileType::FlacFile) {
                        if (onSoundPreview_) {
                            onSoundPreview_(*item);
                        }
                    }
                }
            }
            return true;
        }
    }

    return false;
}

void FileBrowser::onMouseLeave() {
    if (hoveredIndex_ >= 0 || hoveredNavIndex_ >= 0 || hoveredChromeAction_ != ChromeAction::None) {
        hoveredIndex_ = -1;
        hoveredNavIndex_ = -1;
        hoveredChromeAction_ = ChromeAction::None;
        setDirty(true); // hover overlay only — no cache rebuild
    }
    NUIComponent::hideRemoteTooltip(this);
    NUIComponent::onMouseLeave();
}

void FileBrowser::tryAutoPreview() {
    const auto& view = getActiveView();
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(view.size())) return;
    const FileItem* item = view[selectedIndex_];
    if (!item || item->isDirectory) return;
    FileType t = item->type;
    if (t == FileType::AudioFile || t == FileType::MusicFile ||
        t == FileType::WavFile || t == FileType::Mp3File || t == FileType::FlacFile) {
        if (onSoundPreview_) onSoundPreview_(*item);
    }
}

void FileBrowser::scrollToSelected() {
    const auto& view = getActiveView();
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(view.size())) return;
    float contentHeight = view.size() * itemHeight_;
    float maxScroll = std::max(0.0f, contentHeight - scrollbarTrackHeight_);
    float itemTop = selectedIndex_ * itemHeight_;
    float itemBottom = itemTop + itemHeight_;

    if (itemTop < scrollOffset_) {
        targetScrollOffset_ = itemTop;
    } else if (itemBottom > scrollOffset_ + scrollbarTrackHeight_) {
        targetScrollOffset_ = itemBottom - scrollbarTrackHeight_;
    }
    targetScrollOffset_ = std::clamp(targetScrollOffset_, 0.0f, maxScroll);
}

bool FileBrowser::isPointOverPlacesSection(float x, float y) const {
    // Check if point falls within the Places section of the nav pane
    for (const auto& hit : navHits_) {
        if (hit.action == BrowserNavAction::Packs ||
            hit.action == BrowserNavAction::UserLibrary ||
            hit.action == BrowserNavAction::CurrentProject ||
            hit.action == BrowserNavAction::CustomPlace ||
            hit.action == BrowserNavAction::AddFolder) {
            if (hit.bounds.contains(x, y)) return true;
        }
    }
    return false;
}

void FileBrowser::onDropFileToPlaces(const std::string& path) {
    if (path.empty()) return;

    std::error_code ec;
    if (!std::filesystem::is_directory(path, ec)) return;

    const std::string key = canonicalOrNormalized(std::filesystem::path(path)).string();
    if (key.empty()) return;
    if (std::find(customPlacePaths_.begin(), customPlacePaths_.end(), key) != customPlacePaths_.end()) return;

    customPlacePaths_.push_back(key);
    saveState(getSettingsPath());
    invalidateCache();
}
void FileBrowser::setCurrentPath(const std::string& path) {
    const std::string targetPath = resolveExistingDirectoryPath(path, rootPath_);
    if (targetPath.empty()) {
        currentPath_.clear();
        rootItems_.clear();
        displayItems_.clear();
        filteredFiles_.clear();
        selectedFile_ = nullptr;
        selectedIndex_ = -1;
        selectedIndices_.clear();
        hoveredIndex_ = -1;
        viewDirty_ = true;
        invalidateCache();
        return;
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
    invalidateCache();
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

void FileBrowser::clearActiveFilters() {
    activeQuickFilter_ = QuickFilter::All;
    activeTagFilter_.clear();
    if (searchInput_ && !searchInput_->getText().empty()) searchInput_->clear();
    else applyFilter();
}

std::string FileBrowser::getQuickFilterLabel() const {
    switch (activeQuickFilter_) {
        case QuickFilter::All: return "All";
        case QuickFilter::Audio: return "Audio";
        case QuickFilter::Projects: return "Projects";
        case QuickFilter::Folders: return "Folders";
    }
    return "All";
}

void FileBrowser::refresh() {
    pendingSelectionPath_.clear();
    if (selectedFile_) {
        pendingSelectionPath_ = selectedFile_->path;
    }

    hidePopupMenu();
    popupMenuTargetPath_.clear();
    popupMenuTargetIsDirectory_ = false;
    hoveredIndex_ = -1;
    hoveredBreadcrumbIndex_ = -1;

    const std::string resolvedPath = resolveExistingDirectoryPath(currentPath_, rootPath_);
    if (resolvedPath.empty()) {
        return;
    }
    if (resolvedPath != currentPath_) {
        currentPath_ = resolvedPath;
        if (!isNavigatingHistory_ && (navHistory_.empty() || navHistory_[navHistoryIndex_] != currentPath_)) {
            pushToHistory(currentPath_);
        }
        updateBreadcrumbs();
        if (onPathChanged_) {
            onPathChanged_(currentPath_);
        }
    }

    loadDirectoryContents();
    invalidateCache();
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
    const std::string targetPath = resolveExistingDirectoryPath(path, rootPath_);
    if (targetPath.empty()) return;
    setCurrentPath(targetPath);
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
            invalidateCache();
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
            invalidateCache();
            return;
        }
    }
}

void FileBrowser::setActivePlaybackPath(const std::string& path) {
    const std::string next = mapKeyForPath(path);
    if (activePlaybackPath_ == next) return;
    activePlaybackPath_ = next;
    invalidateCache();
}

void FileBrowser::openFile(const std::string& path) {
    const std::filesystem::path p(path);
    const std::filesystem::path parent = p.parent_path();
    if (!parent.empty() && parent.string() != currentPath_) {
        setCurrentPath(parent.string());
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
    invalidateCache();
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

    invalidateCache();
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

    invalidateCache();
}

void FileBrowser::loadDirectoryContents() {
    const std::string resolvedPath = resolveExistingDirectoryPath(currentPath_, rootPath_);
    if (!resolvedPath.empty()) {
        currentPath_ = resolvedPath;
    }

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
    scanError_.clear();

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
    invalidateCache();
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
    invalidateCache();
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
	    invalidateCache();
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
    if (extension == ".Aestra" || extension == ".aes" || extension == ".Aestraproj") return FileType::ProjectFile;
    if (extension == ".mid" || extension == ".midi") return FileType::MidiFile;

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
    auto& themeManager = NUIThemeManager::getInstance();
    const BrowserLayout browserLayout = computeBrowserLayout();
    NUIRect listClip = browserLayout.list;
    const float scrollbarGutter = scrollbarVisible_ ? scrollbarWidth_ + 4.0f : 0.0f;
    listClip.width = std::max(0.0f, listClip.width - scrollbarGutter);

    const auto& view = getActiveView();
    renderer.fillRect(browserLayout.list, themeManager.getColor("backgroundPrimary"));

    if (scanningRoot_ && view.empty()) {
        renderer.setClipRect(listClip);
        NUIRect titleRect(listClip.x, listClip.y + listClip.height * 0.43f - 12.0f, listClip.width, 20.0f);
        NUIRect hintRect(listClip.x, titleRect.bottom() + 4.0f, listClip.width, 18.0f);
        renderer.drawTextCentered("Scanning library", titleRect, themeManager.getFontSize("l"),
                                  themeManager.getColor("textPrimary").withAlpha(0.72f));
        renderer.drawTextCentered("Large folders stay responsive while results load", hintRect,
                                  themeManager.getFontSize("s"),
                                  themeManager.getColor("textSecondary").withAlpha(0.56f));
        renderer.clearClipRect();
        return;
    }

    if (view.empty()) {
        renderer.setClipRect(listClip);
        std::string title;
        std::string hint;
        if (!scanError_.empty()) {
            title = "Folder unavailable";
            hint = "Press F5 to retry or choose another location";
        } else if (isFilterActive()) {
            title = "No matches";
            hint = "Press Esc to clear search and filters";
        } else {
            title = "No supported files here";
            hint = "Audio, MIDI, and Aestra projects appear in the browser";
        }
        NUIRect titleRect(listClip.x, listClip.y + listClip.height * 0.42f - 12.0f, listClip.width, 20.0f);
        NUIRect hintRect(listClip.x, titleRect.bottom() + 4.0f, listClip.width, 18.0f);
        renderer.drawTextCentered(title, titleRect, themeManager.getFontSize("l"), themeManager.getColor("textPrimary").withAlpha(0.72f));
        renderer.drawTextCentered(hint, hintRect, themeManager.getFontSize("s"), themeManager.getColor("textSecondary").withAlpha(0.58f));
        renderer.clearClipRect();
        return;
    }

    const float itemHeight = BROWSER_LIST_ROW_H;
    const int firstVisibleIndex = std::max(0, static_cast<int>(scrollOffset_ / itemHeight));
    const int lastVisibleIndex = std::min(static_cast<int>(view.size()),
        static_cast<int>((scrollOffset_ + listClip.height) / itemHeight) + 2);

    renderer.setClipRect(listClip);

    const NUIColor oddRow = themeManager.getColor("backgroundSecondary").withAlpha(0.72f);
    const NUIColor evenRow = themeManager.getColor("backgroundPrimary");
    const NUIColor selectedRow = themeManager.getColor("selection");
    const NUIColor secondarySelectedRow = themeManager.getColor("accentPrimary").withAlpha(0.12f);
    const NUIColor gridLine = themeManager.getColor("gridMinor");
    const NUIColor text = themeManager.getColor("textPrimary").withAlpha(0.82f);
    const NUIColor folderText = themeManager.getColor("textPrimary").withAlpha(0.92f);
    const NUIColor muted = themeManager.getColor("textSecondary").withAlpha(0.56f);
    const auto& themeProps = themeManager.getCurrentTheme();
    const float labelFont = themeProps.fontSizeS;
    const float rowIndentStep = 18.0f;

    for (int i = firstVisibleIndex; i < lastVisibleIndex; ++i) {
        const float itemY = listClip.y + (i * itemHeight) - scrollOffset_;
        if (itemY + itemHeight < listClip.y || itemY > listClip.bottom()) continue;

        NUIRect itemRect(listClip.x, itemY, listClip.width, itemHeight);
        const bool primarySelected = i == selectedIndex_;
        const bool selected = primarySelected ||
                              std::find(selectedIndices_.begin(), selectedIndices_.end(), i) != selectedIndices_.end();
        renderer.fillRect(itemRect, (i % 2 == 0) ? evenRow : oddRow);
        if (selected) {
            renderer.fillRect(itemRect, primarySelected ? selectedRow : secondarySelectedRow);
            renderer.fillRect({itemRect.x, itemRect.y + 3.0f, 2.0f, itemRect.height - 6.0f},
                              themeManager.getColor("accentPrimary").withAlpha(primarySelected ? 0.85f : 0.44f));
        }
        // Hover wash is drawn by renderHoverOverlays() OUTSIDE the FBO cache —
        // hover must never invalidate the cache (rebuilding the whole list per
        // row crossing cost ~11 ms/frame of the mouse-active render budget).
        renderer.drawLine({itemRect.x, itemRect.bottom()}, {itemRect.right(), itemRect.bottom()}, 1.0f, gridLine);

        const FileItem* item = view[i];
        if (!item) continue;
        const bool playbackActive = !activePlaybackPath_.empty() && mapKeyForPath(item->path) == activePlaybackPath_;
        if (playbackActive) {
            renderer.fillRect({itemRect.x, itemRect.y, 3.0f, itemRect.height}, themeManager.getColor("accentPrimary").withAlpha(0.96f));
        }

        const float indent = std::min(static_cast<float>(item->depth) * rowIndentStep, 68.0f);
        float contentX = itemRect.x + 12.0f + indent;

        if (item->isDirectory) {
            // Crisp vector chevrons
            const float cx = contentX + 5.0f;
            const float cy = itemRect.y + itemRect.height * 0.5f;
            const float s = 4.0f;
            const NUIColor chevronColor = muted.withAlpha(0.72f);
            if (item->isExpanded) {
                renderer.drawLine({cx - s, cy - s * 0.5f}, {cx, cy + s * 0.5f}, 1.6f, chevronColor);
                renderer.drawLine({cx, cy + s * 0.5f}, {cx + s, cy - s * 0.5f}, 1.6f, chevronColor);
            } else {
                renderer.drawLine({cx - s * 0.5f, cy - s}, {cx + s * 0.5f, cy}, 1.6f, chevronColor);
                renderer.drawLine({cx + s * 0.5f, cy}, {cx - s * 0.5f, cy + s}, 1.6f, chevronColor);
            }
            contentX += 16.0f;
        } else {
            contentX += 10.0f;
        }

        auto icon = getIconForFileType(item->type);
        if (icon) {
            NUIRect iconRect(contentX, itemRect.y + 5.0f, 14.0f, 14.0f);
            icon->setBounds(iconRect);
            icon->setColor(item->isDirectory ? muted.withAlpha(0.86f) : muted);
            icon->onRender(renderer);
        }
        contentX += 20.0f;

        const float maxTextWidth = std::max(0.0f, itemRect.right() - contentX - 12.0f);
        std::string displayName = item->name;
        if (renderer.measureText(displayName, labelFont).width > maxTextWidth) {
            displayName = ellipsizeEnd(renderer, displayName, labelFont, maxTextWidth);
            item->isTruncated = true;
        } else {
            item->isTruncated = false;
        }

        const NUIColor itemTextColor = item->isPlaceholder
                                           ? muted.withAlpha(0.60f)
                                           : selected ? themeManager.getColor("textPrimary")
                                                      : item->isDirectory ? folderText : text;
        renderer.drawText(displayName, {contentX, std::round(renderer.calculateTextY(itemRect, labelFont))},
                          labelFont, itemTextColor);

        // BPM stays in metadata (search/drag) but is not shown as a row
        // column — owner direction: no number on the right of audio rows.

        // Tag dots (after name)
        if (!item->isDirectory) {
            const std::string key = mapKeyForPath(item->path);
            auto tagIt = tagsByPath_.find(key);
            if (tagIt != tagsByPath_.end() && !tagIt->second.empty()) {
                const float tagDotStartX = contentX + renderer.measureText(displayName, labelFont).width + 8.0f;
                float dotX = tagDotStartX;
                const float rowCenterY = itemRect.y + itemRect.height * 0.5f;
                static const std::unordered_map<std::string, NUIColor> kTagColors = {
                    {"Purple", NUIColor(0.486f, 0.227f, 0.929f, 1.0f)},
                    {"Drums", NUIColor(0.961f, 0.620f, 0.043f, 1.0f)},
                    {"Instruments", NUIColor(0.204f, 0.835f, 0.600f, 1.0f)},
                    {"Vocals", NUIColor(0.957f, 0.447f, 0.714f, 1.0f)},
                    {"Effects", NUIColor(0.376f, 0.647f, 0.980f, 1.0f)},
                    {"Clips", NUIColor(0.984f, 0.741f, 0.141f, 1.0f)},
                };
                for (const auto& tag : tagIt->second) {
                    NUIColor dotColor = NUIColor(0.42f, 0.42f, 0.42f, 1.0f);
                    auto cit = kTagColors.find(tag);
                    if (cit != kTagColors.end()) dotColor = cit->second;
                    renderer.fillRoundedRect({dotX, rowCenterY - 4.0f, 8.0f, 8.0f}, 4.0f, dotColor);
                    dotX += 10.0f;
                    if (dotX > tagDotStartX + 34.0f) break;
                }
            }
        }
    }

    // A scan that produced partial results but also hit an error must not read as
    // a clean, complete listing. The empty-state branch above covers no-results;
    // here (non-empty view) overlay a compact warning strip at the top of the list.
    if (!scanError_.empty()) {
        NUIRect warnRect(listClip.x, listClip.y, listClip.width, 20.0f);
        renderer.fillRect(warnRect, themeManager.getColor("warning").withAlpha(0.16f));
        renderer.drawTextCentered("Some items couldn't be read \xe2\x80\x94 press F5 to retry", warnRect,
                                  themeManager.getFontSize("s"), themeManager.getColor("warning").withAlpha(0.90f));
    }

    renderer.clearClipRect();
}

void FileBrowser::renderToolbar(NUIRenderer& renderer) {
    (void)renderer;
    // Toolbar header row removed - only search overlay is rendered in onRender
}

void FileBrowser::renderSearchBox(NUIRenderer& renderer) {
    (void)renderer;
    // DEPRECATED: Handled by searchInput_ child component
}

		void FileBrowser::hidePopupMenu() {
		    if (popupMenu_ && popupMenu_->isVisible()) {
		        popupMenu_->hide();
		        detachPopupMenu(popupMenu_);
		        popupMenuTargetPath_.clear();
		        popupMenuTargetIsDirectory_ = false;
		        invalidateCache();
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
		        invalidateCache();
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
		            std::filesystem::path p(favPath);
		            const std::string name = p.filename().string();
		            if (!name.empty()) label = name;

		            std::error_code ec;
		            const bool isDir = std::filesystem::exists(favPath, ec) && std::filesystem::is_directory(favPath, ec);

		            if (isDir) {
		                popupMenu_->addItem(label, [this, path = favPath]() { openFolder(path); });
		            } else {
		                popupMenu_->addItem(label, [this, path = favPath]() { openFile(path); });
		            }
		        }

		        popupMenu_->addSeparator();
		        popupMenu_->addItem("Clear Favorites", [this]() {
		            favoritesPaths_.clear();
		            invalidateCache();
		        });
		    }

const float menuX = lastMousePos_.x;
    const float menuY = lastMousePos_.y + 6.0f;
    attachAndShowPopupMenu(this, popupMenu_, NUIPoint(menuX, menuY));
    invalidateCache();
}

void FileBrowser::showAddFolderMenu() {
		    if (!popupMenu_) return;

		    popupMenu_->clear();
		    popupMenuTargetPath_.clear();
		    popupMenuTargetIsDirectory_ = false;

		    popupMenu_->addItem("Add Current Folder to Places", [this]() {
		        const std::string key = canonicalOrNormalized(std::filesystem::path(currentPath_)).string();
		        if (!key.empty() && std::find(customPlacePaths_.begin(), customPlacePaths_.end(), key) == customPlacePaths_.end()) {
		            customPlacePaths_.push_back(key);
		        }
		        invalidateCache();
		    });
		    popupMenu_->addItem(isFavorite(currentPath_) ? "Unfavorite Current Folder" : "Favorite Current Folder",
		                        [this]() { toggleFavorite(currentPath_); });

		    if (!customPlacePaths_.empty()) {
		        popupMenu_->addSeparator();
		        popupMenu_->addItem("Remove Current Folder from Places", [this]() {
		            const std::string key = canonicalOrNormalized(std::filesystem::path(currentPath_)).string();
		            auto it = std::remove(customPlacePaths_.begin(), customPlacePaths_.end(), key);
		            customPlacePaths_.erase(it, customPlacePaths_.end());
		            invalidateCache();
		        });
		        popupMenu_->addItem("Clear Custom Places", [this]() {
		            customPlacePaths_.clear();
		            invalidateCache();
		        });
		    }

		    attachAndShowPopupMenu(this, popupMenu_, NUIPoint(lastMousePos_.x, lastMousePos_.y + 6.0f));
		    invalidateCache();
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

    const float menuX = lastMousePos_.x;
    const float menuY = lastMousePos_.y + 6.0f;
    attachAndShowPopupMenu(this, popupMenu_, NUIPoint(menuX, menuY));
    invalidateCache();
}

void FileBrowser::showQuickFilterMenu() {
		    if (!popupMenu_) return;

		    popupMenu_->clear();
			    popupMenuTargetPath_.clear();
			    popupMenuTargetIsDirectory_ = false;

                if (isFilterActive()) {
                    popupMenu_->addItem("Clear Search and Filters", [this]() { clearActiveFilters(); });
                    popupMenu_->addSeparator();
                }

		    popupMenu_->addRadioItem("All Files", "quick_filter", activeQuickFilter_ == QuickFilter::All, [this]() {
		        activeQuickFilter_ = QuickFilter::All;
		        applyFilter();
		    });
		    popupMenu_->addRadioItem("Audio", "quick_filter", activeQuickFilter_ == QuickFilter::Audio, [this]() {
		        activeQuickFilter_ = QuickFilter::Audio;
		        applyFilter();
		    });
		    popupMenu_->addRadioItem("Projects", "quick_filter", activeQuickFilter_ == QuickFilter::Projects, [this]() {
		        activeQuickFilter_ = QuickFilter::Projects;
applyFilter();
    });
    popupMenu_->addRadioItem("Folders", "quick_filter", activeQuickFilter_ == QuickFilter::Folders, [this]() {
        activeQuickFilter_ = QuickFilter::Folders;
        applyFilter();
    });

    const auto tags = getAllTagsSorted();
    if (!tags.empty()) {
        popupMenu_->addSeparator();
        popupMenu_->addRadioItem("All Collections", "tag_filter", activeTagFilter_.empty(), [this]() {
            activeTagFilter_.clear();
            applyFilter();
        });
        for (const auto& tag : tags) {
            popupMenu_->addRadioItem("Collection: " + tag, "tag_filter", activeTagFilter_ == tag, [this, tag]() {
                activeTagFilter_ = tag;
                applyFilter();
            });
        }
    }

    const float menuX = lastMousePos_.x;
    const float menuY = lastMousePos_.y + 6.0f;
    attachAndShowPopupMenu(this, popupMenu_, NUIPoint(menuX, menuY));
    invalidateCache();
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

    const float menuX = lastMousePos_.x;
    const float menuY = lastMousePos_.y + 6.0f;
    attachAndShowPopupMenu(this, popupMenu_, NUIPoint(menuX, menuY));
    invalidateCache();
}

void FileBrowser::showItemContextMenu(const FileItem& item, const NUIPoint& position) {
		    if (!popupMenu_) return;

	    popupMenu_->clear();
	    popupMenuTargetPath_ = item.path;
	    popupMenuTargetIsDirectory_ = item.isDirectory;

	    const auto copyToClipboard = [](const std::string& text) {
	        if (auto* utils = Aestra::Platform::getUtils()) {
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
	                invalidateCache();
	            });
	        }
	        popupMenu_->addSeparator();

		        const bool fav = isFavorite(item.path);
		        popupMenu_->addItem(fav ? "Remove from Favorites" : "Add to Favorites",
		                            [this, path = item.path]() { toggleFavorite(path); invalidateCache(); });
		        {
		            auto collectionsMenu = std::make_shared<NUIContextMenu>();
            static const std::vector<std::string> kCollections = {"Purple", "Drums", "Instruments", "Vocals", "Effects", "Clips"};
            for (const auto& tag : kCollections) {
                collectionsMenu->addCheckbox(tag, hasTag(item.path, tag),
                                             [this, path = item.path, tag](bool) { toggleTag(path, tag); });
            }
            popupMenu_->addSubmenu("Add to Collection", collectionsMenu);
        }
        {
            auto tagsMenu = std::make_shared<NUIContextMenu>();
            static const std::vector<std::string> kPresetTags = {
                "Bass", "Vocal", "FX", "Loops", "One-shots", "Synth", "Pads", "Ambience"};
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
	            std::filesystem::path p(path);
	            std::filesystem::path parent = p.parent_path();
	            if (!parent.empty()) {
	                setCurrentPath(parent.string());
	                selectFile(path);
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

		        {
		            auto collectionsMenu = std::make_shared<NUIContextMenu>();
            static const std::vector<std::string> kCollections = {"Purple", "Drums", "Instruments", "Vocals", "Effects", "Clips"};
            for (const auto& tag : kCollections) {
                collectionsMenu->addCheckbox(tag, hasTag(item.path, tag),
                                             [this, path = item.path, tag](bool) { toggleTag(path, tag); });
            }
            popupMenu_->addSubmenu("Add to Collection", collectionsMenu);
        }
        {
            auto tagsMenu = std::make_shared<NUIContextMenu>();
            static const std::vector<std::string> kPresetTags = {
                "Bass", "Vocal", "FX", "Loops", "One-shots", "Synth", "Pads", "Ambience"};
            for (const auto& tag : kPresetTags) {
                tagsMenu->addCheckbox(tag, hasTag(item.path, tag),
                                      [this, path = item.path, tag](bool) { toggleTag(path, tag); });
            }
            popupMenu_->addSubmenu("Tags", tagsMenu);
        }

		        popupMenu_->addSeparator();
		        popupMenu_->addItem("Copy Path", [path = item.path, copyToClipboard]() { copyToClipboard(path); });
		    }

	    attachAndShowPopupMenu(this, popupMenu_, position);
	    invalidateCache();
	}

void FileBrowser::updateScrollPosition() {
    if (selectedIndex_ < 0) return;

    const BrowserLayout browserLayout = computeBrowserLayout();
    float listY = browserLayout.list.y;
    float listHeight = browserLayout.list.height;

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

    const BrowserLayout browserLayout = computeBrowserLayout();
    float scrollbarX = browserLayout.list.right() - scrollbarWidth_ - 2.0f;
    float scrollbarY = browserLayout.list.y;
    float scrollbarHeight = scrollbarTrackHeight_;

    float opacity = std::clamp(scrollbarOpacity_, 0.0f, 1.0f);
    if (opacity <= 0.01f) return;

    const float radius = themeManager.getRadius("s");
    const bool hot = isDraggingScrollbar_ || scrollbarHovered_;
    const float hoverGrow = hot ? 1.0f : 0.0f;
    const float trackWidth = scrollbarWidth_ + hoverGrow;
    const float trackX = scrollbarX - hoverGrow * 0.5f;

    // === GLASS/PRO SCROLLBAR STYLE ===
    // Adapted from NUIScrollbar::drawEnhancedTrack & drawEnhancedThumb

    // 1. Draw Track (Subtle Gradient)
    const float trackAlphaMul = (hot ? 0.18f : 0.08f) * opacity;
    NUIColor trackBase = themeManager.getColor("border").withAlpha(std::clamp(trackAlphaMul, 0.0f, 1.0f));
    NUIColor trackTop = trackBase.lightened(0.03f);
    NUIColor trackBottom = trackBase.darkened(0.06f);

    NUIRect trackRect(trackX, scrollbarY, trackWidth, scrollbarHeight);

    // Draw gradient track background
    for (int i = 0; i < 4; ++i) {
        float factor = static_cast<float>(i) / 3.0f;
        NUIColor gradientColor = AestraUI::NUIColor::lerp(trackTop, trackBottom, factor);
        NUIRect gradientRect = trackRect;
        gradientRect.y += i * 0.5f;
        gradientRect.height -= i * 0.5f;
        renderer.fillRoundedRect(gradientRect, radius, gradientColor);
    }

    // Track Inner Highlight
    NUIRect highlightRect = trackRect;
    highlightRect.x += 1.0f;
    highlightRect.y += 1.0f;
    highlightRect.width -= 2.0f;
    highlightRect.height = trackRect.height * 0.3f;
    const float highlightAlphaMul = (hot ? 0.35f : 0.25f);
    renderer.fillRoundedRect(highlightRect, std::max(0.0f, radius - 1.0f),
                             trackTop.withAlpha(trackTop.a * highlightAlphaMul));


    // 2. Draw Thumb (Glass w/ Gradient & Markers)
    float thumbY = scrollbarY + scrollbarThumbY_;
    NUIRect thumbRect(trackX, thumbY, trackWidth, scrollbarThumbHeight_);

    // Determine thumb base color
    const bool thumbPressed = isDraggingScrollbar_;
    const bool thumbHot = thumbPressed || scrollbarHovered_;

    // Use textSecondary as base (neutral grey/white)
    NUIColor thumbBase = themeManager.getColor("textSecondary");
    if (thumbPressed) {
        thumbBase = themeManager.getColor("textPrimary"); // Brighter when dragging
    } else if (thumbHot) {
        thumbBase = thumbBase.lightened(0.2f);
    }
    // Apply opacity base
    thumbBase = thumbBase.withAlpha((thumbHot ? 0.55f : 0.35f) * opacity);

    NUIColor thumbTopColor = thumbBase.lightened(0.06f);
    NUIColor thumbBottomColor = thumbBase.darkened(0.06f);

    // Thumb Thickness Affordance
    NUIRect visualThumb = thumbRect;
    const float inset = thumbHot ? 1.0f : 2.0f;
    visualThumb.x += inset;
    visualThumb.width = std::max(0.0f, visualThumb.width - inset * 2.0f);
    const float thumbRadius = std::min(visualThumb.width, visualThumb.height) * 0.5f;

    // Draw Gradient Thumb
    for (int i = 0; i < 4; ++i) {
        float factor = static_cast<float>(i) / 3.0f;
        NUIColor gradientColor = AestraUI::NUIColor::lerp(thumbTopColor, thumbBottomColor, factor);
        NUIRect gradientRect = visualThumb;
        gradientRect.y += i * 0.5f;
        gradientRect.height -= i * 0.5f;
        renderer.fillRoundedRect(gradientRect, thumbRadius, gradientColor);
    }

    // Draw Markers (Horizontal Lines)
    if (visualThumb.height > 12.0f) {
        float markerHeight = 2.0f;
        float markerSpacing = 3.0f;
        float totalMarkerHeight = (markerHeight * 2) + markerSpacing;
        float markerY = visualThumb.y + (visualThumb.height - totalMarkerHeight) * 0.5f;

        NUIColor markerColor = AestraUI::NUIColor::white().withAlpha((thumbHot ? 0.4f : 0.2f) * opacity);

        // Top marker
        renderer.fillRoundedRect(NUIRect(visualThumb.x + 3.0f, markerY, visualThumb.width - 6.0f, markerHeight),
                                 1.0f, markerColor);
        // Bottom marker
        renderer.fillRoundedRect(NUIRect(visualThumb.x + 3.0f, markerY + markerHeight + markerSpacing, visualThumb.width - 6.0f, markerHeight),
                                 1.0f, markerColor);
    }

    // Thumb Inner Highlight
    NUIRect thumbHighlight = visualThumb;
    thumbHighlight.x += 1.0f;
    thumbHighlight.y += 1.0f;
    thumbHighlight.width -= 2.0f;
    thumbHighlight.height = visualThumb.height * 0.4f;
    renderer.fillRoundedRect(thumbHighlight, std::max(0.0f, thumbRadius - 1.0f),
                             thumbTopColor.withAlpha(thumbTopColor.a * 0.3f));

    // Thumb Border (Subtle)
    renderer.strokeRoundedRect(visualThumb, thumbRadius, 1.0f,
        thumbBase.lightened(0.1f).withAlpha(std::clamp(thumbBase.a * 0.8f, 0.0f, 1.0f)));
}

bool FileBrowser::handleScrollbarMouseEvent(const NUIMouseEvent& event) {
    const BrowserLayout browserLayout = computeBrowserLayout();
    float scrollbarX = browserLayout.list.right() - scrollbarWidth_ - 2.0f;
    float scrollbarY = browserLayout.list.y;

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
        float itemHeight = BROWSER_LIST_ROW_H;
        float maxScroll = std::max(0.0f, (view.size() * itemHeight) - scrollbarTrackHeight_);

        // Direct scrolling for responsive dragging - set BOTH for instant response
        targetScrollOffset_ = dragStartScrollOffset_ + (scrollRatio * maxScroll);
        scrollOffset_ = targetScrollOffset_; // Instant during drag, no lerp

        // Clamp scroll offset
        scrollOffset_ = std::max(0.0f, std::min(scrollOffset_, maxScroll));
        targetScrollOffset_ = scrollOffset_;

        invalidateCache();
        return true;
    }

    // Check if mouse is over scrollbar area (with padding for easier clicking)
    bool inScrollbarArea = (event.position.x >= scrollbarX - 10 &&
                             event.position.x <= scrollbarX + scrollbarWidth_ + 10 &&
                             event.position.y >= scrollbarY - 10 &&
                             event.position.y <= scrollbarY + scrollbarTrackHeight_ + 10);

    if (scrollbarHovered_ != inScrollbarArea) {
        scrollbarHovered_ = inScrollbarArea;
        invalidateCache();
    }

    // Hovering the scrollbar should reveal it (auto-hide UX)
    if (inScrollbarArea) {
        scrollbarFadeTimer_ = 0.0f;
        if (scrollbarOpacity_ < 1.0f) {
            scrollbarOpacity_ = 1.0f;
            invalidateCache();
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
            float itemHeight = BROWSER_LIST_ROW_H;
            float maxScroll = std::max(0.0f, (view.size() * itemHeight) - scrollbarTrackHeight_);
            // Direct jump to clicked position - set both for instant response
            targetScrollOffset_ = scrollRatio * maxScroll;
            scrollOffset_ = targetScrollOffset_;

            // Clamp scroll offset
            scrollOffset_ = std::max(0.0f, std::min(scrollOffset_, maxScroll));
            targetScrollOffset_ = scrollOffset_;

            invalidateCache();
        }
        return true;
    } else if (!event.pressed && event.button == NUIMouseButton::Left) {
        // Stop dragging
        isDraggingScrollbar_ = false;
        return true;
    }

    return false;
}

bool FileBrowser::handleNavigationMouseEvent(const NUIMouseEvent& event, const BrowserLayout& layout) {
    if (event.cursorCaptured) return false;

    // Ignore the fixed folder-name header band: rows scrolled up under it are
    // visually clipped, so they must not be clickable there either.
    const float navContentTop = layout.navPane.y + 28.0f;
    const bool insideNav = layout.navPane.contains(event.position) && event.position.y >= navContentTop;
    int newHovered = -1;
    if (insideNav) {
        for (int i = 0; i < static_cast<int>(navHits_.size()); ++i) {
            if (navHits_[i].bounds.contains(event.position)) {
                newHovered = i;
                break;
            }
        }
    }

    if (newHovered != hoveredNavIndex_) {
        hoveredNavIndex_ = newHovered;
        setDirty(true); // hover overlay only — no cache rebuild
    }

    if (usesCompactNavigation() && newHovered >= 0 && newHovered < static_cast<int>(navHits_.size())) {
        NUIPoint tooltipPosition = event.position;
        tooltipPosition.x = layout.navPane.right() + 8.0f;
        NUIComponent::showRemoteTooltip(navHits_[newHovered].label, tooltipPosition, this);
    } else if (newHovered < 0 || !usesCompactNavigation()) {
        NUIComponent::hideRemoteTooltip(this);
    }

    if (!event.pressed || event.button != NUIMouseButton::Left || newHovered < 0 ||
        newHovered >= static_cast<int>(navHits_.size())) {
        return false;
    }

    const BrowserNavAction previousAction = activeNavAction_;
    const BrowserNavAction action = navHits_[newHovered].action;
    activeNavAction_ = action;
    activeNavPath_ = navHits_[newHovered].path;
    updateContentViews();
    auto setFilter = [this](QuickFilter filter) {
        if (activeQuickFilter_ != filter) {
            activeQuickFilter_ = filter;
            applyFilter();
        } else {
            invalidateCache();
        }
    };

    switch (action) {
        case BrowserNavAction::Favorites:
            showFavoritesMenu();
            break;
        case BrowserNavAction::Purple:
            activeTagFilter_ = "Purple";
            activeQuickFilter_ = QuickFilter::All;
            applyFilter();
            break;
        case BrowserNavAction::CollectionDrums:
            activeTagFilter_ = "Drums";
            activeQuickFilter_ = QuickFilter::All;
            applyFilter();
            break;
        case BrowserNavAction::CollectionInstruments:
            activeTagFilter_ = "Instruments";
            activeQuickFilter_ = QuickFilter::All;
            applyFilter();
            break;
        case BrowserNavAction::Vocals:
            activeTagFilter_ = "Vocals";
            activeQuickFilter_ = QuickFilter::All;
            applyFilter();
            break;
        case BrowserNavAction::Sounds:
        case BrowserNavAction::Samples:
            activeTagFilter_.clear();
            setFilter(QuickFilter::Audio);
            break;
        case BrowserNavAction::Drums: {
            auto path = std::filesystem::path(rootPath_) / "User Library" / "Drums";
            std::filesystem::create_directories(path);
            activeTagFilter_ = "Drums";
            activeQuickFilter_ = QuickFilter::Audio;
            activeNavAction_ = BrowserNavAction::Drums;
            navigateTo(path.string());
            applyFilter();
            break;
        }
        case BrowserNavAction::Instruments: {
            auto path = std::filesystem::path(rootPath_) / "User Library" / "Instruments";
            std::filesystem::create_directories(path);
            activeTagFilter_ = "Instruments";
            activeQuickFilter_ = QuickFilter::All;
            activeNavAction_ = BrowserNavAction::Instruments;
            navigateTo(path.string());
            applyFilter();
            break;
        }
        case BrowserNavAction::AudioEffects: {
            auto path = std::filesystem::path(rootPath_) / "User Library" / "Effects";
            std::filesystem::create_directories(path);
            activeTagFilter_ = "Effects";
            activeQuickFilter_ = QuickFilter::All;
            activeNavAction_ = BrowserNavAction::AudioEffects;
            navigateTo(path.string());
            applyFilter();
            break;
        }
        case BrowserNavAction::Patterns:
            activeTagFilter_.clear();
            setFilter(QuickFilter::All);
            break;
        case BrowserNavAction::Clips: {
            auto path = std::filesystem::path(rootPath_) / "User Library" / "Clips";
            std::filesystem::create_directories(path);
            activeTagFilter_ = "Clips";
            activeQuickFilter_ = QuickFilter::All;
            activeNavAction_ = BrowserNavAction::Clips;
            navigateTo(path.string());
            applyFilter();
            break;
        }
        case BrowserNavAction::CurrentProject:
            if (!rootPath_.empty()) {
                navigateTo(rootPath_);
            }
            activeTagFilter_.clear();
            setFilter(QuickFilter::All);
            break;
        case BrowserNavAction::UserLibrary:
        case BrowserNavAction::Packs: {
            std::filesystem::path base = rootPath_.empty() ? std::filesystem::path(currentPath_) : std::filesystem::path(rootPath_);
            std::filesystem::path target = base / (action == BrowserNavAction::Packs ? "Packs" : "User Library");
            std::error_code ec;
            std::filesystem::create_directories(target, ec);
            navigateTo(target.string());
            activeTagFilter_.clear();
            setFilter(QuickFilter::All);
            break;
        }
        case BrowserNavAction::CustomPlace:
            if (!navHits_[newHovered].path.empty()) {
                navigateTo(navHits_[newHovered].path);
            }
            activeTagFilter_.clear();
            setFilter(QuickFilter::All);
            break;
        case BrowserNavAction::AddFolder:
            showAddFolderMenu();
            break;
        default:
            activeTagFilter_.clear();
            setFilter(QuickFilter::All);
            break;
    }

    // Returning from an embedded view (Plugins/Patterns) to a file-backed view:
    // while embedded, search-text changes route to the embedded view rather than
    // applyFilter(), so the file list still reflects the pre-embedded query. The
    // per-case setFilter() above only re-filters when the quick filter changed, so
    // reapply here to honor the current search text on the way back to files.
    const bool leavingEmbeddedView =
        (previousAction == BrowserNavAction::Plugins || previousAction == BrowserNavAction::Patterns);
    const bool enteringFileView =
        (action != BrowserNavAction::Plugins && action != BrowserNavAction::Patterns);
    if (leavingEmbeddedView && enteringFileView) {
        applyFilter();
    }

    if (onNavActionSelected_) {
        onNavActionSelected_(action);
    }

    invalidateCache();
    return true;
}

void FileBrowser::updateScrollbarVisibility() {
    // Get component dimensions from theme
    auto& themeManager = NUIThemeManager::getInstance();
    float itemHeight = BROWSER_LIST_ROW_H;
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
    return (searchInput_ && !searchInput_->getText().empty()) ||
           !activeTagFilter_.empty() ||
           activeQuickFilter_ != QuickFilter::All;
}

bool FileBrowser::matchesQuickFilter(const FileItem& item) const {
    switch (activeQuickFilter_) {
        case QuickFilter::All:
            return true;
        case QuickFilter::Audio:
            return item.type == FileType::AudioFile ||
                   item.type == FileType::MusicFile ||
                   item.type == FileType::WavFile ||
                   item.type == FileType::Mp3File ||
                   item.type == FileType::FlacFile;
        case QuickFilter::Projects:
            return item.type == FileType::ProjectFile;
        case QuickFilter::Folders:
            return item.isDirectory;
    }
    return true;
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

void FileBrowser::setSearchPlaceholder(const std::string& placeholder) {
    if (searchInput_ && searchInput_->getPlaceholderText() != placeholder) {
        searchInput_->setPlaceholderText(placeholder);
    }
}

void FileBrowser::setPreviewPanelVisible(bool visible) {
    if (previewPanelVisible_ == visible) {
        return;
    }

    previewPanelVisible_ = visible;
    const auto bounds = getBounds();
    onResize(static_cast<int>(bounds.width), static_cast<int>(bounds.height));
    invalidateCache();
}

void FileBrowser::applyFilter() {
    filteredFiles_.clear();
    std::string query = searchInput_ ? searchInput_->getText() : "";
    const bool hasNameFilter = !query.empty();
    const bool hasTagFilter = !activeTagFilter_.empty();
    const bool hasQuickFilter = activeQuickFilter_ != QuickFilter::All;

    if (!hasNameFilter && !hasTagFilter && !hasQuickFilter) {
        // No filter active, display all root items
        updateDisplayList();
        selectedFile_ = nullptr;
        selectedIndex_ = -1;
        selectedIndices_.clear();
        updateScrollbarVisibility();
        invalidateCache();
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
        bool matchesType = !hasQuickFilter || matchesQuickFilter(*item);

        if (matchesSearch && matchesTag && matchesType) {
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
    invalidateCache();
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
        std::filesystem::path rel = p.lexically_relative(root);
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
             renderer.fillRoundedRect(partRect, chipRadius, NUIThemeManager::getInstance().getColor("hover").withAlpha(0.44f));
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
            renderer.fillRoundedRect(partRect, chipRadius, NUIThemeManager::getInstance().getColor("hover").withAlpha(0.44f));
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

// renderPreviewPanel is implemented at the bottom of this file

std::string FileBrowser::getSearchQuery() const {
    return searchInput_ ? searchInput_->getText() : "";
}

bool FileBrowser::isSearchBoxFocused() const {
    return searchInput_ ? searchInput_->isFocused() : false;
}

// === PERSISTENT STATE SAVE/LOAD ===

void FileBrowser::saveState(const std::string& filePath) {
    Aestra::JSON j = Aestra::JSON::object();
    j.set("version", Aestra::JSON(2.0));
    j.set("currentPath", Aestra::JSON(currentPath_));
    j.set("rootPath", Aestra::JSON(rootPath_));
    j.set("scrollOffset", Aestra::JSON(static_cast<double>(scrollOffset_)));
    j.set("sortMode", Aestra::JSON(static_cast<double>(sortMode_)));
    j.set("sortAscending", Aestra::JSON(sortAscending_));

    // Expanded folders
    Aestra::JSON expandedArr = Aestra::JSON::array();
    std::function<void(const FileItem&)> collectExpanded = [&](const FileItem& item) {
        if (item.isDirectory && item.isExpanded) {
            expandedArr.push(Aestra::JSON(item.path));
        }
        for (const auto& child : item.children) {
            collectExpanded(child);
        }
    };
    for (const auto& item : rootItems_) {
        collectExpanded(item);
    }
    j.set("expandedFolders", expandedArr);

    // Favorites
    Aestra::JSON favArr = Aestra::JSON::array();
    for (const auto& f : favoritesPaths_) {
        favArr.push(Aestra::JSON(f));
    }
    j.set("favorites", favArr);

    // Custom places
    Aestra::JSON placesArr = Aestra::JSON::array();
    for (const auto& p : customPlacePaths_) {
        placesArr.push(Aestra::JSON(p));
    }
    j.set("customPlaces", placesArr);

    // Tag filter
    j.set("tagFilter", Aestra::JSON(activeTagFilter_));

    // Tags by path
    Aestra::JSON tagsObj = Aestra::JSON::object();
    for (const auto& [pathKey, tags] : tagsByPath_) {
        if (pathKey.empty() || tags.empty()) continue;
        Aestra::JSON tagArr = Aestra::JSON::array();
        for (const auto& t : tags) {
            tagArr.push(Aestra::JSON(t));
        }
        tagsObj.set(pathKey, tagArr);
    }
    j.set("tagsByPath", tagsObj);

    std::ofstream file(filePath);
    if (file.is_open()) {
        file << j.toString(2);
        file.close();
        Aestra::Log::info("[FileBrowser] State saved to: " + filePath);
    } else {
        Aestra::Log::warning("[FileBrowser] Failed to save state to: " + filePath);
    }
}

void FileBrowser::loadState(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        Aestra::Log::info("[FileBrowser] No saved state found at: " + filePath);
        return;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    if (content.empty()) return;

    // Check if this is legacy pipe-separated format
    if (content.find("currentPath=") != std::string::npos && content.find('{') == std::string::npos) {
        migrateLegacySettings(filePath);
        // Re-read as JSON after migration
        std::ifstream file2(filePath);
        if (!file2.is_open()) return;
        content = std::string((std::istreambuf_iterator<char>(file2)), std::istreambuf_iterator<char>());
    }

    Aestra::JSON j;
    try {
        j = Aestra::JSON::parse(content);
    } catch (...) {
        Aestra::Log::warning("[FileBrowser] Failed to parse state JSON");
        return;
    }

    if (!j.isObject()) return;

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
    std::vector<std::string> loadedCustomPlaces;
    bool hasCustomPlaces = false;
    std::string loadedTagFilter;
    bool hasTagFilter = false;
    std::unordered_map<std::string, std::vector<std::string>> loadedTagsByPath;
    bool hasTags = false;

    if (j.has("currentPath") && j["currentPath"].isString()) {
        loadedCurrentPath = j["currentPath"].asString();
    }
    if (j.has("rootPath") && j["rootPath"].isString()) {
        loadedRootPath = j["rootPath"].asString();
    }
    if (j.has("scrollOffset") && j["scrollOffset"].isNumber()) {
        loadedScrollOffset = static_cast<float>(j["scrollOffset"].asNumber());
        hasScrollOffset = true;
    }
    if (j.has("sortMode") && j["sortMode"].isNumber()) {
        loadedSortMode = static_cast<SortMode>(static_cast<int>(j["sortMode"].asNumber()));
        hasSortMode = true;
    }
    if (j.has("sortAscending") && j["sortAscending"].isBool()) {
        loadedSortAscending = j["sortAscending"].asBool();
        hasSortAscending = true;
    }
    if (j.has("expandedFolders") && j["expandedFolders"].isArray()) {
        auto arr = j["expandedFolders"].asArray();
        for (size_t i = 0; i < arr.size(); ++i) {
            if (arr[i].isString()) expandedFolders.push_back(arr[i].asString());
        }
    }
    if (j.has("favorites") && j["favorites"].isArray()) {
        hasFavorites = true;
        auto arr = j["favorites"].asArray();
        for (size_t i = 0; i < arr.size(); ++i) {
            if (arr[i].isString()) loadedFavorites.push_back(arr[i].asString());
        }
    }
    if (j.has("customPlaces") && j["customPlaces"].isArray()) {
        hasCustomPlaces = true;
        auto arr = j["customPlaces"].asArray();
        for (size_t i = 0; i < arr.size(); ++i) {
            if (arr[i].isString()) loadedCustomPlaces.push_back(arr[i].asString());
        }
    }
    if (j.has("tagFilter") && j["tagFilter"].isString()) {
        hasTagFilter = true;
        loadedTagFilter = j["tagFilter"].asString();
    }
    if (j.has("tagsByPath") && j["tagsByPath"].isObject()) {
        hasTags = true;
        auto obj = j["tagsByPath"].asObject();
        for (const auto& [pathKey, val] : obj) {
            if (!val.isArray()) continue;
            auto arr = val.asArray();
            std::vector<std::string> tags;
            for (size_t i = 0; i < arr.size(); ++i) {
                if (arr[i].isString()) tags.push_back(arr[i].asString());
            }
            if (!tags.empty()) loadedTagsByPath[pathKey] = std::move(tags);
        }
    }

    // Apply settings in safe order
    std::error_code rootEc;
    if (!loadedRootPath.empty() &&
        std::filesystem::exists(loadedRootPath, rootEc) &&
        std::filesystem::is_directory(loadedRootPath, rootEc)) {
        rootPath_ = canonicalOrNormalized(std::filesystem::path(loadedRootPath)).string();
    } else {
        rootPath_.clear();
    }

    if (hasSortMode) sortMode_ = loadedSortMode;
    if (hasSortAscending) sortAscending_ = loadedSortAscending;
    if (hasFavorites) favoritesPaths_ = std::move(loadedFavorites);
    if (hasCustomPlaces) customPlacePaths_ = std::move(loadedCustomPlaces);
    if (hasTags) tagsByPath_ = std::move(loadedTagsByPath);
    if (hasTagFilter) activeTagFilter_ = std::move(loadedTagFilter);

    if (loadedCurrentPath.empty() && !rootPath_.empty()) {
        loadedCurrentPath = rootPath_;
    }
    if (!loadedCurrentPath.empty()) {
        setCurrentPath(loadedCurrentPath);
    } else {
        loadDirectoryContents();
        updateBreadcrumbs();
    }

    // Re-expand saved folders
    std::function<void(FileItem&)> expandSaved = [&](FileItem& item) {
        if (item.isDirectory) {
            for (const auto& expandedPath : expandedFolders) {
                if (item.path == expandedPath) {
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
    Aestra::Log::info("[FileBrowser] State loaded from: " + filePath);
}

void FileBrowser::migrateLegacySettings(const std::string& filePath) {
    std::ifstream f(filePath);
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("customPlaces=", 0) == 0) {
            std::string val = line.substr(13);
            std::stringstream ss(val);
            std::string token;
            while (std::getline(ss, token, '|'))
                if (!token.empty()) customPlacePaths_.push_back(token);
        }
        if (line.rfind("favorites=", 0) == 0) {
            std::string val = line.substr(10);
            std::stringstream ss(val);
            std::string token;
            while (std::getline(ss, token, '|'))
                if (!token.empty()) favoritesPaths_.push_back(token);
        }
    }
    saveState(filePath);
}

// Issue #120: Get list of currently expanded folder paths for UIState persistence
std::vector<std::string> FileBrowser::getExpandedFolders() const {
    std::vector<std::string> expanded;

    std::function<void(const FileItem&)> collectExpanded = [&](const FileItem& item) {
        if (item.isDirectory && item.isExpanded) {
            expanded.push_back(item.path);
            for (const auto& child : item.children) {
                collectExpanded(child);
            }
        }
    };

    for (const auto& item : rootItems_) {
        collectExpanded(item);
    }

    return expanded;
}

// Issue #120: Expand folders from a list of paths (used when restoring UIState)
void FileBrowser::expandFolders(const std::vector<std::string>& folders) {
    if (folders.empty()) return;

    std::function<void(FileItem&)> expandMatching = [&](FileItem& item) {
        if (!item.isDirectory) return;

        for (const auto& path : folders) {
            if (item.path == path) {
                if (!item.hasLoadedChildren) {
                    loadFolderContents(&item);
                }
                item.isExpanded = true;
                break;
            }
        }

        for (auto& child : item.children) {
            expandMatching(child);
        }
    };

    for (auto& item : rootItems_) {
        expandMatching(item);
    }

    updateDisplayList();
    invalidateCache();
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



} // namespace AestraUI
