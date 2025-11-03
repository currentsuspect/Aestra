// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "FileBrowser.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "../NomadCore/include/NomadLog.h"
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>

using namespace Nomad;

namespace NomadUI {

FileBrowser::FileBrowser()
    : NUIComponent()
    , selectedFile_(nullptr)
    , selectedIndex_(-1)
    , scrollOffset_(0.0f)
    , targetScrollOffset_(0.0f)   // Initialize lerp target
    , itemHeight_(32.0f)           // Initialize to default theme value
    , visibleItems_(0)
    , showHiddenFiles_(false)
    , lastCachedWidth_(0.0f)       // Initialize cache width tracker
    , lastRenderedOffset_(0.0f)    // Initialize render tracking
    , scrollbarVisible_(false)
    , scrollbarOpacity_(0.0f)
    , scrollbarWidth_(8.0f)        // Initialize to default theme value
    , scrollbarTrackHeight_(0.0f)
    , scrollbarThumbHeight_(0.0f)
    , scrollbarThumbY_(0.0f)
    , isDraggingScrollbar_(false)
    , dragStartY_(0.0f)
    , dragStartScrollOffset_(0.0f)
    , scrollbarFadeTimer_(0.0f)
    , hoveredIndex_(-1)
    , sortMode_(SortMode::Name)
    , sortAscending_(true)
{
    // Set default size from theme
    auto& themeManager = NUIThemeManager::getInstance();
    float defaultWidth = themeManager.getLayoutDimension("fileBrowserWidth");
    float defaultHeight = 300.0f; // Default height
    setSize(defaultWidth, defaultHeight);
    
    // Initialize icons with improved visibility for Liminal Dark v2.0
    // Use inline SVG content for reliable icon loading
    folderIcon_ = std::make_shared<NUIIcon>();
    const char* folderSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M10 4H4c-1.11 0-2 .89-2 2v12c0 1.11.89 2 2 2h16c1.11 0 2-.89 2-2V8c0-1.11-.89-2-2-2h-8l-2-2z"/>
        </svg>
    )";
    folderIcon_->loadSVG(folderSvg);
    folderIcon_->setIconSize(24, 24);
    folderIcon_->setColor(NUIColor(0.733f, 0.525f, 0.988f, 1.0f));  // #bb86fc - Purple accent for folders
    
    folderOpenIcon_ = std::make_shared<NUIIcon>();
    const char* folderOpenSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M20 6h-8l-2-2H4c-1.11 0-1.99.89-1.99 2L2 18c0 1.11.89 2 2 2h16c1.11 0 2-.89 2-2V8c0-1.11-.89-2-2-2zm0 12H4V8h16v10z"/>
        </svg>
    )";
    folderOpenIcon_->loadSVG(folderOpenSvg);
    folderOpenIcon_->setIconSize(24, 24);
    folderOpenIcon_->setColor(NUIColor(0.733f, 0.525f, 0.988f, 1.0f));  // #bb86fc - Purple accent for folders
    
    audioFileIcon_ = std::make_shared<NUIIcon>();
    const char* audioFileSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M14,2H6A2,2 0 0,0 4,4V20A2,2 0 0,0 6,22H18A2,2 0 0,0 20,20V8L14,2M18,20H6V4H13V9H18V20Z"/>
        </svg>
    )";
    audioFileIcon_->loadSVG(audioFileSvg);
    audioFileIcon_->setIconSize(24, 24);
    audioFileIcon_->setColor(NUIColor(0.733f, 0.525f, 0.988f, 1.0f));  // #bb86fc - Purple accent for file icons
    
    musicFileIcon_ = std::make_shared<NUIIcon>();
    const char* musicFileSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M14,2H6A2,2 0 0,0 4,4V20A2,2 0 0,0 6,22H18A2,2 0 0,0 20,20V8L14,2M18,20H6V4H13V9H18V20Z"/>
        </svg>
    )";
    musicFileIcon_->loadSVG(musicFileSvg);
    musicFileIcon_->setIconSize(24, 24);
    musicFileIcon_->setColor(NUIColor(0.733f, 0.525f, 0.988f, 1.0f));  // #bb86fc - Purple accent for file icons
    
    projectFileIcon_ = std::make_shared<NUIIcon>();
    const char* projectFileSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M14,2H6A2,2 0 0,0 4,4V20A2,2 0 0,0 6,22H18A2,2 0 0,0 20,20V8L14,2M18,20H6V4H13V9H18V20Z"/>
        </svg>
    )";
    projectFileIcon_->loadSVG(projectFileSvg);
    projectFileIcon_->setIconSize(24, 24);
    projectFileIcon_->setColor(NUIColor(0.733f, 0.525f, 0.988f, 1.0f));  // #bb86fc - Purple accent for file icons
    
    wavFileIcon_ = std::make_shared<NUIIcon>();
    const char* wavFileSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M14,2H6A2,2 0 0,0 4,4V20A2,2 0 0,0 6,22H18A2,2 0 0,0 20,20V8L14,2M18,20H6V4H13V9H18V20Z"/>
        </svg>
    )";
    wavFileIcon_->loadSVG(wavFileSvg);
    wavFileIcon_->setIconSize(24, 24);
    wavFileIcon_->setColor(NUIColor(0.733f, 0.525f, 0.988f, 1.0f));  // #bb86fc - Purple accent for file icons
    
    mp3FileIcon_ = std::make_shared<NUIIcon>();
    const char* mp3FileSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M14,2H6A2,2 0 0,0 4,4V20A2,2 0 0,0 6,22H18A2,2 0 0,0 20,20V8L14,2M18,20H6V4H13V9H18V20Z"/>
        </svg>
    )";
    mp3FileIcon_->loadSVG(mp3FileSvg);
    mp3FileIcon_->setIconSize(24, 24);
    mp3FileIcon_->setColor(NUIColor(0.733f, 0.525f, 0.988f, 1.0f));  // #bb86fc - Purple accent for file icons
    
    flacFileIcon_ = std::make_shared<NUIIcon>();
    const char* flacFileSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M14,2H6A2,2 0 0,0 4,4V20A2,2 0 0,0 6,22H18A2,2 0 0,0 20,20V8L14,2M18,20H6V4H13V9H18V20Z"/>
        </svg>
    )";
    flacFileIcon_->loadSVG(flacFileSvg);
    flacFileIcon_->setIconSize(24, 24);
    flacFileIcon_->setColor(NUIColor(0.733f, 0.525f, 0.988f, 1.0f));  // #bb86fc - Purple accent for file icons
    
    // Unknown file icon (use a generic document icon)
    unknownFileIcon_ = std::make_shared<NUIIcon>();
    const char* unknownFileSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M14,2H6A2,2 0 0,0 4,4V20A2,2 0 0,0 6,22H18A2,2 0 0,0 20,20V8L14,2M18,20H6V4H13V9H18V20Z"/>
        </svg>
    )";
    unknownFileIcon_->loadSVG(unknownFileSvg);
    unknownFileIcon_->setIconSize(24, 24);
    unknownFileIcon_->setColor(NUIColor(0.604f, 0.604f, 0.639f, 1.0f));  // #9a9aa3 - Secondary text for unknown files
    
    // Set initial path to current directory
    currentPath_ = std::filesystem::current_path().string();
    loadDirectoryContents();
    
    // Load Liminal Dark v2.0 theme colors
    backgroundColor_ = themeManager.getColor("backgroundSecondary");  // #1b1b1f - Panels, sidebars, file browser
    textColor_ = themeManager.getColor("textPrimary");                // #e6e6eb - Soft white
    selectedColor_ = themeManager.getColor("accentCyan");             // #00bcd4 - Accent cyan for selection
    hoverColor_ = themeManager.getColor("surfaceRaised");             // #32323a - Hovered buttons, focused areas
    borderColor_ = themeManager.getColor("border");                   // #2e2e35 - Subtle separation lines
}

