// © 2025 Nomad Studios All Rights Reserved. Licensed for personal & educational use only.
#include "TrackManagerUI.h"
#include "../NomadAudio/include/MixerChannel.h"
#include "../NomadAudio/include/TrackManager.h"
#include "../NomadUI/Platform/NUIPlatformBridge.h"

#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Core/NUIDragDrop.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "../NomadCore/include/NomadLog.h"
#include "../NomadAudio/include/AudioFileValidator.h"
#include "../NomadAudio/include/MiniAudioDecoder.h"
#include "../NomadAudio/include/ClipSource.h"
#include <algorithm>
#include <cmath>
#include <map>

// Remotery profiling (conditionally enabled via CMake)
#ifdef NOMAD_ENABLE_REMOTERY
    #include "Remotery.h"
#else
    // Stub macros when Remotery is disabled
    #define rmt_ScopedCPUSample(name, flags) ((void)0)
    #define rmt_BeginCPUSample(name, flags) ((void)0)
    #define rmt_EndCPUSample() ((void)0)
#endif

namespace Nomad {
namespace Audio {

TrackManagerUI::TrackManagerUI(std::shared_ptr<TrackManager> trackManager)
    : m_trackManager(trackManager)
    , m_cacheId(reinterpret_cast<uint64_t>(this))
    , m_cacheInvalidated(true)
    , m_isRenderingToCache(false)
{
    if (!m_trackManager) {
        Log::error("TrackManagerUI created with null track manager");
        return;
    }

    // Create add-track icon (SVG) for consistent header styling
    const char* addTrackSvg = R"(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round">
            <path d="M12 5v14M5 12h14"/>
        </svg>
    )";
    m_addTrackIcon = std::make_shared<NomadUI::NUIIcon>(addTrackSvg);
    m_addTrackIcon->setIconSize(16.0f, 16.0f);
    m_addTrackIcon->setColorFromTheme("textPrimary");
    m_addTrackIcon->setVisible(true);
    
    // Create scrollbar
    m_scrollbar = std::make_shared<NomadUI::NUIScrollbar>(NomadUI::NUIScrollbar::Orientation::Vertical);
    m_scrollbar->setOnScroll([this](double position) {
        onScroll(position);
    });
    addChild(m_scrollbar);
    
    // Timeline minimap (replaces top horizontal scrollbar).
    m_timelineMinimap = std::make_shared<NomadUI::TimelineMinimapBar>();
    m_timelineMinimap->onRequestCenterView = [this](double centerBeat) { centerTimelineViewAtBeat(centerBeat); };
    m_timelineMinimap->onRequestSetViewStart = [this](double viewStartBeat, bool isFinal) {
        setTimelineViewStartBeat(viewStartBeat, isFinal);
    };
    m_timelineMinimap->onRequestResizeViewEdge =
        [this](NomadUI::TimelineMinimapResizeEdge edge, double anchorBeat, double edgeBeat, bool isFinal) {
            resizeTimelineViewEdgeFromMinimap(edge, anchorBeat, edgeBeat, isFinal);
        };
    m_timelineMinimap->onRequestZoomAround = [this](double anchorBeat, float zoomMultiplier) {
        zoomTimelineAroundBeat(anchorBeat, zoomMultiplier);
    };
    m_timelineMinimap->onModeChanged = [this](NomadUI::TimelineMinimapMode mode) {
        m_minimapMode = mode;
        setDirty(true);
    };
    addChild(m_timelineMinimap);

    // Create UI components for existing tracks
    refreshTracks();

    // Register as observer of the playlist model to handle dynamic changes (FL Studio style)
    m_trackManager->getPlaylistModel().addChangeObserver([this]() {
        Log::info("[TrackManagerUI] Playlist model changed, refreshing UI...");
        refreshTracks();
        invalidateCache();
        scheduleTimelineMinimapRebuild();
        setDirty(true);
    });

    // Create tool icons
    createToolIcons();
    
    // Register as drop target for drag-and-drop
    // Moved to onUpdate to allow shared_from_this() to work
    // NomadUI::NUIDragDropManager::getInstance().registerDropTarget(this);
}

TrackManagerUI::~TrackManagerUI() {
    // Unregister from drag-drop manager
    NomadUI::NUIDragDropManager::getInstance().unregisterDropTarget(this);
    
    // ⚡ Texture cleanup handled by renderer shutdown
    // Note: NUIRenderer is not a singleton, so manual texture cleanup in destructor
    // is not feasible. The renderer will clean up all textures on shutdown.
    Log::info("TrackManagerUI destroyed");
}

void TrackManagerUI::setPlatformWindow(NomadUI::NUIPlatformBridge* window) {
    m_window = window;
}

void TrackManagerUI::createToolIcons() {
    // === POINTER/SELECT TOOL ICON (Simple arrow) ===
    const char* selectSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path d="M5 3 L5 17 L9 13 L12 19 L14 18 L11 12 L16 12 Z" fill="#AAAAAA" stroke="#666666" stroke-width="0.5"/></svg>)";
    m_selectToolIcon = std::make_shared<NomadUI::NUIIcon>(selectSvg);
    
    // === SPLIT TOOL ICON (Scissors) ===
    const char* splitSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><circle cx="6" cy="6" r="3" fill="none" stroke="#FF6B6B" stroke-width="1.5"/><circle cx="6" cy="18" r="3" fill="none" stroke="#FF6B6B" stroke-width="1.5"/><line x1="8" y1="8" x2="18" y2="16" stroke="#AAAAAA" stroke-width="2"/><line x1="8" y1="16" x2="18" y2="8" stroke="#AAAAAA" stroke-width="2"/></svg>)";
    m_splitToolIcon = std::make_shared<NomadUI::NUIIcon>(splitSvg);
    
    // === MULTI-SELECT TOOL ICON (Dashed box) ===
    const char* multiSelectSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><rect x="4" y="6" width="16" height="12" fill="none" stroke="#4FC3F7" stroke-width="2" stroke-dasharray="3,2"/></svg>)";
    m_multiSelectToolIcon = std::make_shared<NomadUI::NUIIcon>(multiSelectSvg);
    
    // Loop Dropdown (replaces loop tool icon)
    m_loopDropdown = std::make_shared<NomadUI::NUIDropdown>();
    m_loopDropdown->addItem("Off", 0);
    m_loopDropdown->addItem("1 Bar", 1);
    m_loopDropdown->addItem("2 Bars", 2);
    m_loopDropdown->addItem("4 Bars", 3);
    m_loopDropdown->addItem("8 Bars", 4);
    m_loopDropdown->addItem("Selection", 5);
    m_loopDropdown->setSelectedIndex(1);  // FL Studio mode: 1 Bar by default
    m_loopDropdown->setOnSelectionChanged([this](int idx, int id, const std::string& txt) {
        m_loopPreset = id;
        
        // Calculate loop region and propagate to TrackUIComponents for visual rendering
        bool loopEnabled = (id != 0);
        double loopStartBeat = 0.0;
        double loopEndBeat = 4.0;  // Default 1 bar
        
        if (id >= 1 && id <= 4) {
            double loopBars = (id == 1) ? 1.0 : (id == 2) ? 2.0 : (id == 3) ? 4.0 : 8.0;
            loopEndBeat = loopBars * m_beatsPerBar;
        } else if (id == 5) {
            // Selection: Use calculated selection range
            auto selection = getSelectionBeatRange();
            if (selection.second > selection.first) {
                loopStartBeat = selection.first;
                loopEndBeat = selection.second;
                loopEnabled = true; // Ensure enabled even if id != 0
            } else {
                loopEnabled = false; // Disable if no valid selection
                Log::warning("Loop Selection: No valid selection found");
            }
        }
        
        // Propagate loop state to all track components
        for (auto& trackUI : m_trackUIComponents) {
            if (trackUI) {
                trackUI->setLoopEnabled(loopEnabled);
                trackUI->setLoopRegion(loopStartBeat, loopEndBeat);
            }
        }
        
        // Trigger external callback (for AudioEngine)
        if (m_onLoopPresetChanged) {
            m_onLoopPresetChanged(id);
        }
        
        invalidateCache();
        Log::info("Loop preset changed to: " + txt);
    });
    m_loopDropdown->setMaxVisibleItems(6);
    addChild(m_loopDropdown);

    // Snap Dropdown
    m_snapDropdown = std::make_shared<NomadUI::NUIDropdown>();
    auto snaps = NomadUI::MusicTheory::getSnapOptions();
    for(size_t i=0; i<snaps.size(); ++i) {
         m_snapDropdown->addItem(NomadUI::MusicTheory::getSnapName(snaps[i]), static_cast<int>(snaps[i]));
    }
    m_snapDropdown->setSelectedIndex(1); // Beat
    m_snapDropdown->setOnSelectionChanged([this](int idx, int id, const std::string& txt){
        setSnapSetting(static_cast<NomadUI::SnapGrid>(id));
    });
    m_snapDropdown->setMaxVisibleItems(12);
    addChild(m_snapDropdown);
    
    Log::info("Tool icons created");
}

void TrackManagerUI::setCurrentTool(PlaylistTool tool) {
    if (m_currentTool != tool) {
        m_currentTool = tool;
        m_showSplitCursor = (tool == PlaylistTool::Split);
        m_cacheInvalidated = true;  // Redraw toolbar with new selection
        
        // Restore cursor visibility when switching away from Split tool
        if (tool != PlaylistTool::Split && m_onCursorVisibilityChanged) {
            m_onCursorVisibilityChanged(true);
        }
        
        const char* toolNames[] = {"Select", "Split", "MultiSelect", "Loop", "Draw", "Erase", "Mute", "Slip"};
        Log::info("Active tool changed to: " + std::string(toolNames[static_cast<int>(tool)]));
    }
}

void TrackManagerUI::setSnapSetting(NomadUI::SnapGrid snap) {
    if (m_snapSetting == snap) return;
    
    m_snapSetting = snap;
    
    // Propagate to tracks
    for (auto& track : m_trackUIComponents) {
        if (track) track->setSnapSetting(snap);
    }
    
    // Request redraw of grid
    m_backgroundNeedsUpdate = true;
    invalidateCache();
    
    Log::info("TrackManager Snap set to: " + NomadUI::MusicTheory::getSnapName(snap));
}

void TrackManagerUI::updateToolbarBounds() {
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    NomadUI::NUIRect bounds = getBounds();

    constexpr float headerHeight = 40.0f; // Unified playlist header strip (Standardized)
    const float iconSpacing = themeManager.getSpacing("xs"); // 4px grid
    const float innerPad = themeManager.getSpacing("s"); // 8px

    // Toolbar is docked inside the header, to the right of the + button.
    const float plusSize = headerHeight - themeManager.getSpacing("xs") * 2.0f; // 30px
    const float plusX = bounds.x + innerPad;
    const float plusY = bounds.y + (headerHeight - plusSize) * 0.5f;
    m_addTrackBounds = NomadUI::NUIRect(plusX, plusY, plusSize, plusSize);

    const float iconSize = plusSize; // Keep all toolbar buttons consistent with +
    float toolbarX = plusX + plusSize + innerPad;
    float toolbarY = plusY;

    const int numTools = 3;  // Select, Split, MultiSelect (dropdown replaces Loop icon)
    float toolbarWidth = (iconSize * numTools) + (iconSpacing * (numTools - 1));
    float toolbarHeight = iconSize;

    m_toolbarBounds = NomadUI::NUIRect(toolbarX, toolbarY, toolbarWidth, toolbarHeight);

    float iconX = toolbarX;
    float iconY = toolbarY;

    m_selectToolBounds = NomadUI::NUIRect(iconX, iconY, iconSize, iconSize);
    iconX += iconSize + iconSpacing;
    m_splitToolBounds = NomadUI::NUIRect(iconX, iconY, iconSize, iconSize);
    iconX += iconSize + iconSpacing;
    m_multiSelectToolBounds = NomadUI::NUIRect(iconX, iconY, iconSize, iconSize);
    
    // Loop Dropdown (after tool icons)
    iconX += iconSize + iconSpacing + 8.0f;
    float loopDropdownW = 70.0f;
    m_loopDropdownBounds = NomadUI::NUIRect(iconX, iconY, loopDropdownW, iconSize);
    if (m_loopDropdown) m_loopDropdown->setBounds(m_loopDropdownBounds);
    
    // Snap Dropdown (after loop dropdown)
    iconX += loopDropdownW + iconSpacing + 4.0f;
    float dropdownW = 100.0f;
    m_snapDropdownBounds = NomadUI::NUIRect(iconX, iconY, dropdownW, iconSize);
    if(m_snapDropdown) m_snapDropdown->setBounds(m_snapDropdownBounds);
}

void TrackManagerUI::renderToolbar(NomadUI::NUIRenderer& renderer) {
    // Update bounds before rendering
    updateToolbarBounds();

    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const float radius = themeManager.getRadius("m"); // 8px buttons
    const auto activeBg = themeManager.getColor("primary").withAlpha(0.25f);
    const auto hoverBg = themeManager.getColor("hover").withAlpha(0.6f);

    // Add-track button (left)
    if (m_addTrackHovered) {
        // True Glass Hover
        renderer.fillRoundedRect(m_addTrackBounds, radius, themeManager.getColor("glassHover"));
        renderer.strokeRoundedRect(m_addTrackBounds, radius, 1.0f, themeManager.getColor("glassBorder"));
    }
    if (m_addTrackIcon) {
        const float iconSz = 16.0f;
        NomadUI::NUIRect iconRect(
            std::round(m_addTrackBounds.x + (m_addTrackBounds.width - iconSz) * 0.5f),
            std::round(m_addTrackBounds.y + (m_addTrackBounds.height - iconSz) * 0.5f),
            iconSz, iconSz
        );
        m_addTrackIcon->setBounds(iconRect);
        m_addTrackIcon->setColorFromTheme("textPrimary");
        m_addTrackIcon->onRender(renderer);
    }

    // Helper lambda to draw icon with selection state
    auto drawToolIcon = [&](std::shared_ptr<NomadUI::NUIIcon>& icon, const NomadUI::NUIRect& bounds, PlaylistTool tool, bool hovered) {
        bool isActive = (m_currentTool == tool);
        
        // Draw selection highlight background
        if (isActive) {
            // Active state: Glassy colored overlay
            renderer.fillRoundedRect(bounds, radius, themeManager.getColor("glassActive"));
            renderer.strokeRoundedRect(bounds, radius, 1.0f, themeManager.getColor("primary").withAlpha(0.4f));
        } else if (hovered) {
            // Hover state: True Glass (neutral)
            renderer.fillRoundedRect(bounds, radius, themeManager.getColor("glassHover"));
            renderer.strokeRoundedRect(bounds, radius, 1.0f, themeManager.getColor("glassBorder"));
        }
        
        // Draw icon
        if (icon) {
            const float iconSz = 16.0f;
            NomadUI::NUIRect iconRect(
                std::round(bounds.x + (bounds.width - iconSz) * 0.5f),
                std::round(bounds.y + (bounds.height - iconSz) * 0.5f),
                iconSz, iconSz
            );
            icon->setBounds(iconRect);
            icon->onRender(renderer);
        }
    };
    
    // Draw each tool icon with hover state
    drawToolIcon(m_selectToolIcon, m_selectToolBounds, PlaylistTool::Select, m_selectToolHovered);
    drawToolIcon(m_splitToolIcon, m_splitToolBounds, PlaylistTool::Split, m_splitToolHovered);
    drawToolIcon(m_multiSelectToolIcon, m_multiSelectToolBounds, PlaylistTool::MultiSelect, m_multiSelectToolHovered);

    // Render Loop Dropdown
    if (m_loopDropdown) {
        m_loopDropdown->setBounds(m_loopDropdownBounds);
        m_loopDropdown->onRender(renderer);
    }

    // Render Snap Dropdown
    if (m_snapDropdown) {
        m_snapDropdown->setBounds(m_snapDropdownBounds);
        m_snapDropdown->onRender(renderer);
    }

    // Render Popup Lists (Manual Z-order correction if needed)
    if (m_loopDropdown && m_loopDropdown->isOpen()) {
        m_loopDropdown->renderDropdownList(renderer);
    }
    if (m_snapDropdown && m_snapDropdown->isOpen()) {
        m_snapDropdown->renderDropdownList(renderer);
    }
}

bool TrackManagerUI::handleToolbarClick(const NomadUI::NUIPoint& position) {
    if (m_addTrackBounds.contains(position)) {
        onAddTrackClicked();
        return true;
    }
    if (m_selectToolBounds.contains(position)) {
        setCurrentTool(PlaylistTool::Select);
        return true;
    }
    if (m_splitToolBounds.contains(position)) {
        setCurrentTool(PlaylistTool::Split);
        return true;
    }
    if (m_multiSelectToolBounds.contains(position)) {
        setCurrentTool(PlaylistTool::MultiSelect);
        return true;
    }
    // Loop dropdown handles its own click events

    return false;
}

