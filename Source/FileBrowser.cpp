// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "FileBrowser.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Core/NUIDragDrop.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "../NomadCore/include/NomadLog.h"
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <cmath>
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
    , scrollVelocity_(0.0f)
    , itemHeight_(32.0f)           // Initialize to default theme value
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
    , dragStartY_(0.0f)
    , dragStartScrollOffset_(0.0f)
    , scrollbarFadeTimer_(0.0f)
    , hoveredIndex_(-1)
    , lastClickedIndex_(-1)
    , lastClickTime_(0.0)
    , sortMode_(SortMode::Name)
    , sortAscending_(true)
    , lastShiftSelectIndex_(-1)
    , searchBoxFocused_(false)
    , searchBoxWidth_(180.0f)
    , previewPanelVisible_(false)  // Disabled - preview panel causes layout glitches
    , previewPanelWidth_(220.0f)
    , hoveredBreadcrumbIndex_(-1)
    , navHistoryIndex_(-1)
    , isNavigatingHistory_(false)
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
    
    // Search icon
    searchIcon_ = std::make_shared<NUIIcon>();
    const char* searchSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M15.5 14h-.79l-.28-.27C15.41 12.59 16 11.11 16 9.5 16 5.91 13.09 3 9.5 3S3 5.91 3 9.5 5.91 16 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z"/>
        </svg>
    )";
    searchIcon_->loadSVG(searchSvg);
    searchIcon_->setIconSize(20, 20);
    searchIcon_->setColor(themeManager.getColor("textSecondary"));
    
    // Star icon (outline)
    starIcon_ = std::make_shared<NUIIcon>();
    const char* starSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M22 9.24l-7.19-.62L12 2 9.19 8.63 2 9.24l5.46 4.73L5.82 21 12 17.27 18.18 21l-1.63-7.03L22 9.24zM12 15.4l-3.76 2.27 1-4.28-3.32-2.88 4.38-.38L12 6.1l1.71 4.04 4.38.38-3.32 2.88 1 4.28L12 15.4z"/>
        </svg>
    )";
    starIcon_->loadSVG(starSvg);
    starIcon_->setIconSize(16, 16);
    starIcon_->setColor(themeManager.getColor("textSecondary"));
    
    // Star filled icon
    starFilledIcon_ = std::make_shared<NUIIcon>();
    const char* starFilledSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M12 17.27L18.18 21l-1.64-7.03L22 9.24l-7.19-.61L12 2 9.19 8.63 2 9.24l5.46 4.73L5.82 21z"/>
        </svg>
    )";
    starFilledIcon_->loadSVG(starFilledSvg);
    starFilledIcon_->setIconSize(16, 16);
    starFilledIcon_->setColor(themeManager.getColor("warning")); // Gold/yellow color
    
    // Chevron icon for breadcrumbs
    chevronIcon_ = std::make_shared<NUIIcon>();
    const char* chevronSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M10 6L8.59 7.41 13.17 12l-4.58 4.59L10 18l6-6z"/>
        </svg>
    )";
    chevronIcon_->loadSVG(chevronSvg);
    chevronIcon_->setIconSize(12, 12);
    chevronIcon_->setColor(themeManager.getColor("textSecondary"));
    
    // Chevron down icon for expanded folders
    chevronDownIcon_ = std::make_shared<NUIIcon>();
    const char* chevronDownSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M7.41 8.59L12 13.17l4.59-4.58L18 10l-6 6-6-6 1.41-1.41z"/>
        </svg>
    )";
    chevronDownIcon_->loadSVG(chevronDownSvg);
    chevronDownIcon_->setIconSize(12, 12);
    chevronDownIcon_->setColor(themeManager.getColor("textSecondary"));
    
    // Play icon for preview panel
    playIcon_ = std::make_shared<NUIIcon>();
    const char* playSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M8 5v14l11-7z"/>
        </svg>
    )";
    playIcon_->loadSVG(playSvg);
    playIcon_->setIconSize(16, 16);
    playIcon_->setColor(themeManager.getColor("primary"));
    
    // Pause icon for preview panel
    pauseIcon_ = std::make_shared<NUIIcon>();
    const char* pauseSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M6 19h4V5H6v14zm8-14v14h4V5h-4z"/>
        </svg>
    )";
    pauseIcon_->loadSVG(pauseSvg);
    pauseIcon_->setIconSize(16, 16);
    pauseIcon_->setColor(themeManager.getColor("primary"));

    // Back icon
    backIcon_ = std::make_shared<NUIIcon>();
    const char* backSvg = R"(<svg viewBox="0 0 24 24"><path d="M20 11H7.83l5.59-5.59L12 4l-8 8 8 8 1.41-1.41L7.83 13H20v-2z"/></svg>)";
    backIcon_->loadSVG(backSvg);
    backIcon_->setIconSize(20, 20);
    backIcon_->setColor(themeManager.getColor("textSecondary"));

    // Forward icon
    forwardIcon_ = std::make_shared<NUIIcon>();
    const char* forwardSvg = R"(<svg viewBox="0 0 24 24"><path d="M12 4l-1.41 1.41L16.17 11H4v2h12.17l-5.58 5.59L12 20l8-8z"/></svg>)";
    forwardIcon_->loadSVG(forwardSvg);
    forwardIcon_->setIconSize(20, 20);
    forwardIcon_->setColor(themeManager.getColor("textSecondary"));
    
    // Set initial path to current directory
    currentPath_ = std::filesystem::current_path().string();
    
    // Initialize navigation history
    navHistory_.push_back(currentPath_);
    navHistoryIndex_ = 0;

    loadDirectoryContents();
    
    // Load Liminal Dark v2.0 theme colors
    backgroundColor_ = themeManager.getColor("backgroundSecondary");  // #1b1b1f - Panels, sidebars, file browser
    textColor_ = themeManager.getColor("textPrimary");                // #e6e6eb - Soft white
    
    // Use a refined purple accent to match the icons
    selectedColor_ = NUIColor(0.733f, 0.525f, 0.988f, 1.0f);          // #bb86fc - Matching the folder icons
    
    hoverColor_ = themeManager.getColor("surfaceRaised").lightened(0.05f); // Slightly lighter for better visibility
    borderColor_ = themeManager.getColor("border");                   // #2e2e35 - Subtle separation lines
}

