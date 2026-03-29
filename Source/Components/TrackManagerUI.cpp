// © 2025 Aestra Studios All Rights Reserved. Licensed for personal & educational use only.
#include "TrackManagerUI.h"
#include "MixerChannel.h"
#include <memory>
#include "TrackManager.h"
#include "PluginManager.h"
#include "../AestraUI/Platform/NUIPlatformBridge.h"

#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Core/NUIDragDrop.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../AestraCore/include/AestraLog.h"
#include "AudioFileValidator.h"
#include "MiniAudioDecoder.h"
#include "ClipSource.h"
#include "Commands/SplitClipCommand.h"
#include "Commands/AddClipCommand.h"
#include "Commands/DuplicateClipCommand.h"
#include "Commands/MoveClipCommand.h"
#include "Commands/RemoveClipCommand.h"
#include "Commands/CommandTransaction.h"
#include "../AestraCore/include/AestraUnifiedProfiler.h"
#include <algorithm>
#include <cmath>
#include <map>

// Remotery profiling (conditionally enabled via CMake)
#ifdef AESTRA_ENABLE_REMOTERY
    #include "Remotery.h"
#else
    // Stub macros when Remotery is disabled
    #define rmt_ScopedCPUSample(name, flags) ((void)0)
    #define rmt_BeginCPUSample(name, flags) ((void)0)
    #define rmt_EndCPUSample() ((void)0)
#endif

namespace {

AestraUI::NUIComponent* getRootComponent(AestraUI::NUIComponent* component) {
    AestraUI::NUIComponent* root = component;
    while (root && root->getParent()) {
        root = root->getParent();
    }
    return root;
}

void detachContextMenu(const std::shared_ptr<AestraUI::NUIContextMenu>& menu) {
    if (!menu) return;
    if (auto* parent = menu->getParent()) {
        parent->removeChild(menu);
    }
}

void attachAndShowContextMenu(AestraUI::NUIComponent* owner,
                              const std::shared_ptr<AestraUI::NUIContextMenu>& menu,
                              const AestraUI::NUIPoint& position) {
    if (!owner || !menu) return;
    AestraUI::NUIComponent* root = getRootComponent(owner);
    if (!root) root = owner;
    root->addChild(menu);
    menu->showAt(position);
    root->repaint();
}

} // namespace

namespace Aestra {
namespace Audio {

// =============================================================================
// SECTION: Construction & Destruction
// =============================================================================

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
    m_addTrackIcon = std::make_shared<AestraUI::NUIIcon>(addTrackSvg);
    m_addTrackIcon->setIconSize(16.0f, 16.0f);
    m_addTrackIcon->setColorFromTheme("textPrimary");
    m_addTrackIcon->setColorFromTheme("textPrimary");
    m_addTrackIcon->setVisible(true);
    
    // Create Follow Playhead icon (Right Arrow in box)
    const char* followSvg = R"(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round">
            <path d="M5 12h14M12 5l7 7-7 7"/>
        </svg>
    )";
    m_followPlayheadIcon = std::make_shared<AestraUI::NUIIcon>(followSvg);
    m_followPlayheadIcon->setIconSize(16.0f, 16.0f);
    m_followPlayheadIcon->setVisible(true);
    m_followPlayheadIcon->setColorFromTheme("textSecondary"); // Default off state
    
    // Create scrollbar
    m_scrollbar = std::make_shared<AestraUI::NUIScrollbar>(AestraUI::NUIScrollbar::Orientation::Vertical);
    m_scrollbar->setOnScroll([this](double position) {
        onScroll(position);
    });
    addChild(m_scrollbar);
    
    // Timeline minimap (replaces top horizontal scrollbar).
    m_timelineMinimap = std::make_shared<AestraUI::TimelineMinimapBar>();
    m_timelineMinimap->onRequestCenterView = [this](double centerBeat) { centerTimelineViewAtBeat(centerBeat); };
    m_timelineMinimap->onRequestSetViewStart = [this](double viewStartBeat, bool isFinal) {
        setTimelineViewStartBeat(viewStartBeat, isFinal);
    };
    m_timelineMinimap->onRequestResizeViewEdge =
        [this](AestraUI::TimelineMinimapResizeEdge edge, double anchorBeat, double edgeBeat, bool isFinal) {
            resizeTimelineViewEdgeFromMinimap(edge, anchorBeat, edgeBeat, isFinal);
        };
    m_timelineMinimap->onRequestZoomAround = [this](double anchorBeat, float zoomMultiplier) {
        zoomTimelineAroundBeat(anchorBeat, zoomMultiplier);
    };
    m_timelineMinimap->onModeChanged = [this](AestraUI::TimelineMinimapMode mode) {
        m_minimapMode = mode;
        setDirty(true);
    };
    addChild(m_timelineMinimap);

    // Create UI components for existing tracks
    refreshTracks();

    // Register as observer of the playlist model to handle dynamic changes
    // TODO: addChangeObserver not yet implemented in PlaylistModel
    // m_trackManager->getPlaylistModel().addChangeObserver([this]() {
    //     if (m_suppressPlaylistRefresh) return;
    //     
    //     Log::info("[TrackManagerUI] Playlist model changed, refreshing UI...");
    //     refreshTracks();
    //     invalidateCache();
    //     scheduleTimelineMinimapRebuild();
    //     setDirty(true);
    //     
    //     // Auto-update loop region if in "Project" mode (preset 6)
    //     if (m_loopPreset == 6 && m_trackManager) {
    //         double projectEndBeat = m_trackManager->getPlaylistModel().getTotalDurationBeats();
    //         if (projectEndBeat > 0.001) {
    //             // Update internal loop markers
    //             m_loopStartBeat = 0.0;
    //             m_loopEndBeat = projectEndBeat;
    //             m_loopEnabled = true;
    //             
    //             // Notify audio engine to update loop region
    //             if (m_onLoopRegionUpdate) {
    //                 m_onLoopRegionUpdate(0.0, projectEndBeat);
    //             }
    //             
    //             Log::info("[TrackManagerUI] Project loop auto-updated: 0 to " + std::to_string(projectEndBeat) + " beats");
    //         }
    //     }
    // });

    // Create tool icons
    createToolIcons();
    
    // Register as drop target for drag-and-drop
    // Moved to onUpdate to allow shared_from_this() to work
    // AestraUI::NUIDragDropManager::getInstance().registerDropTarget(this);
}

TrackManagerUI::~TrackManagerUI() {
    // Unregister from drag-drop manager
    AestraUI::NUIDragDropManager::getInstance().unregisterDropTarget(this);
    
    // ⚡ Texture cleanup handled by renderer shutdown
    // Note: NUIRenderer is not a singleton, so manual texture cleanup in destructor
    // is not feasible. The renderer will clean up all textures on shutdown.
    Log::info("TrackManagerUI destroyed");
}

void TrackManagerUI::setPlatformWindow(AestraUI::NUIPlatformBridge* window) {
    m_window = window;
}

// =============================================================================
// SECTION: Toolbar & Tools
// =============================================================================

void TrackManagerUI::createToolIcons() {
    // === POINTER/SELECT TOOL ICON (Simple arrow) ===
    const char* selectSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path d="M5 3 L5 17 L9 13 L12 19 L14 18 L11 12 L16 12 Z" fill="#AAAAAA" stroke="#666666" stroke-width="0.5"/></svg>)";
    m_selectToolIcon = std::make_shared<AestraUI::NUIIcon>(selectSvg);
    
    // === SPLIT TOOL ICON (Scissors) ===
    const char* splitSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><circle cx="6" cy="6" r="3" fill="none" stroke="#FF6B6B" stroke-width="1.5"/><circle cx="6" cy="18" r="3" fill="none" stroke="#FF6B6B" stroke-width="1.5"/><line x1="8" y1="8" x2="18" y2="16" stroke="#AAAAAA" stroke-width="2"/><line x1="8" y1="16" x2="18" y2="8" stroke="#AAAAAA" stroke-width="2"/></svg>)";
    m_splitToolIcon = std::make_shared<AestraUI::NUIIcon>(splitSvg);
    
    // === MULTI-SELECT TOOL ICON (Dashed box) ===
    const char* multiSelectSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><rect x="4" y="6" width="16" height="12" fill="none" stroke="#4FC3F7" stroke-width="2" stroke-dasharray="3,2"/></svg>)";
    m_multiSelectToolIcon = std::make_shared<AestraUI::NUIIcon>(multiSelectSvg);
    
    // === PAINT/STAMP TOOL ICON (Brush/stamp) ===
    const char* paintSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path d="M7 14c-1.66 0-3 1.34-3 3 0 1.31-1.16 2-2 2 .92 1.22 2.49 2 4 2 2.21 0 4-1.79 4-4 0-1.66-1.34-3-3-3zm13.71-9.37l-1.34-1.34a.996.996 0 00-1.41 0L9 12.25 11.75 15l8.96-8.96a.996.996 0 000-1.41z" fill="#BB86FC"/></svg>)";
    m_paintToolIcon = std::make_shared<AestraUI::NUIIcon>(paintSvg);
    
    // === MENU ICON (Hamburger) ===
    const char* menuSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M3 18h18v-2H3v2zm0-5h18v-2H3v2zm0-7v2h18V6H3z"/></svg>)";
    m_menuIcon = std::make_shared<AestraUI::NUIIcon>(menuSvg);
    
    // === VISUAL MOVE CURSOR (4-way arrow) ===
    const char* moveSvg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" xmlns="http://www.w3.org/2000/svg"><path d="M5 9l-3 3 3 3M9 5l3-3 3 3M19 9l3 3-3 3M9 19l3 3 3-3M2 12h20M12 2v20" stroke="#FFFFFF" stroke-opacity="0.9"/></svg>)";
    m_moveCursorIcon = std::make_shared<AestraUI::NUIIcon>(moveSvg);

    Log::info("Tool icons created");
}

void TrackManagerUI::setCurrentTool(PlaylistTool tool) {
    if (m_currentTool != tool) {
        m_currentTool = tool;
        // Split AND Paint tools manage their own cursor visibility
        m_showSplitCursor = (tool == PlaylistTool::Split || tool == PlaylistTool::Paint);
        
        // If switching to Paint tool and clipboard is empty, try to pick selected clip
        if (tool == PlaylistTool::Paint && !m_clipboardClip.id.isValid() && m_selectedClipId.isValid()) {
            copySelectedClip();
        }

        invalidateCache();  // Redraw toolbar with new selection
        
        // Note: System cursor is now always hidden by Main.cpp custom cursor system
        
        const char* toolNames[] = {"Select", "Split", "MultiSelect", "Paint", "Loop", "Draw", "Erase", "Mute", "Slip"};
        Log::info("Active tool changed to: " + std::string(toolNames[static_cast<int>(tool)]));
    }
}

bool TrackManagerUI::isMinimapResizeCursorActive() const {
    return m_timelineMinimap && m_timelineMinimap->isVisible() &&
           m_timelineMinimap->getCursorHint() == AestraUI::TimelineMinimapCursorHint::ResizeHorizontal;
}

bool TrackManagerUI::isCustomCursorActive() const {
    // Check if any custom cursor should be displayed (for exclusive cursor rendering)
    
    // 1. Trim edge hover/active
    for (const auto& trackUI : m_trackUIComponents) {
        if (!trackUI) continue;
        if (trackUI->isHoveringTrimEdge() || trackUI->isTrimming()) {
            return true;
        }
    }
    
    // 2. Split or Paint tool in grid area
    if (m_currentTool == PlaylistTool::Split || m_currentTool == PlaylistTool::Paint) {
        AestraUI::NUIRect bounds = getBounds();
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        const float controlAreaWidth = layout.trackControlsWidth;
        const float gridStartX = bounds.x + controlAreaWidth + 5.0f;
        const float headerHeight = 38.0f;
        const float rulerHeight = 28.0f;
        const float horizontalScrollbarHeight = 24.0f;
        const float trackAreaTop = bounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;

        AestraUI::NUIRect gridBounds(gridStartX, trackAreaTop, bounds.width - controlAreaWidth - 20.0f,
                                     bounds.height - headerHeight - rulerHeight - horizontalScrollbarHeight);
        if (gridBounds.contains(m_lastMousePos)) {
            return true;
        }
    }
    
    // 3. Minimap resize cursor
    if (m_timelineMinimap && m_timelineMinimap->isVisible()) {
        AestraUI::NUIRect minimapBounds = m_timelineMinimap->getBounds();
        if (minimapBounds.contains(m_lastMousePos) &&
            m_timelineMinimap->getCursorHint() == AestraUI::TimelineMinimapCursorHint::ResizeHorizontal) {
            return true;
        }
    }
    
    return false;
}

void TrackManagerUI::setSnapSetting(AestraUI::SnapGrid snap) {
    if (m_snapSetting == snap) return;
    
    m_snapSetting = snap;
    
    // Propagate to tracks
    for (auto& track : m_trackUIComponents) {
        if (track) track->setSnapSetting(snap);
    }
    
    // Request redraw of grid
    m_backgroundNeedsUpdate = true;
    invalidateCache();
    
    Log::info("TrackManager Snap set to: " + AestraUI::MusicTheory::getSnapName(snap));
}

void TrackManagerUI::updateToolbarBounds() {
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    AestraUI::NUIRect bounds = getBounds();

    constexpr float headerHeight = 40.0f; // Unified playlist header strip (Standardized)
    const float innerPad = themeManager.getSpacing("s"); // 8px from edge
    const float buttonSpacing = 6.0f; // Standardized gap for Aestra UI philosophy

    // AESTRA UI Philosophy: Standardized 24px toolbar buttons in 40px headers
    const float buttonSize = 24.0f; 
    const float iconSize = buttonSize;
    
    // Vertical centering for 24px button in 40px header (8px top/bottom padding)
    float currentX = bounds.x + innerPad;
    float currentY = bounds.y + (headerHeight - buttonSize) * 0.5f;
    
    // 1. Menu Icon (Far Left)
    m_menuIconBounds = AestraUI::NUIRect(currentX, currentY, buttonSize, buttonSize);
    currentX += buttonSize + 14.0f; // Extra gap for separator (Aestra UI update)

    // 2. Add Track Button
    m_addTrackBounds = AestraUI::NUIRect(currentX, currentY, buttonSize, buttonSize);
    currentX += buttonSize + (innerPad * 1.5f); // Slightly larger gap to separate major modules

    // 3. Tools Module
    float toolbarX = currentX;
    float toolbarY = currentY;

    const int numTools = 4;  // Select, Split, MultiSelect, Paint
    float toolbarWidth = (iconSize * numTools) + (buttonSpacing * (numTools - 1));
    float toolbarHeight = iconSize;

    m_toolbarBounds = AestraUI::NUIRect(toolbarX, toolbarY, toolbarWidth, toolbarHeight);

    float iconX = toolbarX;
    float iconY = toolbarY;

    m_selectToolBounds = AestraUI::NUIRect(iconX, iconY, iconSize, iconSize);
    iconX += iconSize + buttonSpacing;
    m_splitToolBounds = AestraUI::NUIRect(iconX, iconY, iconSize, iconSize);
    iconX += iconSize + buttonSpacing;
    m_multiSelectToolBounds = AestraUI::NUIRect(iconX, iconY, iconSize, iconSize);
    iconX += iconSize + buttonSpacing;
    m_paintToolBounds = AestraUI::NUIRect(iconX, iconY, iconSize, iconSize);
    iconX += iconSize + (innerPad * 1.5f); // Gap before extras

    // 4. Extras Module
    // Follow Playhead (Toggle)
    m_followPlayheadBounds = AestraUI::NUIRect(iconX, iconY, iconSize, iconSize);
    
    // Dropdowns removed -> Moved to Context Menu
}

// =============================================================================
// SECTION: Rendering
// =============================================================================