void FileBrowser::onRender(NUIRenderer& renderer) {
    NUIRect bounds = getBounds();
    if (bounds.isEmpty()) return;
    
    
    // Render background with enhanced borders
    renderer.fillRoundedRect(bounds, 8, backgroundColor_);
    
    // Main border
    renderer.strokeRoundedRect(bounds, 8, 1, borderColor_);
    
    // Inner black border for cleaner look
    NUIRect innerBounds(bounds.x + 1, bounds.y + 1, bounds.width - 2, bounds.height - 2);
    renderer.strokeRoundedRect(innerBounds, 7, 1, NUIColor(0.0f, 0.0f, 0.0f, 0.4f));
    
    // Render toolbar
    renderToolbar(renderer);
    
    // Render path bar
    renderPathBar(renderer);
    
    // Render file list
    renderFileList(renderer);
    
    // Render scrollbar
    renderScrollbar(renderer);
}

void FileBrowser::onUpdate(double deltaTime) {
    NUIComponent::onUpdate(deltaTime);
    
    // Pure instant scrolling - no lerp, just sync
    scrollOffset_ = targetScrollOffset_;
    
    // ALWAYS repaint if scroll position changed at all
    if (std::abs(scrollOffset_ - lastRenderedOffset_) > 0.01f) {
        lastRenderedOffset_ = scrollOffset_;
        setDirty(true);
    }
    
    // Update scrollbar thumb position based on current scroll
    float maxScroll = std::max(0.0f, files_.size() * itemHeight_ - scrollbarTrackHeight_);
    if (maxScroll > 0.0f) {
        scrollbarThumbY_ = (scrollOffset_ / maxScroll) * (scrollbarTrackHeight_ - scrollbarThumbHeight_);
    }
}

void FileBrowser::onResize(int width, int height) {
    NUIComponent::onResize(width, height);

    // Get component dimensions from theme
    auto& themeManager = NUIThemeManager::getInstance();

    // Update visible items count using configurable dimensions
    float headerHeight = themeManager.getComponentDimension("fileBrowser", "headerHeight");
    float itemHeight = themeManager.getComponentDimension("fileBrowser", "itemHeight");
    visibleItems_ = static_cast<int>((height - headerHeight) / itemHeight);
    visibleItems_ = std::max(1, visibleItems_);

    // Update scrollbar dimensions using configurable dimensions
    float scrollbarWidth = themeManager.getComponentDimension("fileBrowser", "scrollbarWidth");
    scrollbarTrackHeight_ = height - headerHeight - 8 - 20; // Account for path bar spacing and height
    scrollbarWidth_ = scrollbarWidth;

    updateScrollbarVisibility();

}