void FileBrowser::onRender(NUIRenderer& renderer) {
    NUIRect bounds = getBounds();
    if (bounds.isEmpty()) return;
    
    // Adjust bounds if preview panel is visible
    float fileBrowserWidth = bounds.width;
    if (previewPanelVisible_ && selectedFile_ && !selectedFile_->isDirectory) {
        fileBrowserWidth -= previewPanelWidth_ + 8;  // Subtract preview panel width + spacing
    }
    effectiveWidth_ = fileBrowserWidth; // Store for child render functions
    
    NUIRect fileBrowserBounds(bounds.x, bounds.y, fileBrowserWidth, bounds.height);
    
    // Render background with enhanced borders
    renderer.fillRoundedRect(fileBrowserBounds, 8, backgroundColor_);
    
    // Main border
    renderer.strokeRoundedRect(fileBrowserBounds, 8, 1, borderColor_);
    
    // Inner black border for cleaner look
    NUIRect innerBounds(fileBrowserBounds.x + 1, fileBrowserBounds.y + 1, fileBrowserBounds.width - 2, fileBrowserBounds.height - 2);
    renderer.strokeRoundedRect(innerBounds, 7, 1, NUIColor(0.0f, 0.0f, 0.0f, 0.4f));
    
    // Render toolbar with search box
    renderToolbar(renderer);
    
    // Render breadcrumbs (instead of simple path bar)
    renderInteractiveBreadcrumbs(renderer);
    
    // Render file list
    renderFileList(renderer);
    
    // Render scrollbar
    renderScrollbar(renderer);
    
    // Render preview panel if visible and file is selected
    if (previewPanelVisible_ && selectedFile_ && !selectedFile_->isDirectory) {
        renderPreviewPanel(renderer);
    }
}

