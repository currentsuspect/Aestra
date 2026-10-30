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

// The tools that draw their own glyph beside the pointer while it is over the grid.
// Draw only shows its pencil when it has a clip to stamp; with nothing to stamp it behaves like Select and keeps the arrow.
inline bool toolOwnsCursor(PlaylistTool t, bool canPaint) {
    return t == PlaylistTool::Split || t == PlaylistTool::Mute || (t == PlaylistTool::Paint && canPaint);
}

// A cursor glyph with a soft dark edge, so a white glyph stays readable over bright clips as well as the dark grid.
static void drawToolCursorGlyph(AestraUI::NUIRenderer& renderer, const std::shared_ptr<AestraUI::NUIIcon>& icon,
                                AestraUI::NUIRect rect) {
    if (!icon) return;
    const AestraUI::NUIRect shadow(rect.x + 1.0f, rect.y + 1.0f, rect.width, rect.height);
    icon->setColor(AestraUI::NUIColor(0, 0, 0, 150));
    icon->setBounds(shadow);
    icon->onRender(renderer);
    icon->setColor(AestraUI::NUIColor(255, 255, 255, 255));
    icon->setBounds(rect);
    icon->onRender(renderer);
}

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

    // === PAINT/DRAW TOOL ICON ===
    // A pencil: you draw clips in with it. The Material paintbrush it replaces
    // was the stock glyph every web app ships, and read as "styling", not "draw".
    const char* paintSvg =
        R"(<svg viewBox="0 0 24 24"><path fill="currentColor" fill-rule="evenodd" d="M15.9 3.1A1.9 1.9 0 0 1 18.6 3.1L20.9 5.4A1.9 1.9 0 0 1 20.9 8.1L9.1 19.9 3.4 20.6 4.1 14.9Z M14.5 6.7L17.3 9.5 18.2 8.6 15.4 5.8Z"/></svg>)";
    m_paintToolIcon = std::make_shared<AestraUI::NUIIcon>(paintSvg);

    // === MUTE TOOL ICON ===
    // A speaker with a cross: the clip goes quiet. Solid, like the other tool glyphs.
    const char* muteSvg =
        R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M2.6 9.4v5.2h3.7l4.6 4V5.4l-4.6 4z"/><path d="M14.3 9.2l1.4-1.4 2.4 2.4 2.4-2.4 1.4 1.4-2.4 2.4 2.4 2.4-1.4 1.4-2.4-2.4-2.4 2.4-1.4-1.4 2.4-2.4z"/></svg>)";
    m_muteToolIcon = std::make_shared<AestraUI::NUIIcon>(muteSvg);

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

    // Trim-edge stretch cursor: the same white, outlined ResizeEW glyph every
    // other resize uses. It is drawn untinted — a grey tint read as a disabled,
    // second-class cursor next to the white overlay cursors.
    m_trimCursorIcon =
        std::make_shared<AestraUI::NUIIcon>(AestraUI::nuiCursorSvg(AestraUI::NUICursorStyle::ResizeEW));
    // Minimap window-resize cursor: the canonical high-contrast ResizeEW glyph.
    m_resizeCursorIcon = std::make_shared<AestraUI::NUIIcon>(AestraUI::nuiCursorSvg(AestraUI::NUICursorStyle::ResizeEW));
    // Ruler / minimap-pan hand cursor: the canonical grab glyph.
    m_grabCursorIcon = std::make_shared<AestraUI::NUIIcon>(AestraUI::nuiCursorSvg(AestraUI::NUICursorStyle::Grab));
    // The same hand closed, shown only while the drag is actually happening.
    m_grabbingCursorIcon =
        std::make_shared<AestraUI::NUIIcon>(AestraUI::nuiCursorSvg(AestraUI::NUICursorStyle::Grabbing));

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
        m_showSplitCursor = toolOwnsCursor(tool, true);

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
    // A hidden timeline receives no mouse events, so m_lastMousePos freezes at
    // whatever position it had when the view switched — gating on visibility
    // keeps a frozen ruler hit from suppressing the cursor in other views.
    if (!isVisible()) {
        return false;
    }
    // While scrubbing/loop/selection-dragging the grab stays even if the
    // pointer leaves the ruler row (selection drags extend below the ruler).
    if (m_isDraggingPlayhead || m_isDraggingLoopStart || m_isDraggingLoopEnd || m_isDraggingRulerSelection) {
        return true;
    }
    const AestraUI::NUIRect bounds = getBounds();
    // Hovering a loop handle claims the grab hand anywhere in the ruler row —
    // handles may sit over the toolbar corner's dead zone when scrolled.
    if ((m_hoveringLoopStart || m_hoveringLoopEnd) && m_lastMousePos.y >= bounds.y + kTimelineRulerTop &&
        m_lastMousePos.y < bounds.y + kTimelineTimeBandHeight) {
        return true;
    }
    // Same geometry as the events file's ruler row, but starting after the
    // control-area column: the grab hand belongs over scrubbable ruler only.
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    const float rulerStartX = bounds.x + layout.trackControlsWidth + kTimelineGridInsetX;
    const AestraUI::NUIRect ruler(rulerStartX,
                                  bounds.y + kTimelineRulerTop,
                                  std::max(0.0f, bounds.width - layout.trackControlsWidth - kTimelineGridInsetX),
                                  kTimelineRulerHeight);
    return ruler.contains(m_lastMousePos);
}