void TrackManagerUI::renderSplitCursor(NomadUI::NUIRenderer& renderer, const NomadUI::NUIPoint& position) {
    // Only render if split tool is active
    if (m_currentTool != PlaylistTool::Split) {
        // Ensure cursor is visible when not in split mode
        if (m_cursorHidden && m_onCursorVisibilityChanged) {
            m_cursorHidden = false;
            m_onCursorVisibilityChanged(true);
        }
        return;
    }
    
    // Calculate grid bounds for checking
    NomadUI::NUIRect bounds = getBounds();
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    float controlAreaWidth = layout.trackControlsWidth;
    float gridStartX = bounds.x + controlAreaWidth + 5;
    float headerHeight = 38.0f;
    float rulerHeight = 28.0f;
    float horizontalScrollbarHeight = 24.0f;
    float trackAreaTop = bounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;
    
    NomadUI::NUIRect gridBounds(gridStartX, trackAreaTop, 
                                 bounds.width - controlAreaWidth - 20.0f, 
                                 bounds.height - headerHeight - rulerHeight - horizontalScrollbarHeight);
    
    // If mouse is outside grid, restore cursor and return
    if (!gridBounds.contains(position)) {
        if (m_cursorHidden && m_onCursorVisibilityChanged) {
            m_cursorHidden = false;
            m_onCursorVisibilityChanged(true);  // Show cursor when outside grid
        }
        return;
    }
    
    // Mouse is inside grid - ALWAYS hide system cursor and show scissors
    if (!m_cursorHidden && m_onCursorVisibilityChanged) {
        m_cursorHidden = true;
        m_onCursorVisibilityChanged(false);  // Hide cursor
    }
    
    // Find the clip directly under the cursor (both X AND Y position)
    float lineX = position.x;
    float lineY = position.y;
    NomadUI::NUIColor splitColor(255, 107, 107, 200);  // Coral red
    float dashLength = 6.0f;
    float gapLength = 4.0f;
    
    // Check each track for the clip directly under cursor (must match both X and Y)
    for (auto& trackUI : m_trackUIComponents) {
        if (!trackUI) continue;
        
        // Use the pre-calculated clip bounds from the component (populated during render)
        const auto& allClipBounds = trackUI->getAllClipBounds();
        
        for (auto it = allClipBounds.begin(); it != allClipBounds.end(); ++it) {
            const auto& clipBounds = it->second;
            // Check if mouse is within this clip's bounds (BOTH X and Y)
            if (clipBounds.contains(NomadUI::NUIPoint(lineX, lineY))) {
                // Draw dotted line only within this specific clip
                float lineTop = clipBounds.y;
                float lineBottom = clipBounds.y + clipBounds.height;
                
                float y = lineTop;
                while (y < lineBottom) {
                    float dashEnd = std::min(y + dashLength, lineBottom);
                    renderer.drawLine(NomadUI::NUIPoint(lineX, y), NomadUI::NUIPoint(lineX, dashEnd), 2.0f, splitColor);
                    y = dashEnd + gapLength;
                }
                return; // Stop after finding the clip under cursor
            }
        }
    }

    
    // Always draw scissors icon at cursor position when in grid (Split tool)
    if (m_splitToolIcon) {
        NomadUI::NUIRect iconRect(position.x - 10, position.y - 10, 20, 20);
        m_splitToolIcon->setBounds(iconRect);
        m_splitToolIcon->onRender(renderer);
    }
}

void TrackManagerUI::renderMinimapResizeCursor(NomadUI::NUIRenderer& renderer, const NomadUI::NUIPoint& position)
{
    // Split tool owns the cursor while we're over the clip grid.
    if (m_currentTool == PlaylistTool::Split) {
        NomadUI::NUIRect bounds = getBounds();
        auto& themeManager = NomadUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        const float controlAreaWidth = layout.trackControlsWidth;
        const float gridStartX = bounds.x + controlAreaWidth + 5.0f;
        const float headerHeight = 38.0f;
        const float rulerHeight = 28.0f;
        const float horizontalScrollbarHeight = 24.0f;
        const float trackAreaTop = bounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;

        NomadUI::NUIRect gridBounds(gridStartX, trackAreaTop, bounds.width - controlAreaWidth - 20.0f,
                                     bounds.height - headerHeight - rulerHeight - horizontalScrollbarHeight);
        if (gridBounds.contains(position)) {
            return;
        }
    }

    const bool wantsResizeCursor =
        (m_timelineMinimap && m_timelineMinimap->isVisible() &&
         m_timelineMinimap->getCursorHint() == NomadUI::TimelineMinimapCursorHint::ResizeHorizontal);

    if (!wantsResizeCursor) {
        if (m_cursorHidden && m_onCursorVisibilityChanged) {
            m_cursorHidden = false;
            m_onCursorVisibilityChanged(true);
        }
        return;
    }

    if (!m_cursorHidden && m_onCursorVisibilityChanged) {
        m_cursorHidden = true;
        m_onCursorVisibilityChanged(false);
    }

    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const NomadUI::NUIColor active = themeManager.getColor("borderActive").withAlpha(0.95f);
    const NomadUI::NUIColor shadow(0.0f, 0.0f, 0.0f, 0.70f);

    const float size = 18.0f;
    const float half = size * 0.5f;
    const float a = 5.0f; // Slightly larger arrow head for crispness

    const float x = std::floor(position.x) + 0.5f; // Align to pixel grid
    const float y = std::floor(position.y) + 0.5f;

    // Crisp White
    const NomadUI::NUIColor white(1.0f, 1.0f, 1.0f, 1.0f);
    // Dark shadow for contrast
    const NomadUI::NUIColor arrowShadow(0.0f, 0.0f, 0.0f, 0.8f);

    // Shadow (outline)
    constexpr float kShadowWidth = 4.0f;
    renderer.drawLine(NomadUI::NUIPoint(x - half, y), NomadUI::NUIPoint(x + half, y), kShadowWidth, arrowShadow);
    renderer.drawLine(NomadUI::NUIPoint(x - half, y), NomadUI::NUIPoint(x - half + a, y - a), kShadowWidth, arrowShadow);
    renderer.drawLine(NomadUI::NUIPoint(x - half, y), NomadUI::NUIPoint(x - half + a, y + a), kShadowWidth, arrowShadow);
    renderer.drawLine(NomadUI::NUIPoint(x + half, y), NomadUI::NUIPoint(x + half - a, y - a), kShadowWidth, arrowShadow);
    renderer.drawLine(NomadUI::NUIPoint(x + half, y), NomadUI::NUIPoint(x + half - a, y + a), kShadowWidth, arrowShadow);

    // Foreground (White)
    constexpr float kLineWidth = 2.0f;
    renderer.drawLine(NomadUI::NUIPoint(x - half, y), NomadUI::NUIPoint(x + half, y), kLineWidth, white);
    renderer.drawLine(NomadUI::NUIPoint(x - half, y), NomadUI::NUIPoint(x - half + a, y - a), kLineWidth, white);
    renderer.drawLine(NomadUI::NUIPoint(x - half, y), NomadUI::NUIPoint(x - half + a, y + a), kLineWidth, white);
    renderer.drawLine(NomadUI::NUIPoint(x + half, y), NomadUI::NUIPoint(x + half - a, y - a), kLineWidth, white);
    renderer.drawLine(NomadUI::NUIPoint(x + half, y), NomadUI::NUIPoint(x + half - a, y + a), kLineWidth, white);
}

void TrackManagerUI::performSplitAtPosition(int laneIndex, double timeSeconds) {
    auto& playlist = m_trackManager->getPlaylistModel();
    PlaylistLaneID laneId = playlist.getLaneId(laneIndex);
    if (!laneId.isValid()) return;

    double bpm = playlist.getBPM();
    double splitBeat = timeSeconds * (bpm / 60.0);

    auto* lane = playlist.getLane(laneId);
    if (!lane) return;

    ClipInstanceID targetClipId;
    for (const auto& clip : lane->clips) {
        if (splitBeat >= clip.startBeat && splitBeat < clip.startBeat + clip.durationBeats) {
            targetClipId = clip.id;
            break;
        }
    }

    if (targetClipId.isValid()) {
        playlist.splitClip(targetClipId, splitBeat);
        m_trackManager->markModified();
        refreshTracks();
        invalidateCache();
        Log::info("Successfully split clip at " + std::to_string(timeSeconds) + "s");
    }
}

// === INSTANT CLIP DRAGGING ===
void TrackManagerUI::startInstantClipDrag(TrackUIComponent* clip, const NomadUI::NUIPoint& clickPos) {
    if (!clip) return;
    
    m_isDraggingClipInstant = true;
    m_draggedClipTrack = clip;
    m_clipDragOffsetX = clickPos.x - clip->getBounds().x;
    
    Log::info("Started instant clip drag");
}

void TrackManagerUI::updateInstantClipDrag(const NomadUI::NUIPoint& currentPos) {
    if (!m_isDraggingClipInstant || !m_draggedClipTrack) return;
    
    // TODO: Implement actual clip position update when Track API supports it
    // For now just invalidate cache to show we're handling it
    m_cacheInvalidated = true;
}

void TrackManagerUI::finishInstantClipDrag() {
    if (!m_isDraggingClipInstant) return;
    
    Log::info("Finished instant clip drag");
    
    m_isDraggingClipInstant = false;
    m_draggedClipTrack = nullptr;
    m_clipOriginalTrackIndex = -1;
    m_cacheInvalidated = true;
}

void TrackManagerUI::cancelInstantClipDrag() {
    if (!m_isDraggingClipInstant || !m_draggedClipTrack) return;
    
    Log::info("Cancelled instant clip drag");
    
    m_isDraggingClipInstant = false;
    m_draggedClipTrack = nullptr;
    m_clipOriginalTrackIndex = -1;
    m_cacheInvalidated = true;
}

void TrackManagerUI::addTrack(const std::string& name) {
    if (m_trackManager) {
        // Create lane in PlaylistModel
        PlaylistLaneID laneId = m_trackManager->getPlaylistModel().createLane(name);
        
        // Create Mixer Channel, linking it to the new lane
        auto channel = m_trackManager->addChannel(name); // Assuming addChannel creates and returns a new channel
        
        // Create UI component for the track, passing both identifiers
        auto trackUI = std::make_shared<TrackUIComponent>(laneId, channel, m_trackManager.get());
        
        // Register callback for exclusive solo coordination
        trackUI->setOnSoloToggled([this](TrackUIComponent* soloedTrack) {
            this->onTrackSoloToggled(soloedTrack);
        });
        
        // Register callback for cache invalidation (button hover, etc.)
        trackUI->setOnCacheInvalidationNeeded([this]() {
            this->invalidateCache();
        });
        
        // Register callback for clip deletion with ripple animation
        trackUI->setOnClipDeleted([this](TrackUIComponent* trackComp, ClipInstanceID clipId, NomadUI::NUIPoint ripplePos) {
            this->onClipDeleted(trackComp, clipId, ripplePos);
        });
        
        m_trackUIComponents.push_back(trackUI);
        addChild(trackUI);

        layoutTracks();
        scheduleTimelineMinimapRebuild();
        m_cacheInvalidated = true;  // Invalidate cache when track added
        Log::info("Added track UI: " + name);
    }
}

void TrackManagerUI::refreshTracks() {
    if (!m_trackManager) return;

    // Clear existing UI components
    for (auto& trackUI : m_trackUIComponents) {
        removeChild(trackUI);
    }
    m_trackUIComponents.clear();

    // v3.0 logic: iterate over PlaylistModel lanes instead of Mixer channels
    auto& playlist = m_trackManager->getPlaylistModel();
    for (size_t i = 0; i < playlist.getLaneCount(); ++i) {
        auto laneId = playlist.getLaneId(i);
        auto lane = playlist.getLane(laneId);
        if (!lane) continue;
        
        // Find associated MixerChannel (we maintain a 1:1 mapping between lane index and channel index for now)
        auto channel = m_trackManager->getTrack(i);
        if (!channel) continue;

        // Create UI component with LaneID and MixerChannel
        auto trackUI = std::make_shared<TrackUIComponent>(laneId, channel, m_trackManager.get());
        
        // Register callbacks
        trackUI->setOnSoloToggled([this](TrackUIComponent* soloedTrack) {
            this->onTrackSoloToggled(soloedTrack);
        });
        
        trackUI->setOnCacheInvalidationNeeded([this]() {
            this->invalidateCache();
        });
        
        trackUI->setOnClipDeleted([this](TrackUIComponent* trackComp, ClipInstanceID clipId, NomadUI::NUIPoint ripplePos) {
            this->onClipDeleted(trackComp, clipId, ripplePos);
        });

        
        trackUI->setIsSplitToolActive([this]() {
            return this->m_currentTool == PlaylistTool::Split;
        });
        
        trackUI->setOnSplitRequested([this](TrackUIComponent* trackComp, double splitTime) {
            this->onSplitRequested(trackComp, splitTime);
        });
        
        trackUI->setOnClipSelected([this](TrackUIComponent* trackComp, ClipInstanceID clipId) {
            this->m_selectedClipId = clipId;
            Log::info("TrackManagerUI: Clip selected " + clipId.toString());
        });

        trackUI->setOnTrackSelected([this](TrackUIComponent* trackComp, bool addToSelection) {
            this->selectTrack(trackComp, addToSelection);
        });

        
        // Sync zoom/scroll settings
        trackUI->setPixelsPerBeat(m_pixelsPerBeat);
        trackUI->setBeatsPerBar(m_beatsPerBar);
        trackUI->setTimelineScrollOffset(m_timelineScrollOffset);
        
        m_trackUIComponents.push_back(trackUI);
        addChild(trackUI);
    } // Close lane loop

    layoutTracks();
    
    // Mixer strips are now refreshed by NomadContent when syncing state
    
    // Update scrollbar after tracks are refreshed (fixes initial glitch)
    scheduleTimelineMinimapRebuild();
    updateTimelineMinimap(0.0);
    
    m_cacheInvalidated = true;  // Invalidate cache when tracks refreshed
}

void TrackManagerUI::onTrackSoloToggled(TrackUIComponent* soloedTrack) {
    if (!m_trackManager || !soloedTrack) return;
    
    // Exclusive Solo: Unsolo everyone ELSE
    for (auto& trackUI : m_trackUIComponents) {
        // Skip the track that was just soloed
        if (trackUI.get() == soloedTrack) continue;
        
        // Check if other track is soloed
        auto channel = trackUI->getChannel();
        if (channel && channel->isSoloed()) {
            channel->setSolo(false); // Unsolo model
            trackUI->updateUI();     // Update UI
            trackUI->repaint();
        }
    }
    
    invalidateCache();
    Log::info("Solo coordination: Cleared other solos (Exclusive Mode)");
}

void TrackManagerUI::onClipDeleted(TrackUIComponent* trackComp, ClipInstanceID clipId, const NomadUI::NUIPoint& rippleCenter) {
    if (!trackComp || !m_trackManager || !clipId.isValid()) return;
    
    auto& playlist = m_trackManager->getPlaylistModel();
    const auto* clip = playlist.getClip(clipId);
    if (!clip) return;

    // Get clip bounds for animation before we delete
    NomadUI::NUIRect clipBounds = trackComp->getBounds(); 

    // Start delete animation
    DeleteAnimation anim;
    anim.laneId = trackComp->getLaneId();
    anim.clipId = clipId;
    anim.rippleCenter = rippleCenter;
    anim.clipBounds = clipBounds;
    anim.progress = 0.0f;
    anim.duration = 0.25f;
    m_deleteAnimations.push_back(anim);
    
    // Core deletion: remove from PlaylistModel
    playlist.removeClip(clipId);
    
    // FL-style transport behavior: if we just cleared the last clip while playing,
    // snap back to bar 1.
    if (m_trackManager->isPlaying()) {
        if (playlist.getTotalDurationBeats() <= 1e-6) {
            m_trackManager->setPosition(0.0);
        }
    }
    
    refreshTracks();
    invalidateCache();
    scheduleTimelineMinimapRebuild();
    
    Log::info("[TrackManagerUI] Clip deleted via PlaylistModel: " + clipId.toString());
}



void TrackManagerUI::onSplitRequested(TrackUIComponent* trackComp, double splitBeat) {
    if (!trackComp || !m_trackManager) return;
    
    // Find which clip is at this beat position on this lane
    auto& playlist = m_trackManager->getPlaylistModel();
    PlaylistLaneID laneId = trackComp->getLaneId();
    auto lane = playlist.getLane(laneId);
    if (!lane) return;
    
    ClipInstanceID targetClipId;
    for (const auto& clip : lane->clips) {
        if (splitBeat > clip.startBeat && splitBeat < clip.startBeat + clip.durationBeats) {
            targetClipId = clip.id;
            break;
        }
    }
    
    if (targetClipId.isValid()) {
        playlist.splitClip(targetClipId, splitBeat);
        refreshTracks();
        invalidateCache();
        scheduleTimelineMinimapRebuild();
        Log::info("[TrackManagerUI] Clip split via PlaylistModel at beat " + std::to_string(splitBeat));
    }
}


void TrackManagerUI::setPlaylistVisible(bool visible) {
    m_playlistVisible = visible;
    layoutTracks();
    setDirty(true);
}

void TrackManagerUI::onAddTrackClicked() {
    addTrack(); // Add track with auto-generated name
}

void TrackManagerUI::layoutTracks() {
    NomadUI::NUIRect bounds = getBounds();

    // Get layout dimensions from theme
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();

    float headerHeight = 40.0f;
    float scrollbarWidth = 15.0f;
    float horizontalScrollbarHeight = 24.0f;
    float rulerHeight = 28.0f;
    
    float viewportHeight = bounds.height - headerHeight - horizontalScrollbarHeight - rulerHeight;
    
    // In v3.1, panels are floating overlays and do not affect workspace viewport directly.
    // If we wanted docking, we'd subtract their space here based on external state pointers.
    
    // Layout timeline minimap (top, right after header, before ruler)
    if (m_timelineMinimap) {
        float minimapWidth = bounds.width - scrollbarWidth;
        float minimapY = headerHeight;
        m_timelineMinimap->setBounds(NomadUI::NUIAbsolute(bounds, 0, minimapY, minimapWidth, horizontalScrollbarHeight));
        updateTimelineMinimap(0.0);
    }
    
    // Layout vertical scrollbar (right side, below header, horizontal scrollbar, and ruler)
    if (m_scrollbar) {
        float scrollbarY = headerHeight + horizontalScrollbarHeight + rulerHeight;
        float scrollbarX = bounds.width - scrollbarWidth;
        m_scrollbar->setBounds(NomadUI::NUIAbsolute(bounds, scrollbarX, scrollbarY, scrollbarWidth, viewportHeight));
        updateScrollbar();
    }

    float controlAreaWidth = layout.trackControlsWidth;
    float gridStartX = bounds.x + controlAreaWidth + 5.0f;
    float trackAreaTop = bounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;

    // === V3.0 LANE LAYOUT (Two-Rect Model) ===
    for (size_t i = 0; i < m_trackUIComponents.size(); ++i) {
        auto trackUI = m_trackUIComponents[i];
        if (!trackUI) continue;
        
        float yPos = trackAreaTop + (i * (m_trackHeight + m_trackSpacing)) - m_scrollOffset;
        
        // Fix: Use absolute coordinates (bounds.x, yPos).
        // NomadUI components use absolute screen coordinates.
        float trackWidth = bounds.width - scrollbarWidth - 5.0f;
        trackUI->setBounds(bounds.x, yPos, trackWidth, m_trackHeight);
        trackUI->setVisible(m_playlistVisible);
        
        // Zebra Striping: Ensure index is set during layout (critical for refresh persistence)
        trackUI->setRowIndex(static_cast<int>(i));
    }
    
    // Panels (Mixer, Piano Roll, Sequencer) now live in OverlayLayer 
    // and handle their own layout reacting to visibility changes.
}

void TrackManagerUI::updateTrackPositions() {
    layoutTracks();
}

