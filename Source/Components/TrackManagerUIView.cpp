// © 2025 Aestra Studios All Rights Reserved. Licensed for personal & educational use only.
// TrackManagerUI — timeline view state: scroll, zoom, minimap, loop region, extent.
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

void TrackManagerUI::updateScrollbar() {
    if (!m_scrollbar)
        return;

    AestraUI::NUIRect bounds = getBounds();

    // In v3.1, panels are floating overlays and do not affect the scrollbar's viewport directly.
    float viewportHeight = bounds.height - kTimelineTimeBandHeight;

    const float laneCount = static_cast<float>(m_trackUIComponents.size());
    float totalContentHeight = laneCount * (m_trackHeight + m_trackSpacing);

    // Set scrollbar range
    m_scrollbar->setRangeLimit(0, totalContentHeight);
    m_scrollbar->setCurrentRange(m_scrollOffset, viewportHeight);
    m_scrollbar->setAutoHide(totalContentHeight <= viewportHeight);
}

void TrackManagerUI::onScroll(double position) {
    m_scrollOffset = static_cast<float>(position);
    m_targetScrollOffset = m_scrollOffset;
    layoutTracks();
    invalidateCache();
}

void TrackManagerUI::scheduleTimelineMinimapRebuild() {
    m_minimapNeedsRebuild = true;
}

float TrackManagerUI::getTimelineGridWidthPixels() const {
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();

    const float controlAreaWidth = layout.trackControlsWidth;
    // Grid width is derived from the component bounds, not the minimap surface —
    // the minimap is a cropped overview and no longer defines the plane width.
    const float trackWidth = getBounds().width;
    float gridWidth = trackWidth - kTimelineScrollbarWidth - controlAreaWidth - 10.0f; // Match TrackUIComponent grid width
    return std::max(0.0f, gridWidth);
}

double TrackManagerUI::secondsToBeats(double seconds) const {
    return m_trackManager->getPlaylistModel().secondsToBeats(seconds);
}

void TrackManagerUI::setTimelineViewStartBeat(double viewStartBeat, bool isFinal) {
    const float gridWidthPx = getTimelineGridWidthPixels();
    if (!(m_pixelsPerBeat > 0.0f) || gridWidthPx <= 0.0f)
        return;

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
                                                       double edgeBeat, bool isFinal) {
    const float gridWidthPx = getTimelineGridWidthPixels();
    if (gridWidthPx <= 0.0f)
        return;

    constexpr float kMinPixelsPerBeat = 1.0f; // Allow extreme zoom-out for long clips
    constexpr float kMaxPixelsPerBeat = kMaxTimelinePixelsPerBeat;

    const double domainStart = m_minimapDomainStartBeat;
    const double domainEnd = std::max(m_minimapDomainEndBeat, domainStart + 1.0);

    const double minWidthBeats = static_cast<double>(gridWidthPx / kMaxPixelsPerBeat);
    // Max zoom-out is limited by domain size (can't view beyond domain via minimap resize)
    const double maxWidthBeats = std::min(static_cast<double>(gridWidthPx / kMinPixelsPerBeat),
                                          domainEnd - domainStart // Can't zoom out beyond current domain
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
        const float newPixelsPerBeat =
            safeClampFloat(static_cast<float>(gridWidthPx / desiredWidth), kMinPixelsPerBeat, kMaxPixelsPerBeat);
        applyZoom(newPixelsPerBeat);

        const double viewWidthBeats = static_cast<double>(gridWidthPx / m_pixelsPerBeat);
        const double viewStartBeat = anchorBeat - viewWidthBeats;
        setTimelineViewStartBeat(viewStartBeat, isFinal);
    } else {
        // Dragging the right edge: keep left edge anchored.
        // Clamp to domain - minimap resize does NOT expand domain (only edge-scroll during clip drag does)
        const double clampedEdge = std::min(domainEnd, std::max(edgeBeat, anchorBeat + std::max(1e-6, minWidthBeats)));
        const double desiredWidth = std::max(minWidthBeats, std::min(maxWidthBeats, clampedEdge - anchorBeat));
        const float newPixelsPerBeat =
            safeClampFloat(static_cast<float>(gridWidthPx / desiredWidth), kMinPixelsPerBeat, kMaxPixelsPerBeat);
        applyZoom(newPixelsPerBeat);

        setTimelineViewStartBeat(anchorBeat, isFinal);
    }

    invalidateCache(); // Always invalidate on zoom/resize changes
    updateTimelineMinimap(0.0);
}