bool FileBrowser::onMouseEvent(const NUIMouseEvent& event) {
    NUIRect bounds = getBounds();
    
    // If we're dragging the scrollbar, handle mouse events even outside bounds
    if (isDraggingScrollbar_) {
        // Always check scrollbar events if we're dragging
        if (handleScrollbarMouseEvent(event)) {
            return true;
        }
    }
    
    // Check if mouse is within bounds
    bool mouseInside = bounds.contains(event.position.x, event.position.y);

    // Clear hover if mouse leaves the file browser entirely (but allow scrollbar dragging)
    if (!mouseInside && !isDraggingScrollbar_) {
        if (hoveredIndex_ != -1) {
            hoveredIndex_ = -1;
            setDirty(true); // Trigger redraw when clearing hover
        }
        return false;
    }
    
    
    // Handle scrollbar mouse events first - check if scrollbar should be visible
    float contentHeight = files_.size() * itemHeight_;
    float maxScroll = std::max(0.0f, contentHeight - scrollbarTrackHeight_);
    bool needsScrollbar = maxScroll > 0.0f;
    
    // Debug scrollbar state
    // static bool debugPrinted = false;
    // if (!debugPrinted && event.pressed) {
    //     printf("FileBrowser Mouse Event Debug:\n");
    //     printf("  files_.size(): %zu\n", files_.size());
    //     printf("  itemHeight_: %.1f\n", itemHeight_);
    //     printf("  scrollbarTrackHeight_: %.1f\n", scrollbarTrackHeight_);
    //     printf("  contentHeight: %.1f\n", contentHeight);
    //     printf("  maxScroll: %.1f\n", maxScroll);
    //     printf("  needsScrollbar: %s\n", needsScrollbar ? "YES" : "NO");
    //     printf("  isDraggingScrollbar_: %s\n", isDraggingScrollbar_ ? "YES" : "NO");
    //     debugPrinted = true;
    // }
    
    // Handle mouse wheel scrolling first (works anywhere in the file browser)
    if (event.wheelDelta != 0) {
        auto& themeManager = NUIThemeManager::getInstance();
        float headerHeight = themeManager.getComponentDimension("fileBrowser", "headerHeight");
        float itemHeight = themeManager.getComponentDimension("fileBrowser", "itemHeight");
        float listY = bounds.y + headerHeight + 8 + 20; // After path bar
        float listHeight = bounds.height - headerHeight - 8 - 20;

        // Mouse wheel scrolling - INSTANT with micro-smoothing
        float scrollSpeed = 2.0f; // Scroll 2 items per wheel step
        float scrollDelta = event.wheelDelta * scrollSpeed * itemHeight;
        
        targetScrollOffset_ -= scrollDelta;
        scrollOffset_ -= scrollDelta; // INSTANT - set both for zero lag

        // Clamp BOTH scroll offsets
        float maxScroll = std::max(0.0f, (files_.size() * itemHeight) - listHeight);
        targetScrollOffset_ = std::max(0.0f, std::min(targetScrollOffset_, maxScroll));
        scrollOffset_ = std::max(0.0f, std::min(scrollOffset_, maxScroll));

        // Let onUpdate handle thumb position and dirty flag
        return true;
    }
    
    // Check scrollbar events if scrollbar is needed (but not dragging - handled above)
    if (needsScrollbar && files_.size() > 0 && !isDraggingScrollbar_) {
        if (handleScrollbarMouseEvent(event)) {
            return true;
        }
    }
    
    // Check if click is in file list area
    auto& themeManager = NUIThemeManager::getInstance();
    float headerHeight = themeManager.getComponentDimension("fileBrowser", "headerHeight");
    float itemHeight = themeManager.getComponentDimension("fileBrowser", "itemHeight");
    float listY = bounds.y + headerHeight + 8 + 20; // After path bar
    float listHeight = bounds.height - headerHeight - 8 - 20;

    if (event.position.x >= bounds.x && event.position.x <= bounds.x + bounds.width &&
        event.position.y >= listY && event.position.y <= listY + listHeight) {

        // Calculate which item is being hovered
        float relativeY = event.position.y - listY;
        int itemIndex = static_cast<int>((relativeY + scrollOffset_) / itemHeight);

        // Update hover state
        int newHoveredIndex = (itemIndex >= 0 && itemIndex < static_cast<int>(files_.size())) ? itemIndex : -1;
        if (newHoveredIndex != hoveredIndex_) {
            hoveredIndex_ = newHoveredIndex;
            setDirty(true); // Trigger redraw when hover state changes
        }

        if (event.pressed && event.button == NUIMouseButton::Left) {
            // Calculate which file was clicked
            float relativeY = event.position.y - listY;
            int itemIndex = static_cast<int>((relativeY + scrollOffset_) / itemHeight);
            
            if (itemIndex >= 0 && itemIndex < static_cast<int>(files_.size())) {
                selectedIndex_ = itemIndex;
                selectedFile_ = &files_[itemIndex];
                
                if (onFileSelected_) {
                    onFileSelected_(*selectedFile_);
                }
                
                // Trigger sound preview for audio files
                if (onSoundPreview_ && selectedFile_ && !selectedFile_->isDirectory) {
                    // Check if it's an audio file
                    FileType type = selectedFile_->type;
                    if (type == FileType::AudioFile || type == FileType::MusicFile ||
                        type == FileType::WavFile || type == FileType::Mp3File ||
                        type == FileType::FlacFile) {
                        onSoundPreview_(*selectedFile_);
                    }
                }
                
                // Double-click removed - use Enter key to load files
                
                setDirty(true);
                return true;
            }
        }
    }
    
    return false;
}