void TrackManagerUI::onRender(NomadUI::NUIRenderer& renderer) {
    rmt_ScopedCPUSample(TrackMgrUI_Render, 0);
    
    NomadUI::NUIRect bounds = getBounds();
    
    // Normal rendering with FBO CACHING for massive FPS boost! 🚀
    // Cache the entire playlist view except the playhead (which moves every frame)
    
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    
    auto* renderCache = renderer.getRenderCache();
    if (!renderCache) {
        // Fallback: No cache available, render normally
        renderTrackManagerDirect(renderer);
        return;
    }
    
    // === FBO CACHING ENABLED ===
    // Get or create FBO cache (cache entire playlist view area)
    NomadUI::NUISize cacheSize(
        static_cast<int>(bounds.width),
        static_cast<int>(bounds.height)
    );
    m_cachedRender = renderCache->getOrCreateCache(m_cacheId, cacheSize);
    
    // Check if we need to invalidate the cache
    if (m_cacheInvalidated && m_cachedRender) {
        renderCache->invalidate(m_cacheId);
        m_cacheInvalidated = false;
    }
    
    // Render using cache (auto-rebuild if invalid)
    if (m_cachedRender) {
        renderCache->renderCachedOrUpdate(m_cachedRender, bounds, [&]() {
            // Re-render playlist contents into the cache
            m_isRenderingToCache = true;
            
            renderer.clear(NomadUI::NUIColor(0, 0, 0, 0));
            renderer.pushTransform(-bounds.x, -bounds.y);
            renderTrackManagerDirect(renderer);
            renderer.popTransform();
            
            m_isRenderingToCache = false;
        });
    } else {
        renderTrackManagerDirect(renderer);
    }

    // Render the left control strip OUTSIDE the cache to keep M/S/R hover/press responsive
    // without forcing expensive cache invalidations on every mouse move.
    //
    // IMPORTANT: This pass must be clipped to the track viewport; otherwise partially-visible
    // tracks can draw "above" the viewport and bleed into the ruler/corner region.
    if (m_playlistVisible) {
        const float headerHeight = 38.0f;
        const float horizontalScrollbarHeight = 24.0f;
        const float rulerHeight = 28.0f;
        const float scrollbarWidth = 15.0f;

        // Since panels are overlays, we render the playlist underneath them.
        // If we want clipping to stop at panel borders, we'd need to subtract them here.
        // For v3.1 simplicity, we just fill the workspace and let overlays cover it.

        const float viewportTop = bounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;
        const float viewportHeight = std::max(0.0f, bounds.height - headerHeight - horizontalScrollbarHeight - rulerHeight);
        const float trackWidth = std::max(0.0f, bounds.width - scrollbarWidth);
        const NomadUI::NUIRect viewportClip(bounds.x, viewportTop, trackWidth, viewportHeight);

        bool clipEnabled = false;
        if (!viewportClip.isEmpty()) {
            renderer.setClipRect(viewportClip);
            clipEnabled = true;
        }

        const float viewportBottom = viewportTop + viewportHeight;
        for (const auto& trackUI : m_trackUIComponents) {
            if (!trackUI || !trackUI->isVisible() || !trackUI->isPrimaryForLane()) continue;
            const auto trackBounds = trackUI->getBounds();
            if (trackBounds.bottom() < viewportTop || trackBounds.y > viewportBottom) continue;
            trackUI->renderControlOverlay(renderer);
        }

        if (clipEnabled) {
            renderer.clearClipRect();
        }
    }
    
    // Render playhead OUTSIDE cache (it moves every frame during playback)
    renderPlayhead(renderer);
    
    // Render drop preview OUTSIDE cache (dynamic during drag)
    if (m_showDropPreview) {
        renderDropPreview(renderer);
    }
    
    // Render delete animations OUTSIDE cache (FL Studio ripple effect)
    renderDeleteAnimations(renderer);
    
    // Render scrollbars OUTSIDE cache (they interact with mouse)
    if (m_timelineMinimap && m_timelineMinimap->isVisible()) m_timelineMinimap->onRender(renderer);
    if (m_scrollbar && m_scrollbar->isVisible()) m_scrollbar->onRender(renderer);
    
    // Panels are now handled by OverlayLayer rendering.
    
    // Render toolbar OUTSIDE cache (interactive tool selection)
    renderToolbar(renderer);
    
    // Render split cursor if split tool is active (follows mouse)
    if (m_currentTool == PlaylistTool::Split) {
        renderSplitCursor(renderer, m_lastMousePos);
    }

    // Minimap edge-resize cursor (custom overlay).
    renderMinimapResizeCursor(renderer, m_lastMousePos);
    
    // Render selection box if currently drawing one
    if (m_isDrawingSelectionBox) {
        float minX = std::min(m_selectionBoxStart.x, m_selectionBoxEnd.x);
        float maxX = std::max(m_selectionBoxStart.x, m_selectionBoxEnd.x);
        float minY = std::min(m_selectionBoxStart.y, m_selectionBoxEnd.y);
        float maxY = std::max(m_selectionBoxStart.y, m_selectionBoxEnd.y);
        
        NomadUI::NUIRect selectionRect(minX, minY, maxX - minX, maxY - minY);

        // CLIPPING: Constrain selection to grid area (ignore headers/rulers)
        auto& themeManager = NomadUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        
        float headerHeight = 38.0f;
        float rulerHeight = 28.0f;
        float horizontalScrollbarHeight = 24.0f;
        float controlAreaWidth = layout.trackControlsWidth;
        float scrollbarWidth = 15.0f;
        
        float gridTop = getBounds().y + headerHeight + rulerHeight + horizontalScrollbarHeight;
        float gridLeft = getBounds().x + controlAreaWidth + 5.0f; // +5 margin
        float gridWidth = getBounds().width - (controlAreaWidth + 5.0f) - scrollbarWidth;
        float gridHeight = getBounds().height - (headerHeight + rulerHeight + horizontalScrollbarHeight);

        NomadUI::NUIRect gridBounds(gridLeft, gridTop, gridWidth, gridHeight);
        
        // Intersect selection with grid - if no intersection, don't draw
        if (gridBounds.intersects(selectionRect)) {
            // Clip the rect
            float clipX = std::max(selectionRect.x, gridBounds.x);
            float clipY = std::max(selectionRect.y, gridBounds.y);
            float clipR = std::min(selectionRect.right(), gridBounds.right());
            float clipB = std::min(selectionRect.bottom(), gridBounds.bottom());
            
            NomadUI::NUIRect clippedRect(clipX, clipY, clipR - clipX, clipB - clipY);

            // "Glass Tech" Theme Style
            NomadUI::NUIColor accent = themeManager.getColor("accentCyan");
            
            // 1. Vertical Gradient Fill for "Glass" depth
            // Top: Almost transparent (5/255 ~= 0.02)
            // Bottom: Slightly more visible (30/255 ~= 0.12)
            NomadUI::NUIColor fillTop = accent.withAlpha(5.0f / 255.0f);
            NomadUI::NUIColor fillBottom = accent.withAlpha(30.0f / 255.0f);
            renderer.fillRectGradient(clippedRect, fillTop, fillBottom, true /* vertical */);
            
            // 2. Main Border: Solid, sharp line (200/255 ~= 0.78)
            NomadUI::NUIColor borderColor = accent.withAlpha(200.0f / 255.0f);
            renderer.strokeRect(clippedRect, 1.0f, borderColor);
            
            // 3. Tech Corner Accents (Solid 255/255 = 1.0) - Gives precision feel
            // Only draw corners if they weren't clipped away (simplified logic: just draw at clipped corners)
            NomadUI::NUIColor cornerColor = accent.withAlpha(1.0f);
            float cornerLen = 6.0f;
            float cornerThick = 2.0f;
            
            // Top-Left
            renderer.fillRect(NomadUI::NUIRect(clipX, clipY, cornerLen, cornerThick), cornerColor);
            renderer.fillRect(NomadUI::NUIRect(clipX, clipY, cornerThick, cornerLen), cornerColor);
            
            // Top-Right
            renderer.fillRect(NomadUI::NUIRect(clipR - cornerLen, clipY, cornerLen, cornerThick), cornerColor);
            renderer.fillRect(NomadUI::NUIRect(clipR - cornerThick, clipY, cornerThick, cornerLen), cornerColor);
            
            // Bottom-Left
            renderer.fillRect(NomadUI::NUIRect(clipX, clipB - cornerThick, cornerLen, cornerThick), cornerColor);
            renderer.fillRect(NomadUI::NUIRect(clipX, clipB - cornerLen, cornerThick, cornerLen), cornerColor);
            
            // Bottom-Right
            renderer.fillRect(NomadUI::NUIRect(clipR - cornerLen, clipB - cornerThick, cornerLen, cornerThick), cornerColor);
            renderer.fillRect(NomadUI::NUIRect(clipR - cornerThick, clipB - cornerLen, cornerThick, cornerLen), cornerColor);
        }
    }
}

// Helper method: Direct rendering (used both for fallback and cache rebuild)
void TrackManagerUI::renderTrackManagerDirect(NomadUI::NUIRenderer& renderer) {
    NomadUI::NUIRect bounds = getBounds();
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    
    // Calculate where the grid/background should end
    
    // Calculate where the grid/background should end
    float controlAreaWidth = layout.trackControlsWidth;
    float gridStartX = controlAreaWidth + 5;
    
    // Draw background (control area + full grid area - no bounds restriction)
    NomadUI::NUIColor bgColor = themeManager.getColor("backgroundPrimary");
    
	    if (m_playlistVisible) {
	        // Background for control area (always visible)
	        NomadUI::NUIRect controlBg(bounds.x, bounds.y, controlAreaWidth, bounds.height);
	        renderer.fillRect(controlBg, bgColor);
	        
	        // Background for grid area (match track background; zebra grid provides contrast)
	        float scrollbarWidth = 15.0f;
	        float gridWidth = bounds.width - controlAreaWidth - scrollbarWidth - 5;
	        NomadUI::NUIRect gridBg(bounds.x + gridStartX, bounds.y, gridWidth, bounds.height);
	        renderer.fillRect(gridBg, bgColor);

	        // Draw border
	        NomadUI::NUIColor borderColor = themeManager.getColor("border");
	        renderer.strokeRect(bounds, 1, borderColor);
	    }
    
    // Minimap is rendered outside the playlist cache; keep it updated in layout/update paths.

    // Calculate available width for header elements
    float headerAvailableWidth = bounds.width;
    
	    // Draw track count - positioned in top-right corner of available space with proper margin
	    if (m_playlistVisible) {
	        std::string infoText = "Tracks: " + std::to_string(m_trackManager ? m_trackManager->getTrackCount() - (m_trackManager->getTrackCount() > 0 ? 1 : 0) : 0);  // Exclude preview track
	        const float infoFont = 12.0f;
	        auto infoSize = renderer.measureText(infoText, infoFont);

        // Ensure text doesn't exceed available width and position with proper margin
        float margin = layout.panelMargin;
        float maxTextWidth = headerAvailableWidth - 2 * margin;
        if (infoSize.width > maxTextWidth) {
            // Truncate if too long
            std::string truncatedText = infoText;
            while (!truncatedText.empty() && renderer.measureText(truncatedText, infoFont).width > maxTextWidth) {
                truncatedText = truncatedText.substr(0, truncatedText.length() - 1);
            }
            infoText = truncatedText + "...";
            infoSize = renderer.measureText(infoText, infoFont);
        }

        const float headerHeight = 38.0f;
        const NomadUI::NUIRect headerBounds(bounds.x, bounds.y, headerAvailableWidth, headerHeight);
	        // Slightly larger inset keeps the label safely inside the header bounds.
	        const float rightPad = layout.panelMargin + 18.0f;
	        const float textX = std::max(headerBounds.x + margin, headerBounds.right() - infoSize.width - rightPad);
	        const float textY = std::round(renderer.calculateTextY(headerBounds, infoFont));

	        renderer.drawText(infoText, NomadUI::NUIPoint(textX, textY), infoFont, themeManager.getColor("textSecondary"));
	    }

    // Custom render order: tracks first, then UI controls on top
    // (Grid is now drawn by individual tracks in TrackUIComponent::drawPlaylistGrid)
    renderChildren(renderer);
    
    // === GRID SELECTION HIGHLIGHT (extends ruler selection into track area) ===
    if ((m_isDraggingRulerSelection || m_hasRulerSelection) && m_playlistVisible) {
        double selStartBeat = std::min(m_rulerSelectionStartBeat, m_rulerSelectionEndBeat);
        double selEndBeat = std::max(m_rulerSelectionStartBeat, m_rulerSelectionEndBeat);
        
        auto& themeManager = NomadUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        float controlAreaWidth = layout.trackControlsWidth;
        float gridStartX = bounds.x + controlAreaWidth + 5.0f;
        
        // Convert beats to pixel positions
        float selStartX = gridStartX + static_cast<float>(selStartBeat * m_pixelsPerBeat) - m_timelineScrollOffset;
        float selEndX = gridStartX + static_cast<float>(selEndBeat * m_pixelsPerBeat) - m_timelineScrollOffset;
        
        // Calculate grid area bounds
        float headerHeight = 38.0f;
        float rulerHeight = 28.0f;
        float horizontalScrollbarHeight = 24.0f;
        float trackAreaTop = bounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;
        float trackAreaHeight = bounds.height - (headerHeight + horizontalScrollbarHeight + rulerHeight);
        
        float scrollbarWidth = 15.0f;
        float gridWidth = bounds.width - controlAreaWidth - scrollbarWidth - 5.0f;
        float gridEndX = gridStartX + gridWidth;
        
        // Only draw if visible in grid area
        if (selEndX >= gridStartX && selStartX <= gridEndX) {
            // Clamp to visible area
            float visibleStartX = std::max(selStartX, gridStartX);
            float visibleEndX = std::min(selEndX, gridEndX);
            float selectionWidth = visibleEndX - visibleStartX;
            
            if (selectionWidth > 0.0f) {
                NomadUI::NUIRect selectionRect(visibleStartX, trackAreaTop, selectionWidth, trackAreaHeight);
                
                // Fill with semi-transparent accent color (even more subtle than ruler - 10% alpha)
                auto accentColor = themeManager.getColor("accentPrimary");
                renderer.fillRect(selectionRect, accentColor.withAlpha(0.10f));
                
                // Draw subtle vertical lines at selection edges (slightly more visible - 30% alpha)
                if (selStartX >= gridStartX && selStartX <= gridEndX) {
                    renderer.drawLine(
                        NomadUI::NUIPoint(selStartX, trackAreaTop),
                        NomadUI::NUIPoint(selStartX, trackAreaTop + trackAreaHeight),
                        1.0f,
                        accentColor.withAlpha(0.30f)
                    );
                }
                if (selEndX >= gridStartX && selEndX <= gridEndX) {
                    renderer.drawLine(
                        NomadUI::NUIPoint(selEndX, trackAreaTop),
                        NomadUI::NUIPoint(selEndX, trackAreaTop + trackAreaHeight),
                        1.0f,
                        accentColor.withAlpha(0.30f)
                    );
                }
            }
        }
    }
    
    // Calculate available width for header
    float headerWidth = bounds.width;
    
    // Draw header bar on top of everything (docked playlist header strip)
    if (m_playlistVisible) {
        NomadUI::NUIColor bgColor = themeManager.getColor("backgroundPrimary");
        NomadUI::NUIColor borderColor = themeManager.getColor("border");

        float headerHeight = 38.0f;
        NomadUI::NUIRect headerRect(bounds.x, bounds.y, headerWidth, headerHeight);
        renderer.fillRect(headerRect, bgColor);
        renderer.strokeRect(headerRect, 1, borderColor);
        
        // Draw time ruler below header and horizontal scrollbar (20px tall)
        float rulerHeight = 28.0f;
        float horizontalScrollbarHeight = 24.0f;
        NomadUI::NUIRect rulerRect(bounds.x, bounds.y + headerHeight + horizontalScrollbarHeight, headerWidth, rulerHeight);
        renderTimeRuler(renderer, rulerRect);
        renderLoopMarkers(renderer, rulerRect);  // Draw FL Studio-style loop markers
        
    }
}

void TrackManagerUI::renderChildren(NomadUI::NUIRenderer& renderer) {
    // ðŸ”¥ VIEWPORT CULLING: Only render visible tracks + always render controls
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    NomadUI::NUIRect bounds = getBounds();
    
    const float headerHeight = 38.0f;
    const float rulerHeight = 28.0f;
    const float horizontalScrollbarHeight = 24.0f;
    const float scrollbarWidth = 15.0f;

    const float viewportHeight = std::max(0.0f, bounds.height - headerHeight - horizontalScrollbarHeight - rulerHeight);
    const float viewportTopAbs = bounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;
    const float viewportBottomAbs = viewportTopAbs + viewportHeight;
    const float trackWidth = std::max(0.0f, bounds.width - scrollbarWidth);

    NomadUI::NUIRect viewportClip(bounds.x, viewportTopAbs, trackWidth, viewportHeight);
    if (m_isRenderingToCache) {
        viewportClip.x -= bounds.x;
        viewportClip.y -= bounds.y;
    }

    bool clipEnabled = false;
    if (m_playlistVisible && !viewportClip.isEmpty()) {
        renderer.setClipRect(viewportClip);
        clipEnabled = true;
    }
    
    // Render all children but skip track UIComponents that are outside viewport
    const auto& children = getChildren();
    for (const auto& child : children) {
        if (!child->isVisible()) continue;
        
        // Always render UI controls (scrollbars)
        if (child == m_scrollbar || child == m_timelineMinimap) {
            // Skip - these are rendered explicitly in onRender()
            continue;
        }
        
        // Track UI components: cull by bounds (robust even with lane-grouping / hidden secondaries).
        bool isTrackUI = false;
        for (const auto& trackUI : m_trackUIComponents) {
            if (child == trackUI) {
                isTrackUI = true;
                break;
            }
        }

        if (isTrackUI) {
            if (!m_playlistVisible) continue;
            const auto trackBounds = child->getBounds();
            if (trackBounds.bottom() < viewportTopAbs || trackBounds.y > viewportBottomAbs) continue;
            child->onRender(renderer);
            continue;
        }

        // Not a track UI, render normally (other UI elements)
        child->onRender(renderer);
    }

    if (clipEnabled) {
        renderer.clearClipRect();
    }
}

