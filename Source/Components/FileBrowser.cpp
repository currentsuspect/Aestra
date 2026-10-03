// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "FileBrowserInternal.h"



using namespace Aestra;

namespace AestraUI {

using namespace FileBrowserInternal;


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
    , scrollbarWidth_(AestraUI::kOverlayScrollbarThickness)
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

    systemPlaces_ = BrowserLibrary::discoverSystemPlaces();

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
    // The search row sits on the surface (0.7.0 triage): the input must not
    // paint its own recessed background/border over the flattened treatment.
    searchInput_->setBackgroundVisible(false);
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
    searchInput_->setJustification(NUITextInput::Justification::Left);
    searchInput_->setPadding(4.0f);
    searchInput_->setBorderRadius(5.0f);
    searchInput_->setBackgroundColor(themeManager.getColor("backgroundPrimary"));
    searchInput_->setBorderColor(themeManager.getColor("borderSubtle").withAlpha(0.62f));
    searchInput_->setFocusedBorderColor(themeManager.getColor("focusRing"));
    searchInput_->setBorderWidth(1.0f);

    m_searchIcon = std::make_shared<NUIIcon>();
    // Search icon (magnifier), split across literals to respect the column limit
    const char* searchSvg =
        R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M10.4 3.2a7.2 7.2 0 1 0 4.55 12.78l4.03 )"
        R"(4.03 1.7-1.7-4.03-4.03A7.2 7.2 0 0 0 10.4 3.2Zm0 2.4a4.8 4.8 0 1 1 0 9.6 4.8 4.8 )"
        R"(0 0 1 0-9.6Z"/></svg>)";
    m_searchIcon->loadSVG(searchSvg);
    m_searchIcon->setIconSize(15.0f, 15.0f);
    m_searchIcon->setColor(themeManager.getColor("textSecondary").withAlpha(0.72f));

    // Initialize icons with improved visibility for Liminal Dark v2.0
    // Use inline SVG content for reliable icon loading
    // Folder icon (Mac-style smooth)
    folderIcon_ = std::make_shared<NUIIcon>();
    // Single solid folder. The old version drew a second, opacity-0.8 path that
    // the solid one covered completely — invisible work on every rasterization.
    const char* folderSvg = R"(<svg viewBox="0 0 24 24"><path fill="currentColor" fill-rule="evenodd" d="M2.5 6.3A1.8 1.8 0 0 1 4.3 4.5H9.2L11.2 6.5H19.7A1.8 1.8 0 0 1 21.5 8.3V17.7A1.8 1.8 0 0 1 19.7 19.5H4.3A1.8 1.8 0 0 1 2.5 17.7Z M4.5 9.3H19.5V10.8H4.5Z"/></svg>)";
    folderIcon_->loadSVG(folderSvg);
    folderIcon_->setIconSize(20, 20);
    folderIcon_->setColor(themeManager.getColor("textSecondary"));

    // File Icon (Generic) -> unknownFileIcon_
    unknownFileIcon_ = std::make_shared<NUIIcon>();
    const char* fileSvg = R"(<svg viewBox="0 0 24 24"><path fill="currentColor" fill-rule="evenodd" d="M6 2.5H14.2L19.5 7.8V20.1A1.4 1.4 0 0 1 18.1 21.5H6A1.4 1.4 0 0 1 4.6 20.1V3.9A1.4 1.4 0 0 1 6 2.5Z M13.6 3.9V8.4H18.1Z"/></svg>)";
    unknownFileIcon_->loadSVG(fileSvg);
    unknownFileIcon_->setIconSize(20, 20);
    // Increased visibility for dark theme
    unknownFileIcon_->setColor(themeManager.getColor("textSecondary").withAlpha(0.9f));

    // Generic Audio Icon -> audioFileIcon_ (Standard Music Note)
    audioFileIcon_ = std::make_shared<NUIIcon>();
    const char* audioSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><circle cx="8.4" cy="16.8" r="3.9"/><path d="M10.4 3.2h2.1v13.6h-2.1z"/><path d="M12.5 3.2c3.5 1.2 5.5 3.1 5.7 6.1-1.2-2.3-3.1-3.4-5.7-3.7z"/></svg>)";
    audioFileIcon_->loadSVG(audioSvg);
    audioFileIcon_->setIconSize(20, 20);
    audioFileIcon_->setColor(themeManager.getColor("textSecondary"));

    // WAV Icon (Waveform visual)
    wavFileIcon_ = std::make_shared<NUIIcon>();
    // WAV — a sample's peak envelope, mirrored about the centre line. The old
    // bars all grew from a shared baseline, which reads as a bar chart or a
    // level meter rather than audio.
    const char* wavSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><rect x="2.1" y="9.4" width="1.9" height="5.2" rx="0.95"/><rect x="5.2" y="6.6" width="1.9" height="10.8" rx="0.95"/><rect x="8.3" y="8.6" width="1.9" height="6.8" rx="0.95"/><rect x="11.4" y="4.4" width="1.9" height="15.2" rx="0.95"/><rect x="14.5" y="7.4" width="1.9" height="9.2" rx="0.95"/><rect x="17.6" y="5.8" width="1.9" height="12.4" rx="0.95"/><rect x="20.7" y="9.8" width="1.9" height="4.4" rx="0.95"/></svg>)";
    wavFileIcon_->loadSVG(wavSvg);
    wavFileIcon_->setIconSize(20, 20);
    wavFileIcon_->setColor(themeManager.getColor("textSecondary"));

    // MP3 Icon (Music Note Circle)
    mp3FileIcon_ = std::make_shared<NUIIcon>();
    // MP3 — a record: outer disc, groove gap, centre label. A finished track
    // rather than raw material, which is what an mp3 in a browser usually is.
    const char* mp3Svg = R"(<svg viewBox="0 0 24 24"><path fill="currentColor" fill-rule="evenodd" d="M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20zm0 3.2a6.8 6.8 0 1 1 0 13.6 6.8 6.8 0 0 1 0-13.6z"/><circle cx="12" cy="12" r="2.4" fill="currentColor"/></svg>)";
    mp3FileIcon_->loadSVG(mp3Svg);
    mp3FileIcon_->setIconSize(20, 20);
    mp3FileIcon_->setColor(themeManager.getColor("textSecondary"));

    // FLAC Icon (lossless compression)
    flacFileIcon_ = std::make_shared<NUIIcon>();
    // FLAC — a waveform squeezed between two plates: lossless *compression*.
    // The previous glyph was a bare sine, which at 20px was one more wavy line
    // next to WAV's wavy lines; the difference has to be in the silhouette, not
    // in the curvature (spec 2 §7). Two heavy horizontal rules read instantly
    // as "packed", and nothing else in the browser has that outline.
    const char* flacSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><rect x="2.4" y="3.4" width="19.2" height="2.6" rx="1.3"/><rect x="2.4" y="18" width="19.2" height="2.6" rx="1.3"/><rect x="4.6" y="10.6" width="2.2" height="2.8" rx="1.1"/><rect x="8.2" y="8.6" width="2.2" height="6.8" rx="1.1"/><rect x="11.8" y="9.8" width="2.2" height="4.4" rx="1.1"/><rect x="15.4" y="7.8" width="2.2" height="8.4" rx="1.1"/><rect x="19" y="10.2" width="2.2" height="3.6" rx="1.1"/></svg>)";
    flacFileIcon_->loadSVG(flacSvg);
    flacFileIcon_->setIconSize(20, 20);
    flacFileIcon_->setColor(themeManager.getColor("textSecondary"));

    // OGG Icon (container / stream)
    oggFileIcon_ = std::make_shared<NUIIcon>();
    // OGG — a container: a solid capsule with a play triangle cut out of it
    // (evenodd, so the background shows through). Deliberately not another
    // waveform; the browser already has two, and a third would not be told
    // apart at 20px. The cut-out is one shape, so the holes cannot cancel.
    const char* oggSvg = R"(<svg viewBox="0 0 24 24"><path fill="currentColor" fill-rule="evenodd" d="M4.6 4H19.4A2.6 2.6 0 0 1 22 6.6V17.4A2.6 2.6 0 0 1 19.4 20H4.6A2.6 2.6 0 0 1 2 17.4V6.6A2.6 2.6 0 0 1 4.6 4Z M4.6 6.2V17.8H19.4V6.2Z"/><g fill="currentColor"><rect x="6.6" y="10.4" width="1.9" height="3.2" rx=".95"/><rect x="9.6" y="8.4" width="1.9" height="7.2" rx=".95"/><rect x="12.6" y="9.6" width="1.9" height="4.8" rx=".95"/><rect x="15.6" y="10.8" width="1.9" height="2.4" rx=".95"/></g></svg>)";
    oggFileIcon_->loadSVG(oggSvg);
    oggFileIcon_->setIconSize(20, 20);
    oggFileIcon_->setColor(themeManager.getColor("textSecondary"));

    // MIDI Icon — piano-roll note blocks. MIDI files previously fell through to
    // the generic document glyph because getIconForFileType had no MidiFile
    // case, so a first-class producer file type looked like a text file.
    // Deliberately not a waveform: MIDI carries no audio.
    midiFileIcon_ = std::make_shared<NUIIcon>();
    const char* midiSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><rect x="2.8" y="5.2" width="8.2" height="3" rx="1.5"/><rect x="12.2" y="9.4" width="9" height="3" rx="1.5"/><rect x="5.4" y="13.6" width="7.4" height="3" rx="1.5"/><rect x="13.6" y="17.8" width="7.6" height="3" rx="1.5"/></svg>)";
    midiFileIcon_->loadSVG(midiSvg);
    midiFileIcon_->setIconSize(20, 20);
    midiFileIcon_->setColor(themeManager.getColor("textSecondary"));

    // Project Icon
    projectFileIcon_ = std::make_shared<NUIIcon>();
    // Project — an arrangement: a card with track lanes cut out of it. The old
    // glyph was an abstract diamond that could have meant anything.
    const char* projectSvg = R"(<svg viewBox="0 0 24 24"><path fill="currentColor" fill-rule="evenodd" d="M5 2.5H19A2.5 2.5 0 0 1 21.5 5V19A2.5 2.5 0 0 1 19 21.5H5A2.5 2.5 0 0 1 2.5 19V5A2.5 2.5 0 0 1 5 2.5Z M12 6.14L13.64 10.83L18.6 10.94L14.35 13.52Q11.55 16.7 7.62 18.27L9.35 13.94L5.4 10.94L10.36 10.83Z M14.96 14.37L16.08 18.7L11.05 17.71Q13.22 16.35 14.96 14.37Z"/></svg>)";
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
    const char* chevronSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M9.3 5.4 16 12l-6.7 6.6-1.7-1.7 4.9-4.9-4.9-4.9z"/></svg>)";
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