bool FileBrowser::onKeyEvent(const NUIKeyEvent& event) {
    if (!event.pressed) return false;
    
    Log::info("FileBrowser received key: " + std::to_string(static_cast<int>(event.keyCode)));
    
    switch (event.keyCode) {
        case NUIKeyCode::Up:
            if (selectedIndex_ > 0) {
                selectedIndex_--;
                selectedFile_ = &files_[selectedIndex_];
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
            if (selectedIndex_ < static_cast<int>(files_.size()) - 1) {
                selectedIndex_++;
                selectedFile_ = &files_[selectedIndex_];
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
            
        case NUIKeyCode::Left:
            // Navigate up one directory level
            navigateUp();
            return true;
            
        case NUIKeyCode::Right:
            // Navigate into selected folder (if it's a directory)
            if (selectedFile_ && selectedFile_->isDirectory) {
                navigateTo(selectedFile_->path);
                return true;
            }
            break;
            
        case NUIKeyCode::Enter:
            Log::info("FileBrowser: Enter key pressed, selectedFile_ = " + 
                     (selectedFile_ ? selectedFile_->name : "nullptr"));
            if (selectedFile_) {
                Log::info("  isDirectory: " + std::string(selectedFile_->isDirectory ? "true" : "false"));
                if (selectedFile_->isDirectory) {
                    navigateTo(selectedFile_->path);
                } else {
                    Log::info("  Calling onFileOpened callback");
                    if (onFileOpened_) {
                        onFileOpened_(*selectedFile_);
                    } else {
                        Log::warning("  onFileOpened callback is nullptr!");
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
    currentPath_ = path;
    loadDirectoryContents();
    if (onPathChanged_) {
        onPathChanged_(currentPath_);
    }
    setDirty(true);
}

void FileBrowser::refresh() {
    loadDirectoryContents();
    setDirty(true);
}

void FileBrowser::navigateUp() {
    std::filesystem::path parent = std::filesystem::path(currentPath_).parent_path();
    if (parent != currentPath_) {
        setCurrentPath(parent.string());
    }
}

void FileBrowser::navigateTo(const std::string& path) {
    if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
        setCurrentPath(path);
    }
}

void FileBrowser::selectFile(const std::string& path) {
    for (size_t i = 0; i < files_.size(); ++i) {
        if (files_[i].path == path) {
            selectedIndex_ = static_cast<int>(i);
            selectedFile_ = &files_[i];
            updateScrollPosition();
            if (onFileSelected_) {
                onFileSelected_(*selectedFile_);
            }
            setDirty(true);
            break;
        }
    }
}

void FileBrowser::openFile(const std::string& path) {
    selectFile(path);
    if (selectedFile_ && onFileOpened_) {
        onFileOpened_(*selectedFile_);
    }
}

void FileBrowser::openFolder(const std::string& path) {
    navigateTo(path);
}

void FileBrowser::setSortMode(SortMode mode) {
    sortMode_ = mode;
    sortFiles();
    setDirty(true);
}

void FileBrowser::setSortAscending(bool ascending) {
    sortAscending_ = ascending;
    sortFiles();
    setDirty(true);
}

void FileBrowser::loadDirectoryContents() {
    files_.clear();
    selectedFile_ = nullptr;
    selectedIndex_ = -1;
    
    try {
        std::filesystem::path currentDir(currentPath_);
        
        // Add parent directory entry if not at root
        if (currentDir.has_parent_path() && currentDir.parent_path() != currentDir) {
            files_.emplace_back("..", currentDir.parent_path().string(), FileType::Folder, true);
        }
        
        // Iterate through directory contents
        // CRITICAL: Wrap in try-catch to handle Windows permission errors gracefully
        try {
            for (const auto& entry : std::filesystem::directory_iterator(currentDir)) {
                if (!showHiddenFiles_ && entry.path().filename().string()[0] == '.') {
                    continue;
                }
                
                std::string name = entry.path().filename().string();
                std::string path = entry.path().string();
                bool isDir = false;
                
                // Safely check if directory (can throw on permission errors)
                try {
                    isDir = entry.is_directory();
                } catch (const std::filesystem::filesystem_error&) {
                    // Skip entries we can't access
                    continue;
                }
                
                FileType type = FileType::Unknown;
                size_t size = 0;
                std::string lastModified;
                
                if (isDir) {
                    type = FileType::Folder;
                } else {
                    std::string extension = entry.path().extension().string();
                    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
                    
                    type = getFileTypeFromExtension(extension);
                    
                    // Safely get file size (can throw on permission errors)
                    try {
                        size = entry.file_size();
                    } catch (const std::filesystem::filesystem_error&) {
                        size = 0; // Default to 0 if can't read
                    }
                    
                    // Get last modified time with error handling
                    try {
                        auto time = std::filesystem::last_write_time(entry);
                        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                            time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
                        auto time_t = std::chrono::system_clock::to_time_t(sctp);
                        std::stringstream ss;
                        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M");
                        lastModified = ss.str();
                    } catch (const std::filesystem::filesystem_error&) {
                        lastModified = "Unknown";
                    }
                }
                
                files_.emplace_back(name, path, type, isDir, size, lastModified);
            }
        } catch (const std::filesystem::filesystem_error& e) {
            // Directory iteration itself failed - likely permission denied
            std::cerr << "Error iterating directory: " << e.what() << std::endl;
            // Show helpful error message to user
            if (files_.empty()) {
                files_.emplace_back("âš ï¸ Access Denied", "", FileType::Unknown, false);
                files_.emplace_back("Cannot read this directory", "", FileType::Unknown, false);
            }
        }
        
        sortFiles();
        
        // Auto-select first item for keyboard navigation
        if (!files_.empty()) {
            selectedIndex_ = 0;
            selectedFile_ = &files_[0];
        }
    }
    catch (const std::exception& e) {
        // Handle directory access errors
        std::cerr << "Error loading directory: " << e.what() << std::endl;
    }
    
    // Update scrollbar visibility after loading files
    updateScrollbarVisibility();
}

void FileBrowser::sortFiles() {
    std::sort(files_.begin(), files_.end(), [this](const FileItem& a, const FileItem& b) {
        // Always put directories first
        if (a.isDirectory != b.isDirectory) {
            return a.isDirectory > b.isDirectory;
        }
        
        // Handle ".." entry
        if (a.name == "..") return true;
        if (b.name == "..") return false;
        
        bool result = false;
        
        switch (sortMode_) {
            case SortMode::Name:
                result = a.name < b.name;
                break;
            case SortMode::Type:
                result = a.type < b.type;
                break;
            case SortMode::Size:
                result = a.size < b.size;
                break;
            case SortMode::Modified:
                result = a.lastModified < b.lastModified;
                break;
        }
        
        return sortAscending_ ? result : !result;
    });
}

FileType FileBrowser::getFileTypeFromExtension(const std::string& extension) {
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
    float headerHeight = themeManager.getComponentDimension("fileBrowser", "headerHeight");
    float itemHeight = themeManager.getComponentDimension("fileBrowser", "itemHeight");
    float listY = bounds.y + headerHeight + 8 + 20; // After toolbar + spacing + path bar height
    float listHeight = bounds.height - headerHeight - 8 - 20;

    // CRITICAL: Check if width changed - invalidate ALL caches if so
    if (std::abs(lastCachedWidth_ - bounds.width) > 0.1f) {
        for (auto& file : files_) {
            file.invalidateCache();
        }
        lastCachedWidth_ = bounds.width;
    }

    // OPTIMIZATION: Only render visible items (virtualization)
    int firstVisibleIndex = std::max(0, static_cast<int>(scrollOffset_ / itemHeight));
    int lastVisibleIndex = std::min(static_cast<int>(files_.size()), 
                                     static_cast<int>((scrollOffset_ + listHeight) / itemHeight) + 1);

    // Render file items (ONLY VISIBLE ONES!) - NO CLIPPING, manual bounds check instead
    for (int i = firstVisibleIndex; i < lastVisibleIndex; ++i) {
        // Calculate item position: each item is positioned at listY + (i * itemHeight) - scrollOffset
        float itemY = listY + (i * itemHeight) - scrollOffset_;

        // CRITICAL: Skip rendering if item would bleed above the list area (into path bar)
        if (itemY < listY) {
            continue;
        }

        // Create item rect with proper dimensions
        NUIRect itemRect(bounds.x + layout.panelMargin, itemY, bounds.width - 2 * layout.panelMargin, itemHeight);
        
        // Enhanced selection highlighting with purple accent (matches Audio Settings border)
        if (i == selectedIndex_) {
            // Use purple accent color (same as Audio Settings dialog border)
            NomadUI::NUIColor purpleAccent = themeManager.getColor("accent");  // Purple accent
            
            // Heavy selection highlight with purple accent
            renderer.fillRoundedRect(itemRect, 4, purpleAccent.withAlpha(0.3f));
            renderer.strokeRoundedRect(itemRect, 4, 2, purpleAccent.withAlpha(0.8f));
            
            // Left accent bar in purple for selected items
            NUIRect accentBar(itemRect.x, itemRect.y, 3, itemRect.height);
            renderer.fillRoundedRect(accentBar, 1, purpleAccent);
        } else if (i == hoveredIndex_) {
            // Hover highlight with subtle grey background
            renderer.fillRoundedRect(itemRect, 4, hoverColor_.withAlpha(0.4f));
            renderer.strokeRoundedRect(itemRect, 4, 1, hoverColor_.withAlpha(0.6f));
        } else {
            // Subtle background for all items
            renderer.fillRoundedRect(itemRect, 4, backgroundColor_.withAlpha(0.02f));
        }
        
        // Add subtle divider line between items (except for the last item)
        if (i < files_.size() - 1) {
            NUIRect dividerRect(itemRect.x + layout.panelMargin, itemRect.y + itemRect.height - 1, itemRect.width - 2 * layout.panelMargin, 1);
            renderer.fillRect(dividerRect, NUIColor(0.0f, 0.0f, 0.0f, 0.3f)); // Black divider
        }
        
        // Render icon
        auto icon = getIconForFileType(files_[i].type);
        if (icon) {
            float iconSize = themeManager.getComponentDimension("fileBrowser", "iconSize");
            NUIRect iconRect(itemRect.x + layout.panelMargin, itemY + 4, iconSize, iconSize);
            icon->setBounds(iconRect);
            icon->onRender(renderer);
        } else {
            // Debug: Draw a simple rectangle if no icon
            float iconSize = themeManager.getComponentDimension("fileBrowser", "iconSize");
            renderer.fillRect(NUIRect(itemRect.x + layout.panelMargin, itemY + 4, iconSize, iconSize), NUIColor::white().withAlpha(0.3f));
        }

        // Render file name with proper vertical alignment and bounds checking to prevent bleeding
        float textX = itemRect.x + layout.panelMargin + themeManager.getComponentDimension("fileBrowser", "iconSize") + 8; // Icon size + margin
        float textY = itemY + itemHeight / 2 + 7; // Perfect vertical centering for 14px font (baseline)
        
        // OPTIMIZATION: Cache file size string and display name
        auto& fileItem = files_[i];
        if (!fileItem.cacheValid) {
            // Calculate file size string (cache it)
            fileItem.cachedSizeStr.clear();
            bool hasSize = !fileItem.isDirectory && fileItem.size > 0;
            
            if (hasSize) {
                if (fileItem.size < 1024) {
                    fileItem.cachedSizeStr = std::to_string(fileItem.size) + " B";
                } else if (fileItem.size < 1024 * 1024) {
                    fileItem.cachedSizeStr = std::to_string(fileItem.size / 1024) + " KB";
                } else {
                    fileItem.cachedSizeStr = std::to_string(fileItem.size / (1024 * 1024)) + " MB";
                }
            }
            
            // Calculate display name with truncation (cache it)
            float iconWidth = themeManager.getComponentDimension("fileBrowser", "iconSize");
            float minGap = 20.0f;
            float rightMargin = 12.0f;
            float actualSizeWidth = hasSize ? renderer.measureText(fileItem.cachedSizeStr, 12).width : 0.0f;
            float reservedForSize = hasSize ? (actualSizeWidth + minGap + rightMargin) : rightMargin;
            float maxTextWidth = itemRect.width - layout.panelMargin - iconWidth - 8 - reservedForSize;
            
            fileItem.cachedDisplayName = fileItem.name;
            auto nameTextSize = renderer.measureText(fileItem.cachedDisplayName, 14);
            
            if (nameTextSize.width > maxTextWidth) {
                // Truncate with ellipsis
                std::string truncated = fileItem.name;
                while (truncated.length() > 3) {
                    truncated.pop_back();
                    NUISize truncSize = renderer.measureText(truncated + "...", 14);
                    if (truncSize.width <= maxTextWidth) break;
                }
                fileItem.cachedDisplayName = truncated + "...";
            }
            
            fileItem.cacheValid = true;
        }
        
        // Add hover effect
        NUIColor nameColor = textColor_;
        if (i == selectedIndex_) {
            nameColor = NUIColor::white(); // Bright white for selected
        }
        
        // USE CACHED display name
        renderer.drawText(fileItem.cachedDisplayName, NUIPoint(textX, textY), 14, nameColor);
        
        // Render file size (only if cached size exists)
        if (!fileItem.cachedSizeStr.empty()) {
            auto sizeText = renderer.measureText(fileItem.cachedSizeStr, 12);
            
            // Calculate position from the right
            float rightMargin = 12.0f;
            float sizeX = itemRect.x + itemRect.width - sizeText.width - rightMargin;
            
            // Render size
            renderer.drawText(fileItem.cachedSizeStr, NUIPoint(sizeX, textY), 12, textColor_.withAlpha(0.7f));
        }
    }
}

void FileBrowser::renderPathBar(NUIRenderer& renderer) {
    // Get layout dimensions from theme
    auto& themeManager = NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    float headerHeight = themeManager.getComponentDimension("fileBrowser", "headerHeight");

    NUIRect bounds = getBounds();
    float toolbarHeight = headerHeight; // Use theme headerHeight instead of hardcoded
    NUIRect pathRect(bounds.x + layout.panelMargin, bounds.y + toolbarHeight + 8, bounds.width - 2 * layout.panelMargin, 20);

    // Render path bar background with enhanced styling
    renderer.fillRoundedRect(pathRect, 4, backgroundColor_.darkened(0.15f));
    renderer.strokeRoundedRect(pathRect, 4, 1, borderColor_.withAlpha(0.6f));

    // Render current path with proper vertical alignment and padding
    float textY = pathRect.y + pathRect.height / 2 + 4; // Better centering
    float textX = pathRect.x + layout.panelMargin; // Configurable padding from left edge
    float maxTextWidth = pathRect.width - 2 * layout.panelMargin; // Account for configurable padding on both sides

    // Truncate text if it's too long to prevent bleeding
    std::string displayPath = currentPath_;
    if (displayPath.length() > 40) { // More aggressive truncation
        displayPath = "..." + displayPath.substr(displayPath.length() - 37);
    }

    // Measure text and ensure it fits within bounds
    auto pathTextSize = renderer.measureText(displayPath, 12);
    if (pathTextSize.width > maxTextWidth) {
        // Further truncate if still too long
        std::string truncatedPath = displayPath;
        while (!truncatedPath.empty() && renderer.measureText(truncatedPath, 12).width > maxTextWidth) {
            truncatedPath = truncatedPath.substr(0, truncatedPath.length() - 1);
        }
        displayPath = truncatedPath + "...";
    }

    // Ensure text fits within bounds
    renderer.drawText(displayPath, NUIPoint(textX, textY), 12, textColor_);
}

void FileBrowser::renderToolbar(NUIRenderer& renderer) {
    // Get layout dimensions from theme
    auto& themeManager = NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    float headerHeight = themeManager.getComponentDimension("fileBrowser", "headerHeight");

    NUIRect bounds = getBounds();
    float toolbarHeight = headerHeight; // Use theme headerHeight instead of hardcoded
    NUIRect toolbarRect(bounds.x + layout.panelMargin, bounds.y + layout.panelMargin, bounds.width - 2 * layout.panelMargin, toolbarHeight);

    // Render toolbar background with enhanced styling
    renderer.fillRoundedRect(toolbarRect, 4, backgroundColor_.darkened(0.08f));
    renderer.strokeRoundedRect(toolbarRect, 4, 1, borderColor_.withAlpha(0.3f));

    // Render refresh button with proper vertical alignment and bounds checking
    float textY = toolbarRect.y + toolbarRect.height / 2 + 6; // Perfect centering for 12px font (baseline)
    
    // Measure refresh text and ensure it fits
    auto refreshTextSize = renderer.measureText("Refresh", 12);
    float maxRefreshWidth = toolbarRect.width * 0.3f; // Limit to 30% of toolbar width
    if (refreshTextSize.width > maxRefreshWidth) {
        std::string truncatedRefresh = "Refresh";
        while (!truncatedRefresh.empty() && renderer.measureText(truncatedRefresh, 12).width > maxRefreshWidth) {
            truncatedRefresh = truncatedRefresh.substr(0, truncatedRefresh.length() - 1);
        }
        renderer.drawText(truncatedRefresh + "...", NUIPoint(toolbarRect.x + layout.panelMargin, textY), 12, textColor_);
    } else {
        renderer.drawText("Refresh", NUIPoint(toolbarRect.x + layout.panelMargin, textY), 12, textColor_);
    }

    // Render sort mode indicator with proper alignment and bounds checking
    std::string sortText = "Sort: ";
    switch (sortMode_) {
        case SortMode::Name: sortText += "Name"; break;
        case SortMode::Type: sortText += "Type"; break;
        case SortMode::Size: sortText += "Size"; break;
        case SortMode::Modified: sortText += "Modified"; break;
    }
    sortText += sortAscending_ ? " â†‘" : " â†“";

    auto sortTextSize = renderer.measureText(sortText, 12);
    
    // Ensure sort text doesn't exceed toolbar bounds
    float maxSortWidth = toolbarRect.width * 0.4f; // Limit to 40% of toolbar width
    if (sortTextSize.width > maxSortWidth) {
        std::string truncatedSort = sortText;
        while (!truncatedSort.empty() && renderer.measureText(truncatedSort, 12).width > maxSortWidth) {
            truncatedSort = truncatedSort.substr(0, truncatedSort.length() - 1);
        }
        sortText = truncatedSort + "...";
        sortTextSize = renderer.measureText(sortText, 12);
    }
    
    // Position sort text to ensure it fits within bounds
    float sortX = std::max(toolbarRect.x + layout.panelMargin, toolbarRect.x + toolbarRect.width - sortTextSize.width - layout.panelMargin);
    renderer.drawText(sortText, NUIPoint(sortX, textY), 12, textColor_.withAlpha(0.7f));
}

void FileBrowser::updateScrollPosition() {
    if (selectedIndex_ < 0) return;

    // Get component dimensions from theme
    auto& themeManager = NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();

    NUIRect bounds = getBounds();
    float headerHeight = themeManager.getComponentDimension("fileBrowser", "headerHeight");
    float itemHeight = themeManager.getComponentDimension("fileBrowser", "itemHeight");
    float listY = bounds.y + headerHeight + 8 + 20; // After path bar
    float listHeight = bounds.height - headerHeight - 8 - 20;

    float itemY = listY + (selectedIndex_ * itemHeight) - scrollOffset_;

    // Scroll up if item is above visible area
    if (itemY < listY) {
        scrollOffset_ = selectedIndex_ * itemHeight;
    }
    // Scroll down if item is below visible area
    else if (itemY + itemHeight > listY + listHeight) {
        scrollOffset_ = (selectedIndex_ + 1) * itemHeight - listHeight;
    }

    // Clamp scroll offset
    float maxScroll = std::max(0.0f, (files_.size() * itemHeight_) - listHeight);
    scrollOffset_ = std::max(0.0f, std::min(scrollOffset_, maxScroll));

    // Update scrollbar visibility and position
    updateScrollbarVisibility();
}

void FileBrowser::renderScrollbar(NUIRenderer& renderer) {
    // Get component dimensions from theme
    auto& themeManager = NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    float itemHeight = themeManager.getComponentDimension("fileBrowser", "itemHeight");
    float headerHeight = themeManager.getComponentDimension("fileBrowser", "headerHeight");

    // Check if we need a scrollbar
    float contentHeight = files_.size() * itemHeight;
    float maxScroll = std::max(0.0f, contentHeight - scrollbarTrackHeight_);
    bool needsScrollbar = maxScroll > 0.0f;

    if (!needsScrollbar || files_.size() == 0) return;

    // Position scrollbar FLUSH with the right edge (no gap)
    NUIRect bounds = getBounds();
    float scrollbarX = bounds.x + bounds.width - scrollbarWidth_;  // FLUSH alignment - no panelMargin
    float scrollbarY = bounds.y + headerHeight + 8 + 20; // After path bar
    float scrollbarHeight = bounds.height - headerHeight - 8 - 20;

    // Render solid background layer beneath scrollbar to prevent selection bleed
    NUIColor bgColor = themeManager.getColor("backgroundSecondary");  // Solid background
    renderer.fillRoundedRect(NUIRect(scrollbarX, scrollbarY, scrollbarWidth_, scrollbarHeight),
                             4, bgColor);

    // Render scrollbar track on top of background
    NUIColor trackColor = themeManager.getColor("backgroundSecondary").withAlpha(0.8f);
    renderer.fillRoundedRect(NUIRect(scrollbarX, scrollbarY, scrollbarWidth_, scrollbarHeight),
                             4, trackColor);

    // Render scrollbar thumb with faded white color using CACHED position
    NUIColor thumbColor = NUIColor(0.8f, 0.8f, 0.8f, 0.8f); // Faded white thumb color
    float thumbY = scrollbarY + scrollbarThumbY_; // Use cached thumb position!
    renderer.fillRoundedRect(NUIRect(scrollbarX, thumbY, scrollbarWidth_, scrollbarThumbHeight_),
                             4, thumbColor);
}

bool FileBrowser::handleScrollbarMouseEvent(const NUIMouseEvent& event) {
    // Get component dimensions from theme
    auto& themeManager = NUIThemeManager::getInstance();

    NUIRect bounds = getBounds();
    float headerHeight = themeManager.getComponentDimension("fileBrowser", "headerHeight");
    float scrollbarX = bounds.x + bounds.width - scrollbarWidth_;  // FLUSH alignment - use member variable
    float scrollbarY = bounds.y + headerHeight + 8 + 20; // After path bar
    
    // Debug: Print scrollbar dimensions
    // static bool printed = false;
    // if (!printed && event.pressed) {
    //     printf("Scrollbar Debug:\n");
    //     printf("  scrollbarX: %.1f, scrollbarWidth_: %.1f\n", scrollbarX, scrollbarWidth_);
    //     printf("  scrollbarY: %.1f, scrollbarTrackHeight_: %.1f\n", scrollbarY, scrollbarTrackHeight_);
    //     printf("  Mouse: %.1f, %.1f\n", event.position.x, event.position.y);
    //     printed = true;
    // }
    
    // Use the member variable scrollbarTrackHeight_ for consistency
    // It's set in onResize() and used for thumb calculation

    // If we're dragging, continue dragging regardless of mouse position
    if (isDraggingScrollbar_) {
        // Stop dragging on mouse release (anywhere, not just in scrollbar area)
        if (!event.pressed && event.button == NUIMouseButton::Left) {
            isDraggingScrollbar_ = false;
            return true;
        }

        // Continue dragging even if mouse is outside scrollbar area
        float deltaY = event.position.y - dragStartY_;
        float scrollRatio = deltaY / scrollbarTrackHeight_;
        float itemHeight = themeManager.getComponentDimension("fileBrowser", "itemHeight");
        float maxScroll = std::max(0.0f, (files_.size() * itemHeight) - scrollbarTrackHeight_);

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

    if (!inScrollbarArea) {
        return false;
    }


    if (event.pressed && event.button == NUIMouseButton::Left) {
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
            float maxScroll = std::max(0.0f, (files_.size() * itemHeight) - scrollbarTrackHeight_);
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

    // Calculate if we need a scrollbar
    float contentHeight = files_.size() * itemHeight;
    float maxScroll = std::max(0.0f, contentHeight - scrollbarTrackHeight_);
    bool needsScrollbar = maxScroll > 0.0f;

    if (needsScrollbar) {
        scrollbarVisible_ = true;
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
    }
}

} // namespace NomadUI
