// © 2025 Aestra Studios All Rights Reserved. Licensed for personal & educational use only.
// TrackManagerUI — mouse and keyboard event handling.
// Split out of the former monolithic TrackManagerUI.cpp — bodies moved verbatim.
#include "TrackManagerUI.h"

#include "../AestraCore/include/AestraLog.h"
#include "../AestraCore/include/AestraUnifiedProfiler.h"
#include "../AestraUI/Core/NUIDragDrop.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../AestraUI/Platform/NUIPlatformBridge.h"
#include "AudioFileValidator.h"
#include "ClipSource.h"
#include "Commands/AddChannelCommand.h"
#include "Commands/AddClipCommand.h"
#include "Commands/CommandTransaction.h"
#include "Commands/CreateLaneCommand.h"
#include "Commands/DuplicateClipCommand.h"
#include "Commands/MoveClipCommand.h"
#include "Commands/RemoveClipCommand.h"
#include "Commands/SplitClipCommand.h"
#include "MiniAudioDecoder.h"
#include "MixerChannel.h"
#include "PluginManager.h"
#include "TrackManager.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

#include "TrackManagerUIInternal.h"

namespace Aestra {
namespace Audio {

// =============================================================================
// SECTION: Event Handling
// =============================================================================

bool TrackManagerUI::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    // Track mouse position for tool cursors (Split/Paint)
    m_lastMousePos = event.position;

    // Handle hovering state updates for toolbar icons
    AestraUI::NUIRect bounds = getBounds();
    AestraUI::NUIPoint localPos(event.position.x - bounds.x, event.position.y - bounds.y);

    // Fix for "Sticky Drag": Route events to any track that is currently dragging automation
    // regardless of whether the mouse is inside its bounds.
    for (auto& track : m_trackUIComponents) {
        if (!track || !track->isVisible())
            continue;

        // If track is in the middle of an automation drag operation, force-feed it the event
        if (track->isDraggingAutomation()) {
            // Pass event with global coordinates since TrackUIComponent expects global coords
            if (track->onMouseEvent(event))
                return true;
        }
    }

    // Claim keyboard focus on click so keyboard routing moves off the file browser.
    if (event.pressed &&
        (event.button == AestraUI::NUIMouseButton::Left || event.button == AestraUI::NUIMouseButton::Right) &&
        bounds.contains(event.position)) {
        setFocused(true);
    }

    // Update toolbar bounds before checking hover (critical!)
    updateToolbarBounds();

    updateToolbarHover(event);

    if (m_addTrackBtn && m_addTrackBtn->onMouseEvent(event)) {
        return true;
    }

    if (handleContextMenuMouse(event)) {
        return true;
    }

    // === DROPDOWNS removed (now via Context Menu) ===

    // Handle toolbar clicks (icons only, not dropdowns - they handled themselves above)
    if (event.pressed && event.button == AestraUI::NUIMouseButton::Left) {
        // Loop markers outrank toolbar icons (their grab zones can overlap a
        // button when a handle is scrolled over the corner column).
        if (tryBeginLoopHandleDrag(event, localPos)) {
            return true;
        }
        if (handleToolbarClick(event.position)) {
            return true;
        }
    }

    // In v3.1, overlays are handled by OverlayLayer::onMouseEvent.
    // TrackManagerUI only handles clicks that reach the workspace.

    // Active marquee owns every mouse event until release (#847), and routes
    // BEFORE sibling controls: the minimap consumes in-bounds releases even
    // when idle, so a release over it would strand the gesture.
    if (m_marquee.active()) {
        return handleSelectionBoxMouse(event, localPos);
    }

    // Give the vertical scrollbar priority so it stays usable even with complex track interactions.
    if (m_playlistVisible && m_scrollbar && m_scrollbar->isVisible()) {
        if (m_scrollbar->onMouseEvent(event)) {
            return true;
        }
    }

    // Give horizontal scrollbar (minimap) priority too
    if (m_timelineMinimap && m_timelineMinimap->isVisible()) {
        if (m_timelineMinimap->onMouseEvent(event)) {
            return true;
        }
    }

    // If playlist is hidden, still allow toolbar toggles and panel interaction.
    // The playlist content itself should not consume events in this mode.
    if (!m_playlistVisible) {
        return AestraUI::NUIComponent::onMouseEvent(event);
    }

    // Handle instant clip dragging
    if (m_isDraggingClipInstant) {
        if (event.released && event.button == AestraUI::NUIMouseButton::Left) {
            finishInstantClipDrag();
            return true;
        }
        updateInstantClipDrag(event.position);
        return true;
    }

    // In the explicit Multi-Select tool, either mouse button owns the marquee.
    // In every other tool, right-click remains a context-menu gesture.
    if (event.pressed && event.button == AestraUI::NUIMouseButton::Right &&
        m_currentTool != PlaylistTool::MultiSelect) {
        if (AestraUI::NUIComponent::onMouseEvent(event)) {
            return true;
        }
    }

    if (handleSelectionBoxMouse(event, localPos)) {
        return true;
    }

    // Layout constants — the time band is minimap row + ruler row.
    float rulerHeight = kTimelineRulerHeight;
    float minimapHeight = kTimelineMinimapHeight;
    AestraUI::NUIRect rulerRect(0, minimapHeight, bounds.width, rulerHeight);

    // Track area (below ruler)
    float trackAreaTop = kTimelineTimeBandHeight;
    AestraUI::NUIRect trackArea(0, trackAreaTop, bounds.width, bounds.height - trackAreaTop);