bool TrackManagerUI::isCustomCursorActive() const {
    // Check if any custom cursor should be displayed (for exclusive cursor rendering)
    // A hidden timeline receives no events, so every hover state below is frozen
    // — never claim the cursor from a view that isn't on screen.
    if (!isVisible()) {
        return false;
    }

    // Hover claims below are made from m_lastMousePos, which only moves when a
    // mouse event reaches the timeline. A popup above it (a clip context menu, a
    // dialog) consumes those events and freezes that position where the pointer
    // left from. Honouring a claim from a frozen position hid the real pointer
    // and drew a stale tool glyph instead: the invisible cursor over context
    // menus. Active drags own the pointer and are exempt.
    const bool hoverCurrent = isHoverPointerCurrent();

    // 1. Trim edge active, or hovered from a current pointer position
    for (const auto& trackUI : m_trackUIComponents) {
        if (!trackUI)
            continue;
        if (trackUI->isTrimming() || (hoverCurrent && trackUI->isHoveringTrimEdge())) {
            return true;
        }
    }

    // 2. Split or Paint tool in grid area.
    // Use getBounds() (not getGlobalBounds()) so this suppression region shares
    // the exact coordinate basis renderToolCursor()/renderMinimapResizeCursor()
    // draw in. m_lastMousePos is window-space and this component's bounds are
    // already window-absolute, so getGlobalBounds() (gone since V8-X2b 6b) double-counted the parent
    // offset and shifts the region down, leaving a top strip where the arrow is
    // not suppressed while the tool cursor still draws (both cursors visible).
    if (hoverCurrent && toolOwnsCursor(m_currentTool, canPaintClip())) {
        AestraUI::NUIRect bounds = getBounds();
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        const float controlAreaWidth = layout.trackControlsWidth;
        const float gridStartX = bounds.x + controlAreaWidth + kTimelineGridInsetX;
        const float trackAreaTop = bounds.y + kTimelineTimeBandHeight;

        AestraUI::NUIRect gridBounds(gridStartX, trackAreaTop, bounds.width - controlAreaWidth - 20.0f,
                                     bounds.height - kTimelineTimeBandHeight);
        if (gridBounds.contains(m_lastMousePos)) {
            return true;
        }
    }

    // 3. Minimap resize cursor
    if (hoverCurrent && m_timelineMinimap && m_timelineMinimap->isVisible()) {
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
    if (hoverCurrent && isRulerPointerActive()) {
        return true;
    }

    return false;
}

void TrackManagerUI::setWindowPointerPosition(float x, float y) {
    m_windowPointerPos = AestraUI::NUIPoint(x, y);
    m_hasWindowPointerPos = true;
}

bool TrackManagerUI::isHoverPointerCurrent() const {
    return isTimelineHoverPointerCurrent(m_lastMousePos.x, m_lastMousePos.y, m_windowPointerPos.x,
                                         m_windowPointerPos.y, m_hasWindowPointerPos);
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

// FD-50 board 7: the toolbar is the band's own first row, across the timeline:
//   [ Select  Draw  Split  Erase ]  SNAP 1/4 ▾   (follow)          ☰   Fit song   + Track
// Tools are words, not bare icons; the snap value is always visible and opens the snap menu.
namespace {
constexpr float kToolbarControlHeight = 22.0f;
constexpr float kToolButtonWidth = 30.0f; // Select, Draw, Split, Mute: a 14 px white glyph each; the active one is named beside the group
constexpr float kToolNameSlot = 52.0f;
} // namespace

void TrackManagerUI::updateToolbarBounds() {
    const AestraUI::NUIRect bounds = getBounds();
    const float rowY = bounds.y;
    const float y = rowY + std::round((kTimelineToolbarHeight - kToolbarControlHeight) * 0.5f);
    const float h = kToolbarControlHeight;

    float x = bounds.x + 10.0f;
    const float groupLeft = x;
    AestraUI::NUIRect* tools[] = {&m_selectToolBounds, &m_paintToolBounds, &m_splitToolBounds, &m_muteToolBounds};
    for (int i = 0; i < 4; ++i) {
        *tools[i] = AestraUI::NUIRect(x + 2.0f, y + 2.0f, kToolButtonWidth, h - 4.0f);
        x += kToolButtonWidth;
    }
    m_toolbarBounds = AestraUI::NUIRect(groupLeft, y, x + 4.0f - groupLeft, h);
    x += 4.0f + 8.0f + kToolNameSlot; // the active tool's name sits here, so nothing shifts when the tool changes
    m_multiSelectToolBounds = AestraUI::NUIRect(); // not on the board; empty-grid drag marquees (ruling 2026-09-23)

    x += 10.0f;
    m_snapBounds = AestraUI::NUIRect(x, y, 78.0f, h);
    x += 78.0f + 10.0f;
    m_followPlayheadBounds = AestraUI::NUIRect(x, y, h, h);

    float right = bounds.right() - kTimelineScrollbarWidth - 10.0f;
    m_addTrackBounds = AestraUI::NUIRect(right - 64.0f, y, 64.0f, h);
    right -= 64.0f + 8.0f;
    m_fitSongBounds = AestraUI::NUIRect(right - 66.0f, y, 66.0f, h);
    right -= 66.0f + 8.0f;
    m_menuIconBounds = AestraUI::NUIRect(right - h, y, h, h);

    // Hit testing for "is this press on the toolbar" covers the whole row.
    m_toolbarCornerBounds = AestraUI::NUIRect(bounds.x, rowY, bounds.width - kTimelineScrollbarWidth,
                                              kTimelineToolbarHeight);
    if (m_addTrackBtn) {
        m_addTrackBtn->setBounds(m_addTrackBounds);
    }
}

void TrackManagerUI::fitSong() {
    const float gridWidth = getTimelineGridWidthPixels();
    if (gridWidth <= 0.0f || !m_trackManager) return;
    double endBeat = m_trackManager->getPlaylistModel().getTotalDurationBeats();
    if (!(endBeat > 0.001)) endBeat = 4.0 * m_beatsPerBar; // an empty song fits four bars
    setHorizontalZoom(static_cast<float>(gridWidth / (endBeat * 1.04))); // a little air after the last clip
    setHorizontalScroll(0.0f);
    setDirty(true);
}

void TrackManagerUI::showSnapMenuAt(const AestraUI::NUIPoint& anchor) {
    if (m_activeContextMenu) {
        detachContextMenu(m_activeContextMenu);
        m_activeContextMenu = nullptr;
    }
    m_activeContextMenu = std::make_shared<AestraUI::NUIContextMenu>();
    for (auto snap : AestraUI::MusicTheory::getSnapOptions()) {
        m_activeContextMenu->addRadioItem(AestraUI::MusicTheory::getSnapName(snap), "SnapGroup", snap == m_snapSetting,
                                          [this, snap]() { setSnapSetting(snap); });
    }
    attachAndShowContextMenu(this, m_activeContextMenu, anchor);
}

// =============================================================================
// SECTION: Rendering
// =============================================================================

void TrackManagerUI::renderToolbar(AestraUI::NUIRenderer& renderer) {
    updateToolbarBounds();
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    const AestraUI::NUIRect bounds = getBounds();
    const auto text = theme.getColor("textPrimary");
    const auto border = theme.getColor("border");
    const float fontSize = theme.getFontSize("s");
    const float smallSize = theme.getFontSize("xs");

    // The row: a raised strip with a hairline under it, so the band reads as toolbar, minimap, ruler.
    const AestraUI::NUIRect row(bounds.x, bounds.y, bounds.width, kTimelineToolbarHeight);
    renderer.fillRect(row, theme.getColor("surfaceRaised").withAlpha(0.35f));
    renderer.fillRect({row.x, row.bottom() - 1.0f, row.width, 1.0f}, border.withAlpha(0.35f));

    const auto outlined = [&](const AestraUI::NUIRect& r, bool hovered) {
        if (hovered) renderer.fillRoundedRect(r, 4.0f, theme.getColor("buttonBgHover").withAlpha(0.9f));
        renderer.strokeRoundedRect(r, 4.0f, 1.0f, border.withAlpha(hovered ? 0.55f : 0.35f));
    };

    // Tools: one segmented group of white glyphs; the active one is filled and fully lit, and named beside the group.
    renderer.strokeRoundedRect(m_toolbarBounds, 5.0f, 1.0f, border.withAlpha(0.35f));
    struct Tool { const AestraUI::NUIRect* r; PlaylistTool tool; const char* label; bool hovered; };
    const Tool tools[] = {
        {&m_selectToolBounds, PlaylistTool::Select, "Select", m_selectToolHovered},
        {&m_paintToolBounds, PlaylistTool::Paint, "Draw", m_paintToolHovered},
        {&m_splitToolBounds, PlaylistTool::Split, "Split", m_splitToolHovered},
        {&m_muteToolBounds, PlaylistTool::Mute, "Mute", m_muteToolHovered},
    };
    constexpr float kGlyph = 14.0f;
    for (const auto& t : tools) {
        const bool active = m_currentTool == t.tool;
        if (active) {
            renderer.fillRoundedRect(*t.r, 3.0f, theme.getColor("accentPrimary").withAlpha(0.28f));
        } else if (t.hovered) {
            renderer.fillRoundedRect(*t.r, 3.0f, theme.getColor("buttonBgHover").withAlpha(0.9f));
        }
        const auto& glyph = t.tool == PlaylistTool::Select ? m_selectToolIcon
                            : t.tool == PlaylistTool::Paint ? m_paintToolIcon
                            : t.tool == PlaylistTool::Split ? m_splitToolIcon
                                                            : m_muteToolIcon;
        if (glyph) {
            glyph->setIconSize(kGlyph, kGlyph);
            glyph->setBounds({std::round(t.r->x + (t.r->width - kGlyph) * 0.5f), std::round(t.r->y + (t.r->height - kGlyph) * 0.5f), kGlyph, kGlyph});
            glyph->setColor(text.withAlpha(active ? 1.0f : (t.hovered ? 0.9f : 0.62f)));
            glyph->onRender(renderer);
        }
        if (active) {
            const AestraUI::NUIRect slot(m_toolbarBounds.right() + 8.0f, m_toolbarBounds.y, kToolNameSlot, m_toolbarBounds.height);
            renderer.drawText(t.label, {slot.x, renderer.calculateTextY(slot, fontSize)}, fontSize, text.withAlpha(0.8f));
        }
    }

    // SNAP value: small caps label, then the current value and a caret.
    {
        const AestraUI::NUIRect& r = m_snapBounds;
        if (m_snapHovered) renderer.fillRoundedRect(r, 4.0f, theme.getColor("buttonBgHover").withAlpha(0.9f));
        const float ty = renderer.calculateTextY(r, smallSize);
        renderer.drawText("SNAP", {r.x + 6.0f, ty}, smallSize, text.withAlpha(0.45f));
        const std::string value = (m_snapEnabled && m_snapSetting != AestraUI::SnapGrid::None)
                                      ? AestraUI::MusicTheory::getSnapName(m_snapSetting)
                                      : std::string("Off");
        renderer.drawText(value, {r.x + 40.0f, renderer.calculateTextY(r, fontSize)}, fontSize,
                          text.withAlpha(m_snapHovered ? 1.0f : 0.85f));
        // Caret drawn, not typed: the UI font has no down-triangle glyph.
        const float cx = r.x + 40.0f + renderer.measureText(value, fontSize).width + 7.0f;
        const float cy = std::round(r.y + r.height * 0.5f);
        for (int i = 0; i < 3; ++i) {
            renderer.fillRect({cx - 2.0f + static_cast<float>(i), cy - 1.0f + static_cast<float>(i),
                               5.0f - 2.0f * static_cast<float>(i), 1.0f},
                              text.withAlpha(0.6f));
        }
    }

    // The ruler row's left corner (the old toolbar home) names the track count, as board 7 does.
    {
        size_t tracks = 0;
        for (const auto& row : m_trackUIComponents) {
            if (row && row->isPrimaryForLane() && !row->isAutomationRow()) ++tracks;
        }
        const AestraUI::NUIRect corner(bounds.x + 10.0f, bounds.y + kTimelineRulerTop, 160.0f, kTimelineRulerHeight);
        std::string count = std::to_string(tracks) + (tracks == 1 ? " TRACK" : " TRACKS");
        if (m_foldedEmptyTracks > 0)
            count += " \xc2\xb7 " + std::to_string(m_foldedEmptyTracks) + " EMPTY";
        renderer.drawText(count, {corner.x, renderer.calculateTextY(corner, smallSize)}, smallSize,
                          text.withAlpha(0.4f));
    }

    // Follow playhead: the existing icon toggle, filled while on.
    if (m_followPlayhead || m_followPlayheadHovered) {
        renderer.fillRoundedRect(m_followPlayheadBounds, 4.0f,
                                 m_followPlayhead ? theme.getColor("selection")
                                                  : theme.getColor("buttonBgHover").withAlpha(0.9f));
    }
    if (m_followPlayheadIcon) {
        const float iconSz = theme.getLayoutDimension("standardIconSize");
        m_followPlayheadIcon->setBounds({std::round(m_followPlayheadBounds.x + (m_followPlayheadBounds.width - iconSz) * 0.5f),
                                         std::round(m_followPlayheadBounds.y + (m_followPlayheadBounds.height - iconSz) * 0.5f),
                                         iconSz, iconSz});
        m_followPlayheadIcon->setColor(text.withAlpha(m_followPlayhead ? 0.9f : (m_followPlayheadHovered ? 0.7f : 0.4f)));
        m_followPlayheadIcon->onRender(renderer);
    }

    // Right side: the timeline menu (loop presets, send to Audition), Fit song, + Track.
    {
        const bool menuActive = m_activeContextMenu != nullptr && !m_snapHovered;
        if (menuActive || m_menuHovered) {
            renderer.fillRoundedRect(m_menuIconBounds, 4.0f, theme.getColor("buttonBgHover").withAlpha(0.9f));
        }
        if (m_menuIcon) {
            const float iconSz = theme.getLayoutDimension("standardIconSize");
            m_menuIcon->setBounds({std::round(m_menuIconBounds.x + (m_menuIconBounds.width - iconSz) * 0.5f),
                                   std::round(m_menuIconBounds.y + (m_menuIconBounds.height - iconSz) * 0.5f), iconSz,
                                   iconSz});
            m_menuIcon->setColor(text.withAlpha(m_menuHovered ? 0.85f : 0.55f));
            m_menuIcon->onRender(renderer);
        }
    }
    outlined(m_fitSongBounds, m_fitSongHovered);
    renderer.drawTextCentered("Fit song", m_fitSongBounds, fontSize, text.withAlpha(m_fitSongHovered ? 1.0f : 0.8f));
    const bool addHovered = m_addTrackBtn && m_addTrackBtn->isHovered();
    outlined(m_addTrackBounds, addHovered);
    renderer.drawTextCentered("+ Track", m_addTrackBounds, fontSize, text.withAlpha(addHovered ? 1.0f : 0.85f));
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
    if (m_muteToolBounds.contains(position)) {
        setCurrentTool(PlaylistTool::Mute);
        return true;
    }
    if (m_snapBounds.contains(position)) {
        showSnapMenuAt(AestraUI::NUIPoint(m_snapBounds.x, m_snapBounds.bottom()));
        return true;
    }
    if (m_fitSongBounds.contains(position)) {
        fitSong();
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
        // Render horizontal resize cursor — the canonical white ResizeEW glyph,
        // drawn untinted so it matches every other resize cursor. Hover and an
        // active trim share it; the clip edge itself shows the trim in progress.
        (void)isTrimming;
        if (m_trimCursorIcon) {
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
        // Open hand while hovering a draggable surface, closed hand while the
        // drag is live: the cursor should say what the pointer is doing.
        const bool dragging = m_isDraggingPlayhead || m_isDraggingLoopStart || m_isDraggingLoopEnd ||
                              m_isDraggingRulerSelection ||
                              (m_timelineMinimap && m_timelineMinimap->isViewportDragActive());
        const auto& handIcon = (dragging && m_grabbingCursorIcon) ? m_grabbingCursorIcon : m_grabCursorIcon;
        if (handIcon) {
            handIcon->setBounds(AestraUI::NUIRect(position.x - 9, position.y - 9, 18, 18));
            handIcon->onRender(renderer);
        }
        return;
    }

    // Only render if tool requires custom cursor
    if (!toolOwnsCursor(m_currentTool, canPaintClip())) {
        // No tool cursor needed - Main.cpp renders default arrow
        return;
    }

    // Calculate grid bounds
    AestraUI::NUIRect bounds = getBounds();
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    float controlAreaWidth = layout.trackControlsWidth;
    float gridStartX = bounds.x + controlAreaWidth + kTimelineGridInsetX;
    float trackAreaTop = bounds.y + kTimelineTimeBandHeight;

    AestraUI::NUIRect gridBounds(gridStartX, trackAreaTop, bounds.width - controlAreaWidth - 20.0f,
                                 bounds.height - kTimelineTimeBandHeight);

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

        // A hairline: the rest of the app draws its guides one pixel wide, and a 2 px dashed line read as heavy beside them.
        // Centred on a pixel so it stays crisp instead of smearing across two.
        float lineX = std::floor(snappedX) + 0.5f;
        float lineY = position.y;
        AestraUI::NUIColor splitColor(255, 107, 107, 170);
        float dashLength = 5.0f;
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
                        renderer.drawLine(AestraUI::NUIPoint(lineX, y), AestraUI::NUIPoint(lineX, dashEnd), 1.0f,
                                          splitColor);
                        y = dashEnd + gapLength;
                    }
                    break;
                }
            }
        }

        // The blade follows the pointer exactly; only the dashed line is snapped, because that line is where the cut will
        // land. (The glyph used to sit ON the snapped line, so the cursor itself jumped from grid line to grid line.)
        drawToolCursorGlyph(renderer, m_splitToolIcon, AestraUI::NUIRect(position.x - 10, position.y - 10, 20, 20));
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
            drawToolCursorGlyph(renderer, m_moveCursorIcon, AestraUI::NUIRect(position.x - 10, position.y - 10, 20, 20));
        } else {
            // Render PAINT cursor
            // The pencil's tip is the bottom-left of its glyph box: put that on the pointer.
            drawToolCursorGlyph(renderer, m_paintToolIcon, AestraUI::NUIRect(position.x, position.y - 20, 20, 20));
        }
        return;
    }

    // === MUTE TOOL LOGIC ===
    // The speaker-with-a-cross, centred on the pointer: the click lands on the clip under it.
    drawToolCursorGlyph(renderer, m_muteToolIcon, AestraUI::NUIRect(position.x - 10, position.y - 10, 20, 20));
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
    if (toolOwnsCursor(m_currentTool, canPaintClip())) {
        AestraUI::NUIRect bounds = getBounds();
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        const float controlAreaWidth = layout.trackControlsWidth;
        const float gridStartX = bounds.x + controlAreaWidth + kTimelineGridInsetX;
        const float trackAreaTop = bounds.y + kTimelineTimeBandHeight;

        AestraUI::NUIRect gridBounds(gridStartX, trackAreaTop, bounds.width - controlAreaWidth - 20.0f,
                                     bounds.height - kTimelineTimeBandHeight);
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