void TrackManagerUI::onUpdate(double deltaTime) {
    // One-time registration for drag-and-drop
    // We do this here because shared_from_this() is not available in the constructor
    if (!m_dropTargetRegistered) {
        try {
            // Ensure we are managed by a shared_ptr before calling shared_from_this()
            auto sharedThis = std::dynamic_pointer_cast<NomadUI::IDropTarget>(shared_from_this());
            if (sharedThis) {
                NomadUI::NUIDragDropManager::getInstance().registerDropTarget(sharedThis);
                m_dropTargetRegistered = true;
            }
        } catch (const std::bad_weak_ptr&) {
            // Object might be stack-allocated or not yet managed by shared_ptr
            // We'll try again next frame or fail silently if it never happens
        }
    }

    NUIComponent::onUpdate(deltaTime);
    
    // Smooth zoom animation (FL Studio style)
    if (std::abs(m_targetPixelsPerBeat - m_pixelsPerBeat) > 0.01f) {
        // Get control area width for zoom pivot calculation
        auto& themeManager = NomadUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        float controlAreaWidth = layout.trackControlsWidth;
        float gridStartX = controlAreaWidth + 5;
        
        // Calculate world position under the zoom pivot point
        float worldUnderMouse = (m_lastMouseZoomX - gridStartX) + m_timelineScrollOffset;
        float beatUnderMouse = worldUnderMouse / m_pixelsPerBeat;
        
        // Smooth interpolation toward target zoom
        float lerpSpeed = 12.0f;
        float t = std::min(1.0f, static_cast<float>(deltaTime * lerpSpeed));
        float oldZoom = m_pixelsPerBeat;
        m_pixelsPerBeat = oldZoom + (m_targetPixelsPerBeat - oldZoom) * t;
        
        // Keep the beat under the mouse at the same screen position
        float newWorldUnderMouse = beatUnderMouse * m_pixelsPerBeat;
        m_timelineScrollOffset = std::max(0.0f, newWorldUnderMouse - (m_lastMouseZoomX - gridStartX));
        
        // Sync to all tracks
        for (auto& trackUI : m_trackUIComponents) {
            trackUI->setPixelsPerBeat(m_pixelsPerBeat);
            trackUI->setTimelineScrollOffset(m_timelineScrollOffset);
        }
        
        m_cacheInvalidated = true;
        setDirty(true);
    }

    updateTimelineMinimap(deltaTime);
}

void TrackManagerUI::onResize(int width, int height) {
    // Update cached dimensions before layout/cache update
    m_backgroundCachedWidth = width;
    m_backgroundCachedHeight = height;
    m_backgroundNeedsUpdate = true;
    m_cacheInvalidated = true;  // Invalidate FBO cache on resize
    
    layoutTracks();
    // Zebra Striping: Assign row index to tracks
    for (size_t i = 0; i < m_trackUIComponents.size(); ++i) {
        if (m_trackUIComponents[i]) {
            m_trackUIComponents[i]->setRowIndex(static_cast<int>(i));
        }
    }
    NomadUI::NUIComponent::onResize(width, height);
}

bool TrackManagerUI::onMouseEvent(const NomadUI::NUIMouseEvent& event) {
    NomadUI::NUIRect bounds = getBounds();
    NomadUI::NUIPoint localPos(event.position.x - bounds.x, event.position.y - bounds.y);
    
    // Track mouse position for split cursor rendering
    m_lastMousePos = event.position;

    // Fix for "Sticky Drag": Route events to any track that is currently dragging automation
    // regardless of whether the mouse is inside its bounds.
    for (auto& track : m_trackUIComponents) {
        if (!track || !track->isVisible()) continue;
        
        // If track is in the middle of an automation drag operation, force-feed it the event
        if (track->isDraggingAutomation()) { 
             // Pass event with global coordinates since TrackUIComponent expects global coords
             if (track->onMouseEvent(event)) return true;
        }
    }

    // Claim keyboard focus on click so keyboard routing moves off the file browser.
    if (event.pressed && event.button == NomadUI::NUIMouseButton::Left && bounds.contains(event.position)) {
        setFocused(true);
    }
    
    // Update toolbar bounds before checking hover (critical!)
    updateToolbarBounds();
    
    // Update toolbar hover states
    bool oldAddHovered = m_addTrackHovered;
    bool oldSelectHovered = m_selectToolHovered;
    bool oldSplitHovered = m_splitToolHovered;
    bool oldMultiSelectHovered = m_multiSelectToolHovered;
    
    m_addTrackHovered = m_addTrackBounds.contains(event.position);
    m_selectToolHovered = m_selectToolBounds.contains(event.position);
    m_splitToolHovered = m_splitToolBounds.contains(event.position);
    m_multiSelectToolHovered = m_multiSelectToolBounds.contains(event.position);
    // Loop dropdown handles its own hover state
    
    // Toolbar is rendered outside the playlist cache; don't invalidate the cache on hover.
    if (m_addTrackHovered != oldAddHovered ||
        m_selectToolHovered != oldSelectHovered || m_splitToolHovered != oldSplitHovered ||
        m_multiSelectToolHovered != oldMultiSelectHovered) {
        setDirty(true);
    }
    
    // === DROPDOWNS FIRST: Give dropdowns absolute priority over other UI ===
    // Loop dropdown - check if it's open or if click is within its bounds
    if (m_loopDropdown) {
        // If dropdown is open, forward ALL events to it (to handle outside clicks and selection)
        if (m_loopDropdown->isOpen()) {
            if (m_loopDropdown->onMouseEvent(event)) {
                return true;
            }
        }
        // If click is on the dropdown button area, forward the event
        if (m_loopDropdownBounds.contains(event.position)) {
            if (m_loopDropdown->onMouseEvent(event)) {
                return true;
            }
        }
    }
    
    // Snap dropdown
    if (m_snapDropdown) {
        if (m_snapDropdown->isOpen()) {
            if (m_snapDropdown->onMouseEvent(event)) {
                return true;
            }
        }
        if (m_snapDropdownBounds.contains(event.position)) {
            if (m_snapDropdown->onMouseEvent(event)) {
                return true;
            }
        }
    }
    
    // Handle toolbar clicks (icons only, not dropdowns - they handled themselves above)
    if (event.pressed && event.button == NomadUI::NUIMouseButton::Left) {
        if (handleToolbarClick(event.position)) {
            return true;
        }
    }

    // In v3.1, overlays are handled by OverlayLayer::onMouseEvent.
    // TrackManagerUI only handles clicks that reach the workspace.

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
        return NomadUI::NUIComponent::onMouseEvent(event);
    }
    
    // Handle instant clip dragging
    if (m_isDraggingClipInstant) {
        if (event.released && event.button == NomadUI::NUIMouseButton::Left) {
            finishInstantClipDrag();
            return true;
        }
        updateInstantClipDrag(event.position);
        return true;
    }

    // Allow children (clips) to handle right-click press first.
    // FL Studio style: right-click on a clip deletes it; only start selection
    // box if nothing underneath handled the event.
    if (event.pressed && event.button == NomadUI::NUIMouseButton::Right) {
        if (NomadUI::NUIComponent::onMouseEvent(event)) {
            return true;
        }
    }
    
    // === SELECTION BOX: Right-click drag or MultiSelect tool or Ctrl+LeftClick ===
    // Start selection box on right-click drag OR left-click with MultiSelect tool OR Ctrl+LeftClick
    bool ctrlHeld = (event.modifiers & NomadUI::NUIModifiers::Ctrl);
    bool startSelectionBox = (event.pressed && event.button == NomadUI::NUIMouseButton::Right) ||
                             (event.pressed && event.button == NomadUI::NUIMouseButton::Left && 
                              (m_currentTool == PlaylistTool::MultiSelect || ctrlHeld));
    
    if (startSelectionBox && !m_isDrawingSelectionBox) {
        float headerHeight = 38.0f;
        float rulerHeight = 28.0f;
        float horizontalScrollbarHeight = 24.0f;
        float trackAreaTop = headerHeight + horizontalScrollbarHeight + rulerHeight;
        
        // Only start selection box in track area
        if (localPos.y > trackAreaTop) {
            m_isDrawingSelectionBox = true;
            m_selectionBoxStart = event.position;
            m_selectionBoxEnd = event.position;
            
            // Hide cursor for "flush feeling" during drag
            if (m_window) {
                m_window->setCursorVisible(false);
            }
            return true;
        }
    }
    
    // Update selection box while dragging
    if (m_isDrawingSelectionBox) {
        if (m_window) {
            // Calculate constrained cursor position
            auto& themeManager = NomadUI::NUIThemeManager::getInstance();
            const auto& layout = themeManager.getLayoutDimensions();
            
            float headerHeight = 38.0f;
            float rulerHeight = 28.0f;
            float horizontalScrollbarHeight = 24.0f;
            float controlAreaWidth = layout.trackControlsWidth;
            float scrollbarWidth = 15.0f;
            
            NomadUI::NUIRect globalBounds = getBounds();
            
            int winX, winY;
            m_window->getPosition(winX, winY);
            
            float gridTopLocal = globalBounds.y + headerHeight + rulerHeight + horizontalScrollbarHeight;
            float gridLeftLocal = globalBounds.x + controlAreaWidth + 5.0f; 
            float gridRightLocal = globalBounds.x + globalBounds.width - scrollbarWidth; // Corrected width calc
            float gridBottomLocal = globalBounds.y + globalBounds.height; // Full height down
            
            // Clamp event position (window-local) to grid area
            float targetX = std::clamp(event.position.x, gridLeftLocal, gridRightLocal);
            float targetY = std::clamp(event.position.y, gridTopLocal, gridBottomLocal);
            
            // Apply bounds to internal selection logic
            m_selectionBoxEnd = {targetX, targetY};

            // Force physical cursor to match clamped position
            // Add window offset to get screen coordinates
            m_window->setCursorPosition(winX + (int)targetX, winY + (int)targetY);
        } else {
             m_selectionBoxEnd = event.position; 
        }
        
        // Check for release to finalize selection
        // Allow release of Left button even if tool isn't MultiSelect (e.g. Ctrl override case)
        bool endSelectionBox = (event.released && event.button == NomadUI::NUIMouseButton::Right) ||
                               (event.released && event.button == NomadUI::NUIMouseButton::Left);
        
        if (endSelectionBox) {
            // Calculate selection rectangle
            float minX = std::min(m_selectionBoxStart.x, m_selectionBoxEnd.x);
            float maxX = std::max(m_selectionBoxStart.x, m_selectionBoxEnd.x);
            float minY = std::min(m_selectionBoxStart.y, m_selectionBoxEnd.y);
            float maxY = std::max(m_selectionBoxStart.y, m_selectionBoxEnd.y);
            
            NomadUI::NUIRect selectionRect(minX, minY, maxX - minX, maxY - minY);
            
            // Select all tracks that intersect with selection box
            clearSelection();
            for (auto& trackUI : m_trackUIComponents) {
                if (trackUI->getBounds().intersects(selectionRect)) {
                    selectTrack(trackUI.get(), true);
                }
            }
            
            // Restore cursor visibility
            if (m_window) {
                m_window->setCursorVisible(true);
            }
            
            m_isDrawingSelectionBox = false;
            m_cacheInvalidated = true;
            
            Log::info("Selection box completed, selected " + std::to_string(m_selectedTracks.size()) + " tracks");
        }
        
        m_cacheInvalidated = true;
        return true;
    }
    
    // Layout constants
    float headerHeight = 38.0f;
    float rulerHeight = 28.0f;
    float horizontalScrollbarHeight = 24.0f;
    NomadUI::NUIRect rulerRect(0, headerHeight + horizontalScrollbarHeight, bounds.width, rulerHeight);
    
    // Track area (below ruler)
    float trackAreaTop = headerHeight + horizontalScrollbarHeight + rulerHeight;
    NomadUI::NUIRect trackArea(0, trackAreaTop, bounds.width, bounds.height - trackAreaTop);
    
    bool isInRuler = rulerRect.contains(localPos);
    bool isInTrackArea = trackArea.contains(localPos);
    
    // Mouse wheel handling
    if (event.wheelDelta != 0.0f && (isInRuler || isInTrackArea)) {
        // Check for Shift modifier - Shift+scroll = ZOOM
        bool shiftHeld = (event.modifiers & NomadUI::NUIModifiers::Shift);
        
        if (shiftHeld || isInRuler) {
            // ZOOM: Shift+scroll anywhere OR scroll on ruler
            m_lastMouseZoomX = localPos.x;
            
            // FL Studio style exponential zoom
            float zoomMultiplier = event.wheelDelta > 0 ? 1.15f : 0.87f;
            m_targetPixelsPerBeat = std::clamp(m_targetPixelsPerBeat * zoomMultiplier, 8.0f, 300.0f);
            
            for (auto& trackUI : m_trackUIComponents) {
                trackUI->setBeatsPerBar(m_beatsPerBar);
            }
            
            m_cacheInvalidated = true;
            setDirty(true);
            return true;
        } else {
            // VERTICAL SCROLL: Regular scroll in track area (no shift)
            float scrollSpeed = 60.0f;
            float scrollDelta = -event.wheelDelta * scrollSpeed; // Invert for natural scroll direction
            
            m_scrollOffset += scrollDelta;
            
            // Clamp scroll offset
            float viewportHeight = bounds.height - headerHeight - rulerHeight - horizontalScrollbarHeight;

            const float laneCount = static_cast<float>(m_trackUIComponents.size());
            float totalContentHeight = laneCount * (m_trackHeight + m_trackSpacing);
            float maxScroll = std::max(0.0f, totalContentHeight - viewportHeight);
            m_scrollOffset = std::max(0.0f, std::min(m_scrollOffset, maxScroll));
            
            if (m_scrollbar) {
                m_scrollbar->setCurrentRange(m_scrollOffset, viewportHeight);
            }
            
            layoutTracks();
            m_cacheInvalidated = true;
            return true;
        }
    }
    // === RULER INTERACTION: Loop markers, Playhead scrubbing OR timeline selection ===
    if (isInRuler) {
        auto& themeManager = NomadUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        float controlAreaWidth = layout.trackControlsWidth;
        float gridStartX = controlAreaWidth + 5;
        
        // === LOOP MARKER INTERACTION (highest priority) ===
        if (m_hasRulerSelection) {
            // Calculate marker positions
            float loopStartX = gridStartX + (static_cast<float>(m_loopStartBeat) * m_pixelsPerBeat) - m_timelineScrollOffset;
            float loopEndX = gridStartX + (static_cast<float>(m_loopEndBeat) * m_pixelsPerBeat) - m_timelineScrollOffset;
            
            const float hitZone = 12.0f;  // Hit zone around markers
            bool nearLoopStart = std::abs(localPos.x - loopStartX) < hitZone;
            bool nearLoopEnd = std::abs(localPos.x - loopEndX) < hitZone;
            
            // Update hover states
            bool wasHoveringStart = m_hoveringLoopStart;
            bool wasHoveringEnd = m_hoveringLoopEnd;
            m_hoveringLoopStart = nearLoopStart;
            m_hoveringLoopEnd = nearLoopEnd;
            
            if (wasHoveringStart != m_hoveringLoopStart || wasHoveringEnd != m_hoveringLoopEnd) {
                m_cacheInvalidated = true;
            }
            
            // Start dragging loop marker
            if (event.pressed && event.button == NomadUI::NUIMouseButton::Left) {
                if (nearLoopStart) {
                    m_isDraggingLoopStart = true;
                    m_loopDragStartBeat = m_loopStartBeat;
                    return true;
                } else if (nearLoopEnd) {
                    m_isDraggingLoopEnd = true;
                    m_loopDragStartBeat = m_loopEndBeat;
                    return true;
                }
            }
        }
        
        // Right-click or Ctrl+Left-click starts ruler selection for looping
        bool isSelectionClick = (event.pressed && event.button == NomadUI::NUIMouseButton::Right) ||
                                (event.pressed && event.button == NomadUI::NUIMouseButton::Left && 
                                 (event.modifiers & NomadUI::NUIModifiers::Ctrl));
        
        // Regular left-click (without Ctrl) starts playhead scrubbing
        // BUT NOT if we're hovering over a loop marker!
        bool isPlayheadClick = event.pressed && event.button == NomadUI::NUIMouseButton::Left && 
                              !(event.modifiers & NomadUI::NUIModifiers::Ctrl) &&
                              !m_hoveringLoopStart && !m_hoveringLoopEnd;
        
        if (isSelectionClick) {
            // Start ruler selection
            m_isDraggingRulerSelection = true;
            
            float gridStartX = controlAreaWidth + 5;
            
            // Convert mouse position to beat
            float mouseX = localPos.x - gridStartX + m_timelineScrollOffset;
            double positionInBeats = mouseX / m_pixelsPerBeat;
            
            // Snap to grid
            positionInBeats = snapBeatToGrid(positionInBeats);
            positionInBeats = std::max(0.0, positionInBeats);
            
            m_rulerSelectionStartBeat = positionInBeats;
            m_rulerSelectionEndBeat = positionInBeats;
            m_hasRulerSelection = false; // Not confirmed until mouse moves/releases
            
            m_cacheInvalidated = true;
            return true;
        }
        else if (isPlayheadClick && !m_isDraggingRulerSelection) {
            // Start dragging playhead (existing behavior)
            // Don't start if we're already doing a ruler selection!
            m_isDraggingPlayhead = true;
            if (m_trackManager) {
                m_trackManager->setUserScrubbing(true);
            }
            return true;
        }
    }
    
    // Handle ruler selection dragging
    if (m_isDraggingRulerSelection) {
        auto& themeManager = NomadUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        float controlAreaWidth = layout.trackControlsWidth;
        float gridStartX = controlAreaWidth + 5;
        
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
        
        m_cacheInvalidated = true;
        
        // Stop dragging on mouse release
        if ((event.released && event.button == NomadUI::NUIMouseButton::Right) ||
            (event.released && event.button == NomadUI::NUIMouseButton::Left)) {
            m_isDraggingRulerSelection = false;
            
            // Only keep selection if it has a valid range
            if (m_hasRulerSelection) {
                // Don't update minimap selection - only use ruler/grid blue highlight
                
                // Update loop dropdown to "Selection" mode
                if (m_loopDropdown) {
                    m_loopDropdown->setSelectedIndex(5);  // Select "Selection" item
                }
                
                // Trigger loop preset to "Selection" mode
                if (m_onLoopPresetChanged) {
                    m_onLoopPresetChanged(5); // 5 = Selection preset
                }
                
                Log::info("[TrackManagerUI] Ruler selection: " + 
                         std::to_string(m_minimapSelectionBeatRange.start) + " to " + 
                         std::to_string(m_minimapSelectionBeatRange.end) + " beats");
            } else {
                // Click without drag - clear selection and reset to 1 Bar
                m_hasRulerSelection = false;
                
                // Reset dropdown to "1 Bar"
                if (m_loopDropdown) {
                    m_loopDropdown->setSelectedIndex(1);
                }
                // Trigger 1-bar loop callback
                if (m_onLoopPresetChanged) {
                    m_onLoopPresetChanged(1);
                }
            }
            
            return true;
        }
        
        return true;
    }
    
    // Handle loop marker dragging
    if (m_isDraggingLoopStart || m_isDraggingLoopEnd) {
        auto& themeManager = NomadUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        float controlAreaWidth = layout.trackControlsWidth;
        float gridStartX = controlAreaWidth + 5;
        
        // Stop dragging on mouse release
        if (event.released && event.button == NomadUI::NUIMouseButton::Left) {
            m_isDraggingLoopStart = false;
            m_isDraggingLoopEnd = false;
            
            // Update audio engine loop region
            if (m_onLoopPresetChanged) {
                m_onLoopPresetChanged(5);  // Selection preset
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
                m_loopStartBeat = positionInBeats;
                m_rulerSelectionStartBeat = positionInBeats;
            }
        } else if (m_isDraggingLoopEnd) {
            // Don't allow end to go before start
            if (positionInBeats > m_loopStartBeat) {
                m_loopEndBeat = positionInBeats;
                m_rulerSelectionEndBeat = positionInBeats;
            }
        }
        
        // Update minimap selection range
        m_minimapSelectionBeatRange = {m_loopStartBeat, m_loopEndBeat};
        
        m_cacheInvalidated = true;
        return true;
    }
    
    // Handle playhead dragging (continuous scrub)
    // IMPORTANT: Don't handle playhead if we're doing ruler selection!
    if (m_isDraggingPlayhead && !m_isDraggingRulerSelection) {
        // Stop dragging on mouse release
        if (event.released && event.button == NomadUI::NUIMouseButton::Left) {
            m_isDraggingPlayhead = false;
            if (m_trackManager) {
                m_trackManager->setUserScrubbing(false);
            }
            return true;
        }
        
        // Update playhead position while dragging (even outside ruler bounds for smooth scrubbing)
        auto& themeManager = NomadUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        float controlAreaWidth = layout.trackControlsWidth;
        float gridStartX = controlAreaWidth + 5;
        
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
        }
        
        return true;
    }
    
    // (Vertical scroll handling moved to main wheel handler above)
    
    // First, let children handle the event
    bool handled = NomadUI::NUIComponent::onMouseEvent(event);
    if (handled) return true;

    // === SPLIT TOOL: Click to split track at position ===
    if (m_currentTool == PlaylistTool::Split && event.pressed && event.button == NomadUI::NUIMouseButton::Left) {
        // Check if click is in track area
        auto& themeManager = NomadUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        float controlAreaWidth = layout.trackControlsWidth;
        float gridStartX = controlAreaWidth + 5;
        
        float headerHeight = 38.0f;
        float rulerHeight = 28.0f;
        float horizontalScrollbarHeight = 24.0f;
        float trackAreaTop = headerHeight + horizontalScrollbarHeight + rulerHeight;
        
        NomadUI::NUIRect gridBounds(bounds.x + gridStartX, bounds.y + trackAreaTop,
                                     bounds.width - controlAreaWidth - 20.0f,
                                     bounds.height - trackAreaTop);
        
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
    
    return handled;
}