void TrackManagerUI::renderToolbar(AestraUI::NUIRenderer& renderer) {
    // Update bounds before rendering
    updateToolbarBounds();

    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const float radius = themeManager.getRadius("m"); // 8px buttons
    const auto activeBg = themeManager.getColor("primary").withAlpha(0.25f);
    const auto hoverBg = themeManager.getColor("hover").withAlpha(0.6f);
    
    // Idle button style (matching transport bar glassBg)
    const auto idleBg = themeManager.getColor("textSecondary").withAlpha(0.15f);
    const auto idleBorder = themeManager.getColor("glassBorder");

    // Menu Button (leftmost)
    {
        auto currentBg = idleBg;
        auto currentBorder = idleBorder;
        if (m_menuHovered) {
            currentBg = themeManager.getColor("glassHover");
            currentBorder = themeManager.getColor("glassBorder");
        }
        renderer.fillRoundedRect(m_menuIconBounds, radius, currentBg);
        renderer.strokeRoundedRect(m_menuIconBounds, radius, 1.0f, currentBorder);
    }
    if (m_menuIcon) {
        const float iconSz = 16.0f;
        AestraUI::NUIPoint center = m_menuIconBounds.center();
        
        // Save current state
        // Rotate around the center of the icon
        renderer.pushTransform(center.x, center.y, m_menuIconRotation, 1.0f);
        
        // Draw centered at local (0,0) - renderer handles the translation to 'center' via pushTransform
        // Note: We use a temporary rect centered at 0,0
        m_menuIcon->setBounds(AestraUI::NUIRect(-iconSz * 0.5f, -iconSz * 0.5f, iconSz, iconSz));
        m_menuIcon->setColorFromTheme("textPrimary");
        m_menuIcon->onRender(renderer);
        
        renderer.popTransform();
    }

    // Separator line (user requested - same as Piano Roll)
    {
        float sepX = m_menuIconBounds.right() + 7.0f; // Center in 14px gap
        renderer.drawLine(
            AestraUI::NUIPoint(sepX, m_menuIconBounds.y + 4), 
            AestraUI::NUIPoint(sepX, m_menuIconBounds.bottom() - 4), 
            1.0f, idleBorder.withAlpha(0.3f)
        );
    }

    // Add-track button (next to menu)
    {
        auto currentBg = idleBg;
        auto currentBorder = idleBorder;
        if (m_addTrackHovered) {
            currentBg = themeManager.getColor("glassHover");
            currentBorder = themeManager.getColor("glassBorder");
        }
        renderer.fillRoundedRect(m_addTrackBounds, radius, currentBg);
        renderer.strokeRoundedRect(m_addTrackBounds, radius, 1.0f, currentBorder);
    }
    if (m_addTrackIcon) {
        const float iconSz = 16.0f;
        AestraUI::NUIRect iconRect(
            std::round(m_addTrackBounds.x + (m_addTrackBounds.width - iconSz) * 0.5f),
            std::round(m_addTrackBounds.y + (m_addTrackBounds.height - iconSz) * 0.5f),
            iconSz, iconSz
        );
        m_addTrackIcon->setBounds(iconRect);
        m_addTrackIcon->setColorFromTheme("textPrimary");
        m_addTrackIcon->onRender(renderer);
    }

    // Helper lambda to draw icon with selection state
    auto drawToolIcon = [&](std::shared_ptr<AestraUI::NUIIcon>& icon, const AestraUI::NUIRect& bounds, PlaylistTool tool, bool hovered) {
        bool isActive = (m_currentTool == tool);
        
        // Always draw background - idle, hover, or active
        auto currentBg = idleBg;
        auto currentBorder = idleBorder;
        
        if (isActive) {
            // Active state: Glassy colored overlay
            currentBg = themeManager.getColor("glassActive");
            currentBorder = themeManager.getColor("primary").withAlpha(0.4f);
        } else if (hovered) {
            // Hover state: True Glass (neutral)
            currentBg = themeManager.getColor("glassHover");
            currentBorder = themeManager.getColor("glassBorder");
        }
        
        renderer.fillRoundedRect(bounds, radius, currentBg);
        renderer.strokeRoundedRect(bounds, radius, 1.0f, currentBorder);
        
        // Draw icon
        if (icon) {
            const float iconSz = 16.0f;
            AestraUI::NUIRect iconRect(
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
    drawToolIcon(m_paintToolIcon, m_paintToolBounds, PlaylistTool::Paint, m_paintToolHovered);

    // Render Follow Playhead Toggle
    bool followActive = m_followPlayhead;
    {
        auto currentBg = idleBg;
        auto currentBorder = idleBorder;
        if (followActive) {
            currentBg = themeManager.getColor("glassActive");
            currentBorder = themeManager.getColor("primary").withAlpha(0.4f);
        } else if (m_followPlayheadHovered) {
            currentBg = themeManager.getColor("glassHover");
            currentBorder = themeManager.getColor("glassBorder");
        }
        renderer.fillRoundedRect(m_followPlayheadBounds, radius, currentBg);
        renderer.strokeRoundedRect(m_followPlayheadBounds, radius, 1.0f, currentBorder);
    }
    
    if (m_followPlayheadIcon) {
        const float iconSz = 16.0f;
        AestraUI::NUIRect iconRect(
            std::round(m_followPlayheadBounds.x + (m_followPlayheadBounds.width - iconSz) * 0.5f),
            std::round(m_followPlayheadBounds.y + (m_followPlayheadBounds.height - iconSz) * 0.5f),
            iconSz, iconSz
        );
        m_followPlayheadIcon->setBounds(iconRect);
        
        if (followActive) {
            m_followPlayheadIcon->setColorFromTheme("accentPrimary");
        } else {
            m_followPlayheadIcon->setColorFromTheme("textSecondary");
        }
        m_followPlayheadIcon->onRender(renderer);
    }

    // Render Loop Dropdown (moved to menu)
    // Render Snap Dropdown (moved to menu)
    // Render Popup Lists (moved to menu)
}

bool TrackManagerUI::handleToolbarClick(const AestraUI::NUIPoint& position) {
    // Log click attempt
    // Log::info("TrackManagerUI::handleToolbarClick at " + std::to_string(position.x) + ", " + std::to_string(position.y));

    if (m_menuIconBounds.contains(position)) {
        Log::info("TrackManagerUI: Menu Icon Clicked! Bounds: " + 
                  std::to_string(m_menuIconBounds.x) + "," + std::to_string(m_menuIconBounds.y));

        // Cleanup previous menu if exists
        if (m_activeContextMenu) {
            Log::info("TrackManagerUI: Closing existing menu");
            detachContextMenu(m_activeContextMenu);
            m_activeContextMenu = nullptr;
        } else {
             Log::info("TrackManagerUI: Creating new context menu");
            // Create context menu
            m_activeContextMenu = std::make_shared<AestraUI::NUIContextMenu>();
            // ... capture rest of logic ...
        }

        // Create context menu (RE-ADDING MISSING LOGIC because I am replacing the block)
        if (!m_activeContextMenu) {
            m_activeContextMenu = std::make_shared<AestraUI::NUIContextMenu>();
        }
        auto menu = m_activeContextMenu;
        
        // === SNAP SUBMENU ===
        auto snapMenu = std::make_shared<AestraUI::NUIContextMenu>();
        auto snaps = AestraUI::MusicTheory::getSnapOptions();
        for (auto snap : snaps) {
            bool isSelected = (snap == m_snapSetting);
            snapMenu->addRadioItem(AestraUI::MusicTheory::getSnapName(snap), "SnapGroup", isSelected, [this, snap]() {
                setSnapSetting(snap);
            });
        }
        menu->addSubmenu("Snap", snapMenu);
        
        // === LOOP SUBMENU ===
        auto loopMenu = std::make_shared<AestraUI::NUIContextMenu>();
        auto addLoopItem = [&](const std::string& name, int id) {
            bool isSelected = (m_loopPreset == id);
            loopMenu->addRadioItem(name, "LoopGroup", isSelected, [this, id, name]() {
                m_loopPreset = id;
                bool loopEnabled = (id != 0);
                double loopStartBeat = 0.0;
                double loopEndBeat = 4.0;
                
                if (id >= 1 && id <= 4) {
                    double loopBars = (id == 1) ? 1.0 : (id == 2) ? 2.0 : (id == 3) ? 4.0 : 8.0;
                    loopEndBeat = loopBars * m_beatsPerBar;
                } else if (id == 5) {
                    auto selection = getSelectionBeatRange();
                    if (selection.second > selection.first) {
                        loopStartBeat = selection.first;
                        loopEndBeat = selection.second;
                        loopEnabled = true;
                    } else {
                        loopEnabled = false;
                        Log::warning("Loop Selection: No valid selection found");
                    }
                } else if (id == 6) {
                    // Project: Loop from 0 to the end of the last clip
                    if (m_trackManager) {
                        double projectEnd = m_trackManager->getPlaylistModel().getTotalDurationBeats();
                        if (projectEnd > 0.001) {
                            loopStartBeat = 0.0;
                            loopEndBeat = projectEnd;
                            loopEnabled = true;
                        } else {
                            loopEnabled = false;
                            Log::warning("Loop Project: No clips found");
                        }
                    }
                }
                
                for (auto& trackUI : m_trackUIComponents) {
                    if (trackUI) {
                        trackUI->setLoopEnabled(loopEnabled);
                        trackUI->setLoopRegion(loopStartBeat, loopEndBeat);
                    }
                }
                
                if (m_onLoopPresetChanged) m_onLoopPresetChanged(id);
                invalidateCache();
                Log::info("Loop preset changed to: " + name);
            });
        };
        
        addLoopItem("Off", 0);
        addLoopItem("1 Bar", 1);
        addLoopItem("2 Bars", 2);
        addLoopItem("4 Bars", 3);
        addLoopItem("8 Bars", 4);
        addLoopItem("Selection", 5);
        addLoopItem("Project", 6);
        menu->addSubmenu("Loop", loopMenu);
        
        // === AUDITION MODE ===
        menu->addSeparator();
        menu->addItem("Send to Audition", [this]() {
            // Get selected track or first track
            if (m_onSendToAudition && m_trackManager) {
                auto& playlist = m_trackManager->getPlaylistModel();
                int trackIndex = 0;
                
                // If we have a selected track, use it
                if (!m_selectedTracks.empty()) {
                    auto* selectedTrack = *m_selectedTracks.begin();
                    for (size_t i = 0; i < m_trackUIComponents.size(); ++i) {
                        if (m_trackUIComponents[i].get() == selectedTrack) {
                            trackIndex = static_cast<int>(i);
                            break;
                        }
                    }
                }
                
                PlaylistLaneID laneId = playlist.getLaneId(trackIndex);
                if (laneId.isValid()) {
                    const PlaylistLane* lane = playlist.getLane(laneId);
                    std::string trackName = lane ? lane->name : ("Track " + std::to_string(trackIndex + 1));
                    // Pass track index instead of lane ID internal value
                    m_onSendToAudition(static_cast<uint32_t>(trackIndex), trackName);
                    Log::info("Sending track to Audition: " + trackName);
                }
            }
        });

        // Show menu below the icon
        menu->showAt(m_menuIconBounds.x, m_menuIconBounds.y + m_menuIconBounds.height);
        
        // Add to parent for rendering (NUIContextMenu handles internal popup management, 
        // but typically needs to be added to a root or managed by a popup manager. 
        // Assuming showAt handles standard NUI popup logic, but usually we need to retain reference 
        // or add it to a layer. If NUIContextMenu destroys itself on close, we are good.
        // For safety in this codebase, adding to children is often required unless using global overlay).
        attachAndShowContextMenu(this, menu, AestraUI::NUIPoint(m_menuIconBounds.x, m_menuIconBounds.y + m_menuIconBounds.height));
        
        return true;
    }

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
    if (m_paintToolBounds.contains(position)) {
        setCurrentTool(PlaylistTool::Paint);
        return true;
    }
    
    if (m_followPlayheadBounds.contains(position)) {
        setFollowPlayhead(!isFollowPlayhead());
        Log::info("Follow Playhead toggled: " + std::string(isFollowPlayhead() ? "ON" : "OFF"));
        setDirty(true);
        return true;
    }

    // Dropdowns handled via menu now

    return false;
}

void TrackManagerUI::renderToolCursor(AestraUI::NUIRenderer& renderer, const AestraUI::NUIPoint& position) {
    // Check if any track is hovering a trim edge - render horizontal resize cursor
    bool isHoveringTrimEdge = false;
    bool isTrimming = false;
    for (auto& trackUI : m_trackUIComponents) {
        if (!trackUI) continue;
        if (trackUI->isHoveringTrimEdge() || trackUI->isTrimming()) {
            isHoveringTrimEdge = true;
            isTrimming = trackUI->isTrimming();
            break;
        }
    }
    
    if (isHoveringTrimEdge) {
        // Render horizontal resize cursor (⬌)
        auto& theme = AestraUI::NUIThemeManager::getInstance();
        AestraUI::NUIColor cursorColor = isTrimming 
            ? theme.getColor("accentCyan") 
            : AestraUI::NUIColor(255, 255, 255, 200);
        
        // Draw left-right arrows (simple ⬌ shape)
        float cx = position.x;
        float cy = position.y;
        float arrowSize = 8.0f;
        
        // Left arrow
        renderer.drawLine({cx - arrowSize * 2, cy}, {cx - arrowSize, cy}, 2.0f, cursorColor);
        renderer.drawLine({cx - arrowSize * 2, cy}, {cx - arrowSize * 1.5f, cy - arrowSize * 0.5f}, 2.0f, cursorColor);
        renderer.drawLine({cx - arrowSize * 2, cy}, {cx - arrowSize * 1.5f, cy + arrowSize * 0.5f}, 2.0f, cursorColor);
        
        // Right arrow
        renderer.drawLine({cx + arrowSize, cy}, {cx + arrowSize * 2, cy}, 2.0f, cursorColor);
        renderer.drawLine({cx + arrowSize * 2, cy}, {cx + arrowSize * 1.5f, cy - arrowSize * 0.5f}, 2.0f, cursorColor);
        renderer.drawLine({cx + arrowSize * 2, cy}, {cx + arrowSize * 1.5f, cy + arrowSize * 0.5f}, 2.0f, cursorColor);
        
        // Center bar
        renderer.drawLine({cx - arrowSize, cy}, {cx + arrowSize, cy}, 2.0f, cursorColor);
        
        return; // Skip other tool cursors when resize cursor is active
    }
    
    // Only render if tool requires custom cursor
    if (m_currentTool != PlaylistTool::Split && m_currentTool != PlaylistTool::Paint) {
        // No tool cursor needed - Main.cpp renders default arrow
        return;
    }
    
    // Calculate grid bounds
    AestraUI::NUIRect bounds = getBounds();
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    float controlAreaWidth = layout.trackControlsWidth;
    float gridStartX = bounds.x + controlAreaWidth + 5;
    float headerHeight = 38.0f;
    float rulerHeight = 28.0f;
    float horizontalScrollbarHeight = 24.0f;
    float trackAreaTop = bounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;
    
    AestraUI::NUIRect gridBounds(gridStartX, trackAreaTop, 
                                 bounds.width - controlAreaWidth - 20.0f, 
                                 bounds.height - headerHeight - rulerHeight - horizontalScrollbarHeight);
    
    // If mouse is outside grid, don't render tool cursor (Main.cpp renders default arrow)
    if (!gridBounds.contains(position)) {
        return;
    }
    
    // Mouse is inside grid - render tool-specific cursor
    // Note: System cursor is always hidden by Main.cpp custom cursor system
    
    // === SPLIT TOOL LOGIC ===
    if (m_currentTool == PlaylistTool::Split) {
        // ... (Existing Split Logic) ...
        float mouseRelX = position.x - gridStartX + m_timelineScrollOffset;
        double mouseBeat = mouseRelX / m_pixelsPerBeat;
        double snappedBeat = snapBeatToGrid(mouseBeat);
        float snappedX = gridStartX + static_cast<float>(snappedBeat * m_pixelsPerBeat - m_timelineScrollOffset);
        
        float lineX = snappedX;
        float lineY = position.y;
        AestraUI::NUIColor splitColor(255, 107, 107, 200);
        float dashLength = 6.0f;
        float gapLength = 4.0f;
        
        for (auto& trackUI : m_trackUIComponents) {
            if (!trackUI) continue;
            const auto& allClipBounds = trackUI->getAllClipBounds();
            for (auto it = allClipBounds.begin(); it != allClipBounds.end(); ++it) {
                const auto& clipBounds = it->second;
                if (clipBounds.contains(AestraUI::NUIPoint(position.x, lineY))) {
                    float lineTop = clipBounds.y;
                    float lineBottom = clipBounds.y + clipBounds.height;
                    float y = lineTop;
                    while (y < lineBottom) {
                        float dashEnd = std::min(y + dashLength, lineBottom);
                        renderer.drawLine(AestraUI::NUIPoint(lineX, y), AestraUI::NUIPoint(lineX, dashEnd), 2.0f, splitColor);
                        y = dashEnd + gapLength;
                    }
                    break;
                }
            }
        }
        
        if (m_splitToolIcon) {
            AestraUI::NUIRect iconRect(lineX - 10, position.y - 10, 20, 20);
            m_splitToolIcon->setBounds(iconRect);
            m_splitToolIcon->onRender(renderer);
        }
        return;
    }

    // === PAINT TOOL LOGIC ===
    if (m_currentTool == PlaylistTool::Paint) {
        // Check if hovering over any clip
        bool hoveringClip = false;
        for (auto& trackUI : m_trackUIComponents) {
             if (!trackUI) continue;
             const auto& allClipBounds = trackUI->getAllClipBounds();
             for (auto it = allClipBounds.begin(); it != allClipBounds.end(); ++it) {
                 if (it->second.contains(position)) {
                     hoveringClip = true;
                     break;
                 }
             }
             if (hoveringClip) break;
        }

        if (hoveringClip) {
            // Render MOVE cursor
            if (m_moveCursorIcon) {
                AestraUI::NUIRect iconRect(position.x - 10, position.y - 10, 20, 20);
                m_moveCursorIcon->setBounds(iconRect);
                m_moveCursorIcon->onRender(renderer);
            }
        } else {
            // Render PAINT cursor
            if (m_paintToolIcon) {
                // Offset slightly so tip is at cursor
                AestraUI::NUIRect iconRect(position.x, position.y - 20, 20, 20);
                m_paintToolIcon->setBounds(iconRect);
                m_paintToolIcon->onRender(renderer);
            }
        }
    }
}

void TrackManagerUI::renderMinimapResizeCursor(AestraUI::NUIRenderer& renderer, const AestraUI::NUIPoint& position)
{
    // === EXCLUSION: Don't render minimap resize cursor if any other custom cursor is active ===
    
    // 1. Check if trim cursor is active (hovering or actively trimming clip edges)
    for (auto& trackUI : m_trackUIComponents) {
        if (!trackUI) continue;
        if (trackUI->isHoveringTrimEdge() || trackUI->isTrimming()) {
            return;  // Trim cursor takes priority
        }
    }
    
    // 2. Split and Paint tools own the cursor while we're over the clip grid.
    if (m_currentTool == PlaylistTool::Split || m_currentTool == PlaylistTool::Paint) {
        AestraUI::NUIRect bounds = getBounds();
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        const float controlAreaWidth = layout.trackControlsWidth;
        const float gridStartX = bounds.x + controlAreaWidth + 5.0f;
        const float headerHeight = 38.0f;
        const float rulerHeight = 28.0f;
        const float horizontalScrollbarHeight = 24.0f;
        const float trackAreaTop = bounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;

        AestraUI::NUIRect gridBounds(gridStartX, trackAreaTop, bounds.width - controlAreaWidth - 20.0f,
                                     bounds.height - headerHeight - rulerHeight - horizontalScrollbarHeight);
        if (gridBounds.contains(position)) {
            return;  // Tool cursor takes priority in grid area
        }
    }

    // 3. Only show minimap resize cursor if mouse is actually OVER the minimap
    if (!m_timelineMinimap || !m_timelineMinimap->isVisible()) {
        return;
    }
    
    // Check if mouse is within minimap bounds
    AestraUI::NUIRect minimapBounds = m_timelineMinimap->getBounds();
    if (!minimapBounds.contains(position)) {
        return;  // Mouse not over minimap - no minimap cursor
    }

    const bool wantsResizeCursor =
        (m_timelineMinimap->getCursorHint() == AestraUI::TimelineMinimapCursorHint::ResizeHorizontal);

    if (!wantsResizeCursor) {
        // No resize cursor needed, Main.cpp renders default arrow
        return;
    }
    
    // Render custom resize cursor (system cursor always hidden by Main.cpp)

    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const AestraUI::NUIColor active = themeManager.getColor("borderActive").withAlpha(0.95f);
    const AestraUI::NUIColor shadow(0.0f, 0.0f, 0.0f, 0.70f);

    const float size = 18.0f;
    const float half = size * 0.5f;
    const float a = 5.0f; // Slightly larger arrow head for crispness

    const float x = std::floor(position.x) + 0.5f; // Align to pixel grid
    const float y = std::floor(position.y) + 0.5f;

    // Crisp White
    const AestraUI::NUIColor white(1.0f, 1.0f, 1.0f, 1.0f);
    // Dark shadow for contrast
    const AestraUI::NUIColor arrowShadow(0.0f, 0.0f, 0.0f, 0.8f);

    // Shadow (outline)
    constexpr float kShadowWidth = 4.0f;
    renderer.drawLine(AestraUI::NUIPoint(x - half, y), AestraUI::NUIPoint(x + half, y), kShadowWidth, arrowShadow);
    renderer.drawLine(AestraUI::NUIPoint(x - half, y), AestraUI::NUIPoint(x - half + a, y - a), kShadowWidth, arrowShadow);
    renderer.drawLine(AestraUI::NUIPoint(x - half, y), AestraUI::NUIPoint(x - half + a, y + a), kShadowWidth, arrowShadow);
    renderer.drawLine(AestraUI::NUIPoint(x + half, y), AestraUI::NUIPoint(x + half - a, y - a), kShadowWidth, arrowShadow);
    renderer.drawLine(AestraUI::NUIPoint(x + half, y), AestraUI::NUIPoint(x + half - a, y + a), kShadowWidth, arrowShadow);

    // Foreground (White)
    constexpr float kLineWidth = 2.0f;
    renderer.drawLine(AestraUI::NUIPoint(x - half, y), AestraUI::NUIPoint(x + half, y), kLineWidth, white);
    renderer.drawLine(AestraUI::NUIPoint(x - half, y), AestraUI::NUIPoint(x - half + a, y - a), kLineWidth, white);
    renderer.drawLine(AestraUI::NUIPoint(x - half, y), AestraUI::NUIPoint(x - half + a, y + a), kLineWidth, white);
    renderer.drawLine(AestraUI::NUIPoint(x + half, y), AestraUI::NUIPoint(x + half - a, y - a), kLineWidth, white);
    renderer.drawLine(AestraUI::NUIPoint(x + half, y), AestraUI::NUIPoint(x + half - a, y + a), kLineWidth, white);
}

void TrackManagerUI::performSplitAtPosition(int laneIndex, double timeSeconds) {
    auto& playlist = m_trackManager->getPlaylistModel();
    PlaylistLaneID laneId = playlist.getLaneId(laneIndex);
    if (!laneId.isValid()) return;

    double bpm = playlist.getBPM();
    double splitBeat = timeSeconds * (bpm / 60.0);
    
    // Snap split position to grid
    splitBeat = snapBeatToGrid(splitBeat);

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
        auto cmd = std::make_shared<Aestra::Audio::SplitClipCommand>(playlist, targetClipId, splitBeat);
        m_trackManager->getCommandHistory().pushAndExecute(cmd);
        
        m_trackManager->markModified();
        refreshTracks();
        invalidateCache();
        Log::info("Successfully split clip at " + std::to_string(timeSeconds) + "s (via Command)");
    }
}

// =============================================================================
// SECTION: Clip Dragging & Operations
// =============================================================================

void TrackManagerUI::startInstantClipDrag(TrackUIComponent* trackComp, ClipInstanceID clipId, const AestraUI::NUIPoint& clickPos) {
    if (!trackComp || !clipId.isValid() || !m_trackManager) return;
    
    m_isDraggingClipInstant = true;
    m_draggedClipTrack = trackComp;
    m_draggedClipId = clipId;
    m_suppressPlaylistRefresh = true; // Suppress full rebuilds for smoothness
    
    auto& playlist = m_trackManager->getPlaylistModel();
    if (const auto* clip = playlist.getClip(clipId)) {
        m_clipOriginalStartTime = clip->startBeat;
        m_clipOriginalLaneId = playlist.findClipLane(clipId);
        
        // Calculate offset (Cursor Beat - Clip Start Beat)
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
        float controlAreaWidth = themeManager.getLayoutDimensions().trackControlsWidth;
        float gridStartX = getBounds().x + controlAreaWidth + 5.0f;
        
        double cursorBeat = (clickPos.x - gridStartX + m_timelineScrollOffset) / m_pixelsPerBeat;
        m_clipDragOffsetBeats = cursorBeat - clip->startBeat;
    }
    
    Log::info("Started instant clip drag");
}

void TrackManagerUI::updateInstantClipDrag(const AestraUI::NUIPoint& currentPos) {
    if (!m_isDraggingClipInstant || !m_trackManager) return;
    
    // Track mouse position for edge-scrolling in onUpdate
    m_lastMousePos = currentPos;
    
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    float controlAreaWidth = themeManager.getLayoutDimensions().trackControlsWidth;
    float gridStartX = getBounds().x + controlAreaWidth + 5.0f;
    
    double cursorBeat = (currentPos.x - gridStartX + m_timelineScrollOffset) / m_pixelsPerBeat;
    
    // Apply relative offset
    double newStartBeat = cursorBeat - m_clipDragOffsetBeats;
    
    // Snap (optional, or always on)
    newStartBeat = snapBeatToGrid(newStartBeat);
    newStartBeat = std::max(0.0, newStartBeat);

    // Live update model
    auto& playlist = m_trackManager->getPlaylistModel();
    
    // Determine target track from Y position
    int targetTrackIndex = getTrackAtPosition(currentPos.y);
    int trackCount = static_cast<int>(m_trackUIComponents.size());
    
    // Clamp to valid tracks
    if (trackCount > 0) {
        targetTrackIndex = std::clamp(targetTrackIndex, 0, trackCount - 1);
        
        auto targetLaneId = playlist.getLaneId(targetTrackIndex);
        if (targetLaneId.isValid()) {
            playlist.moveClip(m_draggedClipId, newStartBeat, targetLaneId);
        }
    } else {
        // Fallback if no tracks? (Unlikely)
        auto laneId = playlist.findClipLane(m_draggedClipId);
        if (laneId.isValid()) {
            playlist.moveClip(m_draggedClipId, newStartBeat, laneId);
        }
    }
    
    // Redraw immediately (GPU cache handles the rest)
    invalidateCache();
}

void TrackManagerUI::finishInstantClipDrag() {
    if (!m_isDraggingClipInstant) return;
    
    Log::info("Finished instant clip drag");
    
    m_isDraggingClipInstant = false;
    m_draggedClipTrack = nullptr;
    m_draggedClipId = ClipInstanceID{};
    m_suppressPlaylistRefresh = false; // Restore normal behavior
    
    // Final refresh to ensure everything is consistent
    refreshTracks();
    invalidateCache();
    scheduleTimelineMinimapRebuild();
}

void TrackManagerUI::cancelInstantClipDrag() {
    if (!m_isDraggingClipInstant) return;
    
    Log::info("Cancelled instant clip drag");
    
    // Revert position using command so it's undoable
    if (m_trackManager && m_draggedClipId.isValid()) {
        auto& playlist = m_trackManager->getPlaylistModel();
        auto laneId = playlist.findClipLane(m_draggedClipId);
        if (laneId.isValid() && m_clipOriginalLaneId.isValid()) {
            auto cmd = std::make_shared<MoveClipCommand>(
                playlist,
                m_draggedClipId,
                m_clipOriginalStartTime,
                m_clipOriginalLaneId
            );
            m_trackManager->getCommandHistory().pushAndExecute(cmd);
        }
    }
    
    m_isDraggingClipInstant = false;
    m_draggedClipTrack = nullptr;
    m_draggedClipId = ClipInstanceID{};
    m_suppressPlaylistRefresh = false;
    
    refreshTracks();
    invalidateCache();
}

void TrackManagerUI::addTrack(const std::string& name) {
    if (m_trackManager) {
        // Create lane in PlaylistModel
        PlaylistLaneID laneId = m_trackManager->getPlaylistModel().createLane(name);
        
        // Create Mixer Channel, linking it to the new lane
        auto channel = m_trackManager->addChannel(name); // Assuming addChannel creates and returns a new channel
        
        // Create UI component for the track, passing both identifiers
        auto trackUI = std::make_shared<TrackUIComponent>(laneId, std::shared_ptr<MixerChannel>(channel, [](MixerChannel*){}), m_trackManager.get());
        
        // Register callback for exclusive solo coordination
        trackUI->setOnSoloToggled([this](TrackUIComponent* soloedTrack) {
            this->onTrackSoloToggled(soloedTrack);
        });
        
        // Register callback for cache invalidation (button hover, etc.)
        trackUI->setOnCacheInvalidationNeeded([this]() {
            this->invalidateCache();
        });
        
        // Register callback for clip deletion with ripple animation
        trackUI->setOnClipDeleted([this](TrackUIComponent* trackComp, ClipInstanceID clipId, AestraUI::NUIPoint ripplePos) {
            this->onClipDeleted(trackComp, clipId, ripplePos);
        });
        
        m_trackUIComponents.push_back(trackUI);
        addChild(trackUI);

        layoutTracks();
        scheduleTimelineMinimapRebuild();
        invalidateCache();  // Invalidate cache when track added
        Log::info("Added track UI: " + name);
    }
}

void TrackManagerUI::refreshTracks() {
    if (!m_trackManager) {
        Log::error("refreshTracks: m_trackManager is null!");
        return;
    }

    Log::info("refreshTracks: starting, laneCount=" + std::to_string(m_trackManager->getPlaylistModel().getLaneCount()));

    // v3.0 logic: iterate over PlaylistModel lanes instead of Mixer channels
    auto& playlist = m_trackManager->getPlaylistModel();
    size_t laneCount = playlist.getLaneCount();
    Log::info("refreshTracks: looping over " + std::to_string(laneCount) + " lanes");

    // Clear existing UI components
    for (auto& trackUI : m_trackUIComponents) {
        removeChild(trackUI);
    }
    m_trackUIComponents.clear();
    for (size_t i = 0; i < laneCount; ++i) {
        auto laneId = playlist.getLaneId(i);
        auto lane = playlist.getLane(laneId);
        if (!lane) {
            Log::warning("refreshTracks: lane " + std::to_string(i) + " is null!");
            continue;
        }
        
        // Find associated MixerChannel (we maintain a 1:1 mapping between lane index and channel index for now)
        auto channel = m_trackManager->getTrack(i);
        if (!channel) {
            Log::warning("refreshTracks: channel " + std::to_string(i) + " is null!");
            continue;
        }

        // Create UI component with LaneID and MixerChannel
        auto trackUI = std::make_shared<TrackUIComponent>(laneId, std::shared_ptr<MixerChannel>(channel, [](MixerChannel*){}), m_trackManager.get());
        
        // Register callbacks
        trackUI->setOnSoloToggled([this](TrackUIComponent* soloedTrack) {
            this->onTrackSoloToggled(soloedTrack);
        });
        
        trackUI->setOnCacheInvalidationNeeded([this]() {
            this->invalidateCache();
        });
        
        trackUI->setOnClipDeleted([this](TrackUIComponent* trackComp, ClipInstanceID clipId, AestraUI::NUIPoint ripplePos) {
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
            // Auto-Picking: Selecting a clip automatically loads it into the clipboard/brush
            copySelectedClip();
        });

        trackUI->setOnTrackSelected([this](TrackUIComponent* trackComp, bool addToSelection) {
            this->selectTrack(trackComp, addToSelection);
        });

        trackUI->setOnSendToAudition([this, i, lane]() {
            if (this->m_onSendToAudition) {
                this->m_onSendToAudition(static_cast<uint32_t>(i), lane->name);
            }
        });

        
        // Sync zoom/scroll settings
        trackUI->setPixelsPerBeat(m_pixelsPerBeat);
        trackUI->setBeatsPerBar(m_beatsPerBar);
        trackUI->setTimelineScrollOffset(m_timelineScrollOffset);
        trackUI->setSnapSetting(m_snapSetting); // Sync snap setting for resize
        
        m_trackUIComponents.push_back(trackUI);
        addChild(trackUI);
    } // Close lane loop

    layoutTracks();
    
    // Mixer strips are now refreshed by AestraContent when syncing state
    
    // Update scrollbar after tracks are refreshed (fixes initial glitch)
    scheduleTimelineMinimapRebuild();
    updateTimelineMinimap(0.0);
    
    invalidateCache();  // Invalidate cache when tracks refreshed
    
    Log::info("refreshTracks: completed, created " + std::to_string(m_trackUIComponents.size()) + " TrackUIs");
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

void TrackManagerUI::onClipDeleted(TrackUIComponent* trackComp, ClipInstanceID clipId, const AestraUI::NUIPoint& rippleCenter) {
    if (!trackComp || !m_trackManager || !clipId.isValid()) return;
    
    auto& playlist = m_trackManager->getPlaylistModel();
    const auto* clip = playlist.getClip(clipId);
    if (!clip) return;

    // Get clip bounds for animation before we delete
    AestraUI::NUIRect clipBounds = trackComp->getBounds(); 

    // Start delete animation
    DeleteAnimation anim;
    anim.laneId = trackComp->getLaneId();
    anim.clipId = clipId;
    anim.rippleCenter = rippleCenter;
    anim.clipBounds = clipBounds;
    anim.progress = 0.0f;
    anim.duration = 0.25f;
    m_deleteAnimations.push_back(anim);
    
    // Core deletion: remove from PlaylistModel using command for undo support
    auto cmd = std::make_shared<RemoveClipCommand>(playlist, clipId);
    m_trackManager->getCommandHistory().pushAndExecute(cmd);
    
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
    
    // Snap split position to grid
    double snappedBeat = snapBeatToGrid(splitBeat);
    
    ClipInstanceID targetClipId;
    for (const auto& clip : lane->clips) {
        if (snappedBeat > clip.startBeat && snappedBeat < clip.startBeat + clip.durationBeats) {
            targetClipId = clip.id;
            break;
        }
    }
    
    if (targetClipId.isValid()) {
        auto cmd = std::make_shared<Aestra::Audio::SplitClipCommand>(playlist, targetClipId, snappedBeat);
        m_trackManager->getCommandHistory().pushAndExecute(cmd);

        refreshTracks();
        invalidateCache();
        scheduleTimelineMinimapRebuild();
        Log::info("[TrackManagerUI] Clip split via Command at beat " + std::to_string(snappedBeat));
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
    AestraUI::NUIRect bounds = getBounds();

    // Get layout dimensions from theme
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
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
        m_timelineMinimap->setBounds(AestraUI::NUIAbsolute(bounds, 0, minimapY, minimapWidth, horizontalScrollbarHeight));
        updateTimelineMinimap(0.0);
    }
    
    // Layout vertical scrollbar (right side, below header, horizontal scrollbar, and ruler)
    if (m_scrollbar) {
        float scrollbarY = headerHeight + horizontalScrollbarHeight + rulerHeight;
        float scrollbarX = bounds.width - scrollbarWidth;
        m_scrollbar->setBounds(AestraUI::NUIAbsolute(bounds, scrollbarX, scrollbarY, scrollbarWidth, viewportHeight));
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
        // AestraUI components use absolute screen coordinates.
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

// =============================================================================
// SECTION: Main Render Entry
// =============================================================================

void TrackManagerUI::onRender(AestraUI::NUIRenderer& renderer) {
    AESTRA_ZONE("TrackMgrUI_Render");
    rmt_ScopedCPUSample(TrackMgrUI_Render, 0);
    
    // Skip rendering if not visible (e.g., when user is on Mixer tab)
    if (!isVisible()) return;
    
    AestraUI::NUIRect bounds = getBounds();
    
    // Normal rendering with FBO CACHING for massive FPS boost! 🚀
    // Cache the entire playlist view except the playhead (which moves every frame)
    
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    
    auto* renderCache = renderer.getRenderCache();
    if (!renderCache) {
        // Fallback: No cache available, render normally
        renderTrackManagerStatic(renderer);
        return;
    }
    
    // === FBO CACHING ENABLED ===
    // Get or create FBO cache (cache entire playlist view area)
    AestraUI::NUISize cacheSize(
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
            
            renderer.clear(AestraUI::NUIColor(0, 0, 0, 0));
            renderer.pushTransform(-bounds.x, -bounds.y);
            renderTrackManagerStatic(renderer);
            renderer.popTransform();
            
            m_isRenderingToCache = false;
        });
    } else {
        renderTrackManagerStatic(renderer);
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
        const AestraUI::NUIRect viewportClip(bounds.x, viewportTop, trackWidth, viewportHeight);

        bool clipEnabled = false;
        if (!viewportClip.isEmpty()) {
            renderer.setClipRect(viewportClip);
            clipEnabled = true;
        }

        const float viewportBottom = viewportTop + viewportHeight;
        
        // Render Dynamic Content (Overlays, Meters, Highlight) - Always real-time outside cache
        renderTrackManagerDynamic(renderer);

        if (clipEnabled) {
            renderer.clearClipRect();
        }
    }
    // Render drop preview OUTSIDE cache (dynamic during drag)
    if (m_showDropPreview) {
        renderDropPreview(renderer);
    }
    
    // Render playhead OUTSIDE cache (it moves every frame during playback)
    renderPlayhead(renderer);

    // Render drag drop preview
    
    // Render delete animations OUTSIDE cache (Ripple effect)
    renderDeleteAnimations(renderer);
    
    // Render scrollbars OUTSIDE cache (they interact with mouse)
    if (m_timelineMinimap && m_timelineMinimap->isVisible()) m_timelineMinimap->onRender(renderer);
    if (m_scrollbar && m_scrollbar->isVisible()) m_scrollbar->onRender(renderer);
    
    // Panels are now handled by OverlayLayer rendering.
    
    // Render toolbar OUTSIDE cache (interactive tool selection)
    renderToolbar(renderer);
    
    // Render tool cursor (Split, Paint, AND trim resize cursor)
    // renderToolCursor handles: trim edges, split tool, paint tool
    renderToolCursor(renderer, m_lastMousePos);

    // Minimap edge-resize cursor (custom overlay) - has internal exclusion for other cursors
    renderMinimapResizeCursor(renderer, m_lastMousePos);
    
    // Render selection box if currently drawing one
    if (m_isDrawingSelectionBox) {
        float minX = std::min(m_selectionBoxStart.x, m_selectionBoxEnd.x);
        float maxX = std::max(m_selectionBoxStart.x, m_selectionBoxEnd.x);
        float minY = std::min(m_selectionBoxStart.y, m_selectionBoxEnd.y);
        float maxY = std::max(m_selectionBoxStart.y, m_selectionBoxEnd.y);
        
        AestraUI::NUIRect selectionRect(minX, minY, maxX - minX, maxY - minY);

        // CLIPPING: Constrain selection to grid area
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
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

        AestraUI::NUIRect gridBounds(gridLeft, gridTop, gridWidth, gridHeight);
        
        // Intersect selection with grid - if no intersection, don't draw
        if (gridBounds.intersects(selectionRect)) {
            // Clip the rect
            float clipX = std::max(selectionRect.x, gridBounds.x);
            float clipY = std::max(selectionRect.y, gridBounds.y);
            float clipR = std::min(selectionRect.right(), gridBounds.right());
            float clipB = std::min(selectionRect.bottom(), gridBounds.bottom());
            
            AestraUI::NUIRect clippedRect(clipX, clipY, clipR - clipX, clipB - clipY);

            // "Glass Tech" Theme Style - POLISHED
            AestraUI::NUIColor accent = themeManager.getColor("accentCyan");
            
            // 1. Vertical Gradient Fill for "Glass" depth
            // Top: More transparent
            // Bottom: Denser
            AestraUI::NUIColor fillTop = accent.withAlpha(0.04f);
            AestraUI::NUIColor fillBottom = accent.withAlpha(0.15f);
            renderer.fillRectGradient(clippedRect, fillTop, fillBottom, true /* vertical */);
            
            // 2. Main Border with Glow
            // Outer Glow (Blurred/Wide)
            renderer.strokeRect(clippedRect, 3.0f, accent.withAlpha(0.25f));
            // Inner Core (Sharp)
            renderer.strokeRect(clippedRect, 1.0f, accent.withAlpha(0.9f));
            
            // 3. Glowing Corners
            // Helper for corner rendering
            auto drawCorner = [&](float x, float y, float w, float h) {
                // Outer Glow
                renderer.fillRect(AestraUI::NUIRect(x - 1, y - 1, w + 2, h + 2), accent.withAlpha(0.5f));
                // Core
                renderer.fillRect(AestraUI::NUIRect(x, y, w, h), accent.withAlpha(1.0f));
            };

            float cornerLen = 6.0f;
            float cornerThick = 2.0f;
            
            // Only draw corners if rect is large enough
            if (clippedRect.width > cornerLen * 2 && clippedRect.height > cornerLen * 2) {
                // Top-Left
                drawCorner(clipX, clipY, cornerLen, cornerThick);
                drawCorner(clipX, clipY, cornerThick, cornerLen);
                
                // Top-Right
                drawCorner(clipR - cornerLen, clipY, cornerLen, cornerThick);
                drawCorner(clipR - cornerThick, clipY, cornerThick, cornerLen);
                
                // Bottom-Left
                drawCorner(clipX, clipB - cornerThick, cornerLen, cornerThick);
                drawCorner(clipX, clipB - cornerLen, cornerThick, cornerLen);
                
                // Bottom-Right
                drawCorner(clipR - cornerLen, clipB - cornerThick, cornerLen, cornerThick);
                drawCorner(clipR - cornerThick, clipB - cornerLen, cornerThick, cornerLen);
            }
        }
    }

    // Render Context Menu LAST (Topmost Z-order, not clipped)
    if (m_activeContextMenu && m_activeContextMenu->isVisible()) {
        m_activeContextMenu->onRender(renderer);
    }
}

// Helper method: Static content (used for cache)
void TrackManagerUI::renderTrackManagerStatic(AestraUI::NUIRenderer& renderer) {
    AestraUI::NUIRenderer& r = renderer; 

    AestraUI::NUIRect bounds = getBounds();
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    
    // Calculate where the grid/background should end
    float controlAreaWidth = layout.trackControlsWidth;
    float gridStartX = controlAreaWidth + 5;
    
    // Draw background (control area + full grid area - no bounds restriction)
    // Deep Space Background (Explicitly dark to avoid missing-token pinks)
    AestraUI::NUIColor bgColor = AestraUI::NUIColor::fromHex(0x050508); // Deep Void
    
    if (m_playlistVisible) {
        // Background for control area (always visible)
        AestraUI::NUIRect controlBg(bounds.x, bounds.y, controlAreaWidth, bounds.height);
        renderer.fillRect(controlBg, bgColor);
        
        // Background for grid area (match track background; zebra grid provides contrast)
        float scrollbarWidth = 15.0f;
        float gridWidth = bounds.width - controlAreaWidth - scrollbarWidth - 5;
        AestraUI::NUIRect gridBg(bounds.x + gridStartX, bounds.y, gridWidth, bounds.height);
        renderer.fillRect(gridBg, bgColor);
        
        // Draw border
        AestraUI::NUIColor borderColor = themeManager.getColor("border");
        renderer.strokeRect(bounds, 1, borderColor);
        
        // Draw Grid (Restored and moved after background)
        drawGrid(renderer, bounds, gridStartX, gridWidth, m_timelineScrollOffset);
    }
    
    // Header/Track Count (Static)
    float headerAvailableWidth = bounds.width;
    if (m_playlistVisible) {
        std::string infoText = "Tracks: " + std::to_string(m_trackManager ? m_trackManager->getTrackCount() - (m_trackManager->getTrackCount() > 0 ? 1 : 0) : 0);  // Exclude preview track
        const float infoFont = 12.0f;
        auto infoSize = renderer.measureText(infoText, infoFont);

        // Ensure text doesn't exceed available width and position with proper margin
        float margin = layout.panelMargin;
        float maxTextWidth = headerAvailableWidth - 2 * margin;
        if (infoSize.width > maxTextWidth) {
            std::string truncatedText = infoText;
            while (!truncatedText.empty() && renderer.measureText(truncatedText, infoFont).width > maxTextWidth) {
                truncatedText = truncatedText.substr(0, truncatedText.length() - 1);
            }
            infoText = truncatedText + "...";
            infoSize = renderer.measureText(infoText, infoFont);
        }

        const float headerHeight = 38.0f;
        const AestraUI::NUIRect headerBounds(bounds.x, bounds.y, headerAvailableWidth, headerHeight);
        const float rightPad = layout.panelMargin + 18.0f;
        const float textX = std::max(headerBounds.x + margin, headerBounds.right() - infoSize.width - rightPad);
        const float textY = std::round(renderer.calculateTextY(headerBounds, infoFont));

        renderer.drawText(infoText, AestraUI::NUIPoint(textX, textY), infoFont, themeManager.getColor("textPrimary"));
    }

    // Render Static Track Content (with Viewport Culling AND Clipping)
    const float headerHeight = 38.0f;
    const float horizontalScrollbarHeight = 24.0f;
    const float rulerHeight = 28.0f;
    const float scrollbarWidth = 15.0f;
    
    // Calculate viewport bounds for culling
    const float viewportTop = bounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;
    const float viewportHeight = std::max(0.0f, bounds.height - headerHeight - horizontalScrollbarHeight - rulerHeight);
    const float viewportBottom = viewportTop + viewportHeight;
    const float trackWidth = std::max(0.0f, bounds.width - scrollbarWidth);
    
    // Set clip rect to prevent tracks from rendering behind ruler/header
    AestraUI::NUIRect trackClipRect(bounds.x, viewportTop, trackWidth, viewportHeight);
    bool clipEnabled = false;
    if (!trackClipRect.isEmpty()) {
        renderer.setClipRect(trackClipRect);
        clipEnabled = true;
    }

    for (size_t i = 0; i < m_trackUIComponents.size(); ++i) {
        auto track = m_trackUIComponents[i];
        if (!track || !track->isVisible()) continue;
        
        // Culling: Skip tracks outside the visible viewport
        const auto trackBounds = track->getBounds();
        if (trackBounds.bottom() < viewportTop || trackBounds.y > viewportBottom) continue;

        // Zebra Striping (Handled here for guaranteed ordering/visibility)
        if (i % 2 == 0) {
            // Even rows: overlay for separation (8% white opacity)
            renderer.fillRect(trackBounds, AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.08f));
        }

        track->renderStatic(renderer);
    }
    
    // Clear clip rect before drawing header/ruler (they should draw fully)
    if (clipEnabled) {
        renderer.clearClipRect();
    }
    
    // Header Bar (Static overlay)
    float headerWidth = bounds.width;
    if (m_playlistVisible) {
        AestraUI::NUIColor bgColor = themeManager.getColor("backgroundPrimary");
        AestraUI::NUIColor borderColor = themeManager.getColor("border");

        float headerHeight = 38.0f;
        AestraUI::NUIRect headerRect(bounds.x, bounds.y, headerWidth, headerHeight);
        renderer.fillRect(headerRect, bgColor);
        renderer.strokeRect(headerRect, 1, borderColor);
        
        // Draw time ruler below header
        float rulerHeight = 28.0f;
        float horizontalScrollbarHeight = 24.0f;
        AestraUI::NUIRect rulerRect(bounds.x, bounds.y + headerHeight + horizontalScrollbarHeight, headerWidth, rulerHeight);
        renderTimeRuler(renderer, rulerRect);
        renderLoopMarkers(renderer, rulerRect);
    }
}

// Helper method: Dynamic content (Meters, Overlays, Selection)
void TrackManagerUI::renderTrackManagerDynamic(AestraUI::NUIRenderer& renderer) {
    AestraUI::NUIRect bounds = getBounds();
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();

    if (m_playlistVisible) {
        const float headerHeight = 38.0f;
        const float horizontalScrollbarHeight = 24.0f;
        const float rulerHeight = 28.0f;
        const float scrollbarWidth = 15.0f;

        const float viewportTop = bounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;
        const float viewportHeight = std::max(0.0f, bounds.height - headerHeight - horizontalScrollbarHeight - rulerHeight);
        const float trackWidth = std::max(0.0f, bounds.width - scrollbarWidth);
        const AestraUI::NUIRect viewportClip(bounds.x, viewportTop, trackWidth, viewportHeight);

        bool clipEnabled = false;
        if (!viewportClip.isEmpty()) {
            renderer.setClipRect(viewportClip);
            clipEnabled = true;
        }

        const float viewportBottom = viewportTop + viewportHeight;

        // Render Dynamic Content Loops (Buttons/Meters + Grid/Highlights)
        // Note: Controls are rendered in onRender loop too? 
        // No, we removed the loop in step 1273 and replaced with call to renderTrackManagerDynamic.
        // Wait, did we?
        // Yes, renderTrackManagerDynamic(renderer) call was inserted.
        // So this loop handles BOTH Controls and Dynamic Overlays.
        
        for (const auto& trackUI : m_trackUIComponents) {
            if (!trackUI || !trackUI->isVisible() || !trackUI->isPrimaryForLane()) continue;
            
            // Culling
            const auto trackBounds = trackUI->getBounds();
            if (trackBounds.bottom() < viewportTop || trackBounds.y > viewportBottom) continue;
            
            trackUI->renderControlOverlay(renderer); // Left Side: Buttons + Meters
            trackUI->renderDynamic(renderer);        // Right Side: Grid Overlays
        }

        if (clipEnabled) {
            renderer.clearClipRect();
        }
    } // End of Dynamic Content Loop

    // Render Pending Imports (Holographic Visualizer)
    renderPendingImports(renderer);

    // === GRID SELECTION HIGHLIGHT ===
    if ((m_isDraggingRulerSelection || m_hasRulerSelection) && m_playlistVisible) {
        double selStartBeat = std::min(m_rulerSelectionStartBeat, m_rulerSelectionEndBeat);
        double selEndBeat = std::max(m_rulerSelectionStartBeat, m_rulerSelectionEndBeat);
        
        float controlAreaWidth = layout.trackControlsWidth;
        float gridStartX = bounds.x + controlAreaWidth + 5.0f;
        
        float selStartX = gridStartX + static_cast<float>(selStartBeat * m_pixelsPerBeat) - m_timelineScrollOffset;
        float selEndX = gridStartX + static_cast<float>(selEndBeat * m_pixelsPerBeat) - m_timelineScrollOffset;
        
        float headerHeight = 38.0f;
        float rulerHeight = 28.0f;
        float horizontalScrollbarHeight = 24.0f;
        float trackAreaTop = bounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;
        float trackAreaHeight = bounds.height - (headerHeight + horizontalScrollbarHeight + rulerHeight);
        
        float scrollbarWidth = 15.0f;
        float gridWidth = bounds.width - controlAreaWidth - scrollbarWidth - 5.0f;
        float gridEndX = gridStartX + gridWidth;
        
        if (selEndX >= gridStartX && selStartX <= gridEndX) {
            float visibleStartX = std::max(selStartX, gridStartX);
            float visibleEndX = std::min(selEndX, gridEndX);
            float selectionWidth = visibleEndX - visibleStartX;
            
            if (selectionWidth > 0.0f) {
                AestraUI::NUIRect selectionRect(visibleStartX, trackAreaTop, selectionWidth, trackAreaHeight);
                auto accentColor = themeManager.getColor("accentPrimary");
                renderer.fillRect(selectionRect, accentColor.withAlpha(0.10f));
                
                if (selStartX >= gridStartX && selStartX <= gridEndX) {
                    renderer.drawLine(AestraUI::NUIPoint(selStartX, trackAreaTop), AestraUI::NUIPoint(selStartX, trackAreaTop + trackAreaHeight), 1.0f, accentColor.withAlpha(0.30f));
                }
                if (selEndX >= gridStartX && selEndX <= gridEndX) {
                    renderer.drawLine(AestraUI::NUIPoint(selEndX, trackAreaTop), AestraUI::NUIPoint(selEndX, trackAreaTop + trackAreaHeight), 1.0f, accentColor.withAlpha(0.30f));
                }
            }
        }
    }
}



    

    

    




    


    


void TrackManagerUI::renderChildren(AestraUI::NUIRenderer& renderer) {
    // ðŸ”¥ VIEWPORT CULLING: Only render visible tracks + always render controls
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    AestraUI::NUIRect bounds = getBounds();
    
    const float headerHeight = 38.0f;
    const float rulerHeight = 28.0f;
    const float horizontalScrollbarHeight = 24.0f;
    const float scrollbarWidth = 15.0f;

    const float viewportHeight = std::max(0.0f, bounds.height - headerHeight - horizontalScrollbarHeight - rulerHeight);
    const float viewportTopAbs = bounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;
    const float viewportBottomAbs = viewportTopAbs + viewportHeight;
    const float trackWidth = std::max(0.0f, bounds.width - scrollbarWidth);

    AestraUI::NUIRect viewportClip(bounds.x, viewportTopAbs, trackWidth, viewportHeight);
    // Note: setClipRect handles FBO transforms automatically.
    
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
        if (child == m_scrollbar || child == m_timelineMinimap || child == m_activeContextMenu) {
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
    // Process pending main-thread tasks (e.g., from async loaders)
    {
        std::vector<std::function<void()>> tasks;
        {
             std::lock_guard<std::mutex> lock(m_pendingTasksMutex);
             if (!m_pendingTasks.empty()) {
                 tasks.swap(m_pendingTasks);
             }
        }
        for (auto& task : tasks) {
            if (task) task();
        }
    }

    // One-time registration for drag-and-drop
    // We do this here because shared_from_this() is not available in the constructor
    if (!m_dropTargetRegistered) {
        try {
            // Ensure we are managed by a shared_ptr before calling shared_from_this()
            auto sharedThis = std::dynamic_pointer_cast<AestraUI::IDropTarget>(shared_from_this());
            if (sharedThis) {
                AestraUI::NUIDragDropManager::getInstance().registerDropTarget(sharedThis);
                m_dropTargetRegistered = true;
            }
        } catch (const std::bad_weak_ptr&) {
            // Object might be stack-allocated or not yet managed by shared_ptr
            // We'll try again next frame or fail silently if it never happens
        }
    }

    NUIComponent::onUpdate(deltaTime);

    // Animate Menu Icon Rotation
    float targetRot = m_activeContextMenu ? 90.0f : 0.0f;
    float diff = targetRot - m_menuIconRotation;

    // Debug logging (throttled)
    static double logTimer = 0.0;
    logTimer += deltaTime;
    if (logTimer > 1.0) {
        if (m_activeContextMenu) {
             Log::info("TrackManagerUI::onUpdate - Menu Active. Rot: " + std::to_string(m_menuIconRotation));
        }
        logTimer = 0.0;
    }
    
    // Smooth lerp toward target
    if (std::abs(diff) > 0.5f) {
        // Adjust speed here (higher = faster)
        m_menuIconRotation += diff * 10.0f * static_cast<float>(deltaTime); 
        setDirty(true);
    } else {
        m_menuIconRotation = targetRot;
    }
    
    // Smooth zoom animation
    if (std::abs(m_targetPixelsPerBeat - m_pixelsPerBeat) > 0.01f) {
        // Get control area width for zoom pivot calculation
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        float controlAreaWidth = layout.trackControlsWidth;
        float gridStartX = controlAreaWidth + 5;
        float gridWidthPx = getTimelineGridWidthPixels();
        
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
        float newScrollOffset = newWorldUnderMouse - (m_lastMouseZoomX - gridStartX);
        
        // Clamp scroll to domain bounds
        double maxStartBeat = std::max(0.0, m_minimapDomainEndBeat - (gridWidthPx / m_pixelsPerBeat));
        m_timelineScrollOffset = std::clamp(newScrollOffset, 0.0f, static_cast<float>(maxStartBeat * m_pixelsPerBeat));
        
        // Sync to all tracks
        for (auto& trackUI : m_trackUIComponents) {
            trackUI->setPixelsPerBeat(m_pixelsPerBeat);
            trackUI->setTimelineScrollOffset(m_timelineScrollOffset);
        }
        
        invalidateCache();  // Full cache invalidation for smooth zoom animation
    }

    // === Follow Playhead Logic (Page & Continuous) ===
    if (m_followPlayhead && m_trackManager && m_trackManager->isPlaying()) {
        if (!AestraUI::NUIDragDropManager::getInstance().isDragging() && 
            !m_isDraggingPlayhead && !m_isDraggingRulerSelection && 
            !m_isDraggingLoopStart && !m_isDraggingLoopEnd) {
            
            double currentBeat = secondsToBeats(m_trackManager->getUIPosition());
            float gridWidth = getTimelineGridWidthPixels();
            
            if (gridWidth > 0 && m_pixelsPerBeat > 0) {
                double viewStartBeat = m_timelineScrollOffset / m_pixelsPerBeat;
                double viewWidthBeats = gridWidth / m_pixelsPerBeat;
                double viewEndBeat = viewStartBeat + viewWidthBeats;
                
                if (m_followMode == FollowMode::Page) {
                    // === PAGE SCROLLING ===
                    // If playhead reaches 95% of the screen, scroll to next page
                    double rightMargin = viewWidthBeats * 0.05;
                    
                    if (currentBeat >= viewEndBeat - rightMargin) {
                        double newStart = currentBeat - rightMargin;
                        setTimelineViewStartBeat(newStart, true);
                    } else if (currentBeat < viewStartBeat) {
                        // Loop jump back
                        double newStart = currentBeat - rightMargin;
                        setTimelineViewStartBeat(std::max(0.0, newStart), true);
                    }
                } else {
                    // === CONTINUOUS SCROLLING ===
                    // Keep playhead centered
                    double targetStart = currentBeat - (viewWidthBeats * 0.5);
                    
                    // Smooth lerp for continuous follow (optional, but direct set is more responsive)
                    // Directly setting it creates the "locked" feel
                    setTimelineViewStartBeat(std::max(0.0, targetStart), true);
                }
            }
        }
    }

    // Update animation for pending imports
    {
        bool hasImports = !m_pendingImports.empty();
        static bool lastHadImports = false;
        if (hasImports) {
            // Reset all tracks loading state first (to avoid stale loading icons on lanes)
            for (auto& trackUI : m_trackUIComponents) trackUI->setLoading(false);

            for (auto& item : m_pendingImports) {
                item.animationTime += static_cast<float>(deltaTime);
                
                // Sync to track UI
                for (auto& trackUI : m_trackUIComponents) {
                    if (trackUI->getLaneId() == item.laneId) {
                        trackUI->setLoading(true, item.progress);
                        break;
                    }
                }
            }
            setDirty(true);
            lastHadImports = true;
        } else if (lastHadImports) {
            // Ensure all tracks reset when all imports finished
            for (auto& trackUI : m_trackUIComponents) trackUI->setLoading(false);
            lastHadImports = false;
        }
    }

    // === EDGE-SCROLLING DURING CLIP DRAG ===
    // When dragging a clip near the edges of the grid, auto-scroll the view
    if (m_isDraggingClipInstant && m_pixelsPerBeat > 0) {
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        float controlAreaWidth = layout.trackControlsWidth;
        float scrollbarWidth = 15.0f;
        
        AestraUI::NUIRect bounds = getBounds();
        float gridStartX = bounds.x + controlAreaWidth + 5.0f;
        float gridEndX = bounds.x + bounds.width - scrollbarWidth;
        float gridWidth = gridEndX - gridStartX;
        
        // Edge zone size (pixels from edge where scrolling activates)
        const float edgeZone = 60.0f;
        // Base scroll speed (beats per second)
        const float scrollSpeed = 8.0f;
        
        float mouseX = m_lastMousePos.x;
        float scrollDelta = 0.0f;
        
        // Check left edge
        if (mouseX < gridStartX + edgeZone && mouseX >= gridStartX - 20.0f) {
            // Proximity factor: closer to edge = faster scroll
            float proximity = 1.0f - ((mouseX - gridStartX) / edgeZone);
            proximity = std::clamp(proximity, 0.0f, 1.0f);
            scrollDelta = -scrollSpeed * proximity * static_cast<float>(deltaTime);
        }
        // Check right edge
        else if (mouseX > gridEndX - edgeZone && mouseX <= gridEndX + 20.0f) {
            float proximity = 1.0f - ((gridEndX - mouseX) / edgeZone);
            proximity = std::clamp(proximity, 0.0f, 1.0f);
            scrollDelta = scrollSpeed * proximity * static_cast<float>(deltaTime);
        }
        
        if (std::abs(scrollDelta) > 0.001f) {
            double currentStartBeat = m_timelineScrollOffset / m_pixelsPerBeat;
            double newStartBeat = currentStartBeat + scrollDelta;
            
            // Clamp to valid range (can't scroll before beat 0)
            newStartBeat = std::max(0.0, newStartBeat);
            
            // Apply the scroll
            setTimelineViewStartBeat(newStartBeat, false);
            
            // Also update the clip drag position to follow the scroll
            updateInstantClipDrag(m_lastMousePos);
        }
    }

    updateTimelineMinimap(deltaTime);
}

void TrackManagerUI::onResize(int width, int height) {
    // Update cached dimensions before layout/cache update
    m_backgroundCachedWidth = width;
    m_backgroundCachedHeight = height;
    invalidateCache();  // Full invalidation on resize for immediate repaint
    
    layoutTracks();
    // Zebra Striping: Assign row index to tracks
    for (size_t i = 0; i < m_trackUIComponents.size(); ++i) {
        if (m_trackUIComponents[i]) {
            m_trackUIComponents[i]->setRowIndex(static_cast<int>(i));
        }
    }
    AestraUI::NUIComponent::onResize(width, height);
}

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
        if (!track || !track->isVisible()) continue;
        
        // If track is in the middle of an automation drag operation, force-feed it the event
        if (track->isDraggingAutomation()) { 
             // Pass event with global coordinates since TrackUIComponent expects global coords
             if (track->onMouseEvent(event)) return true;
        }
    }

    // Claim keyboard focus on click so keyboard routing moves off the file browser.
    if (event.pressed && event.button == AestraUI::NUIMouseButton::Left && bounds.contains(event.position)) {
        setFocused(true);
    }
    
    // Update toolbar bounds before checking hover (critical!)
    updateToolbarBounds();
    
    // Update toolbar hover states
    bool oldMenuHovered = m_menuHovered;
    bool oldAddHovered = m_addTrackHovered;
    bool oldSelectHovered = m_selectToolHovered;
    bool oldSplitHovered = m_splitToolHovered;
    bool oldMultiSelectHovered = m_multiSelectToolHovered;
    bool oldFollowHovered = m_followPlayheadHovered;
    
    m_menuHovered = m_menuIconBounds.contains(event.position);
    m_addTrackHovered = m_addTrackBounds.contains(event.position);
    m_selectToolHovered = m_selectToolBounds.contains(event.position);
    m_splitToolHovered = m_splitToolBounds.contains(event.position);
    m_multiSelectToolHovered = m_multiSelectToolBounds.contains(event.position);
    m_followPlayheadHovered = m_followPlayheadBounds.contains(event.position);
    
    // Toolbar Tooltips
    bool anyToolbarHovered = m_menuHovered || m_addTrackHovered || m_selectToolHovered || 
                             m_splitToolHovered || m_multiSelectToolHovered || m_followPlayheadHovered;
    bool anyOldHovered = oldMenuHovered || oldAddHovered || oldSelectHovered || 
                         oldSplitHovered || oldMultiSelectHovered || oldFollowHovered;
    
    if (anyToolbarHovered) {
        std::string tooltipText;
        if (m_menuHovered && !oldMenuHovered) tooltipText = "Menu";
        else if (m_addTrackHovered && !oldAddHovered) tooltipText = "Add Track";
        else if (m_selectToolHovered && !oldSelectHovered) tooltipText = "Select Tool (V)";
        else if (m_splitToolHovered && !oldSplitHovered) tooltipText = "Split Tool (B)";
        else if (m_multiSelectToolHovered && !oldMultiSelectHovered) tooltipText = "Multi-Select Tool";
        else if (m_followPlayheadHovered && !oldFollowHovered) tooltipText = "Follow Playhead";
        
        if (!tooltipText.empty()) {
            AestraUI::NUIComponent::showRemoteTooltip(tooltipText, event.position);
        }
    } else if (anyOldHovered && !anyToolbarHovered) {
        AestraUI::NUIComponent::hideRemoteTooltip();
    }
    
    // Toolbar is rendered outside the playlist cache; don't invalidate the cache on hover.
    if (m_menuHovered != oldMenuHovered || m_addTrackHovered != oldAddHovered ||
        m_selectToolHovered != oldSelectHovered || m_splitToolHovered != oldSplitHovered ||
        m_multiSelectToolHovered != oldMultiSelectHovered || m_followPlayheadHovered != oldFollowHovered) {
        setDirty(true);
    }
    
    // === CONTEXT MENU Handling ===
    // Special handling for Right-Click on Follow Button
    if (event.pressed && event.button == AestraUI::NUIMouseButton::Right && m_followPlayheadBounds.contains(event.position)) {
        
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
        
        attachAndShowContextMenu(this, menu, AestraUI::NUIPoint(m_followPlayheadBounds.x, m_followPlayheadBounds.y + m_followPlayheadBounds.height));
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
             // Let execution continue so the click can interact with whatever is underneath
             // (e.g. Stop button, Track header, etc.)
         } else if (handled) {
             // Menu handled the event, consume it.
             return true;
         }
    }
    
    // === DROPDOWNS removed (now via Context Menu) ===
    
    // Handle toolbar clicks (icons only, not dropdowns - they handled themselves above)
    if (event.pressed && event.button == AestraUI::NUIMouseButton::Left) {
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

    // Allow children (clips) to handle right-click press first.
    // Right-click on a clip deletes it; only start selection
    // box if nothing underneath handled the event.
    if (event.pressed && event.button == AestraUI::NUIMouseButton::Right) {
        if (AestraUI::NUIComponent::onMouseEvent(event)) {
            return true;
        }
    }
    

    
    // === SELECTION BOX: Right-click drag or MultiSelect tool or Ctrl+LeftClick ===
    // Start selection box on right-click drag OR left-click with MultiSelect tool OR Ctrl+LeftClick
    bool ctrlHeld = (event.modifiers & AestraUI::NUIModifiers::Ctrl);
    bool startSelectionBox = (event.pressed && event.button == AestraUI::NUIMouseButton::Right) ||
                             (event.pressed && event.button == AestraUI::NUIMouseButton::Left && 
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
            
            // Note: System cursor is always hidden by Main.cpp custom cursor system
            return true;
        }
    }
    
    // Update selection box while dragging
    if (m_isDrawingSelectionBox) {
        if (m_window) {
            // Calculate constrained cursor position
            auto& themeManager = AestraUI::NUIThemeManager::getInstance();
            const auto& layout = themeManager.getLayoutDimensions();
            
            float headerHeight = 38.0f;
            float rulerHeight = 28.0f;
            float horizontalScrollbarHeight = 24.0f;
            float controlAreaWidth = layout.trackControlsWidth;
            float scrollbarWidth = 15.0f;
            
            AestraUI::NUIRect globalBounds = getBounds();
            
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
        bool endSelectionBox = (event.released && event.button == AestraUI::NUIMouseButton::Right) ||
                               (event.released && event.button == AestraUI::NUIMouseButton::Left);
        
        if (endSelectionBox) {
            // Calculate selection rectangle
            float minX = std::min(m_selectionBoxStart.x, m_selectionBoxEnd.x);
            float maxX = std::max(m_selectionBoxStart.x, m_selectionBoxEnd.x);
            float minY = std::min(m_selectionBoxStart.y, m_selectionBoxEnd.y);
            float maxY = std::max(m_selectionBoxStart.y, m_selectionBoxEnd.y);
            
            AestraUI::NUIRect selectionRect(minX, minY, maxX - minX, maxY - minY);
            
            // Select all tracks that intersect with selection box
            clearSelection();
            for (auto& trackUI : m_trackUIComponents) {
                if (trackUI->getBounds().intersects(selectionRect)) {
                    selectTrack(trackUI.get(), true);
                }
            }
            
            // Note: System cursor is always hidden by Main.cpp custom cursor system
            
            m_isDrawingSelectionBox = false;
            invalidateCache();
            
            Log::info("Selection box completed, selected " + std::to_string(m_selectedTracks.size()) + " tracks");
        }
        
        invalidateCache();
        return true;
    }
    
    // Layout constants
    float headerHeight = 38.0f;
    float rulerHeight = 28.0f;
    float horizontalScrollbarHeight = 24.0f;
    AestraUI::NUIRect rulerRect(0, headerHeight + horizontalScrollbarHeight, bounds.width, rulerHeight);
    
    // Track area (below ruler)
    float trackAreaTop = headerHeight + horizontalScrollbarHeight + rulerHeight;
    AestraUI::NUIRect trackArea(0, trackAreaTop, bounds.width, bounds.height - trackAreaTop);
    
    bool isInRuler = rulerRect.contains(localPos);
    bool isInTrackArea = trackArea.contains(localPos);
    
    // Mouse wheel handling
    if (event.wheelDelta != 0.0f && (isInRuler || isInTrackArea)) {
        // Check for Shift modifier - Shift+scroll = ZOOM
        bool shiftHeld = (event.modifiers & AestraUI::NUIModifiers::Shift);
        
        if (shiftHeld || isInRuler) {
            // ZOOM: Shift+scroll anywhere OR scroll on ruler
            m_lastMouseZoomX = localPos.x;
            
            // Calculate mouse position in "content space" BEFORE zoom
            auto& themeManager = AestraUI::NUIThemeManager::getInstance();
            float controlAreaWidth = themeManager.getLayoutDimensions().trackControlsWidth;
            float gridStartX = controlAreaWidth + 5.0f;
            float gridWidthPx = getTimelineGridWidthPixels();
            
            float mouseRelX = localPos.x - gridStartX;
            
            // Current beat at mouse position
            double mouseBeat = (mouseRelX + m_timelineScrollOffset) / m_pixelsPerBeat;
            
            // Calculate min pixels/beat based on domain (can't zoom out beyond domain)
            const double domainWidth = std::max(1.0, m_minimapDomainEndBeat - m_minimapDomainStartBeat);
            float minPPB = std::max(1.0f, static_cast<float>(gridWidthPx / domainWidth));
            
            // Exponential zoom
            float zoomMultiplier = event.wheelDelta > 0 ? 1.15f : 0.87f;
            float newPixelsPerBeat = std::clamp(m_targetPixelsPerBeat * zoomMultiplier, minPPB, 300.0f);
            
            m_targetPixelsPerBeat = newPixelsPerBeat;
            // Update immediate for snappiness (smooth zoom interpolation can be added later if needed)
            m_pixelsPerBeat = newPixelsPerBeat;

            // Recalculate scroll offset to keep mouseBeat at the same screen position
            // (mouseBeat * newPixelsPerBeat) = newOffset + mouseRelX
            // newOffset = (mouseBeat * newPixelsPerBeat) - mouseRelX
            float newScrollOffset = static_cast<float>(mouseBeat * m_pixelsPerBeat) - mouseRelX;
            
            // Clamp scroll to domain bounds
            double maxStartBeat = std::max(0.0, m_minimapDomainEndBeat - (gridWidthPx / m_pixelsPerBeat));
            newScrollOffset = std::clamp(newScrollOffset, 0.0f, static_cast<float>(maxStartBeat * m_pixelsPerBeat));
            m_timelineScrollOffset = newScrollOffset;
            
            for (auto& trackUI : m_trackUIComponents) {
                trackUI->setBeatsPerBar(m_beatsPerBar);
                trackUI->setPixelsPerBeat(m_pixelsPerBeat);
                trackUI->setTimelineScrollOffset(m_timelineScrollOffset);
            }
            
            invalidateCache();  // Full cache invalidation for zoom changes
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
            invalidateCache();  // Vertical scroll needs full redraw
            return true;
        }
    }
    // === RULER INTERACTION: Loop markers, Playhead scrubbing OR timeline selection ===
    if (isInRuler) {
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
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
                invalidateCache();  // Hover state changed
            }
            
            // Start dragging loop marker
            if (event.pressed && event.button == AestraUI::NUIMouseButton::Left) {
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
        bool isSelectionClick = (event.pressed && event.button == AestraUI::NUIMouseButton::Right) ||
                                (event.pressed && event.button == AestraUI::NUIMouseButton::Left && 
                                 (event.modifiers & AestraUI::NUIModifiers::Ctrl));
        
        // Regular left-click (without Ctrl) starts playhead scrubbing
        // BUT NOT if we're hovering over a loop marker!
        bool isPlayheadClick = event.pressed && event.button == AestraUI::NUIMouseButton::Left && 
                              !(event.modifiers & AestraUI::NUIModifiers::Ctrl) &&
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
            
            invalidateCache();  // Selection started
            return true;
        }
        else if (isPlayheadClick && !m_isDraggingRulerSelection) {
            // Start dragging playhead (existing behavior)
            // Don't start if we're already doing a ruler selection!
            m_isDraggingPlayhead = true;
            if (m_trackManager) {
                m_trackManager->setUserScrubbing(true);
                
                // IMMEDIATE CLICK: Move playhead to clicked position right away
                auto& themeManager = AestraUI::NUIThemeManager::getInstance();
                const auto& layout = themeManager.getLayoutDimensions();
                float controlAreaWidth = layout.trackControlsWidth;
                float gridStartX = controlAreaWidth + 5;
                
                auto& playlist = m_trackManager->getPlaylistModel();
                float mouseX = localPos.x - gridStartX + m_timelineScrollOffset;
                
                double positionInBeats = mouseX / m_pixelsPerBeat;
                double positionInSeconds = playlist.beatToSeconds(positionInBeats);
                positionInSeconds = std::max(0.0, positionInSeconds);
                
                m_trackManager->setPosition(positionInSeconds);
                // Also save this as the new play start position for return-on-stop
                m_trackManager->setPlayStartPosition(positionInSeconds);
            }
            return true;
        }
    }
    
    // Handle ruler selection dragging
    if (m_isDraggingRulerSelection) {
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
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
        
        invalidateCache();  // Selection dragging
        
        // Stop dragging on mouse release
        if ((event.released && event.button == AestraUI::NUIMouseButton::Right) ||
            (event.released && event.button == AestraUI::NUIMouseButton::Left)) {
            m_isDraggingRulerSelection = false;
            
            // Only keep selection if it has a valid range
            if (m_hasRulerSelection) {
                // Get normalized selection range
                double selStartBeat = std::min(m_rulerSelectionStartBeat, m_rulerSelectionEndBeat);
                double selEndBeat = std::max(m_rulerSelectionStartBeat, m_rulerSelectionEndBeat);
                
                // Update loop markers to match selection
                m_loopStartBeat = selStartBeat;
                m_loopEndBeat = selEndBeat;
                m_loopEnabled = true;
                
                // Call selection callback - this will jump playhead and set loop region
                if (m_onSelectionMade) {
                    m_onSelectionMade(selStartBeat, selEndBeat);
                }
                
                // Also notify loop preset changed to selection mode
                if (m_onLoopPresetChanged) {
                    m_onLoopPresetChanged(5); // 5 = Selection preset
                }
                
                Log::info("[TrackManagerUI] Ruler selection: " + 
                         std::to_string(selStartBeat) + " to " + 
                         std::to_string(selEndBeat) + " beats");
            } else {
                // Click without drag - clear selection and disable loop
                m_hasRulerSelection = false;
                m_loopEnabled = false;

                // SPECIAL: If we clicked on an EXISTNG range on ruler, show menu
                // (Wait, this is handled in the isInRuler block if it's an instant click)
                
                // Trigger loop OFF callback
                if (m_onLoopPresetChanged) {
                    m_onLoopPresetChanged(0);  // 0 = Off
                }
            }
            
            return true;
        }
        
        return true;
    }

    // === RULER SELECTION CONTEXT MENU (Click on active range) ===
    if (isInRuler && event.pressed && event.button == AestraUI::NUIMouseButton::Left && 
        m_hasRulerSelection && !m_hoveringLoopStart && !m_hoveringLoopEnd) 
    {
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        float gridStartX = layout.trackControlsWidth + 5;
        
        float loopStartX = gridStartX + (static_cast<float>(m_loopStartBeat) * m_pixelsPerBeat) - m_timelineScrollOffset;
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
    
    // Handle loop marker dragging
    if (m_isDraggingLoopStart || m_isDraggingLoopEnd) {
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        float controlAreaWidth = layout.trackControlsWidth;
        float gridStartX = controlAreaWidth + 5;
        
        // Stop dragging on mouse release
        if (event.released && event.button == AestraUI::NUIMouseButton::Left) {
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
        
        invalidateCache();  // Loop marker position changed
        return true;
    }
    
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
    bool handled = AestraUI::NUIComponent::onMouseEvent(event);
    if (handled) return true;

    // === SPLIT TOOL: Click to split track at position ===
    if (m_currentTool == PlaylistTool::Split && event.pressed && event.button == AestraUI::NUIMouseButton::Left) {
        // Check if click is in track area
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        float controlAreaWidth = layout.trackControlsWidth;
        float gridStartX = controlAreaWidth + 5;
        
        float headerHeight = 38.0f;
        float rulerHeight = 28.0f;
        float horizontalScrollbarHeight = 24.0f;
        float trackAreaTop = headerHeight + horizontalScrollbarHeight + rulerHeight;
        
        AestraUI::NUIRect gridBounds(bounds.x + gridStartX, bounds.y + trackAreaTop,
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

// Pattern Playback Mode (Arsenal)
void TrackManagerUI::setPatternMode(bool enabled) {
    if (m_patternMode != enabled) {
        m_patternMode = enabled;
        setDirty(true);
        // Playhead visibility changes, but it's not cached usually.
        // But if we hide it, we need to repaint.
    }
}

bool TrackManagerUI::onKeyEvent(const AestraUI::NUIKeyEvent& event) {
    if (event.pressed) {
        // Hotkey 'A' toggles Automation Mode (FL/Ableton style)
        if (event.keyCode == AestraUI::NUIKeyCode::A && 
            !(event.modifiers & AestraUI::NUIModifiers::Ctrl)) {
            setPlaylistMode(m_playlistMode == PlaylistMode::Clips ? 
                            PlaylistMode::Automation : PlaylistMode::Clips);
            return true;
        }

        // Tool shortcuts
        if (event.keyCode == AestraUI::NUIKeyCode::Num1) { setCurrentTool(PlaylistTool::Select); return true; }
        if (event.keyCode == AestraUI::NUIKeyCode::Num2) { setCurrentTool(PlaylistTool::Split); return true; }

        if (event.keyCode == AestraUI::NUIKeyCode::Delete ||
            event.keyCode == AestraUI::NUIKeyCode::Backspace) {
            deleteSelectedClip();
            return true;
        }
        
        // Undo / Redo
        if (event.modifiers & AestraUI::NUIModifiers::Ctrl) {
            bool performed = false;
            
            // Redo: Ctrl+Shift+Z or Ctrl+Y
            if ((event.keyCode == AestraUI::NUIKeyCode::Z && (event.modifiers & AestraUI::NUIModifiers::Shift)) ||
                (event.keyCode == AestraUI::NUIKeyCode::Y)) {
                if (m_trackManager->getCommandHistory().redo()) {
                    performed = true;
                    Log::info("[TrackManagerUI] Redo performed");
                }
            }
            // Undo: Ctrl+Z
            else if (event.keyCode == AestraUI::NUIKeyCode::Z) {
                if (m_trackManager->getCommandHistory().undo()) {
                    performed = true;
                    Log::info("[TrackManagerUI] Undo performed");
                }
            }
            
            if (performed) {
                refreshTracks();
                invalidateCache();
                scheduleTimelineMinimapRebuild();
                m_trackManager->markModified();
                return true;
            }
            
            // Clipboard
            if (event.keyCode == AestraUI::NUIKeyCode::C) {
                copySelectedClip();
                return true;
            }
            if (event.keyCode == AestraUI::NUIKeyCode::V) {
                pasteClipboardAtCursor();
                return true;
            }
            if (event.keyCode == AestraUI::NUIKeyCode::D) {
                duplicateSelectedClip();
                return true;
            }
            // Ctrl+B: Paste-to-right (paste at end of selected clip, select new clip)
            if (event.keyCode == AestraUI::NUIKeyCode::B) {
                pasteClipToRight();
                return true;
            }
        }
    }
    return false;
}

void TrackManagerUI::copySelectedClip() {
    if (!m_selectedClipId.isValid()) return;
    
    auto& playlist = m_trackManager->getPlaylistModel();
    if (const auto* clip = playlist.getClip(m_selectedClipId)) {
        m_clipboardClip = *clip;
        Log::info("Copied clip: " + m_clipboardClip.name);
    }
}

void TrackManagerUI::pasteClipboardAtCursor() {
    if (!m_clipboardClip.id.isValid()) return;
    
    // Find target lane (currently selected track)
    TrackUIComponent* targetTrack = nullptr;
    for (auto& track : m_trackUIComponents) {
        if (track && track->isSelected()) {
            targetTrack = track.get();
            break;
        }
    }
    
    // Fallback: Use first track if none selected
    if (!targetTrack && !m_trackUIComponents.empty()) {
        targetTrack = m_trackUIComponents[0].get();
    }
    
    if (!targetTrack) return;
    
    // Paste at playhead position (normal snap to nearest)
    double playheadSeconds = m_trackManager->getPosition();
    double playhead = secondsToBeats(playheadSeconds);
    onPaintClip(targetTrack, playhead);
}

void TrackManagerUI::pasteClipToRight() {
    if (!m_clipboardClip.id.isValid()) return;
    
    // Find the currently selected clip
    auto& playlist = m_trackManager->getPlaylistModel();
    const ClipInstance* selectedClip = (m_selectedClipId.isValid()) 
        ? playlist.getClip(m_selectedClipId) 
        : nullptr;
    
    // Find target lane
    PlaylistLaneID targetLaneId;
    double pastePosition = 0.0;
    
    if (selectedClip) {
        // Paste at end of selected clip
        pastePosition = selectedClip->startBeat + selectedClip->durationBeats;
        
        // Find the lane containing the selected clip using API
        targetLaneId = playlist.findClipLane(m_selectedClipId);
    } else {
        // Fallback: Use playhead and first selected/available track
        double playheadSeconds = m_trackManager->getPosition();
        pastePosition = snapBeatToGridForward(secondsToBeats(playheadSeconds));
        
        for (auto& track : m_trackUIComponents) {
            if (track && track->isSelected()) {
                targetLaneId = track->getLaneId();
                break;
            }
        }
        if (!targetLaneId.isValid() && !m_trackUIComponents.empty()) {
            targetLaneId = m_trackUIComponents[0]->getLaneId();
        }
    }
    
    if (!targetLaneId.isValid()) return;
    
    // Create new clip
    ClipInstance newClip = m_clipboardClip;
    newClip.id = ClipInstanceID::generate();
    newClip.startBeat = pastePosition;
    
    // Create Command
    auto cmd = std::make_shared<Aestra::Audio::AddClipCommand>(
        m_trackManager->getPlaylistModel(), 
        targetLaneId, 
        newClip
    );
    m_trackManager->getCommandHistory().pushAndExecute(cmd);
    
    // Select the new clip so repeated Ctrl+B continues to the right
    m_selectedClipId = newClip.id;
    
    refreshTracks();
    invalidateCache();
    scheduleTimelineMinimapRebuild();
    m_trackManager->markModified();
    Log::info("Paste-to-right at beat " + std::to_string(newClip.startBeat));
}

void TrackManagerUI::duplicateSelectedClip() {
    if (!m_trackManager || !m_selectedClipId.isValid()) return;

    auto& playlist = m_trackManager->getPlaylistModel();
    const ClipInstance* selectedClip = playlist.getClip(m_selectedClipId);
    if (!selectedClip) return;

    PlaylistLaneID targetLaneId = playlist.findClipLane(m_selectedClipId);
    if (!targetLaneId.isValid()) return;

    const double targetStartBeat = selectedClip->startBeat + selectedClip->durationBeats;
    auto cmd = std::make_shared<Aestra::Audio::DuplicateClipCommand>(
        playlist,
        m_selectedClipId,
        targetStartBeat,
        targetLaneId
    );
    m_trackManager->getCommandHistory().pushAndExecute(cmd);

    const ClipInstanceID duplicateId = cmd->getDuplicateId();
    if (duplicateId.isValid()) {
        m_selectedClipId = duplicateId;
        if (const auto* duplicateClip = playlist.getClip(duplicateId)) {
            m_clipboardClip = *duplicateClip;
        }
    }

    refreshTracks();
    invalidateCache();
    scheduleTimelineMinimapRebuild();
    m_trackManager->markModified();
    Log::info("Duplicated selected clip");
}

void TrackManagerUI::onPaintClip(TrackUIComponent* trackComp, double beat) {
    if (!trackComp || !m_clipboardClip.id.isValid()) return;
    
    ClipInstance newClip = m_clipboardClip;
    newClip.id = ClipInstanceID::generate();
    newClip.startBeat = snapBeatToGrid(beat);
    
    // Create Command
    auto cmd = std::make_shared<Aestra::Audio::AddClipCommand>(
        m_trackManager->getPlaylistModel(), 
        trackComp->getLaneId(), 
        newClip
    );
    m_trackManager->getCommandHistory().pushAndExecute(cmd);
    
    refreshTracks();
    invalidateCache();
    scheduleTimelineMinimapRebuild();
    m_trackManager->markModified();
    Log::info("Pasted clip via Paint/Paste");
}

void TrackManagerUI::updateScrollbar() {
    if (!m_scrollbar) return;
    
    AestraUI::NUIRect bounds = getBounds();
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
}

float TrackManagerUI::getTimelineGridWidthPixels() const
{
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
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
    float newScrollOffset = std::max(0.0f, static_cast<float>(clampedStart * static_cast<double>(m_pixelsPerBeat)));
    
    // OPTIMIZATION: Only invalidate cache if scroll actually changed
    // Use beat-based threshold to ensure responsiveness at all zoom levels
    const double currentStartBeat = static_cast<double>(m_timelineScrollOffset / m_pixelsPerBeat);
    const double beatThreshold = 0.01; // ~1/100th of a beat is perceptible
    bool scrollChanged = std::abs(clampedStart - currentStartBeat) > beatThreshold;
    
    if (scrollChanged) {
        m_timelineScrollOffset = newScrollOffset;
        
        for (auto& trackUI : m_trackUIComponents) {
            trackUI->setTimelineScrollOffset(m_timelineScrollOffset);
        }

        invalidateCache();
        setDirty(true);
    }

    if (!isFinal) {
        updateTimelineMinimap(0.0);
    }
}

void TrackManagerUI::resizeTimelineViewEdgeFromMinimap(AestraUI::TimelineMinimapResizeEdge edge, double anchorBeat,
                                                       double edgeBeat, bool isFinal)
{
    const float gridWidthPx = getTimelineGridWidthPixels();
    if (gridWidthPx <= 0.0f) return;

    constexpr float kMinPixelsPerBeat = 1.0f;   // Allow extreme zoom-out for long clips
    constexpr float kMaxPixelsPerBeat = 300.0f;

    const double domainStart = m_minimapDomainStartBeat;
    const double domainEnd = std::max(m_minimapDomainEndBeat, domainStart + 1.0);

    const double minWidthBeats = static_cast<double>(gridWidthPx / kMaxPixelsPerBeat);
    // Max zoom-out is limited by domain size (can't view beyond domain via minimap resize)
    const double maxWidthBeats = std::min(
        static_cast<double>(gridWidthPx / kMinPixelsPerBeat),
        domainEnd - domainStart  // Can't zoom out beyond current domain
    );

    const auto applyZoom = [this](float newPixelsPerBeat) {
        m_pixelsPerBeat = newPixelsPerBeat;
        m_targetPixelsPerBeat = newPixelsPerBeat;
        for (auto& trackUI : m_trackUIComponents) {
            trackUI->setPixelsPerBeat(m_pixelsPerBeat);
        }
    };

    if (edge == AestraUI::TimelineMinimapResizeEdge::Left) {
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
        // Clamp to domain - minimap resize does NOT expand domain (only edge-scroll during clip drag does)
        const double clampedEdge =
            std::min(domainEnd, std::max(edgeBeat, anchorBeat + std::max(1e-6, minWidthBeats)));
        const double desiredWidth = std::max(minWidthBeats, std::min(maxWidthBeats, clampedEdge - anchorBeat));
        const float newPixelsPerBeat = std::clamp(static_cast<float>(gridWidthPx / desiredWidth), kMinPixelsPerBeat, kMaxPixelsPerBeat);
        applyZoom(newPixelsPerBeat);

        setTimelineViewStartBeat(anchorBeat, isFinal);
    }

    invalidateCache();  // Always invalidate on zoom/resize changes
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

    // Calculate min pixels/beat based on domain (can't zoom out beyond domain)
    const double domainWidth = std::max(1.0, m_minimapDomainEndBeat - m_minimapDomainStartBeat);
    float minPPB = std::max(1.0f, static_cast<float>(gridWidthPx / domainWidth));

    // Minimap zoom must feel immediate; keep the smooth-zoom system in sync by updating both.
    const float newPixelsPerBeat = std::clamp(m_pixelsPerBeat * zoomMultiplier, minPPB, 300.0f);
    m_pixelsPerBeat = newPixelsPerBeat;
    m_targetPixelsPerBeat = newPixelsPerBeat;

    for (auto& trackUI : m_trackUIComponents) {
        trackUI->setPixelsPerBeat(m_pixelsPerBeat);
    }

    const double viewWidthBeats = static_cast<double>(gridWidthPx / m_pixelsPerBeat);
    const double viewStartBeat = anchorBeat - (viewWidthBeats * 0.5);
    setTimelineViewStartBeat(viewStartBeat, true);
    invalidateCache();  // Always invalidate on zoom changes
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
    
    // === SMART DOMAIN CALCULATION ===
    const double beatsPerBarD = static_cast<double>(std::max(1, m_beatsPerBar));
    
    // Has content?
    bool hasContent = (clipEndBeat > 0.001);
    
    double requiredEndBeat;
    if (!hasContent) {
        // EMPTY PROJECT: Fixed 16 bars - can't zoom out beyond this
        // EXCEPT: edge scrolling can push the view, which expands domain dynamically
        const double emptyFixedBars = 16.0;
        const double emptyMinBeats = beatsPerBarD * emptyFixedBars;
        
        // If view has scrolled beyond the fixed domain (via edge-scroll), expand to accommodate
        // Otherwise stay locked at 16 bars
        if (viewEndBeat > emptyMinBeats) {
            // Edge scrolling pushed us out - expand domain to current view + small buffer
            requiredEndBeat = viewEndBeat + (beatsPerBarD * 2.0);
        } else {
            // Locked at 16 bars
            requiredEndBeat = emptyMinBeats;
        }
        
        // Playhead can also push domain (if playing beyond)
        const double playheadBuffer = beatsPerBarD * 2.0;
        if (playheadBeat + playheadBuffer > requiredEndBeat) {
            requiredEndBeat = playheadBeat + playheadBuffer;
        }
    } else {
        // HAS CONTENT: Dynamic padding that scales with content length
        // Short clips (< 16 bars): Add 8 bars padding (50% headroom feels right)
        // Medium clips (16-64 bars): Add ~25% padding  
        // Long clips (64+ bars): Add ~12.5% padding (efficient use of space)
        double paddingBars;
        double clipBars = clipEndBeat / beatsPerBarD;
        
        if (clipBars < 16.0) {
            paddingBars = 8.0;  // Short clip: fixed 8 bar padding
        } else if (clipBars < 64.0) {
            paddingBars = clipBars * 0.25;  // Medium: 25% of clip length
        } else {
            paddingBars = clipBars * 0.125;  // Long: 12.5%, no cap - infinite if PC handles it
        }
        
        const double padBeats = beatsPerBarD * paddingBars;
        const double playheadBuffer = beatsPerBarD * 4.0;  // 4 bars ahead of playhead
        
        // Required = max of: content + padding, playhead + buffer, view with small margin
        requiredEndBeat = std::max({clipEndBeat + padBeats, playheadBeat + playheadBuffer, viewEndBeat + beatsPerBarD});
        
        // No hard ceiling - let it grow as big as needed (infinite if PC can handle it)
    }

    // Update domain instantly (no cooldown needed)
    if (!(m_minimapDomainEndBeat > 0.0)) {
        // First init
        m_minimapDomainEndBeat = requiredEndBeat;
        m_minimapNeedsRebuild = true;
    } else if (requiredEndBeat > m_minimapDomainEndBeat + 1e-3) {
        // Domain needs to grow - instant
        m_minimapDomainEndBeat = requiredEndBeat;
        m_minimapNeedsRebuild = true;
    } else if (requiredEndBeat < m_minimapDomainEndBeat - 1e-3) {
        // Domain can shrink - also instant, no reason to delay
        m_minimapDomainEndBeat = requiredEndBeat;
        m_minimapNeedsRebuild = true;
    }
    // else: domain unchanged, nothing to do

    if (m_minimapNeedsRebuild) {
        std::vector<AestraUI::TimelineMinimapClipSpan> spans;

        const auto& laneIds = playlist.getLaneIDs();
        for (size_t i = 0; i < laneIds.size(); ++i) {
            const auto& laneId = laneIds[i];
            if (const auto* lane = playlist.getLane(laneId)) {
                for (const auto& clip : lane->clips) {
                    AestraUI::TimelineMinimapClipSpan span;
                    span.id = static_cast<AestraUI::TimelineMinimapClipId>(clip.id.high ^ clip.id.low);
                    span.type = AestraUI::TimelineMinimapClipType::Audio;

                    span.startBeat = clip.startBeat;
                    span.endBeat = clip.startBeat + clip.durationBeats;
                    span.trackIndex = static_cast<uint32_t>(i); // Track index for minimap matching

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
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
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

    AestraUI::TimelineMinimapModel model;
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
    model.showPlayhead = !m_patternMode; // Hide playhead in Arsenal Pattern Mode

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

void TrackManagerUI::renderTimeRuler(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& rulerBounds) {
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    auto borderColor = themeManager.getColor("borderColor");
    auto textColor = themeManager.getColor("textPrimary");
    auto accentColor = themeManager.getColor("accentPrimary");
    
    // === PRO/GLASS RULER STYLE ===
    // Background: Darker than track area to visually separate
    auto glassBg = themeManager.getColor("backgroundSecondary").withAlpha(0.9f);
    auto glassHighlight = AestraUI::NUIColor::white().withAlpha(0.04f); // Top edge highlight
    
    auto textCol = themeManager.getColor("textPrimary");
    auto tickCol = themeManager.getColor("textPrimary").withAlpha(0.72f);
    
    // Restore layout definition
    const auto& layout = themeManager.getLayoutDimensions();

    // Calculate grid bounds FIRST (before drawing)
    float controlAreaWidth = layout.trackControlsWidth;
    float gridStartX = rulerBounds.x + controlAreaWidth + 5.0f;
    
    float scrollbarWidth = 15.0f;
    float trackWidth = rulerBounds.width - scrollbarWidth;
    float gridWidth = std::max(0.0f, trackWidth - controlAreaWidth - 10.0f);
    
    AestraUI::NUIRect gridRulerRect(gridStartX, rulerBounds.y, gridWidth, rulerBounds.height);
    
    // 1. Draw glass background on grid area (timeline portion)
    float cornerRadius = 4.0f;
    renderer.fillRoundedRect(gridRulerRect, cornerRadius, glassBg);
    
    // Subtle top highlight on grid area only
    AestraUI::NUIRect highlightRect(gridRulerRect.x, gridRulerRect.y, gridRulerRect.width, 1.0f);
    renderer.fillRect(highlightRect, glassHighlight);
    
    // Draw subtle glass border on grid area
    renderer.strokeRoundedRect(gridRulerRect, cornerRadius, 1.0f, borderColor.withAlpha(0.4f));

    // === SET CLIP RECT for timeline grid area (prevents text/ticks bleeding outside) ===
    renderer.setClipRect(gridRulerRect);
    
    // Grid spacing - DYNAMIC based on zoom level
    int beatsPerBar = m_beatsPerBar;
    float pixelsPerBar = m_pixelsPerBeat * beatsPerBar;
    
    // === ADAPTIVE DENSITY: Determine bar stride based on zoom level ===
    // When zoomed out far, skip bars to avoid clutter - smooth LOD transitions
    int barStride = 1;  // Default: show every bar
    if (pixelsPerBar < 2.0f) {
        barStride = 128; // Ultra extreme zoom: every 128 bars
    } else if (pixelsPerBar < 4.0f) {
        barStride = 64;  // Extreme zoom out: every 64 bars
    } else if (pixelsPerBar < 8.0f) {
        barStride = 32;  // Very far zoom: every 32 bars
    } else if (pixelsPerBar < 12.0f) {
        barStride = 16;  // Far zoom: every 16 bars
    } else if (pixelsPerBar < 20.0f) {
        barStride = 8;   // Zoomed out: every 8 bars
    } else if (pixelsPerBar < 35.0f) {
        barStride = 4;   // Medium-far zoom: every 4 bars
    } else if (pixelsPerBar < 55.0f) {
        barStride = 2;   // Medium zoom: every 2 bars
    }
    // else barStride = 1 (show every bar)
    
    // Calculate which bar to start drawing from based on scroll offset
    int startBar = static_cast<int>(m_timelineScrollOffset / pixelsPerBar);
    // Align startBar to stride
    startBar = (startBar / barStride) * barStride;
    
    // Calculate end bar based on visible width (no max extent bounds)
    int visibleBars = static_cast<int>(std::ceil((m_timelineScrollOffset + gridWidth) / pixelsPerBar)) - startBar;
    int endBar = startBar + visibleBars + barStride;  // Draw all visible bars
    
    // Draw vertical ticks - dynamically based on visible bars and scroll offset
    for (int bar = startBar; bar <= endBar; bar += barStride) {
        // Calculate x position accounting for scroll offset
        float x = gridStartX + (bar * pixelsPerBar) - m_timelineScrollOffset;
        
        // Bar number (1-based)
        int barNum = bar + 1;
        std::string barText = std::to_string(barNum);
        
        // Bigger text for major bars (multiples of 4 bars from bar 1)
        // When using stride, all shown bars are "major" since we're already filtering
        bool isMajorBar = (barStride > 1) || (barNum == 1) || ((barNum - 1) % 4 == 0); // 1, 5, 9, 13...
        float fontSize = isMajorBar ? 11.0f : 9.0f;  // Compact ruler text
        float textAlpha = isMajorBar ? 1.0f : 0.7f;
        
        auto textSize = renderer.measureText(barText, fontSize);
        
        // Place text vertically centered in ruler area (top-left Y positioning)
        float textY = std::round(renderer.calculateTextY(rulerBounds, fontSize));
        
        // Position text to the RIGHT of the grid line with small offset
        float textX = x + 4.0f;
        
        // Draw text - clip rect handles edge clipping automatically
        renderer.drawText(barText, 
                        AestraUI::NUIPoint(textX, textY),
                        fontSize, textCol);
        
        // Bar tick line - major bars get full height, others half
        // Mature Style: Ticks bottom-up
        float tickHeight = isMajorBar ? rulerBounds.height * 0.5f : rulerBounds.height * 0.25f;
        renderer.drawLine(
            AestraUI::NUIPoint(x, rulerBounds.y + rulerBounds.height - tickHeight),
            AestraUI::NUIPoint(x, rulerBounds.y + rulerBounds.height),
            1.0f,
            isMajorBar ? tickCol : tickCol.withAlpha(0.7f)
        );
        
        // Beat ticks within the bar (only if zoomed in enough AND not striding)
        // DOWNBEATS (1, 2, 3, 4) are BRIGHTER and TALLER for visibility
        if (m_pixelsPerBeat >= 15.0f && barStride == 1) {
            for (int beat = 1; beat < beatsPerBar; ++beat) {
                float beatX = x + (beat * m_pixelsPerBeat);
                
                // Downbeats (main beats 1,2,3,4) get brighter, taller ticks
                float beatTickHeight = rulerBounds.height * 0.35f;  // Taller than before
                AestraUI::NUIColor beatTickColor = accentColor.withAlpha(0.65f);  // Bright purple
                
                renderer.drawLine(
                    AestraUI::NUIPoint(beatX, rulerBounds.y + rulerBounds.height - beatTickHeight),
                    AestraUI::NUIPoint(beatX, rulerBounds.y + rulerBounds.height),
                    1.0f,
                    beatTickColor
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
        
        // Selection rendering - clip rect handles edge clipping
        float selectionWidth = selEndX - selStartX;
        if (selectionWidth > 0.0f) {
            AestraUI::NUIRect selectionRect(selStartX, rulerBounds.y, selectionWidth, rulerBounds.height);
            
            // Fill with semi-transparent accent color
            renderer.fillRect(selectionRect, accentColor.withAlpha(0.25f));
            
            // Draw subtle borders at selection edges
            renderer.drawLine(
                AestraUI::NUIPoint(selStartX, rulerBounds.y),
                AestraUI::NUIPoint(selStartX, rulerBounds.bottom()),
                1.0f,
                accentColor.withAlpha(0.6f)
            );
            renderer.drawLine(
                AestraUI::NUIPoint(selEndX, rulerBounds.y),
                AestraUI::NUIPoint(selEndX, rulerBounds.bottom()),
                1.0f,
                accentColor.withAlpha(0.6f)
            );
        }
    }
    
    // === CLEAR CLIP RECT before drawing control area ===
    renderer.clearClipRect();

    // 2. Draw SOLID background for CONTROL AREA (left side - DRAWN LAST to fully cover any bleed)
    //    Use backgroundPrimary to match minimap's left section exactly
    auto controlBg = themeManager.getColor("backgroundPrimary");
    AestraUI::NUIRect controlRect(rulerBounds.x, rulerBounds.y, controlAreaWidth + 5.0f, rulerBounds.height);
    renderer.fillRect(controlRect, controlBg);

    // Dedicated "corner" panel where track controls meet the ruler.
    const AestraUI::NUIRect cornerRect(rulerBounds.x, rulerBounds.y, controlAreaWidth, rulerBounds.height);
    renderer.drawLine(
        AestraUI::NUIPoint(cornerRect.right(), cornerRect.y),
        AestraUI::NUIPoint(cornerRect.right(), cornerRect.bottom()),
        1.0f,
        borderColor.withAlpha(0.5f)
    );

}

// Set loop region (called from Main.cpp when loop preset changes)
void TrackManagerUI::setLoopRegion(double startBeat, double endBeat, bool enabled) {
    m_loopStartBeat = startBeat;
    m_loopEndBeat = endBeat;
    m_loopEnabled = enabled;
    invalidateCache();  // Redraw to show updated markers
}

// Render loop markers on ruler
void TrackManagerUI::renderLoopMarkers(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& rulerBounds) {
    // Only show markers when there's an active ruler selection
    if (!m_hasRulerSelection) return;
    
    if (m_loopEndBeat <= m_loopStartBeat) return;  // Invalid loop region
    
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
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
    AestraUI::NUIColor markerColor;
    
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
        AestraUI::NUIColor startColor = markerColor;
        if (m_hoveringLoopStart || m_isDraggingLoopStart) {
            startColor = accentColor;  // Full brightness on hover/drag
        }
        
        // Draw triangle pointing down (using lines)
        AestraUI::NUIPoint p1(loopStartX, rulerBounds.y + triangleHeight);  // Bottom center
        AestraUI::NUIPoint p2(loopStartX - triangleWidth / 2, rulerBounds.y);  // Top left
        AestraUI::NUIPoint p3(loopStartX + triangleWidth / 2, rulerBounds.y);  // Top right
        
        // Draw filled triangle using lines
        renderer.drawLine(p1, p2, 2.0f, startColor);
        renderer.drawLine(p2, p3, 2.0f, startColor);
        renderer.drawLine(p3, p1, 2.0f, startColor);
        
        // Draw vertical line from triangle to bottom
        renderer.drawLine(
            AestraUI::NUIPoint(loopStartX, rulerBounds.y + triangleHeight),
            AestraUI::NUIPoint(loopStartX, rulerBounds.y + rulerBounds.height),
            2.0f,
            startColor
        );
    }
    
    // === RENDER LOOP END MARKER ===
    if (endVisible) {
        AestraUI::NUIColor endColor = markerColor;
        if (m_hoveringLoopEnd || m_isDraggingLoopEnd) {
            endColor = accentColor;  // Full brightness on hover/drag
        }
        
        // Draw triangle pointing down (using lines)
        AestraUI::NUIPoint p1(loopEndX, rulerBounds.y + triangleHeight);  // Bottom center
        AestraUI::NUIPoint p2(loopEndX - triangleWidth / 2, rulerBounds.y);  // Top left
        AestraUI::NUIPoint p3(loopEndX + triangleWidth / 2, rulerBounds.y);  // Top right
        
        // Draw filled triangle using lines
        renderer.drawLine(p1, p2, 2.0f, endColor);
        renderer.drawLine(p2, p3, 2.0f, endColor);
        renderer.drawLine(p3, p1, 2.0f, endColor);
        
        // Draw vertical line from triangle to bottom
        renderer.drawLine(
            AestraUI::NUIPoint(loopEndX, rulerBounds.y + triangleHeight),
            AestraUI::NUIPoint(loopEndX, rulerBounds.y + rulerBounds.height),
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
void TrackManagerUI::drawGrid(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& bounds, float gridStartX, float gridWidth, float timelineScrollOffset) {
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    
    // Draw Dynamic Snap Grid
    double snapDur = AestraUI::MusicTheory::getSnapDuration(m_snapSetting);
    if (m_snapSetting == AestraUI::SnapGrid::None) snapDur = 1.0;
    if (snapDur <= 0.0001) snapDur = 1.0;

    // Dynamic Density (Relaxed to 5px)
    while ((m_pixelsPerBeat * snapDur) < 5.0f) {
        snapDur *= 2.0;
    }

    double startBeat = timelineScrollOffset / m_pixelsPerBeat;
    double endBeat = startBeat + (gridWidth / m_pixelsPerBeat);

    // === ADAPTIVE GRID DENSITY based on zoom level ===
    // Determine the minimum spacing between grid lines to avoid clutter
    float pixelsPerBar = m_pixelsPerBeat * m_beatsPerBar;
    int gridBarStride = 1;  // Default: grid line every bar
    bool showBeatLines = true;
    bool showSubdivisions = true;
    
    if (pixelsPerBar < 2.0f) {
        gridBarStride = 128; // Ultra extreme: every 128 bars
        showBeatLines = false;
        showSubdivisions = false;
    } else if (pixelsPerBar < 4.0f) {
        gridBarStride = 64;  // Extreme zoom: every 64 bars
        showBeatLines = false;
        showSubdivisions = false;
    } else if (pixelsPerBar < 8.0f) {
        gridBarStride = 32;  // Very far zoom: every 32 bars
        showBeatLines = false;
        showSubdivisions = false;
    } else if (pixelsPerBar < 12.0f) {
        gridBarStride = 16;  // Far zoom: every 16 bars
        showBeatLines = false;
        showSubdivisions = false;
    } else if (pixelsPerBar < 20.0f) {
        gridBarStride = 8;   // Zoomed out: every 8 bars
        showBeatLines = false;
        showSubdivisions = false;
    } else if (pixelsPerBar < 35.0f) {
        gridBarStride = 4;   // Medium-far zoom: every 4 bars
        showBeatLines = false;
        showSubdivisions = false;
    } else if (pixelsPerBar < 55.0f) {
        gridBarStride = 2;   // Medium zoom: every 2 bars
        showBeatLines = false;
        showSubdivisions = false;
    } else if (m_pixelsPerBeat < 12.0f) {
        // Bars visible but beats too close
        showBeatLines = false;
        showSubdivisions = false;
    } else if (m_pixelsPerBeat < 25.0f) {
        // Beats visible but subdivisions too close
        showSubdivisions = false;
    }

    // Round start to nearest snap (adjusted for stride)
    double barBeats = static_cast<double>(m_beatsPerBar);
    double strideBeats = barBeats * gridBarStride;
    double current = std::floor(startBeat / strideBeats) * strideBeats;
    
    // Grid Lines - Using Theme Tokens
    AestraUI::NUIColor barLineColor = themeManager.getColor("gridBar");
    AestraUI::NUIColor beatLineColor = themeManager.getColor("gridBeat");
    AestraUI::NUIColor subBeatLineColor = themeManager.getColor("gridSubdivision");

    // Draw grid lines with adaptive density
    for (; current <= endBeat + strideBeats; current += snapDur) {
        float xPos = bounds.x + gridStartX + static_cast<float>(current * m_pixelsPerBeat) - timelineScrollOffset;
        
        // Strict culling within valid grid area
        if (xPos < bounds.x + gridStartX || xPos > bounds.x + gridStartX + gridWidth) continue;

        // Hierarchy logic
        bool isBar = (std::fmod(std::abs(current), barBeats) < 0.001);
        bool isStrideBar = (std::fmod(std::abs(current), strideBeats) < 0.001);
        bool isBeat = (std::fmod(std::abs(current), 1.0) < 0.001);
        
        // Skip lines based on adaptive density settings
        if (!isStrideBar && !showBeatLines) continue;  // Skip non-stride bars and beats
        if (!isBar && !isBeat && !showSubdivisions) continue;  // Skip subdivisions
        if (!isBar && !showBeatLines) continue;  // Skip beats when not showing them
        
        // Draw Vertical Grid Line (Full Height) using fillRect for reliable batching
        float trackAreaTop = bounds.y;
        float trackAreaBottom = bounds.y + bounds.height;
        float height = trackAreaBottom - trackAreaTop;
        AestraUI::NUIRect lineRect(xPos, trackAreaTop, 1.0f, height);
        
        // Batching Friendly: fillRect uses Type 0 (Image/Color default) which works perfectly with Unified Batching
        if (isBar) {
            renderer.fillRect(lineRect, barLineColor);
        }
        else if (isBeat) {
            renderer.fillRect(lineRect, beatLineColor);
        }
        else {
            renderer.fillRect(lineRect, subBeatLineColor);
        }
    }
}

// Draw playhead (vertical line showing current playback position)
// Draw playhead (vertical line showing current playback position)
void TrackManagerUI::renderPlayhead(AestraUI::NUIRenderer& renderer) {
    // If in Pattern Mode (Arsenal), hide the global timeline playhead to avoid confusion ("Time Segmentation")
    if (m_patternMode) return;

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
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    float controlAreaWidth = layout.trackControlsWidth;
    
    AestraUI::NUIRect bounds = getBounds();
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
        // Playhead color - Aestra Purple for consistency
        auto& themeManager = AestraUI::NUIThemeManager::getInstance(); // ensure themeManager is available (it was declared above but check scope)
        // Redefine/Reuse themeManager? It's declared at line 2731 in the function scope (based on previous view).
        // Let's use the one from outside if available, or just get it.
        // The visible snippet starts at 2761, check context. 
        // In the previous view_code_item, text was "auto& themeManager = AestraUI::NUIThemeManager::getInstance();" at line 2731.
        AestraUI::NUIColor playheadColor = themeManager.getColor("accentPrimary"); 

        // GLOW EFFECT (BG) - Only when playing
        if (m_trackManager->isPlaying()) {
             // Manual Gradient Glow for smoother look
             float glowWidth = 6.0f; // Width of glow on each side
             float lineH = playheadEndY - playheadStartY;
             
             AestraUI::NUIColor glowColorCenter = playheadColor.withAlpha(0.25f); // Soft center
             AestraUI::NUIColor glowColorEdge = playheadColor.withAlpha(0.0f);    // Transparent edge
             
             // Left side glow (Transparent -> Color)
             renderer.fillRectGradient(
                 AestraUI::NUIRect(playheadX - glowWidth, playheadStartY, glowWidth, lineH),
                 glowColorEdge, glowColorCenter, false // false = horizontal
             );
             
             // Right side glow (Color -> Transparent)
             renderer.fillRectGradient(
                 AestraUI::NUIRect(playheadX, playheadStartY, glowWidth, lineH),
                 glowColorCenter, glowColorEdge, false // false = horizontal
             );
        }

        // Draw playhead line (Thinner 1px, simple)
        renderer.drawLine(
            AestraUI::NUIPoint(playheadX, playheadStartY),
            AestraUI::NUIPoint(playheadX, playheadEndY),
            1.0f, 
            playheadColor
        );

        // Draw playhead Triangle Cap (In Ruler)
        // Triangle pointing down - Filled
        // Manual rasterization using vertical lines since NUIRenderer lacks fillTriangle
        for (float dx = -triangleSize; dx <= triangleSize; dx += 1.0f) {
            float ratio = 1.0f - (std::abs(dx) / triangleSize); // 1.0 at center, 0.0 at edges
            float h = triangleSize * ratio;
            if (h < 1.0f) h = 1.0f; // Ensure at least 1px
            
            renderer.drawLine(
                AestraUI::NUIPoint(playheadX + dx, playheadStartY),
                AestraUI::NUIPoint(playheadX + dx, playheadStartY + h),
                1.0f,
                playheadColor
            );
        }
    }
}

// ⚡ MULTI-LAYER CACHING IMPLEMENTATION

void TrackManagerUI::updateBackgroundCache(AestraUI::NUIRenderer& renderer) {
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
    
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    
    // Calculate layout dimensions
    float controlAreaWidth = layout.trackControlsWidth;
    float gridStartX = controlAreaWidth + 5;
    float scrollbarWidth = 15.0f;
    float gridWidth = width - controlAreaWidth - scrollbarWidth - 5;
    
    AestraUI::NUIRect textureBounds(0, 0, static_cast<float>(width), static_cast<float>(height));
    AestraUI::NUIColor bgColor = themeManager.getColor("backgroundPrimary");
    AestraUI::NUIColor borderColor = themeManager.getColor("border");
    
    // Draw background panels
    AestraUI::NUIRect controlBg(0, 0, controlAreaWidth, static_cast<float>(height));
    renderer.fillRect(controlBg, bgColor);
    
    AestraUI::NUIRect gridBg(gridStartX, 0, gridWidth, static_cast<float>(height));
    // Grid Background: Deep Charcoal (Lifted from Void)
    renderer.fillRect(gridBg, AestraUI::NUIColor(0.09f, 0.09f, 0.10f, 1.0f));
    
    // Draw borders
    renderer.strokeRect(textureBounds, 1, borderColor);
    
    // Draw header bar
    float headerHeight = 38.0f;
    AestraUI::NUIRect headerRect(0, 0, static_cast<float>(width), headerHeight);
    renderer.fillRect(headerRect, bgColor);
    renderer.strokeRect(headerRect, 1, borderColor);
    
    // Draw time ruler
    float rulerHeight = 28.0f;
    float horizontalScrollbarHeight = 24.0f;
    AestraUI::NUIRect rulerRect(0, headerHeight + horizontalScrollbarHeight, static_cast<float>(width), rulerHeight);
    
    // Render ruler ticks (static part only - no moving elements)
    double bpm = 120.0;
    double secondsPerBeat = 60.0 / bpm;
    double maxExtent = getMaxTimelineExtent();
    double maxExtentInBeats = maxExtent / secondsPerBeat;
    
    // Ruler Render: "Mature" Playlist Style
    auto bg = AestraUI::NUIColor(0.08f, 0.08f, 0.10f, 1.0f); 
    auto textCol = AestraUI::NUIColor(0.82f, 0.82f, 0.82f, 1.0f);  // bright gray for ruler labels
    auto tickCol = AestraUI::NUIColor(0.45f, 0.45f, 0.50f, 1.0f);   // visible tick marks
    
    // Draw ruler background
    renderer.fillRect(rulerRect, bg);
    renderer.strokeRect(rulerRect, 1, borderColor);
    
    // Draw beat markers (grid lines)
    // USE SHARED HELPER
    float trackAreaTop = rulerRect.y + rulerRect.height;
    AestraUI::NUIRect gridArea(0, trackAreaTop, static_cast<float>(width), static_cast<float>(height) - trackAreaTop);
    
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
            renderer.drawText(barText, AestraUI::NUIPoint(textX, textY), barFontSize, textCol);
        }
    }
    
    renderer.renderToTextureEnd();
    m_backgroundTextureId = texId;
    m_backgroundNeedsUpdate = false;
    
    Log::info("✅ Background cache updated: " + std::to_string(width) + "×" + std::to_string(height));
}

void TrackManagerUI::updateControlsCache(AestraUI::NUIRenderer& renderer) {
    // TODO: Cache static UI controls (buttons, labels) - not implemented yet
    m_controlsNeedsUpdate = false;
}

void TrackManagerUI::updateTrackCache(AestraUI::NUIRenderer& renderer, size_t trackIndex) {
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





void TrackManagerUI::deleteSelectedClip() {
    if (!m_trackManager || !m_selectedClipId.isValid()) {
        Log::warning("No clip selected for delete");
        return;
    }
    
    auto& playlist = m_trackManager->getPlaylistModel();
    auto cmd = std::make_shared<RemoveClipCommand>(playlist, m_selectedClipId);
    m_trackManager->getCommandHistory().pushAndExecute(cmd);
    m_selectedClipId = ClipInstanceID{};
    
    refreshTracks();
    invalidateCache();
    scheduleTimelineMinimapRebuild();
    
    Log::info("Deleted selected clip via PlaylistModel");
}


// =============================================================================
// Drop Target Implementation (IDropTarget)
// =============================================================================

AestraUI::DropFeedback TrackManagerUI::onDragEnter(const AestraUI::DragData& data, const AestraUI::NUIPoint& position) {
    Log::info("[TrackManagerUI] Drag entered");
    
    // Accept file drops, audio clip moves, and plugins
    if (data.type != AestraUI::DragDataType::File && 
        data.type != AestraUI::DragDataType::AudioClip &&
        data.type != AestraUI::DragDataType::Plugin) {
        return AestraUI::DropFeedback::Invalid;
    }

    // Early reject unsupported formats (cheap extension check; full validation happens on drop).
    if (data.type == AestraUI::DragDataType::File &&
        !AudioFileValidator::hasValidAudioExtension(data.filePath)) {
        m_showDropPreview = false;
        setDirty(true);
        return AestraUI::DropFeedback::Invalid;
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
        // Move for clips; Copy for files and plugins
        return data.type == AestraUI::DragDataType::AudioClip ? 
               AestraUI::DropFeedback::Move : AestraUI::DropFeedback::Copy;
    }
    
    return AestraUI::DropFeedback::Invalid;
}

AestraUI::DropFeedback TrackManagerUI::onDragOver(const AestraUI::DragData& data, const AestraUI::NUIPoint& position) {
    // Keep feedback "Invalid" for unsupported formats while hovering.
    if (data.type == AestraUI::DragDataType::File &&
        !AudioFileValidator::hasValidAudioExtension(data.filePath)) {
        if (m_showDropPreview) {
            m_showDropPreview = false;
            setDirty(true);
        }
        return AestraUI::DropFeedback::Invalid;
    }

    // Update target track and time as mouse moves
    int newTrack = getTrackAtPosition(position.y);
    
    // Explicit mapping: Workspace -> Grid -> Beat
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    float controlWidth = theme.getLayoutDimensions().trackControlsWidth;
    float gridStartX = getBounds().x + controlWidth + 5.0f;
    
    // REJECTION: If dropping on the control area
    if (position.x < gridStartX) {
        if (m_showDropPreview) {
            m_showDropPreview = false;
            setDirty(true);
            Log::info("[TrackManagerUI] Drag over rejected: Cursor in control area");
        }
        return AestraUI::DropFeedback::Invalid;
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
            // Move for clips; Copy for files and plugins
            return data.type == AestraUI::DragDataType::AudioClip ? 
                   AestraUI::DropFeedback::Move : AestraUI::DropFeedback::Copy;
        } else {
            m_showDropPreview = false;
            setDirty(true);
            return AestraUI::DropFeedback::Invalid;
        }
    }
    
    // Return appropriate feedback based on preview state
    if (m_showDropPreview) {
        return data.type == AestraUI::DragDataType::AudioClip ? 
               AestraUI::DropFeedback::Move : AestraUI::DropFeedback::Copy;
    }
    return AestraUI::DropFeedback::Invalid;
}

void TrackManagerUI::onDragLeave() {
    Log::info("[TrackManagerUI] Drag left");
    clearDropPreview();
    setDirty(true);
}

AestraUI::DropResult TrackManagerUI::onDrop(const AestraUI::DragData& data, const AestraUI::NUIPoint& position) {
    AestraUI::DropResult result;
    
    // 1. Calculate drop location
    int laneIndex = getTrackAtPosition(position.y);
    double rawTimeSeconds = std::max(0.0, getTimeAtPosition(position.x));
    
    // v3.0: We work strictly in beats for arrangement
    double rawTimeBeats = m_trackManager->getPlaylistModel().secondsToBeats(rawTimeSeconds);
    
    // Snap-to-grid logic (Canonical Beat-Space)
    double timePositionBeats = snapBeatToGrid(rawTimeBeats);
    
    auto& playlist = m_trackManager->getPlaylistModel();
    size_t laneCount = playlist.getLaneCount();
    
    Log::info("[TrackManagerUI] onDrop: position.y=" + std::to_string(position.y) + 
              ", laneIndex=" + std::to_string(laneIndex) + ", laneCount=" + std::to_string(laneCount));
    
    if (laneIndex < 0 || laneIndex > static_cast<int>(laneCount)) {
        result.accepted = false;
        result.message = "Invalid lane position";
        clearDropPreview();
        return result;
    }
    
    // Set target track index for logging
    result.targetTrackIndex = laneIndex;
    
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
    if (data.type == AestraUI::DragDataType::AudioClip) {
        ClipInstanceID clipId = ClipInstanceID::fromString(data.sourceClipIdString);
        
        if (clipId.isValid()) {
            auto cmd = std::make_shared<MoveClipCommand>(
                playlist, clipId, timePositionBeats, targetLaneId
            );
            m_trackManager->getCommandHistory().pushAndExecute(cmd);
            result.accepted = true;
            result.message = "Clip moved to lane " + std::to_string(laneIndex) + " at beat " + std::to_string(timePositionBeats);
            Log::info("[TrackManagerUI] Clip moved via PlaylistModel: " + data.sourceClipIdString);
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
    if (data.type == AestraUI::DragDataType::Pattern) {
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
                // Create clip instance manually and use command for undo support
                ClipInstance clip;
                clip.id = ClipInstanceID::generate();
                clip.startBeat = timePositionBeats;
                clip.durationBeats = duration;
                clip.patternId = pid;
                clip.sourceId = pid.value;
                
                auto cmd = std::make_shared<AddClipCommand>(playlist, targetLaneId, clip);
                m_trackManager->getCommandHistory().pushAndExecute(cmd);
                
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
    if (data.type == AestraUI::DragDataType::File) {
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
            // Helper to create clip once source is ready
            auto createClipFromSource = [this, source, sourceId, displayName = data.displayName, targetLaneId, timePositionBeats]() {
                // Remove from pending imports (animation cleanup)
                auto it = std::remove_if(m_pendingImports.begin(), m_pendingImports.end(), 
                    [&](const PendingImport& pi) {
                        return pi.laneId == targetLaneId && 
                               std::abs(pi.startBeat - timePositionBeats) < 0.001 &&
                               pi.displayName == displayName;
                    });
                if (it != m_pendingImports.end()) {
                    m_pendingImports.erase(it, m_pendingImports.end());
                    setDirty(true);
                }

                if (m_onClipLibraryChanged) {
                    m_onClipLibraryChanged();
                }

                if (!source || !source->isReady()) return;

                double durationSeconds = source->getDurationSeconds();
                double durationBeats = secondsToBeats(durationSeconds);
                Log::info("[TrackManagerUI] Duration: " + std::to_string(durationSeconds) + "s, beats: " + std::to_string(durationBeats));

                // Create Audio Pattern
                AudioSlicePayload payload;
                payload.audioSourceId = sourceId;
                payload.slices.push_back({0.0, static_cast<double>(source->getNumFrames())});
                
                auto& patternManager = m_trackManager->getPatternManager();
                PatternID patternId = patternManager.createAudioPattern(displayName, durationBeats, payload);
                
                if (patternId.isValid()) {
                    auto& playlist = m_trackManager->getPlaylistModel();
                    
                    // Create clip instance manually and use command for undo support
                    ClipInstance clip;
                    clip.id = ClipInstanceID::generate();
                    clip.startBeat = timePositionBeats;
                    clip.durationBeats = durationBeats;
                    clip.patternId = patternId;
                    clip.sourceId = patternId.value;
                    
                    auto cmd = std::make_shared<AddClipCommand>(playlist, targetLaneId, clip);
                    m_trackManager->getCommandHistory().pushAndExecute(cmd);
                    
                    refreshTracks();
                    invalidateCache();
                    scheduleTimelineMinimapRebuild();
                    Log::info("[TrackManagerUI] Clip added successfully via command");
                } else {
                    Log::error("[TrackManagerUI] PatternManager::createAudioPattern failed");
                }
            };

            if (!source->isReady()) {
                Log::info("[TrackManagerUI] Decoding new source (ASYNC): " + data.filePath);
                
                // Add to pending imports for visualizer
                PendingImport pending;
                pending.displayName = data.displayName;
                pending.laneId = targetLaneId;
                pending.startBeat = timePositionBeats;
                m_pendingImports.push_back(pending);
                setDirty(true);

                // Capture necessary data for async thread
                std::string filePath = data.filePath;
                std::string displayName = data.displayName;
                std::shared_ptr<TrackManager> tm = m_trackManager;

                // Launch decoding in background thread to prevent UI freeze
                std::thread([this, tm, sourceId, filePath, displayName, createClipFromSource, targetLaneId, timePositionBeats]() {
                    std::vector<float> decodedData;
                    uint32_t sampleRate = 0;
                    uint32_t numChannels = 0;
                    
                    auto lastUpdate = std::make_shared<std::chrono::steady_clock::time_point>(std::chrono::steady_clock::now());
                    
                    // Capture by value for the progress callback to avoid reference lifetime issues
                    auto progressCb = [this, targetLaneId, timePositionBeats, displayName, lastUpdate](float p) {
                        auto now = std::chrono::steady_clock::now();
                        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - *lastUpdate).count() < 16 && p < 1.0f) return;
                        *lastUpdate = now;

                        std::lock_guard<std::mutex> lock(m_pendingTasksMutex);
                        m_pendingTasks.push_back([this, targetLaneId, timePositionBeats, displayName, p]() {
                            for (auto& pi : m_pendingImports) {
                                if (pi.laneId == targetLaneId && 
                                    std::abs(pi.startBeat - timePositionBeats) < 0.001 &&
                                    pi.displayName == displayName) {
                                    pi.progress = p;
                                    setDirty(true);
                                    break;
                                }
                            }
                        });
                    };

                    // Heavy IO/Decoding operation
                    bool success = decodeAudioFile(filePath, decodedData, sampleRate, numChannels, progressCb);
                    
                    // Post completion task to main thread
                    {
                        std::lock_guard<std::mutex> lock(m_pendingTasksMutex);
                        m_pendingTasks.push_back([this, tm, sourceId, success, decodedData = std::move(decodedData), 
                                                 sampleRate, numChannels, displayName, createClipFromSource, filePath]() mutable {
                            
                            auto& sm = tm->getSourceManager();
                            ClipSource* src = sm.getSource(sourceId);
                            if (!src) return;
                            
                            if (success) {
                                auto buffer = std::make_shared<AudioBufferData>();
                                buffer->interleavedData = std::move(decodedData);
                                buffer->sampleRate = sampleRate;
                                buffer->numChannels = numChannels;
                                buffer->numFrames = buffer->interleavedData.size() / numChannels;
                                src->setBuffer(buffer);
                                
                                Log::info("[TrackManagerUI] Async load complete for: " + filePath);
                                
                                // Trigger waveform cache build
                                m_waveformBuilder.buildAsync(*src, [this, src](std::shared_ptr<Aestra::Audio::WaveformCache> cache) {
                                     if (cache) {
                                          std::lock_guard<std::mutex> lock(m_pendingTasksMutex);
                                          m_pendingTasks.push_back([this, src, cache]() {
                                              src->setWaveformCache(cache);
                                              Log::info("✅ Waveform cache ready for: " + src->getName());
                                              this->invalidateCache(); 
                                              this->m_backgroundNeedsUpdate = true;
                                              this->setDirty(true);
                                              // Refresh tracks to update UI with waveform data
                                              this->refreshTracks();
                                          });
                                     }
                                });
                                
                                // Queue clip creation to main thread via pending tasks
                                // This ensures UI updates happen on the main thread, not the background decode thread
                                std::lock_guard<std::mutex> lock(m_pendingTasksMutex);
                                m_pendingTasks.push_back([this, createClipFromSource]() {
                                    createClipFromSource();
                                });
                                
                            } else {
                                Log::error("[TrackManagerUI] Failed to decode file async: " + filePath);
                                // Queue cleanup to main thread
                                std::lock_guard<std::mutex> lock(m_pendingTasksMutex);
                                m_pendingTasks.push_back([this, createClipFromSource]() {
                                    createClipFromSource(); // Cleanup UI
                                });
                            }
                        });
                    }
                }).detach();
                
                result.accepted = true;
                result.message = "Importing...";
            } else {
                // Already loaded, proceed immediately
                createClipFromSource();
                result.accepted = true;
                result.message = "Imported: " + data.displayName;
            }
        } else {
            result.accepted = false;
            result.message = "Failed to create source";
        }
        
        clearDropPreview();
        return result;
    }
    
    // 5. Handle Plugin Drop
    if (data.type == AestraUI::DragDataType::Plugin) {
        Log::info("[TrackManagerUI] Plugin drop received: " + data.displayName);
        
        std::string pluginId = data.sourceClipIdString;
        if (!pluginId.empty()) {
            auto& pluginManager = Aestra::Audio::PluginManager::getInstance();
            
            // Validate target channel exists
            int channelIndex = laneIndex;
            auto channel = m_trackManager->getTrack(channelIndex);
            
            if (!channel) {
                result.accepted = false;
                result.message = "Target channel not found";
                clearDropPreview();
                return result;
            }

            // Check if chain has space (pre-check)
            auto& chain = channel->getEffectChain();
            if (chain.getFirstEmptySlot() >= Aestra::Audio::EffectChain::MAX_SLOTS) {
                result.accepted = false;
                result.message = "Effect chain full";
                clearDropPreview();
                return result;
            }

            // Request Plugin Creation (Async)
            // Captured variables must be kept alive. m_trackManager is a shared_ptr.
            std::string displayName = data.displayName;
            auto trackManager = m_trackManager;
            
            pluginManager.createInstanceByIdAsync(pluginId, [trackManager, channelIndex, displayName, pluginId](Aestra::Audio::PluginInstancePtr instance) {
                if (!instance) {
                    Log::error("[TrackManagerUI] Plugin creation failed for ID: " + pluginId);
                    return;
                }

                auto channel = trackManager->getTrack(channelIndex);
                if (!channel) return;

                auto& pluginManager = Aestra::Audio::PluginManager::getInstance();
                if (instance->initialize(pluginManager.getDefaultSampleRate(), pluginManager.getDefaultBlockSize())) {
                    instance->activate(); 
                    
                    auto& chain = channel->getEffectChain();
                    size_t slot = chain.getFirstEmptySlot();
                    
                    if (slot < Aestra::Audio::EffectChain::MAX_SLOTS) {
                        chain.insertPlugin(slot, instance);
                        Log::info("[TrackManagerUI] Added plugin (Async): " + displayName);
                    } else {
                        Log::warning("[TrackManagerUI] Effect chain became full during async load");
                    }
                } else {
                    Log::error("[TrackManagerUI] Plugin initialization failed for ID: " + pluginId);
                }
            });

            // Return immediate acceptance
            result.accepted = true;
            result.message = "Loading " + data.displayName + "...";
            
        } else {
            result.accepted = false;
            result.message = "Invalid plugin ID";
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
    if (!m_snapEnabled || m_snapSetting == AestraUI::SnapGrid::None) {
        return beat;
    }
    
    double grid = AestraUI::MusicTheory::getSnapDuration(m_snapSetting);
    if (grid <= 0.00001) return beat;
    
    // Round to nearest grid line
    double snappedBeats = std::round(beat / grid) * grid;
    
    return std::max(0.0, snappedBeats);
}

double TrackManagerUI::snapBeatToGridForward(double beat) const {
    if (!m_snapEnabled || m_snapSetting == AestraUI::SnapGrid::None) {
        return beat;
    }
    
    double grid = AestraUI::MusicTheory::getSnapDuration(m_snapSetting);
    if (grid <= 0.00001) return beat;
    
    // Snap forward to next grid line
    double snappedBeats = std::floor(beat / grid) * grid + grid;
    
    return std::max(0.0, snappedBeats);
}

// =============================================================================
// Helper Methods for Drop Target
// =============================================================================

int TrackManagerUI::getTrackAtPosition(float y) const {
    AestraUI::NUIRect bounds = getBounds();
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    
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
    AestraUI::NUIRect bounds = getBounds();
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    
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

void TrackManagerUI::renderDropPreview(AestraUI::NUIRenderer& renderer) {
    if (!m_showDropPreview || m_dropTargetTrack < 0) {
        return;
    }
    
    // Issue #50: Don't show skeleton preview for existing clip drags
    // Instant clip drag provides real-time visual feedback by moving the actual clip
    if (m_isDraggingClipInstant) {
        return;
    }
    
    AestraUI::NUIRect bounds = getBounds();
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    
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
    AestraUI::NUIRect trackHighlight(
        gridStartX,
        trackY,
        bounds.width - controlAreaWidth - 20,
        static_cast<float>(m_trackHeight)
    );
    AestraUI::NUIColor highlightColor(0.733f, 0.525f, 0.988f, 0.08f);  // Very subtle
    renderer.fillRect(trackHighlight, highlightColor);
    
    // Draw clip skeleton preview - EXACT same measurements as real clips
    // Real clips use: y + 2, height - 4 (see TrackUIComponent clippedClipBounds)
    if (timeX >= gridStartX && timeX <= bounds.right() - 20) {
        float previewWidth = 150.0f;  // Reasonable preview width
        
        // Clip skeleton bounds - matches actual clip positioning exactly
        AestraUI::NUIRect clipSkeleton(
            timeX,
            trackY + 2,  // Same as real clip: bounds.y + 2
            previewWidth,
            static_cast<float>(m_trackHeight) - 4  // Same as real clip: bounds.height - 4
        );
        
        // Semi-transparent fill
        AestraUI::NUIColor skeletonFill(0.733f, 0.525f, 0.988f, 0.25f);
        renderer.fillRect(clipSkeleton, skeletonFill);
        
        // Border matching clip style
        AestraUI::NUIColor skeletonBorder(0.733f, 0.525f, 0.988f, 0.7f);
        
        // Top border (thicker, like real clip)
        renderer.drawLine(
            AestraUI::NUIPoint(clipSkeleton.x, clipSkeleton.y),
            AestraUI::NUIPoint(clipSkeleton.x + clipSkeleton.width, clipSkeleton.y),
            2.0f,
            skeletonBorder
        );
        
        // Other borders
        renderer.drawLine(
            AestraUI::NUIPoint(clipSkeleton.x, clipSkeleton.y + clipSkeleton.height),
            AestraUI::NUIPoint(clipSkeleton.x + clipSkeleton.width, clipSkeleton.y + clipSkeleton.height),
            1.0f,
            skeletonBorder.withAlpha(0.5f)
        );
        renderer.drawLine(
            AestraUI::NUIPoint(clipSkeleton.x, clipSkeleton.y),
            AestraUI::NUIPoint(clipSkeleton.x, clipSkeleton.y + clipSkeleton.height),
            1.0f,
            skeletonBorder.withAlpha(0.5f)
        );
        renderer.drawLine(
            AestraUI::NUIPoint(clipSkeleton.x + clipSkeleton.width, clipSkeleton.y),
            AestraUI::NUIPoint(clipSkeleton.x + clipSkeleton.width, clipSkeleton.y + clipSkeleton.height),
            1.0f,
            skeletonBorder.withAlpha(0.5f)
        );
        
        // Name strip at top (like real clips have)
        float nameStripHeight = 16.0f;
        AestraUI::NUIRect nameStrip(
            clipSkeleton.x,
            clipSkeleton.y,
            clipSkeleton.width,
            nameStripHeight
        );
        renderer.fillRect(nameStrip, skeletonBorder.withAlpha(0.6f));
        
        // Get drag data for display name
        auto& dragManager = AestraUI::NUIDragDropManager::getInstance();
        if (dragManager.isDragging()) {
            const auto& dragData = dragManager.getDragData();
            std::string displayName = dragData.displayName;
            if (displayName.length() > 18) {
                displayName = displayName.substr(0, 15) + "...";
            }
            AestraUI::NUIPoint textPos(clipSkeleton.x + 4, clipSkeleton.y + 2);
            renderer.drawText(displayName, textPos, 11.0f, AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.9f));
        }
    }
}

void TrackManagerUI::renderDeleteAnimations(AestraUI::NUIRenderer& renderer) {
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
        
        // Subtle red ripple expanding from click point
        
        // Calculate ripple radius (smaller, more subtle)
        float maxRadius = 50.0f;
        float currentRadius = anim.progress * maxRadius;
        
        // Ripple alpha fades out as it expands
        float rippleAlpha = (1.0f - anim.progress) * 0.4f;
        
        // Draw single subtle expanding ring
        if (currentRadius > 0) {
            AestraUI::NUIColor ringColor(1.0f, 0.3f, 0.3f, rippleAlpha);
            
            // Draw a circle using lines
            const int segments = 24;
            for (int i = 0; i < segments; i++) {
                float angle1 = (float)i / segments * 2.0f * 3.14159f;
                float angle2 = (float)(i + 1) / segments * 2.0f * 3.14159f;
                
                AestraUI::NUIPoint p1(
                    anim.rippleCenter.x + std::cos(angle1) * currentRadius,
                    anim.rippleCenter.y + std::sin(angle1) * currentRadius
                );
                AestraUI::NUIPoint p2(
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

void TrackManagerUI::openTrackContextMenu(const ::AestraUI::NUIPoint& position, std::function<void()> onSendToAudition) {
    if (m_activeContextMenu) {
        detachContextMenu(m_activeContextMenu);
    }

    m_activeContextMenu = std::make_shared<AestraUI::NUIContextMenu>();
    auto menu = m_activeContextMenu;

    menu->addItem("Send Track to Audition", [onSendToAudition]() {
        if (onSendToAudition) onSendToAudition();
    });

    attachAndShowContextMenu(this, menu, position);
}

} // namespace Audio
} // namespace Aestra

void Aestra::Audio::TrackManagerUI::renderPendingImports(AestraUI::NUIRenderer& renderer) {
    if (m_pendingImports.empty() || !m_playlistVisible) return;

    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    
    float headerHeight = 38.0f;
    float rulerHeight = 28.0f;
    float horizontalScrollbarHeight = 24.0f;
    float controlAreaWidth = layout.trackControlsWidth;
    float gridStartX = getBounds().x + controlAreaWidth + 5.0f;
    float trackAreaTop = getBounds().y + headerHeight + horizontalScrollbarHeight + rulerHeight;
    
    // Tech/Holo Colors
    AestraUI::NUIColor cyan = themeManager.getColor("accentCyan");
    AestraUI::NUIColor holoFill = cyan.withAlpha(0.1f);
    AestraUI::NUIColor holoBorder = cyan.withAlpha(0.8f);
    
    for (auto& item : m_pendingImports) {
        // Find Y position based on Lane ID
        float yPos = -1.0f;
        
        // Find track index for this lane
        for(size_t i=0; i<m_trackUIComponents.size(); ++i) {
            if (m_trackUIComponents[i] && m_trackUIComponents[i]->getLaneId() == item.laneId) {
                 yPos = m_trackUIComponents[i]->getBounds().y;
                 break;
            }
        }
        
        // Skip if track not found (maybe scrolled out or invalid)
        if (yPos < 0) continue;
        
        // Calculate X position
        float xPos = gridStartX + (static_cast<float>(item.startBeat) * m_pixelsPerBeat) - m_timelineScrollOffset;
        float width = static_cast<float>(item.estimatedDurationBeats) * m_pixelsPerBeat;
        float height = static_cast<float>(m_trackHeight);
        
        AestraUI::NUIRect rect(xPos, yPos, width, height);
        
        // Clipping check (basic)
        if (rect.right() < gridStartX || rect.x > getBounds().right()) continue;
        if (rect.bottom() < trackAreaTop || rect.y > getBounds().bottom()) continue;

        // ANIMATION: Pulsing Border
        float pulse = (std::sin(item.animationTime * 10.0f) * 0.5f + 0.5f); // 0.0 to 1.0
        float alphaMult = 0.5f + (pulse * 0.5f); // 0.5 to 1.0
        
        renderer.fillRect(rect, holoFill);
        
        // PROGRESS BAR: Functional fill
        if (item.progress > 0.001f) {
             AestraUI::NUIRect progressRect = rect;
             progressRect.width *= std::min(1.0f, item.progress);
             renderer.fillRect(progressRect, cyan.withAlpha(0.4f));

             // Highlight edge of progress
             float edgeX = progressRect.right();
             renderer.drawLine(AestraUI::NUIPoint(edgeX, rect.y), AestraUI::NUIPoint(edgeX, rect.bottom()), 1.5f, cyan.withAlpha(0.8f));
        }

        renderer.strokeRect(rect, 1.5f, holoBorder.withAlpha(alphaMult));
        
        // ANIMATION: Scanning Bar (Subtle during progress)
        float scanAlpha = item.progress > 0.99f ? 0.0f : 0.4f * (1.0f - item.progress * 0.5f);
        if (scanAlpha > 0.01f) {
            float scanProgress = std::fmod(item.animationTime * 1.5f, 1.0f); // 0.0 to 1.0 loops
            float scanX = rect.x + (rect.width * scanProgress);
            AestraUI::NUIColor scanColor = cyan.withAlpha(scanAlpha * (1.0f - std::abs(scanProgress - 0.5f))); 
            renderer.drawLine(AestraUI::NUIPoint(scanX, rect.y), AestraUI::NUIPoint(scanX, rect.bottom()), 2.0f, scanColor);
        }
        
        // Text
        std::string progressStr = " (" + std::to_string((int)(item.progress * 100)) + "%)";
        std::string text = "ANALYZING: " + item.displayName + (item.progress > 0 ? progressStr : "");
        float fontSize = 10.0f;
        auto textSize = renderer.measureText(text, fontSize);
        
        // Center text in rect
        float textX = rect.x + (rect.width - textSize.width) * 0.5f;
        float textY = rect.y + (rect.height - textSize.height) * 0.5f + textSize.height; // approximate baseline
        
        // Ensure text stays within view if rect is partially off-screen
        float visibleLeft = std::max(rect.x, gridStartX);
        float visibleRight = std::min(rect.right(), getBounds().right());
        float visibleWidth = visibleRight - visibleLeft;
        
        if (visibleWidth > textSize.width + 10.0f) {
             textX = visibleLeft + (visibleWidth - textSize.width) * 0.5f;
             
             // Draw text background for readability
             AestraUI::NUIRect textBg(textX - 2, textY - fontSize, textSize.width + 4, fontSize + 2);
             renderer.fillRect(textBg, AestraUI::NUIColor(0,0,0,0.6f));
             
             renderer.drawText(text, AestraUI::NUIPoint(textX, textY), fontSize, cyan);
        }
    }
}