// =============================================================================
// SECTION: Rendering
// =============================================================================

// =============================================================================
// SECTION: Rendering with FBO Caching
// =============================================================================

void FileBrowser::drawListEmptyState(NUIRenderer& renderer, const NUIRect& listClip,
                                     const std::shared_ptr<NUIIcon>& icon, const std::string& title,
                                     const std::string& hint) {
    auto& themeManager = NUIThemeManager::getInstance();
    const float titleFont = themeManager.getFontSize("l");
    const float hintFont = themeManager.getFontSize("s");
    constexpr float kIconSize = 28.0f;
    constexpr float kIconGap = 12.0f;
    constexpr float kTitleH = 20.0f;
    constexpr float kTitleGap = 6.0f;
    constexpr float kHintLineH = 16.0f;

    // Cap the hint measure so it reads as a compact centered paragraph with
    // real side margins instead of running edge to edge.
    const float hintMaxWidth = std::max(60.0f, std::min(listClip.width - 48.0f, 240.0f));
    const auto hintLines = wrapHintBalanced(renderer, hint, hintFont, hintMaxWidth);

    float blockH = kTitleH + kTitleGap + static_cast<float>(hintLines.size()) * kHintLineH;
    if (icon) blockH += kIconSize + kIconGap;
    float y = listClip.y + std::max(12.0f, listClip.height * 0.42f - blockH * 0.5f);

    renderer.setClipRect(listClip);
    if (icon) {
        const NUIRect iconRect(std::round(listClip.x + (listClip.width - kIconSize) * 0.5f), std::round(y),
                               kIconSize, kIconSize);
        icon->setBounds(iconRect);
        icon->setColor(themeManager.getColor("textSecondary").withAlpha(0.30f));
        icon->onRender(renderer);
        y += kIconSize + kIconGap;
    }
    renderer.drawTextCentered(title, NUIRect(listClip.x, y, listClip.width, kTitleH), titleFont,
                              themeManager.getColor("textPrimary").withAlpha(0.66f));
    y += kTitleH + kTitleGap;
    const NUIColor hintColor = themeManager.getColor("textSecondary").withAlpha(0.5f);
    for (const auto& line : hintLines) {
        renderer.drawTextCentered(line, NUIRect(listClip.x, y, listClip.width, kHintLineH), hintFont, hintColor);
        y += kHintLineH;
    }
    renderer.clearClipRect();
}

bool FileBrowser::searchQueryIsEmpty() const {
    return !searchInput_ || searchInput_->getText().empty();
}

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
    const float searchTrailing = searchQueryIsEmpty() ? 34.0f : 60.0f;
    layout.search = NUIRect(bounds.x + 26.0f, bounds.y + 4.0f,
                            std::max(0.0f, effectiveW - searchTrailing), searchH - 8.0f);
    layout.navPane = NUIRect(bounds.x, contentY, navW, contentH);
    layout.listHeader = NUIRect(listX, contentY, listW, BROWSER_LIST_HEADER_H);
    const float previewH = previewPanelVisible_ ? std::min(kPreviewPanelHeight, contentH) : 0.0f;
    layout.list = NUIRect(listX, contentY + BROWSER_LIST_HEADER_H, listW,
                          std::max(0.0f, contentH - BROWSER_LIST_HEADER_H - previewH));
    const float chromeY = layout.listHeader.y + 5.0f;
    layout.backButton = NUIRect(layout.listHeader.x + 5.0f, chromeY, 22.0f, 24.0f);
    layout.forwardButton = NUIRect(layout.backButton.right() + 2.0f, chromeY, 22.0f, 24.0f);
    layout.upButton = NUIRect(layout.forwardButton.right() + 2.0f, chromeY, 22.0f, 24.0f);
    // The sort control names its key ("Name", "Date", ...) once the list has
    // room; narrower lists keep the bare glyph.
    const float sortW = listW >= 300.0f ? 62.0f : 22.0f;
    layout.sortButton = NUIRect(layout.listHeader.right() - 5.0f - sortW, chromeY, sortW, 24.0f);
    layout.filterButton = NUIRect(layout.sortButton.x - 24.0f, chromeY, 22.0f, 24.0f);
    layout.pathLabel = NUIRect(layout.upButton.right() + 7.0f, chromeY,
                               std::max(0.0f, layout.filterButton.x - layout.upButton.right() - 11.0f), 24.0f);
    // Clear-query button only. The filter control that used to share this slot
    // was a duplicate of the funnel in the list header below (spec 2 §3), and
    // the two used different icons for the same menu.
    //
    // An empty query leaves no button and no reserved gap: layout.search widens
    // to the full row instead, so nothing unexplained is left behind.
    layout.searchActionButton = searchQueryIsEmpty()
                                    ? NUIRect()
                                    : NUIRect(layout.searchBar.right() - 30.0f,
                                              layout.searchBar.y + (layout.searchBar.height - 26.0f) * 0.5f,
                                              26.0f, 26.0f);
    layout.navWidth = navW;
    return layout;
}