void TrackManagerUI::setPlaylistMode(PlaylistMode mode) {
    if (m_playlistMode != mode) {
        m_playlistMode = mode;
        
        // Propagate to all tracks
        for (auto& trackUI : m_trackUIComponents) {
            trackUI->setPlaylistMode(mode);
        }
        
        // Invalidate cache since rendering changes significantly
        invalidateCache();
        setDirty(true);
        
        Log::info("[TrackManagerUI] Mode changed to: " + 
                  std::string(mode == PlaylistMode::Clips ? "Clips" : "Automation"));
    }
}

bool TrackManagerUI::onKeyEvent(const NomadUI::NUIKeyEvent& event) {
    if (event.pressed) {
        // Hotkey 'A' toggles Automation Mode (FL/Ableton style)
        if (event.keyCode == NomadUI::NUIKeyCode::A && 
            !(event.modifiers & NomadUI::NUIModifiers::Ctrl)) {
            setPlaylistMode(m_playlistMode == PlaylistMode::Clips ? 
                            PlaylistMode::Automation : PlaylistMode::Clips);
            return true;
        }

        // Tool shortcuts
        if (event.keyCode == NomadUI::NUIKeyCode::Num1) { setCurrentTool(PlaylistTool::Select); return true; }
        if (event.keyCode == NomadUI::NUIKeyCode::Num2) { setCurrentTool(PlaylistTool::Split); return true; }
    }
    return false;
}

void TrackManagerUI::updateScrollbar() {
    if (!m_scrollbar) return;
    
    NomadUI::NUIRect bounds = getBounds();
    float headerHeight = 38.0f;
    float rulerHeight = 28.0f;
    float horizontalScrollbarHeight = 24.0f;

    // In v3.1, panels are floating overlays and do not affect the scrollbar's viewport directly.
    float viewportHeight = bounds.height - headerHeight - rulerHeight - horizontalScrollbarHeight;

    const float laneCount = static_cast<float>(m_trackUIComponents.size());
    float totalContentHeight = laneCount * (m_trackHeight + m_trackSpacing);
    
    // Set scrollbar range
    m_scrollbar->setRangeLimit(0, totalContentHeight);
    m_scrollbar->setCurrentRange(m_scrollOffset, viewportHeight);
    m_scrollbar->setAutoHide(totalContentHeight <= viewportHeight);
}

void TrackManagerUI::onScroll(double position) {
    m_scrollOffset = static_cast<float>(position);
    layoutTracks(); // Re-layout with new scroll offset
    invalidateCache();  // ⚡ Mark cache dirty
}

void TrackManagerUI::scheduleTimelineMinimapRebuild()
{
    m_minimapNeedsRebuild = true;
    m_minimapShrinkCooldown = 0.0;
}

float TrackManagerUI::getTimelineGridWidthPixels() const
{
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();

    const float controlAreaWidth = layout.trackControlsWidth;
    const float trackWidth = m_timelineMinimap ? m_timelineMinimap->getBounds().width : getBounds().width;
    float gridWidth = trackWidth - controlAreaWidth - 10.0f; // Match TrackUIComponent grid width
    return std::max(0.0f, gridWidth);
}

double TrackManagerUI::secondsToBeats(double seconds) const
{
    return m_trackManager->getPlaylistModel().secondsToBeats(seconds);
}

void TrackManagerUI::setTimelineViewStartBeat(double viewStartBeat, bool isFinal)
{
    const float gridWidthPx = getTimelineGridWidthPixels();
    if (!(m_pixelsPerBeat > 0.0f) || gridWidthPx <= 0.0f) return;

    const double viewWidthBeats = static_cast<double>(gridWidthPx / m_pixelsPerBeat);
    const double domainStart = m_minimapDomainStartBeat;
    const double domainEnd = std::max(m_minimapDomainEndBeat, domainStart + viewWidthBeats);
    const double maxStart = std::max(domainStart, domainEnd - viewWidthBeats);

    const double clampedStart = std::max(domainStart, std::min(viewStartBeat, maxStart));
    m_timelineScrollOffset = std::max(0.0f, static_cast<float>(clampedStart * static_cast<double>(m_pixelsPerBeat)));

    for (auto& trackUI : m_trackUIComponents) {
        trackUI->setTimelineScrollOffset(m_timelineScrollOffset);
    }

    invalidateCache();
    setDirty(true);

    if (!isFinal) {
        updateTimelineMinimap(0.0);
    }
}

void TrackManagerUI::resizeTimelineViewEdgeFromMinimap(NomadUI::TimelineMinimapResizeEdge edge, double anchorBeat,
                                                       double edgeBeat, bool isFinal)
{
    const float gridWidthPx = getTimelineGridWidthPixels();
    if (gridWidthPx <= 0.0f) return;

    constexpr float kMinPixelsPerBeat = 8.0f;
    constexpr float kMaxPixelsPerBeat = 300.0f;

    const double domainStart = m_minimapDomainStartBeat;
    const double domainEnd = std::max(m_minimapDomainEndBeat, domainStart + 1.0);

    const double minWidthBeats = static_cast<double>(gridWidthPx / kMaxPixelsPerBeat);
    const double maxWidthBeats = static_cast<double>(gridWidthPx / kMinPixelsPerBeat);

    const auto applyZoom = [this](float newPixelsPerBeat) {
        m_pixelsPerBeat = newPixelsPerBeat;
        m_targetPixelsPerBeat = newPixelsPerBeat;
        for (auto& trackUI : m_trackUIComponents) {
            trackUI->setPixelsPerBeat(m_pixelsPerBeat);
        }
    };

    if (edge == NomadUI::TimelineMinimapResizeEdge::Left) {
        // Dragging the left edge: keep right edge anchored.
        const double clampedEdge =
            std::max(domainStart, std::min(edgeBeat, anchorBeat - std::max(1e-6, minWidthBeats)));
        const double desiredWidth = std::max(minWidthBeats, std::min(maxWidthBeats, anchorBeat - clampedEdge));
        const float newPixelsPerBeat = std::clamp(static_cast<float>(gridWidthPx / desiredWidth), kMinPixelsPerBeat, kMaxPixelsPerBeat);
        applyZoom(newPixelsPerBeat);

        const double viewWidthBeats = static_cast<double>(gridWidthPx / m_pixelsPerBeat);
        const double viewStartBeat = anchorBeat - viewWidthBeats;
        setTimelineViewStartBeat(viewStartBeat, isFinal);
    } else {
        // Dragging the right edge: keep left edge anchored.
        const double clampedEdge =
            std::min(domainEnd, std::max(edgeBeat, anchorBeat + std::max(1e-6, minWidthBeats)));
        const double desiredWidth = std::max(minWidthBeats, std::min(maxWidthBeats, clampedEdge - anchorBeat));
        const float newPixelsPerBeat = std::clamp(static_cast<float>(gridWidthPx / desiredWidth), kMinPixelsPerBeat, kMaxPixelsPerBeat);
        applyZoom(newPixelsPerBeat);

        setTimelineViewStartBeat(anchorBeat, isFinal);
    }

    updateTimelineMinimap(0.0);
}

void TrackManagerUI::centerTimelineViewAtBeat(double centerBeat)
{
    const float gridWidthPx = getTimelineGridWidthPixels();
    if (!(m_pixelsPerBeat > 0.0f) || gridWidthPx <= 0.0f) return;

    const double viewWidthBeats = static_cast<double>(gridWidthPx / m_pixelsPerBeat);
    const double start = centerBeat - (viewWidthBeats * 0.5);
    setTimelineViewStartBeat(start, true);
}

void TrackManagerUI::zoomTimelineAroundBeat(double anchorBeat, float zoomMultiplier)
{
    const float gridWidthPx = getTimelineGridWidthPixels();
    if (gridWidthPx <= 0.0f) return;

    // Minimap zoom must feel immediate; keep the smooth-zoom system in sync by updating both.
    const float newPixelsPerBeat = std::clamp(m_pixelsPerBeat * zoomMultiplier, 8.0f, 300.0f);
    m_pixelsPerBeat = newPixelsPerBeat;
    m_targetPixelsPerBeat = newPixelsPerBeat;

    for (auto& trackUI : m_trackUIComponents) {
        trackUI->setPixelsPerBeat(m_pixelsPerBeat);
    }

    const double viewWidthBeats = static_cast<double>(gridWidthPx / m_pixelsPerBeat);
    const double viewStartBeat = anchorBeat - (viewWidthBeats * 0.5);
    setTimelineViewStartBeat(viewStartBeat, true);
    updateTimelineMinimap(0.0);
}

void TrackManagerUI::updateTimelineMinimap(double deltaTime)
{
    if (!m_timelineMinimap) return;
    if (!m_playlistVisible) return;
    if (!m_trackManager) return;

    const float gridWidthPx = getTimelineGridWidthPixels();
    if (!(m_pixelsPerBeat > 0.0f) || gridWidthPx <= 0.0f) return;

    const double viewStartBeat = static_cast<double>(m_timelineScrollOffset / m_pixelsPerBeat);
    const double viewWidthBeats = static_cast<double>(gridWidthPx / m_pixelsPerBeat);
    const double viewEndBeat = viewStartBeat + viewWidthBeats;

    const double playheadBeat = secondsToBeats(m_trackManager->getUIPosition());

    auto& playlist = m_trackManager->getPlaylistModel();
    double clipEndBeat = playlist.getTotalDurationBeats();

    const double padBeats = static_cast<double>(std::max(1, m_beatsPerBar)) * 2.0;
    const double minBeats = static_cast<double>(std::max(1, m_beatsPerBar)) * 8.0;
    double requiredEndBeat = std::max({minBeats, clipEndBeat + padBeats, playheadBeat + padBeats});
    requiredEndBeat = std::max(requiredEndBeat, viewWidthBeats + padBeats);

    if (!(m_minimapDomainEndBeat > 0.0)) {
        m_minimapDomainEndBeat = requiredEndBeat;
        m_minimapNeedsRebuild = true;
        m_minimapShrinkCooldown = 0.0;
    } else if (requiredEndBeat > m_minimapDomainEndBeat + 1e-3) {
        m_minimapDomainEndBeat = requiredEndBeat;
        m_minimapNeedsRebuild = true;
        m_minimapShrinkCooldown = 0.0;
    } else if (requiredEndBeat < m_minimapDomainEndBeat - 1e-3) {
        m_minimapShrinkCooldown += deltaTime;
        if (m_minimapShrinkCooldown >= 2.0) {
            m_minimapDomainEndBeat = requiredEndBeat;
            m_minimapNeedsRebuild = true;
            m_minimapShrinkCooldown = 0.0;
        }
    } else {
        m_minimapShrinkCooldown = 0.0;
    }

    if (m_minimapNeedsRebuild) {
        std::vector<NomadUI::TimelineMinimapClipSpan> spans;

        for (const auto& laneId : playlist.getLaneIDs()) {
            if (const auto* lane = playlist.getLane(laneId)) {
                for (const auto& clip : lane->clips) {
                    NomadUI::TimelineMinimapClipSpan span;
                    span.id = static_cast<NomadUI::TimelineMinimapClipId>(clip.id.high ^ clip.id.low);
                    span.type = NomadUI::TimelineMinimapClipType::Audio;

                    span.startBeat = clip.startBeat;
                    span.endBeat = clip.startBeat + clip.durationBeats;
                    if (!(span.endBeat > span.startBeat)) continue;

                    spans.push_back(span);
                }
            }
        }

        m_timelineSummaryCache.requestRebuild(std::move(spans), m_minimapDomainStartBeat, m_minimapDomainEndBeat);
        m_minimapNeedsRebuild = false;
    }


    m_timelineSummarySnapshot = m_timelineSummaryCache.getSnapshot();

    if (m_isDrawingSelectionBox) {
        auto& themeManager = NomadUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        const float controlAreaWidth = layout.trackControlsWidth;
        const float gridStartXAbs = getBounds().x + controlAreaWidth + 5.0f;

        const float minX = std::min(m_selectionBoxStart.x, m_selectionBoxEnd.x);
        const float maxX = std::max(m_selectionBoxStart.x, m_selectionBoxEnd.x);

        const double startBeat = (static_cast<double>((minX - gridStartXAbs) + m_timelineScrollOffset)) / m_pixelsPerBeat;
        const double endBeat = (static_cast<double>((maxX - gridStartXAbs) + m_timelineScrollOffset)) / m_pixelsPerBeat;
        m_minimapSelectionBeatRange.start = std::max(0.0, std::min(startBeat, endBeat));
        m_minimapSelectionBeatRange.end = std::max(0.0, std::max(startBeat, endBeat));
    }

    NomadUI::TimelineMinimapModel model;
    model.summary = &m_timelineSummarySnapshot;
    model.view.start = viewStartBeat;
    model.view.end = viewEndBeat;
    model.playheadBeat = playheadBeat;
    model.selection = m_minimapSelectionBeatRange;
    model.mode = m_minimapMode;
    model.aggregation = m_minimapAggregation;
    model.beatsPerBar = m_beatsPerBar;
    model.showSelection = model.selection.isValid();
    model.showLoop = false;
    model.showMarkers = false;
    model.showDiagnostics = false;

    m_timelineMinimap->setModel(model);
}

void TrackManagerUI::onHorizontalScroll(double position) {
    // Clamp scroll position to valid range (no negative scrolling)
    m_timelineScrollOffset = std::max(0.0f, static_cast<float>(position));
    
    // Sync horizontal scroll offset to all tracks
    for (auto& trackUI : m_trackUIComponents) {
        trackUI->setTimelineScrollOffset(m_timelineScrollOffset);
    }
    
    invalidateCache();  // ⚡ Mark cache dirty
}

void TrackManagerUI::deselectAllTracks() {
    for (auto& trackUI : m_trackUIComponents) {
        trackUI->setSelected(false);
    }
}

