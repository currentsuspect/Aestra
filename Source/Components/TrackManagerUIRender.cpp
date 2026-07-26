// © 2025 Aestra Studios All Rights Reserved. Licensed for personal & educational use only.
// TrackManagerUI — rendering: main render entry, ruler, loop markers, playhead, caches.
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
// SECTION: Main Render Entry
// =============================================================================

void TrackManagerUI::onRender(AestraUI::NUIRenderer& renderer) {
    AESTRA_ZONE("TrackMgrUI_Render");
    rmt_ScopedCPUSample(TrackMgrUI_Render, 0);

    // Lazy-init track UIs on first render for instant startup
    if (m_needsTrackRefresh) {
        m_needsTrackRefresh = false;
        refreshTracks();
    }

    // Skip rendering if not visible (e.g., when user is on Mixer tab)
    if (!isVisible())
        return;

    const bool anyPlaylistLaneSoloed =
        m_trackManager && m_trackManager->getPlaylistModel().hasAudibleSoloLane();
    for (const auto& trackUI : m_trackUIComponents) {
        if (trackUI) {
            trackUI->setAnyPlaylistLaneSoloed(anyPlaylistLaneSoloed);
        }
    }

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
    AestraUI::NUISize cacheSize(static_cast<int>(bounds.width), static_cast<int>(bounds.height));
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
        const float headerHeight = kTimelineHeaderHeight;
        const float horizontalScrollbarHeight = kTimelineHorizontalScrollbarHeight;
        const float rulerHeight = kTimelineRulerHeight;
        const float scrollbarWidth = kTimelineScrollbarWidth;

        // Since panels are overlays, we render the playlist underneath them.
        // If we want clipping to stop at panel borders, we'd need to subtract them here.
        // For v3.1 simplicity, we just fill the workspace and let overlays cover it.

        const float viewportTop = bounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;
        const float viewportHeight =
            std::max(0.0f, bounds.height - headerHeight - horizontalScrollbarHeight - rulerHeight);
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
    if (m_timelineMinimap && m_timelineMinimap->isVisible())
        m_timelineMinimap->onRender(renderer);
    if (m_scrollbar && m_scrollbar->isVisible())
        m_scrollbar->onRender(renderer);

    // Panels are now handled by OverlayLayer rendering.

    // Render toolbar OUTSIDE cache (interactive tool selection)
    renderToolbar(renderer);

    // Render tool cursor (Split, Paint, AND trim resize cursor)
    // renderToolCursor handles: trim edges, split tool, paint tool
    // Skip custom tool/minimap cursor rendering during hidden-cursor drag
    if (!m_window || m_window->getCursorStyle() != AestraUI::NUICursorStyle::Hidden) {
        // CURSOR PIPELINE BYPASS: renderToolCursor draws directly on renderer at
        // m_lastMousePos. Outside both SVG cursor system and SDL cursor system.
        // Suppressed here rather than through cursor abstraction — intentional.
        // See renderMinimapResizeCursor below for identical pattern.
        renderToolCursor(renderer, m_lastMousePos);

        // CURSOR PIPELINE BYPASS: renderMinimapResizeCursor draws directly on
        // renderer at m_lastMousePos. Outside both SVG cursor system and SDL
        // cursor system. Suppressed here rather than through cursor abstraction.
        // See renderToolCursor above for identical pattern.
        renderMinimapResizeCursor(renderer, m_lastMousePos);
    }

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

        float headerHeight = kTimelineHeaderHeight;
        float rulerHeight = kTimelineRulerHeight;
        float horizontalScrollbarHeight = kTimelineHorizontalScrollbarHeight;
        float controlAreaWidth = layout.trackControlsWidth;
        float scrollbarWidth = kTimelineScrollbarWidth;

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
    AestraUI::NUIColor bgColor = themeManager.getColor("backgroundPrimary");
    const AestraUI::NUIColor gridBgColor = themeManager.getColor("timelineBed"); // pure black on dark (owner direction), recessed on light

    if (m_playlistVisible) {
        // Background for control area (always visible)
        AestraUI::NUIRect controlBg(bounds.x, bounds.y, controlAreaWidth, bounds.height);
        renderer.fillRect(controlBg, themeManager.getColor("recessedPanel"));

        // Background for grid area (match track background; zebra grid provides contrast)
        float scrollbarWidth = kTimelineScrollbarWidth;
        float gridWidth = bounds.width - controlAreaWidth - scrollbarWidth - 5;
        AestraUI::NUIRect gridBg(bounds.x + gridStartX, bounds.y, gridWidth, bounds.height);
        renderer.fillRect(gridBg, gridBgColor);
        // No depth bands: the grid is uniform pure black.

        // Draw border
        AestraUI::NUIColor borderColor = themeManager.getColor("border");
        renderer.strokeRect(bounds, 1, borderColor.withAlpha(0.42f));
    }

    // Header/Track Count (Static)
    float headerAvailableWidth = bounds.width;
    if (m_playlistVisible) {
        std::string infoText =
            "Tracks: " + std::to_string(m_trackManager ? m_trackManager->getTrackCount() -
                                                             (m_trackManager->getTrackCount() > 0 ? 1 : 0)
                                                       : 0); // Exclude preview track
        const auto& themeProps = themeManager.getCurrentTheme();
        const float infoFont = themeProps.fontSizeXS;
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

        const float headerHeight = kTimelineHeaderHeight;
        const AestraUI::NUIRect headerBounds(bounds.x, bounds.y, headerAvailableWidth, headerHeight);
        const float rightPad = layout.panelMargin + 18.0f;
        const float textX = std::max(headerBounds.x + margin, headerBounds.right() - infoSize.width - rightPad);
        const float textY = std::round(renderer.calculateTextY(headerBounds, infoFont));

        renderer.drawText(infoText, AestraUI::NUIPoint(textX, textY), infoFont, themeManager.getColor("textPrimary"));
    }

    // Render Static Track Content (with Viewport Culling AND Clipping)
    constexpr float kHeaderHeight = kTimelineHeaderHeight;
    constexpr float kHScrollbarHeight = kTimelineHorizontalScrollbarHeight;
    constexpr float kRulerHeight = kTimelineRulerHeight;
    constexpr float kScrollbarWidth = kTimelineScrollbarWidth;
    const float headerHeight = kHeaderHeight;
    const float horizontalScrollbarHeight = kHScrollbarHeight;
    const float rulerHeight = kRulerHeight;
    const float scrollbarWidth = kScrollbarWidth;

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
        if (!track || !track->isVisible())
            continue;

        // Culling: Skip tracks outside the visible viewport
        const auto trackBounds = track->getBounds();
        if (trackBounds.bottom() < viewportTop || trackBounds.y > viewportBottom)
            continue;

        // Uniform row base (owner direction: no row zebra; the bar zebra
        // inside drawPlaylistGrid provides the only alternation). Pure black
        // on dark themes, recessed light bed on light themes.
        renderer.fillRect(trackBounds, themeManager.getColor("timelineBed"));

        track->renderStatic(renderer);

        // Light separator strip filling the lane gap across the grid area
        // (owner direction: the black gaps read as holes — lift them to a
        // shade of white so rows stay legible). This is the only row
        // separator; TrackUIComponent draws none. Kept very faint so track rows
        // read as quietly separated lanes, not a hard spreadsheet grid — the
        // musical content (clips) carries the visual weight, not the chrome.
        const AestraUI::NUIRect gapRect(bounds.x + gridStartX, trackBounds.bottom(),
                                        std::max(0.0f, trackWidth - gridStartX),
                                        static_cast<float>(m_trackSpacing));
        renderer.fillRect(gapRect, AestraUI::NUIThemeManager::getInstance().getCurrentTheme().textPrimary.withAlpha(0.06f));
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

        float headerHeight = kTimelineHeaderHeight;
        AestraUI::NUIRect headerRect(bounds.x, bounds.y, headerWidth, headerHeight);
        // Elevated header: base fill + soft vertical gradient + glass top edge.
        // Rendered into the playlist FBO cache, so the richness is free per frame.
        renderer.fillRect(headerRect, themeManager.getColor("recessedPanel"));
        // Shade-only elevation — any white component reads as a sheen on this
        // near-black chrome (owner direction: no light gradients anywhere).
        renderer.fillRectGradient(headerRect, AestraUI::NUIColor(0.0f, 0.0f, 0.0f, 0.0f),
                                  AestraUI::NUIColor(0.0f, 0.0f, 0.0f, 0.075f),
                                  /*vertical=*/true);
        // Soft drop below the header so it reads as a raised surface.
        const float shadowH = 7.0f;
        renderer.fillRectGradient(AestraUI::NUIRect(headerRect.x, headerRect.bottom(), headerRect.width, shadowH),
                                  AestraUI::NUIColor(0.0f, 0.0f, 0.0f, 0.22f),
                                  AestraUI::NUIColor(0.0f, 0.0f, 0.0f, 0.0f),
                                  /*vertical=*/true);
        const auto headerBorder = borderColor.withAlpha(0.50f);
        renderer.drawLine({headerRect.x, headerRect.y}, {headerRect.x, headerRect.bottom()}, 1.0f, headerBorder);
        renderer.drawLine({headerRect.right(), headerRect.y}, {headerRect.right(), headerRect.bottom()}, 1.0f,
                          headerBorder);
        renderer.drawLine({headerRect.x, headerRect.bottom()}, {headerRect.right(), headerRect.bottom()}, 1.0f,
                          headerBorder);

        // Draw time ruler below header
        float rulerHeight = kTimelineRulerHeight;
        float horizontalScrollbarHeight = kTimelineHorizontalScrollbarHeight;
        AestraUI::NUIRect rulerRect(bounds.x, bounds.y + headerHeight + horizontalScrollbarHeight, headerWidth,
                                    rulerHeight);
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
        constexpr float kHeaderHeight = kTimelineHeaderHeight;
        constexpr float kHScrollbarHeight = kTimelineHorizontalScrollbarHeight;
        constexpr float kRulerHeight = kTimelineRulerHeight;
        constexpr float kScrollbarWidth = kTimelineScrollbarWidth;
        const float headerHeight = kHeaderHeight;
        const float horizontalScrollbarHeight = kHScrollbarHeight;
        const float rulerHeight = kRulerHeight;
        const float scrollbarWidth = kScrollbarWidth;

        const float viewportTop = bounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;
        const float viewportHeight =
            std::max(0.0f, bounds.height - headerHeight - horizontalScrollbarHeight - rulerHeight);
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
            if (!trackUI || !trackUI->isVisible() || !trackUI->isPrimaryForLane())
                continue;

            // Culling
            const auto trackBounds = trackUI->getBounds();
            if (trackBounds.bottom() < viewportTop || trackBounds.y > viewportBottom)
                continue;

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

        float headerHeight = kTimelineHeaderHeight;
        float rulerHeight = kTimelineRulerHeight;
        float horizontalScrollbarHeight = kTimelineHorizontalScrollbarHeight;
        float trackAreaTop = bounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;
        float trackAreaHeight = bounds.height - (headerHeight + horizontalScrollbarHeight + rulerHeight);

        float scrollbarWidth = kTimelineScrollbarWidth;
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
                    renderer.drawLine(AestraUI::NUIPoint(selStartX, trackAreaTop),
                                      AestraUI::NUIPoint(selStartX, trackAreaTop + trackAreaHeight), 1.0f,
                                      accentColor.withAlpha(0.30f));
                }
                if (selEndX >= gridStartX && selEndX <= gridEndX) {
                    renderer.drawLine(AestraUI::NUIPoint(selEndX, trackAreaTop),
                                      AestraUI::NUIPoint(selEndX, trackAreaTop + trackAreaHeight), 1.0f,
                                      accentColor.withAlpha(0.30f));
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

    const float headerHeight = kTimelineHeaderHeight;
    const float rulerHeight = kTimelineRulerHeight;
    const float horizontalScrollbarHeight = kTimelineHorizontalScrollbarHeight;
    const float scrollbarWidth = kTimelineScrollbarWidth;

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
        if (!child->isVisible())
            continue;

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
            if (!m_playlistVisible)
                continue;
            const auto trackBounds = child->getBounds();
            if (trackBounds.bottom() < viewportTopAbs || trackBounds.y > viewportBottomAbs)
                continue;
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
            if (task)
                task();
        }
    }

    // Plugin insert/remove requests go through the PlaybackGraphController.
    // TrackManagerUI does NOT consume graph dirty state here - the controller drains in AestraApp::run().
    // UI code should only request rebuilds via requestAudioGraphRebuild().
    if (m_trackManager && m_trackManager->hasPendingGraphRebuild()) {
        graphDirty.emit();
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

    if (m_loopPreset == 6 && m_trackManager) {
        double projectEndBeat = m_trackManager->getPlaylistModel().getTotalDurationBeats();
        if (projectEndBeat <= 0.001) {
            const double emptyProjectBeats = static_cast<double>(std::max(1, m_beatsPerBar)) * 16.0;
            projectEndBeat = emptyProjectBeats;
        }

        if (std::abs(projectEndBeat - m_lastProjectLoopExtentBeats) > 1e-3) {
            m_lastProjectLoopExtentBeats = projectEndBeat;
            setLoopRegion(0.0, projectEndBeat, true);
            if (m_onLoopRegionUpdate) {
                m_onLoopRegionUpdate(0.0, projectEndBeat);
            }
        }
    } else {
        m_lastProjectLoopExtentBeats = -1.0;
    }

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
        m_timelineScrollOffset =
            safeClampFloat(newScrollOffset, 0.0f, static_cast<float>(maxStartBeat * m_pixelsPerBeat));

        // Sync to all tracks
        for (auto& trackUI : m_trackUIComponents) {
            trackUI->setPixelsPerBeat(m_pixelsPerBeat);
            trackUI->setTimelineScrollOffset(m_timelineScrollOffset);
        }

        invalidateCache(); // Full cache invalidation for smooth zoom animation
    }

    if (std::abs(m_targetScrollOffset - m_scrollOffset) > 0.25f) {
        const float lerpSpeed = 14.0f;
        const float t = std::min(1.0f, static_cast<float>(deltaTime * lerpSpeed));
        m_scrollOffset += (m_targetScrollOffset - m_scrollOffset) * t;
        layoutTracks();
        invalidateCache();
    } else if (m_scrollOffset != m_targetScrollOffset) {
        m_scrollOffset = m_targetScrollOffset;
        layoutTracks();
        invalidateCache();
    }

    // === Follow Playhead Logic (Page & Continuous) ===
    if (m_followPlayhead && m_trackManager && m_trackManager->isPlaying()) {
        if (!AestraUI::NUIDragDropManager::getInstance().isDragging() && !m_isDraggingPlayhead &&
            !m_isDraggingRulerSelection && !m_isDraggingLoopStart && !m_isDraggingLoopEnd) {
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
            for (auto& trackUI : m_trackUIComponents)
                trackUI->setLoading(false);

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
            for (auto& trackUI : m_trackUIComponents)
                trackUI->setLoading(false);
            lastHadImports = false;
        }
    }

    // === EDGE-SCROLLING DURING TIMELINE DRAG OPERATIONS ===
    // Keep the timeline moving while dragging clips, selections, loop markers, or the playhead near edges.
    const bool needsEdgeScroll = m_isDraggingClipInstant || m_isDrawingSelectionBox || m_isDraggingRulerSelection ||
                                 m_isDraggingLoopStart || m_isDraggingLoopEnd || m_isDraggingPlayhead;

    if (needsEdgeScroll && m_pixelsPerBeat > 0) {
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        float controlAreaWidth = layout.trackControlsWidth;
        float scrollbarWidth = kTimelineScrollbarWidth;

        AestraUI::NUIRect bounds = getBounds();
        float gridStartX = bounds.x + controlAreaWidth + 5.0f;
        float gridEndX = bounds.x + bounds.width - scrollbarWidth;
        float gridWidth = gridEndX - gridStartX;

        // Edge zone size (pixels from edge where scrolling activates)
        const float edgeZone = 110.0f;
        // Allow a little overshoot beyond the grid so edge-scroll keeps breathing while dragging.
        const float edgeOvershoot = 36.0f;
        // Scroll speed is pixel-based so the feel stays consistent across zoom levels.
        const float minScrollPxPerSecond = 220.0f;
        const float maxScrollPxPerSecond = 1500.0f;

        float mouseX = m_lastMousePos.x;
        float scrollDelta = 0.0f;

        // Check left edge
        if (mouseX < gridStartX + edgeZone && mouseX >= gridStartX - edgeOvershoot) {
            float proximity = 1.0f - ((mouseX - gridStartX + edgeOvershoot) / (edgeZone + edgeOvershoot));
            proximity = std::clamp(proximity, 0.0f, 1.0f);
            const float response = proximity * proximity * (3.0f - 2.0f * proximity); // smoothstep
            const float pxPerSecond = minScrollPxPerSecond + (maxScrollPxPerSecond - minScrollPxPerSecond) * response;
            scrollDelta = -(pxPerSecond / std::max(1.0f, m_pixelsPerBeat)) * static_cast<float>(deltaTime);
        }
        // Check right edge
        else if (mouseX > gridEndX - edgeZone && mouseX <= gridEndX + edgeOvershoot) {
            float proximity = 1.0f - ((gridEndX + edgeOvershoot - mouseX) / (edgeZone + edgeOvershoot));
            proximity = std::clamp(proximity, 0.0f, 1.0f);
            const float response = proximity * proximity * (3.0f - 2.0f * proximity); // smoothstep
            const float pxPerSecond = minScrollPxPerSecond + (maxScrollPxPerSecond - minScrollPxPerSecond) * response;
            scrollDelta = (pxPerSecond / std::max(1.0f, m_pixelsPerBeat)) * static_cast<float>(deltaTime);
        }

        if (std::abs(scrollDelta) > 0.001f) {
            double currentStartBeat = m_timelineScrollOffset / m_pixelsPerBeat;
            if (scrollDelta < 0.0f && currentStartBeat <= 0.0001) {
                scrollDelta = 0.0f;
            }
        }

        if (std::abs(scrollDelta) > 0.001f) {
            double currentStartBeat = m_timelineScrollOffset / m_pixelsPerBeat;
            double newStartBeat = currentStartBeat + scrollDelta;

            // Clamp to valid range (can't scroll before beat 0)
            newStartBeat = std::max(0.0, newStartBeat);

            // Apply the scroll
            setTimelineViewStartBeat(newStartBeat, false);

            // Keep the active gesture aligned with the newly scrolled view.
            if (m_isDraggingClipInstant) {
                updateInstantClipDrag(m_lastMousePos);
            } else if (m_isDraggingRulerSelection) {
                const float localMouseX = m_lastMousePos.x - gridStartX + m_timelineScrollOffset;
                double positionInBeats = std::max(0.0, snapBeatToGrid(localMouseX / m_pixelsPerBeat));
                m_rulerSelectionEndBeat = positionInBeats;
                if (std::abs(m_rulerSelectionEndBeat - m_rulerSelectionStartBeat) > 0.001) {
                    m_hasRulerSelection = true;
                }
                invalidateCache();
            } else if (m_isDraggingLoopStart || m_isDraggingLoopEnd) {
                const float localMouseX = m_lastMousePos.x - gridStartX + m_timelineScrollOffset;
                double positionInBeats = std::max(0.0, snapBeatToGrid(localMouseX / m_pixelsPerBeat));
                if (m_isDraggingLoopStart) {
                    if (positionInBeats < m_loopEndBeat) {
                        setLoopRegion(positionInBeats, m_loopEndBeat, true);
                    }
                } else if (positionInBeats > m_loopStartBeat) {
                    setLoopRegion(m_loopStartBeat, positionInBeats, true);
                }
                m_minimapSelectionBeatRange = {m_loopStartBeat, m_loopEndBeat};
                invalidateCache();
            } else if (m_isDraggingPlayhead && m_trackManager) {
                auto& playlist = m_trackManager->getPlaylistModel();
                const float localMouseX = m_lastMousePos.x - gridStartX + m_timelineScrollOffset;
                const double positionInBeats = localMouseX / m_pixelsPerBeat;
                const double positionInSeconds = std::max(0.0, playlist.beatToSeconds(positionInBeats));
                m_trackManager->setPosition(positionInSeconds);
                m_trackManager->setPlayStartPosition(positionInSeconds);
            } else if (m_isDrawingSelectionBox) {
                m_selectionBoxEnd.x = safeClampFloat(m_lastMousePos.x, gridStartX, gridEndX);
                invalidateCache();
            }
        }
    }

    updateTimelineMinimap(deltaTime);
}

