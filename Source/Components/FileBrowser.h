// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../AestraUI/Core/NUIComponent.h"
#include "NUIIcon.h"
#include "../AestraUI/Core/NUIDragDrop.h"
#include "BrowserLibrary.h"
#include "AestraFileDialog.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace AestraUI {

class NUIContextMenu;
class BrowserLibraryIndex;
class NUITextInput;
class NUIPlatformBridge;

/**
 * File type enumeration for proper icon display
 */
/**
 * File type enumeration for proper icon display
 */
enum class FileType {
    Folder,
    AudioFile,
    MusicFile,
    ProjectFile,
    WavFile,
    Mp3File,
    FlacFile,
    OggFile,
    MidiFile,
    Unknown
};

/**
 * Smart File Filter - Whitelist approach
 */
struct FileFilter {
    static const std::unordered_set<std::string> audioExtensions;
    static const std::unordered_set<std::string> projectExtensions;

    static bool isAllowed(const std::string& path);
    static FileType getType(const std::string& path, bool isDir);
};

/**
 * File item structure
 */
struct FileItem {
    std::string name;
    std::string path;
    FileType type;
    bool isDirectory;
    size_t size;
    std::string lastModified;
    int64_t modifiedTime = 0; // filesystem clock ticks; 0 = unknown. Sort key only.
    
    // Tree view support
    bool isExpanded = false;
    bool hasLoadedChildren = false;
    bool isLoadingChildren = false;
    bool isPlaceholder = false;
    std::vector<FileItem> children;
    int depth = 0;
    
    // Cache for performance
    mutable std::string cachedDisplayName;
    mutable std::string cachedSizeStr;
    mutable bool cacheValid = false;
    mutable bool isTruncated = false;
    mutable int searchScore = 0;
    int detectedBpm = 0;          // filename, else a WAV ACID tempo; 0 = unknown
    double durationSec = 0.0;     // from the header; 0 = unknown
    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    std::string musicalKey;       // from the filename ("Am", "F#"); "" = unknown

    FileItem()
        : type(FileType::Unknown), isDirectory(false), size(0) {}

    FileItem(const std::string& n, const std::string& p, FileType t, bool isDir, size_t s = 0, const std::string& modified = "")
        : name(n), path(p), type(t), isDirectory(isDir), size(s), lastModified(modified) {}
        
    void invalidateCache() const { cacheValid = false; }
};

/**
 * File Browser Component
 * 
 * A modern file browser with icons, sorting, and navigation.
 * Integrates with AestraUI theme system for consistent styling.
 */
class FileBrowser : public NUIComponent {
public:
    FileBrowser();
    ~FileBrowser() override;
    