void TrackManagerUI::renderTimeRuler(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& rulerBounds) {
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    auto borderColor = themeManager.getColor("borderColor");
    auto textColor = themeManager.getColor("textSecondary");
    auto accentColor = themeManager.getColor("accentPrimary");
    // Ruler Render: "Mature" Playlist Style
    // Darker, more professional background
    auto bg = NomadUI::NUIColor(0.08f, 0.08f, 0.10f, 1.0f); 
    auto textCol = NomadUI::NUIColor(0.7f, 0.7f, 0.75f, 1.0f);
    auto tickCol = NomadUI::NUIColor(0.35f, 0.35f, 0.40f, 1.0f);
    auto borderCol = NomadUI::NUIColor(0.0f, 0.0f, 0.0f, 0.5f);
    
    // Restore layout definition
    const auto& layout = themeManager.getLayoutDimensions();

    // Draw full ruler background
    renderer.fillRect(rulerBounds, bg);
    
    // Calculate grid start EXACTLY like TrackUIComponent
    float controlAreaWidth = layout.trackControlsWidth;
    float gridStartX = rulerBounds.x + controlAreaWidth + 5.0f;
    
    // Calculate gridWidth EXACTLY like TrackUIComponent
    // Track uses: gridWidth = bounds.width - (buttonX + layout.controlButtonWidth + 10)
    // But tracks have scrollbar subtracted from their bounds.width
    float scrollbarWidth = 15.0f;
    float trackWidth = rulerBounds.width - scrollbarWidth; // Match track width (subtract scrollbar)
    float gridWidth = trackWidth - controlAreaWidth - 10.0f; // Match TrackUIComponent grid width
    gridWidth = std::max(0.0f, gridWidth);
    
    // Grid area background (same as ruler bg for unity, or slightly darker if desired)
    NomadUI::NUIRect gridRulerRect(gridStartX, rulerBounds.y, gridWidth, rulerBounds.height);
    renderer.fillRect(gridRulerRect, bg);
    
    // Draw border
    renderer.strokeRect(rulerBounds, 1, borderColor);

    // Dedicated "corner" panel where track controls meet the ruler.
    // It draws its own right-hand separator so nothing feels like it bleeds across panels.
    const NomadUI::NUIRect cornerRect(rulerBounds.x, rulerBounds.y, controlAreaWidth, rulerBounds.height);
    renderer.drawLine(
        NomadUI::NUIPoint(cornerRect.right(), cornerRect.y),
        NomadUI::NUIPoint(cornerRect.right(), cornerRect.bottom()),
        1.0f,
        borderColor.withAlpha(0.5f)
    );

    // Clip ticks/labels to the grid area (prevents any accidental bleed into the corner/scrollbar).
    NomadUI::NUIRect gridClip = gridRulerRect;
    if (m_isRenderingToCache) {
        const NomadUI::NUIRect componentBounds = getBounds();
        gridClip.x -= componentBounds.x;
        gridClip.y -= componentBounds.y;
    }
    bool rulerClipEnabled = false;
    if (!gridClip.isEmpty()) {
        renderer.setClipRect(gridClip);
        rulerClipEnabled = true;
    }
    
    // Grid spacing - DYNAMIC based on zoom level
    int beatsPerBar = m_beatsPerBar;
    float pixelsPerBar = m_pixelsPerBeat * beatsPerBar;
    
    // Calculate which bar to start drawing from based on scroll offset
    int startBar = static_cast<int>(m_timelineScrollOffset / pixelsPerBar);
    
    // Calculate end bar based on visible width (no max extent bounds)
    int visibleBars = static_cast<int>(std::ceil((m_timelineScrollOffset + gridWidth) / pixelsPerBar)) - startBar;
    int endBar = startBar + visibleBars + 1;  // Draw all visible bars
    
    // Strict manual culling boundaries
    float gridEndX = gridStartX + gridWidth;
    
    // Draw vertical ticks - dynamically based on visible bars and scroll offset
    for (int bar = startBar; bar <= endBar; ++bar) {
        // Calculate x position accounting for scroll offset
        float x = gridStartX + (bar * pixelsPerBar) - m_timelineScrollOffset;
        
        // Strict manual culling - only draw if strictly within grid area
        if (x < gridStartX || x > gridEndX) {
            continue;
        }
        
        // Bar number (1-based)
        int barNum = bar + 1;
        std::string barText = std::to_string(barNum);
        
        // FL Studio style: Bigger text for major bars (1, 5, 9, etc.), smaller for others
        // At low zoom, only show major bar numbers; at high zoom show all
        bool isMajorBar = (barNum == 1) || ((barNum - 1) % 4 == 0); // 1, 5, 9, 13...
        float fontSize = isMajorBar ? 11.0f : 9.0f;  // Compact ruler text
        float textAlpha = isMajorBar ? 1.0f : 0.7f;
        
        auto textSize = renderer.measureText(barText, fontSize);
        
        // Place text vertically centered in ruler area (top-left Y positioning)
        float textY = std::round(renderer.calculateTextY(rulerBounds, fontSize));
        
        // Position text to the RIGHT of the grid line with small offset
        float textX = x + 4.0f;
        
        // Only draw text if it won't bleed off the right edge
        // Allow it to appear from the left (even partially) so "1" shows up early
        float textWidth = textSize.width;
        // Only check right edge - allow text to start appearing from left
        if (textX + textWidth <= gridEndX) {
            renderer.drawText(barText, 
                            NomadUI::NUIPoint(textX, textY),
                            fontSize, textCol);
        }
        
        // Bar tick line - major bars get full height, others half
        // Bar tick line - major bars get full height/top-half style, others half
        // Mature Style: Ticks bottom-up
        float tickHeight = isMajorBar ? rulerBounds.height * 0.5f : rulerBounds.height * 0.25f;
        renderer.drawLine(
            NomadUI::NUIPoint(x, rulerBounds.y + rulerBounds.height - tickHeight),
            NomadUI::NUIPoint(x, rulerBounds.y + rulerBounds.height),
            1.0f,
            isMajorBar ? tickCol : tickCol.withAlpha(0.7f)
        );
        
        // Beat ticks within the bar (only if zoomed in enough)
        if (m_pixelsPerBeat >= 15.0f) {
            for (int beat = 1; beat < beatsPerBar; ++beat) {
                float beatX = x + (beat * m_pixelsPerBeat);
                
                // Strict manual culling for beat lines
                if (beatX < gridStartX || beatX > gridEndX) {
                    continue;
                }
                
                renderer.drawLine(
                    NomadUI::NUIPoint(beatX, rulerBounds.y + rulerBounds.height * 0.75f), // Short tick
                    NomadUI::NUIPoint(beatX, rulerBounds.y + rulerBounds.height),
                    1.0f,
                    tickCol.withAlpha(0.6f)
                );
            }
        }
    }

    // === RULER SELECTION HIGHLIGHT ===
    // Draw selection highlight if active (either dragging or confirmed)
    if (m_isDraggingRulerSelection || m_hasRulerSelection) {
        double selStartBeat = std::min(m_rulerSelectionStartBeat, m_rulerSelectionEndBeat);
        double selEndBeat = std::max(m_rulerSelectionStartBeat, m_rulerSelectionEndBeat);
        
        // Convert beats to pixel positions
        float selStartX = gridStartX + static_cast<float>(selStartBeat * m_pixelsPerBeat) - m_timelineScrollOffset;
        float selEndX = gridStartX + static_cast<float>(selEndBeat * m_pixelsPerBeat) - m_timelineScrollOffset;
        
        // Only draw if visible in grid area
        if (selEndX >= gridStartX && selStartX <= gridEndX) {
            // Clamp to visible area
            float visibleStartX = std::max(selStartX, gridStartX);
            float visibleEndX = std::min(selEndX, gridEndX);
            float selectionWidth = visibleEndX - visibleStartX;
            
            if (selectionWidth > 0.0f) {
                NomadUI::NUIRect selectionRect(visibleStartX, rulerBounds.y, selectionWidth, rulerBounds.height);
                
                // Fill with semi-transparent accent color (FL Studio style)
                auto accentColor = themeManager.getColor("accentPrimary");
                renderer.fillRect(selectionRect, accentColor.withAlpha(0.25f));
                
                // Draw subtle borders at selection edges
                if (selStartX >= gridStartX && selStartX <= gridEndX) {
                    renderer.drawLine(
                        NomadUI::NUIPoint(selStartX, rulerBounds.y),
                        NomadUI::NUIPoint(selStartX, rulerBounds.bottom()),
                        1.0f,
                        accentColor.withAlpha(0.6f)
                    );
                }
                if (selEndX >= gridStartX && selEndX <= gridEndX) {
                    renderer.drawLine(
                        NomadUI::NUIPoint(selEndX, rulerBounds.y),
                        NomadUI::NUIPoint(selEndX, rulerBounds.bottom()),
                        1.0f,
                        accentColor.withAlpha(0.6f)
                    );
                }
            }
        }
    }

    if (rulerClipEnabled) {
        renderer.clearClipRect();
    }
}

// Set loop region (called from Main.cpp when loop preset changes)
void TrackManagerUI::setLoopRegion(double startBeat, double endBeat, bool enabled) {
    m_loopStartBeat = startBeat;
    m_loopEndBeat = endBeat;
    m_loopEnabled = enabled;
    m_cacheInvalidated = true;  // Redraw to show updated markers
}

// Render FL Studio-style loop markers on ruler
void TrackManagerUI::renderLoopMarkers(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& rulerBounds) {
    // Only show markers when there's an active ruler selection
    if (!m_hasRulerSelection) return;
    
    if (m_loopEndBeat <= m_loopStartBeat) return;  // Invalid loop region
    
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    
    // Calculate grid start (same as ruler)
    float controlAreaWidth = layout.trackControlsWidth;
    float gridStartX = rulerBounds.x + controlAreaWidth + 5.0f;
    float scrollbarWidth = 15.0f;
    float trackWidth = rulerBounds.width - scrollbarWidth;
    float gridWidth = trackWidth - controlAreaWidth - 10.0f;
    gridWidth = std::max(0.0f, gridWidth);
    float gridEndX = gridStartX + gridWidth;
    
    // Convert loop beats to pixel positions
    float loopStartX = gridStartX + (static_cast<float>(m_loopStartBeat) * m_pixelsPerBeat) - m_timelineScrollOffset;
    float loopEndX = gridStartX + (static_cast<float>(m_loopEndBeat) * m_pixelsPerBeat) - m_timelineScrollOffset;
    
    // Check if markers are visible
    bool startVisible = (loopStartX >= gridStartX && loopStartX <= gridEndX);
    bool endVisible = (loopEndX >= gridStartX && loopEndX <= gridEndX);
    
    if (!startVisible && !endVisible) return;  // Both markers off-screen
    
    // Color based on enabled state and hover
    auto accentColor = themeManager.getColor("accentPrimary");
    NomadUI::NUIColor markerColor;
    
    if (m_loopEnabled) {
        markerColor = accentColor.withAlpha(0.8f);  // Bright when active
    } else {
        markerColor = accentColor.withAlpha(0.3f);  // Dimmed when inactive
    }
    
    // Marker dimensions
    const float triangleWidth = 12.0f;
    const float triangleHeight = 10.0f;
    
    // === RENDER LOOP START MARKER ===
    if (startVisible) {
        NomadUI::NUIColor startColor = markerColor;
        if (m_hoveringLoopStart || m_isDraggingLoopStart) {
            startColor = accentColor;  // Full brightness on hover/drag
        }
        
        // Draw triangle pointing down (using lines)
        NomadUI::NUIPoint p1(loopStartX, rulerBounds.y + triangleHeight);  // Bottom center
        NomadUI::NUIPoint p2(loopStartX - triangleWidth / 2, rulerBounds.y);  // Top left
        NomadUI::NUIPoint p3(loopStartX + triangleWidth / 2, rulerBounds.y);  // Top right
        
        // Draw filled triangle using lines
        renderer.drawLine(p1, p2, 2.0f, startColor);
        renderer.drawLine(p2, p3, 2.0f, startColor);
        renderer.drawLine(p3, p1, 2.0f, startColor);
        
        // Draw vertical line from triangle to bottom
        renderer.drawLine(
            NomadUI::NUIPoint(loopStartX, rulerBounds.y + triangleHeight),
            NomadUI::NUIPoint(loopStartX, rulerBounds.y + rulerBounds.height),
            2.0f,
            startColor
        );
    }
    
    // === RENDER LOOP END MARKER ===
    if (endVisible) {
        NomadUI::NUIColor endColor = markerColor;
        if (m_hoveringLoopEnd || m_isDraggingLoopEnd) {
            endColor = accentColor;  // Full brightness on hover/drag
        }
        
        // Draw triangle pointing down (using lines)
        NomadUI::NUIPoint p1(loopEndX, rulerBounds.y + triangleHeight);  // Bottom center
        NomadUI::NUIPoint p2(loopEndX - triangleWidth / 2, rulerBounds.y);  // Top left
        NomadUI::NUIPoint p3(loopEndX + triangleWidth / 2, rulerBounds.y);  // Top right
        
        // Draw filled triangle using lines
        renderer.drawLine(p1, p2, 2.0f, endColor);
        renderer.drawLine(p2, p3, 2.0f, endColor);
        renderer.drawLine(p3, p1, 2.0f, endColor);
        
        // Draw vertical line from triangle to bottom
        renderer.drawLine(
            NomadUI::NUIPoint(loopEndX, rulerBounds.y + triangleHeight),
            NomadUI::NUIPoint(loopEndX, rulerBounds.y + rulerBounds.height),
            2.0f,
            endColor
        );
    }
}


// Calculate maximum timeline extent needed based on all samples
// Calculate maximum timeline extent needed based on all clips
double TrackManagerUI::getMaxTimelineExtent() const {
    if (!m_trackManager) return 0.0;
    
    auto& playlist = m_trackManager->getPlaylistModel();
    double totalDurationBeats = playlist.getTotalDurationBeats();
    
    double bpm = 120.0; // TODO: Get from project/transport
    double secondsPerBeat = 60.0 / bpm;
    
    // Minimum extent - at least 8 bars even if empty
    double minExtent = 8.0 * m_beatsPerBar * secondsPerBeat;
    
    // Convert beats to seconds for extent
    double totalDurationSeconds = totalDurationBeats * secondsPerBeat;
    
    // Add 2 bars padding
    double paddedEnd = totalDurationSeconds + (2.0 * m_beatsPerBar * secondsPerBeat);
    
    return std::max(paddedEnd, minExtent);
}


// Shared Grid Drawing Helper
void TrackManagerUI::drawGrid(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& bounds, float gridStartX, float gridWidth, float timelineScrollOffset) {
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    
    // Draw Dynamic Snap Grid
    double snapDur = NomadUI::MusicTheory::getSnapDuration(m_snapSetting);
    if (m_snapSetting == NomadUI::SnapGrid::None) snapDur = 1.0;
    if (snapDur <= 0.0001) snapDur = 1.0;

    // Dynamic Density (Relaxed to 5px)
    while ((m_pixelsPerBeat * snapDur) < 5.0f) {
        snapDur *= 2.0;
    }

    double startBeat = timelineScrollOffset / m_pixelsPerBeat;
    double endBeat = startBeat + (gridWidth / m_pixelsPerBeat);

    // Round start to nearest snap
    double current = std::floor(startBeat / snapDur) * snapDur;
    
    // Grid Lines - Using Theme Tokens
    NomadUI::NUIColor barLineColor = themeManager.getColor("gridBar");
    NomadUI::NUIColor beatLineColor = themeManager.getColor("gridBeat");
    NomadUI::NUIColor subBeatLineColor = themeManager.getColor("gridSubdivision");

    for (; current <= endBeat + snapDur; current += snapDur) {
        float xPos = bounds.x + gridStartX + static_cast<float>(current * m_pixelsPerBeat) - timelineScrollOffset;
        
        // Strict culling within valid grid area
        if (xPos < bounds.x + gridStartX || xPos > bounds.x + gridStartX + gridWidth) continue;

        // Hierarchy logic
        bool isBar = (std::fmod(std::abs(current), (double)m_beatsPerBar) < 0.001);
        bool isBeat = (std::fmod(std::abs(current), 1.0) < 0.001);
        
        // Draw Vertical Grid Line (Full Height)
        float trackAreaTop = bounds.y;
        float trackAreaBottom = bounds.y + bounds.height;
        
        if (isBar) {
            renderer.drawLine(NomadUI::NUIPoint(xPos, trackAreaTop), NomadUI::NUIPoint(xPos, trackAreaBottom), 1.0f, barLineColor);
        }
        else if (isBeat) {
            renderer.drawLine(NomadUI::NUIPoint(xPos, trackAreaTop), NomadUI::NUIPoint(xPos, trackAreaBottom), 1.0f, beatLineColor);
        }
        else {
            renderer.drawLine(NomadUI::NUIPoint(xPos, trackAreaTop), NomadUI::NUIPoint(xPos, trackAreaBottom), 1.0f, subBeatLineColor);
        }
    }
}

// Draw playhead (vertical line showing current playback position)
void TrackManagerUI::renderPlayhead(NomadUI::NUIRenderer& renderer) {
    if (!m_trackManager) return;
    
    // Get current playback position from track manager (UI Safe)
    double currentPosition = m_trackManager->getUIPosition();
    
    // Convert position (seconds) to pixel position
    double bpm = m_trackManager->getPlaylistModel().getBPM();
    double secondsPerBeat = 60.0 / bpm;
    double positionInBeats = currentPosition / secondsPerBeat;
    
    // Use double-precision relative calculate to avoid playhead jitter
    double relPositionX = (positionInBeats * m_pixelsPerBeat) - static_cast<double>(m_timelineScrollOffset);
    
    // Calculate playhead X position accounting for scroll offset
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    float controlAreaWidth = layout.trackControlsWidth;
    
    NomadUI::NUIRect bounds = getBounds();
    float gridStartX = bounds.x + controlAreaWidth + 5;
    float playheadX = gridStartX + static_cast<float>(relPositionX);
    
    // Calculate bounds and triangle size for precise culling
    float scrollbarWidth = 15.0f;
    float trackWidth = bounds.width - scrollbarWidth;
    float gridWidth = trackWidth - (controlAreaWidth + 5.0f);
    float gridEndX = gridStartX + gridWidth;
    float triangleSize = 6.0f;  // Triangle extends this much left/right from playhead center
    
    // Calculate playhead boundaries
    float headerHeight = 38.0f;
    float horizontalScrollbarHeight = 24.0f;
    float rulerHeight = 28.0f;
    float playheadStartY = bounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;
    
    // In v3.1, overlays are hit-test transparent and don't affect playhead line culling directly.
    // We just cull against the workspace grid area.
    float playheadEndX = bounds.x + bounds.width - scrollbarWidth;
    float playheadEndY = bounds.y + bounds.height;
    
    // PRECISE CULLING: Draw if the playhead CENTER is within bounds
    // We allow the triangle to extend slightly outside for better visibility at boundaries
    // This ensures playhead shows at position 0 (start) and at the right edge
    // Only cull if the entire playhead is clearly outside the visible area
    float playheadLeftEdge = playheadX - triangleSize;
    float playheadRightEdge = playheadX + triangleSize;
    
    // Draw if playhead center is within the visible timeline bounds
    // Allow triangle to extend outside as long as center line is visible
    if (playheadX >= gridStartX && playheadX <= playheadEndX) {
        // Playhead color - White/Cyan for contrast against dark UI
        NomadUI::NUIColor playheadColor = NomadUI::NUIColor::white(); // Crisp white
        // Alternative: themeManager.getColor("accentCyan");

        // Draw playhead line (Thinner 1px, no glow)
        renderer.drawLine(
            NomadUI::NUIPoint(playheadX, playheadStartY),
            NomadUI::NUIPoint(playheadX, playheadEndY),
            1.0f, 
            playheadColor
        );

        // Draw playhead Triangle Cap (In Ruler)
        // Triangle pointing down
        NomadUI::NUIPoint p1(playheadX, playheadStartY + triangleSize);  // Bottom center
        NomadUI::NUIPoint p2(playheadX - triangleSize, playheadStartY);  // Top left
        NomadUI::NUIPoint p3(playheadX + triangleSize, playheadStartY);  // Top right
        
        // Fill triangle (fallback to lines)
        renderer.drawLine(p1, p2, 1.0f, playheadColor);
        renderer.drawLine(p2, p3, 1.0f, playheadColor);
        renderer.drawLine(p3, p1, 1.0f, playheadColor);
        
        // Draw a small "Jewel" at the top center
        renderer.fillRoundedRect(NomadUI::NUIRect(playheadX - 2.0f, playheadStartY - 2.0f, 4.0f, 4.0f), 2.0f, NomadUI::NUIColor::white());
    }
}

