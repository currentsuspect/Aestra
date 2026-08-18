// © 2025 Aestra Studios All Rights Reserved. Licensed for personal & educational use only.
#include "TrackManagerUI.h"

#include "../AestraCore/include/AestraLog.h"
#include "../AestraCore/include/AestraUnifiedProfiler.h"
#include "../AestraUI/Core/NUIDragDrop.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../AestraUI/Platform/NUIPlatformBridge.h"
#include "AudioFileValidator.h"
#include "ClipSource.h"
#include "Commands/AddClipCommand.h"
#include "Commands/CreateLaneCommand.h"
#include "Commands/DeleteLaneCommand.h"
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
// SECTION: Construction & Destruction
// =============================================================================

TrackManagerUI::TrackManagerUI(std::shared_ptr<TrackManager> trackManager)
    : m_trackManager(trackManager), m_cacheId(reinterpret_cast<uint64_t>(this)), m_cacheInvalidated(true),
      m_isRenderingToCache(false) {
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
    {
        auto& theme = AestraUI::NUIThemeManager::getInstance();
        m_scrollbar->setArrowSize(0.0f);
        m_scrollbar->setBorderWidth(0.0f);
        m_scrollbar->setBorderRadius(8.0f);
        m_scrollbar->setTrackColor(theme.getColor("surfaceRaised").withAlpha(0.55f));
        m_scrollbar->setThumbColor(theme.getColor("textPrimary").withAlpha(0.30f));
        m_scrollbar->setThumbHoverColor(theme.getColor("textPrimary").withAlpha(0.48f));
        m_scrollbar->setThumbPressedColor(theme.getColor("accentPrimary").withAlpha(0.68f));
        m_scrollbar->setMinimumThumbSize(0.06);
    }
    m_scrollbar->setOnScroll([this](double position) { onScroll(position); });
    addChild(m_scrollbar);

    // Timeline minimap (replaces top horizontal scrollbar).
    m_timelineMinimap = std::make_shared<AestraUI::TimelineMinimapBar>();
    m_timelineMinimap->onRequestCenterView = [this](double centerBeat) { centerTimelineViewAtBeat(centerBeat); };
    m_timelineMinimap->onRequestSetViewStart = [this](double viewStartBeat, bool isFinal) {
        setTimelineViewStartBeat(viewStartBeat, isFinal);
    };
    m_timelineMinimap->onRequestResizeViewEdge = [this](AestraUI::TimelineMinimapResizeEdge edge, double anchorBeat,
                                                        double edgeBeat, bool isFinal) {
        resizeTimelineViewEdgeFromMinimap(edge, anchorBeat, edgeBeat, isFinal);
    };
    m_timelineMinimap->onRequestZoomAround = [this](double anchorBeat, float zoomMultiplier) {
        zoomTimelineAroundBeat(anchorBeat, zoomMultiplier);
    };
    m_timelineMinimap->onModeChanged = [this](AestraUI::TimelineMinimapMode mode) {
        m_minimapMode = mode;
        setDirty(true);
    };
    m_timelineMinimap->setShowModeToggles(false);
    m_timelineMinimap->setLeadingInset(
        AestraUI::NUIThemeManager::getInstance().getLayoutDimensions().trackControlsWidth);
    addChild(m_timelineMinimap);

    // Defer track UI creation to first render for instant startup.
    // refreshTracks() will be called lazily in onRender() via m_needsTrackRefresh.

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
void TrackManagerUI::addTrack(const std::string& name) {
    if (!m_trackManager)
        return;

    // Playlist lanes are created independently from mixer inserts.
    auto laneCmd = std::make_shared<CreateLaneCommand>(m_trackManager->getPlaylistModel(), name);
    m_trackManager->getCommandHistory().pushAndExecute(laneCmd);

    // FD-14: every lane belongs to a Track. The app's add-track creates the
    // lane AND its owning Track in one operation (ownership by stable id).
    if (laneCmd->getLaneId().isValid()) {
        m_trackManager->createTrack(laneCmd->getLaneId(), name);
    }

    // Rebuild UI from model state
    refreshTracks();
    layoutTracks();
    scheduleTimelineMinimapRebuild();
    invalidateCache();
    Log::info("Added Playlist lane via command: " + name);
}

void TrackManagerUI::refreshTracks() {
    m_needsTrackRefresh = false; // Clear lazy-init flag if we got here directly

    if (!m_trackManager) {
        Log::error("refreshTracks: m_trackManager is null!");
        return;
    }

    Log::info("refreshTracks: starting, laneCount=" +
              std::to_string(m_trackManager->getPlaylistModel().getLaneCount()));

    // v3.0 logic: iterate over PlaylistModel lanes instead of Mixer channels
    auto& playlist = m_trackManager->getPlaylistModel();
    size_t laneCount = playlist.getLaneCount();
    Log::info("refreshTracks: looping over " + std::to_string(laneCount) + " lanes");

    // Stable lane IDs own selection. Raw pointers only describe the current
    // widget generation, so discard that view before destroying any rows.
    m_selectedTracks.clear();
    for (auto& trackUI : m_trackUIComponents) {
        removeChild(trackUI);
    }
    m_trackUIComponents.clear();

    // FD-14 §10/§11: render lanes grouped by owning Track — the primary lane
    // row first, then (when expanded) the track's owned lanes nested directly
    // under it. Collapsed tracks show only the primary row; unowned lanes
    // stay in playlist order.
    std::vector<PlaylistLaneID> orderedLaneIds;
    orderedLaneIds.reserve(laneCount);
    std::unordered_set<uint64_t> seenTracks;
    for (size_t i = 0; i < laneCount; ++i) {
        const auto laneId = playlist.getLaneId(i);
        const auto* lane = playlist.getLane(laneId);
        if (!lane) {
            continue;
        }
        const auto* track = lane->trackId != 0 ? m_trackManager->getTrack(lane->trackId) : nullptr;
        if (!track) {
            orderedLaneIds.push_back(laneId);
            continue;
        }
        if (track->laneIds.empty() || laneId != track->laneIds.front()) {
            continue; // Owned lane: rendered nested under its track's primary row.
        }
        if (seenTracks.count(track->trackId)) {
            continue;
        }
        seenTracks.insert(track->trackId);
        orderedLaneIds.push_back(laneId);
        if (!isTrackCollapsed(track->trackId)) {
            for (const auto& owned : track->laneIds) {
                if (owned != laneId) {
                    orderedLaneIds.push_back(owned);
                }
            }
        }
    }

    for (size_t i = 0; i < orderedLaneIds.size(); ++i) {
        auto laneId = orderedLaneIds[i];
        auto lane = playlist.getLane(laneId);
        if (!lane) {
            Log::warning("refreshTracks: lane " + std::to_string(i) + " is null!");
            continue;
        }

        // Playlist lanes are arrangement-only; mixer inserts are managed in
        // the mixer and sources keep their own stable destinations.
        auto trackUI = std::make_shared<TrackUIComponent>(laneId, nullptr, m_trackManager.get());

        // FD-14 §10 nesting: mark owned non-primary rows as nested (no record
        // arm, indented "Lane N" label) and mirror the track's collapse state
        // for the chevron glyph. The expansion toggle lives on the primary
        // row of a multi-lane track.
        const auto* owningTrack = lane->trackId != 0 ? m_trackManager->getTrack(lane->trackId) : nullptr;
        const bool isPrimaryRow = owningTrack && !owningTrack->laneIds.empty() && owningTrack->laneIds.front() == laneId;
        trackUI->setIsNestedLane(owningTrack != nullptr && !isPrimaryRow);
        trackUI->setTrackCollapsed(isTrackCollapsed(lane->trackId));
        if (owningTrack && isPrimaryRow && owningTrack->laneIds.size() > 1) {
            trackUI->setOnExpandToggled(
                [this, trackId = owningTrack->trackId]() { this->toggleTrackCollapsed(trackId); });
        }
        trackUI->updateUI();

        // Register callbacks
        trackUI->setOnSoloToggled([this](TrackUIComponent* soloedTrack) { this->onTrackSoloToggled(soloedTrack); });

        trackUI->setOnCacheInvalidationNeeded([this]() { this->invalidateCache(); });

        trackUI->setOnClipDeleted(
            [this](TrackUIComponent* trackComp, ClipInstanceID clipId, AestraUI::NUIPoint ripplePos) {
                this->onClipDeleted(trackComp, clipId, ripplePos);
            });

        trackUI->setIsSplitToolActive([this]() { return this->m_currentTool == PlaylistTool::Split; });

        trackUI->setOnSplitRequested(
            [this](TrackUIComponent* trackComp, double splitTime) { this->onSplitRequested(trackComp, splitTime); });

        trackUI->setOnClipSelected([this](TrackUIComponent*, ClipInstanceID clipId) {
            selectClip(clipId);
            if (clipId.isValid()) {
                Log::info("TrackManagerUI: Clip selected " + clipId.toString());
                // Auto-Picking: Selecting a clip automatically loads it into the clipboard/brush
                copySelectedClip();
            }
        });

        trackUI->setOnPatternClipOpenRequested([this](PatternID patternId) {
            if (m_onOpenPatternInPianoRoll) {
                m_onOpenPatternInPianoRoll(patternId);
            }
        });
        trackUI->setOnAudioClipOpenRequested([this](ClipInstanceID clipId) {
            if (m_onOpenAudioClipEditor) {
                m_onOpenAudioClipEditor(clipId);
            }
        });
        trackUI->setOnPatternClipDragStarted([this](PatternID patternId) {
            if (m_onPreviewPatternClip && patternId.isValid()) {
                m_onPreviewPatternClip(patternId);
                m_dragPatternPreviewActive = true;
            }
        });

        trackUI->setOnTrackSelected([this](TrackUIComponent* trackComp, TrackSelectionIntent intent) {
            this->selectTrack(trackComp, intent);
        });

        trackUI->setOnSendToAudition([this, i, lane]() {
            if (this->m_onSendToAudition) {
                // Lanes and mixer channels are separate domains (#761 review);
                // resolve through the shared helper so the row button and the
                // toolbar menu audition the same channel.
                const uint32_t channelIndex = this->resolveLaneToChannelIndex(lane, static_cast<uint32_t>(i));
                this->m_onSendToAudition(channelIndex, lane->name);
            }
        });

        // Sync zoom/scroll settings
        trackUI->setPixelsPerBeat(m_pixelsPerBeat);
        trackUI->setBeatsPerBar(m_beatsPerBar);
        trackUI->setTimelineScrollOffset(m_timelineScrollOffset);
        trackUI->setSnapSetting(m_snapSetting); // Sync snap setting for resize

        // Pass platform bridge for cursor capture (volume knob)
        trackUI->setPlatformBridge(m_window);

        m_trackUIComponents.push_back(trackUI);
        addChild(trackUI);
    } // Close lane loop

    std::vector<PlaylistLaneID> validLaneIds;
    validLaneIds.reserve(m_trackUIComponents.size());
    for (const auto& trackUI : m_trackUIComponents) {
        if (trackUI) {
            validLaneIds.push_back(trackUI->getLaneId());
        }
    }
    m_trackSelection.retainOnly(validLaneIds);
    syncTrackSelectionView();

    if (m_selectedClipId.isValid() && !playlist.getClip(m_selectedClipId)) {
        m_selectedClipId = ClipInstanceID{};
    }
    selectClip(m_selectedClipId);

    layoutTracks();

    // Mixer strips are now refreshed by AestraContent when syncing state

    // Update scrollbar after tracks are refreshed (fixes initial glitch)
    scheduleTimelineMinimapRebuild();
    updateTimelineMinimap(0.0);

    invalidateCache(); // Invalidate cache when tracks refreshed

    Log::info("refreshTracks: completed, created " + std::to_string(m_trackUIComponents.size()) + " TrackUIs");
}

uint32_t TrackManagerUI::resolveLaneToChannelIndex(const Audio::PlaylistLane* lane, uint32_t fallbackIndex) const {
    // Lanes and mixer channels are separate domains (a lane can be
    // arrangement-only or its clips routed to any channel), so the lane index
    // is not a valid channel position (#761 review). Use the first clip's
    // mixer channel; fall back to the lane index when unresolved.
    if (!lane) {
        return fallbackIndex;
    }
    for (const auto& clip : lane->clips) {
        if (!clip.patternId.isValid()) {
            continue;
        }
        auto* pattern = m_trackManager->getPatternManager().getPattern(clip.patternId);
        if (!pattern) {
            continue;
        }
        const uint32_t channelId = pattern->getMixerChannelId();
        for (size_t c = 0; c < m_trackManager->getChannelCount(); ++c) {
            if (const auto* channel = m_trackManager->getChannel(c)) {
                if (channel->getChannelId() == channelId) {
                    return static_cast<uint32_t>(c);
                }
            }
        }
        break;
    }
    return fallbackIndex;
}

void TrackManagerUI::toggleTrackCollapsed(uint64_t trackId) {
    if (isTrackCollapsed(trackId)) {
        m_collapsedTrackIds.erase(trackId);
    } else {
        m_collapsedTrackIds.insert(trackId);
    }
    refreshTracks();
}

void TrackManagerUI::revealLane(PlaylistLaneID laneId) {
    // Phase-5: a committed take must be discoverable. Expand the owning track
    // so its lanes render, rebuild rows, then scroll the take lane into view.
    const auto* lane = m_trackManager ? m_trackManager->getPlaylistModel().getLane(laneId) : nullptr;
    if (lane && lane->trackId != 0) {
        expandTrack(lane->trackId);
    }
    refreshTracks();
    for (size_t i = 0; i < m_trackUIComponents.size(); ++i) {
        if (m_trackUIComponents[i] && m_trackUIComponents[i]->getLaneId() == laneId) {
            setVerticalScroll(static_cast<float>(i) * (m_trackHeight + m_trackSpacing));
            break;
        }
    }
}

void TrackManagerUI::onTrackSoloToggled(TrackUIComponent* soloedTrack) {
    if (!m_trackManager || !soloedTrack)
        return;

    // Playlist solos are additive. Refresh every lane because the aggregate
    // solo gate changes the dimmed/audible state of non-soloed lanes.
    for (auto& trackUI : m_trackUIComponents) {
        trackUI->updateUI();
        trackUI->repaint();
    }

    invalidateCache();
    Log::info("Playlist solo set changed (additive mode)");
}

void TrackManagerUI::onClipDeleted(TrackUIComponent* trackComp, ClipInstanceID clipId,
                                   const AestraUI::NUIPoint& rippleCenter) {
    if (!trackComp || !m_trackManager || !clipId.isValid())
        return;

    auto& playlist = m_trackManager->getPlaylistModel();
    const auto* clip = playlist.getClip(clipId);
    if (!clip)
        return;

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
    if (m_selectedClipId == clipId) {
        m_selectedClipId = ClipInstanceID{};
    }

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
    if (!trackComp || !m_trackManager)
        return;

    // Find which clip is at this beat position on this lane
    auto& playlist = m_trackManager->getPlaylistModel();
    PlaylistLaneID laneId = trackComp->getLaneId();
    auto lane = playlist.getLane(laneId);
    if (!lane)
        return;

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
    float scrollbarWidth = kTimelineScrollbarWidth;
    float horizontalScrollbarHeight = kTimelineHorizontalScrollbarHeight;
    float rulerHeight = kTimelineRulerHeight;

    float viewportHeight = std::max(0.0f, bounds.height - headerHeight - horizontalScrollbarHeight - rulerHeight);

    // In v3.1, panels are floating overlays and do not affect workspace viewport directly.
    // If we wanted docking, we'd subtract their space here based on external state pointers.

    // Layout timeline minimap (top, right after header, before ruler)
    if (m_timelineMinimap) {
        float minimapWidth = std::max(0.0f, bounds.width - scrollbarWidth);
        float minimapY = headerHeight;
        m_timelineMinimap->setBounds(
            AestraUI::NUIAbsolute(bounds, 0, minimapY, minimapWidth, horizontalScrollbarHeight));
        updateTimelineMinimap(0.0);
    }

    // Layout vertical scrollbar (right side, below header, horizontal scrollbar, and ruler)
    if (m_scrollbar) {
        float scrollbarY = headerHeight + horizontalScrollbarHeight + rulerHeight;
        float scrollbarX = std::max(0.0f, bounds.width - scrollbarWidth);
        m_scrollbar->setBounds(AestraUI::NUIAbsolute(bounds, scrollbarX, scrollbarY, scrollbarWidth, viewportHeight));
        updateScrollbar();
    }

    float controlAreaWidth = layout.trackControlsWidth;
    float gridStartX = bounds.x + std::max(0.0f, controlAreaWidth + kTimelineGridInsetX);
    float trackAreaTop = bounds.y + std::max(0.0f, headerHeight + horizontalScrollbarHeight + rulerHeight);

    // === V3.0 LANE LAYOUT (Two-Rect Model) ===
    for (size_t i = 0; i < m_trackUIComponents.size(); ++i) {
        auto trackUI = m_trackUIComponents[i];
        if (!trackUI)
            continue;

        float yPos = trackAreaTop + (i * (m_trackHeight + m_trackSpacing)) - m_scrollOffset;

        // Fix: Use absolute coordinates (bounds.x, yPos).
        // AestraUI components use absolute screen coordinates.
        // FD-14 §10: nested rows keep FULL-WIDTH bounds — the timeline grid
        // must stay globally aligned across lanes (a row-x indent would shift
        // clip snapping); nesting is expressed in the chrome instead.
        float trackWidth = std::max(0.0f, bounds.width - scrollbarWidth - 5.0f);
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
// =============================================================================
// MULTI-SELECTION METHODS
// =============================================================================

void TrackManagerUI::selectTrack(TrackUIComponent* track, bool addToSelection) {
    selectTrack(track, addToSelection ? TrackSelectionIntent::Add : TrackSelectionIntent::Replace);
}

void TrackManagerUI::selectTrack(TrackUIComponent* track, TrackSelectionIntent intent) {
    if (!track)
        return;

    m_trackSelection.apply(track->getLaneId(), intent);
    syncTrackSelectionView();

    const auto* lane = m_trackManager ? m_trackManager->getPlaylistModel().getLane(track->getLaneId()) : nullptr;
    const std::string trackName = lane ? lane->name : "Track";
    Log::info("[TrackManagerUI] Track selection updated: " + trackName +
              " (total selected: " + std::to_string(m_trackSelection.size()) + ")");

    invalidateCache();
}

void TrackManagerUI::deselectTrack(TrackUIComponent* track) {
    if (!track)
        return;

    if (!m_trackSelection.contains(track->getLaneId()))
        return;

    m_trackSelection.apply(track->getLaneId(), TrackSelectionIntent::Toggle);
    syncTrackSelectionView();
    invalidateCache();
}

void TrackManagerUI::clearSelection() {
    m_trackSelection.clear();
    syncTrackSelectionView();

    Log::info("[TrackManagerUI] Cleared all track selection");
    invalidateCache();
}

bool TrackManagerUI::isTrackSelected(TrackUIComponent* track) const {
    return track && m_trackSelection.contains(track->getLaneId());
}

void TrackManagerUI::selectAllTracks() {
    std::vector<PlaylistLaneID> laneIds;
    laneIds.reserve(m_trackUIComponents.size());
    for (auto& trackUI : m_trackUIComponents) {
        if (trackUI) {
            laneIds.push_back(trackUI->getLaneId());
        }
    }
    m_trackSelection.selectAll(laneIds);
    syncTrackSelectionView();

    Log::info("[TrackManagerUI] Selected all tracks (" + std::to_string(m_selectedTracks.size()) + ")");
    invalidateCache();
}

void TrackManagerUI::syncTrackSelectionView() {
    m_selectedTracks.clear();
    for (const auto& trackUI : m_trackUIComponents) {
        if (!trackUI) {
            continue;
        }
        const bool selected = m_trackSelection.contains(trackUI->getLaneId());
        trackUI->setSelected(selected);
        if (selected) {
            m_selectedTracks.insert(trackUI.get());
        }
    }
}

void TrackManagerUI::selectClip(ClipInstanceID clipId) {
    m_selectedClipId = clipId;
    for (const auto& trackUI : m_trackUIComponents) {
        if (trackUI) {
            trackUI->setSelectedClipId(clipId);
        }
    }
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

void TrackManagerUI::openTrackContextMenu(const ::AestraUI::NUIPoint& position,
                                          std::function<void()> onSendToAudition) {
    if (m_activeContextMenu) {
        detachContextMenu(m_activeContextMenu);
    }

    m_activeContextMenu = std::make_shared<AestraUI::NUIContextMenu>();
    auto menu = m_activeContextMenu;

    TrackUIComponent* selectedTrack = getSelectedTrackUI();
    auto* lane = selectedTrack && m_trackManager
                     ? m_trackManager->getPlaylistModel().getLane(selectedTrack->getLaneId())
                     : nullptr;
    if (lane && selectedTrack) {
        const PlaylistLaneID laneId = lane->id;
        menu->addCheckbox("Mute Track", lane->muted, [this, laneId](bool muted) {
            if (auto* target = m_trackManager->getPlaylistModel().getLane(laneId)) {
                target->muted = muted;
                m_trackManager->requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
                m_trackManager->markModified();
                for (const auto& trackUI : m_trackUIComponents) {
                    if (trackUI && trackUI->getLaneId() == laneId) trackUI->updateUI();
                }
                invalidateCache();
            }
        });
        menu->addCheckbox("Solo Track", lane->solo, [this, laneId](bool soloed) {
            if (auto* target = m_trackManager->getPlaylistModel().getLane(laneId)) {
                target->solo = soloed;
                m_trackManager->requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
                m_trackManager->markModified();
                for (const auto& trackUI : m_trackUIComponents) {
                    if (trackUI && trackUI->getLaneId() == laneId) {
                        onTrackSoloToggled(trackUI.get());
                        break;
                    }
                }
            }
        });
        menu->addSeparator();

        // FD-14 phase-5: take lanes accumulate with no escape hatch. Only the
        // LAST owned lane is protected — a track must keep at least one lane;
        // unowned lanes are always deletable.
        const uint64_t owningTrackId = lane->trackId;
        const auto* owningTrack = owningTrackId != 0 ? m_trackManager->getTrack(owningTrackId) : nullptr;
        const bool deletable = owningTrack == nullptr || owningTrack->laneIds.size() > 1;
        auto deleteLaneItem = std::make_shared<AestraUI::NUIContextMenuItem>("Delete Lane");
        deleteLaneItem->setEnabled(deletable);
        deleteLaneItem->setOnClick([this, laneId]() { deleteLane(laneId); });
        menu->addItem(deleteLaneItem);
    }

    menu->addItem("Send Track to Audition", [onSendToAudition]() {
        if (onSendToAudition)
            onSendToAudition();
    });
    menu->addSeparator();
    auto selectAllItem = std::make_shared<AestraUI::NUIContextMenuItem>("Select All Tracks");
    selectAllItem->setShortcut("Ctrl+A");
    selectAllItem->setOnClick([this]() { selectAllTracks(); });
    menu->addItem(selectAllItem);

    attachAndShowContextMenu(this, menu, position);
}

void TrackManagerUI::deleteLane(PlaylistLaneID laneId) {
    if (!m_trackManager) {
        return;
    }
    const auto* lane = m_trackManager->getPlaylistModel().getLane(laneId);
    if (!lane) {
        return;
    }
    const auto* track = lane->trackId != 0 ? m_trackManager->getTrack(lane->trackId) : nullptr;
    if (track && track->laneIds.size() <= 1) {
        return;
    }
    const auto executeDelete = [this, laneId]() {
        m_trackManager->getCommandHistory().pushAndExecute(std::make_shared<DeleteLaneCommand>(*m_trackManager, laneId));
        refreshTracks();
        invalidateCache();
        scheduleTimelineMinimapRebuild();
    };

    // Deleting a lane that still holds clips is destructive: ask first when a
    // dialog channel is wired. Headless callers without one proceed directly.
    if (!lane->clips.empty() && m_onConfirmDialogRequest) {
        m_onConfirmDialogRequest(
            "Delete Lane",
            "This lane still contains " + std::to_string(lane->clips.size()) +
                (lane->clips.size() == 1 ? " clip" : " clips") +
                ". Deleting the lane removes them. This can be undone.",
            "Delete Lane", [executeDelete](bool confirmed) {
                if (confirmed) {
                    executeDelete();
                }
            });
        return;
    }
    executeDelete();
}

} // namespace Audio
} // namespace Aestra
