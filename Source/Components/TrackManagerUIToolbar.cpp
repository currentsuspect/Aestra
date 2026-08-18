// © 2025 Aestra Studios All Rights Reserved. Licensed for personal & educational use only.
// TrackManagerUI — toolbar, tools, and tool cursors.
// Split out of the former monolithic TrackManagerUI.cpp — bodies moved verbatim.
#include "TrackManagerUI.h"

#include "../AestraCore/include/AestraLog.h"
#include "../AestraCore/include/AestraUnifiedProfiler.h"
#include "../AestraUI/Core/NUIDragDrop.h"
#include "../AestraUI/Core/NUICursorRegistry.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../AestraUI/Platform/NUIPlatformBridge.h"
#include "../AestraUI/Base/NUIButton.h"
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
// SECTION: Toolbar & Tools
// =============================================================================

// These icons are all tinted via NUIIcon::setColor at render time, and the tint
// is a flat RGB overwrite of every opaque pixel — so any colour baked into the
// SVG is discarded. The old hard-coded palette here (#AAAAAA / #FF6B6B /
// #4FC3F7 / #BB86FC) was never visible; it only made the source misleading.
// Everything is currentColor now, and shapes carry the meaning instead.
void TrackManagerUI::createToolIcons() {
    // === POINTER/SELECT TOOL ICON ===
    // Solid arrow. The old one filled grey and outlined it in a 0.5px darker
    // grey that could never resolve at toolbar size.
    const char* selectSvg =
        R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M6 2.8 L6 19.4 L10.3 15.2 L13.2 21.5 L16 20.2 L13.1 14 L18.9 14 Z"/></svg>)";
    m_selectToolIcon = std::make_shared<AestraUI::NUIIcon>(selectSvg);

    // === SPLIT TOOL ICON ===
    // A clip parted by the cut, with the blade line through the gap. The old
    // glyph was two rings and a grey X that read as scissors at best and as
    // nothing at 16px.
    const char* splitSvg =
        R"(<svg viewBox="0 0 24 24" fill="currentColor"><rect x="2.4" y="5.6" width="8" height="12.8" rx="1.6"/><rect x="13.6" y="5.6" width="8" height="12.8" rx="1.6"/><rect x="11.3" y="2.6" width="1.4" height="18.8" rx="0.7"/></svg>)";
    m_splitToolIcon = std::make_shared<AestraUI::NUIIcon>(splitSvg);

    // === MULTI-SELECT TOOL ICON ===
    // Corner brackets rather than a dashed outline: a 3,2 dash pattern turns to
    // mush once the icon is scaled down to toolbar size.
    const char* multiSelectSvg =
        R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M3 3h7v2.1H5.1V10H3V3zm11 0h7v7h-2.1V5.1H14V3zM3 14h2.1v4.9H10V21H3v-7zm15.9 0H21v7h-7v-2.1h4.9V14z"/></svg>)";
    m_multiSelectToolIcon = std::make_shared<AestraUI::NUIIcon>(multiSelectSvg);

    // === PAINT/STAMP TOOL ICON (Brush/stamp) ===
    const char* paintSvg =
        R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path d="M7 14c-1.66 0-3 1.34-3 3 0 1.31-1.16 2-2 2 .92 1.22 2.49 2 4 2 2.21 0 4-1.79 4-4 0-1.66-1.34-3-3-3zm13.71-9.37l-1.34-1.34a.996.996 0 00-1.41 0L9 12.25 11.75 15l8.96-8.96a.996.996 0 000-1.41z" fill="currentColor"/></svg>)";
    m_paintToolIcon = std::make_shared<AestraUI::NUIIcon>(paintSvg);

    // === MENU ICON (Hamburger) ===
    const char* menuSvg =
        R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M3 18h18v-2H3v2zm0-5h18v-2H3v2zm0-7v2h18V6H3z"/></svg>)";
    m_menuIcon = std::make_shared<AestraUI::NUIIcon>(menuSvg);

    // === VISUAL MOVE CURSOR (4-way arrow) ===
    // Solid arrowheads on a slim cross. The old version drew four chevrons plus
    // a full-width crosshair in 2px strokes — six subpaths competing in a 16px
    // box. It also carried a path-level stroke="#FFFFFF" that silently beat the
    // svg-level currentColor, so it never followed the theme.
    const char* moveSvg =
        R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M11.05 2.2h1.9v19.6h-1.9z"/><path d="M2.2 11.05h19.6v1.9H2.2z"/><path d="M12 1 15.1 5.2H8.9L12 1zm0 22-3.1-4.2h6.2L12 23zM1 12l4.2-3.1v6.2L1 12zm22 0-4.2 3.1V8.9L23 12z"/></svg>)";
    m_moveCursorIcon = std::make_shared<AestraUI::NUIIcon>(moveSvg);

    // Trim-edge stretch cursor: the canonical registry glyph (tinted at render
    // time), replacing the old ad-hoc drawLine arrow construction.
    m_trimCursorIcon = std::make_shared<AestraUI::NUIIcon>(AestraUI::nuiTrimResizeCursorSvg());
    // Minimap window-resize cursor: the canonical high-contrast ResizeEW glyph.
    m_resizeCursorIcon = std::make_shared<AestraUI::NUIIcon>(AestraUI::nuiCursorSvg(AestraUI::NUICursorStyle::ResizeEW));
    // Ruler / minimap-pan hand cursor: the canonical grab glyph.
    m_grabCursorIcon = std::make_shared<AestraUI::NUIIcon>(AestraUI::nuiCursorSvg(AestraUI::NUICursorStyle::Grab));

    if (!m_addTrackBtn) {
        m_addTrackBtn = std::make_shared<AestraUI::NUIButton>("");
        m_addTrackBtn->setBackgroundColor(AestraUI::NUIColor::transparent());
        m_addTrackBtn->setBorderEnabled(false);
        m_addTrackBtn->setTooltip("Add Track");
        m_addTrackBtn->setOnClick([this]() {
            addTrack();
        });
        addChild(m_addTrackBtn);
    }

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

        invalidateCache(); // Redraw toolbar with new selection

        // Note: System cursor is now always hidden by Main.cpp custom cursor system

        const char* toolNames[] = {"Select", "Split", "MultiSelect", "Paint", "Loop", "Draw", "Erase", "Mute", "Slip"};
        Log::info("Active tool changed to: " + std::string(toolNames[static_cast<int>(tool)]));
    }
}