// ⚡ MULTI-LAYER CACHING IMPLEMENTATION

void TrackManagerUI::updateBackgroundCache(NomadUI::NUIRenderer& renderer) {
    rmt_ScopedCPUSample(TrackMgr_UpdateBgCache, 0);
    
    int width = m_backgroundCachedWidth;
    int height = m_backgroundCachedHeight;
    
    if (width <= 0 || height <= 0) return;
    
    // Create FBO for background
    uint32_t texId = renderer.renderToTextureBegin(width, height);
    if (texId == 0) {
        Log::warning("❌ Failed to create background FBO");
        m_backgroundNeedsUpdate = false; // Don't retry every frame
        return;
    }
    
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    
    // Calculate layout dimensions
    float controlAreaWidth = layout.trackControlsWidth;
    float gridStartX = controlAreaWidth + 5;
    float scrollbarWidth = 15.0f;
    float gridWidth = width - controlAreaWidth - scrollbarWidth - 5;
    
    NomadUI::NUIRect textureBounds(0, 0, static_cast<float>(width), static_cast<float>(height));
    NomadUI::NUIColor bgColor = themeManager.getColor("backgroundPrimary");
    NomadUI::NUIColor borderColor = themeManager.getColor("border");
    
    // Draw background panels
    NomadUI::NUIRect controlBg(0, 0, controlAreaWidth, static_cast<float>(height));
    renderer.fillRect(controlBg, bgColor);
    
    NomadUI::NUIRect gridBg(gridStartX, 0, gridWidth, static_cast<float>(height));
    // Grid Background: Deep Charcoal (Lifted from Void)
    renderer.fillRect(gridBg, NomadUI::NUIColor(0.09f, 0.09f, 0.10f, 1.0f));
    
    // Draw borders
    renderer.strokeRect(textureBounds, 1, borderColor);
    
    // Draw header bar
    float headerHeight = 38.0f;
    NomadUI::NUIRect headerRect(0, 0, static_cast<float>(width), headerHeight);
    renderer.fillRect(headerRect, bgColor);
    renderer.strokeRect(headerRect, 1, borderColor);
    
    // Draw time ruler
    float rulerHeight = 28.0f;
    float horizontalScrollbarHeight = 24.0f;
    NomadUI::NUIRect rulerRect(0, headerHeight + horizontalScrollbarHeight, static_cast<float>(width), rulerHeight);
    
    // Render ruler ticks (static part only - no moving elements)
    double bpm = 120.0;
    double secondsPerBeat = 60.0 / bpm;
    double maxExtent = getMaxTimelineExtent();
    double maxExtentInBeats = maxExtent / secondsPerBeat;
    
    // Ruler Render: "Mature" Playlist Style
    auto bg = NomadUI::NUIColor(0.08f, 0.08f, 0.10f, 1.0f); 
    auto textCol = NomadUI::NUIColor(0.7f, 0.7f, 0.75f, 1.0f);
    auto tickCol = NomadUI::NUIColor(0.35f, 0.35f, 0.40f, 1.0f);
    
    // Draw ruler background
    renderer.fillRect(rulerRect, bg);
    renderer.strokeRect(rulerRect, 1, borderColor);
    
    // Draw beat markers (grid lines)
    // USE SHARED HELPER
    float trackAreaTop = rulerRect.y + rulerRect.height;
    NomadUI::NUIRect gridArea(0, trackAreaTop, static_cast<float>(width), static_cast<float>(height) - trackAreaTop);
    
    drawGrid(renderer, gridArea, gridStartX, gridWidth, m_timelineScrollOffset);



    // Bar numbers (cached in background texture)
    float barFontSize = 11.0f;
    for (int bar = 0; bar <= static_cast<int>(maxExtentInBeats / m_beatsPerBar) + 4; ++bar) {
        float x = rulerRect.x + gridStartX + (bar * m_beatsPerBar * m_pixelsPerBeat) - m_timelineScrollOffset;
        if (x < rulerRect.x + gridStartX - 2.0f || x > rulerRect.right() + m_pixelsPerBeat) continue;

        std::string barText = std::to_string(bar + 1);
        auto textSize = renderer.measureText(barText, barFontSize);
        
        // Center text box vertically: baseline at middle + half text height
        // Note: drawText expects Top-Left coordinate, renderer handles baseline conversion
        float textY = std::floor(rulerRect.y + (rulerRect.height - textSize.height) * 0.5f);
        
        // Center text horizontally on the grid line
        float textX = std::floor(x - textSize.width * 0.5f);

        if (textX + textSize.width <= rulerRect.right() - 6.0f) {
            renderer.drawText(barText, NomadUI::NUIPoint(textX, textY), barFontSize, textCol);
        }
    }
    
    renderer.renderToTextureEnd();
    m_backgroundTextureId = texId;
    m_backgroundNeedsUpdate = false;
    
    Log::info("✅ Background cache updated: " + std::to_string(width) + "×" + std::to_string(height));
}

void TrackManagerUI::updateControlsCache(NomadUI::NUIRenderer& renderer) {
    // TODO: Cache static UI controls (buttons, labels) - not implemented yet
    m_controlsNeedsUpdate = false;
}

void TrackManagerUI::updateTrackCache(NomadUI::NUIRenderer& renderer, size_t trackIndex) {
    // TODO: Per-track FBO caching for waveforms - not implemented yet
    if (trackIndex < m_trackCaches.size()) {
        m_trackCaches[trackIndex].needsUpdate = false;
    }
}

void TrackManagerUI::invalidateAllCaches() {
    m_backgroundNeedsUpdate = true;
    m_controlsNeedsUpdate = true;
    for (size_t i = 0; i < m_trackCaches.size(); ++i) {
        m_trackCaches[i].needsUpdate = true;
    }
}

void TrackManagerUI::invalidateCache() {
    // New FBO caching system - invalidate the main cache
    m_cacheInvalidated = true;
    
    // Also invalidate old multi-layer caches for compatibility
    m_backgroundNeedsUpdate = true;

    // Ensure we get a redraw even if the outer loop is dirty-driven.
    setDirty(true);
}

// =============================================================================
// Clip Manipulation Methods
// =============================================================================

TrackUIComponent* TrackManagerUI::getSelectedTrackUI() const {
    for (const auto& trackUI : m_trackUIComponents) {
        if (trackUI && trackUI->isSelected()) {
            return trackUI.get();
        }
    }
    return nullptr;
}

void TrackManagerUI::splitSelectedClipAtPlayhead() {
    if (!m_trackManager || !m_selectedClipId.isValid()) {
        Log::warning("No clip selected for split");
        return;
    }
    
    // Get current playhead position from transport
    double currentPosSeconds = m_trackManager->getPosition();
    double bpm = 120.0; // TODO: Get from transport
    double secondsPerBeat = 60.0 / bpm;
    double splitBeat = currentPosSeconds / secondsPerBeat;
    
    auto& playlist = m_trackManager->getPlaylistModel();
    const auto* clip = playlist.getClip(m_selectedClipId);
    
    if (!clip || splitBeat <= clip->startBeat || splitBeat >= clip->startBeat + clip->durationBeats) {
        Log::warning("Playhead not within selected clip bounds for split");
        return;
    }
    
    playlist.splitClip(m_selectedClipId, splitBeat);
    refreshTracks();
    invalidateCache();
    scheduleTimelineMinimapRebuild();
    
    Log::info("[TrackManagerUI] Clip split at playhead (beat " + std::to_string(splitBeat) + ")");
}


void TrackManagerUI::copySelectedClip() {
    if (!m_trackManager || !m_selectedClipId.isValid()) {
        Log::warning("No clip selected for copy");
        return;
    }
    
    auto& playlist = m_trackManager->getPlaylistModel();
    const auto* clip = playlist.getClip(m_selectedClipId);
    if (!clip) return;
    
    // Copy to clipboard (v3.0 metadata)
    m_clipboard.hasData = true;
    m_clipboard.patternId = clip->patternId;
    m_clipboard.durationBeats = clip->durationBeats;
    m_clipboard.edits = clip->edits;
    m_clipboard.name = clip->name;
    m_clipboard.colorRGBA = clip->colorRGBA;
    
    Log::info("Copied clip: " + m_clipboard.name);
}


void TrackManagerUI::cutSelectedClip() {
    if (!m_trackManager || !m_selectedClipId.isValid()) {
        Log::warning("No clip selected for cut");
        return;
    }
    
    auto& playlist = m_trackManager->getPlaylistModel();
    const auto* clip = playlist.getClip(m_selectedClipId);
    if (!clip) return;
    
    // Copy to clipboard first
    m_clipboard.hasData = true;
    m_clipboard.patternId = clip->patternId;
    m_clipboard.durationBeats = clip->durationBeats;
    m_clipboard.edits = clip->edits;
    m_clipboard.name = clip->name;
    m_clipboard.colorRGBA = clip->colorRGBA;
    
    // Now remove the source clip
    playlist.removeClip(m_selectedClipId);
    m_selectedClipId = ClipInstanceID{};
    
    refreshTracks();
    invalidateCache();
    scheduleTimelineMinimapRebuild();
    
    Log::info("Cut clip to clipboard: " + m_clipboard.name);
}


void TrackManagerUI::pasteClip() {
    if (!m_clipboard.hasData || !m_trackManager) {
        Log::warning("Clipboard is empty");
        return;
    }
    
    // Find target lane (currently selected track, or first lane)
    auto* selectedUI = getSelectedTrackUI();
    PlaylistLaneID targetLaneId;
    
    if (selectedUI) {
        targetLaneId = selectedUI->getLaneId();
    } else {
        targetLaneId = m_trackManager->getPlaylistModel().getLaneId(0);
    }
    
    if (!targetLaneId.isValid()) {
        Log::warning("No valid lane for paste");
        return;
    }
    
    // Get paste position (at playhead)
    double currentPosSeconds = m_trackManager->getPosition();
    double bpm = 120.0; // TODO: Get from transport
    double secondsPerBeat = 60.0 / bpm;
    double pasteBeat = currentPosSeconds / secondsPerBeat;
    
    // Create new clip from clipboard data
    ClipInstance newClip;
    newClip.patternId = m_clipboard.patternId;
    newClip.startBeat = pasteBeat;
    newClip.durationBeats = m_clipboard.durationBeats;
    newClip.edits = m_clipboard.edits;
    newClip.name = m_clipboard.name;
    newClip.colorRGBA = m_clipboard.colorRGBA;
    
    m_trackManager->getPlaylistModel().addClip(targetLaneId, newClip);
    
    refreshTracks();
    invalidateCache();
    scheduleTimelineMinimapRebuild();
    
    Log::info("Pasted clip to lane: " + m_clipboard.name);
}


void TrackManagerUI::duplicateSelectedClip() {
    if (!m_trackManager || !m_selectedClipId.isValid()) {
        Log::warning("No clip selected for duplicate");
        return;
    }
    
    ClipInstanceID newClipId = m_trackManager->getPlaylistModel().duplicateClip(m_selectedClipId);
    if (newClipId.isValid()) {
        m_selectedClipId = newClipId; // Select the newly created clip
        
        refreshTracks();
        invalidateCache();
        scheduleTimelineMinimapRebuild();
        
        Log::info("Duplicated clip via PlaylistModel");
    }
}


void TrackManagerUI::deleteSelectedClip() {
    if (!m_trackManager || !m_selectedClipId.isValid()) {
        Log::warning("No clip selected for delete");
        return;
    }
    
    m_trackManager->getPlaylistModel().removeClip(m_selectedClipId);
    m_selectedClipId = ClipInstanceID{};
    
    refreshTracks();
    invalidateCache();
    scheduleTimelineMinimapRebuild();
    
    Log::info("Deleted selected clip via PlaylistModel");
}


// =============================================================================
// Drop Target Implementation (IDropTarget)
// =============================================================================

NomadUI::DropFeedback TrackManagerUI::onDragEnter(const NomadUI::DragData& data, const NomadUI::NUIPoint& position) {
    Log::info("[TrackManagerUI] Drag entered");
    
    // Accept file drops and audio clip moves
    if (data.type != NomadUI::DragDataType::File && data.type != NomadUI::DragDataType::AudioClip) {
        return NomadUI::DropFeedback::Invalid;
    }

    // Early reject unsupported formats (cheap extension check; full validation happens on drop).
    if (data.type == NomadUI::DragDataType::File &&
        !AudioFileValidator::hasValidAudioExtension(data.filePath)) {
        m_showDropPreview = false;
        setDirty(true);
        return NomadUI::DropFeedback::Invalid;
    }
    
    // Calculate target track and time
    m_dropTargetTrack = getTrackAtPosition(position.y);
    m_dropTargetTime = getTimeAtPosition(position.x);
    
    // Allow dropping on existing tracks OR appending a new track
    int trackCount = static_cast<int>(m_trackManager->getTrackCount());
    
    // If dragging below last track, target the next available slot
    if (m_dropTargetTrack >= trackCount) {
        m_dropTargetTrack = trackCount;
    }
    
    if (m_dropTargetTrack >= 0 && m_dropTargetTrack <= trackCount) {
        m_showDropPreview = true;
        setDirty(true);
        // Move for clips, Copy for files
        return data.type == NomadUI::DragDataType::AudioClip ? 
               NomadUI::DropFeedback::Move : NomadUI::DropFeedback::Copy;
    }
    
    return NomadUI::DropFeedback::Invalid;
}

NomadUI::DropFeedback TrackManagerUI::onDragOver(const NomadUI::DragData& data, const NomadUI::NUIPoint& position) {
    // Keep feedback "Invalid" for unsupported formats while hovering.
    if (data.type == NomadUI::DragDataType::File &&
        !AudioFileValidator::hasValidAudioExtension(data.filePath)) {
        if (m_showDropPreview) {
            m_showDropPreview = false;
            setDirty(true);
        }
        return NomadUI::DropFeedback::Invalid;
    }

    // Update target track and time as mouse moves
    int newTrack = getTrackAtPosition(position.y);
    
    // Explicit mapping: Workspace -> Grid -> Beat
    auto& theme = NomadUI::NUIThemeManager::getInstance();
    float controlWidth = theme.getLayoutDimensions().trackControlsWidth;
    float gridStartX = getBounds().x + controlWidth + 5.0f;
    
    // REJECTION: If dropping on the control area
    if (position.x < gridStartX) {
        if (m_showDropPreview) {
            m_showDropPreview = false;
            setDirty(true);
            Log::info("[TrackManagerUI] Drag over rejected: Cursor in control area");
        }
        return NomadUI::DropFeedback::Invalid;
    }

    double gridX = position.x - gridStartX;
    double rawTimeBeats = (gridX + m_timelineScrollOffset) / m_pixelsPerBeat;
    double snappedBeats = snapBeatToGrid(rawTimeBeats); 
    double newTime = m_trackManager->getPlaylistModel().beatToSeconds(snappedBeats);
    
    int trackCount = static_cast<int>(m_trackManager->getTrackCount());
    
    // If dragging below last track, target the next available slot
    if (newTrack >= trackCount) {
        newTrack = trackCount;
    }
    
    // Only update if changed (performance optimization)
    if (newTrack != m_dropTargetTrack || std::abs(newTime - m_dropTargetTime) > 0.001) {
        m_dropTargetTrack = newTrack;
        m_dropTargetTime = std::max(0.0, newTime);  // Don't allow negative time
        
        if (m_dropTargetTrack >= 0 && m_dropTargetTrack <= trackCount) {
            m_showDropPreview = true;
            setDirty(true);
            // Move for clips, Copy for files
            return data.type == NomadUI::DragDataType::AudioClip ? 
                   NomadUI::DropFeedback::Move : NomadUI::DropFeedback::Copy;
        } else {
            m_showDropPreview = false;
            setDirty(true);
            return NomadUI::DropFeedback::Invalid;
        }
    }
    
    // Return appropriate feedback based on preview state
    if (m_showDropPreview) {
        return data.type == NomadUI::DragDataType::AudioClip ? 
               NomadUI::DropFeedback::Move : NomadUI::DropFeedback::Copy;
    }
    return NomadUI::DropFeedback::Invalid;
}

void TrackManagerUI::onDragLeave() {
    Log::info("[TrackManagerUI] Drag left");
    clearDropPreview();
    setDirty(true);
}