float FileBrowser::getNavPaneWidth() const {
    NUIRect bounds = getBounds();
    const float effectiveW = bounds.width;
    return computeNavigationWidth(effectiveW);
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

void FileBrowser::renderListHeader(NUIRenderer& renderer, const BrowserLayout& layout) {
    auto& themeManager = NUIThemeManager::getInstance();
    const NUIColor headerBg = themeManager.getColor("backgroundSecondary").darkened(0.03f);
    const NUIColor border = themeManager.getColor("border").withAlpha(0.38f);
    const NUIColor text = themeManager.getColor("textPrimary");
    const NUIColor muted = themeManager.getColor("textSecondary");
    const NUIColor accent = themeManager.getColor("accentPrimary");
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

    const bool canNavigateUp = isShowingListing() ||
                               (!currentPath_.empty() &&
                                std::filesystem::path(currentPath_).parent_path() != std::filesystem::path(currentPath_) &&
                                !std::filesystem::path(currentPath_).parent_path().empty());
    const float upCx = layout.upButton.x + layout.upButton.width * 0.5f;
    const float upCy = layout.upButton.y + layout.upButton.height * 0.5f;
    const NUIColor upColor = muted.withAlpha(canNavigateUp ? 0.78f : 0.24f);
    renderer.drawLine({upCx - 4.0f, upCy + 2.0f}, {upCx, upCy - 2.0f}, 1.5f, upColor);
    renderer.drawLine({upCx, upCy - 2.0f}, {upCx + 4.0f, upCy + 2.0f}, 1.5f, upColor);
    renderer.drawLine({upCx, upCy - 2.0f}, {upCx, upCy + 5.0f}, 1.5f, upColor);

    std::string location = "Library";
    if (isShowingListing()) {
        location = listingTitle_;
    } else if (!currentPath_.empty()) {
        std::filesystem::path p(currentPath_);
        location = p.filename().string();
        if (location.empty()) location = currentPath_;
    }
    location = ellipsizeMiddle(renderer, location, themeProps.fontSizeS, layout.pathLabel.width);
    renderer.drawText(location,
                      {layout.pathLabel.x, std::round(renderer.calculateTextY(layout.pathLabel, themeProps.fontSizeS))},
                      themeProps.fontSizeS, text.withAlpha(0.88f));

    renderer.drawLine({layout.listHeader.x, layout.listHeader.bottom()},
                      {layout.listHeader.right(), layout.listHeader.bottom()},
                      1.0f, border);

    const bool filterActive = isFilterActive();
    const NUIColor controlColor = filterActive ? accent.withAlpha(0.94f) : muted.withAlpha(0.62f);
    const float filterCx = layout.filterButton.x + layout.filterButton.width * 0.5f;
    const float filterCy = layout.filterButton.y + layout.filterButton.height * 0.5f;
    renderer.drawLine({filterCx - 5.0f, filterCy - 4.0f}, {filterCx + 5.0f, filterCy - 4.0f}, 1.2f, controlColor);
    renderer.drawLine({filterCx - 3.0f, filterCy}, {filterCx + 3.0f, filterCy}, 1.2f, controlColor);
    renderer.drawLine({filterCx - 1.0f, filterCy + 4.0f}, {filterCx + 1.0f, filterCy + 4.0f}, 1.2f, controlColor);
    if (filterActive) renderer.fillCircle({layout.filterButton.right() - 5.0f, layout.filterButton.y + 5.0f}, 2.0f, accent);

    const NUIColor sortColor = muted.withAlpha(0.62f);
    if (layout.sortButton.width > 30.0f) {
        const char* sortLabel = "Name";
        switch (sortMode_) {
            case SortMode::Name: sortLabel = "Name"; break;
            case SortMode::Type: sortLabel = "Type"; break;
            case SortMode::Size: sortLabel = "Size"; break;
            case SortMode::Modified: sortLabel = "Date"; break;
            case SortMode::Length: sortLabel = "Length"; break;
            case SortMode::Bpm: sortLabel = "BPM"; break;
        }
        const NUIRect labelRect(layout.sortButton.x + 6.0f, layout.sortButton.y, layout.sortButton.width - 28.0f,
                                layout.sortButton.height);
        renderer.drawText(sortLabel,
                          {labelRect.x, std::round(renderer.calculateTextY(labelRect, themeProps.fontSizeXS))},
                          themeProps.fontSizeXS, muted.withAlpha(0.86f));
    }
    const float sortCx = layout.sortButton.right() - 11.0f;
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
    // Search row sits ON the surface (DESIGN.md rule 4: no recessed dark
    // well); the single quiet divider below it explains the boundary.
    renderer.drawLine({browserLayout.searchBar.x, browserLayout.searchBar.bottom() - 1.0f},
                      {browserLayout.searchBar.right(), browserLayout.searchBar.bottom() - 1.0f},
                      1.0f, themeManager.getColor("border").withAlpha(0.30f));

    // Flat square border — the curved grey top treatment is gone (0.7.0 triage).
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
        case ChromeAction::ClearSearch: chromeHover = layout.searchActionButton; break;
        case ChromeAction::None: break;
    }
    if (!chromeHover.isEmpty()) {
        renderer.fillRoundedRect(chromeHover, themeProps.radiusS, NUIColor::white().withAlpha(0.07f));
    }
}

void FileBrowser::onRender(NUIRenderer& renderer) {
    AESTRA_ZONE("FileBrowser_Render");

    // Rename editors that finished last frame: safe to detach now, outside
    // their own callbacks and outside any child iteration.
    for (auto& retired : retiredNavEditors_) removeChild(retired);
    retiredNavEditors_.clear();

    if (!isVisible()) return;

    NUIRect bounds = getBounds();
    if (bounds.isEmpty()) return;

    // Specialized views (Plugins, Patterns, future browser modes) share this
    // geometry even when the File Browser's static FBO cache is reused.
    updateContentViews();

    // FBO Caching Logic
    //
    // The uncached branch draws the static content and FALLS THROUGH. It used
    // to `return`, and everything below this point — the hover overlays, the
    // children, the search icon and the search action icons — therefore never
    // ran without the cache. The widget cache is disabled on Linux (V8-C2), so
    // on the development platform that trailing block had never executed once:
    // the "missing" search icon was drawing correctly into a branch nobody
    // reached.
    //
    // TrackManagerUIRender carries the same fix and states the rule: the cache
    // is a performance optimisation, so losing it must cost frame rate, never
    // content. This is that rule applied to the second consumer.
    auto* renderCache = renderer.getRenderCache();
    const bool cacheUsable = (renderCache != nullptr && renderCache->isEnabled());

    if (!cacheUsable) {
        renderStaticContent(renderer, bounds);
    } else {
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
    }  // end cached branch — everything below runs either way

    // Hover washes render every frame on top of the content — this is what
    // lets hover changes skip cache rebuilds entirely.
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
        if (m_searchIcon) {
            m_searchIcon->setBounds({search.x + 6.0f, search.y + (search.height - 15.0f) * 0.5f, 15.0f, 15.0f});
            m_searchIcon->setColor(themeManager.getColor("textSecondary").withAlpha(0.72f));
            m_searchIcon->onRender(renderer);
        }

        const NUIColor iconColor = themeManager.getColor("textSecondary").withAlpha(0.58f);
        const float cy = search.y + search.height * 0.5f;

        if (!layout.searchActionButton.isEmpty()) {
            const float fx = layout.searchActionButton.x + layout.searchActionButton.width * 0.5f;
            renderer.drawLine({fx - 4.0f, cy - 4.0f}, {fx + 4.0f, cy + 4.0f}, 1.3f, iconColor);
            renderer.drawLine({fx + 4.0f, cy - 4.0f}, {fx - 4.0f, cy + 4.0f}, 1.3f, iconColor);
        }
    }

    // Loading spinner removed - now handled by FilePreviewPanel
}