void FileBrowser::onUpdate(double deltaTime) {
    NUIComponent::onUpdate(deltaTime);
    
    // Smooth scrolling with lerp
    float lerpSpeed = 12.0f;
    float snapThreshold = 0.5f;

    float scrollDelta = targetScrollOffset_ - scrollOffset_;
    if (std::abs(scrollDelta) > snapThreshold) {
        float step = std::min(1.0f, static_cast<float>(deltaTime * lerpSpeed));
        scrollOffset_ += scrollDelta * step;
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
    const auto& view = searchQuery_.empty() ? displayItems_ : filteredFiles_;
    float maxScroll = std::max(0.0f, view.size() * itemHeight_ - scrollbarTrackHeight_);
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
    const auto& view = searchQuery_.empty() ? displayItems_ : filteredFiles_;
    auto& themeManager = NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    float headerHeight = themeManager.getComponentDimension("fileBrowser", "headerHeight");
    float itemHeight = themeManager.getComponentDimension("fileBrowser", "itemHeight");
    float listY = bounds.y + headerHeight + 8 + 30; // After path bar
    float listHeight = bounds.height - headerHeight - 8 - 30;
    
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
    
    // Check for potential drag initiation (mouse moved while button held)
    if (dragPotential_ && dragSourceIndex_ >= 0 && dragSourceIndex_ < static_cast<int>(view.size())) {
        float dx = event.position.x - dragStartPos_.x;
        float dy = event.position.y - dragStartPos_.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        
        if (dist >= dragManager.getDragThreshold()) {
            // Start the drag!
            FileItem* dragFile = view[dragSourceIndex_];
            
            // Only drag audio files, not folders
            if (!dragFile->isDirectory) {
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
                
                Log::info("[FileBrowser] Started drag: " + dragFile->name);
                return true;
            }
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

    // Clear hover if mouse leaves the file browser entirely (but allow scrollbar dragging)
    if (!mouseInside && !isDraggingScrollbar_) {
        if (hoveredIndex_ != -1) {
            hoveredIndex_ = -1;
            setDirty(true); // Trigger redraw when clearing hover
        }
        return false;
    }
    
    // Handle scrollbar mouse events first - check if scrollbar should be visible
    float contentHeight = view.size() * itemHeight;
    float maxScroll = std::max(0.0f, contentHeight - scrollbarTrackHeight_);
    bool needsScrollbar = maxScroll > 0.0f;
    
    // Handle mouse wheel scrolling first (works anywhere in the file browser)
    if (event.wheelDelta != 0) {
        float scrollSpeed = 2.0f; // Scroll 2 items per wheel step
        float scrollDelta = event.wheelDelta * scrollSpeed * itemHeight;
        
        targetScrollOffset_ -= scrollDelta;

        // Clamp target scroll offset
        float maxScroll = std::max(0.0f, (view.size() * itemHeight) - listHeight);
        targetScrollOffset_ = std::max(0.0f, std::min(targetScrollOffset_, maxScroll));

        // Let onUpdate handle interpolation and thumb position
        setDirty(true);
        return true;
    }

    // Navigation buttons (Back/Forward) in toolbar
    if (event.pressed && event.button == NUIMouseButton::Left) {
        float startX = bounds.x + layout.panelMargin + 4.0f;
        float startY = bounds.y + layout.panelMargin + (headerHeight - 20.0f) * 0.5f;

        if (event.position.x >= startX && event.position.x <= startX + 24 &&
            event.position.y >= startY && event.position.y <= startY + 24) {
            navigateBack();
            return true;
        }
        
        if (event.position.x >= startX + 28 && event.position.x <= startX + 52 &&
            event.position.y >= startY && event.position.y <= startY + 24) {
            navigateForward();
            return true;
        }
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
    if (event.position.x >= bounds.x && event.position.x <= bounds.x + bounds.width &&
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

        if (event.pressed && event.button == NUIMouseButton::Left) {
            // Calculate which file was clicked
            float relativeY = event.position.y - listY;
            int itemIndex = static_cast<int>((relativeY + scrollOffset_) / itemHeight);
            
            if (itemIndex >= 0 && itemIndex < static_cast<int>(view.size())) {
                FileItem* clickedFile = view[itemIndex];
                
                // Check for expander click
                float indent = clickedFile->depth * 20.0f;
                float arrowX = bounds.x + layout.panelMargin + indent;
                float arrowSize = 16.0f;
                
                if (clickedFile->isDirectory && 
                    event.position.x >= arrowX && event.position.x <= arrowX + arrowSize) {
                    toggleFolder(clickedFile);
                    return true;
                }

                // Store drag potential state
                if (!clickedFile->isDirectory) {
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
                const auto& activeView = searchQuery_.empty() ? displayItems_ : filteredFiles_;
                if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(activeView.size())) {
                    selectedFile_ = activeView[selectedIndex_];
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
    if (!event.pressed) return false;
    
    Log::info("FileBrowser received key: " + std::to_string(static_cast<int>(event.keyCode)));
    
    const auto& view = searchQuery_.empty() ? displayItems_ : filteredFiles_;

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
            Log::info("FileBrowser: Enter key pressed, selectedFile_ = " + 
                     (selectedFile_ ? selectedFile_->name : "nullptr"));
            if (selectedFile_) {
                Log::info("  isDirectory: " + std::string(selectedFile_->isDirectory ? "true" : "false"));
                if (selectedFile_->isDirectory) {
                    toggleFolder(const_cast<FileItem*>(selectedFile_));
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
    if (currentPath_ == path) {
        return;
    }

    currentPath_ = path;
    
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
    for (size_t i = 0; i < displayItems_.size(); ++i) {
        if (displayItems_[i]->path == path) {
            selectedIndex_ = static_cast<int>(i);
            selectedFile_ = displayItems_[i];
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
    rootItems_.clear();
    displayItems_.clear();
    selectedFile_ = nullptr;
    selectedIndex_ = -1;
    selectedIndices_.clear();
    
    try {
        std::filesystem::path currentDir(currentPath_);
        
        // Iterate through directory contents
        try {
            for (const auto& entry : std::filesystem::directory_iterator(currentDir)) {
                if (!showHiddenFiles_ && entry.path().filename().string()[0] == '.') {
                    continue;
                }
                
                std::string name = entry.path().filename().string();
                std::string path = entry.path().string();
                bool isDir = false;
                
                try {
                    isDir = entry.is_directory();
                } catch (const std::filesystem::filesystem_error&) {
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
                    
                    try {
                        size = entry.file_size();
                    } catch (const std::filesystem::filesystem_error&) {
                        size = 0;
                    }
                    
                    // Simplified last modified for brevity/robustness
                    lastModified = ""; 
                }
                
                rootItems_.emplace_back(name, path, type, isDir, size, lastModified);
            }
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Error iterating directory: " << e.what() << std::endl;
        }
        
        sortFiles();
        updateDisplayList();
        
        if (!displayItems_.empty()) {
            selectedIndex_ = 0;
            selectedFile_ = displayItems_[0];
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error loading directory: " << e.what() << std::endl;
    }
    
    if (!searchQuery_.empty()) {
        applyFilter();
    } else {
        filteredFiles_.clear();
    }
    
    updateScrollbarVisibility();
}

void FileBrowser::loadFolderContents(FileItem* item) {
    item->children.clear();
    try {
        for (const auto& entry : std::filesystem::directory_iterator(item->path)) {
            if (!showHiddenFiles_ && entry.path().filename().string()[0] == '.') continue;
            
            std::string name = entry.path().filename().string();
            std::string path = entry.path().string();
            bool isDir = false;
            try { isDir = entry.is_directory(); } catch (...) { continue; }
            
            FileType type = FileType::Unknown;
            if (isDir) type = FileType::Folder;
            else {
                std::string extension = entry.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
                type = getFileTypeFromExtension(extension);
            }
            
            size_t size = 0;
            if (!isDir) try { size = entry.file_size(); } catch (...) {}
            
            FileItem child(name, path, type, isDir, size, "");
            child.depth = item->depth + 1;
            item->children.push_back(child);
        }
        item->hasLoadedChildren = true;
        
        // Sort children
        std::sort(item->children.begin(), item->children.end(), [this](const FileItem& a, const FileItem& b) {
            if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
            
            bool result = false;
            switch (sortMode_) {
                case SortMode::Name: result = a.name < b.name; break;
                case SortMode::Type: result = a.type < b.type; break;
                case SortMode::Size: result = a.size < b.size; break;
                // case SortMode::Modified: result = a.lastModified < b.lastModified; break;
                default: result = a.name < b.name; break;
            }
            return sortAscending_ ? result : !result;
        });
        
    } catch (...) {}
}

void FileBrowser::updateDisplayList() {
    displayItems_.clear();
    for (auto& item : rootItems_) {
        displayItems_.push_back(&item);
        if (item.isExpanded) {
            updateDisplayListRecursive(item, displayItems_);
        }
    }
}

void FileBrowser::updateDisplayListRecursive(FileItem& item, std::vector<FileItem*>& list) {
    for (auto& child : item.children) {
        list.push_back(&child);
        if (child.isExpanded) {
            updateDisplayListRecursive(child, list);
        }
    }
}

void FileBrowser::toggleFolder(FileItem* item) {
    if (!item->isDirectory) return;
    
    if (item->isExpanded) {
        item->isExpanded = false;
    } else {
        if (!item->hasLoadedChildren) {
            loadFolderContents(item);
        }
        item->isExpanded = true;
    }
    updateDisplayList();
    setDirty(true);
}

void FileBrowser::sortFiles() {
    std::sort(rootItems_.begin(), rootItems_.end(), [this](const FileItem& a, const FileItem& b) {
        if (a.isDirectory != b.isDirectory) {
            return a.isDirectory > b.isDirectory;
        }
        
        bool result = false;
        switch (sortMode_) {
            case SortMode::Name: result = a.name < b.name; break;
            case SortMode::Type: result = a.type < b.type; break;
            case SortMode::Size: result = a.size < b.size; break;
            case SortMode::Modified: result = a.lastModified < b.lastModified; break;
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
    // Use effective width to account for preview panel
    float effectiveW = effectiveWidth_ > 0 ? effectiveWidth_ : bounds.width;
    float headerHeight = themeManager.getComponentDimension("fileBrowser", "headerHeight");
    float itemHeight = themeManager.getComponentDimension("fileBrowser", "itemHeight");
    float listY = bounds.y + headerHeight + 8 + 30; // After toolbar + spacing + path bar height
    float listHeight = bounds.height - headerHeight - 8 - 30;
    NUIRect listClip(bounds.x + layout.panelMargin, listY, effectiveW - 2 * layout.panelMargin, listHeight);

    const auto& view = searchQuery_.empty() ? displayItems_ : filteredFiles_;

    // CRITICAL: Check if width changed - invalidate ALL caches if so
    if (std::abs(lastCachedWidth_ - effectiveW) > 0.1f) {
        for (auto* file : displayItems_) {
            file->invalidateCache();
        }
        lastCachedWidth_ = effectiveW;
    }

    // OPTIMIZATION: Only render visible items (virtualization)
    int firstVisibleIndex = std::max(0, static_cast<int>(scrollOffset_ / itemHeight));
    int lastVisibleIndex = std::min(static_cast<int>(view.size()), 
                                     static_cast<int>((scrollOffset_ + listHeight) / itemHeight) + 1);

    auto isSelected = [this](int idx) {
        return std::find(selectedIndices_.begin(), selectedIndices_.end(), idx) != selectedIndices_.end() ||
               idx == selectedIndex_;
    };

    // Clip file items to the list area to prevent bleed
    renderer.setClipRect(listClip);

    // Render file items (ONLY VISIBLE ONES!) - NO CLIPPING, manual bounds check instead
    for (int i = firstVisibleIndex; i < lastVisibleIndex; ++i) {
        // Calculate item position: each item is positioned at listY + (i * itemHeight) - scrollOffset
        float itemY = listY + (i * itemHeight) - scrollOffset_;

        // CRITICAL: Skip rendering if item would bleed above the list area (into path bar)
        if (itemY < listY) {
            continue;
        }

        // Create item rect with proper dimensions (use effectiveW for preview panel support)
        NUIRect itemRect(bounds.x + layout.panelMargin, itemY, effectiveW - 2 * layout.panelMargin, itemHeight);
        bool selected = isSelected(i);
        bool hovered = (i == hoveredIndex_);
        
        FileItem* item = view[i];

        // Background styling
        if (selected) {
            // Selected state: Richer background + Left accent bar
            renderer.fillRoundedRect(itemRect, 4, selectedColor_.withAlpha(0.2f));
            // Left accent bar - slightly thicker and more rounded
            NUIRect accentRect(itemRect.x, itemRect.y + 3, 4.0f, itemRect.height - 6);
            renderer.fillRoundedRect(accentRect, 2.0f, selectedColor_);
        } else if (hovered) {
            // Hover state: Subtle background
            renderer.fillRoundedRect(itemRect, 4, hoverColor_.withAlpha(0.6f));
        } else if (i % 2 == 1) {
            // Alternating rows: Extremely subtle
            renderer.fillRect(itemRect, NUIColor(1.0f, 1.0f, 1.0f, 0.02f));
        }
        
        // Indentation & Tree Lines
        float indentStep = 24.0f; // Increased indentation for better hierarchy
        float indent = item->depth * indentStep;
        float contentX = itemRect.x + layout.panelMargin + indent;
        
        // Draw vertical guide lines for tree structure
        if (item->depth > 0) {
            float lineX = itemRect.x + layout.panelMargin + 12.0f; // Center of first indent level
            for (int d = 0; d < item->depth; ++d) {
                // Draw vertical line segment
                renderer.drawLine(NUIPoint(lineX, itemRect.y), 
                                  NUIPoint(lineX, itemRect.y + itemRect.height), 
                                  1.0f, borderColor_.withAlpha(0.08f)); // Very subtle lines
                lineX += indentStep;
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
            float iconSize = themeManager.getComponentDimension("fileBrowser", "iconSize");
            NUIRect iconRect(contentX, itemY + 4, iconSize, iconSize);
            icon->setBounds(iconRect);
            // Tint folder icons if selected
            if (item->isDirectory && selected) {
                // Keep original color or tint? Let's keep original for now but maybe brighten
            }
            icon->onRender(renderer);
            contentX += iconSize + 8.0f;
        } else {
            float iconSize = themeManager.getComponentDimension("fileBrowser", "iconSize");
            contentX += iconSize + 8.0f;
        }

        // Render file name with proper vertical alignment and bounds checking to prevent bleeding
        float labelFont = 18.0f;
        float metaFont = 16.0f;
        float textX = contentX;
        float textY = itemY + (itemHeight - labelFont) * 0.5f;
        
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
            float iconWidth = themeManager.getComponentDimension("fileBrowser", "iconSize");
            float minGap = 20.0f;
            float rightMargin = 12.0f;
            float actualSizeWidth = hasSize ? renderer.measureText(item->cachedSizeStr, metaFont).width : 0.0f;
            float reservedForSize = hasSize ? (actualSizeWidth + minGap + rightMargin) : rightMargin;
            float maxTextWidth = itemRect.width - (textX - itemRect.x) - reservedForSize;
            
            item->cachedDisplayName = item->name;
            auto nameTextSize = renderer.measureText(item->cachedDisplayName, labelFont);
            
            if (nameTextSize.width > maxTextWidth) {
                // Truncate with ellipsis
                std::string truncated = item->name;
                while (truncated.length() > 3) {
                    truncated.pop_back();
                    NUISize truncSize = renderer.measureText(truncated + "...", labelFont);
                    if (truncSize.width <= maxTextWidth) break;
                }
                item->cachedDisplayName = truncated + "...";
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
        renderer.drawText(item->cachedDisplayName, NUIPoint(textX, textY), labelFont, nameColor);
        
        // Render file size (only if cached size exists)
        if (!item->cachedSizeStr.empty()) {
            auto sizeText = renderer.measureText(item->cachedSizeStr, metaFont);
            
            // Calculate position from the right
            float rightMargin = 12.0f;
            float sizeX = itemRect.x + itemRect.width - sizeText.width - rightMargin;
            
            // Render size
            renderer.drawText(item->cachedSizeStr, NUIPoint(sizeX, textY), metaFont, textColor_.withAlpha(0.5f));
        }
    }

    renderer.clearClipRect();
}

void FileBrowser::renderToolbar(NUIRenderer& renderer) {
    // Get layout dimensions from theme
    auto& themeManager = NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    float headerHeight = themeManager.getComponentDimension("fileBrowser", "headerHeight");

    NUIRect bounds = getBounds();
    // Use effective width to account for preview panel
    float effectiveW = effectiveWidth_ > 0 ? effectiveWidth_ : bounds.width;
    float toolbarHeight = headerHeight; // Use theme headerHeight instead of hardcoded
    NUIRect toolbarRect(bounds.x + layout.panelMargin, bounds.y + layout.panelMargin, effectiveW - 2 * layout.panelMargin, toolbarHeight);

    // Render toolbar background with enhanced styling
    renderer.fillRoundedRect(toolbarRect, 4, backgroundColor_.darkened(0.08f));
    renderer.strokeRoundedRect(toolbarRect, 4, 1, borderColor_.withAlpha(0.3f));

    // Navigation buttons
    float iconY = toolbarRect.y + (toolbarRect.height - 20.0f) * 0.5f;
    float iconX = toolbarRect.x + 4.0f;
    if (backIcon_) {
        backIcon_->setPosition(iconX, iconY);
        backIcon_->setOpacity(navHistoryIndex_ > 0 ? 1.0f : 0.3f);
        backIcon_->onRender(renderer);
    }
    if (forwardIcon_) {
        forwardIcon_->setPosition(iconX + 28.0f, iconY);
        forwardIcon_->setOpacity(navHistoryIndex_ < static_cast<int>(navHistory_.size()) - 1 ? 1.0f : 0.3f);
        forwardIcon_->onRender(renderer);
    }

    float contentStartX = iconX + 60.0f; // Leave space for nav icons

    // Render refresh button with proper vertical alignment and bounds checking
    float toolbarFont = 16.0f;
    float textY = toolbarRect.y + (toolbarRect.height - toolbarFont) * 0.5f;
    
    // Measure refresh text and ensure it fits
    auto refreshTextSize = renderer.measureText("Refresh", toolbarFont);
    float maxRefreshWidth = toolbarRect.width * 0.3f; // Limit to 30% of toolbar width
    
    // Draw button background for Refresh
    NUIRect refreshRect(contentStartX - 10, toolbarRect.y + 4, refreshTextSize.width + 20, toolbarRect.height - 8);
    renderer.fillRoundedRect(refreshRect, 6, NUIColor(1.0f, 1.0f, 1.0f, 0.08f)); // Slightly more visible
    renderer.strokeRoundedRect(refreshRect, 6, 1, borderColor_.withAlpha(0.4f));

    if (refreshTextSize.width > maxRefreshWidth) {
        std::string truncatedRefresh = "Refresh";
        while (!truncatedRefresh.empty() && renderer.measureText(truncatedRefresh, toolbarFont).width > maxRefreshWidth) {
            truncatedRefresh = truncatedRefresh.substr(0, truncatedRefresh.length() - 1);
        }
        renderer.drawText(truncatedRefresh + "...", NUIPoint(contentStartX, textY), toolbarFont, textColor_);
    } else {
        renderer.drawText("Refresh", NUIPoint(contentStartX, textY), toolbarFont, textColor_);
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

    auto sortTextSize = renderer.measureText(sortText, toolbarFont);
    
    // Ensure sort text doesn't exceed toolbar bounds
    float maxSortWidth = toolbarRect.width * 0.4f; // Limit to 40% of toolbar width
    if (sortTextSize.width > maxSortWidth) {
        std::string truncatedSort = sortText;
        while (!truncatedSort.empty() && renderer.measureText(truncatedSort, toolbarFont).width > maxSortWidth) {
            truncatedSort = truncatedSort.substr(0, truncatedSort.length() - 1);
        }
        sortText = truncatedSort + "...";
        sortTextSize = renderer.measureText(sortText, toolbarFont);
    }
    
    // Position sort text to ensure it fits within bounds
    float sortX = std::max(toolbarRect.x + layout.panelMargin, toolbarRect.x + toolbarRect.width - sortTextSize.width - layout.panelMargin - 10);
    
    // Draw button background for Sort
    NUIRect sortRect(sortX - 10, toolbarRect.y + 4, sortTextSize.width + 20, toolbarRect.height - 8);
    renderer.fillRoundedRect(sortRect, 6, NUIColor(1.0f, 1.0f, 1.0f, 0.08f));
    renderer.strokeRoundedRect(sortRect, 6, 1, borderColor_.withAlpha(0.4f));

    renderer.drawText(sortText, NUIPoint(sortX, textY), toolbarFont, textColor_.withAlpha(0.9f));
    
    // Draw separator line below toolbar
    renderer.drawLine(NUIPoint(bounds.x, toolbarRect.y + toolbarRect.height + 4), 
                      NUIPoint(bounds.x + bounds.width, toolbarRect.y + toolbarRect.height + 4), 
                      1.0f, borderColor_.withAlpha(0.5f));
}

void FileBrowser::updateScrollPosition() {
    if (selectedIndex_ < 0) return;

    // Get component dimensions from theme
    auto& themeManager = NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();

    NUIRect bounds = getBounds();
    float headerHeight = themeManager.getComponentDimension("fileBrowser", "headerHeight");
    float itemHeight = themeManager.getComponentDimension("fileBrowser", "itemHeight");
    float listY = bounds.y + headerHeight + 8 + 30; // After path bar
    float listHeight = bounds.height - headerHeight - 8 - 30;

    const auto& view = searchQuery_.empty() ? displayItems_ : filteredFiles_;
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
    float maxScroll = std::max(0.0f, (view.size() * itemHeight_) - listHeight);
    scrollOffset_ = std::max(0.0f, std::min(scrollOffset_, maxScroll));
    targetScrollOffset_ = scrollOffset_;
    scrollVelocity_ = 0.0f;

    // Update scrollbar visibility and position
    updateScrollbarVisibility();
}

void FileBrowser::renderScrollbar(NUIRenderer& renderer) {
    // Get component dimensions from theme
    auto& themeManager = NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    float itemHeight = themeManager.getComponentDimension("fileBrowser", "itemHeight");
    float headerHeight = themeManager.getComponentDimension("fileBrowser", "headerHeight");

    const auto& view = searchQuery_.empty() ? displayItems_ : filteredFiles_;
    // Check if we need a scrollbar
    float contentHeight = view.size() * itemHeight;
    float maxScroll = std::max(0.0f, contentHeight - scrollbarTrackHeight_);
    bool needsScrollbar = maxScroll > 0.0f;

    if (!needsScrollbar || view.empty()) return;

    // Position scrollbar FLUSH with the right edge (no gap)
    // Use effective width to account for preview panel
    NUIRect bounds = getBounds();
    float effectiveW = effectiveWidth_ > 0 ? effectiveWidth_ : bounds.width;
    float scrollbarX = bounds.x + effectiveW - scrollbarWidth_;  // FLUSH alignment - no panelMargin
    float scrollbarY = bounds.y + headerHeight + 8 + 30; // After path bar
    float scrollbarHeight = bounds.height - headerHeight - 8 - 30;

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
    float scrollbarY = bounds.y + headerHeight + 8 + 30; // After path bar
    
    // Use the member variable scrollbarTrackHeight_ for consistency
    // It's set in onResize() and used for thumb calculation

    const auto& view = searchQuery_.empty() ? displayItems_ : filteredFiles_;

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
    const auto& view = searchQuery_.empty() ? displayItems_ : filteredFiles_;

    // Calculate if we need a scrollbar
    float contentHeight = view.size() * itemHeight;
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

// ========================================================================
// Selection / Filtering / Breadcrumb helpers
// ========================================================================

void FileBrowser::toggleFileSelection(int index, bool ctrlPressed, bool shiftPressed) {
    const auto& view = searchQuery_.empty() ? displayItems_ : filteredFiles_;
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
    searchQuery_ = query;
    applyFilter();
}

void FileBrowser::applyFilter() {
    filteredFiles_.clear();
    if (searchQuery_.empty()) {
        updateScrollbarVisibility();
        setDirty(true);
        return;
    }

    std::string needle = searchQuery_;
    std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
    
    // Recursive search helper
    std::function<void(const std::vector<FileItem>&)> searchRecursive = 
        [&](const std::vector<FileItem>& items) {
        for (const auto& item : items) {
            std::string hay = item.name;
            std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
            if (hay.find(needle) != std::string::npos) {
                filteredFiles_.push_back(const_cast<FileItem*>(&item));
            }
            if (item.hasLoadedChildren) {
                searchRecursive(item.children);
            }
        }
    };
    
    searchRecursive(rootItems_);

    clearSelection();
    updateScrollbarVisibility();
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
        breadcrumbs_.push_back({name, accum.string(), x, approxWidth});
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
    auto& themeManager = NUIThemeManager::getInstance();
    float headerHeight = themeManager.getComponentDimension("fileBrowser", "headerHeight");
    float fontSize = themeManager.getFontSize("s");
    NUIRect bounds = getBounds();
    // Use effective width to account for preview panel
    float effectiveW = effectiveWidth_ > 0 ? effectiveWidth_ : bounds.width;

    // Increased height for better touch target and visual balance
    float breadcrumbHeight = 26.0f;
    NUIRect breadcrumbRect(bounds.x + 8, bounds.y + headerHeight + 6, effectiveW - 16, breadcrumbHeight);

    std::filesystem::path p(currentPath_);
    std::vector<std::filesystem::path> parts;
    for (auto it = p.begin(); it != p.end(); ++it) {
        parts.push_back(*it);
    }

    // Calculate total width needed for all breadcrumbs
    float iconSize = 12.0f;
    float chevronWidth = iconSize + 12.0f; // More generous spacing
    float totalWidth = 0.0f;
    std::vector<float> partWidths;
    
    for (size_t i = 0; i < parts.size(); ++i) {
        std::string partName = parts[i].string();
        if (!partName.empty() && partName.back() == std::filesystem::path::preferred_separator) {
            partName.pop_back();
        }
        float textWidth = renderer.measureText(partName, fontSize).width;
        partWidths.push_back(textWidth);
        totalWidth += textWidth + 20.0f; // More padding for chip look
        if (i < parts.size() - 1) {
            totalWidth += chevronWidth;
        }
    }
    
    // Determine if we need to truncate from the left
    float availableWidth = breadcrumbRect.width;
    float ellipsisWidth = renderer.measureText("...", fontSize).width + 20.0f;
    
    // Find starting index - skip parts from the beginning if path is too long
    size_t startIndex = 0;
    if (totalWidth > availableWidth && parts.size() > 1) {
        // Calculate how much we need to skip
        float currentWidth = 0.0f;
        // Start from end and work backwards
        for (int i = static_cast<int>(parts.size()) - 1; i >= 0; --i) {
            float partW = partWidths[i] + 20.0f;
            if (i < static_cast<int>(parts.size()) - 1) partW += chevronWidth;
            
            if (currentWidth + partW + ellipsisWidth > availableWidth) {
                startIndex = i + 1;
                break;
            }
            currentWidth += partW;
        }
    }
    
    float currentX = breadcrumbRect.x;
    breadcrumbs_.clear();
    std::filesystem::path buildPath;
    
    // Build path up to start index (for navigation)
    for (size_t i = 0; i < startIndex; ++i) {
        buildPath /= parts[i];
    }
    
    // Draw ellipsis if truncated
    if (startIndex > 0) {
        NUIRect ellipsisRect(currentX, breadcrumbRect.y, ellipsisWidth, breadcrumbRect.height);
        renderer.drawText("...", NUIPoint(currentX + 10, breadcrumbRect.y + (breadcrumbRect.height - fontSize) * 0.5f), fontSize, textColor_.withAlpha(0.5f));
        currentX += ellipsisWidth;
        
        // Draw separator after ellipsis
        if (chevronIcon_) {
            NUIRect chevronRect(currentX, breadcrumbRect.y + (breadcrumbRect.height - iconSize) * 0.5f, iconSize, iconSize);
            chevronIcon_->setBounds(chevronRect);
            chevronIcon_->setColor(textColor_.withAlpha(0.4f));
            chevronIcon_->onRender(renderer);
        }
        currentX += chevronWidth;
    }
    
    // Draw visible parts
    for (size_t i = startIndex; i < parts.size(); ++i) {
        std::string partName = parts[i].string();
        if (!partName.empty() && partName.back() == std::filesystem::path::preferred_separator) {
            partName.pop_back();
        }
        
        float width = partWidths[i] + 20.0f; // Padding
        NUIRect partRect(currentX, breadcrumbRect.y, width, breadcrumbRect.height);
        
        buildPath /= parts[i];
        
        // Store for hit testing
        Breadcrumb b;
        b.name = partName;
        b.path = buildPath.string();
        b.x = currentX;
        b.width = width;
        breadcrumbs_.push_back(b);
        
        bool isHovered = (static_cast<int>(i) == hoveredBreadcrumbIndex_);
        bool isLast = (i == parts.size() - 1);
        
        // Draw chip background
        if (isHovered) {
            renderer.fillRoundedRect(partRect, 6, hoverColor_);
            renderer.strokeRoundedRect(partRect, 6, 1, hoverColor_.lightened(0.2f));
        } else if (isLast) {
            renderer.fillRoundedRect(partRect, 6, selectedColor_.withAlpha(0.15f));
            renderer.strokeRoundedRect(partRect, 6, 1, selectedColor_.withAlpha(0.3f));
        } else {
            // Subtle background for non-hovered items to show clickability
            renderer.fillRoundedRect(partRect, 6, NUIColor(1.0f, 1.0f, 1.0f, 0.03f));
        }
        
        // Draw text
        NUIColor color = isHovered ? NUIColor::white() : (isLast ? selectedColor_ : textColor_);
        renderer.drawText(partName, NUIPoint(currentX + 10, breadcrumbRect.y + (breadcrumbRect.height - fontSize) * 0.5f), fontSize, color);
        
        currentX += width;
        
        // Draw separator
        if (i < parts.size() - 1) {
            if (chevronIcon_) {
                NUIRect chevronRect(currentX + 6.0f, breadcrumbRect.y + (breadcrumbRect.height - iconSize) * 0.5f, iconSize, iconSize);
                chevronIcon_->setBounds(chevronRect);
                chevronIcon_->setColor(textColor_.withAlpha(0.4f));
                chevronIcon_->onRender(renderer);
            }
            currentX += chevronWidth;
        }
    }
}
bool FileBrowser::handleBreadcrumbMouseEvent(const NUIMouseEvent& event) {
    if (breadcrumbs_.empty()) return false;
    auto& themeManager = NUIThemeManager::getInstance();
    float headerHeight = themeManager.getComponentDimension("fileBrowser", "headerHeight");
    float fontSize = themeManager.getFontSize("s");
    float y = getBounds().y + headerHeight + 4;

    for (size_t i = 0; i < breadcrumbs_.size(); ++i) {
        const auto& crumb = breadcrumbs_[i];
        float w = crumb.width > 0.0f ? crumb.width : static_cast<float>(crumb.name.size()) * 7.0f;
        if (event.position.x >= crumb.x && event.position.x <= crumb.x + w &&
            event.position.y >= y && event.position.y <= y + 20.0f) {
            if (event.pressed && event.button == NUIMouseButton::Left) {
                navigateToBreadcrumb(static_cast<int>(i));
                return true;
            }
        }
    }
    return false;
}

bool FileBrowser::handleSearchBoxMouseEvent(const NUIMouseEvent& event) {
    // Placeholder: no interactive search box yet
    return false;
}

void FileBrowser::setPreviewPanelVisible(bool visible) {
    previewPanelVisible_ = visible;
}

// Placeholder for preview panel (waveform/metadata) to satisfy linkage
void FileBrowser::renderPreviewPanel(NUIRenderer& renderer) {
    (void)renderer;
    // To be implemented in preview phase (waveform + metadata + mini transport)
}

} // namespace NomadUI