    // Component interface
    void onRender(NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    bool onKeyEvent(const NUIKeyEvent& event) override;
    void onMouseLeave() override;

    /** @brief Set the platform bridge for hover cursor styling (grab on rows). */
    void setPlatformBridge(NUIPlatformBridge* bridge) { m_platformBridge = bridge; }
    
    // File browser functionality
    void setCurrentPath(const std::string& path);
    void refresh();
    void navigateUp();
    void navigateTo(const std::string& path);
    
    // File operations
    void selectFile(const std::string& path);
    void openFile(const std::string& path);
    void openFolder(const std::string& path);
    void toggleFolder(const FileItem* item);
    
    // Callbacks
    void setOnFileSelected(std::function<void(const FileItem&)> callback) { onFileSelected_ = callback; }
    void setOnFileOpened(std::function<void(const FileItem&)> callback) { onFileOpened_ = callback; }
    void setOnPathChanged(std::function<void(const std::string&)> callback) { onPathChanged_ = callback; }
    void setOnSoundPreview(std::function<void(const FileItem&)> callback) { onSoundPreview_ = callback; }
    void setOnSearchTextChanged(std::function<void(const std::string&)> callback) { onSearchTextChanged_ = callback; }
    
    // Loading state control (for external async operations)
    void setLoadingPlayback(bool loading) { 
        isLoadingPlayback_ = loading;
        if (loading) loadingAnimationTime_ = 0.0f;
        setDirty(true);
    }
    bool isLoadingPlayback() const { return isLoadingPlayback_; }
    void setActivePlaybackPath(const std::string& path);
    
    // Multi-select
    void toggleFileSelection(int index, bool ctrlPressed, bool shiftPressed);
    void clearSelection();
    const std::vector<int>& getSelectedIndices() const { return selectedIndices_; }
    
    // Search/filter
    void setSearchQuery(const std::string& query);
    void setSearchPlaceholder(const std::string& placeholder);
    void applyFilter();
    std::string getSearchQuery() const;
    bool isSearchBoxFocused() const;
    // Drop search-box focus when a press at `pos` lands outside it. The content
    // root calls this on every left-press so clicks in sibling panels (which
    // consume the event before it ever reaches the browser) still dismiss the
    // search caret. Returns true if focus was actually cleared.
    bool blurSearchIfPressOutside(const NUIPoint& pos);
    
    // Preview panel
    void setPreviewPanelVisible(bool visible);
    bool isPreviewPanelVisible() const { return previewPanelVisible_; }
    
    // Favorites (files or folders, anywhere on disk)
    void addToFavorites(const std::string& path);
    void removeFromFavorites(const std::string& path);
    bool isFavorite(const std::string& path) const;
    void toggleFavorite(const std::string& path);
    const std::vector<std::string>& getFavorites() const { return favoritesPaths_; }

    // Places: user-added folders shown in the navigation pane. Any directory
    // qualifies — the library root is a starting point, not a boundary.
    void addPlace(const std::string& path);
    void removePlace(const std::string& path);
    bool isPlace(const std::string& path) const;
    const std::vector<std::string>& getPlaces() const { return customPlacePaths_; }

    // Collections (tags). A collection view lists every tagged item, wherever it lives.
    void toggleTag(const std::string& path, const std::string& tag);
    bool hasTag(const std::string& path, const std::string& tag) const;

    // User collections: the named tags pinned to the navigation pane, in order.
    const std::vector<std::string>& getCollections() const { return collections_; }
    bool createCollection(const std::string& name);                        ///< false if empty or taken
    bool renameCollection(const std::string& from, const std::string& to); ///< retags every member
    void deleteCollection(const std::string& name);                        ///< untags members; files untouched
    /// Open an inline name editor on the collection's nav row (Enter/click-away
    /// commits, Esc cancels).
    void beginCollectionRename(const std::string& name);
    bool isRenamingCollection() const { return navEditor_ != nullptr; }

    /// Current Project: supplies the audio files the open project uses. The app
    /// wires it to the engine's source list; the browser stays engine-agnostic.
    void setProjectFilesProvider(std::function<std::vector<std::string>()> provider) {
        projectFilesProvider_ = std::move(provider);
    }
    void showCurrentProject();

    // Listing views: Favorites and Collections show a flat list of items gathered
    // from anywhere on disk instead of one directory's contents.
    void showFavorites();
    void showCollection(const std::string& tag);
    bool isShowingListing() const { return listingKind_ != ListingKind::None; }
    const std::string& getListingTitle() const { return listingTitle_; }

    /// Library root: the folder the Library categories (User Library, Packs, ...)
    /// live under, and where the browser starts. It does not restrict navigation.
    const std::string& getLibraryRoot() const { return rootPath_; }
    void setLibraryRoot(const std::string& path);

    // Persistent library state: favorites, places, collections, sort, library root.
    // Where the browser is looking (last path, expanded folders) is UIState's job.
    // Once a state path is set, every library change is written back to it.
    bool saveState(const std::string& filePath) const;
    bool loadState(const std::string& filePath);
    void setStatePath(const std::string& filePath) { statePath_ = filePath; }
    /// App startup: adopt @p statePath as the autosave target and load it, or,
    /// on first run, import the pre-v3 ~/.config/aestra/browser_settings.json.
    void initLibraryState(const std::string& statePath);
    const std::string& getStatePath() const { return statePath_; }

    /// Pump background scan results into the model. onUpdate() does this every
    /// frame; exposed so headless callers (tests) can drive the browser.
    void pollScanResults() { processScanResults(); }
    bool isScanPending() const;
    
    // Issue #120: Get/set expanded folders for UIState persistence
    std::vector<std::string> getExpandedFolders() const;
    void expandFolders(const std::vector<std::string>& folders);
    
    // Properties
    const std::string& getCurrentPath() const { return currentPath_; }
    const FileItem* getSelectedFile() const { return selectedFile_; }
    const std::vector<const FileItem*>& getFiles() const { return displayItems_; }
    /// The rows actually on screen: getFiles() after search, quick and tag filters.
    const std::vector<const FileItem*>& getVisibleFiles() const { return getActiveView(); }
    
    // Sorting
    // Persisted as an integer: append only.
    enum class SortMode {
        Name,
        Type,
        Size,
        Modified,
        Length,
        Bpm
    };

    enum class QuickFilter {
        All,
        Audio,
        Projects,
        Folders
    };
    
    void setSortMode(SortMode mode);

    // Library-wide search: a background index of the library root, Places and
    // Music/Downloads lets the search box reach folders that are not open.
    void rescanLibrary();
    bool isLibraryIndexing() const;
    void setSearchWholeLibrary(bool wholeLibrary);
    bool isSearchingWholeLibrary() const { return searchWholeLibrary_; }
    void setSortAscending(bool ascending);
    SortMode getSortMode() const { return sortMode_; }
    bool isSortAscending() const { return sortAscending_; }

    // Navigation actions
    enum class BrowserNavAction {
        Favorites,
        Collection,    // one row per user collection; BrowserNavHit::path is its name
        AddCollection, // "+ New Collection"
        Sounds,
        Drums,
        Instruments,
        AudioEffects,
        Plugins,
        Patterns,
        Clips,
        Samples,
        Packs,
        UserLibrary,
        CurrentProject,
        CustomPlace,
        AddFolder,
        SystemPlace
    };

    struct BrowserNavHit {
        BrowserNavAction action;
        NUIRect bounds;
        std::string label;
        std::string path;
    };

    struct BrowserLayout {
        NUIRect searchBar;
        NUIRect search;
        NUIRect navPane;
        NUIRect listHeader;
        NUIRect list;
        NUIRect backButton;
        NUIRect forwardButton;
        NUIRect upButton;
        NUIRect pathLabel;
        NUIRect filterButton;
        NUIRect sortButton;
        NUIRect searchActionButton;
        float navWidth = 0.0f;
    };

    BrowserLayout computeBrowserLayout() const;

    /// Navigation rows as laid out by the last paint (bounds are screen space).
    const std::vector<BrowserNavHit>& getNavHits() const { return navHits_; }

    /// True when the search field is empty (or absent) — the search row's
    /// trailing clear button exists only when there is a query to clear.
    bool searchQueryIsEmpty() const;

    /// Hover washes drawn per-frame OUTSIDE the FBO cache, so hover changes
    /// never force a cache rebuild (see renderFileList / nav drawRow).
    void renderHoverOverlays(NUIRenderer& renderer);
    float getNavPaneWidth() const;
    bool usesCompactNavigation() const;
    BrowserNavAction getActiveNavAction() const { return activeNavAction_; }

    // Shared results-pane host for specialized browser views. FileBrowser owns
    // their geometry and active-state visibility; the application retains the
    // component lifetime and workspace z-order.
    void registerContentView(BrowserNavAction action, const std::shared_ptr<NUIComponent>& component);
    void setContentViewsEnabled(bool enabled);
    NUIRect getContentViewBounds() const;

    /// Programmatically select a navigation entry — same pipeline as clicking
    /// it (content view swap, nav callback), minus hit-testing. Lets host
    /// surfaces (e.g. the mixer plugin dropdown's "Browse all plugins") open a
    /// specific library section.
    void selectNavAction(BrowserNavAction action);

    void setOnNavActionSelected(std::function<void(BrowserNavAction)> callback) { onNavActionSelected_ = callback; }

    // Drop target support for Places section
    bool isPointOverPlacesSection(float x, float y) const;
    void onDropFileToPlaces(const std::string& path);
    void setDragOverPlaces(bool over) { m_isDragOverPlaces = over; }
    bool isDragOverPlaces() const { return m_isDragOverPlaces; }

	private:
	    void loadDirectoryContents();
	    void loadFolderContents(FileItem* item);

        // Async scanning (prevents UI stalls on large directories)
        // Listing: stat an explicit list of paths (Favorites / Collections)
        // instead of iterating a directory. Same generation guard as Root.
        enum class ScanKind { Root, Folder, Listing };
        struct ScanTask {
            ScanKind kind;
            std::string path;
            std::vector<std::string> paths; // Listing only
            int depth = 0;
            bool showHidden = false;
            uint64_t generation = 0;
        };
        struct ScanResult {
            ScanKind kind;
            std::string path;
            std::string error;
            int depth = 0;
            uint64_t generation = 0;
            std::vector<FileItem> items;
        };

        void ensureScanWorker();
        void stopScanWorker();
        void enqueueScan(ScanKind kind, const std::string& path, int depth);
        void processScanResults();
        std::vector<FileItem> scanDirectory(const std::string& path, int depth, bool showHidden,
                                            uint64_t generation, std::string& error) const;
        std::vector<FileItem> scanListing(const std::vector<std::string>& paths, uint64_t generation) const;
        FileItem* findItemByPath(const std::string& path);
        void scanWorkerLoop();

        std::thread scanWorker_;
        mutable std::mutex scanMutex_;
        std::condition_variable scanCv_;
        std::deque<ScanTask> scanTasks_;
        std::deque<ScanResult> scanResults_;
        std::atomic<bool> scanStop_{false};
        std::atomic<uint64_t> scanGeneration_{0};
        bool scanWorkerStarted_{false};
        bool scanningRoot_{false};
        bool bootScanRecoveryAttempted_{false};

		    void updateDisplayList();
		    void updateDisplayListRecursive(FileItem& item, std::vector<const FileItem*>& list);
		    void sortFiles();
		    bool compareFileItems(const FileItem& a, const FileItem& b) const;
		    std::shared_ptr<NUIIcon> getIconForFileType(FileType type);
		    bool isFilterActive() const;
            bool matchesQuickFilter(const FileItem& item) const;
		    const std::vector<const FileItem*>& getActiveView() const;
		    void invalidateAllItemCaches();
            void renderStaticContent(NUIRenderer& renderer, const NUIRect& bounds);
		    void renderFileList(NUIRenderer& renderer);
		    void renderInteractiveBreadcrumbs(NUIRenderer& renderer);
		    void renderToolbar(NUIRenderer& renderer);
            void renderNavigationPane(NUIRenderer& renderer, const BrowserLayout& layout);
            void renderListHeader(NUIRenderer& renderer, const BrowserLayout& layout);
            /// Centered empty/scanning/error block: optional glyph, title,
            /// balanced word-wrapped hint. Shared by all list placeholder states.
            void drawListEmptyState(NUIRenderer& renderer, const NUIRect& listClip,
                                    const std::shared_ptr<NUIIcon>& icon, const std::string& title,
                                    const std::string& hint);
	    void renderScrollbar(NUIRenderer& renderer);
    void renderSearchBox(NUIRenderer& renderer);
    void updateScrollPosition();
	    void updateBreadcrumbs();
	    void navigateToBreadcrumb(int index);
	    bool handleSearchBoxMouseEvent(const NUIMouseEvent& event);
	    bool handleScrollbarMouseEvent(const NUIMouseEvent& event);
	    bool handleBreadcrumbMouseEvent(const NUIMouseEvent& event);
        bool handleNavigationMouseEvent(const NUIMouseEvent& event, const BrowserLayout& layout);

        // onMouseEvent decomposition — called by the dispatcher in this order;
        // bool handlers return true when the event was consumed.
        bool handleChromeMouse(const NUIMouseEvent& event, const BrowserLayout& browserLayout);
        bool handleActiveDragMouse(const NUIMouseEvent& event);
        bool handleDragInitiation(const NUIMouseEvent& event, const std::vector<const FileItem*>& view);
        bool handleWheelScroll(const NUIMouseEvent& event, const BrowserLayout& browserLayout, bool mouseInside,
                               const std::vector<const FileItem*>& view);
        void updateBreadcrumbHover(const NUIMouseEvent& event);
        bool handleListMouse(const NUIMouseEvent& event, const std::vector<const FileItem*>& view,
                             const BrowserLayout& browserLayout);
	    void updateScrollbarVisibility();
	    void showFavoritesMenu();
	    void showAddFolderMenu();
	    void showSortMenu();
	    void showQuickFilterMenu();
	    void showTagFilterMenu();
	    void showItemContextMenu(const FileItem& item, const NUIPoint& position);
	    void showHiddenBreadcrumbMenu(const std::vector<std::string>& hiddenPaths, const NUIPoint& position);
	    void hidePopupMenu();
	    std::vector<std::string> getAllTagsSorted() const;
	    void pushToHistory(const std::string& path);
	    void navigateBack();
	    void navigateForward();
        void clearActiveFilters();
        std::string getQuickFilterLabel() const;

        // Legacy settings migration (v1 pipe-separated → in-memory lists)
        void migrateLegacySettings(const std::string& filePath);

        // Listing views
        enum class ListingKind { None, Favorites, Collection, Project };
        void beginListing(ListingKind kind, const std::string& title, std::vector<std::string> paths);
        void reissueListing();
        void exitListing();
        std::vector<std::string> pathsWithTag(const std::string& tag) const;
        void showPlaceContextMenu(const BrowserNavHit& hit, const NUIPoint& position);
        void showCollectionContextMenu(const std::string& name, const NUIPoint& position);
        void addTaggingSubmenus(const std::string& path);
        std::string createUntitledCollection();
        void finishCollectionRename(bool accept);
        NUIColor collectionColor(const std::string& name) const;
        std::vector<std::string> collections_{"Purple", "Drums", "Instruments", "Vocals"};
        std::function<std::vector<std::string>()> projectFilesProvider_;
        // Inline collection-name editor. A finished editor is parked in
        // retiredNavEditors_ and dropped on the next onRender(): it must never
        // be freed or detached from inside its own callbacks.
        std::shared_ptr<NUITextInput> navEditor_;
        std::string navEditorTarget_;
        std::vector<std::shared_ptr<NUITextInput>> retiredNavEditors_;
        void persistState();
        ListingKind listingKind_ = ListingKind::None;
        std::string listingTitle_;
        std::string listingTag_;
        std::string statePath_;
        std::vector<BrowserLibrary::SystemPlace> systemPlaces_;
        std::vector<std::string> indexRoots() const;
        std::unique_ptr<BrowserLibraryIndex> libraryIndex_;
        uint64_t seenIndexRevision_ = 0;
        double indexRefreshCooldown_ = 0.0;
        // Rows for library-wide search hits (the list holds pointers into it).
        std::vector<FileItem> searchResults_;
        bool searchWholeLibrary_ = true;
        // "+ Add Folder > Choose a Folder...": the native picker runs off the
        // UI thread; onUpdate() collects the answer.
        Aestra::PendingFileDialog folderPicker_;

        // Auto-preview + keyboard helpers
        void tryAutoPreview();
        void scrollToSelected();

    // File management
    std::string currentPath_;
    std::string pendingSelectionPath_;
    std::string activePlaybackPath_;
    std::vector<FileItem> rootItems_;
    std::vector<const FileItem*> displayItems_;
    std::vector<const FileItem*> filteredFiles_;  // Filtered files for search
    const FileItem* selectedFile_;
    int selectedIndex_;
    std::vector<int> selectedIndices_;     // Multi-select support
    int lastShiftSelectIndex_;             // For shift-select range
    
    // UI state
    float scrollOffset_;          // Current rendered scroll position
    float targetScrollOffset_;    // Target scroll position for lerp
    float scrollVelocity_;        // Current scroll velocity for smoothing
    float itemHeight_;
    int visibleItems_;
    bool showHiddenFiles_;
    float lastCachedWidth_;       // Track width changes to invalidate cache
    float lastRenderedOffset_;    // Track when to trigger repaint
    float effectiveWidth_;        // Current render width (accounts for preview panel)
    
    // View Cache
    mutable std::vector<const FileItem*> cachedView_;
    mutable bool viewDirty_ = true;

    // FBO Caching
    uint64_t m_cacheId;
    bool m_cacheInvalidated;
    bool m_isRenderingToCache;
    void invalidateCache() { m_cacheInvalidated = true; setDirty(true); }
    void* m_cachedRender = nullptr; // Opaque pointer to CachedRenderData
    
    // Scrollbar state
    bool scrollbarVisible_;
    float scrollbarOpacity_;
    float scrollbarWidth_;
    float scrollbarTrackHeight_;
    float scrollbarThumbHeight_;
    float scrollbarThumbY_;
    bool isDraggingScrollbar_;
    bool scrollbarHovered_;
    float dragStartY_;
    float dragStartScrollOffset_;
    float scrollbarFadeTimer_;
    static constexpr float SCROLLBAR_FADE_DELAY = 1.0f; // seconds
    static constexpr float SCROLLBAR_FADE_DURATION = 0.3f; // seconds
    
	    // Hover state
	    int hoveredIndex_;
	    NUIPoint lastMousePos_{0.0f, 0.0f};
	    NUIPlatformBridge* m_platformBridge = nullptr;
    
	    // Search/filter state
	    std::shared_ptr<NUITextInput> searchInput_; // Replaced searchQuery_, searchBoxFocused_, searchCaretBlinkTime_, searchCaretVisible_
	    float searchBoxWidth_;
	    NUIRect searchBoxBounds_;
        QuickFilter activeQuickFilter_ = QuickFilter::All;
        BrowserNavAction activeNavAction_ = BrowserNavAction::Sounds;
        std::string activeNavPath_;
        int hoveredNavIndex_ = -1;
        enum class ChromeAction {
            None,
            Back,
            Forward,
            Up,
            Filter,
            Sort,
            ClearSearch
        };
        ChromeAction hoveredChromeAction_ = ChromeAction::None;
        std::vector<BrowserNavHit> navHits_;
        struct BrowserContentView {
            BrowserNavAction action;
            std::weak_ptr<NUIComponent> component;
        };
        std::vector<BrowserContentView> contentViews_;
        bool contentViewsEnabled_ = true;
        void updateContentViews();
        // Nav pane vertical scroll for short browser viewports.
        float navScrollOffset_ = 0.0f;   // current offset, clamped in render
        float navContentHeight_ = 0.0f;  // measured content height below the header
        float navViewportHeight_ = 0.0f; // visible content height below the header
	    std::shared_ptr<NUIContextMenu> popupMenu_;
	    std::string popupMenuTargetPath_;
	    bool popupMenuTargetIsDirectory_ = false;
	    std::string rootPath_;

	    // Tags / filtering
	    std::unordered_map<std::string, std::vector<std::string>> tagsByPath_;
	    std::string activeTagFilter_;
	    std::string scanError_;
	    std::vector<std::string> customPlacePaths_;
    
    // Preview panel state
        bool previewPanelVisible_{false};
        float previewPanelWidth_{0.0f};
        std::vector<float> waveformData_;  // Cached waveform amplitude data
        bool isLoadingPreview_{false};     // True while loading waveform/preview
        float loadingAnimationTime_{0.0f}; // Animation timer for loading spinner
        bool isLoadingPlayback_;           // True while loading audio for playback
        bool wasLoadingPlayback_;          // Previous frame's playback loading state

        // Breadcrumb state
        struct Breadcrumb {
            std::string name;
            std::string path;
            std::vector<std::string> hiddenPaths; // For ellipsis menu
            float x;
            float width;
        };
    std::vector<Breadcrumb> breadcrumbs_;
    int hoveredBreadcrumbIndex_;
    NUIRect breadcrumbBounds_;
    
    // Favorites state
    std::vector<std::string> favoritesPaths_;
    std::string favoritesConfigPath_;
    
    // Double-click detection
    int lastClickedIndex_;
    double lastClickTime_;
    static constexpr double DOUBLE_CLICK_TIME = 0.5; // 500ms window for double-click
    
    // Sorting
    SortMode sortMode_;
    bool sortAscending_;
    
    // Icons
    std::shared_ptr<NUIIcon> m_searchIcon;
    std::shared_ptr<NUIIcon> folderIcon_;
    std::shared_ptr<NUIIcon> folderOpenIcon_;
    std::shared_ptr<NUIIcon> audioFileIcon_;
    std::shared_ptr<NUIIcon> musicFileIcon_;
    std::shared_ptr<NUIIcon> projectFileIcon_;
    std::shared_ptr<NUIIcon> wavFileIcon_;
    std::shared_ptr<NUIIcon> mp3FileIcon_;
    std::shared_ptr<NUIIcon> flacFileIcon_;
    std::shared_ptr<NUIIcon> oggFileIcon_;
    std::shared_ptr<NUIIcon> midiFileIcon_;
    std::shared_ptr<NUIIcon> unknownFileIcon_;
    std::shared_ptr<NUIIcon> chevronIcon_;
    
    // Callbacks
    std::function<void(const FileItem&)> onFileSelected_;
    std::function<void(const FileItem&)> onFileOpened_;
    std::function<void(const std::string&)> onPathChanged_;
    std::function<void(const FileItem&)> onSoundPreview_;
    std::function<void(const std::string&)> onSearchTextChanged_;
    std::function<void(BrowserNavAction)> onNavActionSelected_;
    
    // Theme colors
    NUIColor backgroundColor_;
    NUIColor textColor_;
    NUIColor selectedColor_;
    NUIColor hoverColor_;
    
    // Navigation history
    std::vector<std::string> navHistory_;
    int navHistoryIndex_;
    bool isNavigatingHistory_;
    
    // Drag-and-drop state
    bool isDraggingFile_ = false;          // True when dragging from file list
    int dragSourceIndex_ = -1;             // Index of file being dragged
    NUIPoint dragStartPos_;                // Position where drag started
    bool dragPotential_ = false;           // True when mouse down, waiting for threshold

    // Drop target state for Places section
    bool m_isDragOverPlaces = false;

    // Auto-preview on keyboard navigation
    bool m_autoPreviewEnabled = true;
};

} // namespace AestraUI