NomadUI::DropResult TrackManagerUI::onDrop(const NomadUI::DragData& data, const NomadUI::NUIPoint& position) {
    NomadUI::DropResult result;
    
    // 1. Calculate drop location
    int laneIndex = getTrackAtPosition(position.y);
    double rawTimeSeconds = std::max(0.0, getTimeAtPosition(position.x));
    
    // v3.0: We work strictly in beats for arrangement
    double rawTimeBeats = m_trackManager->getPlaylistModel().secondsToBeats(rawTimeSeconds);
    
    // Snap-to-grid logic (Canonical Beat-Space)
    double timePositionBeats = snapBeatToGrid(rawTimeBeats);
    
    auto& playlist = m_trackManager->getPlaylistModel();
    size_t laneCount = playlist.getLaneCount();
    
    if (laneIndex < 0 || laneIndex > static_cast<int>(laneCount)) {
        result.accepted = false;
        result.message = "Invalid lane position";
        clearDropPreview();
        return result;
    }
    
    // 2. Resolve target lane
    PlaylistLaneID targetLaneId;
    if (laneIndex == static_cast<int>(laneCount)) {
        // Create new lane if dropping at the end
        targetLaneId = playlist.createLane("Lane " + std::to_string(laneIndex + 1));
        
        // Ensure we also have a mixer channel (we maintain 1:1 mapping for now)
        if (m_trackManager->getChannelCount() <= static_cast<size_t>(laneIndex)) {
            m_trackManager->addChannel("Channel " + std::to_string(m_trackManager->getChannelCount() + 1));
        }
        
        Log::info("[TrackManagerUI] Created new lane " + std::to_string(laneIndex) + " for drop.");
    } else {
        targetLaneId = playlist.getLaneId(laneIndex);
    }

    
    // 3. Handle AudioClip repositioning
    if (data.type == NomadUI::DragDataType::AudioClip) {
        ClipInstanceID clipId = ClipInstanceID::fromString(data.sourceClipIdString);
        
        if (clipId.isValid()) {
            bool moved = playlist.moveClip(clipId, targetLaneId, timePositionBeats);
            
            if (moved) {
                result.accepted = true;
                result.message = "Clip moved to lane " + std::to_string(laneIndex) + " at beat " + std::to_string(timePositionBeats);
                Log::info("[TrackManagerUI] Clip moved via PlaylistModel: " + data.sourceClipIdString);
            } else {
                result.accepted = false;
                result.message = "Could not move clip (collision or error)";
            }
        } else {
            result.accepted = false;
            result.message = "Invalid clip reference";
        }
        
        refreshTracks();
        invalidateCache();
        clearDropPreview();
        refreshTracks();
        invalidateCache();
        clearDropPreview();
        return result;
    }

    // 4. Handle Pattern Drop
    if (data.type == NomadUI::DragDataType::Pattern) {
        PatternID pid;
        
        // Try to extract PatternID from customData
        try {
            if (data.customData.has_value()) {
                pid = std::any_cast<PatternID>(data.customData);
            }
        } catch (...) {
            Log::error("[TrackManagerUI] Failed to cast pattern ID from drag data");
        }

        if (pid.isValid()) {
            auto pattern = m_trackManager->getPatternManager().getPattern(pid);
            if (pattern) {
                double duration = pattern->lengthBeats;
                // Add clip from pattern
                playlist.addClipFromPattern(targetLaneId, pid, timePositionBeats, duration);
                
                result.accepted = true;
                result.message = "Pattern added: " + pattern->name;
                Log::info("[TrackManagerUI] Pattern added to timeline: " + pattern->name);
                
                refreshTracks();
                invalidateCache();
                scheduleTimelineMinimapRebuild();
            } else {
                result.accepted = false;
                result.message = "Pattern not found";
            }
        } else {
            result.accepted = false;
            result.message = "Invalid pattern ID";
        }
        clearDropPreview();
        return result;
    }

    
    // 4. Handle File Drop (New Audio Content)
    if (data.type == NomadUI::DragDataType::File) {
        Log::info("[TrackManagerUI] File drop received: " + data.filePath);
        
        if (!AudioFileValidator::isValidAudioFile(data.filePath)) {
            result.accepted = false;
            result.message = "Unsupported file format";
            Log::warning("[TrackManagerUI] File rejected (validator): " + data.filePath);
            clearDropPreview();
            return result;
        }
        
        // Register file with SourceManager
        auto& sourceManager = m_trackManager->getSourceManager();
        ClipSourceID sourceId = sourceManager.getOrCreateSource(data.filePath);
        ClipSource* source = sourceManager.getSource(sourceId);
        
        if (source) {
            // v3.0: Ensure the source is loaded. 
            // If it's a new source, we decode it immediately (synchronous for now)
            if (!source->isReady()) {
                Log::info("[TrackManagerUI] Decoding new source: " + data.filePath);
                std::vector<float> decodedData;
                uint32_t sampleRate = 0;
                uint32_t numChannels = 0;
                
                if (decodeAudioFile(data.filePath, decodedData, sampleRate, numChannels)) {
                    auto buffer = std::make_shared<AudioBufferData>();
                    buffer->interleavedData = std::move(decodedData);
                    buffer->sampleRate = sampleRate;
                    buffer->numChannels = numChannels;
                    buffer->numFrames = buffer->interleavedData.size() / numChannels;
                    source->setBuffer(buffer);
                } else {
                    Log::error("[TrackManagerUI] Failed to decode file: " + data.filePath);
                }
            }

            Log::info("[TrackManagerUI] Source status: " + std::to_string(sourceId.value) + ", Ready: " + std::to_string(source->isReady()));
            
            if (source->isReady()) {
                // Calculate duration in beats for pattern/clip metadata
                double durationSeconds = source->getDurationSeconds();
                double durationBeats = secondsToBeats(durationSeconds);
                Log::info("[TrackManagerUI] Duration: " + std::to_string(durationSeconds) + "s, beats: " + std::to_string(durationBeats));

                // Create Audio Pattern
                AudioSlicePayload payload;
                payload.audioSourceId = sourceId;
                // Default to one slice encompassing the whole file
                payload.slices.push_back({0.0, static_cast<double>(source->getNumFrames())});
                
                auto& patternManager = m_trackManager->getPatternManager();
                PatternID patternId = patternManager.createAudioPattern(data.displayName, durationBeats, payload);
                
                if (patternId.isValid()) {
                    Log::info("[TrackManagerUI] Pattern created: " + std::to_string(patternId.value));
                    
                    // Add Clip Instance to Playlist
                    ClipInstanceID clipId = playlist.addClipFromPattern(targetLaneId, patternId, timePositionBeats, durationBeats);
                    
                    if (clipId.isValid()) {
                        result.accepted = true;
                        result.message = "Imported: " + data.displayName;
                        Log::info("[TrackManagerUI] Clip added successfully: " + clipId.toString());
                        
                        refreshTracks();
                        invalidateCache();
                        scheduleTimelineMinimapRebuild();
                    } else {
                        result.accepted = false;
                        result.message = "Failed to add clip to playlist";
                        Log::error("[TrackManagerUI] PlaylistModel::addClipFromPattern failed");
                    }
                } else {
                    result.accepted = false;
                    result.message = "Failed to create pattern";
                    Log::error("[TrackManagerUI] PatternManager::createAudioPattern failed");
                }
            } else {
                result.accepted = false;
                result.message = "Audio source not ready";
                Log::warning("[TrackManagerUI] Source exists but not ready (async loading?): " + data.filePath);
            }
        } else {
            result.accepted = false;
            result.message = "Failed to load audio data";
            Log::error("[TrackManagerUI] SourceManager returned null source for: " + data.filePath);
        }
        
        clearDropPreview();
        return result;
    }
    
    result.accepted = false;
    result.message = "Unknown drop type";
    clearDropPreview();
    return result;
}


void TrackManagerUI::clearDropPreview() {
    m_showDropPreview = false;
    m_dropTargetTrack = -1;
    m_dropTargetTime = 0.0;
}

double TrackManagerUI::snapBeatToGrid(double beat) const {
    if (!m_snapEnabled || m_snapSetting == NomadUI::SnapGrid::None) {
        return beat;
    }
    
    double grid = NomadUI::MusicTheory::getSnapDuration(m_snapSetting);
    if (grid <= 0.00001) return beat;
    
    // Round to nearest grid line
    double snappedBeats = std::round(beat / grid) * grid;
    
    return std::max(0.0, snappedBeats);
}

// =============================================================================
// Helper Methods for Drop Target
// =============================================================================

int TrackManagerUI::getTrackAtPosition(float y) const {
    NomadUI::NUIRect bounds = getBounds();
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    
    // Get ruler height and track area start
    // MUST match renderTrackManagerDirect layout exactly:
    // header(38) + horizontalScrollbar(24) + ruler(28)
    float headerHeight = 38.0f;
    float horizontalScrollbarHeight = 24.0f;
    float rulerHeight = 28.0f;
    float trackAreaY = bounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;
    
    // Relative Y position in track area
    float relativeY = y - trackAreaY + m_scrollOffset;
    
    if (relativeY < 0) {
        return -1;  // Above track area
    }
    
    // Calculate track index based on track height + spacing
    int trackIndex = static_cast<int>(relativeY / (m_trackHeight + m_trackSpacing));
    
    return trackIndex;
}

double TrackManagerUI::getTimeAtPosition(float x) const {
    NomadUI::NUIRect bounds = getBounds();
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    
    // Get control area width (where track buttons are)
    float controlAreaWidth = themeManager.getLayoutDimensions().trackControlsWidth;
    float gridStartX = controlAreaWidth + 5;
    
    // Relative X position in grid area
    float relativeX = x - bounds.x - gridStartX + m_timelineScrollOffset;
    
    if (relativeX < 0) {
        return 0.0;  // Before grid start
    }
    
    // Convert pixels to beats, then to seconds
    // pixels / pixelsPerBeat = beats
    // beats / beatsPerMinute * 60 = seconds (but we use BPM = 120 assumed)
    double beats = relativeX / m_pixelsPerBeat;
    double bpm = 120.0;  // TODO: Get actual BPM from transport
    double seconds = (beats / bpm) * 60.0;
    
    return seconds;
}

void TrackManagerUI::renderDropPreview(NomadUI::NUIRenderer& renderer) {
    if (!m_showDropPreview || m_dropTargetTrack < 0) {
        return;
    }
    
    NomadUI::NUIRect bounds = getBounds();
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    
    // Calculate grid area
    float controlAreaWidth = themeManager.getLayoutDimensions().trackControlsWidth;
    float gridStartX = bounds.x + controlAreaWidth + 5;
    
    // Calculate track Y position - MUST match layoutTracks() calculation exactly
    // layoutTracks uses: headerHeight(38) + horizontalScrollbarHeight(24) + rulerHeight(28)
    float headerHeight = 38.0f;
    float horizontalScrollbarHeight = 24.0f;
    float rulerHeight = 28.0f;
    float trackAreaStartY = bounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;
    float trackY = trackAreaStartY + (m_dropTargetTrack * (m_trackHeight + m_trackSpacing)) - m_scrollOffset;
    
    // Calculate X position from time
    double bpm = m_trackManager->getPlaylistModel().getBPM();
    double beats = (m_dropTargetTime * bpm) / 60.0;
    float timeX = gridStartX + static_cast<float>(beats * m_pixelsPerBeat) - m_timelineScrollOffset;
    
    // Draw subtle track highlight (just a hint)
    NomadUI::NUIRect trackHighlight(
        gridStartX,
        trackY,
        bounds.width - controlAreaWidth - 20,
        static_cast<float>(m_trackHeight)
    );
    NomadUI::NUIColor highlightColor(0.733f, 0.525f, 0.988f, 0.08f);  // Very subtle
    renderer.fillRect(trackHighlight, highlightColor);
    
    // Draw clip skeleton preview - EXACT same measurements as real clips
    // Real clips use: y + 2, height - 4 (see TrackUIComponent clippedClipBounds)
    if (timeX >= gridStartX && timeX <= bounds.right() - 20) {
        float previewWidth = 150.0f;  // Reasonable preview width
        
        // Clip skeleton bounds - matches actual clip positioning exactly
        NomadUI::NUIRect clipSkeleton(
            timeX,
            trackY + 2,  // Same as real clip: bounds.y + 2
            previewWidth,
            static_cast<float>(m_trackHeight) - 4  // Same as real clip: bounds.height - 4
        );
        
        // Semi-transparent fill
        NomadUI::NUIColor skeletonFill(0.733f, 0.525f, 0.988f, 0.25f);
        renderer.fillRect(clipSkeleton, skeletonFill);
        
        // Border matching clip style
        NomadUI::NUIColor skeletonBorder(0.733f, 0.525f, 0.988f, 0.7f);
        
        // Top border (thicker, like real clip)
        renderer.drawLine(
            NomadUI::NUIPoint(clipSkeleton.x, clipSkeleton.y),
            NomadUI::NUIPoint(clipSkeleton.x + clipSkeleton.width, clipSkeleton.y),
            2.0f,
            skeletonBorder
        );
        
        // Other borders
        renderer.drawLine(
            NomadUI::NUIPoint(clipSkeleton.x, clipSkeleton.y + clipSkeleton.height),
            NomadUI::NUIPoint(clipSkeleton.x + clipSkeleton.width, clipSkeleton.y + clipSkeleton.height),
            1.0f,
            skeletonBorder.withAlpha(0.5f)
        );
        renderer.drawLine(
            NomadUI::NUIPoint(clipSkeleton.x, clipSkeleton.y),
            NomadUI::NUIPoint(clipSkeleton.x, clipSkeleton.y + clipSkeleton.height),
            1.0f,
            skeletonBorder.withAlpha(0.5f)
        );
        renderer.drawLine(
            NomadUI::NUIPoint(clipSkeleton.x + clipSkeleton.width, clipSkeleton.y),
            NomadUI::NUIPoint(clipSkeleton.x + clipSkeleton.width, clipSkeleton.y + clipSkeleton.height),
            1.0f,
            skeletonBorder.withAlpha(0.5f)
        );
        
        // Name strip at top (like real clips have)
        float nameStripHeight = 16.0f;
        NomadUI::NUIRect nameStrip(
            clipSkeleton.x,
            clipSkeleton.y,
            clipSkeleton.width,
            nameStripHeight
        );
        renderer.fillRect(nameStrip, skeletonBorder.withAlpha(0.6f));
        
        // Get drag data for display name
        auto& dragManager = NomadUI::NUIDragDropManager::getInstance();
        if (dragManager.isDragging()) {
            const auto& dragData = dragManager.getDragData();
            std::string displayName = dragData.displayName;
            if (displayName.length() > 18) {
                displayName = displayName.substr(0, 15) + "...";
            }
            NomadUI::NUIPoint textPos(clipSkeleton.x + 4, clipSkeleton.y + 2);
            renderer.drawText(displayName, textPos, 11.0f, NomadUI::NUIColor(1.0f, 1.0f, 1.0f, 0.9f));
        }
    }
}

void TrackManagerUI::renderDeleteAnimations(NomadUI::NUIRenderer& renderer) {
    if (m_deleteAnimations.empty()) {
        return;
    }
    
    // Update and render each animation
    auto it = m_deleteAnimations.begin();
    while (it != m_deleteAnimations.end()) {
        DeleteAnimation& anim = *it;
        
        // Update progress (assume ~60fps, so ~16ms per frame)
        anim.progress += (1.0f / 60.0f) / anim.duration;
        
        if (anim.progress >= 1.0f) {
            // Animation complete, remove it
            it = m_deleteAnimations.erase(it);
            continue;
        }
        
        // FL Studio style: Subtle red ripple expanding from click point
        
        // Calculate ripple radius (smaller, more subtle)
        float maxRadius = 50.0f;
        float currentRadius = anim.progress * maxRadius;
        
        // Ripple alpha fades out as it expands
        float rippleAlpha = (1.0f - anim.progress) * 0.4f;
        
        // Draw single subtle expanding ring
        if (currentRadius > 0) {
            NomadUI::NUIColor ringColor(1.0f, 0.3f, 0.3f, rippleAlpha);
            
            // Draw a circle using lines
            const int segments = 24;
            for (int i = 0; i < segments; i++) {
                float angle1 = (float)i / segments * 2.0f * 3.14159f;
                float angle2 = (float)(i + 1) / segments * 2.0f * 3.14159f;
                
                NomadUI::NUIPoint p1(
                    anim.rippleCenter.x + std::cos(angle1) * currentRadius,
                    anim.rippleCenter.y + std::sin(angle1) * currentRadius
                );
                NomadUI::NUIPoint p2(
                    anim.rippleCenter.x + std::cos(angle2) * currentRadius,
                    anim.rippleCenter.y + std::sin(angle2) * currentRadius
                );
                
                renderer.drawLine(p1, p2, 1.5f, ringColor);
            }
        }
        
        // Force continuous redraw during animation
        invalidateCache();
        
        ++it;
    }
}

// =============================================================================
// MULTI-SELECTION METHODS
// =============================================================================

void TrackManagerUI::selectTrack(TrackUIComponent* track, bool addToSelection) {
    if (!track) return;
    
    if (!addToSelection) {
        // Clear existing selection first
        clearSelection();
    }
    
    m_selectedTracks.insert(track);
    track->setSelected(true);
    
    std::string trackName = track->getTrack() ? track->getTrack()->getName() : "Unknown";
    Log::info("[TrackManagerUI] Selected track: " + trackName + 
              " (total selected: " + std::to_string(m_selectedTracks.size()) + ")");
    
    invalidateCache();
}

void TrackManagerUI::deselectTrack(TrackUIComponent* track) {
    if (!track) return;
    
    auto it = m_selectedTracks.find(track);
    if (it != m_selectedTracks.end()) {
        m_selectedTracks.erase(it);
        track->setSelected(false);
        
        std::string trackName = track->getTrack() ? track->getTrack()->getName() : "Unknown";
        Log::info("[TrackManagerUI] Deselected track: " + trackName);
        invalidateCache();
    }
}

void TrackManagerUI::clearSelection() {
    for (auto* track : m_selectedTracks) {
        if (track) {
            track->setSelected(false);
        }
    }
    m_selectedTracks.clear();
    
    Log::info("[TrackManagerUI] Cleared all track selection");
    invalidateCache();
}

bool TrackManagerUI::isTrackSelected(TrackUIComponent* track) const {
    return m_selectedTracks.find(track) != m_selectedTracks.end();
}

void TrackManagerUI::selectAllTracks() {
    clearSelection();
    
    for (auto& trackUI : m_trackUIComponents) {
        if (trackUI) {
            m_selectedTracks.insert(trackUI.get());
            trackUI->setSelected(true);
        }
    }
    
    Log::info("[TrackManagerUI] Selected all tracks (" + std::to_string(m_selectedTracks.size()) + ")");
    invalidateCache();
}

// Selection query for looping
std::pair<double, double> TrackManagerUI::getSelectionBeatRange() const {
    // Priority 1: Ruler selection (for looping)
    if (m_hasRulerSelection) {
        double start = std::min(m_rulerSelectionStartBeat, m_rulerSelectionEndBeat);
        double end = std::max(m_rulerSelectionStartBeat, m_rulerSelectionEndBeat);
        return {start, end};
    }
    
    // Priority 2: Single selected clip
    if (m_selectedClipId.isValid() && m_trackManager) {
        const auto* clip = m_trackManager->getPlaylistModel().getClip(m_selectedClipId);
        if (clip) {
            return {clip->startBeat, clip->startBeat + clip->durationBeats};
        }
    }
    
    // Priority 3: Selection box / Multi-selection (future)
    // For now, if no clip is selected, return invalid range
    
    return {0.0, 0.0};
}

} // namespace Audio
} // namespace Nomad