void TrackManagerUI::onResize(int width, int height) {
    // Update cached dimensions before layout/cache update
    m_backgroundCachedWidth = width;
    m_backgroundCachedHeight = height;
    invalidateCache(); // Full invalidation on resize for immediate repaint

    layoutTracks();
    if (m_timelineMinimap) {
        m_timelineMinimap->setLeadingInset(
            AestraUI::NUIThemeManager::getInstance().getLayoutDimensions().trackControlsWidth);
    }
    // Zebra Striping: Assign row index to tracks
    for (size_t i = 0; i < m_trackUIComponents.size(); ++i) {
        if (m_trackUIComponents[i]) {
            m_trackUIComponents[i]->setRowIndex(static_cast<int>(i));
        }
    }
    AestraUI::NUIComponent::onResize(width, height);
}
void TrackManagerUI::renderTimeRuler(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& rulerBounds) {
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    auto borderColor = themeManager.getColor("borderSubtle");
    auto accentColor = themeManager.getColor("accentPrimary");

    // Opaque near-black ruler material — identical across the full row and the
    // grid shell so the corner over the track controls is flush by construction.
    auto glassBg = themeManager.getColor("recessedPanel");
    auto glassHighlight = themeManager.getColor("textPrimary").withAlpha(0.014f);

    auto textCol = themeManager.getColor("textPrimary").withAlpha(0.86f);
    auto majorTickCol = themeManager.getColor("gridMajor");
    auto minorTickCol = themeManager.getColor("gridMinor");

    // Restore layout definition
    const auto& layout = themeManager.getLayoutDimensions();

    // Calculate grid bounds FIRST (before drawing)
    float controlAreaWidth = layout.trackControlsWidth;
    float gridStartX = rulerBounds.x + controlAreaWidth + 5.0f;

    float scrollbarWidth = kTimelineScrollbarWidth;
    float trackWidth = rulerBounds.width - scrollbarWidth;
    float gridWidth = std::max(0.0f, trackWidth - controlAreaWidth - 10.0f);

    AestraUI::NUIRect gridRulerRect(gridStartX, rulerBounds.y, gridWidth, rulerBounds.height);

    // One material across the whole ruler row: fill the full row (including the
    // corner over the track controls) with the ruler base so the left edge is
    // flush with the rest of the ruler instead of showing the layer beneath.
    renderer.fillRect(rulerBounds, glassBg);

    float cornerRadius = 3.0f;
    renderer.fillRoundedRect(gridRulerRect, cornerRadius, glassBg);

    // Subtle top highlight on grid area only
    AestraUI::NUIRect highlightRect(gridRulerRect.x, gridRulerRect.y, gridRulerRect.width, 1.0f);
    renderer.fillRect(highlightRect, glassHighlight);

    renderer.strokeRoundedRect(gridRulerRect, cornerRadius, 1.0f, borderColor);
    renderer.drawLine(AestraUI::NUIPoint(gridRulerRect.x, gridRulerRect.bottom() - 1.0f),
                      AestraUI::NUIPoint(gridRulerRect.right(), gridRulerRect.bottom() - 1.0f), 1.0f,
                      AestraUI::NUIColor::white().withAlpha(0.075f));

    // === SET CLIP RECT for timeline grid area (prevents text/ticks bleeding outside) ===
    renderer.setClipRect(gridRulerRect);

    // Grid spacing - DYNAMIC based on zoom level
    int beatsPerBar = m_beatsPerBar;
    float pixelsPerBar = m_pixelsPerBeat * beatsPerBar;

    // === ADAPTIVE DENSITY: keep contiguous 1,2,3... bars unless labels would overlap ===
    int barStride = 1;
    const float minLabelSpacingPx = 28.0f;
    while ((pixelsPerBar * static_cast<float>(barStride)) < minLabelSpacingPx && barStride < 128) {
        barStride *= 2;
    }

    // Calculate which bar to start drawing from based on scroll offset
    int startBar = static_cast<int>(m_timelineScrollOffset / pixelsPerBar);
    // Align startBar to stride
    startBar = (startBar / barStride) * barStride;

    // Calculate end bar based on visible width (no max extent bounds)
    int visibleBars = static_cast<int>(std::ceil((m_timelineScrollOffset + gridWidth) / pixelsPerBar)) - startBar;
    int endBar = startBar + visibleBars + barStride; // Draw all visible bars

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
        float fontSize = isMajorBar ? 11.0f : 10.0f;

        auto textSize = renderer.measureText(barText, fontSize);

        // Place text vertically centered in ruler area (top-left Y positioning)
        float textY = std::round(renderer.calculateTextY(rulerBounds, fontSize));

        // Position text to the RIGHT of the grid line with small offset
        float textX = x + 4.0f;

        // Draw text - clip rect handles edge clipping automatically
        renderer.drawText(barText, AestraUI::NUIPoint(textX, textY), fontSize,
                          isMajorBar ? textCol : textCol.withAlpha(0.76f));

        // Bar tick line - major bars get full height, others half
        // Mature Style: Ticks bottom-up
        float tickHeight = isMajorBar ? rulerBounds.height * 0.58f : rulerBounds.height * 0.24f;
        renderer.drawLine(AestraUI::NUIPoint(x, rulerBounds.y + rulerBounds.height - tickHeight),
                          AestraUI::NUIPoint(x, rulerBounds.y + rulerBounds.height), isMajorBar ? 1.15f : 1.0f,
                          isMajorBar ? majorTickCol : minorTickCol);

        // Beat ticks within the bar (only if zoomed in enough AND not striding)
        // DOWNBEATS (1, 2, 3, 4) are BRIGHTER and TALLER for visibility
        if (m_pixelsPerBeat >= 10.0f && barStride == 1) {
            for (int beat = 1; beat < beatsPerBar; ++beat) {
                float beatX = x + (beat * m_pixelsPerBeat);

                float beatTickHeight = rulerBounds.height * 0.22f;
                AestraUI::NUIColor beatTickColor = minorTickCol;

                renderer.drawLine(AestraUI::NUIPoint(beatX, rulerBounds.y + rulerBounds.height - beatTickHeight),
                                  AestraUI::NUIPoint(beatX, rulerBounds.y + rulerBounds.height), 1.0f, beatTickColor);
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
            renderer.drawLine(AestraUI::NUIPoint(selStartX, rulerBounds.y),
                              AestraUI::NUIPoint(selStartX, rulerBounds.bottom()), 1.0f, accentColor.withAlpha(0.6f));
            renderer.drawLine(AestraUI::NUIPoint(selEndX, rulerBounds.y),
                              AestraUI::NUIPoint(selEndX, rulerBounds.bottom()), 1.0f, accentColor.withAlpha(0.6f));
        }
    }

    // === CLEAR CLIP RECT before drawing control area ===
    renderer.clearClipRect();

    // 2. Draw SOLID background for CONTROL AREA (left side - DRAWN LAST to fully cover any bleed)
    //    Use backgroundPrimary to match minimap's left section exactly
    auto controlBg = themeManager.getColor("backgroundSecondary");
    AestraUI::NUIRect controlRect(rulerBounds.x, rulerBounds.y, controlAreaWidth + 5.0f, rulerBounds.height);
    renderer.fillRect(controlRect, controlBg);
    renderer.drawLine(AestraUI::NUIPoint(controlRect.x, controlRect.bottom() - 1.0f),
                      AestraUI::NUIPoint(controlRect.right(), controlRect.bottom() - 1.0f), 1.0f,
                      AestraUI::NUIColor::white().withAlpha(0.060f));

    // Dedicated "corner" panel where track controls meet the ruler.
    const AestraUI::NUIRect cornerRect(rulerBounds.x, rulerBounds.y, controlAreaWidth, rulerBounds.height);
    renderer.drawLine(AestraUI::NUIPoint(cornerRect.right(), cornerRect.y),
                      AestraUI::NUIPoint(cornerRect.right(), cornerRect.bottom()), 1.0f, borderColor.withAlpha(0.82f));
}
// Render loop markers on ruler
void TrackManagerUI::renderLoopMarkers(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& rulerBounds) {
    if (!m_loopEnabled)
        return;

    if (m_loopEndBeat <= m_loopStartBeat)
        return; // Invalid loop region

    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();

    // Calculate grid start (same as ruler)
    float controlAreaWidth = layout.trackControlsWidth;
    float gridStartX = rulerBounds.x + controlAreaWidth + 5.0f;
    float scrollbarWidth = kTimelineScrollbarWidth;
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

    if (!startVisible && !endVisible)
        return; // Both markers off-screen

    // Color based on enabled state and hover
    auto accentColor = themeManager.getColor("accentPrimary");
    AestraUI::NUIColor markerColor;

    if (m_loopEnabled) {
        markerColor = accentColor.withAlpha(0.8f); // Bright when active
    } else {
        markerColor = accentColor.withAlpha(0.3f); // Dimmed when inactive
    }

    // Marker dimensions
    const float triangleWidth = 12.0f;
    const float triangleHeight = 10.0f;

    // === RENDER LOOP START MARKER ===
    if (startVisible) {
        AestraUI::NUIColor startColor = markerColor;
        if (m_hoveringLoopStart || m_isDraggingLoopStart) {
            startColor = accentColor; // Full brightness on hover/drag
        }

        // Draw triangle pointing down (using lines)
        AestraUI::NUIPoint p1(loopStartX, rulerBounds.y + triangleHeight);    // Bottom center
        AestraUI::NUIPoint p2(loopStartX - triangleWidth / 2, rulerBounds.y); // Top left
        AestraUI::NUIPoint p3(loopStartX + triangleWidth / 2, rulerBounds.y); // Top right

        // Draw filled triangle using lines
        renderer.drawLine(p1, p2, 2.0f, startColor);
        renderer.drawLine(p2, p3, 2.0f, startColor);
        renderer.drawLine(p3, p1, 2.0f, startColor);

        // Draw vertical line from triangle to bottom
        renderer.drawLine(AestraUI::NUIPoint(loopStartX, rulerBounds.y + triangleHeight),
                          AestraUI::NUIPoint(loopStartX, rulerBounds.y + rulerBounds.height), 2.0f, startColor);
    }

    // === RENDER LOOP END MARKER ===
    if (endVisible) {
        AestraUI::NUIColor endColor = markerColor;
        if (m_hoveringLoopEnd || m_isDraggingLoopEnd) {
            endColor = accentColor; // Full brightness on hover/drag
        }

        // Draw triangle pointing down (using lines)
        AestraUI::NUIPoint p1(loopEndX, rulerBounds.y + triangleHeight);    // Bottom center
        AestraUI::NUIPoint p2(loopEndX - triangleWidth / 2, rulerBounds.y); // Top left
        AestraUI::NUIPoint p3(loopEndX + triangleWidth / 2, rulerBounds.y); // Top right

        // Draw filled triangle using lines
        renderer.drawLine(p1, p2, 2.0f, endColor);
        renderer.drawLine(p2, p3, 2.0f, endColor);
        renderer.drawLine(p3, p1, 2.0f, endColor);

        // Draw vertical line from triangle to bottom
        renderer.drawLine(AestraUI::NUIPoint(loopEndX, rulerBounds.y + triangleHeight),
                          AestraUI::NUIPoint(loopEndX, rulerBounds.y + rulerBounds.height), 2.0f, endColor);
    }
}
// Draw playhead (vertical line showing current playback position)
// Draw playhead (vertical line showing current playback position)
void TrackManagerUI::renderPlayhead(AestraUI::NUIRenderer& renderer) {
    // If in Pattern Mode (Arsenal), hide the global timeline playhead to avoid confusion ("Time Segmentation")
    if (m_patternMode)
        return;

    if (!m_trackManager)
        return;

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
    float scrollbarWidth = kTimelineScrollbarWidth;
    float trackWidth = bounds.width - scrollbarWidth;
    float gridWidth = trackWidth - (controlAreaWidth + 5.0f);
    float gridEndX = gridStartX + gridWidth;
    float triangleSize = 6.0f; // Marker extends this much left/right from playhead center

    // Calculate playhead boundaries
    float headerHeight = kTimelineHeaderHeight;
    float horizontalScrollbarHeight = kTimelineHorizontalScrollbarHeight;
    float rulerHeight = kTimelineRulerHeight;
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
        auto& themeManager = AestraUI::NUIThemeManager::getInstance(); // ensure themeManager is available (it was
                                                                       // declared above but check scope)
        // Redefine/Reuse themeManager? It's declared at line 2731 in the function scope (based on previous view).
        // Let's use the one from outside if available, or just get it.
        // The visible snippet starts at 2761, check context.
        // In the previous view_code_item, text was "auto& themeManager = AestraUI::NUIThemeManager::getInstance();" at
        // line 2731.
        AestraUI::NUIColor playheadColor = themeManager.getColor("accentPrimary");

        // GLOW EFFECT (BG) - Only when playing
        if (m_trackManager->isPlaying()) {
            // Manual Gradient Glow for smoother look
            float glowWidth = 4.0f; // Width of glow on each side
            float lineH = playheadEndY - playheadStartY;

            AestraUI::NUIColor glowColorCenter = playheadColor.withAlpha(0.14f); // Soft center
            AestraUI::NUIColor glowColorEdge = playheadColor.withAlpha(0.0f);    // Transparent edge

            // Left side glow (Transparent -> Color)
            renderer.fillRectGradient(AestraUI::NUIRect(playheadX - glowWidth, playheadStartY, glowWidth, lineH),
                                      glowColorEdge, glowColorCenter, false // false = horizontal
            );

            // Right side glow (Color -> Transparent)
            renderer.fillRectGradient(AestraUI::NUIRect(playheadX, playheadStartY, glowWidth, lineH), glowColorCenter,
                                      glowColorEdge, false // false = horizontal
            );
        }

        // Draw playhead line (thin, faint, pixel-aligned)
        renderer.drawLine(AestraUI::NUIPoint(playheadX, playheadStartY), AestraUI::NUIPoint(playheadX, playheadEndY),
                          1.0f, playheadColor.withAlpha(0.55f));

        // Draw ruler-locked circular marker. It sits just above the grid start so the
        // vertical line reads as anchored to the ruler rather than floating.
        const float markerRadius = 6.0f;
        const AestraUI::NUIPoint markerCenter(playheadX, playheadStartY - 1.0f);
        renderer.fillCircle(markerCenter, markerRadius + 2.0f,
                            themeManager.getColor("backgroundPrimary").withAlpha(0.92f));
        renderer.fillCircle(markerCenter, markerRadius, playheadColor.withAlpha(0.20f));
        renderer.strokeCircle(markerCenter, markerRadius, 1.3f, playheadColor.withAlpha(0.98f));
        renderer.fillCircle(markerCenter, 1.7f, playheadColor);
    }
}