void TrackManagerUI::centerTimelineViewAtBeat(double centerBeat) {
    const float gridWidthPx = getTimelineGridWidthPixels();
    if (!(m_pixelsPerBeat > 0.0f) || gridWidthPx <= 0.0f)
        return;

    const double viewWidthBeats = static_cast<double>(gridWidthPx / m_pixelsPerBeat);
    const double start = centerBeat - (viewWidthBeats * 0.5);
    setTimelineViewStartBeat(start, true);
}

void TrackManagerUI::zoomTimelineAroundBeat(double anchorBeat, float zoomMultiplier) {
    const float gridWidthPx = getTimelineGridWidthPixels();
    if (gridWidthPx <= 0.0f)
        return;

    // Calculate min pixels/beat based on domain (can't zoom out beyond domain)
    const double domainWidth = std::max(1.0, m_minimapDomainEndBeat - m_minimapDomainStartBeat);
    float minPPB = std::max(1.0f, static_cast<float>(gridWidthPx / domainWidth));

    // Minimap zoom must feel immediate; keep the smooth-zoom system in sync by updating both.
    const float newPixelsPerBeat = safeClampFloat(m_pixelsPerBeat * zoomMultiplier, minPPB, kMaxTimelinePixelsPerBeat);
    m_pixelsPerBeat = newPixelsPerBeat;
    m_targetPixelsPerBeat = newPixelsPerBeat;

    for (auto& trackUI : m_trackUIComponents) {
        trackUI->setPixelsPerBeat(m_pixelsPerBeat);
    }

    const double viewWidthBeats = static_cast<double>(gridWidthPx / m_pixelsPerBeat);
    const double viewStartBeat = anchorBeat - (viewWidthBeats * 0.5);
    setTimelineViewStartBeat(viewStartBeat, true);
    invalidateCache(); // Always invalidate on zoom changes
    updateTimelineMinimap(0.0);
}

void TrackManagerUI::updateTimelineMinimap(double deltaTime) {
    if (!m_timelineMinimap)
        return;
    if (!m_playlistVisible)
        return;
    if (!m_trackManager)
        return;

    const float gridWidthPx = getTimelineGridWidthPixels();
    if (!(m_pixelsPerBeat > 0.0f) || gridWidthPx <= 0.0f)
        return;

    // Not const: a shrinking domain re-clamps the scroll offset further down, and the
    // model published at the end of this function must describe the viewport we ended
    // up with, not the one we started with.
    double viewStartBeat = static_cast<double>(m_timelineScrollOffset / m_pixelsPerBeat);
    const double viewWidthBeats = static_cast<double>(gridWidthPx / m_pixelsPerBeat);
    double viewEndBeat = viewStartBeat + viewWidthBeats;

    const double playheadBeat = secondsToBeats(m_trackManager->getUIPosition());

    auto& playlist = m_trackManager->getPlaylistModel();
    double clipEndBeat = playlist.getTotalDurationBeats();

    // === SMART DOMAIN CALCULATION ===
    const double beatsPerBarD = static_cast<double>(std::max(1, m_beatsPerBar));

    // Has content?
    bool hasContent = (clipEndBeat > 0.001);

    double requiredEndBeat;
    const bool allowViewDrivenDomainExpand = m_isDraggingClipInstant;
    if (!hasContent) {
        // EMPTY PROJECT: Fixed 16 bars - can't zoom out beyond this
        const double emptyFixedBars = 16.0;
        const double emptyMinBeats = beatsPerBarD * emptyFixedBars;

        // Only an active clip drag may extend the domain beyond the empty-project floor.
        if (allowViewDrivenDomainExpand && viewEndBeat > emptyMinBeats) {
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
            paddingBars = 8.0; // Short clip: fixed 8 bar padding
        } else if (clipBars < 64.0) {
            paddingBars = clipBars * 0.25; // Medium: 25% of clip length
        } else {
            paddingBars = clipBars * 0.125; // Long: 12.5%, no cap - infinite if PC handles it
        }

        const double padBeats = beatsPerBarD * paddingBars;
        const double playheadBuffer = beatsPerBarD * 4.0; // 4 bars ahead of playhead

        // Normal scrolling/zooming should not mutate the domain; only a real clip edge-drag may do that.
        requiredEndBeat = std::max(clipEndBeat + padBeats, playheadBeat + playheadBuffer);
        if (allowViewDrivenDomainExpand) {
            requiredEndBeat = std::max(requiredEndBeat, viewEndBeat + beatsPerBarD);
        }

        // No hard ceiling - let it grow as big as needed (infinite if PC can handle it)
    }

    // Update domain instantly (no cooldown needed)
    bool domainShrank = false;
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
        domainShrank = true;
    }
    // else: domain unchanged, nothing to do

    // A shrinking domain can leave the viewport scrolled past the new end — delete the
    // last clip in a long arrangement and the view stays out where that clip used to
    // be, showing empty grid with a minimap thumb pinned off its own track. Growing is
    // safe (the old view is still inside the larger domain), so only the shrink needs
    // this. setTimelineViewStartBeat applies exactly the clamp this needs and is safe
    // to call here: it only re-enters updateTimelineMinimap when isFinal is false.
    if (domainShrank) {
        setTimelineViewStartBeat(viewStartBeat, /*isFinal=*/true);

        // Re-read what the clamp actually settled on. Zoom is untouched, so the width
        // is unchanged and only the start moves. Without this the minimap model below
        // would publish the pre-clamp viewport — the thumb would stay parked outside
        // the domain for a frame, which is the exact symptom this block exists to fix.
        viewStartBeat = static_cast<double>(m_timelineScrollOffset / m_pixelsPerBeat);
        viewEndBeat = viewStartBeat + viewWidthBeats;
    }

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

                    if (!(span.endBeat > span.startBeat))
                        continue;

                    spans.push_back(span);
                }
            }
        }

        m_timelineSummaryCache.requestRebuild(std::move(spans), m_minimapDomainStartBeat, m_minimapDomainEndBeat);
        m_minimapNeedsRebuild = false;
    }

    m_timelineSummarySnapshot = m_timelineSummaryCache.getSnapshot();

    if (m_marquee.active()) {
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        const float controlAreaWidth = layout.trackControlsWidth;
        const float gridStartXAbs = getBounds().x + controlAreaWidth + kTimelineGridInsetX;

        const float minX = m_marquee.rectMinX();
        const float maxX = m_marquee.rectMaxX();

        const double startBeat =
            (static_cast<double>((minX - gridStartXAbs) + m_timelineScrollOffset)) / m_pixelsPerBeat;
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

    invalidateCache(); // ⚡ Mark cache dirty
}

