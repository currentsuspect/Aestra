// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "FileBrowser.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Core/NUIDragDrop.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "../NomadCore/include/NomadLog.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <fstream>

using namespace Nomad;

namespace NomadUI {

namespace {

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
    const auto& view = searchQuery_.empty() ? displayItems_ : filteredFiles_;
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
    }
}

void FileBrowser::onResize(int width, int height) {
    NUIComponent::onResize(width, height);

    // Get component dimensions from theme
    auto& themeManager = NUIThemeManager::getInstance();

    // Update visible items count using configurable dimensions
    float headerHeight = themeManager.getComponentDimension("fileBrowser", "headerHeight");
    float itemHeight = themeManager.getComponentDimension("fileBrowser", "itemHeight");
    const float pathBarHeight = 30.0f;
    float listHeight = height - headerHeight - 8.0f - pathBarHeight;
    visibleItems_ = static_cast<int>(listHeight / itemHeight);
    visibleItems_ = std::max(1, visibleItems_);

    // Update scrollbar dimensions using configurable dimensions
    float scrollbarWidth = themeManager.getComponentDimension("fileBrowser", "scrollbarWidth");
    scrollbarTrackHeight_ = listHeight; // Match list height for correct thumb math
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
	    float effectiveW = effectiveWidth_ > 0 ? effectiveWidth_ : bounds.width;
    
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
            const FileItem* dragFile = view[dragSourceIndex_];
            
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
	    const float scrollbarGutter = needsScrollbar ? (scrollbarWidth_ + themeManager.getSpacing("xs")) : 0.0f;
	    const float listX = bounds.x + layout.panelMargin + scrollbarGutter;
	    const float listW = effectiveW - 2 * layout.panelMargin - scrollbarGutter;
    
    // Handle mouse wheel scrolling first (works anywhere in the file browser)
    if (event.wheelDelta != 0) {
        if (needsScrollbar) {
            scrollbarFadeTimer_ = 0.0f;
            scrollbarOpacity_ = 1.0f;
        }
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

    // Search box focus (search-first workflow)
    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (searchBoxBounds_.contains(event.position)) {
            searchBoxFocused_ = true;
            setDirty(true);
            return true;
        }
        if (searchBoxFocused_) {
            searchBoxFocused_ = false;
            setDirty(true);
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

        if (event.pressed && event.button == NUIMouseButton::Left) {
            // Calculate which file was clicked
            float relativeY = event.position.y - listY;
            int itemIndex = static_cast<int>((relativeY + scrollOffset_) / itemHeight);
            
	            if (itemIndex >= 0 && itemIndex < static_cast<int>(view.size())) {
	                const FileItem* clickedFile = view[itemIndex];
	                
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
    
    // Ctrl+F focuses search (producer-fast workflow)
    if ((event.modifiers & NUIModifiers::Ctrl) && event.keyCode == NUIKeyCode::F) {
        searchBoxFocused_ = true;
        setDirty(true);
        return true;
    }

    // When search is focused, consume keystrokes for query editing.
    if (searchBoxFocused_) {
        const bool ctrlDown = (event.modifiers & NUIModifiers::Ctrl);
        const bool altDown = (event.modifiers & NUIModifiers::Alt);

        auto appendChar = [&](char c) {
            if (c == 0) return;
            if (!std::isprint(static_cast<unsigned char>(c))) return;
            searchQuery_.push_back(c);
            applyFilter();
            setDirty(true);
        };

        auto keyCodeToChar = [&](NUIKeyCode code) -> char {
            if (code == NUIKeyCode::Space) return ' ';
            if (code >= NUIKeyCode::A && code <= NUIKeyCode::Z) {
                char base = static_cast<char>('a' + (static_cast<int>(code) - static_cast<int>(NUIKeyCode::A)));
                if (event.modifiers & NUIModifiers::Shift) base = static_cast<char>(std::toupper(static_cast<unsigned char>(base)));
                return base;
            }
            if (code >= NUIKeyCode::Num0 && code <= NUIKeyCode::Num9) {
                return static_cast<char>('0' + (static_cast<int>(code) - static_cast<int>(NUIKeyCode::Num0)));
            }
            return 0;
        };

        switch (event.keyCode) {
            case NUIKeyCode::Escape:
                if (!searchQuery_.empty()) {
                    searchQuery_.clear();
                    applyFilter();
                } else {
                    searchBoxFocused_ = false;
                }
                setDirty(true);
                return true;
            case NUIKeyCode::Enter:
                searchBoxFocused_ = false;
                setDirty(true);
                return true;
            case NUIKeyCode::Backspace:
                if (!searchQuery_.empty()) {
                    searchQuery_.pop_back();
                    applyFilter();
                    setDirty(true);
                }
                return true;
            case NUIKeyCode::Delete:
                if (!searchQuery_.empty()) {
                    searchQuery_.clear();
                    applyFilter();
                    setDirty(true);
                }
                return true;
            default:
                break;
        }

        // Don't interpret modifier shortcuts as text input.
        if (ctrlDown || altDown) {
            return true;
        }

        // Prefer provided character; fall back to keyCode mapping.
        if (event.character != 0) {
            appendChar(event.character);
            return true;
        }
        appendChar(keyCodeToChar(event.keyCode));
        return true;
    }

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
                } catch (const std::exception& e) {
                    Log::warning("[FileBrowser] Failed to check directory status for " + path + ": " + std::string(e.what()));
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
        } catch (const std::exception& e) {
            Log::warning("[FileBrowser] Error iterating directory " + currentPath_ + ": " + std::string(e.what()));
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
            try { 
                isDir = entry.is_directory(); 
            } catch (const std::exception& e) { 
                Log::debug("[FileBrowser] Failed to check directory status for " + path + ": " + std::string(e.what()));
                continue; 
            }
            
            FileType type = FileType::Unknown;
            if (isDir) {
                type = FileType::Folder;
            } else {
                std::string extension = entry.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
                type = getFileTypeFromExtension(extension);
            }
            
            size_t size = 0;
            if (!isDir) {
                try { 
                    size = entry.file_size(); 
                } catch (const std::exception& e) { 
                    Log::debug("[FileBrowser] Could not get file size for " + path + ": " + std::string(e.what()));
                    size = 0;
                }
            }
            
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
        
    } catch (const std::exception& e) {
        Log::warning("[FileBrowser] Failed to load folder contents for " + item->path + ": " + std::string(e.what()));
    }
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

    const auto& view = searchQuery_.empty() ? displayItems_ : filteredFiles_;
    const float contentHeight = static_cast<float>(view.size()) * itemHeight;
    const bool needsScrollbar = contentHeight > listHeight;

    // Reserve a left gutter when scrollbar is needed (scrollbar is drawn on the left)
    const float scrollbarGutter = needsScrollbar ? (scrollbarWidth_ + themeManager.getSpacing("xs")) : 0.0f;
    const float listX = bounds.x + layout.panelMargin + scrollbarGutter;
    const float listW = effectiveW - 2 * layout.panelMargin - scrollbarGutter;
    NUIRect listClip(listX, listY, listW, listHeight);

    // CRITICAL: Check if layout width changed - invalidate ALL caches if so (affects truncation)
    if (std::abs(lastCachedWidth_ - listW) > 0.1f) {
        for (auto* file : displayItems_) {
            file->invalidateCache();
        }
        lastCachedWidth_ = listW;
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

    const float labelFont = 14.0f;   // +3px for better legibility
    const float metaFont = 12.0f;    // +2px for metadata
    const float rowIndentStep = 18.0f;
    const float maxIndent = std::min(72.0f, listW * 0.35f);
    const int maxGuideDepth = static_cast<int>(std::floor(maxIndent / rowIndentStep));

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
            // Hover state: Subtle background
            renderer.fillRoundedRect(itemRect, 4, hoverColor_.withAlpha(0.6f));
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
            float lineX = itemRect.x + layout.panelMargin + rowIndentStep * 0.5f;
            for (int d = 0; d < guideDepth; ++d) {
                // Draw vertical line segment
                renderer.drawLine(NUIPoint(lineX, itemRect.y), 
                                  NUIPoint(lineX, itemRect.y + itemRect.height), 
                                  1.0f, borderColor_.withAlpha(0.08f)); // Very subtle lines
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
            float iconSize = themeManager.getComponentDimension("fileBrowser", "iconSize");
            NUIRect iconRect(contentX, itemY + (itemHeight - iconSize) * 0.5f, iconSize, iconSize);
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
        float textX = contentX;
        float nameTextY = std::round(renderer.calculateTextY(itemRect, labelFont));
        
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
            float sizeTextY = std::round(renderer.calculateTextY(itemRect, metaFont));
            
            // Calculate position from the right
            float rightMargin = 12.0f;
            float sizeX = itemRect.x + itemRect.width - sizeText.width - rightMargin;
            
            // Render size
            renderer.drawText(item->cachedSizeStr, NUIPoint(sizeX, sizeTextY), metaFont, textColor_.withAlpha(0.5f));
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

    const float toolbarRadius = themeManager.getRadius("m");

    // Render toolbar background with enhanced styling
    renderer.fillRoundedRect(toolbarRect, toolbarRadius, backgroundColor_.darkened(0.08f));
    renderer.strokeRoundedRect(toolbarRect, toolbarRadius, 1, borderColor_.withAlpha(0.3f));

    // Three-row header: toolbar row, breadcrumb row, search row.
    const float innerPad = themeManager.getSpacing("xs"); // 4px
    const float rowSpacing = themeManager.getSpacing("xs"); // 4px
    const float toolbarRowHeight = 24.0f;
    const float breadcrumbRowHeight = std::max(0.0f, toolbarRect.height - (innerPad * 2.0f) - rowSpacing - toolbarRowHeight);

    const NUIRect toolbarRow(toolbarRect.x + innerPad, toolbarRect.y + innerPad,
                             toolbarRect.width - innerPad * 2.0f, toolbarRowHeight);
    const NUIRect breadcrumbRow(toolbarRect.x + innerPad, toolbarRow.y + toolbarRowHeight + rowSpacing,
                                toolbarRect.width - innerPad * 2.0f, breadcrumbRowHeight);

    // Toolbar row
    float contentStartX = toolbarRow.x;
    const float toolbarFont = themeManager.getFontSize("s");

    // Left: refresh label (kept minimal for now)
    const auto refreshTextSize = renderer.measureText("Refresh", toolbarFont);

    // Right: sort label
    std::string sortText = "Sort ";
    switch (sortMode_) {
        case SortMode::Name: sortText += "Name"; break;
        case SortMode::Type: sortText += "Type"; break;
        case SortMode::Size: sortText += "Size"; break;
        case SortMode::Modified: sortText += "Modified"; break;
    }

    auto sortTextSize = renderer.measureText(sortText, toolbarFont);
    float textY = std::round(renderer.calculateTextY(toolbarRow, toolbarFont));

    renderer.drawText("Refresh", NUIPoint(contentStartX, textY), toolbarFont, textColor_.withAlpha(0.9f));

	    // Slightly larger right inset keeps the label fully inside the rounded toolbar bounds.
	    const float sortRightPad = innerPad + 14.0f;
	    float sortX = toolbarRow.x + toolbarRow.width - sortTextSize.width - sortRightPad;
	    // Ensure there is always some separation from the left cluster.
	    sortX = std::max(sortX, contentStartX + refreshTextSize.width + 30.0f);
	    renderer.drawText(sortText, NUIPoint(sortX, textY), toolbarFont, textColor_.withAlpha(0.75f));

    // Breadcrumb row (dedicated row to prevent collisions with sort label)
    if (breadcrumbRow.height > 0.0f) {
        breadcrumbBounds_ = breadcrumbRow;
        renderInteractiveBreadcrumbs(renderer);
    } else {
        breadcrumbBounds_ = NUIRect(0, 0, 0, 0);
    }

    // Search row (dedicated, full-width) lives in the reserved "path bar" strip below the header.
    const float pathBarHeight = 30.0f;
    const NUIRect searchRow(toolbarRect.x + innerPad,
                            toolbarRect.y + toolbarRect.height + rowSpacing,
                            toolbarRect.width - innerPad * 2.0f,
                            std::max(0.0f, pathBarHeight - rowSpacing));

    if (searchRow.height > 0.0f) {
        searchBoxBounds_ = searchRow;

        const float searchRadius = themeManager.getRadius("s");
        const NUIColor searchBg = themeManager.getColor("inputBgDefault");
        const NUIColor searchBorder = searchBoxFocused_
            ? themeManager.getColor("primary").withAlpha(0.9f)
            : borderColor_.withAlpha(0.5f);

        renderer.fillRoundedRect(searchRow, searchRadius, searchBg);
        renderer.strokeRoundedRect(searchRow, searchRadius, 1, searchBorder);

        const float searchIconSize = 16.0f;
        const NUIRect searchIconRect(searchRow.x + 6.0f,
                                     searchRow.y + (searchRow.height - searchIconSize) * 0.5f,
                                     searchIconSize, searchIconSize);
        if (searchIcon_) {
            searchIcon_->setBounds(searchIconRect);
            searchIcon_->setColor(themeManager.getColor("textSecondary"));
            searchIcon_->onRender(renderer);
        }

        const float searchFont = themeManager.getFontSize("m");
        float searchTextX = searchIconRect.x + searchIconRect.width + 6.0f;
        float maxTextWidth = searchRow.x + searchRow.width - searchTextX - 6.0f;

        std::string displayText = searchQuery_.empty() ? "Search..." : searchQuery_;
        NUIColor displayColor = searchQuery_.empty()
            ? themeManager.getColor("textSecondary").withAlpha(0.6f)
            : textColor_.withAlpha(0.95f);

        if (maxTextWidth > 0.0f) {
            auto displaySize = renderer.measureText(displayText, searchFont);
            if (displaySize.width > maxTextWidth) {
                std::string truncated = displayText;
                while (truncated.length() > 3) {
                    truncated.pop_back();
                    if (renderer.measureText(truncated + "...", searchFont).width <= maxTextWidth) {
                        displayText = truncated + "...";
                        break;
                    }
                }
            }
        }

        const float searchTextY = std::round(renderer.calculateTextY(searchRow, searchFont));
        renderer.drawText(displayText, NUIPoint(searchTextX, searchTextY), searchFont, displayColor);
    } else {
        searchBoxBounds_ = NUIRect(0, 0, 0, 0);
    }
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

    NUIRect bounds = getBounds();
    // Scrollbar is on the left, inside the panel margin.
    float scrollbarX = bounds.x + layout.panelMargin;
    float scrollbarY = bounds.y + headerHeight + 8 + 30; // After path bar
    float scrollbarHeight = bounds.height - headerHeight - 8 - 30;

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
    float scrollbarY = bounds.y + headerHeight + 8 + 30; // After path bar
    
    // Use the member variable scrollbarTrackHeight_ for consistency
    // It's set in onResize() and used for thumb calculation

    const auto& view = searchQuery_.empty() ? displayItems_ : filteredFiles_;

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
    const auto& view = searchQuery_.empty() ? displayItems_ : filteredFiles_;

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
                filteredFiles_.push_back(&item);
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
    for (auto it = p.begin(); it != p.end(); ++it) {
        parts.push_back(*it);
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
    partWidths.reserve(parts.size());
    float totalWidth = 0.0f;

    for (size_t i = 0; i < parts.size(); ++i) {
        std::string partName = parts[i].string();
        if (!partName.empty() && partName.back() == std::filesystem::path::preferred_separator) {
            partName.pop_back();
        }
        const float textW = renderer.measureText(partName, fontSize).width;
        partWidths.push_back(textW);
        totalWidth += textW + chipPadX * 2.0f;
        if (i < parts.size() - 1) {
            totalWidth += separatorW;
        }
    }

    const float availableWidth = breadcrumbRect.width;
    const auto ellipsisSize = renderer.measureText("...", fontSize);
    const float ellipsisW = ellipsisSize.width + chipPadX * 2.0f;

    // Find start index (truncate from the left)
    size_t startIndex = 0;
    if (totalWidth > availableWidth && parts.size() > 1) {
        float currentWidth = 0.0f;
        for (int i = static_cast<int>(parts.size()) - 1; i >= 0; --i) {
            float partW = partWidths[static_cast<size_t>(i)] + chipPadX * 2.0f;
            if (i < static_cast<int>(parts.size()) - 1) {
                partW += separatorW;
            }
            if (currentWidth + partW + ellipsisW > availableWidth) {
                startIndex = static_cast<size_t>(i + 1);
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
        renderer.drawText("...", NUIPoint(currentX + chipPadX, breadcrumbTextY), fontSize, textColor_.withAlpha(0.55f));
        currentX += ellipsisW;

        renderer.drawText(separatorText, NUIPoint(currentX + separatorPad * 0.5f, breadcrumbTextY), fontSize, textColor_.withAlpha(0.45f));
        currentX += separatorW;
    }

    // Draw visible parts
    for (size_t partIndex = startIndex; partIndex < parts.size(); ++partIndex) {
        std::string partName = parts[partIndex].string();
        if (!partName.empty() && partName.back() == std::filesystem::path::preferred_separator) {
            partName.pop_back();
        }

        const float chipW = partWidths[partIndex] + chipPadX * 2.0f;
        const NUIRect partRect(currentX, chipRowRect.y, chipW, chipRowRect.height);

        buildPath /= parts[partIndex];

        Breadcrumb b;
        b.name = partName;
        b.path = buildPath.string();
        b.x = currentX;
        b.width = chipW;
        breadcrumbs_.push_back(b);

        const int viewIndex = static_cast<int>(breadcrumbs_.size()) - 1;
        const bool isHovered = (viewIndex == hoveredBreadcrumbIndex_);
        const bool isLast = (partIndex == parts.size() - 1);

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

    for (size_t i = 0; i < breadcrumbs_.size(); ++i) {
        const auto& crumb = breadcrumbs_[i];
        const float w = crumb.width > 0.0f ? crumb.width : static_cast<float>(crumb.name.size()) * 7.0f;
        if (event.position.x >= crumb.x && event.position.x <= crumb.x + w &&
            event.position.y >= y && event.position.y <= y + h) {
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

void FileBrowser::renderSearchBox(NUIRenderer& renderer) {
    (void)renderer;
    // Placeholder implementation - search box rendering to be implemented
    // Currently handled within renderToolbar() for integrated approach
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
    std::vector<std::string> expandedFolders;
    
    while (std::getline(file, line)) {
        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;
        
        std::string key = line.substr(0, eqPos);
        std::string value = line.substr(eqPos + 1);
        
        if (key == "currentPath" && !value.empty()) {
            setCurrentPath(value);
        }
        else if (key == "scrollOffset") {
            try {
                scrollOffset_ = std::stof(value);
                targetScrollOffset_ = scrollOffset_;
            } catch (...) {}
        }
        else if (key == "sortMode") {
            try {
                sortMode_ = static_cast<SortMode>(std::stoi(value));
            } catch (...) {}
        }
        else if (key == "sortAscending") {
            sortAscending_ = (value == "1");
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
            favoritesPaths_.clear();
            size_t start = 0;
            size_t end;
            while ((end = value.find('|', start)) != std::string::npos) {
                favoritesPaths_.push_back(value.substr(start, end - start));
                start = end + 1;
            }
            if (start < value.length()) {
                favoritesPaths_.push_back(value.substr(start));
            }
        }
    }
    
    file.close();
    
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
    Nomad::Log::info("[FileBrowser] State loaded from: " + filePath);
}

} // namespace NomadUI