bool TrackManagerUI::isMinimapResizeCursorActive() const {
    return m_timelineMinimap && m_timelineMinimap->isVisible() &&
           m_timelineMinimap->getCursorHint() == AestraUI::TimelineMinimapCursorHint::ResizeHorizontal;
}

bool TrackManagerUI::isRulerPointerActive() const {
    // While scrubbing/loop-dragging the grab stays even if the pointer leaves
    // the ruler row.
    if (m_isDraggingPlayhead || m_isDraggingLoopStart || m_isDraggingLoopEnd) {
        return true;
    }
    const AestraUI::NUIRect bounds = getBounds();
    // Same geometry as the events file's ruler row: below the header +
    // horizontal scrollbar, ruler-height tall, full width.
    const AestraUI::NUIRect ruler(bounds.x, bounds.y + kTimelineHeaderHeight + kTimelineHorizontalScrollbarHeight,
                                  bounds.width, kTimelineRulerHeight);
    return ruler.contains(m_lastMousePos);
}

bool TrackManagerUI::isCustomCursorActive() const {
    // Check if any custom cursor should be displayed (for exclusive cursor rendering)

    // 1. Trim edge hover/active
    for (const auto& trackUI : m_trackUIComponents) {
        if (!trackUI)
            continue;
        if (trackUI->isHoveringTrimEdge() || trackUI->isTrimming()) {
            return true;
        }
    }

    // 2. Split or Paint tool in grid area.
    // Use getBounds() (not getGlobalBounds()) so this suppression region shares
    // the exact coordinate basis renderToolCursor()/renderMinimapResizeCursor()
    // draw in. m_lastMousePos is window-space and this component's bounds are
    // already window-absolute, so getGlobalBounds() double-counts the parent
    // offset and shifts the region down, leaving a top strip where the arrow is
    // not suppressed while the tool cursor still draws (both cursors visible).
    if (m_currentTool == PlaylistTool::Split || m_currentTool == PlaylistTool::Paint) {
        AestraUI::NUIRect bounds = getBounds();
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        const float controlAreaWidth = layout.trackControlsWidth;
        const float gridStartX = bounds.x + controlAreaWidth + kTimelineGridInsetX;
        const float headerHeight = kTimelineHeaderHeight;
        const float rulerHeight = kTimelineRulerHeight;
        const float horizontalScrollbarHeight = kTimelineHorizontalScrollbarHeight;
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

    // 4. Minimap viewport bar body (pan) → grab hand.
    if (m_timelineMinimap && m_timelineMinimap->isVisible() && m_timelineMinimap->isViewportPanActive()) {
        return true;
    }

    // 5. Ruler scrub/loop/selection zone → grab hand.
    if (isRulerPointerActive()) {
        return true;
    }

    return false;
}

void TrackManagerUI::setSnapSetting(AestraUI::SnapGrid snap) {
    if (m_snapSetting == snap)
        return;

    m_snapSetting = snap;

    // Propagate to tracks
    for (auto& track : m_trackUIComponents) {
        if (track)
            track->setSnapSetting(snap);
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

    constexpr float headerHeight = 40.0f;                // Unified playlist header strip (Standardized)
    const float innerPad = themeManager.getSpacing("s"); // 8px from edge
    const float buttonSpacing = 6.0f;                    // Standardized gap for Aestra UI philosophy

    // Secondary toolbar controls are intentionally smaller than primary transport controls.
    const float buttonSize = 22.0f;
    const float iconSize = buttonSize;

    // Vertical centering for 24px button in 40px header (8px top/bottom padding)
    float currentX = bounds.x + innerPad;
    float currentY = bounds.y + (headerHeight - buttonSize) * 0.5f;

    // 2. Add Track (leftmost)
    m_addTrackBounds = AestraUI::NUIRect(currentX, currentY, iconSize, iconSize);
    float iconX = currentX + iconSize + buttonSpacing;
    float iconY = currentY;

    // 3. Tools Module
    float toolbarX = iconX;
    float toolbarY = iconY;

    const int numTools = 4; // Select, Split, MultiSelect, Paint
    float toolbarWidth = (iconSize * numTools) + (buttonSpacing * (numTools - 1));
    float toolbarHeight = iconSize;

    m_toolbarBounds = AestraUI::NUIRect(toolbarX, toolbarY, toolbarWidth, toolbarHeight);

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
    iconX += iconSize + (innerPad * 1.5f);

    // 5. Menu (rightmost)
    m_menuIconBounds = AestraUI::NUIRect(iconX, iconY, iconSize, iconSize);
    if (m_addTrackBtn) {
        m_addTrackBtn->setBounds(m_addTrackBounds);
    }
}

// =============================================================================
// SECTION: Rendering
// =============================================================================

void TrackManagerUI::renderToolbar(AestraUI::NUIRenderer& renderer) {
    // Update bounds before rendering
    updateToolbarBounds();

    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const float radius = themeManager.getRadius("s");
    const float standardIconSize = themeManager.getLayoutDimension("standardIconSize");
    auto utilityBg = themeManager.getColor("buttonBgDefault").withAlpha(0.94f);
    auto utilityHoverBg = themeManager.getColor("buttonBgHover").withAlpha(0.98f);
    auto utilityBorder = themeManager.getColor("border").withAlpha(0.24f);
    auto utilityHoverBorder = themeManager.getColor("border").withAlpha(0.35f);
    
    auto modeIdleBg = themeManager.getColor("buttonBgDefault").withAlpha(0.94f);
    auto modeHoverBg = themeManager.getColor("buttonBgHover").withAlpha(0.98f);
    auto modeActiveBg = themeManager.getColor("buttonBgActive");
    
    auto modeIdleBorder = themeManager.getColor("border").withAlpha(0.24f);
    auto modeHoverBorder = themeManager.getColor("border").withAlpha(0.35f);
    auto modeActiveBorder = themeManager.getColor("accentPrimary").withAlpha(0.34f);

    // Removed cluster plates to reduce chrome.

    auto drawUtilityButton = [&](const AestraUI::NUIRect& bounds, bool hovered) {
        const auto currentBg = hovered ? utilityHoverBg : utilityBg;
        const auto currentBorder = hovered ? utilityHoverBorder : utilityBorder;
        if (hovered) {
            renderer.fillRoundedRect(bounds, radius, currentBg);
            renderer.strokeRoundedRect(bounds, radius, 1.0f, currentBorder);
        }
    };

    // Helper lambda to draw icon with selection state
    auto drawToolIcon = [&](std::shared_ptr<AestraUI::NUIIcon>& icon, const AestraUI::NUIRect& bounds,
                            PlaylistTool tool, bool hovered) {
        bool isActive = (m_currentTool == tool);

        auto currentBg = modeIdleBg;
        auto currentBorder = modeIdleBorder;

        if (isActive) {
            currentBg = modeActiveBg;
            currentBorder = modeActiveBorder;
        } else if (hovered) {
            currentBg = modeHoverBg;
            currentBorder = modeHoverBorder;
        }

        if (isActive || hovered) {
            renderer.fillRoundedRect(bounds, radius, currentBg);
            renderer.strokeRoundedRect(bounds, radius, 1.0f, currentBorder);
        }

        // Draw icon
        if (icon) {
            const float iconSz = standardIconSize;
            AestraUI::NUIRect iconRect(std::round(bounds.x + (bounds.width - iconSz) * 0.5f),
                                       std::round(bounds.y + (bounds.height - iconSz) * 0.5f), iconSz, iconSz);
            icon->setBounds(iconRect);
            icon->setColor(isActive  ? themeManager.getColor("textPrimary").withAlpha(0.90f)
                           : hovered ? themeManager.getColor("textPrimary").withAlpha(0.70f)
                                     : themeManager.getColor("textPrimary").withAlpha(0.55f));
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
        auto currentBg = utilityBg;
        auto currentBorder = utilityBorder;
        if (followActive) {
            currentBg = themeManager.getColor("selection");
            currentBorder = themeManager.getColor("borderActive").withAlpha(0.64f);
        } else if (m_followPlayheadHovered) {
            currentBg = utilityHoverBg;
            currentBorder = utilityHoverBorder;
        }
        if (followActive || m_followPlayheadHovered) {
            renderer.fillRoundedRect(m_followPlayheadBounds, radius, currentBg);
            renderer.strokeRoundedRect(m_followPlayheadBounds, radius, 1.0f, currentBorder);
        }
    }

    if (m_followPlayheadIcon) {
        const float iconSz = standardIconSize;
        AestraUI::NUIRect iconRect(
            std::round(m_followPlayheadBounds.x + (m_followPlayheadBounds.width - iconSz) * 0.5f),
            std::round(m_followPlayheadBounds.y + (m_followPlayheadBounds.height - iconSz) * 0.5f), iconSz, iconSz);
        m_followPlayheadIcon->setBounds(iconRect);

        if (followActive) {
            m_followPlayheadIcon->setColor(themeManager.getColor("textPrimary").withAlpha(0.90f));
        } else {
            m_followPlayheadIcon->setColor(
                themeManager.getColor("textPrimary").withAlpha(m_followPlayheadHovered ? 0.70f : 0.35f));
        }
        m_followPlayheadIcon->onRender(renderer);
    }

    // Render Add Track button. The button owns hover/tooltip/click state; this
    // block draws the chrome and icon from the button's state so there is one
    // state source (same pattern as the transport glass buttons).
    {
        const bool addHovered = m_addTrackBtn && m_addTrackBtn->isHovered();
        auto currentBg = addHovered ? utilityHoverBg : utilityBg;
        auto currentBorder = addHovered ? utilityHoverBorder : utilityBorder;
        if (addHovered) {
            renderer.fillRoundedRect(m_addTrackBounds, radius, currentBg);
            renderer.strokeRoundedRect(m_addTrackBounds, radius, 1.0f, currentBorder);
        }
        if (m_addTrackIcon) {
            const float iconSz = standardIconSize;
            AestraUI::NUIRect iconRect(
                std::round(m_addTrackBounds.x + (m_addTrackBounds.width - iconSz) * 0.5f),
                std::round(m_addTrackBounds.y + (m_addTrackBounds.height - iconSz) * 0.5f), iconSz, iconSz);
            m_addTrackIcon->setBounds(iconRect);
            m_addTrackIcon->setColor(themeManager.getColor("textPrimary").withAlpha(addHovered ? 0.85f : 0.55f));
            m_addTrackIcon->onRender(renderer);
        }
    }

    // Render Menu (hamburger) button
    {
        bool menuActive = m_activeContextMenu != nullptr;
        auto currentBg = menuActive ? themeManager.getColor("accentPrimary").withAlpha(0.34f) : (m_menuHovered ? utilityHoverBg : utilityBg);
        auto currentBorder = menuActive ? themeManager.getColor("accentPrimary").withAlpha(0.34f) : (m_menuHovered ? utilityHoverBorder : utilityBorder);
        if (menuActive || m_menuHovered) {
            renderer.fillRoundedRect(m_menuIconBounds, radius, currentBg);
            renderer.strokeRoundedRect(m_menuIconBounds, radius, 1.0f, currentBorder);
        }
        if (m_menuIcon) {
            const float iconSz = standardIconSize;
            AestraUI::NUIRect iconRect(
                std::round(m_menuIconBounds.x + (m_menuIconBounds.width - iconSz) * 0.5f),
                std::round(m_menuIconBounds.y + (m_menuIconBounds.height - iconSz) * 0.5f), iconSz, iconSz);
            m_menuIcon->setBounds(iconRect);
            m_menuIcon->setColor(themeManager.getColor("textPrimary").withAlpha(
                menuActive ? 0.92f : (m_menuHovered ? 0.80f : 0.55f)));
            m_menuIcon->onRender(renderer);
        }
    }
}

bool TrackManagerUI::handleToolbarClick(const AestraUI::NUIPoint& position) {
    // Log click attempt
    // Log::info("TrackManagerUI::handleToolbarClick at " + std::to_string(position.x) + ", " +
    // std::to_string(position.y));

    if (m_menuIconBounds.contains(position)) {
        Log::info("TrackManagerUI: Menu Icon Clicked! Bounds: " + std::to_string(m_menuIconBounds.x) + "," +
                  std::to_string(m_menuIconBounds.y));

        // Defensive only — NOT the toggle. handleContextMenuMouse runs before this
        // handler (see onMouseEvent) and always clears m_activeContextMenu when a
        // click lands outside the menu, so by the time a mouse click reaches here the
        // pointer is already null. The toggle lives there, where the dismissal
        // happens. This branch stays because "button clicked while its own menu is
        // open" should close rather than rebuild for any caller that does reach it.
        //
        // What this block DID replace was a genuine artifact: an else-branch that
        // constructed a menu, followed by an `if (!m_activeContextMenu)` that
        // constructed another, under a comment reading "RE-ADDING MISSING LOGIC
        // because I am replacing the block".
        if (m_activeContextMenu) {
            Log::info("TrackManagerUI: Closing existing menu");
            detachContextMenu(m_activeContextMenu);
            m_activeContextMenu = nullptr;
            setDirty(true);
            return true;
        }

        Log::info("TrackManagerUI: Creating new context menu");
        m_activeContextMenu = std::make_shared<AestraUI::NUIContextMenu>();
        auto menu = m_activeContextMenu;

        // === SNAP SUBMENU ===
        auto snapMenu = std::make_shared<AestraUI::NUIContextMenu>();
        auto snaps = AestraUI::MusicTheory::getSnapOptions();
        for (auto snap : snaps) {
            bool isSelected = (snap == m_snapSetting);
            snapMenu->addRadioItem(AestraUI::MusicTheory::getSnapName(snap), "SnapGroup", isSelected,
                                   [this, snap]() { setSnapSetting(snap); });
        }
        menu->addSubmenu("Snap", snapMenu);

        // === LOOP SUBMENU ===
        auto loopMenu = std::make_shared<AestraUI::NUIContextMenu>();
        auto addLoopItem = [&](const std::string& name, int id) {
            bool isSelected = (timelineLoopPresetId(m_loopPreset) == id);
            loopMenu->addRadioItem(name, "LoopGroup", isSelected, [this, id, name]() {
                // m_loopPreset is assigned AFTER the branches below, not here. Two of
                // them can decline to apply — "Selection" with no selection, "Project"
                // with no resolvable extent — and assigning up front left the radio
                // item checked for a preset that had not taken effect, so the menu
                // showed "Selection" while looping was actually off.
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
                    // Project: loop to arrangement end when clips exist, otherwise fall back
                    // to a sane empty-project extent instead of leaving the preset in limbo.
                    double projectEnd = 0.0;
                    if (m_trackManager) {
                        projectEnd = m_trackManager->getPlaylistModel().getTotalDurationBeats();
                    }

                    if (projectEnd <= 0.001) {
                        projectEnd = resolveProjectLoopEndBeat(projectEnd, m_beatsPerBar);
                        Log::info("Loop Project: Empty arrangement fallback -> " + std::to_string(projectEnd) +
                                  " beats");
                    }

                    if (projectEnd > 0.001) {
                        loopStartBeat = 0.0;
                        loopEndBeat = projectEnd;
                        loopEnabled = true;
                    } else {
                        loopEnabled = false;
                        Log::warning("Loop Project: Could not resolve a valid project extent");
                    }
                }

                // What is recorded is what actually happened. A preset that meant to
                // enable looping but could not falls back to Off (0), because that is
                // the state the project is now in.
                const bool intendedToEnable = (id != 0);
                const int appliedPreset = (intendedToEnable && !loopEnabled) ? 0 : id;
                m_loopPreset = timelineLoopPresetFromId(appliedPreset);

                if (m_loopPreset != TimelineLoopPreset::Selection) {
                    m_hasRulerSelection = false;
                    m_hoveringLoopStart = false;
                    m_hoveringLoopEnd = false;
                }

                if (m_loopPreset == TimelineLoopPreset::Selection && loopEnabled) {
                    updateSelectionLoopRegion(loopStartBeat, loopEndBeat);
                } else {
                    setLoopRegion(loopStartBeat, loopEndBeat, loopEnabled);
                }

                if (m_loopPreset != TimelineLoopPreset::Selection && m_onLoopRegionUpdate) {
                    if (loopEnabled) {
                        m_onLoopRegionUpdate(loopStartBeat, loopEndBeat);
                    } else {
                        m_onLoopRegionUpdate(0.0, 0.0);
                    }
                }

                if (m_onLoopPresetChanged)
                    m_onLoopPresetChanged(appliedPreset);
                invalidateCache();
                if (appliedPreset == id) {
                    Log::info("Loop preset changed to: " + name);
                } else {
                    Log::info("Loop preset '" + name + "' could not be applied; loop is Off");
                }
            });
        };

        addLoopItem("Off", timelineLoopPresetId(TimelineLoopPreset::Off));
        addLoopItem("1 Bar", timelineLoopPresetId(TimelineLoopPreset::OneBar));
        addLoopItem("2 Bars", timelineLoopPresetId(TimelineLoopPreset::TwoBars));
        addLoopItem("4 Bars", timelineLoopPresetId(TimelineLoopPreset::FourBars));
        addLoopItem("8 Bars", timelineLoopPresetId(TimelineLoopPreset::EightBars));
        addLoopItem("Selection", timelineLoopPresetId(TimelineLoopPreset::Selection));
        addLoopItem("Project", timelineLoopPresetId(TimelineLoopPreset::Project));
        menu->addSubmenu("Loop", loopMenu);

        // === AUDITION MODE ===
        menu->addSeparator();
        menu->addItem("Send Track to Audition", [this]() {
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

                // Display-row index, not playlist index: rows are track-grouped
                // (FD-14 §10), so the lane must come from the row widget itself.
                if (trackIndex < static_cast<int>(m_trackUIComponents.size())) {
                    PlaylistLaneID laneId = m_trackUIComponents[trackIndex]->getLaneId();
                    if (laneId.isValid()) {
                        const PlaylistLane* lane = playlist.getLane(laneId);
                        std::string trackName = lane ? lane->name : ("Track " + std::to_string(trackIndex + 1));
                        // Display rows are not mixer channels after lane grouping;
                        // resolve like the per-track audition button does.
                        const uint32_t channelIndex = resolveLaneToChannelIndex(lane, static_cast<uint32_t>(trackIndex));
                        m_onSendToAudition(channelIndex, trackName);
                        Log::info("Sending track to Audition: " + trackName);
                    }
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
        attachAndShowContextMenu(this, menu,
                                 AestraUI::NUIPoint(m_menuIconBounds.x, m_menuIconBounds.y + m_menuIconBounds.height));

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
        if (!trackUI)
            continue;
        if (trackUI->isHoveringTrimEdge() || trackUI->isTrimming()) {
            isHoveringTrimEdge = true;
            isTrimming = trackUI->isTrimming();
            break;
        }
    }

    if (isHoveringTrimEdge) {
        // Render horizontal resize cursor — the canonical registry glyph,
        // tinted for the neutral/trimming states.
        auto& theme = AestraUI::NUIThemeManager::getInstance();
        AestraUI::NUIColor cursorColor =
            isTrimming ? theme.getColor("accentCyan") : theme.getColor("textPrimary").withAlpha(0.78f);

        if (m_trimCursorIcon) {
            m_trimCursorIcon->setColor(cursorColor);
            AestraUI::NUIRect iconRect(position.x - 9, position.y - 9, 18, 18);
            m_trimCursorIcon->setBounds(iconRect);
            m_trimCursorIcon->onRender(renderer);
        }

        return; // Skip other tool cursors when resize cursor is active
    }

    // Ruler scrub/loop zone and minimap viewport-pan → grab hand (the draggable
    // navigation surfaces). Trim (above), split/paint tools and minimap-edge
    // resize all take precedence over this.
    if (isRulerPointerActive() || (m_timelineMinimap && m_timelineMinimap->isVisible() &&
                                   m_timelineMinimap->isViewportPanActive())) {
        if (m_grabCursorIcon) {
            m_grabCursorIcon->setBounds(AestraUI::NUIRect(position.x - 9, position.y - 9, 18, 18));
            m_grabCursorIcon->onRender(renderer);
        }
        return;
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
    float gridStartX = bounds.x + controlAreaWidth + kTimelineGridInsetX;
    float headerHeight = kTimelineHeaderHeight;
    float rulerHeight = kTimelineRulerHeight;
    float horizontalScrollbarHeight = kTimelineHorizontalScrollbarHeight;
    float trackAreaTop = bounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;

    AestraUI::NUIRect gridBounds(gridStartX, trackAreaTop, bounds.width - controlAreaWidth - 20.0f,
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
            if (!trackUI)
                continue;
            const auto& allClipBounds = trackUI->getAllClipBounds();
            for (auto it = allClipBounds.begin(); it != allClipBounds.end(); ++it) {
                const auto& clipBounds = it->second;
                if (clipBounds.contains(AestraUI::NUIPoint(position.x, lineY))) {
                    float lineTop = clipBounds.y;
                    float lineBottom = clipBounds.y + clipBounds.height;
                    float y = lineTop;
                    while (y < lineBottom) {
                        float dashEnd = std::min(y + dashLength, lineBottom);
                        renderer.drawLine(AestraUI::NUIPoint(lineX, y), AestraUI::NUIPoint(lineX, dashEnd), 2.0f,
                                          splitColor);
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
            if (!trackUI)
                continue;
            const auto& allClipBounds = trackUI->getAllClipBounds();
            for (auto it = allClipBounds.begin(); it != allClipBounds.end(); ++it) {
                if (it->second.contains(position)) {
                    hoveringClip = true;
                    break;
                }
            }
            if (hoveringClip)
                break;
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

void TrackManagerUI::renderMinimapResizeCursor(AestraUI::NUIRenderer& renderer, const AestraUI::NUIPoint& position) {
    // === EXCLUSION: Don't render minimap resize cursor if any other custom cursor is active ===

    // 1. Check if trim cursor is active (hovering or actively trimming clip edges)
    for (auto& trackUI : m_trackUIComponents) {
        if (!trackUI)
            continue;
        if (trackUI->isHoveringTrimEdge() || trackUI->isTrimming()) {
            return; // Trim cursor takes priority
        }
    }

    // 2. Split and Paint tools own the cursor while we're over the clip grid.
    if (m_currentTool == PlaylistTool::Split || m_currentTool == PlaylistTool::Paint) {
        AestraUI::NUIRect bounds = getBounds();
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        const float controlAreaWidth = layout.trackControlsWidth;
        const float gridStartX = bounds.x + controlAreaWidth + kTimelineGridInsetX;
        const float headerHeight = kTimelineHeaderHeight;
        const float rulerHeight = kTimelineRulerHeight;
        const float horizontalScrollbarHeight = kTimelineHorizontalScrollbarHeight;
        const float trackAreaTop = bounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;

        AestraUI::NUIRect gridBounds(gridStartX, trackAreaTop, bounds.width - controlAreaWidth - 20.0f,
                                     bounds.height - headerHeight - rulerHeight - horizontalScrollbarHeight);
        if (gridBounds.contains(position)) {
            return; // Tool cursor takes priority in grid area
        }
    }

    // 3. Only show minimap resize cursor if mouse is actually OVER the minimap
    if (!m_timelineMinimap || !m_timelineMinimap->isVisible()) {
        return;
    }

    // Check if mouse is within minimap bounds
    AestraUI::NUIRect minimapBounds = m_timelineMinimap->getBounds();
    if (!minimapBounds.contains(position)) {
        return; // Mouse not over minimap - no minimap cursor
    }

    const bool wantsResizeCursor =
        (m_timelineMinimap->getCursorHint() == AestraUI::TimelineMinimapCursorHint::ResizeHorizontal);

    if (!wantsResizeCursor) {
        // No resize cursor needed, Main.cpp renders default arrow
        return;
    }

    // Render custom resize cursor — the canonical ResizeEW registry glyph
    // (system cursor always hidden by Main.cpp). Same asset the custom-cursor
    // overlay uses for horizontal resize everywhere.
    if (m_resizeCursorIcon) {
        m_resizeCursorIcon->setBounds(AestraUI::NUIRect(position.x - 9, position.y - 9, 18, 18));
        m_resizeCursorIcon->onRender(renderer);
    }
}

void TrackManagerUI::performSplitAtPosition(int laneIndex, double timeSeconds) {
    auto& playlist = m_trackManager->getPlaylistModel();
    // Display-row index, not playlist index: rows are track-grouped
    // (FD-14 §10), so the lane must come from the row widget itself.
    if (laneIndex < 0 || laneIndex >= static_cast<int>(m_trackUIComponents.size()))
        return;
    PlaylistLaneID laneId = m_trackUIComponents[laneIndex]->getLaneId();
    if (!laneId.isValid())
        return;

    double bpm = playlist.getBPM();
    double splitBeat = timeSeconds * (bpm / 60.0);

    // Snap split position to grid
    splitBeat = snapBeatToGrid(splitBeat);

    auto* lane = playlist.getLane(laneId);
    if (!lane)
        return;

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

} // namespace Audio
} // namespace Aestra
