// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Core/NUIIcon.h"
#include "../NomadUI/Core/NUIDragDrop.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace NomadUI {

class NUIContextMenu;
class NUITextInput;

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
    
    FileItem(const std::string& n, const std::string& p, FileType t, bool isDir, size_t s = 0, const std::string& modified = "")
        : name(n), path(p), type(t), isDirectory(isDir), size(s), lastModified(modified) {}
        
    void invalidateCache() const { cacheValid = false; }
};

/**
 * File Browser Component
 * 
 * A modern file browser with icons, sorting, and navigation.
 * Integrates with NomadUI theme system for consistent styling.
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
    
    // Loading state control (for external async operations)
    void setLoadingPlayback(bool loading) { 
        isLoadingPlayback_ = loading;
        if (loading) loadingAnimationTime_ = 0.0f;
        setDirty(true);
    }
    bool isLoadingPlayback() const { return isLoadingPlayback_; }
    
    // Multi-select
    void toggleFileSelection(int index, bool ctrlPressed, bool shiftPressed);
    void clearSelection();
    const std::vector<int>& getSelectedIndices() const { return selectedIndices_; }
    
    // Search/filter
    void setSearchQuery(const std::string& query);
    void applyFilter();
    std::string getSearchQuery() const;
    bool isSearchBoxFocused() const;
    
    // Preview panel
    void setPreviewPanelVisible(bool visible);
    bool isPreviewPanelVisible() const { return previewPanelVisible_; }
    
    // Favorites
    void addToFavorites(const std::string& path);
    void removeFromFavorites(const std::string& path);
    bool isFavorite(const std::string& path) const;
    void toggleFavorite(const std::string& path);
    const std::vector<std::string>& getFavorites() const { return favoritesPaths_; }
    
    // Persistent state (save/load expanded folders, scroll position, last path)
    void saveState(const std::string& filePath);
    void loadState(const std::string& filePath);
    
    // Properties
    const std::string& getCurrentPath() const { return currentPath_; }
    const FileItem* getSelectedFile() const { return selectedFile_; }
    const std::vector<const FileItem*>& getFiles() const { return displayItems_; }
    
    // Sorting
    enum class SortMode {
        Name,
        Type,
        Size,
        Modified
    };
    
    void setSortMode(SortMode mode);
    void setSortAscending(bool ascending);
    
	private:
	    void loadDirectoryContents();
	    void loadFolderContents(FileItem* item);

        // Async scanning (prevents UI stalls on large directories)
        enum class ScanKind { Root, Folder };
        struct ScanTask {
            ScanKind kind;
            std::string path;
            int depth = 0;
            bool showHidden = false;
            uint64_t generation = 0;
        };
        struct ScanResult {
            ScanKind kind;
            std::string path;
            int depth = 0;
            uint64_t generation = 0;
            std::vector<FileItem> items;
        };

        void ensureScanWorker();
        void stopScanWorker();
        void enqueueScan(ScanKind kind, const std::string& path, int depth);
        void processScanResults();
        std::vector<FileItem> scanDirectory(const std::string& path, int depth, bool showHidden, uint64_t generation) const;
        FileItem* findItemByPath(const std::string& path);
        void scanWorkerLoop();

        std::thread scanWorker_;
        std::mutex scanMutex_;
        std::condition_variable scanCv_;
        std::deque<ScanTask> scanTasks_;
        std::deque<ScanResult> scanResults_;
        std::atomic<bool> scanStop_{false};
        std::atomic<uint64_t> scanGeneration_{0};
        bool scanWorkerStarted_{false};
        bool scanningRoot_{false};

		    void updateDisplayList();
		    void updateDisplayListRecursive(FileItem& item, std::vector<const FileItem*>& list);
		    void sortFiles();
		    bool compareFileItems(const FileItem& a, const FileItem& b) const;
		    FileType getFileTypeFromExtension(const std::string& extension) const;
		    std::shared_ptr<NUIIcon> getIconForFileType(FileType type);
		    bool isFilterActive() const;
		    const std::vector<const FileItem*>& getActiveView() const;
		    void invalidateAllItemCaches();
		    void renderFileList(NUIRenderer& renderer);
		    void renderInteractiveBreadcrumbs(NUIRenderer& renderer);
		    void renderToolbar(NUIRenderer& renderer);
	    void renderScrollbar(NUIRenderer& renderer);
	    void renderPreviewPanel(NUIRenderer& renderer);
    void renderSearchBox(NUIRenderer& renderer);
    void updateScrollPosition();
	    void updateBreadcrumbs();
	    void navigateToBreadcrumb(int index);
	    bool handleSearchBoxMouseEvent(const NUIMouseEvent& event);
	    bool handleScrollbarMouseEvent(const NUIMouseEvent& event);
	    bool handleBreadcrumbMouseEvent(const NUIMouseEvent& event);
	    void updateScrollbarVisibility();
	    void showFavoritesMenu();
	    void showSortMenu();
	    void showTagFilterMenu();
	    void showItemContextMenu(const FileItem& item, const NUIPoint& position);
	    void showHiddenBreadcrumbMenu(const std::vector<std::string>& hiddenPaths, const NUIPoint& position);
	    void hidePopupMenu();
	    void toggleTag(const std::string& path, const std::string& tag);
	    bool hasTag(const std::string& path, const std::string& tag) const;
	    std::vector<std::string> getAllTagsSorted() const;
	    void pushToHistory(const std::string& path);
	    void navigateBack();
	    void navigateForward();
    
    // File management
    std::string currentPath_;
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
    
	    // Search/filter state
	    std::shared_ptr<NUITextInput> searchInput_; // Replaced searchQuery_, searchBoxFocused_, searchCaretBlinkTime_, searchCaretVisible_
	    float searchBoxWidth_;
	    NUIRect searchBoxBounds_;
	    NUIRect refreshButtonBounds_;
	    NUIRect favoritesButtonBounds_;
	    NUIRect tagsButtonBounds_;
	    NUIRect sortButtonBounds_;
	    bool refreshHovered_ = false;
	    bool favoritesHovered_ = false;
	    bool tagsHovered_ = false;
	    bool sortHovered_ = false;
	    std::shared_ptr<NUIContextMenu> popupMenu_;
	    std::string popupMenuTargetPath_;
	    bool popupMenuTargetIsDirectory_ = false;
	    std::string rootPath_;

	    // Tags / filtering
	    std::unordered_map<std::string, std::vector<std::string>> tagsByPath_;
	    std::string activeTagFilter_;
    
    // Preview panel state
    bool previewPanelVisible_;
    float previewPanelWidth_;
    std::vector<float> waveformData_;      // Cached waveform amplitude data
    bool isLoadingPreview_;                // True while loading waveform/preview
    float loadingAnimationTime_;           // Animation timer for loading spinner
    bool isLoadingPlayback_;               // True while loading audio for playback
    bool wasLoadingPlayback_;              // Previous frame's playback loading state
    
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
    std::shared_ptr<NUIIcon> folderIcon_;
    std::shared_ptr<NUIIcon> folderOpenIcon_;
    std::shared_ptr<NUIIcon> audioFileIcon_;
    std::shared_ptr<NUIIcon> musicFileIcon_;
    std::shared_ptr<NUIIcon> projectFileIcon_;
    std::shared_ptr<NUIIcon> wavFileIcon_;
    std::shared_ptr<NUIIcon> mp3FileIcon_;
    std::shared_ptr<NUIIcon> flacFileIcon_;
    std::shared_ptr<NUIIcon> unknownFileIcon_;
    std::shared_ptr<NUIIcon> searchIcon_;
    std::shared_ptr<NUIIcon> starIcon_;
    std::shared_ptr<NUIIcon> starFilledIcon_;
    std::shared_ptr<NUIIcon> chevronIcon_;
    std::shared_ptr<NUIIcon> chevronDownIcon_;
    std::shared_ptr<NUIIcon> playIcon_;
    std::shared_ptr<NUIIcon> pauseIcon_;
    std::shared_ptr<NUIIcon> refreshIcon_; // Added missing icon
    std::shared_ptr<NUIIcon> backIcon_;
    std::shared_ptr<NUIIcon> forwardIcon_;
    
    // Callbacks
    std::function<void(const FileItem&)> onFileSelected_;
    std::function<void(const FileItem&)> onFileOpened_;
    std::function<void(const std::string&)> onPathChanged_;
    std::function<void(const FileItem&)> onSoundPreview_;
    
    // Theme colors
    NUIColor backgroundColor_;
    NUIColor textColor_;
    NUIColor selectedColor_;
    NUIColor hoverColor_;
    NUIColor borderColor_;
    
    // Navigation history
    std::vector<std::string> navHistory_;
    int navHistoryIndex_;
    bool isNavigatingHistory_;
    
    // Drag-and-drop state
    bool isDraggingFile_ = false;          // True when dragging from file list
    int dragSourceIndex_ = -1;             // Index of file being dragged
    NUIPoint dragStartPos_;                // Position where drag started
    bool dragPotential_ = false;           // True when mouse down, waiting for threshold
};

} // namespace NomadUI
