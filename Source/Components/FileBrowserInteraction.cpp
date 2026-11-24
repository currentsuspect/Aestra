// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// FileBrowser, split by concern. This file: mouse and keyboard interaction,
// including list navigation, drag initiation/drop, and pointer handling.
// Split out of FileBrowser.cpp so no one file has to hold the whole browser;
// see Tests/Guards/file_size_ratchet.cmake.
#include "FileBrowserInternal.h"

using namespace Aestra;

namespace AestraUI {

using namespace FileBrowserInternal;

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

    if (handleChromeHits(event) || handleChromeMouse(event, browserLayout)) {
        return true;
    }
    // A press outside Places (but inside the browser) closes it, like a menu.
    if (placesOpen_ && event.pressed && !browserLayout.navPane.contains(event.position) &&
        bounds.contains(event.position)) {
        setPlacesOpen(false);
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
    if (placesOpen_ && browserLayout.navPane.contains(event.position)) {
        return true; // Places covers the rows: nothing under it takes the event
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

	            if (!dragFile->isDirectory && dragFile->type != FileType::ProjectFile && FileFilter::isAllowed(dragFile->path)) { // a project is opened, not dropped
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
    if (placesOpen_ && event.wheelDelta != 0 && browserLayout.navPane.contains(event.position)) {
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
            if (!(placesOpen_ && browserLayout.navPane.contains(event.position))) {
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
                                     hoveredItem->type != FileType::ProjectFile && FileFilter::isAllowed(hoveredItem->path);
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
                const double currentTime = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
                // A double-click is two presses in one spot, whatever row is under the second: the first click can show
                // or hide the preview dock, and the rows shift under a still pointer (owner, 4 Nov 2026).
                const bool isDoubleClick = (currentTime - lastClickTime_) < DOUBLE_CLICK_TIME &&
                    std::abs(event.position.x - lastClickPos_.x) <= 6.0f && std::abs(event.position.y - lastClickPos_.y) <= 6.0f &&
                    lastClickedIndex_ >= 0 && lastClickedIndex_ < static_cast<int>(view.size()) && view[lastClickedIndex_] && view[lastClickedIndex_]->path == lastClickPath_;
                if (isDoubleClick) itemIndex = lastClickedIndex_;

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
		                if (!clickedFile->isDirectory && clickedFile->type != FileType::ProjectFile && FileFilter::isAllowed(clickedFile->path)) {
		                    dragPotential_ = true;
	                    dragSourceIndex_ = itemIndex;
	                    dragStartPos_ = event.position;
	                }

                lastClickedIndex_ = itemIndex;
                lastClickTime_ = isDoubleClick ? 0.0 : currentTime; // a third press starts a new double-click
                lastClickPos_ = event.position; lastClickPath_ = clickedFile->path; // the row index alone can name another item later

                // Update selection with multi-select support
                bool ctrl = event.modifiers & NUIModifiers::Ctrl;
                bool shift = event.modifiers & NUIModifiers::Shift;
                toggleFileSelection(itemIndex, ctrl, shift);
	                // Update selectedFile_ from active view
	                const auto& activeView = getActiveView();
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
                    }
                    // A project opens on double-click, as it does on Enter.
                    if (selectedFile_->type == FileType::ProjectFile && onFileOpened_) {
                        onFileOpened_(*selectedFile_);
                        invalidateCache();
                        return true;
                    }
                    // Double-click on a sound now only previews; loading is Enter/drag-drop
                    if (onSoundPreview_ && !selectedFile_->isDirectory) {
                        FileType type = selectedFile_->type;
                        if (type == FileType::AudioFile || type == FileType::MusicFile ||
                            type == FileType::WavFile || type == FileType::Mp3File ||
                            type == FileType::FlacFile) {
                            onSoundPreview_(*selectedFile_); // PreviewEngine decodes asynchronously
                        }
                    }
                } else {
                    // Single click: trigger sound preview for audio files
                    if (onSoundPreview_ && selectedFile_ && !selectedFile_->isDirectory) {
                        FileType type = selectedFile_->type;
                        if (type == FileType::AudioFile || type == FileType::MusicFile ||
                            type == FileType::WavFile || type == FileType::Mp3File ||
                            type == FileType::FlacFile) {
                            onSoundPreview_(*selectedFile_); // PreviewEngine decodes asynchronously
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
        if (placesOpen_ && event.keyCode == NUIKeyCode::Escape) { setPlacesOpen(false); return true; }
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


} // namespace AestraUI