    // The toolbar dock occupies the ruler row's left corner; presses there are
    // button hits, never ruler scrub/loop gestures. The corner cell's empty
    // remainder is dead space too: ruler gestures start at the plane edge, so
    // a click between the last button and the grid cannot scrub the playhead.
    const float rulerContentStartX = timelineGridStartX(
        0.0f, AestraUI::NUIThemeManager::getInstance().getLayoutDimensions().trackControlsWidth);
    const bool isInRuler = rulerRect.contains(localPos) && localPos.x >= rulerContentStartX &&
                           !m_toolbarCornerBounds.contains(event.position);
    bool isInTrackArea = trackArea.contains(localPos);

    if (handleTimelineWheel(event, localPos, isInRuler, isInTrackArea)) {
        return true;
    }
    if (handleRulerPress(event, localPos, isInRuler)) {
        return true;
    }
    if (handleRulerSelectionDrag(event, localPos)) {
        return true;
    }
    if (handleRulerSelectionMenu(event, localPos, isInRuler)) {
        return true;
    }
    if (handleLoopMarkerDrag(event, localPos)) {
        return true;
    }
    if (handlePlayheadDrag(event, localPos)) {
        return true;
    }

    // (Vertical scroll handling moved to main wheel handler above)

    // First, let children handle the event
    bool handled = AestraUI::NUIComponent::onMouseEvent(event);
    if (handled)
        return true;

    if (handleSplitToolClick(event, localPos)) {
        return true;
    }

    // Close the inter-row seam: a left press between two rows reaches here
    // unhandled (it is inside no track's bounds). Split has its own gap-free
    // mapping above, so this only needs to cover ordinary selection.
    if (handleTrackSeamSelect(event, localPos)) {
        return true;
    }