// ⚡ MULTI-LAYER CACHING IMPLEMENTATION

void TrackManagerUI::updateBackgroundCache(AestraUI::NUIRenderer& renderer) {
    rmt_ScopedCPUSample(TrackMgr_UpdateBgCache, 0);

    int width = m_backgroundCachedWidth;
    int height = m_backgroundCachedHeight;

    if (width <= 0 || height <= 0)
        return;

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
    float scrollbarWidth = kTimelineScrollbarWidth;
    float gridWidth = width - controlAreaWidth - scrollbarWidth - 5;

    AestraUI::NUIRect textureBounds(0, 0, static_cast<float>(width), static_cast<float>(height));
    AestraUI::NUIColor bgColor = themeManager.getColor("backgroundPrimary");
    const AestraUI::NUIColor gridBgColor = themeManager.getColor("workspaceBackground");
    AestraUI::NUIColor borderColor = themeManager.getColor("border");

    // Draw background panels
    AestraUI::NUIRect controlBg(0, 0, controlAreaWidth, static_cast<float>(height));
    renderer.fillRect(controlBg, bgColor);

    AestraUI::NUIRect gridBg(gridStartX, 0, gridWidth, static_cast<float>(height));
    renderer.fillRect(gridBg, gridBgColor);

    // Draw borders (no top edge so transport + toolbar remain a continuous band)
    renderer.drawLine({textureBounds.x, textureBounds.y}, {textureBounds.x, textureBounds.bottom()}, 1.0f, borderColor);
    renderer.drawLine({textureBounds.right(), textureBounds.y}, {textureBounds.right(), textureBounds.bottom()}, 1.0f,
                      borderColor);
    renderer.drawLine({textureBounds.x, textureBounds.bottom()}, {textureBounds.right(), textureBounds.bottom()}, 1.0f,
                      borderColor);

    // Draw header bar
    float headerHeight = kTimelineHeaderHeight;
    AestraUI::NUIRect headerRect(0, 0, static_cast<float>(width), headerHeight);
    renderer.fillRect(headerRect, bgColor);
    renderer.drawLine({headerRect.x, headerRect.y}, {headerRect.x, headerRect.bottom()}, 1.0f, borderColor);
    renderer.drawLine({headerRect.right(), headerRect.y}, {headerRect.right(), headerRect.bottom()}, 1.0f, borderColor);
    renderer.drawLine({headerRect.x, headerRect.bottom()}, {headerRect.right(), headerRect.bottom()}, 1.0f,
                      borderColor);

    // Draw time ruler
    float rulerHeight = kTimelineRulerHeight;
    float horizontalScrollbarHeight = kTimelineHorizontalScrollbarHeight;
    AestraUI::NUIRect rulerRect(0, headerHeight + horizontalScrollbarHeight, static_cast<float>(width), rulerHeight);

    // Render ruler ticks (static part only - no moving elements)
    double bpm = std::max(m_trackManager->getPlaylistModel().getBPM(), 1.0);
    double secondsPerBeat = 60.0 / bpm;
    double maxExtent = getMaxTimelineExtent();
    double maxExtentInBeats = maxExtent / secondsPerBeat;

    // Ruler Render: "Mature" Playlist Style
    auto bg = themeManager.getColor("recessedPanel");
    auto textCol = themeManager.getColor("textPrimary");
    auto tickCol = themeManager.getColor("gridMajor");

    // Draw ruler background
    renderer.fillRect(rulerRect, bg);
    renderer.strokeRect(rulerRect, 1, borderColor);

    // Bar numbers (cached in background texture)
    float barFontSize = 12.0f;
    for (int bar = 0; bar <= static_cast<int>(maxExtentInBeats / m_beatsPerBar) + 4; ++bar) {
        float x = rulerRect.x + gridStartX + (bar * m_beatsPerBar * m_pixelsPerBeat) - m_timelineScrollOffset;
        if (x < rulerRect.x + gridStartX - 2.0f || x > rulerRect.right() + m_pixelsPerBeat)
            continue;

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
    m_controlsNeedsUpdate = false;
}

void TrackManagerUI::updateTrackCache(AestraUI::NUIRenderer& renderer, size_t trackIndex) {
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

void TrackManagerUI::buildAllWaveformCaches() {
    if (!m_trackManager) return;
    auto& srcMgr = m_trackManager->getSourceManager();
    auto ids = srcMgr.getAllSourceIDs();
    std::weak_ptr<AestraUI::NUIComponent> weakSelf = weak_from_this();
    int queued = 0;
    for (auto srcId : ids) {
        auto* src = srcMgr.getSource(srcId);
        if (src && src->isReady() && !src->getWaveformCache()) {
            const uint64_t sourceRevision = src->getContentRevision();
            ++queued;
            m_waveformBuilder.buildAsync(
                *src, [weakSelf, srcId, sourceRevision](std::shared_ptr<Aestra::Audio::WaveformCache> cache) {
                    if (!cache) return;
                    auto self = std::dynamic_pointer_cast<TrackManagerUI>(weakSelf.lock());
                    if (!self) return;

                    std::lock_guard<std::mutex> lock(self->m_pendingTasksMutex);
                    self->m_pendingTasks.push_back([weakSelf, srcId, sourceRevision, cache]() {
                        auto self = std::dynamic_pointer_cast<TrackManagerUI>(weakSelf.lock());
                        if (!self) return;
                        if (!self->m_trackManager) return;

                        auto* src = self->m_trackManager->getSourceManager().getSource(srcId);
                        if (!src) return;
                        if (src->getContentRevision() != sourceRevision) return;

                        src->setWaveformCache(cache);
                        self->invalidateCache();
                        self->m_backgroundNeedsUpdate = true;
                        self->setDirty(true);
                    });
                });
        }
    }
    if (queued > 0) {
        Log::info("[TrackManagerUI] Queued " + std::to_string(queued) + " waveform cache builds (async)");
    }
}

} // namespace Audio
} // namespace Aestra