void FileBrowser::onUpdate(double deltaTime) {
    if (libraryIndex_ && libraryIndex_->revision() != seenIndexRevision_) {
        indexRefreshCooldown_ -= deltaTime;
        if (indexRefreshCooldown_ <= 0.0) {
            seenIndexRevision_ = libraryIndex_->revision();
            indexRefreshCooldown_ = 0.3; // coalesce publishes during a crawl
            if (searchWholeLibrary_ && !isShowingListing() && searchInput_ && !searchInput_->getText().empty()) {
                const std::string keep = selectedFile_ ? selectedFile_->path : std::string();
                applyFilter();
                if (!keep.empty()) selectFile(keep);
            }
        }
    }

    if (auto chosen = folderPicker_.takeResult(); chosen && !chosen->empty()) {
        addPlace(*chosen);
        navigateTo(*chosen);
        activeNavAction_ = BrowserNavAction::CustomPlace;
        activeNavPath_ = currentPath_;
        invalidateCache();
    }

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
        searchInput_->setBackgroundColor(themeManager.getColor("backgroundPrimary"));
        searchInput_->setBorderColor(themeManager.getColor("borderSubtle").withAlpha(0.62f));
        searchInput_->setFocusedBorderColor(themeManager.getColor("focusRing"));
        searchInput_->setPlaceholderColor(themeManager.getColor("textSecondary").withAlpha(0.56f));
        searchInput_->setJustification(NUITextInput::Justification::Left);
        searchInput_->setPadding(4.0f);
        searchInput_->setBorderRadius(5.0f);
        searchInput_->setBorderWidth(1.0f);
    }

    itemHeight_ = BROWSER_LIST_ROW_H;
    float listHeight = browserLayout.list.height;

    visibleItems_ = static_cast<int>(listHeight / itemHeight_);
    visibleItems_ = std::max(1, visibleItems_);

    // Update scrollbar dimensions
    // One gutter width for every scrollbar in the DAW (spec 2 §2); the thumb
    // insets within it, so a narrower gutter would leave no pill to grab.
    scrollbarTrackHeight_ = std::max(0.0f, listHeight);
    scrollbarWidth_ = AestraUI::kOverlayScrollbarThickness;

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

    if (handleChromeMouse(event, browserLayout)) {
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

    if (handleActiveDragMouse(event)) {
        return true;
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

    if (handleDragInitiation(event, view)) {
        return true;
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

    if (handleWheelScroll(event, browserLayout, mouseInside, view)) {
        return true;
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

    // Scrollbar visibility for this event (list overflow)
    const float contentHeight = view.size() * itemHeight;
    const float maxScroll = std::max(0.0f, contentHeight - scrollbarTrackHeight_);
    const bool needsScrollbar = maxScroll > 0.0f;

    updateBreadcrumbHover(event);

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

    if (handleListMouse(event, view, browserLayout)) {
        return true;
    }

    if (mouseInside && event.pressed && event.button == NUIMouseButton::Right) {
        hidePopupMenu();
        return true;
    }

    return false;
}

bool FileBrowser::handleChromeMouse(const NUIMouseEvent& event, const BrowserLayout& browserLayout) {
    ChromeAction newChromeAction = ChromeAction::None;
    if (!event.cursorCaptured) {
        if (browserLayout.backButton.contains(event.position)) newChromeAction = ChromeAction::Back;
        else if (browserLayout.forwardButton.contains(event.position)) newChromeAction = ChromeAction::Forward;
        else if (browserLayout.upButton.contains(event.position)) newChromeAction = ChromeAction::Up;
        else if (browserLayout.filterButton.contains(event.position)) newChromeAction = ChromeAction::Filter;
        else if (browserLayout.sortButton.contains(event.position)) newChromeAction = ChromeAction::Sort;
        else if (!browserLayout.searchActionButton.isEmpty() &&
                 browserLayout.searchActionButton.contains(event.position))
            newChromeAction = ChromeAction::ClearSearch;
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
            case ChromeAction::ClearSearch:
                if (searchInput_) searchInput_->clear();
                break;
            case ChromeAction::None: break;
        }
        return true;
    }
    return false;
}

bool FileBrowser::handleActiveDragMouse(const NUIMouseEvent& event) {
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
    return false;
}

bool FileBrowser::handleDragInitiation(const NUIMouseEvent& event, const std::vector<const FileItem*>& view) {
    auto& dragManager = NUIDragDropManager::getInstance();
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
    return false;
}

bool FileBrowser::handleWheelScroll(const NUIMouseEvent& event, const BrowserLayout& browserLayout, bool mouseInside, const std::vector<const FileItem*>& view) {
    const float itemHeight = BROWSER_LIST_ROW_H;
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
    return false;
}

void FileBrowser::updateBreadcrumbHover(const NUIMouseEvent& event) {
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
}

bool FileBrowser::handleListMouse(const NUIMouseEvent& event, const std::vector<const FileItem*>& view, const BrowserLayout& browserLayout) {
    auto& themeManager = NUIThemeManager::getInstance();
    const float itemHeight = BROWSER_LIST_ROW_H;
    const float listY = browserLayout.list.y;
    const float listHeight = browserLayout.list.height;
    const float scrollbarGutter = scrollbarWidth_ + themeManager.getSpacing("xs");
    const float listX = browserLayout.list.x;
    const float listW = std::max(0.0f, browserLayout.list.width - scrollbarGutter);
	    // Check if click is in file list area
        bool isInsideList = (event.position.x >= listX && event.position.x <= listX + listW &&
                             event.position.y >= listY && event.position.y <= listY + listHeight);

        if (!event.cursorCaptured && !isInsideList) {
            // Outside list area - clear hover and tooltip
            if (hoveredIndex_ != -1) {
                hoveredIndex_ = -1;
                setDirty(true); // hover overlay only — no cache rebuild
            }
            if (m_platformBridge) m_platformBridge->setCursorStyle(NUICursorStyle::Arrow);
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

            // Cursor affordance aligned with the actual drag initiation gate
            // (handleDragInitiation: non-directory, non-placeholder, allowed
            // files only): Grab on rows that can start a drag, Hand on other
            // selectable rows, Arrow elsewhere.
            if (m_platformBridge) {
                const FileItem* hoveredItem =
                    hoveredIndex_ >= 0 && hoveredIndex_ < static_cast<int>(view.size()) ? view[hoveredIndex_] : nullptr;
                const bool canDrag = hoveredItem && !hoveredItem->isDirectory && !hoveredItem->isPlaceholder &&
                                     FileFilter::isAllowed(hoveredItem->path);
                m_platformBridge->setCursorStyle(canDrag ? NUICursorStyle::Grab
                                                         : hoveredItem ? NUICursorStyle::Hand : NUICursorStyle::Arrow);
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

        // NOTE: type-to-search was removed deliberately. A printable key reaching the
        // browser used to call searchInput_->setFocused(true) and forward the character,
        // which meant that once the browser was anywhere in the focus chain every letter
        // the user typed was swallowed into the search box — letters are musical typing
        // and shortcuts in this app, so search would "start typing" seemingly at random.
        // It also double-entered the first character: the char was forwarded manually AND
        // then delivered again by the normal charCallback to the now-focused input
        // ("hello" arrived as "hhello"). Search is now focused explicitly only, via
        // Ctrl+F (above) or by clicking the field. Note the clear button is NOT a
        // focus path — ChromeAction::ClearSearch empties the query, and that is all
        // it does.
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
    if (m_platformBridge) m_platformBridge->setCursorStyle(NUICursorStyle::Arrow);
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

    // A listing (Favorites / Collection) sits on top of currentPath_, so going
    // "to" the folder it was opened from is still a real navigation.
    const bool wasListing = isShowingListing();
    listingKind_ = ListingKind::None;
    listingTitle_.clear();
    listingTag_.clear();
    if (wasListing && (activeNavAction_ == BrowserNavAction::Favorites ||
                       activeNavAction_ == BrowserNavAction::Collection ||
                       activeNavAction_ == BrowserNavAction::CurrentProject)) {
        // Leaving a list for a folder: light that folder's place row, if any.
        activeNavAction_ = BrowserNavAction::CustomPlace;
        activeNavPath_ = targetPath;
    }

    if (currentPath_ == targetPath && !wasListing) {
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

    if (isShowingListing()) {
        reissueListing();
        return;
    }

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
    if (isShowingListing()) {
        // Up out of Favorites / a collection returns to the folder underneath.
        exitListing();
        return;
    }

    std::filesystem::path current(currentPath_);
    std::filesystem::path parent = current.parent_path();
    if (parent.empty() || parent == current) return;

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

// -----------------------------------------------------------------------------
// Places
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// User collections
// -----------------------------------------------------------------------------


NUIColor FileBrowser::collectionColor(const std::string& name) const {
    static constexpr uint32_t kPalette[] = {0x7c3aed, 0xf97316, 0x22c55e, 0x3b82f6,
                                            0xec4899, 0x14b8a6, 0xeab308, 0xef4444};
    const auto it = std::find(collections_.begin(), collections_.end(), name);
    if (it == collections_.end()) return NUIColor(0.42f, 0.42f, 0.42f, 1.0f); // a plain tag
    const size_t index = static_cast<size_t>(it - collections_.begin());
    return NUIColor::fromHex(kPalette[index % (sizeof(kPalette) / sizeof(kPalette[0]))]);
}

void FileBrowser::addTaggingSubmenus(const std::string& path) {
    auto collectionsMenu = std::make_shared<NUIContextMenu>();
    for (const auto& name : collections_) {
        collectionsMenu->addCheckbox(name, hasTag(path, name), [this, path, name](bool) { toggleTag(path, name); });
    }
    if (!collections_.empty()) collectionsMenu->addSeparator();
    collectionsMenu->addItem("New Collection...", [this, path]() {
        const std::string name = createUntitledCollection();
        toggleTag(path, name);
        beginCollectionRename(name);
    });
    popupMenu_->addSubmenu("Add to Collection", collectionsMenu);

    // Plain tags: the presets plus any tag already in use that is not a collection.
    std::vector<std::string> tags = {"Bass", "Vocal", "FX", "Loops", "One-shots", "Synth", "Pads", "Ambience"};
    for (const auto& t : getAllTagsSorted()) {
        if (std::find(collections_.begin(), collections_.end(), t) != collections_.end()) continue;
        if (std::find(tags.begin(), tags.end(), t) == tags.end()) tags.push_back(t);
    }
    auto tagsMenu = std::make_shared<NUIContextMenu>();
    for (const auto& tag : tags) {
        tagsMenu->addCheckbox(tag, hasTag(path, tag), [this, path, tag](bool) { toggleTag(path, tag); });
    }
    popupMenu_->addSubmenu("Tags", tagsMenu);
}

std::vector<std::string> FileBrowser::indexRoots() const {
    std::vector<std::string> roots;
    if (!rootPath_.empty()) roots.push_back(rootPath_);
    for (const auto& place : customPlacePaths_) roots.push_back(place);
    // Where downloaded and collected sounds usually live. Not Home, Desktop or
    // Documents: too broad to crawl on every launch.
    for (const auto& place : systemPlaces_) {
        if (place.label == "Music" || place.label == "Downloads") roots.push_back(place.path);
    }
    return roots;
}

void FileBrowser::showCurrentProject() {
    std::vector<std::string> paths;
    if (projectFilesProvider_) paths = projectFilesProvider_();
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    activeNavAction_ = BrowserNavAction::CurrentProject;
    beginListing(ListingKind::Project, "Current Project", std::move(paths));
}

// -----------------------------------------------------------------------------
// Listing views (Favorites / Collections)
// -----------------------------------------------------------------------------

std::vector<std::string> FileBrowser::pathsWithTag(const std::string& tag) const {
    std::vector<std::string> paths;
    for (const auto& [pathKey, tags] : tagsByPath_) {
        if (std::find(tags.begin(), tags.end(), tag) != tags.end()) paths.push_back(pathKey);
    }
    // tagsByPath_ is unordered; give the scan a stable input order.
    std::sort(paths.begin(), paths.end());
    return paths;
}

void FileBrowser::beginListing(ListingKind kind, const std::string& title, std::vector<std::string> paths) {
    if (kind != ListingKind::Collection) listingTag_.clear();
    listingKind_ = kind;
    listingTitle_ = title;
    // The listing already IS the collection; a tag filter on top would only
    // hide the children of tagged folders the user expands.
    activeTagFilter_.clear();

    rootItems_.clear();
    displayItems_.clear();
    cachedView_.clear();
    filteredFiles_.clear();
    selectedFile_ = nullptr;
    selectedIndex_ = -1;
    selectedIndices_.clear();
    lastShiftSelectIndex_ = -1;
    hoveredIndex_ = -1;
    dragPotential_ = false;
    dragSourceIndex_ = -1;
    scanError_.clear();
    targetScrollOffset_ = 0.0f;
    scrollOffset_ = 0.0f;
    scrollVelocity_ = 0.0f;

    scanGeneration_.fetch_add(1, std::memory_order_acq_rel);
    {
        std::lock_guard<std::mutex> lock(scanMutex_);
        scanTasks_.clear();
        scanResults_.clear();
    }

    scanningRoot_ = true;
    ensureScanWorker();
    ScanTask task;
    task.kind = ScanKind::Listing;
    task.paths = std::move(paths);
    task.generation = scanGeneration_.load(std::memory_order_acquire);
    {
        std::lock_guard<std::mutex> lock(scanMutex_);
        scanTasks_.push_back(std::move(task));
    }
    scanCv_.notify_one();

    updateScrollbarVisibility();
    viewDirty_ = true;
    invalidateCache();
}

void FileBrowser::reissueListing() {
    // Keep the scroll position and selection across a live update (e.g. the
    // user unfavorites one row while looking at Favorites).
    const float keepScroll = targetScrollOffset_;
    std::string keepSelection = selectedFile_ ? selectedFile_->path : std::string();

    switch (listingKind_) {
        case ListingKind::Favorites: showFavorites(); break;
        case ListingKind::Collection: showCollection(listingTag_); break;
        case ListingKind::Project: showCurrentProject(); break;
        case ListingKind::None: return;
    }

    pendingSelectionPath_ = std::move(keepSelection);
    targetScrollOffset_ = keepScroll;
    scrollOffset_ = keepScroll;
}

void FileBrowser::exitListing() {
    if (!isShowingListing()) return;
    // setCurrentPath() clears the listing, treats the same path as a real
    // navigation, and moves the nav highlight to the folder's place row.
    setCurrentPath(currentPath_);
    updateContentViews();
}

bool FileBrowser::isScanPending() const {
    std::lock_guard<std::mutex> lock(scanMutex_);
    return scanningRoot_ || !scanTasks_.empty() || !scanResults_.empty();
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

    persistState();
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

    persistState();
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
        } else if (!nonConstItem->isLoadingChildren) {
            // Folder contents can change while the app runs (users drop new
            // samples into their library), so re-scan on every expand. The
            // stale children stay visible until the fresh listing lands.
            nonConstItem->isLoadingChildren = true;
            enqueueScan(ScanKind::Folder, nonConstItem->path, nonConstItem->depth + 1);
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

	    // Natural, case-insensitive: "kick 2" < "Kick 10" < "snare". Names that
	    // only differ in case fall through to the path so the order is total.
	    const auto tieBreak = [&]() {
	        const int byName = BrowserLibrary::naturalCompare(a.name, b.name);
	        if (byName != 0) {
	            return sortAscending_ ? (byName < 0) : (byName > 0);
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
	        case SortMode::Length:
	            if (a.durationSec != b.durationSec) {
	                return sortAscending_ ? (a.durationSec < b.durationSec) : (a.durationSec > b.durationSec);
	            }
	            return tieBreak();
	        case SortMode::Bpm:
	            if (a.detectedBpm != b.detectedBpm) {
	                return sortAscending_ ? (a.detectedBpm < b.detectedBpm) : (a.detectedBpm > b.detectedBpm);
	            }
	            return tieBreak();
	        case SortMode::Modified:
	            if (a.modifiedTime != b.modifiedTime) {
	                return sortAscending_ ? (a.modifiedTime < b.modifiedTime) : (a.modifiedTime > b.modifiedTime);
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
        case FileType::OggFile:
            return oggFileIcon_;
        case FileType::MidiFile:
            return midiFileIcon_;
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
        drawListEmptyState(renderer, listClip, folderIcon_, "Scanning library",
                           "Results appear as they load");
        return;
    }

    if (view.empty()) {
        if (!scanError_.empty()) {
            drawListEmptyState(renderer, listClip, folderIcon_, "Folder unavailable",
                               "Press F5 to retry, or choose another location");
        } else if (isFilterActive()) {
            drawListEmptyState(renderer, listClip, unknownFileIcon_, "No matches",
                               isLibraryIndexing() && searchWholeLibrary_
                                   ? "Still indexing your library \xe2\x80\x94 results appear as it finds them"
                                   : "Press Esc to clear the search and filters");
        } else if (listingKind_ == ListingKind::Favorites) {
            drawListEmptyState(renderer, listClip, folderIcon_, "No favorites yet",
                               "Right-click any sound or folder and choose Add to Favorites");
        } else if (listingKind_ == ListingKind::Collection) {
            drawListEmptyState(renderer, listClip, folderIcon_, "Nothing in " + listingTitle_ + " yet",
                               "Right-click any sound or folder and choose Add to Collection");
        } else if (listingKind_ == ListingKind::Project) {
            drawListEmptyState(renderer, listClip, folderIcon_, "No audio in this project yet",
                               "Sounds you bring into the arrangement are listed here");
        } else if (!currentPath_.empty() && !rootPath_.empty() &&
                   normalizedPathForCompare(currentPath_) != normalizedPathForCompare(rootPath_) &&
                   isPathUnderRoot(currentPath_, rootPath_)) {
            // Both paths must be real: setCurrentPath() clears currentPath_ when a path
            // fails to resolve, and that is not a subfolder — telling the user to "go up"
            // out of a folder they are not in would be worse than the generic copy.
            // An empty SUBFOLDER is not an empty library. currentPath_ is persisted
            // across sessions (ui_state.json fileBrowser.lastPath), so a user who last
            // browsed into an empty category folder reopens the app to what reads as
            // "you have no content at all" — with nothing pointing back to the library.
            const std::string rootName = std::filesystem::path(rootPath_).filename().string();
            drawListEmptyState(renderer, listClip, folderIcon_, "This folder is empty",
                               rootName.empty() ? "Go up to see the rest of your library"
                                                : ("Go up to " + rootName + " to see your library"));
        } else {
            drawListEmptyState(renderer, listClip, folderIcon_, "Nothing here yet",
                               "Audio, MIDI, and Aestra projects show up here");
        }
        return;
    }

    const float itemHeight = BROWSER_LIST_ROW_H;
    const int firstVisibleIndex = std::max(0, static_cast<int>(scrollOffset_ / itemHeight));
    const int lastVisibleIndex = std::min(static_cast<int>(view.size()),
        static_cast<int>((scrollOffset_ + listClip.height) / itemHeight) + 2);

    renderer.setClipRect(listClip);

    const NUIColor oddRow = themeManager.getColor("backgroundSecondary").withAlpha(0.04f);
    const NUIColor evenRow = themeManager.getColor("backgroundPrimary");
    const NUIColor selectedRow = themeManager.getColor("accentPrimary").withAlpha(previewPanelVisible_ ? 0.0f
                                                                                                       : 0.065f);
    const NUIColor secondarySelectedRow = themeManager.getColor("accentPrimary").withAlpha(0.035f);
    const NUIColor text = themeManager.getColor("textPrimary").withAlpha(0.82f);
    const NUIColor folderText = themeManager.getColor("textPrimary").withAlpha(0.92f);
    const NUIColor muted = themeManager.getColor("textSecondary").withAlpha(0.56f);
    const auto& themeProps = themeManager.getCurrentTheme();
    const float labelFont = themeProps.fontSizeS;
    const float rowIndentStep = 18.0f;
    const auto sharedPrefixes = sharedDisplayPrefixes(view);

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
            // Centred in the row, like the chevron and the name beside it. A fixed "+5" only
            // centred a 24 px row; at the current row height the icon sat 3 px above its label,
            // so the label read as low (SPEC 3 §3.2, measured on screen).
            constexpr float ROW_ICON_SIZE = 14.0f;
            NUIRect iconRect(contentX, std::round(itemRect.y + (itemRect.height - ROW_ICON_SIZE) * 0.5f), ROW_ICON_SIZE,
                             ROW_ICON_SIZE);
            icon->setBounds(iconRect);
            icon->setColor(item->isDirectory ? folderText.withAlpha(0.78f) : muted);
            icon->onRender(renderer);
        }
        contentX += 20.0f;

        const float maxTextWidth = std::max(0.0f, itemRect.right() - contentX - 12.0f);
        std::string displayName = item->name;
        bool prefixDropped = false;
        if (!item->isDirectory && !sharedPrefixes.empty()) {
            const size_t slash = item->path.find_last_of("/\\");
            const auto it = sharedPrefixes.find(slash == std::string::npos ? std::string() : item->path.substr(0, slash));
            if (it != sharedPrefixes.end() && it->second < displayName.size()) {
                displayName.erase(0, it->second);
                prefixDropped = true;
            }
        }
        if (renderer.measureText(displayName, labelFont).width > maxTextWidth) {
            displayName = ellipsizeEnd(renderer, displayName, labelFont, maxTextWidth);
            item->isTruncated = true;
        } else {
            // A dropped prefix counts as truncation, so hovering still shows the full name.
            item->isTruncated = prefixDropped;
        }

        const NUIColor itemTextColor = item->isPlaceholder
                                           ? muted.withAlpha(0.60f)
                                           : selected ? themeManager.getColor("textPrimary")
                                                      : item->isDirectory ? folderText : text;
        renderer.drawText(displayName, {contentX, std::round(renderer.calculateTextY(itemRect, labelFont))},
                          labelFont, itemTextColor);
        float afterNameX = contentX + renderer.measureText(displayName, labelFont).width;

        // A row from somewhere else (library search hit, Favorites, a
        // collection) names its folder, dimmed, so same-named files stay
        // tellable apart. Text, not a number column (see BPM note below).
        if (!item->isPlaceholder && item->depth == 0 && (isShowingListing() || isFilterActive())) {
            const std::filesystem::path parent = std::filesystem::path(item->path).parent_path();
            if (isShowingListing() || mapKeyForPath(parent.string()) != mapKeyForPath(currentPath_)) {
                const std::string folder = parent.filename().string();
                const float hintFont = themeProps.fontSizeXS;
                const float room = itemRect.right() - 12.0f - (afterNameX + 8.0f);
                if (!folder.empty() && room > 40.0f) {
                    const std::string hint = ellipsizeEnd(renderer, folder, hintFont, room);
                    renderer.drawText(hint, {afterNameX + 8.0f, std::round(renderer.calculateTextY(itemRect, hintFont))},
                                      hintFont, muted.withAlpha(0.62f));
                    afterNameX += 8.0f + renderer.measureText(hint, hintFont).width;
                }
            }
        }

        // BPM stays in metadata (search/drag) but is not shown as a row
        // column — owner direction: no number on the right of audio rows.

        // Tag dots (after name)
        if (!item->isDirectory) {
            const std::string key = mapKeyForPath(item->path);
            auto tagIt = tagsByPath_.find(key);
            if (tagIt != tagsByPath_.end() && !tagIt->second.empty()) {
                const float tagDotStartX = afterNameX + 8.0f;
                float dotX = tagDotStartX;
                const float rowCenterY = itemRect.y + itemRect.height * 0.5f;
                for (const auto& tag : tagIt->second) {
                    const NUIColor dotColor = collectionColor(tag);
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
		    persistState();

		    if (listingKind_ == ListingKind::Collection && listingTag_ == tag) {
		        reissueListing(); // an untagged row leaves the open collection
		    } else if (isFilterActive()) {
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
		            persistState();
		            if (listingKind_ == ListingKind::Favorites) reissueListing();
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

    if (!folderPicker_.isPending()) {
        popupMenu_->addItem("Choose a Folder...", [this]() {
            folderPicker_.start(
                [](Aestra::IPlatformUtils& utils) { return utils.selectFolderDialog("Add a Folder to Places"); });
        });
    }

    // "Current folder" is meaningless while a Favorites / Collection list is up.
    if (!isShowingListing() && !currentPath_.empty()) {
        std::string name = std::filesystem::path(currentPath_).filename().string();
        if (name.empty()) name = currentPath_;
        if (isPlace(currentPath_)) {
            popupMenu_->addItem("Remove \"" + name + "\" from Places", [this]() { removePlace(currentPath_); });
        } else {
            popupMenu_->addItem("Add \"" + name + "\" to Places", [this]() { addPlace(currentPath_); });
        }
        popupMenu_->addItem(isFavorite(currentPath_) ? "Remove \"" + name + "\" from Favorites"
                                                     : "Add \"" + name + "\" to Favorites",
                            [this]() { toggleFavorite(currentPath_); });
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
    popupMenu_->addRadioItem("Date Modified", "sort_mode", sortMode_ == SortMode::Modified, [this]() { setSortMode(SortMode::Modified); });
    popupMenu_->addRadioItem("Length", "sort_mode", sortMode_ == SortMode::Length, [this]() { setSortMode(SortMode::Length); });
    popupMenu_->addRadioItem("BPM", "sort_mode", sortMode_ == SortMode::Bpm, [this]() { setSortMode(SortMode::Bpm); });
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

		    popupMenu_->addRadioItem("Search Whole Library", "search_scope", searchWholeLibrary_,
		                             [this]() { setSearchWholeLibrary(true); });
		    popupMenu_->addRadioItem("Search This Folder", "search_scope", !searchWholeLibrary_,
		                             [this]() { setSearchWholeLibrary(false); });
		    popupMenu_->addItem(isLibraryIndexing() ? "Indexing Library..." : "Rescan Library",
		                        [this]() { rescanLibrary(); });
		    popupMenu_->addSeparator();
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
		        if (isShowingListing()) {
		            popupMenu_->addItem("Show in Enclosing Folder", [this, path = item.path]() {
		                const std::filesystem::path parent = std::filesystem::path(path).parent_path();
		                if (parent.empty()) return;
		                setCurrentPath(parent.string());
		                pendingSelectionPath_ = path;
		            });
		        }
		        popupMenu_->addSeparator();

		        const bool fav = isFavorite(item.path);
		        popupMenu_->addItem(fav ? "Remove from Favorites" : "Add to Favorites",
		                            [this, path = item.path]() { toggleFavorite(path); invalidateCache(); });
		        popupMenu_->addItem(isPlace(item.path) ? "Remove from Places" : "Add to Places",
		                            [this, path = item.path]() {
		                                if (isPlace(path)) removePlace(path);
		                                else addPlace(path);
		                            });
        addTaggingSubmenus(item.path);
        popupMenu_->addSeparator();
        popupMenu_->addItem("Copy Path", [path = item.path, copyToClipboard]() { copyToClipboard(path); });
    } else {
	        // Navigate to containing folder
	        popupMenu_->addItem("Show in Enclosing Folder", [this, path = item.path]() {
	            std::filesystem::path p(path);
	            std::filesystem::path parent = p.parent_path();
	            if (!parent.empty()) {
	                setCurrentPath(parent.string());
	                // The folder scan is asynchronous: select once its rows exist.
	                pendingSelectionPath_ = path;
	            }
	        });

	        const bool isAudio =
	            item.type == FileType::AudioFile || item.type == FileType::MusicFile ||
	            item.type == FileType::WavFile || item.type == FileType::Mp3File || item.type == FileType::FlacFile;

		        popupMenu_->addItem(isFavorite(item.path) ? "Remove from Favorites" : "Add to Favorites",
		                            [this, path = item.path]() { toggleFavorite(path); });

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

        addTaggingSubmenus(item.path);

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
    const auto& view = getActiveView();
    // Use stored track height which is correctly calculated in onResize/onMouseEvent
    if (scrollbarTrackHeight_ <= 0.0f) {
        scrollbarTrackHeight_ = getBounds().height; // Rough calc
    }

    const float contentHeight = view.size() * itemHeight_;
    const float maxScroll = std::max(0.0f, contentHeight - scrollbarTrackHeight_);
    if (maxScroll <= 0.0f || view.empty()) return; // Content fits: no scrollbar at all.

    const BrowserLayout browserLayout = computeBrowserLayout();
    const NUIRect gutter(browserLayout.list.right() - scrollbarWidth_ - 2.0f, browserLayout.list.y,
                         scrollbarWidth_, scrollbarTrackHeight_);
    const NUIRect thumb(gutter.x, gutter.y + scrollbarThumbY_, gutter.width, scrollbarThumbHeight_);

    AestraUI::ScrollbarPaintState state;
    state.hovered = scrollbarHovered_;
    state.pressed = isDraggingScrollbar_;
    state.opacity = std::clamp(scrollbarOpacity_, 0.0f, 1.0f);
    AestraUI::drawOverlayScrollbar(renderer, gutter, thumb, state);
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
        scrollbarThumbHeight_ = std::max(AestraUI::kOverlayScrollbarMinThumb,
                                         (scrollbarTrackHeight_ / contentHeight) * scrollbarTrackHeight_);

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
    searchResults_.clear();
    const std::string rawQuery = searchInput_ ? searchInput_->getText() : "";
    const BrowserLibrary::SearchQuery query = BrowserLibrary::parseSearchQuery(rawQuery);
    const bool hasNameFilter = !query.text.empty();
    const bool hasMetaFilter = query.hasMetadataFilter();
    const bool hasTagFilter = !activeTagFilter_.empty();
    const bool hasQuickFilter = activeQuickFilter_ != QuickFilter::All;

    if (!hasNameFilter && !hasMetaFilter && !hasTagFilter && !hasQuickFilter) {
        // No filter active, display all root items
        updateDisplayList();
        selectedFile_ = nullptr;
        selectedIndex_ = -1;
        selectedIndices_.clear();
        updateScrollbarVisibility();
        invalidateCache();
        return;
    }

    // Score an item against every active filter; kNoMatch rejects it.
    // bpm:/len:/key: only ever match audio whose fact is known.
    const auto evaluate = [&](const FileItem& item) -> int {
        int score = 0;
        if (hasNameFilter) {
            std::string hay = item.name;
            std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) { return std::tolower(c); });
            score = BrowserLibrary::matchScore(query.text, hay);
            if (score == BrowserLibrary::kNoMatch) return BrowserLibrary::kNoMatch;
        }
        if (hasMetaFilter) {
            if (item.isDirectory) return BrowserLibrary::kNoMatch;
            if (query.bpmMax > 0 && (item.detectedBpm < query.bpmMin || item.detectedBpm > query.bpmMax)) {
                return BrowserLibrary::kNoMatch;
            }
            const bool lengthFilter = query.lenMin >= 0.0 || query.lenMax >= 0.0;
            if (lengthFilter && item.durationSec <= 0.0) return BrowserLibrary::kNoMatch;
            if (query.lenMin >= 0.0 && item.durationSec < query.lenMin) return BrowserLibrary::kNoMatch;
            if (query.lenMax >= 0.0 && item.durationSec > query.lenMax) return BrowserLibrary::kNoMatch;
            if (!query.key.empty() && item.musicalKey != query.key) return BrowserLibrary::kNoMatch;
        }
        if (hasTagFilter && !hasTag(item.path, activeTagFilter_)) return BrowserLibrary::kNoMatch;
        if (hasQuickFilter && !matchesQuickFilter(item)) return BrowserLibrary::kNoMatch;
        return score;
    };

    // 1. What is loaded here (including expanded subfolders).
    std::unordered_set<std::string> listedPaths;
    std::function<void(const std::vector<FileItem>&)> gather = [&](const std::vector<FileItem>& items) {
        for (const auto& item : items) {
            const int score = evaluate(item);
            if (score != BrowserLibrary::kNoMatch) {
                item.searchScore = score;
                filteredFiles_.push_back(&item);
                listedPaths.insert(item.path);
            }
            if (item.isDirectory && item.hasLoadedChildren) gather(item.children);
        }
    };
    gather(rootItems_);

    // 2. Everything else in the library, from the background index.
    const bool searchLibrary = (hasNameFilter || hasMetaFilter) && searchWholeLibrary_ && !isShowingListing() &&
                               libraryIndex_ != nullptr;
    if (searchLibrary) {
        if (const auto snapshot = libraryIndex_->snapshot()) {
            std::vector<std::pair<int, size_t>> hits;
            for (size_t i = 0; i < snapshot->size(); ++i) {
                const FileItem& entry = (*snapshot)[i];
                if (listedPaths.count(entry.path) != 0) continue;
                const int score = evaluate(entry);
                if (score != BrowserLibrary::kNoMatch) hits.emplace_back(score, i);
            }
            // Keep the best: a one-letter query must not build a 100k-row list.
            constexpr size_t kMaxLibraryHits = 500;
            if (hits.size() > kMaxLibraryHits) {
                std::nth_element(hits.begin(), hits.begin() + kMaxLibraryHits, hits.end(),
                                 [](const auto& a, const auto& b) { return a.first > b.first; });
                hits.resize(kMaxLibraryHits);
            }
            searchResults_.reserve(hits.size());
            for (const auto& [score, index] : hits) {
                searchResults_.push_back((*snapshot)[index]);
                searchResults_.back().depth = 0;
                searchResults_.back().searchScore = score;
            }
            for (const auto& result : searchResults_) filteredFiles_.push_back(&result);
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

bool FileBrowser::blurSearchIfPressOutside(const NUIPoint& pos) {
    bool cleared = false;
    if (navEditor_ && !navEditor_->getBounds().contains(pos)) {
        finishCollectionRename(true);
        cleared = true;
    }
    if (searchInput_ && searchInput_->isFocused() && !searchInput_->getBounds().contains(pos)) {
        searchInput_->setFocused(false);
        cleared = true;
    }
    return cleared;
}

// === PERSISTENT STATE SAVE/LOAD ===

// Library state file. Version 3 holds only what the user curated; v2 files
// (browser_settings.json) also carried view state, which is ignored here
// because UIState owns where the browser is looking.
bool FileBrowser::saveState(const std::string& filePath) const {
    if (filePath.empty()) return false;

    Aestra::JSON j = Aestra::JSON::object();
    j.set("version", Aestra::JSON(3.0));
    j.set("rootPath", Aestra::JSON(rootPath_));
    j.set("sortMode", Aestra::JSON(static_cast<double>(sortMode_)));
    j.set("sortAscending", Aestra::JSON(sortAscending_));
    j.set("searchWholeLibrary", Aestra::JSON(searchWholeLibrary_));

    Aestra::JSON favArr = Aestra::JSON::array();
    for (const auto& f : favoritesPaths_) favArr.push(Aestra::JSON(f));
    j.set("favorites", favArr);

    Aestra::JSON collectionsArr = Aestra::JSON::array();
    for (const auto& c : collections_) collectionsArr.push(Aestra::JSON(c));
    j.set("collections", collectionsArr);

    Aestra::JSON placesArr = Aestra::JSON::array();
    for (const auto& p : customPlacePaths_) placesArr.push(Aestra::JSON(p));
    j.set("customPlaces", placesArr);

    Aestra::JSON tagsObj = Aestra::JSON::object();
    for (const auto& [pathKey, tags] : tagsByPath_) {
        if (pathKey.empty() || tags.empty()) continue;
        Aestra::JSON tagArr = Aestra::JSON::array();
        for (const auto& t : tags) tagArr.push(Aestra::JSON(t));
        tagsObj.set(pathKey, tagArr);
    }
    j.set("tagsByPath", tagsObj);

    if (!Aestra::writeJSONAtomic(filePath, j)) {
        Aestra::Log::warning("[FileBrowser] Failed to save library state to: " + filePath);
        return false;
    }
    return true;
}

void FileBrowser::persistState() {
    if (!statePath_.empty()) saveState(statePath_);
}

bool FileBrowser::loadState(const std::string& filePath) {
    std::optional<Aestra::JSON> parsed = Aestra::readJSONStrict(filePath);
    if (!parsed) {
        // v1 wrote "key=value|value" lines. Import its lists; anything else
        // unreadable means "no saved library", never an error.
        std::ifstream legacy(filePath);
        std::string content((std::istreambuf_iterator<char>(legacy)), std::istreambuf_iterator<char>());
        if (content.find("currentPath=") != std::string::npos && content.find('{') == std::string::npos) {
            migrateLegacySettings(filePath);
            return true;
        }
        return false;
    }

    // Non-const on purpose: AestraJSON's const asArray()/asObject() return empty stubs.
    Aestra::JSON& j = *parsed;
    if (!j.isObject()) return false;

    const auto readStrings = [&](const char* key, std::vector<std::string>& out) {
        if (!j.has(key) || !j[key].isArray()) return false;
        out.clear();
        for (auto& v : j[key].asArray()) {
            if (v.isString() && !v.asString().empty()) out.push_back(v.asString());
        }
        return true;
    };

    std::vector<std::string> favorites;
    if (readStrings("favorites", favorites)) {
        favoritesPaths_.clear();
        for (const auto& f : favorites) {
            const std::string key = mapKeyForPath(f);
            if (std::find(favoritesPaths_.begin(), favoritesPaths_.end(), key) == favoritesPaths_.end()) {
                favoritesPaths_.push_back(key);
            }
        }
    }

    std::vector<std::string> collections;
    if (readStrings("collections", collections)) {
        collections_.clear();
        for (auto& c : collections) {
            const std::string name = trimName(c);
            if (!name.empty() && std::find(collections_.begin(), collections_.end(), name) == collections_.end()) {
                collections_.push_back(name);
            }
        }
    } // absent (pre-v3 file): keep the default collections

    std::vector<std::string> places;
    if (readStrings("customPlaces", places)) {
        customPlacePaths_.clear();
        for (const auto& p : places) {
            // Keep places that are offline right now (unplugged drive): they
            // come back when the drive does. Only de-duplicate.
            if (!isPlace(p)) customPlacePaths_.push_back(p);
        }
    }

    if (j.has("tagsByPath") && j["tagsByPath"].isObject()) {
        tagsByPath_.clear();
        for (auto& [pathKey, val] : j["tagsByPath"].asObject()) {
            if (pathKey.empty() || !val.isArray()) continue;
            std::vector<std::string> tags;
            for (auto& t : val.asArray()) {
                if (t.isString() && !t.asString().empty()) tags.push_back(t.asString());
            }
            if (!tags.empty()) tagsByPath_[mapKeyForPath(pathKey)] = std::move(tags);
        }
    }

    if (j.has("sortMode") && j["sortMode"].isNumber()) {
        const int mode = static_cast<int>(j["sortMode"].asNumber());
        if (mode >= static_cast<int>(SortMode::Name) && mode <= static_cast<int>(SortMode::Bpm)) {
            sortMode_ = static_cast<SortMode>(mode);
        }
    }
    if (j.has("sortAscending") && j["sortAscending"].isBool()) {
        sortAscending_ = j["sortAscending"].asBool();
    }
    if (j.has("searchWholeLibrary") && j["searchWholeLibrary"].isBool()) {
        searchWholeLibrary_ = j["searchWholeLibrary"].asBool();
    }

    if (j.has("rootPath") && j["rootPath"].isString()) {
        const std::string loadedRoot = j["rootPath"].asString();
        std::error_code ec;
        if (!loadedRoot.empty() && std::filesystem::is_directory(loadedRoot, ec)) {
            rootPath_ = canonicalOrNormalized(std::filesystem::path(loadedRoot)).string();
        }
    }

    sortFiles();
    updateDisplayList();
    if (isFilterActive()) applyFilter();
    viewDirty_ = true;
    invalidateCache();
    Aestra::Log::info("[FileBrowser] Library state loaded from: " + filePath);
    return true;
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
                if (!token.empty()) favoritesPaths_.push_back(mapKeyForPath(token));
        }
    }
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