    return handled;
}

bool TrackManagerUI::handleTrackSeamSelect(const AestraUI::NUIMouseEvent& event,
                                           const AestraUI::NUIPoint& localPos) {
    if (!m_playlistVisible || !event.pressed || event.button != AestraUI::NUIMouseButton::Left) {
        return false;
    }
    // Split owns seam clicks in split mode (handleSplitToolClick already ran).
    if (m_currentTool == PlaylistTool::Split) {
        return false;
    }

    const float trackAreaTop = kTimelineTimeBandHeight;
    if (localPos.y < trackAreaTop) {
        return false; // ruler / minimap band
    }

    const float stride = static_cast<float>(m_trackHeight + m_trackSpacing);
    if (stride <= 0.0f) {
        return false;
    }
    const float relativeY = localPos.y - trackAreaTop + m_scrollOffset;
    if (relativeY < 0.0f) {
        return false;
    }
    const int idx = static_cast<int>(relativeY / stride);
    if (idx < 0 || idx >= static_cast<int>(m_trackUIComponents.size())) {
        return false;
    }

    auto& trackUI = m_trackUIComponents[idx];
    if (!trackUI || !trackUI->isVisible()) {
        return false;
    }
    // Only genuine seam clicks reach here; inside the row the child consumes it.
    const AestraUI::NUIRect tb = trackUI->getBounds();
    if (tb.contains(event.position)) {
        return false;
    }
    // Stay within the row's horizontal extent (exclude the scrollbar gutter).
    if (event.position.x < tb.x || event.position.x > tb.x + tb.width) {
        return false;
    }

    const bool toggleModifier =
        (event.modifiers & AestraUI::NUIModifiers::Ctrl) || (event.modifiers & AestraUI::NUIModifiers::Super);
    const TrackSelectionIntent intent =
        trackSelectionIntentForModifierState(toggleModifier, event.modifiers & AestraUI::NUIModifiers::Shift);
    selectTrack(trackUI.get(), intent);
    invalidateCache();
    return true;
}

void TrackManagerUI::updateToolbarHover(const AestraUI::NUIMouseEvent& event) {
    if (!event.cursorCaptured) {
        // Update toolbar hover states
        bool oldMenuHovered = m_menuHovered;
        bool oldSelectHovered = m_selectToolHovered;
        bool oldSplitHovered = m_splitToolHovered;
        bool oldMultiSelectHovered = m_multiSelectToolHovered;
        bool oldPaintHovered = m_paintToolHovered;
        bool oldFollowHovered = m_followPlayheadHovered;

        m_menuHovered = m_menuIconBounds.contains(event.position);
        m_selectToolHovered = m_selectToolBounds.contains(event.position);
        m_splitToolHovered = m_splitToolBounds.contains(event.position);
        m_multiSelectToolHovered = m_multiSelectToolBounds.contains(event.position);
        m_paintToolHovered = m_paintToolBounds.contains(event.position);
        m_followPlayheadHovered = m_followPlayheadBounds.contains(event.position);

        // Toolbar Tooltips
        bool anyToolbarHovered = m_menuHovered || m_selectToolHovered || m_splitToolHovered ||
                                 m_multiSelectToolHovered || m_paintToolHovered || m_followPlayheadHovered;
        bool anyOldHovered = oldMenuHovered || oldSelectHovered || oldSplitHovered ||
                             oldMultiSelectHovered || oldPaintHovered || oldFollowHovered;

        if (m_toolbarBounds.contains(event.position) && anyToolbarHovered) {
            std::string tooltipText;
            if (m_menuHovered && !oldMenuHovered)
                tooltipText = "Menu";
            else if (m_selectToolHovered && !oldSelectHovered)
                tooltipText = "Select Tool";
            else if (m_splitToolHovered && !oldSplitHovered)
                tooltipText = "Split Tool";
            else if (m_multiSelectToolHovered && !oldMultiSelectHovered)
                tooltipText = "Multi-Select Tool";
            else if (m_paintToolHovered && !oldPaintHovered)
                tooltipText = "Paint Tool";
            else if (m_followPlayheadHovered && !oldFollowHovered)
                tooltipText = "Follow Playhead";
            if (!tooltipText.empty()) {
                AestraUI::NUIComponent::showRemoteTooltip(tooltipText, event.position, this);
            }
        } else if (!anyToolbarHovered && anyOldHovered) {
            AestraUI::NUIComponent::hideRemoteTooltip(this);
        }

        // Toolbar is rendered outside the playlist cache; don't invalidate the cache on hover.
        if (m_menuHovered != oldMenuHovered ||
            m_selectToolHovered != oldSelectHovered || m_splitToolHovered != oldSplitHovered ||
            m_multiSelectToolHovered != oldMultiSelectHovered || m_paintToolHovered != oldPaintHovered ||
            m_followPlayheadHovered != oldFollowHovered) {
            setDirty(true);
        }
    }
}

bool TrackManagerUI::handleContextMenuMouse(const AestraUI::NUIMouseEvent& event) {
    // === CONTEXT MENU Handling ===
    // Special handling for Right-Click on Follow Button
    if (event.pressed && event.button == AestraUI::NUIMouseButton::Right &&
        m_followPlayheadBounds.contains(event.position)) {
        if (m_activeContextMenu) {
            detachContextMenu(m_activeContextMenu);
            m_activeContextMenu = nullptr;
        }

        m_activeContextMenu = std::make_shared<AestraUI::NUIContextMenu>();
        auto menu = m_activeContextMenu;

        // Add Modes
        menu->addRadioItem("Page", "FollowMode", m_followMode == FollowMode::Page, [this]() {
            setFollowMode(FollowMode::Page);
            setFollowPlayhead(true); // Auto-enable on selection
        });

        menu->addRadioItem("Continuous", "FollowMode", m_followMode == FollowMode::Continuous, [this]() {
            setFollowMode(FollowMode::Continuous);
            setFollowPlayhead(true);
        });

        attachAndShowContextMenu(
            this, menu,
            AestraUI::NUIPoint(m_followPlayheadBounds.x, m_followPlayheadBounds.y + m_followPlayheadBounds.height));
        return true;
    }

    // If context menu is active, give it priority.
    if (m_activeContextMenu) {
        // Forward event to menu (handles interactions in menu AND submenus)
        // onMouseEvent returns true if the event was handled (clicked inside menu/submenu)
        bool handled = m_activeContextMenu->onMouseEvent(event);

        // If click was NOT handled by the menu (i.e. clicked outside), close it.
        if (!handled && event.pressed) {
            detachContextMenu(m_activeContextMenu);
            m_activeContextMenu = nullptr;

            // Falling through lets the click also act on whatever is underneath
            // (Stop button, track header, ...), which is what we want everywhere
            // EXCEPT on the menu button itself: the control underneath there is the
            // one that opens this menu, so handleToolbarClick would see the pointer
            // we just cleared and immediately build a new menu. That is why the
            // button could only ever open — the dismissal was real, it was just
            // undone one handler later, in the same event.
            //
            // updateToolbarBounds() runs before this handler, so m_menuIconBounds is
            // current.
            if (m_menuIconBounds.contains(event.position)) {
                setDirty(true);
                return true;
            }
            // Let execution continue so the click can interact with whatever is underneath
            // (e.g. Stop button, Track header, etc.)
        } else if (handled) {
            // Menu handled the event, consume it.
            return true;
        }
    }
    return false;
}

bool TrackManagerUI::handleSelectionBoxMouse(const AestraUI::NUIMouseEvent& event, const AestraUI::NUIPoint& localPos) {
    // Marquee selection is owned by the visible Multi-Select tool. Supporting
    // both buttons preserves the timeline's established right-drag gesture;
    // outside this tool, right-click remains reserved for context menus.
    const bool selectionButton =
        event.button == AestraUI::NUIMouseButton::Left || event.button == AestraUI::NUIMouseButton::Right;
    const auto marqueeButton = static_cast<Aestra::Components::MarqueeButton>(event.button);

    // An active marquee owns every mouse event until the button that began it
    // releases (#847): moves, foreign buttons, and hover state cannot escape
    // or end someone else's drag. The machine filters endpoint changes to
    // pointer moves and the initiating button's release.
    if (m_marquee.ownsEvent()) {
        float targetX = event.position.x;
        float targetY = event.position.y;
        if (m_window) {
            // Calculate constrained cursor position
            auto& themeManager = AestraUI::NUIThemeManager::getInstance();
            const auto& layout = themeManager.getLayoutDimensions();

            float controlAreaWidth = layout.trackControlsWidth;
            float scrollbarWidth = kTimelineScrollbarWidth;

            AestraUI::NUIRect globalBounds = getBounds();

            float gridTopLocal = globalBounds.y + kTimelineTimeBandHeight;
            float gridLeftLocal = globalBounds.x + controlAreaWidth + kTimelineGridInsetX;
            float gridRightLocal = globalBounds.x + globalBounds.width - scrollbarWidth; // Corrected width calc
            float gridBottomLocal = globalBounds.y + globalBounds.height;                // Full height down

            // Clamp event position (window-local) to grid area. The selection
            // logic uses the clamped point; the physical cursor is left alone —
            // warping it every move made the pointer fight the synthetic
            // motion events it generated (felt like an app hang, #847).
            targetX = safeClampFloat(event.position.x, gridLeftLocal, gridRightLocal);
            targetY = safeClampFloat(event.position.y, gridTopLocal, gridBottomLocal);
        }

        // The machine consumes every owned event; endpoint changes only on
        // moves and the initiating button's release. True = finalize now.
        if (m_marquee.onEvent(event.pressed, event.released, marqueeButton, targetX, targetY)) {
            const AestraUI::NUIRect selectionRect(m_marquee.rectMinX(), m_marquee.rectMinY(), m_marquee.rectWidth(),
                                                  m_marquee.rectHeight());

            // Clip-level box selection (#848, "the future is now"): intersect
            // the band with every visible clip; fall back to track rows when
            // no clip was boxed.
            std::vector<ClipInstanceID> boxed;
            for (auto& trackUI : m_trackUIComponents) {
                if (!trackUI)
                    continue;
                for (const auto& [clipId, clipRect] : trackUI->getAllClipBounds()) {
                    if (selectionRect.intersects(clipRect)) {
                        boxed.push_back(clipId);
                    }
                }
            }

            // One modifier contract shared with clip/track/seam selection:
            // toggle wins over shift, and the platform toggle key applies.
            const bool toggleModifier = (event.modifiers & AestraUI::NUIModifiers::Ctrl) ||
                                        (event.modifiers & AestraUI::NUIModifiers::Super);
            const TrackSelectionIntent intent = trackSelectionIntentForModifierState(
                toggleModifier, event.modifiers & AestraUI::NUIModifiers::Shift);

            if (intent == TrackSelectionIntent::Replace) {
                clearSelection();
                clearClipSelection();
            }

            if (!boxed.empty()) {
                selectClips(boxed, intent);
            } else {
                for (auto& trackUI : m_trackUIComponents) {
                    if (trackUI->getBounds().intersects(selectionRect)) {
                        selectTrack(trackUI.get(), intent != TrackSelectionIntent::Toggle);
                    }
                }
            }

            // Lane selection follows the FULL resulting clip selection (#853
            // round 1): retained clips from a modifier marquee keep their
            // owning lanes highlighted too.
            if (m_trackManager && !m_clipSelection.empty()) {
                auto& playlist = m_trackManager->getPlaylistModel();
                if (intent == TrackSelectionIntent::Replace) {
                    m_trackSelection.clear();
                }
                m_clipSelection.forEachClip([&](const ClipInstanceID& clipId) {
                    const PlaylistLaneID laneId = playlist.findClipLane(clipId);
                    if (laneId.isValid() && !m_trackSelection.contains(laneId)) {
                        m_trackSelection.apply(laneId,
                                               intent == TrackSelectionIntent::Replace
                                                   ? TrackSelectionIntent::Add
                                                   : intent);
                    }
                });
                syncTrackSelectionView();
            }

            Log::info("Selection box completed: " +
                      std::string(m_clipSelection.empty()
                                      ? "0 clips"
                                      : std::to_string(m_clipSelection.size()) + " clips"));

            // Note: System cursor is always hidden by Main.cpp custom cursor system

            m_marquee.finalize();
            invalidateCache();
        }

        // The rubber band draws outside the playlist FBO cache; rebuilding the
        // whole timeline every move made multi-select drag a per-move full
        // re-render (#847). Finalize invalidates once above.
        return true;
    }

    // A marquee-button press in the track area starts a new drag.
    if (event.pressed && selectionButton && m_currentTool == PlaylistTool::MultiSelect) {
        const float trackAreaTop = kTimelineTimeBandHeight;
        if (m_marquee.begin(true, marqueeButton, event.position.x, event.position.y,
                            localPos.y > trackAreaTop)) {
            // Note: System cursor is always hidden by Main.cpp custom cursor system
            return true;
        }
    }
    return false;
}

bool TrackManagerUI::handleTimelineWheel(const AestraUI::NUIMouseEvent& event, const AestraUI::NUIPoint& localPos, bool isInRuler, bool isInTrackArea) {
    const AestraUI::NUIRect bounds = getBounds();
    // Mouse wheel handling
    if (event.wheelDelta != 0.0f && (isInRuler || isInTrackArea)) {
        const bool shiftHeld = (event.modifiers & AestraUI::NUIModifiers::Shift);
        const bool ctrlHeld = (event.modifiers & AestraUI::NUIModifiers::Ctrl);

        if (isInRuler || ctrlHeld) {
            // ZOOM: ruler wheel or Ctrl+wheel.
            m_lastMouseZoomX = localPos.x;

            // Calculate mouse position in "content space" BEFORE zoom
            auto& themeManager = AestraUI::NUIThemeManager::getInstance();
            float controlAreaWidth = themeManager.getLayoutDimensions().trackControlsWidth;
            float gridStartX = controlAreaWidth + kTimelineGridInsetX;
            float gridWidthPx = getTimelineGridWidthPixels();

            float mouseRelX = localPos.x - gridStartX;

            // Current beat at mouse position
            double mouseBeat = gridOffsetToBeat(mouseRelX);

            // Calculate min pixels/beat based on domain (can't zoom out beyond domain)
            const double domainWidth = std::max(1.0, m_minimapDomainEndBeat - m_minimapDomainStartBeat);
            float minPPB = std::max(1.0f, static_cast<float>(gridWidthPx / domainWidth));

            // Exponential zoom
            float zoomMultiplier = event.wheelDelta > 0 ? 1.15f : 0.87f;
            float newPixelsPerBeat =
                safeClampFloat(m_targetPixelsPerBeat * zoomMultiplier, minPPB, kMaxTimelinePixelsPerBeat);

            m_targetPixelsPerBeat = newPixelsPerBeat;
            // Update immediate for snappiness (smooth zoom interpolation can be added later if needed)
            m_pixelsPerBeat = newPixelsPerBeat;

            // Recalculate scroll offset to keep mouseBeat at the same screen position
            // (mouseBeat * newPixelsPerBeat) = newOffset + mouseRelX
            // newOffset = (mouseBeat * newPixelsPerBeat) - mouseRelX
            float newScrollOffset = static_cast<float>(mouseBeat * m_pixelsPerBeat) - mouseRelX;

            // Clamp scroll to domain bounds
            double maxStartBeat = std::max(0.0, m_minimapDomainEndBeat - (gridWidthPx / m_pixelsPerBeat));
            newScrollOffset = safeClampFloat(newScrollOffset, 0.0f, static_cast<float>(maxStartBeat * m_pixelsPerBeat));
            m_timelineScrollOffset = newScrollOffset;

            for (auto& trackUI : m_trackUIComponents) {
                trackUI->setBeatsPerBar(m_beatsPerBar);
                trackUI->setPixelsPerBeat(m_pixelsPerBeat);
                trackUI->setTimelineScrollOffset(m_timelineScrollOffset);
            }

            invalidateCache(); // Full cache invalidation for zoom changes
            return true;
        } else if (isInTrackArea && shiftHeld) {
            // HORIZONTAL SCROLL: Shift+wheel (and synthetic Shift from laptop horizontal wheel).
            auto& themeManager = AestraUI::NUIThemeManager::getInstance();
            const float controlAreaWidth = themeManager.getLayoutDimensions().trackControlsWidth;
            const float gridStartX = controlAreaWidth + kTimelineGridInsetX;
            const float gridWidthPx = getTimelineGridWidthPixels();
            const double maxStartBeat = std::max(0.0, m_minimapDomainEndBeat - (gridWidthPx / m_pixelsPerBeat));
            const float maxTimelineScroll = static_cast<float>(maxStartBeat * m_pixelsPerBeat);
            constexpr float horizontalSpeed = 64.0f;
            m_timelineScrollOffset =
                safeClampFloat(m_timelineScrollOffset - event.wheelDelta * horizontalSpeed, 0.0f, maxTimelineScroll);
            for (auto& trackUI : m_trackUIComponents) {
                trackUI->setTimelineScrollOffset(m_timelineScrollOffset);
            }
            invalidateCache();
            return true;
        } else {
            // VERTICAL SCROLL: Regular scroll in track area.
            float scrollSpeed = 60.0f;
            float scrollDelta = -event.wheelDelta * scrollSpeed; // Invert for natural scroll direction

            m_targetScrollOffset += scrollDelta;

            // Clamp scroll offset
            float viewportHeight = bounds.height - kTimelineTimeBandHeight;

            const float laneCount = static_cast<float>(m_trackUIComponents.size());
            float totalContentHeight = laneCount * (m_trackHeight + m_trackSpacing);
            float maxScroll = std::max(0.0f, totalContentHeight - viewportHeight);
            m_targetScrollOffset = std::max(0.0f, std::min(m_targetScrollOffset, maxScroll));

            if (m_scrollbar) {
                m_scrollbar->setCurrentRange(m_scrollOffset, viewportHeight);
            }

            setDirty(true);
            return true;
        }
    }
    return false;
}

bool TrackManagerUI::handleRulerPress(const AestraUI::NUIMouseEvent& event, const AestraUI::NUIPoint& localPos, bool isInRuler) {
    // === RULER INTERACTION: Loop markers, Playhead scrubbing OR timeline selection ===
    // Loop handles may sit left of the plane edge when scrolled (the range can
    // intersect the viewport while a handle renders over the toolbar corner's
    // dead zone). They stay hoverable/grabbable anywhere in the ruler row;
    // scrub, selection, and click-seek remain confined to right of that edge.
    const bool inRulerRow =
        localPos.y >= kTimelineMinimapHeight && localPos.y < kTimelineTimeBandHeight;
    if (isInRuler || (inRulerRow && m_hasRulerSelection)) {
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        float controlAreaWidth = layout.trackControlsWidth;
        float gridStartX = controlAreaWidth + kTimelineGridInsetX;

        // === LOOP MARKER INTERACTION (highest priority) ===
        // Hover tracking lives in tryBeginLoopHandleDrag so the pre-toolbar
        // dispatch path and this path share one hit-test/grab contract.
        if (m_hasRulerSelection) {
            updateLoopHandleHover(localPos, gridStartX);
            if (tryBeginLoopHandleDrag(event, localPos)) {
                return true;
            }
        }
        // Corner dead zone: only marker hover/grab (handled above) is allowed
        // here — a click in the toolbar cell's empty remainder must never seek.
        if (!isInRuler) {
            return false;
        }

        // Right-click or Ctrl+Left-click starts ruler selection for looping
        bool isSelectionClick = (event.pressed && event.button == AestraUI::NUIMouseButton::Right) ||
                                (event.pressed && event.button == AestraUI::NUIMouseButton::Left &&
                                 (event.modifiers & AestraUI::NUIModifiers::Ctrl));

        // Regular left-click (without Ctrl) starts playhead scrubbing
        // BUT NOT if we're hovering over a loop marker!
        bool isPlayheadClick = event.pressed && event.button == AestraUI::NUIMouseButton::Left &&
                               !(event.modifiers & AestraUI::NUIModifiers::Ctrl) && !m_hoveringLoopStart &&
                               !m_hoveringLoopEnd;

        if (isSelectionClick) {
            // Start ruler selection
            m_isDraggingRulerSelection = true;

            float gridStartX = controlAreaWidth + kTimelineGridInsetX;

            // Convert mouse position to beat
            float mouseX = localPos.x - gridStartX + m_timelineScrollOffset;
            double positionInBeats = mouseX / m_pixelsPerBeat;

            // Snap to grid
            positionInBeats = snapBeatToGrid(positionInBeats);
            positionInBeats = std::max(0.0, positionInBeats);

            m_rulerSelectionStartBeat = positionInBeats;
            m_rulerSelectionEndBeat = positionInBeats;
            m_hasRulerSelection = false; // Not confirmed until mouse moves/releases

            invalidateCache(); // Selection started
            return true;
        } else if (isPlayheadClick && !m_isDraggingRulerSelection) {
            // Start dragging playhead (existing behavior)
            // Don't start if we're already doing a ruler selection!
            m_isDraggingPlayhead = true;
            if (m_trackManager) {
                m_trackManager->setUserScrubbing(true);

                // IMMEDIATE CLICK: Move playhead to clicked position right away
                auto& themeManager = AestraUI::NUIThemeManager::getInstance();
                const auto& layout = themeManager.getLayoutDimensions();
                float controlAreaWidth = layout.trackControlsWidth;
                float gridStartX = controlAreaWidth + kTimelineGridInsetX;

                auto& playlist = m_trackManager->getPlaylistModel();
                float mouseX = localPos.x - gridStartX + m_timelineScrollOffset;

                double positionInBeats = mouseX / m_pixelsPerBeat;
                double positionInSeconds = playlist.beatToSeconds(positionInBeats);
                positionInSeconds = std::max(0.0, positionInSeconds);

                m_trackManager->setPosition(positionInSeconds);
                m_trackManager->setPlayStartPosition(positionInSeconds);
            }
            return true;
        }
    }
    return false;
}

// Shared loop-handle hit test. Returns true when localPos sits inside a
// handle's grab zone; hitStart reports which one. Pure — no state mutation.
bool TrackManagerUI::hitLoopHandle(const AestraUI::NUIPoint& localPos, float gridStartX, bool& hitStart) const {
    const float loopStartX =
        gridStartX + (static_cast<float>(m_loopStartBeat) * m_pixelsPerBeat) - m_timelineScrollOffset;
    const float loopEndX =
        gridStartX + (static_cast<float>(m_loopEndBeat) * m_pixelsPerBeat) - m_timelineScrollOffset;

    constexpr float hitZone = 12.0f; // Hit zone around markers
    const bool nearLoopStart = std::abs(localPos.x - loopStartX) < hitZone;
    const bool nearLoopEnd = std::abs(localPos.x - loopEndX) < hitZone;
    if (nearLoopStart == nearLoopEnd) {
        return false; // neither zone, or ambiguous double overlap
    }
    hitStart = nearLoopStart;
    return true;
}

void TrackManagerUI::updateLoopHandleHover(const AestraUI::NUIPoint& localPos, float gridStartX) {
    bool hitStart = false;
    const bool hit = hitLoopHandle(localPos, gridStartX, hitStart);
    const bool wasHoveringStart = m_hoveringLoopStart;
    const bool wasHoveringEnd = m_hoveringLoopEnd;
    m_hoveringLoopStart = hit && hitStart;
    m_hoveringLoopEnd = hit && !hitStart;
    if (wasHoveringStart != m_hoveringLoopStart || wasHoveringEnd != m_hoveringLoopEnd) {
        invalidateCache(); // Hover state changed
    }
}

bool TrackManagerUI::tryBeginLoopHandleDrag(const AestraUI::NUIMouseEvent& event,
                                            const AestraUI::NUIPoint& localPos) {
    // Loop markers outrank toolbar icons: a handle scrolled over the corner
    // must stay draggable even where its grab zone overlaps a button.
    if (!event.pressed || event.button != AestraUI::NUIMouseButton::Left) {
        return false;
    }
    if (!m_playlistVisible || !m_hasRulerSelection) {
        return false;
    }
    if (localPos.y < kTimelineMinimapHeight || localPos.y >= kTimelineTimeBandHeight) {
        return false;
    }

    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    float controlAreaWidth = themeManager.getLayoutDimensions().trackControlsWidth;
    float gridStartX = controlAreaWidth + kTimelineGridInsetX;

    updateLoopHandleHover(localPos, gridStartX);

    bool hitStart = false;
    if (!hitLoopHandle(localPos, gridStartX, hitStart)) {
        return false;
    }
    if (hitStart) {
        m_isDraggingLoopStart = true;
        m_loopDragStartBeat = m_loopStartBeat;
    } else {
        m_isDraggingLoopEnd = true;
        m_loopDragStartBeat = m_loopEndBeat;
    }
    return true;
}

bool TrackManagerUI::handleRulerSelectionDrag(const AestraUI::NUIMouseEvent& event, const AestraUI::NUIPoint& localPos) {
    if (m_isDraggingRulerSelection) {
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        float controlAreaWidth = layout.trackControlsWidth;
        float gridStartX = controlAreaWidth + kTimelineGridInsetX;

        // Update selection end position
        float mouseX = localPos.x - gridStartX + m_timelineScrollOffset;
        double positionInBeats = mouseX / m_pixelsPerBeat;

        // Snap to grid
        positionInBeats = snapBeatToGrid(positionInBeats);
        positionInBeats = std::max(0.0, positionInBeats);

        m_rulerSelectionEndBeat = positionInBeats;

        // Mark selection as active if dragged at least one snap unit
        if (std::abs(m_rulerSelectionEndBeat - m_rulerSelectionStartBeat) > 0.001) {
            m_hasRulerSelection = true;
        }

        invalidateCache(); // Selection dragging

        // Stop dragging on mouse release
        if ((event.released && event.button == AestraUI::NUIMouseButton::Right) ||
            (event.released && event.button == AestraUI::NUIMouseButton::Left)) {
            m_isDraggingRulerSelection = false;

            // Only keep selection if it has a valid range
            if (m_hasRulerSelection) {
                // Get normalized selection range
                double selStartBeat = std::min(m_rulerSelectionStartBeat, m_rulerSelectionEndBeat);
                double selEndBeat = std::max(m_rulerSelectionStartBeat, m_rulerSelectionEndBeat);

                // Update loop markers to match selection through centralized propagation.
                updateSelectionLoopRegion(selStartBeat, selEndBeat);
                m_loopPreset = TimelineLoopPreset::Selection;

                // Call selection callback - this will jump playhead and set loop region
                if (m_onSelectionMade) {
                    m_onSelectionMade(selStartBeat, selEndBeat);
                }

                // Also notify loop preset changed to selection mode
                if (m_onLoopPresetChanged) {
                    m_onLoopPresetChanged(timelineLoopPresetId(TimelineLoopPreset::Selection));
                }

                Log::info("[TrackManagerUI] Ruler selection: " + std::to_string(selStartBeat) + " to " +
                          std::to_string(selEndBeat) + " beats");
            } else {
                // Click without drag - clear selection and disable loop
                setLoopRegion(0.0, 0.0, false);
                m_loopPreset = TimelineLoopPreset::Off;

                // SPECIAL: If we clicked on an EXISTNG range on ruler, show menu
                // (Wait, this is handled in the isInRuler block if it's an instant click)

                // Trigger loop OFF callback
                if (m_onLoopPresetChanged) {
                    m_onLoopPresetChanged(timelineLoopPresetId(TimelineLoopPreset::Off));
                }
            }

            return true;
        }

        return true;
    }
    return false;
}

bool TrackManagerUI::handleRulerSelectionMenu(const AestraUI::NUIMouseEvent& event, const AestraUI::NUIPoint& localPos, bool isInRuler) {
    // === RULER SELECTION CONTEXT MENU (Click on active range) ===
    if (isInRuler && event.pressed && event.button == AestraUI::NUIMouseButton::Left && m_hasRulerSelection &&
        !m_hoveringLoopStart && !m_hoveringLoopEnd) {
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        float gridStartX = layout.trackControlsWidth + kTimelineGridInsetX;

        float loopStartX =
            gridStartX + (static_cast<float>(m_loopStartBeat) * m_pixelsPerBeat) - m_timelineScrollOffset;
        float loopEndX = gridStartX + (static_cast<float>(m_loopEndBeat) * m_pixelsPerBeat) - m_timelineScrollOffset;
        float minX = std::min(loopStartX, loopEndX);
        float maxX = std::max(loopStartX, loopEndX);

        if (localPos.x >= minX && localPos.x <= maxX) {
            if (m_activeContextMenu) {
                detachContextMenu(m_activeContextMenu);
            }
            m_activeContextMenu = std::make_shared<AestraUI::NUIContextMenu>();
            auto menu = m_activeContextMenu;

            menu->addItem("Send Selection to Audition", [this]() {
                if (m_onSendSelectionToAudition) {
                    m_onSendSelectionToAudition(m_loopStartBeat, m_loopEndBeat);
                }
            });

            attachAndShowContextMenu(this, menu, event.position);
            return true;
        }
    }
    return false;
}

bool TrackManagerUI::handleLoopMarkerDrag(const AestraUI::NUIMouseEvent& event, const AestraUI::NUIPoint& localPos) {
    // Handle loop marker dragging
    if (m_isDraggingLoopStart || m_isDraggingLoopEnd) {
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        float controlAreaWidth = layout.trackControlsWidth;
        float gridStartX = controlAreaWidth + kTimelineGridInsetX;

        // Stop dragging on mouse release
        if (event.released && event.button == AestraUI::NUIMouseButton::Left) {
            m_isDraggingLoopStart = false;
            m_isDraggingLoopEnd = false;
            m_loopPreset = TimelineLoopPreset::Selection;

            // Update audio engine loop region
            if (m_onLoopPresetChanged) {
                m_onLoopPresetChanged(timelineLoopPresetId(TimelineLoopPreset::Selection));
            }

            return true;
        }

        // Update marker position while dragging
        float mouseX = localPos.x - gridStartX + m_timelineScrollOffset;
        double positionInBeats = mouseX / m_pixelsPerBeat;

        // Snap to grid
        positionInBeats = snapBeatToGrid(positionInBeats);
        positionInBeats = std::max(0.0, positionInBeats);

        if (m_isDraggingLoopStart) {
            // Don't allow start to go past end
            if (positionInBeats < m_loopEndBeat) {
                updateSelectionLoopRegion(positionInBeats, m_loopEndBeat);
            }
        } else if (m_isDraggingLoopEnd) {
            // Don't allow end to go before start
            if (positionInBeats > m_loopStartBeat) {
                updateSelectionLoopRegion(m_loopStartBeat, positionInBeats);
            }
        }

        invalidateCache(); // Loop marker position changed
        return true;
    }
    return false;
}

bool TrackManagerUI::handlePlayheadDrag(const AestraUI::NUIMouseEvent& event, const AestraUI::NUIPoint& localPos) {
    // Handle playhead dragging (continuous scrub)
    // IMPORTANT: Don't handle playhead if we're doing ruler selection!
    if (m_isDraggingPlayhead && !m_isDraggingRulerSelection) {
        // Stop dragging on mouse release
        if (event.released && event.button == AestraUI::NUIMouseButton::Left) {
            m_isDraggingPlayhead = false;
            if (m_trackManager) {
                m_trackManager->setUserScrubbing(false);
            }
            return true;
        }

        // Update playhead position while dragging (even outside ruler bounds for smooth scrubbing)
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        float controlAreaWidth = layout.trackControlsWidth;
        float gridStartX = controlAreaWidth + kTimelineGridInsetX;

        // Update playhead position while dragging
        auto& playlist = m_trackManager->getPlaylistModel();
        float mouseX = localPos.x - gridStartX + m_timelineScrollOffset;

        // Convert pixel position to time (seconds) using new temporal seams
        double positionInBeats = mouseX / m_pixelsPerBeat;
        double positionInSeconds = playlist.beatToSeconds(positionInBeats);

        // Clamp to valid range
        positionInSeconds = std::max(0.0, positionInSeconds);

        if (m_trackManager) {
            m_trackManager->setPosition(positionInSeconds);
            m_trackManager->setPlayStartPosition(positionInSeconds);
        }

        return true;
    }
    return false;
}

bool TrackManagerUI::handleSplitToolClick(const AestraUI::NUIMouseEvent& event, const AestraUI::NUIPoint& localPos) {
    const AestraUI::NUIRect bounds = getBounds();
    // === SPLIT TOOL: Click to split track at position ===
    if (m_currentTool == PlaylistTool::Split && event.pressed && event.button == AestraUI::NUIMouseButton::Left) {
        // Check if click is in track area
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        float controlAreaWidth = layout.trackControlsWidth;
        float gridStartX = controlAreaWidth + kTimelineGridInsetX;

        float trackAreaTop = kTimelineTimeBandHeight;

        AestraUI::NUIRect gridBounds(bounds.x + gridStartX, bounds.y + trackAreaTop,
                                     bounds.width - controlAreaWidth - 20.0f, bounds.height - trackAreaTop);

        if (gridBounds.contains(event.position)) {
            // Find which track was clicked
            float relativeY = localPos.y - trackAreaTop + m_scrollOffset;
            int trackIndex = static_cast<int>(relativeY / (m_trackHeight + m_trackSpacing));

            if (trackIndex >= 0 && trackIndex < static_cast<int>(m_trackUIComponents.size())) {
                // Calculate beat position from click X
                auto& playlist = m_trackManager->getPlaylistModel();
                float mouseX = localPos.x - gridStartX + m_timelineScrollOffset;
                double positionInBeats = mouseX / m_pixelsPerBeat;

                // Snap to grid if enabled (Canonical Beat-Space)
                if (m_snapEnabled) {
                    positionInBeats = snapBeatToGrid(positionInBeats);
                }

                // Perform the split (PlaylistModel now handles beat-space splits)
                performSplitAtPosition(trackIndex, playlist.beatToSeconds(positionInBeats));
                return true;
            }
        }
    }
    return false;
}
bool TrackManagerUI::onKeyEvent(const AestraUI::NUIKeyEvent& event) {
    if (event.pressed) {
        // Hotkey 'A' toggles Automation Mode (FL/Ableton style)
        if (event.keyCode == AestraUI::NUIKeyCode::A && !(event.modifiers & AestraUI::NUIModifiers::Ctrl)) {
            setPlaylistMode(m_playlistMode == PlaylistMode::Clips ? PlaylistMode::Automation : PlaylistMode::Clips);
            return true;
        }

        // Tool shortcuts
        if (event.keyCode == AestraUI::NUIKeyCode::Num1) {
            setCurrentTool(PlaylistTool::Select);
            return true;
        }
        if (event.keyCode == AestraUI::NUIKeyCode::Num2) {
            setCurrentTool(PlaylistTool::Split);
            return true;
        }

        if ((event.keyCode == AestraUI::NUIKeyCode::Delete || event.keyCode == AestraUI::NUIKeyCode::Backspace) &&
            (m_selectedClipId.isValid() || !m_clipSelection.empty())) {
            deleteSelectedClip();
            return true;
        }

        if (event.keyCode == AestraUI::NUIKeyCode::Escape &&
            (m_selectedClipId.isValid() || !m_selectedTracks.empty() || !m_clipSelection.empty())) {
            clearClipSelection();
            selectClip(ClipInstanceID{});
            clearSelection();
            return true;
        }

        // Undo/Redo is handled globally by AestraContent — don't duplicate here

        // Clipboard (Ctrl+C/V/X/D)
        if (event.modifiers & AestraUI::NUIModifiers::Ctrl) {
            if (event.keyCode == AestraUI::NUIKeyCode::A) {
                // #848: Ctrl+A promotes to clip selection; tracks-only remains
                // the fallback for an empty timeline.
                selectAllClips();
                return true;
            }
            if (event.keyCode == AestraUI::NUIKeyCode::X &&
                (m_selectedClipId.isValid() || !m_clipSelection.empty())) {
                cutSelectedClip();
                return true;
            }
            if (event.keyCode == AestraUI::NUIKeyCode::C && m_selectedClipId.isValid()) {
                copySelectedClip();
                return true;
            }
            if (event.keyCode == AestraUI::NUIKeyCode::V && hasClipboardClip()) {
                pasteClipboardAtCursor();
                return true;
            }
            // Ctrl+B: duplicate the whole selection (#848; matches the Arsenal grid).
            if (event.keyCode == AestraUI::NUIKeyCode::B &&
                (m_selectedClipId.isValid() || !m_clipSelection.empty())) {
                duplicateSelectedClip();
                return true;
            }
            if (event.keyCode == AestraUI::NUIKeyCode::D && m_selectedClipId.isValid()) {
                duplicateSelectedClip();
                return true;
            }
            // Ctrl+B: Paste-to-right (paste at end of selected clip, select new clip)
            if (event.keyCode == AestraUI::NUIKeyCode::B &&
                (m_selectedClipId.isValid() || hasClipboardClip())) {
                pasteClipToRight();
                return true;
            }
        }
    }
    return false;
}

} // namespace Audio
} // namespace Aestra