void Aestra::Audio::TrackManagerUI::renderPendingImports(AestraUI::NUIRenderer& renderer) {
    if (m_pendingImports.empty() || !m_playlistVisible)
        return;

    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();

    float headerHeight = kTimelineHeaderHeight;
    float rulerHeight = kTimelineRulerHeight;
    float horizontalScrollbarHeight = kTimelineHorizontalScrollbarHeight;
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
        for (size_t i = 0; i < m_trackUIComponents.size(); ++i) {
            if (m_trackUIComponents[i] && m_trackUIComponents[i]->getLaneId() == item.laneId) {
                yPos = m_trackUIComponents[i]->getBounds().y;
                break;
            }
        }

        // Skip if track not found (maybe scrolled out or invalid)
        if (yPos < 0)
            continue;

        // Calculate X position
        float xPos = gridStartX + (static_cast<float>(item.startBeat) * m_pixelsPerBeat) - m_timelineScrollOffset;
        float width = static_cast<float>(item.estimatedDurationBeats) * m_pixelsPerBeat;
        float height = static_cast<float>(m_trackHeight);

        AestraUI::NUIRect rect(xPos, yPos, width, height);

        // Clipping check (basic)
        if (rect.right() < gridStartX || rect.x > getBounds().right())
            continue;
        if (rect.bottom() < trackAreaTop || rect.y > getBounds().bottom())
            continue;

        // ANIMATION: Pulsing Border
        float pulse = (std::sin(item.animationTime * 10.0f) * 0.5f + 0.5f); // 0.0 to 1.0
        float alphaMult = 0.5f + (pulse * 0.5f);                            // 0.5 to 1.0

        renderer.fillRect(rect, holoFill);

        // PROGRESS BAR: Functional fill
        if (item.progress > 0.001f) {
            AestraUI::NUIRect progressRect = rect;
            progressRect.width *= std::min(1.0f, item.progress);
            renderer.fillRect(progressRect, cyan.withAlpha(0.4f));

            // Highlight edge of progress
            float edgeX = progressRect.right();
            renderer.drawLine(AestraUI::NUIPoint(edgeX, rect.y), AestraUI::NUIPoint(edgeX, rect.bottom()), 1.5f,
                              cyan.withAlpha(0.8f));
        }

        renderer.strokeRect(rect, 1.5f, holoBorder.withAlpha(alphaMult));

        // ANIMATION: Scanning Bar (Subtle during progress)
        float scanAlpha = item.progress > 0.99f ? 0.0f : 0.4f * (1.0f - item.progress * 0.5f);
        if (scanAlpha > 0.01f) {
            float scanProgress = std::fmod(item.animationTime * 1.5f, 1.0f); // 0.0 to 1.0 loops
            float scanX = rect.x + (rect.width * scanProgress);
            AestraUI::NUIColor scanColor = cyan.withAlpha(scanAlpha * (1.0f - std::abs(scanProgress - 0.5f)));
            renderer.drawLine(AestraUI::NUIPoint(scanX, rect.y), AestraUI::NUIPoint(scanX, rect.bottom()), 2.0f,
                              scanColor);
        }

        // Text
        std::string progressStr = " (" + std::to_string((int)(item.progress * 100)) + "%)";
        std::string text = "ANALYZING: " + item.displayName + (item.progress > 0 ? progressStr : "");
        float fontSize = AestraUI::NUIThemeManager::getInstance().getFontSize("s");
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
            renderer.fillRect(textBg, AestraUI::NUIColor(0, 0, 0, 0.6f));

            renderer.drawText(text, AestraUI::NUIPoint(textX, textY), fontSize, cyan);
        }
    }
}