void TrackManagerUI::deselectAllTracks() {
    clearSelection();
}
// Set loop region (called from Main.cpp when loop preset changes)
void TrackManagerUI::setLoopRegion(double startBeat, double endBeat, bool enabled) {
    m_loopStartBeat = startBeat;
    m_loopEndBeat = endBeat;
    m_loopEnabled = enabled;
    for (auto& trackUI : m_trackUIComponents) {
        if (!trackUI) {
            continue;
        }
        trackUI->setLoopEnabled(enabled);
        trackUI->setLoopRegion(startBeat, endBeat);
    }
    invalidateCache(); // Redraw to show updated markers
}

void TrackManagerUI::updateSelectionLoopRegion(double startBeat, double endBeat) {
    setLoopRegion(startBeat, endBeat, true);
    m_rulerSelectionStartBeat = startBeat;
    m_rulerSelectionEndBeat = endBeat;
    m_hasRulerSelection = endBeat > startBeat;
    m_minimapSelectionBeatRange = {startBeat, endBeat};
    if (m_onLoopRegionUpdate) {
        m_onLoopRegionUpdate(startBeat, endBeat);
    }
}
// Calculate maximum timeline extent needed based on all samples
// Calculate maximum timeline extent needed based on all clips
double TrackManagerUI::getMaxTimelineExtent() const {
    if (!m_trackManager)
        return 0.0;

    auto& playlist = m_trackManager->getPlaylistModel();
    double totalDurationBeats = playlist.getTotalDurationBeats();

    double bpm = std::max(playlist.getBPM(), 1.0);
    double secondsPerBeat = 60.0 / bpm;

    // Minimum extent - at least 8 bars even if empty
    double minExtent = 8.0 * m_beatsPerBar * secondsPerBeat;

    // Convert beats to seconds for extent
    double totalDurationSeconds = totalDurationBeats * secondsPerBeat;

    // Add 2 bars padding
    double paddedEnd = totalDurationSeconds + (2.0 * m_beatsPerBar * secondsPerBeat);

    return std::max(paddedEnd, minExtent);
}

} // namespace Audio
} // namespace Aestra
