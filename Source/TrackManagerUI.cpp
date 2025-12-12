// © 2025 Nomad Studios All Rights Reserved. Licensed for personal & educational use only.
#include "TrackManagerUI.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Core/NUIDragDrop.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "../NomadCore/include/NomadLog.h"
#include "../NomadAudio/include/AudioFileValidator.h"
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

    // Get theme colors for consistent styling
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();

    // Create window control icons (lightweight SVGs instead of buttons)
    // Close icon (Ã—)
    const char* closeSvg = R"(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <line x1="18" y1="6" x2="6" y2="18"/>
            <line x1="6" y1="6" x2="18" y2="18"/>
        </svg>
    )";
    m_closeIcon = std::make_shared<NomadUI::NUIIcon>(closeSvg);
    m_closeIcon->setIconSize(NomadUI::NUIIconSize::Small);
    m_closeIcon->setColorFromTheme("textPrimary");
    m_closeIcon->setVisible(true);
    // Don't add as child - we'll render manually

    // Minimize icon (âˆ’)
    const char* minimizeSvg = R"(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <line x1="5" y1="12" x2="19" y2="12"/>
        </svg>
    )";
    m_minimizeIcon = std::make_shared<NomadUI::NUIIcon>(minimizeSvg);
    m_minimizeIcon->setIconSize(NomadUI::NUIIconSize::Small);
    m_minimizeIcon->setColorFromTheme("textPrimary");
    m_minimizeIcon->setVisible(true);
    // Don't add as child - we'll render manually

    // Maximize icon (â–¡)
    const char* maximizeSvg = R"(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <rect x="6" y="6" width="12" height="12"/>
        </svg>
    )";
    m_maximizeIcon = std::make_shared<NomadUI::NUIIcon>(maximizeSvg);
    m_maximizeIcon->setIconSize(NomadUI::NUIIconSize::Small);
    m_maximizeIcon->setColorFromTheme("textPrimary");
    m_maximizeIcon->setVisible(true);
    // Don't add as child - we'll render manually

    // Create add track button
    m_addTrackButton = std::make_shared<NomadUI::NUIButton>();
    m_addTrackButton->setText("+");
    m_addTrackButton->setOnClick([this]() {
        onAddTrackClicked();
    });
    // Set colors: grey on hover, white on click
    NomadUI::NUIColor buttonBg(0.15f, 0.15f, 0.18f, 1.0f);
    NomadUI::NUIColor buttonHover(0.25f, 0.25f, 0.28f, 1.0f);
    NomadUI::NUIColor buttonPressed(0.15f, 0.15f, 0.18f, 1.0f);
    m_addTrackButton->setBackgroundColor(buttonBg);
    m_addTrackButton->setTextColor(themeManager.getColor("textPrimary"));
    m_addTrackButton->setHoverColor(buttonHover);
    m_addTrackButton->setPressedColor(buttonPressed);
    addChild(m_addTrackButton);
    
    // Create scrollbar
    m_scrollbar = std::make_shared<NomadUI::NUIScrollbar>(NomadUI::NUIScrollbar::Orientation::Vertical);
    m_scrollbar->setOnScroll([this](double position) {
        onScroll(position);
    });
    addChild(m_scrollbar);
    
    // Create horizontal scrollbar for timeline panning
    m_horizontalScrollbar = std::make_shared<NomadUI::NUIScrollbar>(NomadUI::NUIScrollbar::Orientation::Horizontal);
    m_horizontalScrollbar->setStyle(NomadUI::NUIScrollbar::Style::Timeline);
    m_horizontalScrollbar->setThumbSize(24.0); // Initial size, will be updated
    // We need to set the height of the scrollbar component itself, not just thumb size
    // The scrollbar size is set in layout code usually.
    
    m_horizontalScrollbar->setOnScroll([this](double position) {
        onHorizontalScroll(position);
    });
    
    m_horizontalScrollbar->setOnRangeChange([this](double start, double size) {
        // Calculate grid width (viewport)
        NomadUI::NUIRect bounds = getBounds();
        auto& themeManager = NomadUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        float buttonX = themeManager.getComponentDimension("trackControls", "buttonStartX");
        float scrollbarWidth = 15.0f;
        float trackWidth = bounds.width - scrollbarWidth;
        float gridWidth = trackWidth - (buttonX + layout.controlButtonWidth + 10);
        
        if (gridWidth <= 0 || size <= 0) return;
        
        // Calculate scale factor
        // size is the amount of content (in current pixels) that we WANT to fit in gridWidth
        double scaleFactor = gridWidth / size;
        
        // Update pixels per beat
        float newPixelsPerBeat = m_pixelsPerBeat * static_cast<float>(scaleFactor);
        
        // Clamp zoom
        newPixelsPerBeat = std::clamp(newPixelsPerBeat, 10.0f, 500.0f);
        
        // Update scroll offset
        // start is the new start position in current pixels. Convert to new pixels.
        // Note: start is in the same units as size (current pixels)
        float newScrollOffset = static_cast<float>(start) * (newPixelsPerBeat / m_pixelsPerBeat);
        
        // Sync both current and target zoom so smooth-zoom animator
        // doesn't immediately lerp us back to the previous target.
        m_pixelsPerBeat = newPixelsPerBeat;
        m_targetPixelsPerBeat = newPixelsPerBeat;
        m_timelineScrollOffset = newScrollOffset;
        
        // Update UI
        for (auto& trackUI : m_trackUIComponents) {
            trackUI->setPixelsPerBeat(m_pixelsPerBeat);
            trackUI->setTimelineScrollOffset(m_timelineScrollOffset);
        }
        
        if (m_pianoRollPanel) {
             m_pianoRollPanel->setPixelsPerBeat(m_pixelsPerBeat);
        }
        
        updateHorizontalScrollbar();
        invalidateCache();
    });
    
    addChild(m_horizontalScrollbar);

    // Create UI components for existing tracks
    refreshTracks();

    // Create piano roll panel
    m_pianoRollPanel = std::make_shared<PianoRollPanel>(m_trackManager);
    m_pianoRollPanel->setPixelsPerBeat(m_pixelsPerBeat);
    m_pianoRollPanel->setBeatsPerBar(m_beatsPerBar);
    m_pianoRollPanel->setOnMinimizeToggle([this](bool minimized) {
        layoutTracks(); // Relayout when minimized/expanded
    });
    m_pianoRollPanel->setOnMaximizeToggle([this](bool maximized) {
        layoutTracks(); // Relayout when maximized/restored
    });
    m_pianoRollPanel->setOnClose([this]() {
        togglePianoRoll(); // Close panel
    });
    addChild(m_pianoRollPanel);
    m_pianoRollPanel->setVisible(false);
    m_showPianoRoll = false;

    // Create step sequencer panel
    m_sequencerPanel = std::make_shared<StepSequencerPanel>(m_trackManager);
    m_sequencerPanel->setOnMinimizeToggle([this](bool) {
        layoutTracks();
    });
    m_sequencerPanel->setOnMaximizeToggle([this](bool) {
        layoutTracks();
    });
    m_sequencerPanel->setOnClose([this]() {
        toggleSequencer();
    });
    addChild(m_sequencerPanel);
    m_sequencerPanel->setVisible(false);
    m_showSequencer = false;
    
    // Create mixer panel
    m_mixerPanel = std::make_shared<MixerPanel>(m_trackManager);
    m_mixerPanel->setOnMinimizeToggle([this](bool minimized) {
        layoutTracks(); // Relayout when minimized/expanded
    });
    m_mixerPanel->setOnMaximizeToggle([this](bool maximized) {
        layoutTracks(); // Relayout when maximized/restored
    });
    m_mixerPanel->setOnClose([this]() {
        toggleMixer(); // Close panel
    });
    addChild(m_mixerPanel);
    m_mixerPanel->setVisible(false);
    m_showMixer = false;

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
    // Texture IDs: m_backgroundTextureId, m_controlsTextureId, m_trackCaches[].textureId
    // are managed by the renderer lifecycle and don't need explicit deletion here.
    Log::info("TrackManagerUI destroyed");
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
    
    // === LOOP TOOL ICON (Circular arrows) ===
    const char* loopSvg = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path d="M17 1 L21 5 L17 9" fill="none" stroke="#66BB6A" stroke-width="2"/><path d="M7 15 L3 19 L7 23" fill="none" stroke="#66BB6A" stroke-width="2"/><path d="M21 5 L12 5 C8 5 4 9 4 12" fill="none" stroke="#66BB6A" stroke-width="2"/><path d="M3 19 L12 19 C16 19 20 15 20 12" fill="none" stroke="#66BB6A" stroke-width="2"/></svg>)";
    m_loopToolIcon = std::make_shared<NomadUI::NUIIcon>(loopSvg);
    
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

void TrackManagerUI::updateToolbarBounds() {
    // Position toolbar at top-left of grid area, aligned with header
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    float buttonX = themeManager.getComponentDimension("trackControls", "buttonStartX");
    float controlAreaWidth = buttonX + layout.controlButtonWidth + 10;
    float gridStartX = controlAreaWidth + 5;
    
    NomadUI::NUIRect bounds = getBounds();
    // Align toolbar with the ruler/header area to feel "attached"
    // Move it slightly left to overlap the control area/grid boundary, acting as a bridge
    float toolbarX = bounds.x + gridStartX - 10.0f; 
    float toolbarY = bounds.y + 2.0f;  // Tucked into the top header space
    float iconSize = 22.0f;  // Smaller, more compact icons
    float iconSpacing = 2.0f;  // Tight spacing
    float toolbarPadding = 4.0f;
    
    // Calculate toolbar width based on number of tools
    int numTools = 4;  // Select, Split, MultiSelect, Loop
    float toolbarWidth = (iconSize * numTools) + (iconSpacing * (numTools - 1)) + (toolbarPadding * 2);
    float toolbarHeight = iconSize + (toolbarPadding * 2);
    
    m_toolbarBounds = NomadUI::NUIRect(toolbarX, toolbarY, toolbarWidth, toolbarHeight);
    
    // Position individual icon bounds
    float iconX = toolbarX + toolbarPadding;
    float iconY = toolbarY + toolbarPadding;
    
    m_selectToolBounds = NomadUI::NUIRect(iconX, iconY, iconSize, iconSize);
    iconX += iconSize + iconSpacing;
    
    m_splitToolBounds = NomadUI::NUIRect(iconX, iconY, iconSize, iconSize);
    iconX += iconSize + iconSpacing;
    
    m_multiSelectToolBounds = NomadUI::NUIRect(iconX, iconY, iconSize, iconSize);
    iconX += iconSize + iconSpacing;
    
    m_loopToolBounds = NomadUI::NUIRect(iconX, iconY, iconSize, iconSize);
}

void TrackManagerUI::renderToolbar(NomadUI::NUIRenderer& renderer) {
    // Update bounds before rendering
    updateToolbarBounds();
    
    // GLASSMORPHISM POLISH
    // 1. Drop Shadow for depth
    renderer.drawShadow(m_toolbarBounds, 0.0f, 4.0f, 12.0f, NomadUI::NUIColor(0.0f, 0.0f, 0.0f, 0.5f));

    // 2. Glassy Background (Semi-transparent dark with slight blue tint)
    // Using 0-1 float range for colors
    renderer.fillRoundedRect(m_toolbarBounds, 6.0f, NomadUI::NUIColor(30.0f/255.0f, 30.0f/255.0f, 35.0f/255.0f, 0.9f));
    
    // 3. Crisp Border (Subtle highlight on top/left, shadow on bottom/right implies 3D, but simple stroke is fine for now)
    renderer.strokeRoundedRect(m_toolbarBounds, 6.0f, 1.0f, NomadUI::NUIColor(1.0f, 1.0f, 1.0f, 0.15f));
    
    // Helper lambda to draw icon with selection state
    auto drawToolIcon = [&](std::shared_ptr<NomadUI::NUIIcon>& icon, const NomadUI::NUIRect& bounds, PlaylistTool tool, bool hovered) {
        bool isActive = (m_currentTool == tool);
        
        // Draw selection highlight background
        if (isActive) {
            // Active tool gets accent color + Glow
            NomadUI::NUIColor accentColor(187.0f/255.0f, 134.0f/255.0f, 252.0f/255.0f, 0.8f);
            renderer.drawGlow(bounds, 4.0f, 0.5f, accentColor);
            renderer.fillRoundedRect(bounds, 4.0f, accentColor);  // Purple accent
        } else if (hovered) {
            // Hover state
            renderer.fillRoundedRect(bounds, 4.0f, NomadUI::NUIColor(1.0f, 1.0f, 1.0f, 0.15f)); // Lighter hover
        }
        
        // Draw icon
        if (icon) {
            // Small padding around icon
            float padding = 2.0f;
            NomadUI::NUIRect iconRect(
                bounds.x + padding,
                bounds.y + padding,
                bounds.width - padding * 2,
                bounds.height - padding * 2
            );
            icon->setBounds(iconRect);
            icon->onRender(renderer);
        }
    };
    
    // Draw each tool icon with hover state
    drawToolIcon(m_selectToolIcon, m_selectToolBounds, PlaylistTool::Select, m_selectToolHovered);
    drawToolIcon(m_splitToolIcon, m_splitToolBounds, PlaylistTool::Split, m_splitToolHovered);
    drawToolIcon(m_multiSelectToolIcon, m_multiSelectToolBounds, PlaylistTool::MultiSelect, m_multiSelectToolHovered);
    drawToolIcon(m_loopToolIcon, m_loopToolBounds, PlaylistTool::Loop, m_loopToolHovered);
}

bool TrackManagerUI::handleToolbarClick(const NomadUI::NUIPoint& position) {
    // Check if click is within toolbar bounds
    if (!m_toolbarBounds.contains(position)) {
        return false;
    }
    
    // Check each icon
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
    if (m_loopToolBounds.contains(position)) {
        setCurrentTool(PlaylistTool::Loop);
        return true;
    }
    
    return true;  // Consumed by toolbar area even if no icon hit
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
    float buttonX = themeManager.getComponentDimension("trackControls", "buttonStartX");
    float controlAreaWidth = buttonX + layout.controlButtonWidth + 10;
    float gridStartX = bounds.x + controlAreaWidth + 5;
    float headerHeight = 30.0f;
    float rulerHeight = 20.0f;
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
    for (size_t i = 0; i < m_trackUIComponents.size(); ++i) {
        auto& trackUI = m_trackUIComponents[i];
        if (!trackUI) continue;
        
        auto track = trackUI->getTrack();
        if (!track || track->getAudioData().empty()) continue;
        
        // Calculate clip bounds for this track
        NomadUI::NUIRect trackBounds = trackUI->getBounds();
        float clipStartPixel = gridStartX + static_cast<float>(track->getStartPositionInTimeline() * m_pixelsPerBeat * (120.0 / 60.0)) - m_timelineScrollOffset;
        float clipWidth = static_cast<float>(track->getDuration() * m_pixelsPerBeat * (120.0 / 60.0));
        
        NomadUI::NUIRect clipBounds(clipStartPixel, trackBounds.y, clipWidth, trackBounds.height);
        
        // Check if mouse is within this clip's bounds (BOTH X and Y)
        // Only draw split line on the clip that's directly under the cursor
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
            break;  // Only one clip can be directly under cursor, so stop after finding it
        }
    }
    
    // Always draw scissors icon at cursor position when in grid (Split tool)
    if (m_splitToolIcon) {
        NomadUI::NUIRect iconRect(position.x - 10, position.y - 10, 20, 20);
        m_splitToolIcon->setBounds(iconRect);
        m_splitToolIcon->onRender(renderer);
    }
}

void TrackManagerUI::performSplitAtPosition(int trackIndex, double timeSeconds) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_trackUIComponents.size())) return;
    
    // Get the track at this index
    auto track = m_trackManager->getTrack(trackIndex);
    if (!track) {
        Log::warning("Split failed: Track not found at index " + std::to_string(trackIndex));
        return;
    }
    
    // Convert timeline position to local track time
    double trackStart = track->getStartPositionInTimeline();
    double localTime = timeSeconds - trackStart;
    
    // Check if split point is within the track bounds
    if (localTime <= 0.0 || localTime >= track->getDuration()) {
        Log::info("Split point outside track bounds: " + std::to_string(localTime) + 
                  "s (duration: " + std::to_string(track->getDuration()) + "s)");
        return;
    }
    
    // Perform the split using TrackManager
    auto newTrack = m_trackManager->sliceClip(trackIndex, localTime);
    
    if (newTrack) {
        Log::info("Successfully split track '" + track->getName() + 
                  "' at " + std::to_string(timeSeconds) + "s (local: " + std::to_string(localTime) + "s)");
        
        // Mark project as modified
        m_trackManager->markModified();
        
        // Refresh UI to show the new track
        refreshTracks();
        invalidateCache();
    } else {
        Log::warning("Split failed: TrackManager::sliceClip returned null");
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
        auto track = m_trackManager->addTrack(name);

        // Create UI component for the track, passing TrackManager for solo coordination
        auto trackUI = std::make_shared<TrackUIComponent>(track, m_trackManager.get());
        
        // Register callback for exclusive solo coordination
        trackUI->setOnSoloToggled([this](TrackUIComponent* soloedTrack) {
            this->onTrackSoloToggled(soloedTrack);
        });
        
        // Register callback for cache invalidation (button hover, etc.)
        trackUI->setOnCacheInvalidationNeeded([this]() {
            this->invalidateCache();
        });
        
        // Register callback for clip deletion with ripple animation
        trackUI->setOnClipDeleted([this](TrackUIComponent* trackComp, NomadUI::NUIPoint ripplePos) {
            this->onClipDeleted(trackComp, ripplePos);
        });
        
        m_trackUIComponents.push_back(trackUI);
        addChild(trackUI);

        layoutTracks();
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

    // Create UI components for all tracks (except preview track)
    for (size_t i = 0; i < m_trackManager->getTrackCount(); ++i) {
        auto track = m_trackManager->getTrack(i);
        if (track && track->getName() != "Preview") {  // Skip preview track
            // Pass TrackManager for solo coordination
            auto trackUI = std::make_shared<TrackUIComponent>(track, m_trackManager.get());
            
            // Register callback for exclusive solo coordination
            trackUI->setOnSoloToggled([this](TrackUIComponent* soloedTrack) {
                this->onTrackSoloToggled(soloedTrack);
            });
            
            // Register callback for cache invalidation (button hover, etc.)
            trackUI->setOnCacheInvalidationNeeded([this]() {
                this->invalidateCache();
            });
            
            // Register callback for clip deletion with ripple animation
            trackUI->setOnClipDeleted([this](TrackUIComponent* trackComp, NomadUI::NUIPoint ripplePos) {
                this->onClipDeleted(trackComp, ripplePos);
            });
            
            // Register callback for checking if split tool is active
            trackUI->setIsSplitToolActive([this]() {
                return this->m_currentTool == PlaylistTool::Split;
            });
            
            // Register callback for split requests
            trackUI->setOnSplitRequested([this](TrackUIComponent* trackComp, double splitTime) {
                this->onSplitRequested(trackComp, splitTime);
            });
            
            // Sync zoom settings to new track
            trackUI->setPixelsPerBeat(m_pixelsPerBeat);
            trackUI->setBeatsPerBar(m_beatsPerBar);
            trackUI->setTimelineScrollOffset(m_timelineScrollOffset);
            // No maxExtent needed - infinite timeline with culling
            
            m_trackUIComponents.push_back(trackUI);
            addChild(trackUI);
        }
    }

    layoutTracks();
    
    // Refresh mixer channel strips when tracks change
    if (m_mixerPanel) {
        m_mixerPanel->refreshChannels();
    }
    
    // Update scrollbar after tracks are refreshed (fixes initial glitch)
    updateHorizontalScrollbar();
    
    m_cacheInvalidated = true;  // Invalidate cache when tracks refreshed
}

void TrackManagerUI::onTrackSoloToggled(TrackUIComponent* soloedTrack) {
    if (!m_trackManager || !soloedTrack) return;
    
    // Clear all solos in the TrackManager
    m_trackManager->clearAllSolos();
    
    // Update ALL track UIs to reflect the cleared solo states
    for (auto& trackUI : m_trackUIComponents) {
        if (trackUI.get() != soloedTrack) {
            // Force UI update for other tracks (their solo is now off)
            trackUI->updateUI();
        }
    }
    
    Log::info("Solo coordination: Cleared all other solos");
}

void TrackManagerUI::onClipDeleted(TrackUIComponent* trackComp, const NomadUI::NUIPoint& rippleCenter) {
    if (!trackComp) return;
    
    auto track = trackComp->getTrack();
    if (!track || track->getAudioData().empty()) return;
    
    // Get clip bounds before we clear
    NomadUI::NUIRect clipBounds = trackComp->getBounds();
    
    // Start delete animation
    DeleteAnimation anim;
    anim.track = track;
    anim.rippleCenter = rippleCenter;
    anim.clipBounds = clipBounds;
    anim.progress = 0.0f;
    anim.duration = 0.25f;
    m_deleteAnimations.push_back(anim);
    
    // Clear the audio data from the track
    track->clearAudioData();
    
    // Invalidate cache
    invalidateCache();
    
    Log::info("Clip deleted from track: " + track->getName());
}

void TrackManagerUI::onSplitRequested(TrackUIComponent* trackComp, double splitTime) {
    if (!trackComp || !m_trackManager) return;
    
    auto track = trackComp->getTrack();
    if (!track || track->getAudioData().empty()) return;
    
    // Find the track index
    int trackIndex = -1;
    for (size_t i = 0; i < m_trackManager->getTrackCount(); ++i) {
        if (m_trackManager->getTrack(i) == track) {
            trackIndex = static_cast<int>(i);
            break;
        }
    }
    
    if (trackIndex < 0) {
        Log::warning("Split failed: Could not find track index");
        return;
    }
    
    // splitTime is already in audio-relative seconds (0 to duration)
    // Validate split time is within clip bounds
    double duration = track->getDuration();
    if (splitTime <= 0.01 || splitTime >= duration - 0.01) {
        Log::warning("Split time outside clip bounds: " + std::to_string(splitTime) + 
                     " (duration=" + std::to_string(duration) + ")");
        return;
    }
    
    Log::info("Splitting track '" + track->getName() + "' at local time: " + std::to_string(splitTime));
    
    // Use TrackManager::sliceClip which handles lane index properly
    auto newTrack = m_trackManager->sliceClip(static_cast<size_t>(trackIndex), splitTime);
    
    if (newTrack) {
        Log::info("Successfully split track - new track created with lane index: " + 
                  std::to_string(newTrack->getLaneIndex()));
        
        // Refresh UI to show the new track
        refreshTracks();
        invalidateCache();
    } else {
        Log::warning("Split failed: TrackManager::sliceClip returned null");
    }
}

void TrackManagerUI::togglePianoRoll() {
    m_showPianoRoll = !m_showPianoRoll;
    if (m_pianoRollPanel) {
        m_pianoRollPanel->setVisible(m_showPianoRoll);
    }
    layoutTracks();  // Recalculate layout when toggled
    Log::info(m_showPianoRoll ? "Piano Roll panel shown" : "Piano Roll panel hidden");
}

void TrackManagerUI::toggleMixer() {
    m_showMixer = !m_showMixer;
    if (m_mixerPanel) {
        m_mixerPanel->setVisible(m_showMixer);
    }
    layoutTracks();  // Recalculate layout when toggled
    Log::info(m_showMixer ? "Mixer panel shown" : "Mixer panel hidden");
}

void TrackManagerUI::toggleSequencer() {
    m_showSequencer = !m_showSequencer;
    if (m_sequencerPanel) {
        m_sequencerPanel->setVisible(m_showSequencer);
    }
    layoutTracks();  // Recalculate layout when toggled
    Log::info(m_showSequencer ? "Sequencer panel shown" : "Sequencer panel hidden");
}

void TrackManagerUI::togglePlaylist() {
    m_showPlaylist = !m_showPlaylist;
    layoutTracks();  // Recalculate layout when toggled
    Log::info(m_showPlaylist ? "Playlist view shown" : "Playlist view hidden");
}

void TrackManagerUI::onAddTrackClicked() {
    addTrack(); // Add track with auto-generated name
}

void TrackManagerUI::layoutTracks() {
    NomadUI::NUIRect bounds = getBounds();
    Log::info("TrackManagerUI layoutTracks: parent bounds x=" + std::to_string(bounds.x) + ", y=" + std::to_string(bounds.y) + ", w=" + std::to_string(bounds.width) + ", h=" + std::to_string(bounds.height));

    // Get layout dimensions from theme
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();

    float headerHeight = 30.0f;
    float scrollbarWidth = 15.0f;
    float iconSize = 20.0f; // Icon size
    float iconPadding = 5.0f; // Padding around icons
    
    // Check if any panel is maximized (takes over full track area)
    bool pianoRollMaximized = m_showPianoRoll && m_pianoRollPanel && m_pianoRollPanel->isMaximized();
    bool sequencerMaximized = m_showSequencer && m_sequencerPanel && m_sequencerPanel->isMaximized();
    bool mixerMaximized = m_showMixer && m_mixerPanel && m_mixerPanel->isMaximized();
    
    // Calculate available width (excluding mixer if visible and not maximized)
    float availableWidth = bounds.width;
    if (m_showMixer && m_mixerPanel && !mixerMaximized) {
        float mixerPanelWidth = m_mixerPanel->isMinimized() ? m_mixerPanel->getTitleBarHeight() : m_mixerWidth;
        availableWidth -= mixerPanelWidth;
    }
    
    // Position window control icons (top-right corner of available space, centered in header)
    // Store as RELATIVE bounds for hit testing (localPos is relative to component)
    float iconY = (headerHeight - iconSize) / 2.0f;
    float iconSpacing = iconSize + iconPadding;
    
    m_closeIconBounds = NomadUI::NUIRect(availableWidth - iconSize - iconPadding, iconY, iconSize, iconSize);
    m_maximizeIconBounds = NomadUI::NUIRect(availableWidth - iconSize - iconPadding - iconSpacing, iconY, iconSize, iconSize);
    m_minimizeIconBounds = NomadUI::NUIRect(availableWidth - iconSize - iconPadding - iconSpacing * 2, iconY, iconSize, iconSize);
    
    // Layout add track button (top-left)
    float currentHeaderX = 0.0f;
    if (m_addTrackButton) {
        float buttonSize = 30.0f;
        m_addTrackButton->setBounds(NUIAbsolute(bounds, 0.0f, 0.0f, buttonSize, buttonSize));
        currentHeaderX += buttonSize + 5.0f;
    }

    // Calculate total content height
    float rulerHeight = 20.0f; // Time ruler height
    float horizontalScrollbarHeight = 24.0f;
    float totalContentHeight = m_trackUIComponents.size() * (m_trackHeight + m_trackSpacing);
    
    // If a panel is maximized, it takes over the ENTIRE area (including title bar)
    if (pianoRollMaximized || mixerMaximized || sequencerMaximized) {
        // Hide track controls when a panel is maximized
        if (m_addTrackButton) m_addTrackButton->setVisible(false);
        if (m_scrollbar) m_scrollbar->setVisible(false);
        if (m_horizontalScrollbar) m_horizontalScrollbar->setVisible(false);
        
        // Hide all tracks
        for (auto& trackUI : m_trackUIComponents) {
            trackUI->setVisible(false);
        }
        
        // Layout the maximized panel to fill ENTIRE bounds (replaces TrackManager completely)
        if (pianoRollMaximized) {
            m_pianoRollPanel->setBounds(NUIAbsolute(bounds, 0, 0, bounds.width, bounds.height));
            m_pianoRollPanel->setVisible(true);
            m_pianoRollPanel->onResize(static_cast<int>(bounds.width), static_cast<int>(bounds.height));
            
            // Update piano roll settings
            m_pianoRollPanel->setBeatsPerBar(m_beatsPerBar);
            m_pianoRollPanel->setPixelsPerBeat(m_pixelsPerBeat);
            
            // Hide other panels when one is maximized
            if (m_mixerPanel) m_mixerPanel->setVisible(false);
            if (m_sequencerPanel) m_sequencerPanel->setVisible(false);
        } else if (sequencerMaximized) {
            m_sequencerPanel->setBounds(NUIAbsolute(bounds, 0, 0, bounds.width, bounds.height));
            m_sequencerPanel->setVisible(true);
            m_sequencerPanel->onResize(static_cast<int>(bounds.width), static_cast<int>(bounds.height));
            
            // Hide other panels when one is maximized
            if (m_pianoRollPanel) m_pianoRollPanel->setVisible(false);
            if (m_mixerPanel) m_mixerPanel->setVisible(false);
        } else if (mixerMaximized) {
            m_mixerPanel->setBounds(NUIAbsolute(bounds, 0, 0, bounds.width, bounds.height));
            m_mixerPanel->setVisible(true);
            m_mixerPanel->onResize(static_cast<int>(bounds.width), static_cast<int>(bounds.height));
            
            // Hide other panels when one is maximized
            if (m_pianoRollPanel) m_pianoRollPanel->setVisible(false);
            if (m_sequencerPanel) m_sequencerPanel->setVisible(false);
        }
        
        return; // Skip normal layout
    }
    
    // Normal layout (no maximized panels)
    // Show track controls ONLY if playlist is visible
    if (m_addTrackButton) m_addTrackButton->setVisible(m_showPlaylist);
    if (m_scrollbar) m_scrollbar->setVisible(m_showPlaylist);
    if (m_horizontalScrollbar) m_horizontalScrollbar->setVisible(m_showPlaylist);
    
    // Show/Hide tracks based on playlist visibility
    for (auto& trackUI : m_trackUIComponents) {
        trackUI->setVisible(m_showPlaylist);
    }
    
    // Reserve space for mixer on right if visible (only title bar if minimized)
    float mixerSpace = 0.0f;
    if (m_showMixer && m_mixerPanel && !mixerMaximized) {
        if (m_mixerPanel->isMinimized()) {
            mixerSpace = m_mixerPanel->getTitleBarHeight() + 5.0f; // Just title bar
        } else {
            mixerSpace = m_mixerWidth + 5.0f; // Full width
        }
    }
    
    // Reserve space for piano roll at bottom if visible (only title bar if minimized)
    float pianoRollSpace = 0.0f;
    if (m_showPianoRoll && m_pianoRollPanel && !pianoRollMaximized) {
        if (m_pianoRollPanel->isMinimized()) {
            pianoRollSpace = m_pianoRollPanel->getTitleBarHeight() + 5.0f; // Just title bar
        } else {
            pianoRollSpace = m_pianoRollHeight + 5.0f; // Full height
        }
    }

    // Reserve space for sequencer at bottom if visible (only title bar if minimized)
    float sequencerSpace = 0.0f;
    if (m_showSequencer && m_sequencerPanel && !sequencerMaximized) {
        if (m_sequencerPanel->isMinimized()) {
            sequencerSpace = m_sequencerPanel->getTitleBarHeight() + 5.0f; // Just title bar
        } else {
            sequencerSpace = m_pianoRollHeight + 5.0f; // Full height (reuse piano roll height for now)
        }
    }
    
    float viewportHeight = bounds.height - headerHeight - horizontalScrollbarHeight - rulerHeight - pianoRollSpace - sequencerSpace;
    
    // Layout horizontal scrollbar (top, right after header, before ruler)
    if (m_horizontalScrollbar) {
        // Position right after header, before ruler, spanning from left edge to mixer/scrollbar
        float horizontalScrollbarWidth = bounds.width - scrollbarWidth - mixerSpace;
        float horizontalScrollbarY = headerHeight;
        m_horizontalScrollbar->setBounds(NUIAbsolute(bounds, 0, horizontalScrollbarY, horizontalScrollbarWidth, horizontalScrollbarHeight));
        updateHorizontalScrollbar();
    }
    
    // Layout vertical scrollbar (right side, below header, horizontal scrollbar, and ruler, left of mixer if visible)
    if (m_scrollbar) {
        float scrollbarY = headerHeight + horizontalScrollbarHeight + rulerHeight;
        float scrollbarX = bounds.width - scrollbarWidth - mixerSpace;
        m_scrollbar->setBounds(NUIAbsolute(bounds, scrollbarX, scrollbarY, scrollbarWidth, viewportHeight));
        updateScrollbar();
    }

    float currentY = headerHeight + horizontalScrollbarHeight + rulerHeight - m_scrollOffset; // Start below header, horizontal scrollbar, and ruler

    // === FL STUDIO STYLE LANE GROUPING ===
    // Group tracks by their laneIndex - tracks with the same laneIndex share a visual row
    // First track in each lane group becomes primary (draws controls), others are clips on that lane
    
    std::map<int32_t, std::vector<std::shared_ptr<TrackUIComponent>>> laneGroups;
    std::map<int32_t, std::vector<std::shared_ptr<Track>>> laneTracks;
    
    // First pass: group tracks by lane index
    for (size_t i = 0; i < m_trackUIComponents.size(); ++i) {
        auto trackUI = m_trackUIComponents[i];
        auto track = trackUI->getTrack();
        if (!track) continue;
        
        int32_t laneIndex = track->getLaneIndex();
        if (laneIndex < 0) {
            // Track has no lane index - treat it as its own lane (auto-assign based on visual order)
            laneIndex = 10000 + static_cast<int32_t>(i);  // High number to place after grouped lanes
        }
        
        laneGroups[laneIndex].push_back(trackUI);
        laneTracks[laneIndex].push_back(track);
    }
    
    // Second pass: layout each lane group
    // Tracks with same laneIndex go on the same visual row
    for (auto& [laneIndex, trackUIs] : laneGroups) {
        if (trackUIs.empty()) continue;
        
        // First component in the lane is primary (draws controls)
        auto primaryUI = trackUIs[0];
        primaryUI->setIsPrimaryForLane(true);
        primaryUI->clearLaneClips();
        
        // Add other tracks in this lane as additional clips
        const auto& tracks = laneTracks[laneIndex];
        for (size_t i = 1; i < tracks.size(); ++i) {
            primaryUI->addLaneClip(tracks[i]);
        }
        
        // Position primary at current Y (only primary is visible)
        float trackWidth = bounds.width - scrollbarWidth - mixerSpace;
        primaryUI->setBounds(NUIAbsolute(bounds, 0, currentY, trackWidth, m_trackHeight));
        primaryUI->setVisible(m_showPlaylist);
        
        // Hide secondary UIs (their clips are rendered by primary)
        for (size_t i = 1; i < trackUIs.size(); ++i) {
            trackUIs[i]->setIsPrimaryForLane(false);
            trackUIs[i]->clearLaneClips();
            trackUIs[i]->setVisible(false);  // Hidden - primary renders all clips
        }
        
        // Only advance Y for each visual lane (not each track)
        currentY += m_trackHeight + m_trackSpacing;
    }
    
    // Layout piano roll panel at bottom (full width - independent of mixer)
    if (m_pianoRollPanel && m_showPianoRoll) {
        float panelWidth = bounds.width;  // Full width, not constrained by mixer
        float panelHeight = m_pianoRollPanel->isMinimized() ? m_pianoRollPanel->getTitleBarHeight() : m_pianoRollHeight;
        float pianoY = bounds.height - panelHeight - sequencerSpace; // Stack above sequencer if both visible
        
        m_pianoRollPanel->setBounds(NUIAbsolute(bounds, 0, pianoY, panelWidth, panelHeight));
        m_pianoRollPanel->setVisible(true);
        
        // Update piano roll settings
        m_pianoRollPanel->setBeatsPerBar(m_beatsPerBar);
        m_pianoRollPanel->setPixelsPerBeat(m_pixelsPerBeat);
        
        // Trigger layout of panel's internal components
        m_pianoRollPanel->onResize(static_cast<int>(panelWidth), static_cast<int>(panelHeight));
    } else if (m_pianoRollPanel) {
        m_pianoRollPanel->setVisible(false);
    }

    // Layout sequencer panel at bottom (full width - independent of mixer)
    if (m_sequencerPanel && m_showSequencer) {
        float panelWidth = bounds.width;  // Full width, not constrained by mixer
        float panelHeight = m_sequencerPanel->isMinimized() ? m_sequencerPanel->getTitleBarHeight() : m_pianoRollHeight; // Reuse height
        float seqY = bounds.height - panelHeight;
        
        m_sequencerPanel->setBounds(NUIAbsolute(bounds, 0, seqY, panelWidth, panelHeight));
        m_sequencerPanel->setVisible(true);
        m_sequencerPanel->onResize(static_cast<int>(panelWidth), static_cast<int>(panelHeight));
    } else if (m_sequencerPanel) {
        m_sequencerPanel->setVisible(false);
    }
    
    // Layout mixer panel on right side (if visible) - ABOVE the title bar
    if (m_mixerPanel && m_showMixer) {
        float panelWidth = m_mixerPanel->isMinimized() ? m_mixerPanel->getTitleBarHeight() : m_mixerWidth;
        float mixerX = bounds.width - panelWidth;
        float mixerY = 0;  // Start at top, above title bar
        float mixerHeight = bounds.height;  // Full height
        
        m_mixerPanel->setBounds(NUIAbsolute(bounds, mixerX, mixerY, panelWidth, mixerHeight));
        m_mixerPanel->setVisible(true);
        
        // Trigger layout of panel's internal components
        m_mixerPanel->onResize(static_cast<int>(panelWidth), static_cast<int>(mixerHeight));
    } else if (m_mixerPanel) {
        m_mixerPanel->setVisible(false);
    }
}

void TrackManagerUI::updateTrackPositions() {
    layoutTracks();
}

void TrackManagerUI::onRender(NomadUI::NUIRenderer& renderer) {
    rmt_ScopedCPUSample(TrackMgrUI_Render, 0);
    
    NomadUI::NUIRect bounds = getBounds();
    
    // Check if any panel is maximized - if so, only render that panel (no caching needed)
    bool pianoRollMaximized = m_showPianoRoll && m_pianoRollPanel && m_pianoRollPanel->isMaximized();
    bool sequencerMaximized = m_showSequencer && m_sequencerPanel && m_sequencerPanel->isMaximized();
    bool mixerMaximized = m_showMixer && m_mixerPanel && m_mixerPanel->isMaximized();
    
    if (pianoRollMaximized && m_pianoRollPanel) {
        m_pianoRollPanel->onRender(renderer);
        return;
    }
    
    if (mixerMaximized && m_mixerPanel) {
        m_mixerPanel->onRender(renderer);
        return;
    }

    if (sequencerMaximized && m_sequencerPanel) {
        m_sequencerPanel->onRender(renderer);
        return;
    }
    
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
    
    // Render playhead OUTSIDE cache (it moves every frame during playback)
    renderPlayhead(renderer);
    
    // Render drop preview OUTSIDE cache (dynamic during drag)
    if (m_showDropPreview) {
        renderDropPreview(renderer);
    }
    
    // Render delete animations OUTSIDE cache (FL Studio ripple effect)
    renderDeleteAnimations(renderer);
    
    // Render scrollbars OUTSIDE cache (they interact with mouse)
    if (m_addTrackButton && m_addTrackButton->isVisible()) m_addTrackButton->onRender(renderer);
    if (m_horizontalScrollbar && m_horizontalScrollbar->isVisible()) m_horizontalScrollbar->onRender(renderer);
    if (m_scrollbar && m_scrollbar->isVisible()) m_scrollbar->onRender(renderer);
    
    // Render panels OUTSIDE cache (they are dynamic/interactive)
    if (m_pianoRollPanel && m_showPianoRoll && m_pianoRollPanel->isVisible()) {
        m_pianoRollPanel->onRender(renderer);
    }
    if (m_sequencerPanel && m_showSequencer && m_sequencerPanel->isVisible()) {
        m_sequencerPanel->onRender(renderer);
    }
    if (m_mixerPanel && m_showMixer && m_mixerPanel->isVisible()) {
        m_mixerPanel->onRender(renderer);
    }
    
    // Render toolbar OUTSIDE cache (interactive tool selection)
    renderToolbar(renderer);
    
    // Render split cursor if split tool is active (follows mouse)
    if (m_currentTool == PlaylistTool::Split) {
        renderSplitCursor(renderer, m_lastMousePos);
    }
    
    // Render selection box if currently drawing one
    if (m_isDrawingSelectionBox) {
        float minX = std::min(m_selectionBoxStart.x, m_selectionBoxEnd.x);
        float maxX = std::max(m_selectionBoxStart.x, m_selectionBoxEnd.x);
        float minY = std::min(m_selectionBoxStart.y, m_selectionBoxEnd.y);
        float maxY = std::max(m_selectionBoxStart.y, m_selectionBoxEnd.y);
        
        NomadUI::NUIRect selectionRect(minX, minY, maxX - minX, maxY - minY);
        
        // Semi-transparent blue fill
        NomadUI::NUIColor fillColor(79, 195, 247, 40);  // Light blue with low alpha
        renderer.fillRect(selectionRect, fillColor);
        
        // Dashed blue border
        NomadUI::NUIColor borderColor(79, 195, 247, 200);  // Light blue
        
        // Draw dashed border (top, bottom, left, right)
        float dashLength = 5.0f;
        float gapLength = 3.0f;
        
        // Top edge
        for (float x = minX; x < maxX; x += dashLength + gapLength) {
            float dashEnd = std::min(x + dashLength, maxX);
            renderer.drawLine(NomadUI::NUIPoint(x, minY), NomadUI::NUIPoint(dashEnd, minY), 1.5f, borderColor);
        }
        // Bottom edge
        for (float x = minX; x < maxX; x += dashLength + gapLength) {
            float dashEnd = std::min(x + dashLength, maxX);
            renderer.drawLine(NomadUI::NUIPoint(x, maxY), NomadUI::NUIPoint(dashEnd, maxY), 1.5f, borderColor);
        }
        // Left edge
        for (float y = minY; y < maxY; y += dashLength + gapLength) {
            float dashEnd = std::min(y + dashLength, maxY);
            renderer.drawLine(NomadUI::NUIPoint(minX, y), NomadUI::NUIPoint(minX, dashEnd), 1.5f, borderColor);
        }
        // Right edge
        for (float y = minY; y < maxY; y += dashLength + gapLength) {
            float dashEnd = std::min(y + dashLength, maxY);
            renderer.drawLine(NomadUI::NUIPoint(maxX, y), NomadUI::NUIPoint(maxX, dashEnd), 1.5f, borderColor);
        }
    }
}

// Helper method: Direct rendering (used both for fallback and cache rebuild)
void TrackManagerUI::renderTrackManagerDirect(NomadUI::NUIRenderer& renderer) {
    NomadUI::NUIRect bounds = getBounds();
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    
    // Check panel states for layout calculations
    bool mixerMaximized = m_showMixer && m_mixerPanel && m_mixerPanel->isMaximized();
    
    // Calculate where the grid/background should end
    float buttonX = themeManager.getComponentDimension("trackControls", "buttonStartX");
    float controlAreaWidth = buttonX + layout.controlButtonWidth + 10;
    float gridStartX = controlAreaWidth + 5;
    
    // Draw background (control area + full grid area - no bounds restriction)
    NomadUI::NUIColor bgColor = themeManager.getColor("backgroundPrimary");
    
    if (m_showPlaylist) {
        // Background for control area (always visible)
        NomadUI::NUIRect controlBg(bounds.x, bounds.y, controlAreaWidth, bounds.height);
        renderer.fillRect(controlBg, bgColor);
        
        // Background for grid area (extends infinitely, culling handles visibility)
        float scrollbarWidth = 15.0f;
        float gridWidth = bounds.width - controlAreaWidth - scrollbarWidth - 5;
        NomadUI::NUIRect gridBg(bounds.x + gridStartX, bounds.y, gridWidth, bounds.height);
        renderer.fillRect(gridBg, bgColor);

        // Draw border
        NomadUI::NUIColor borderColor = themeManager.getColor("border");
        renderer.strokeRect(bounds, 1, borderColor);
    }
    
    // Update scrollbar range dynamically
    updateHorizontalScrollbar();

    // Calculate available width for header elements (excluding mixer if visible)
    float headerAvailableWidth = bounds.width;
    if (m_showMixer && m_mixerPanel && !mixerMaximized) {
        float mixerPanelWidth = m_mixerPanel->isMinimized() ? m_mixerPanel->getTitleBarHeight() : m_mixerWidth;
        headerAvailableWidth -= mixerPanelWidth;
    }
    
    // Draw track count - positioned in top-right corner of available space with proper margin
    if (m_showPlaylist) {
        std::string infoText = "Tracks: " + std::to_string(m_trackManager ? m_trackManager->getTrackCount() - (m_trackManager->getTrackCount() > 0 ? 1 : 0) : 0);  // Exclude preview track
        auto infoSize = renderer.measureText(infoText, 12);

        // Ensure text doesn't exceed available width and position with proper margin
        float margin = layout.panelMargin;
        float maxTextWidth = headerAvailableWidth - 2 * margin;
        if (infoSize.width > maxTextWidth) {
            // Truncate if too long
            std::string truncatedText = infoText;
            while (!truncatedText.empty() && renderer.measureText(truncatedText, 12).width > maxTextWidth) {
                truncatedText = truncatedText.substr(0, truncatedText.length() - 1);
            }
            infoText = truncatedText + "...";
            infoSize = renderer.measureText(infoText, 12);
        }

        renderer.drawText(infoText,
                           NUIAbsolutePoint(bounds, headerAvailableWidth - infoSize.width - margin, 15),
                           12, themeManager.getColor("textSecondary"));
    }

    // Custom render order: tracks first, then UI controls on top
    // (Grid is now drawn by individual tracks in TrackUIComponent::drawPlaylistGrid)
    renderChildren(renderer);
    
    // Calculate available width for header (excluding mixer if visible and not maximized)
    float headerWidth = bounds.width;
    if (m_showMixer && m_mixerPanel && !mixerMaximized) {
        float mixerPanelWidth = m_mixerPanel->isMinimized() ? m_mixerPanel->getTitleBarHeight() : m_mixerWidth;
        headerWidth -= mixerPanelWidth;
    }
    
    // Draw header bar on top of everything (30px tall, available width excluding mixer)
    if (m_showPlaylist) {
        NomadUI::NUIColor bgColor = themeManager.getColor("backgroundPrimary");
        NomadUI::NUIColor borderColor = themeManager.getColor("border");

        float headerHeight = 30.0f;
        NomadUI::NUIRect headerRect(bounds.x, bounds.y, headerWidth, headerHeight);
        renderer.fillRect(headerRect, bgColor);
        renderer.strokeRect(headerRect, 1, borderColor);
        
        // Draw time ruler below header and horizontal scrollbar (20px tall)
        float rulerHeight = 20.0f;
        float horizontalScrollbarHeight = 24.0f;
        NomadUI::NUIRect rulerRect(bounds.x, bounds.y + headerHeight + horizontalScrollbarHeight, headerWidth, rulerHeight);
        renderTimeRuler(renderer, rulerRect);
        
        // Render window control icons on header
        // Convert relative bounds to absolute for rendering
        if (m_closeIcon) {
            NomadUI::NUIRect absoluteBounds = NUIAbsolute(bounds, m_closeIconBounds.x, m_closeIconBounds.y, m_closeIconBounds.width, m_closeIconBounds.height);
            m_closeIcon->setBounds(absoluteBounds);
            m_closeIcon->onRender(renderer);
        }
        if (m_maximizeIcon) {
            NomadUI::NUIRect absoluteBounds = NUIAbsolute(bounds, m_maximizeIconBounds.x, m_maximizeIconBounds.y, m_maximizeIconBounds.width, m_maximizeIconBounds.height);
            m_maximizeIcon->setBounds(absoluteBounds);
            m_maximizeIcon->onRender(renderer);
        }
        if (m_minimizeIcon) {
            NomadUI::NUIRect absoluteBounds = NUIAbsolute(bounds, m_minimizeIconBounds.x, m_minimizeIconBounds.y, m_minimizeIconBounds.width, m_minimizeIconBounds.height);
            m_minimizeIcon->setBounds(absoluteBounds);
            m_minimizeIcon->onRender(renderer);
        }
    }
}

void TrackManagerUI::renderChildren(NomadUI::NUIRenderer& renderer) {
    // ðŸ”¥ VIEWPORT CULLING: Only render visible tracks + always render controls
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    NomadUI::NUIRect bounds = getBounds();
    
    float headerHeight = 30.0f;
    float viewportTop = headerHeight;
    float viewportBottom = bounds.height;
    
    // Calculate which tracks are visible considering scroll offset
    int firstVisibleTrack = 0;
    int lastVisibleTrack = static_cast<int>(m_trackUIComponents.size());
    
    if (m_trackHeight > 0) {
        // Account for scroll offset when calculating visible range
        float scrollTop = m_scrollOffset;
        float scrollBottom = m_scrollOffset + (viewportBottom - viewportTop);
        
        firstVisibleTrack = std::max(0, static_cast<int>(scrollTop / (m_trackHeight + m_trackSpacing)));
        lastVisibleTrack = std::min(static_cast<int>(m_trackUIComponents.size()), 
                                     static_cast<int>(scrollBottom / (m_trackHeight + m_trackSpacing)) + 2);
    }
    
    // Render all children but skip track UIComponents that are outside viewport
    const auto& children = getChildren();
    for (const auto& child : children) {
        if (!child->isVisible()) continue;
        
        // Always render UI controls (add button, scrollbar)
        // Icons, piano roll panel, and mixer panel are rendered manually in onRender()
        if (child == m_addTrackButton || child == m_scrollbar || child == m_horizontalScrollbar || 
            child == m_pianoRollPanel || child == m_mixerPanel || child == m_sequencerPanel) {
            // Skip - these are rendered explicitly in onRender()
            continue;
        }
        
        // Check if this child is a track UI component
        bool isTrackUI = false;
        int trackIndex = -1;
        for (size_t i = 0; i < m_trackUIComponents.size(); ++i) {
            if (child == m_trackUIComponents[i]) {
                isTrackUI = true;
                trackIndex = static_cast<int>(i);
                break;
            }
        }
        
        // If it's a track, only render if visible in viewport AND playlist is visible
        if (isTrackUI) {
            if (m_showPlaylist && trackIndex >= firstVisibleTrack && trackIndex < lastVisibleTrack) {
                child->onRender(renderer);
            }
        } else {
            // Not a track UI, render normally (other UI elements)
            child->onRender(renderer);
        }
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
        float buttonX = themeManager.getComponentDimension("trackControls", "buttonStartX");
        float controlAreaWidth = buttonX + layout.controlButtonWidth + 10;
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
        
        updateHorizontalScrollbar();
        m_cacheInvalidated = true;
        setDirty(true);
    }
}

void TrackManagerUI::onResize(int width, int height) {
    // Update cached dimensions before layout/cache update
    m_backgroundCachedWidth = width;
    m_backgroundCachedHeight = height;
    m_backgroundNeedsUpdate = true;
    m_cacheInvalidated = true;  // Invalidate FBO cache on resize
    
    layoutTracks();
    NomadUI::NUIComponent::onResize(width, height);
}

bool TrackManagerUI::onMouseEvent(const NomadUI::NUIMouseEvent& event) {
    // If playlist is hidden, only pass events to children (panels)
    // This ensures that when playlist is toggled off, we don't block interactions with other panels
    if (!m_showPlaylist) {
        return NomadUI::NUIComponent::onMouseEvent(event);
    }

    NomadUI::NUIRect bounds = getBounds();
    NomadUI::NUIPoint localPos(event.position.x - bounds.x, event.position.y - bounds.y);
    
    // Track mouse position for split cursor rendering
    m_lastMousePos = event.position;
    
    // Update toolbar bounds before checking hover (critical!)
    updateToolbarBounds();
    
    // Update toolbar hover states
    bool oldSelectHovered = m_selectToolHovered;
    bool oldSplitHovered = m_splitToolHovered;
    bool oldMultiSelectHovered = m_multiSelectToolHovered;
    bool oldLoopHovered = m_loopToolHovered;
    
    m_selectToolHovered = m_selectToolBounds.contains(event.position);
    m_splitToolHovered = m_splitToolBounds.contains(event.position);
    m_multiSelectToolHovered = m_multiSelectToolBounds.contains(event.position);
    m_loopToolHovered = m_loopToolBounds.contains(event.position);
    
    // Invalidate cache if hover state changed
    if (m_selectToolHovered != oldSelectHovered || m_splitToolHovered != oldSplitHovered ||
        m_multiSelectToolHovered != oldMultiSelectHovered || m_loopToolHovered != oldLoopHovered) {
        m_cacheInvalidated = true;
    }
    
    // Handle toolbar clicks FIRST (highest priority)
    if (event.pressed && event.button == NomadUI::NUIMouseButton::Left) {
        if (handleToolbarClick(event.position)) {
            return true;
        }
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
    
    // === SELECTION BOX: Right-click drag or MultiSelect tool ===
    // Start selection box on right-click drag OR left-click with MultiSelect tool
    bool startSelectionBox = (event.pressed && event.button == NomadUI::NUIMouseButton::Right) ||
                             (event.pressed && event.button == NomadUI::NUIMouseButton::Left && 
                              m_currentTool == PlaylistTool::MultiSelect);
    
    if (startSelectionBox && !m_isDrawingSelectionBox) {
        float headerHeight = 30.0f;
        float rulerHeight = 20.0f;
        float horizontalScrollbarHeight = 24.0f;
        float trackAreaTop = headerHeight + horizontalScrollbarHeight + rulerHeight;
        
        // Only start selection box in track area
        if (localPos.y > trackAreaTop) {
            m_isDrawingSelectionBox = true;
            m_selectionBoxStart = event.position;
            m_selectionBoxEnd = event.position;
            return true;
        }
    }
    
    // Update selection box while dragging
    if (m_isDrawingSelectionBox) {
        m_selectionBoxEnd = event.position;
        
        // Check for release to finalize selection
        bool endSelectionBox = (event.released && event.button == NomadUI::NUIMouseButton::Right) ||
                               (event.released && event.button == NomadUI::NUIMouseButton::Left && 
                                m_currentTool == PlaylistTool::MultiSelect);
        
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
            
            m_isDrawingSelectionBox = false;
            m_cacheInvalidated = true;
            
            Log::info("Selection box completed, selected " + std::to_string(m_selectedTracks.size()) + " tracks");
        }
        
        m_cacheInvalidated = true;
        return true;
    }
    
    // Layout constants
    float headerHeight = 30.0f;
    float rulerHeight = 20.0f;
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
            float totalContentHeight = m_trackUIComponents.size() * (m_trackHeight + m_trackSpacing);
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
    
    // PLAYHEAD SCRUBBING: Click and drag on ruler to scrub playback position
    if (event.pressed && event.button == NomadUI::NUIMouseButton::Left && isInRuler) {
        // Start dragging playhead
        m_isDraggingPlayhead = true;
        if (m_trackManager) {
            m_trackManager->setUserScrubbing(true);
        }
    }
    
    // Handle playhead dragging (continuous scrub)
    if (m_isDraggingPlayhead) {
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
        float buttonX = themeManager.getComponentDimension("trackControls", "buttonStartX");
        float controlAreaWidth = buttonX + layout.controlButtonWidth + 10;
        float gridStartX = controlAreaWidth + 5;
        
        // Mouse position relative to grid start
        float mouseX = localPos.x - gridStartX + m_timelineScrollOffset;
        
        // Convert pixel position to time (seconds)
        double bpm = 120.0;
        double secondsPerBeat = 60.0 / bpm;
        double positionInBeats = mouseX / m_pixelsPerBeat;
        double positionInSeconds = positionInBeats * secondsPerBeat;
        
        // Clamp to valid range (allow negative briefly for smooth feel, but clamp at 0)
        positionInSeconds = std::max(0.0, positionInSeconds);
        
        // Set playback position continuously
        if (m_trackManager) {
            m_trackManager->setPosition(positionInSeconds);
        }
        
        return true;
    }
    
    // (Vertical scroll handling moved to main wheel handler above)
    
    // === SPLIT TOOL: Click to split track at position ===
    if (m_currentTool == PlaylistTool::Split && event.pressed && event.button == NomadUI::NUIMouseButton::Left) {
        // Check if click is in track area
        auto& themeManager = NomadUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        float buttonX = themeManager.getComponentDimension("trackControls", "buttonStartX");
        float controlAreaWidth = buttonX + layout.controlButtonWidth + 10;
        float gridStartX = controlAreaWidth + 5;
        
        float headerHeight = 30.0f;
        float rulerHeight = 20.0f;
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
                // Calculate time position from click X
                float mouseX = localPos.x - gridStartX + m_timelineScrollOffset;
                double bpm = 120.0;
                double secondsPerBeat = 60.0 / bpm;
                double positionInBeats = mouseX / m_pixelsPerBeat;
                double positionInSeconds = positionInBeats * secondsPerBeat;
                
                // Snap to grid if enabled
                if (m_snapEnabled) {
                    positionInSeconds = snapTimeToGrid(positionInSeconds);
                }
                
                // Perform the split
                performSplitAtPosition(trackIndex, positionInSeconds);
                return true;
            }
        }
    }
    
    // Debug log mouse position and icon bounds
    static int logCounter = 0;
    if (++logCounter % 60 == 0) { // Log every 60 events to avoid spam
        Log::info("Mouse: (" + std::to_string(localPos.x) + ", " + std::to_string(localPos.y) + ")");
        Log::info("Close bounds: (" + std::to_string(m_closeIconBounds.x) + ", " + std::to_string(m_closeIconBounds.y) + 
                  ", " + std::to_string(m_closeIconBounds.width) + "x" + std::to_string(m_closeIconBounds.height) + ")");
    }
    
    // Update hover states for icons
    bool wasHovered = m_closeIconHovered || m_minimizeIconHovered || m_maximizeIconHovered;
    m_closeIconHovered = m_closeIconBounds.contains(localPos);
    m_minimizeIconHovered = m_minimizeIconBounds.contains(localPos);
    m_maximizeIconHovered = m_maximizeIconBounds.contains(localPos);
    bool isHovered = m_closeIconHovered || m_minimizeIconHovered || m_maximizeIconHovered;
    
    // Update icon colors based on hover state (and invalidate cache if hover state changed)
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    if (m_closeIconHovered) {
        m_closeIcon->setColor(NomadUI::NUIColor(1.0f, 0.3f, 0.3f, 1.0f)); // Red on hover
    } else {
        m_closeIcon->setColorFromTheme("textPrimary"); // White normally
    }
    
    if (m_minimizeIconHovered) {
        m_minimizeIcon->setColor(NomadUI::NUIColor(1.0f, 1.0f, 1.0f, 1.0f)); // White on hover
    } else {
        m_minimizeIcon->setColorFromTheme("textPrimary");
    }
    
    if (m_maximizeIconHovered) {
        m_maximizeIcon->setColor(NomadUI::NUIColor(1.0f, 1.0f, 1.0f, 1.0f)); // White on hover
    } else {
        m_maximizeIcon->setColorFromTheme("textPrimary");
    }
    
    // Invalidate cache if hover state changed (for icon color updates)
    if (wasHovered != isHovered) {
        m_cacheInvalidated = true;
    }
    
    // Check if icon was clicked
    if (event.pressed && event.button == NomadUI::NUIMouseButton::Left) {
        // Check close icon
        if (m_closeIconBounds.contains(localPos)) {
            setVisible(false);
            m_cacheInvalidated = true;  // Invalidate cache when visibility changes
            Log::info("Playlist closed");
            return true;
        }
        
        // Check minimize icon
        if (m_minimizeIconBounds.contains(localPos)) {
            m_cacheInvalidated = true;  // Invalidate cache for minimize action
            Log::info("Playlist minimized");
            return true;
        }
        
        // Check maximize icon
        if (m_maximizeIconBounds.contains(localPos)) {
            m_cacheInvalidated = true;  // Invalidate cache for maximize action
            Log::info("Playlist maximized");
            return true;
        }
    }
    
    // First, let children handle the event
    bool handled = NomadUI::NUIComponent::onMouseEvent(event);
    
    // If a track was clicked, deselect all other tracks
    if (event.pressed && event.button == NomadUI::NUIMouseButton::Left) {
        for (size_t i = 0; i < m_trackUIComponents.size(); ++i) {
            auto& trackUI = m_trackUIComponents[i];
            if (trackUI->getBounds().contains(event.position)) {
                // This track was clicked - deselect all others
                for (size_t j = 0; j < m_trackUIComponents.size(); ++j) {
                    if (i != j) {
                        m_trackUIComponents[j]->setSelected(false);
                    }
                }
                m_cacheInvalidated = true;  // Invalidate cache when track selection changes
                break;
            }
        }
    }
    
    return handled;
}

void TrackManagerUI::updateScrollbar() {
    if (!m_scrollbar) return;
    
    NomadUI::NUIRect bounds = getBounds();
    float headerHeight = 30.0f;
    float rulerHeight = 20.0f;
    float horizontalScrollbarHeight = 24.0f;
    float viewportHeight = bounds.height - headerHeight - rulerHeight - horizontalScrollbarHeight;
    float totalContentHeight = m_trackUIComponents.size() * (m_trackHeight + m_trackSpacing);
    
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

void TrackManagerUI::updateHorizontalScrollbar() {
    if (!m_horizontalScrollbar) return;
    
    NomadUI::NUIRect bounds = getBounds();
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    float buttonX = themeManager.getComponentDimension("trackControls", "buttonStartX");
    
    // Calculate grid area width
    float scrollbarWidth = 15.0f;
    float trackWidth = bounds.width - scrollbarWidth;
    float gridWidth = trackWidth - (buttonX + layout.controlButtonWidth + 10);
    
    // Dynamic timeline range - based on clip extents with headroom
    float minTimelineWidth = gridWidth * 3.0f;  // Minimum: 3 screens
    double extentSeconds = getMaxTimelineExtent();
    double bpm = 120.0; // TODO: fetch from transport/project
    double secondsPerBeat = 60.0 / bpm;
    double totalBeats = extentSeconds / secondsPerBeat;
    float contentWidth = static_cast<float>(totalBeats * m_pixelsPerBeat);
    float paddedEnd = contentWidth + gridWidth * 0.5f; // 50% headroom
    float totalTimelineWidth = std::max(minTimelineWidth, paddedEnd);

    // Set horizontal scrollbar range
    m_horizontalScrollbar->setRangeLimit(0, totalTimelineWidth);
    m_horizontalScrollbar->setCurrentRange(m_timelineScrollOffset, gridWidth);
    m_horizontalScrollbar->setAutoHide(totalTimelineWidth <= gridWidth);
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
    auto bgColor = themeManager.getColor("backgroundPrimary"); // Match rest of UI
    const auto& layout = themeManager.getLayoutDimensions();
    
    // Draw full ruler background
    renderer.fillRect(rulerBounds, bgColor);
    
    // Calculate grid start EXACTLY like TrackUIComponent
    float buttonX = themeManager.getComponentDimension("trackControls", "buttonStartX");
    float gridStartX = rulerBounds.x + buttonX + layout.controlButtonWidth + 10;
    
    // Calculate gridWidth EXACTLY like TrackUIComponent
    // Track uses: gridWidth = bounds.width - (buttonX + layout.controlButtonWidth + 10)
    // But tracks have scrollbar subtracted from their bounds.width
    float scrollbarWidth = 15.0f;
    float trackWidth = rulerBounds.width - scrollbarWidth; // Match track width (subtract scrollbar)
    float gridWidth = trackWidth - (buttonX + layout.controlButtonWidth + 10);
    
    // Pitch black background for grid area only
    NomadUI::NUIColor rulerBgColor(0.0f, 0.0f, 0.0f, 1.0f);
    NomadUI::NUIRect gridRulerRect(gridStartX, rulerBounds.y, gridWidth, rulerBounds.height);
    renderer.fillRect(gridRulerRect, rulerBgColor);
    
    // Draw border
    renderer.strokeRect(rulerBounds, 1, borderColor);
    
    // NOTE: Scissor clipping currently disabled - coordinate system issue between UI space and OpenGL window space
    // The setClipRect expects window coordinates but we're passing UI component coordinates
    // TODO: Fix coordinate transformation in setClipRect or add a UI-space clip method
    
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
        float textY = rulerBounds.y + (rulerBounds.height - fontSize) * 0.5f;
        
        // Position text to the RIGHT of the grid line with small offset
        float textX = x + 4.0f;
        
        // Only draw text if it won't bleed off the right edge
        // Allow it to appear from the left (even partially) so "1" shows up early
        float textWidth = textSize.width;
        // Only check right edge - allow text to start appearing from left
        if (textX + textWidth <= gridEndX) {
            renderer.drawText(barText, 
                            NomadUI::NUIPoint(textX, textY),
                            fontSize, accentColor.withAlpha(textAlpha));
        }
        
        // Bar tick line - major bars get full height, others half
        float tickStartY = isMajorBar ? rulerBounds.y : rulerBounds.y + rulerBounds.height * 0.4f;
        renderer.drawLine(
            NomadUI::NUIPoint(x, tickStartY),
            NomadUI::NUIPoint(x, rulerBounds.y + rulerBounds.height),
            1.0f,
            isMajorBar ? accentColor : accentColor.withAlpha(0.5f)
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
                    NomadUI::NUIPoint(beatX, rulerBounds.y + rulerBounds.height * 0.6f),
                    NomadUI::NUIPoint(beatX, rulerBounds.y + rulerBounds.height),
                    1.0f,
                    textColor.withAlpha(0.3f)
                );
            }
        }
    }
    
    // NOTE: clearClipRect() disabled since setClipRect is disabled above
}

// Calculate maximum timeline extent needed based on all samples
double TrackManagerUI::getMaxTimelineExtent() const {
    if (!m_trackManager) return 0.0;
    
    double maxExtent = 0.0;
    double bpm = 120.0; // TODO: Get from project
    double secondsPerBeat = 60.0 / bpm;
    
    // Minimum extent - at least 8 bars even if empty
    double minExtent = 8.0 * m_beatsPerBar * secondsPerBeat;
    
    for (size_t i = 0; i < m_trackManager->getTrackCount(); ++i) {
        auto track = m_trackManager->getTrack(i);
        if (track && !track->getAudioData().empty()) {
            double startPos = track->getStartPositionInTimeline();
            double duration = track->getDuration();
            double endPos = startPos + duration;
            
            // Add 2 bars padding after the last sample
            double paddedEnd = endPos + (2.0 * m_beatsPerBar * secondsPerBeat);
            
            if (paddedEnd > maxExtent) {
                maxExtent = paddedEnd;
            }
        }
    }
    
    // Return at least the minimum extent
    return std::max(maxExtent, minExtent);
}

// Draw playhead (vertical line showing current playback position)
void TrackManagerUI::renderPlayhead(NomadUI::NUIRenderer& renderer) {
    if (!m_trackManager) return;
    
    // Get current playback position from track manager
    double currentPosition = m_trackManager->getPosition();
    
    // Convert position (seconds) to pixel position
    double bpm = 120.0;
    double secondsPerBeat = 60.0 / bpm;
    double positionInBeats = currentPosition / secondsPerBeat;
    float positionInPixels = static_cast<float>(positionInBeats * m_pixelsPerBeat);
    
    // Calculate playhead X position accounting for scroll offset
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    float buttonX = themeManager.getComponentDimension("trackControls", "buttonStartX");
    float controlAreaWidth = buttonX + layout.controlButtonWidth + 10;
    
    NomadUI::NUIRect bounds = getBounds();
    float gridStartX = bounds.x + controlAreaWidth + 5;
    float playheadX = gridStartX + positionInPixels - m_timelineScrollOffset;
    
    // Calculate bounds and triangle size for precise culling
    float scrollbarWidth = 15.0f;
    float trackWidth = bounds.width - scrollbarWidth;
    float gridWidth = trackWidth - (buttonX + layout.controlButtonWidth + 10);
    float gridEndX = gridStartX + gridWidth;
    float triangleSize = 6.0f;  // Triangle extends this much left/right from playhead center
    
    // Calculate playhead boundaries (where mixer is visible)
    float headerHeight = 30.0f;
    float horizontalScrollbarHeight = 24.0f;
    float rulerHeight = 20.0f;
    float playheadStartY = bounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;
    
    float pianoRollSpace = 0.0f;
    if (m_showPianoRoll && m_pianoRollPanel) {
        pianoRollSpace = m_pianoRollPanel->isMinimized() ? m_pianoRollPanel->getTitleBarHeight() + 5.0f : m_pianoRollHeight + 5.0f;
    }
    float mixerSpace = m_showMixer ? m_mixerWidth + 5.0f : 0.0f;
    float playheadEndX = m_showMixer ? bounds.x + bounds.width - mixerSpace : bounds.x + bounds.width - scrollbarWidth;
    // Extend playhead to the very bottom of the view
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
        // Playhead color - white and slender for elegance
        NomadUI::NUIColor playheadColor(1.0f, 1.0f, 1.0f, 0.8f);  // White, slightly transparent
        
        // Draw playhead line (1px thick for slender appearance)
        renderer.drawLine(
            NomadUI::NUIPoint(playheadX, playheadStartY),
            NomadUI::NUIPoint(playheadX, playheadEndY),
            1.0f,  // Slender 1px line
            playheadColor
        );
        
        // Draw playhead triangle/flag at top (smaller, more elegant)
        NomadUI::NUIPoint p1(playheadX, playheadStartY);
        NomadUI::NUIPoint p2(playheadX - triangleSize, playheadStartY - triangleSize);
        NomadUI::NUIPoint p3(playheadX + triangleSize, playheadStartY - triangleSize);
        
        // Draw filled triangle (simplified - just three lines forming triangle)
        renderer.drawLine(p1, p2, 1.0f, playheadColor);
        renderer.drawLine(p2, p3, 1.0f, playheadColor);
        renderer.drawLine(p3, p1, 1.0f, playheadColor);
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
    float buttonX = themeManager.getComponentDimension("trackControls", "buttonStartX");
    float controlAreaWidth = buttonX + layout.controlButtonWidth + 10;
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
    renderer.fillRect(gridBg, bgColor);
    
    // Draw borders
    renderer.strokeRect(textureBounds, 1, borderColor);
    
    // Draw header bar
    float headerHeight = 30.0f;
    NomadUI::NUIRect headerRect(0, 0, static_cast<float>(width), headerHeight);
    renderer.fillRect(headerRect, bgColor);
    renderer.strokeRect(headerRect, 1, borderColor);
    
    // Draw time ruler
    float rulerHeight = 20.0f;
    float horizontalScrollbarHeight = 24.0f;
    NomadUI::NUIRect rulerRect(0, headerHeight + horizontalScrollbarHeight, static_cast<float>(width), rulerHeight);
    
    // Render ruler ticks (static part only - no moving elements)
    double bpm = 120.0;
    double secondsPerBeat = 60.0 / bpm;
    double maxExtent = getMaxTimelineExtent();
    double maxExtentInBeats = maxExtent / secondsPerBeat;
    
    // Draw ruler background
    renderer.fillRect(rulerRect, bgColor);
    renderer.strokeRect(rulerRect, 1, borderColor);
    
    // Draw beat markers (grid lines)
    for (int beat = 0; beat <= static_cast<int>(maxExtentInBeats) + 10; ++beat) {
        float xPos = rulerRect.x + gridStartX + (beat * m_pixelsPerBeat) - m_timelineScrollOffset;
        // LENIENT CULLING: Allow 1px bleed for smooth appearance at boundaries
        if (xPos < rulerRect.x + gridStartX - 1.0f || xPos > rulerRect.right() + 1.0f) continue;
        
        NomadUI::NUIColor tickColor = (beat % m_beatsPerBar == 0) ? 
            themeManager.getColor("textPrimary") : 
            themeManager.getColor("textSecondary");
        
        // Draw tick line
        float tickHeight = (beat % m_beatsPerBar == 0) ? rulerHeight * 0.6f : rulerHeight * 0.4f;
        NomadUI::NUIPoint p1(xPos, rulerRect.y + rulerHeight - tickHeight);
        NomadUI::NUIPoint p2(xPos, rulerRect.y + rulerHeight);
        renderer.drawLine(p1, p2, 1.0f, tickColor);
    }

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
            renderer.drawText(barText, NomadUI::NUIPoint(textX, textY), barFontSize, themeManager.getColor("accentPrimary"));
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
    for (auto& cache : m_trackCaches) {
        cache.needsUpdate = true;
    }
}

void TrackManagerUI::invalidateCache() {
    // New FBO caching system - invalidate the main cache
    m_cacheInvalidated = true;
    
    // Also invalidate old multi-layer caches for compatibility
    m_backgroundNeedsUpdate = true;
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
    auto* selectedUI = getSelectedTrackUI();
    if (!selectedUI) {
        Log::warning("No track selected for split");
        return;
    }
    
    auto track = selectedUI->getTrack();
    if (!track || track->getAudioData().empty()) {
        Log::warning("Selected track has no audio to split");
        return;
    }
    
    // Get current playhead position from transport (via content's transport bar)
    // For now, use a position relative to clip start
    double playheadTime = track->getPosition(); // Current playback position
    double clipStart = track->getStartPositionInTimeline();
    double positionInClip = playheadTime - clipStart;
    
    if (positionInClip <= 0 || positionInClip >= track->getDuration()) {
        Log::warning("Playhead not within clip bounds for split");
        return;
    }
    
    // Split the track
    auto newTrack = track->splitAt(positionInClip);
    if (newTrack) {
        // Add new track to manager
        m_trackManager->addExistingTrack(newTrack);
        
        // Refresh UI
        refreshTracks();
        invalidateCache();
        
        Log::info("Clip split at " + std::to_string(positionInClip) + "s");
    }
}

void TrackManagerUI::copySelectedClip() {
    auto* selectedUI = getSelectedTrackUI();
    if (!selectedUI) {
        Log::warning("No track selected for copy");
        return;
    }
    
    auto track = selectedUI->getTrack();
    if (!track || track->getAudioData().empty()) {
        Log::warning("Selected track has no audio to copy");
        return;
    }
    
    // Copy to clipboard
    m_clipboard.hasData = true;
    m_clipboard.audioData = track->getAudioData();
    m_clipboard.sampleRate = track->getSampleRate();
    m_clipboard.numChannels = track->getNumChannels();
    m_clipboard.name = track->getName();
    m_clipboard.trimStart = track->getTrimStart();
    m_clipboard.trimEnd = track->getTrimEnd();
    m_clipboard.sourceColor = track->getColor();
    
    Log::info("Copied clip: " + m_clipboard.name);
}

void TrackManagerUI::cutSelectedClip() {
    auto* selectedUI = getSelectedTrackUI();
    if (!selectedUI) {
        Log::warning("No track selected for cut");
        return;
    }
    
    auto track = selectedUI->getTrack();
    if (!track || track->getAudioData().empty()) {
        Log::warning("Selected track has no audio to cut");
        return;
    }
    
    // Copy to clipboard first
    m_clipboard.hasData = true;
    m_clipboard.audioData = track->getAudioData();
    m_clipboard.sampleRate = track->getSampleRate();
    m_clipboard.numChannels = track->getNumChannels();
    m_clipboard.name = track->getName();
    m_clipboard.trimStart = track->getTrimStart();
    m_clipboard.trimEnd = track->getTrimEnd();
    m_clipboard.sourceColor = track->getColor();
    
    // Now clear the source track
    track->clearAudioData();
    track->setName("");
    
    refreshTracks();
    invalidateCache();
    
    Log::info("Cut clip to clipboard: " + m_clipboard.name);
}

void TrackManagerUI::pasteClip() {
    if (!m_clipboard.hasData) {
        Log::warning("Clipboard is empty");
        return;
    }
    
    // Find first empty track or selected track
    auto* selectedUI = getSelectedTrackUI();
    std::shared_ptr<Track> targetTrack = nullptr;
    
    if (selectedUI && selectedUI->getTrack()->getAudioData().empty()) {
        targetTrack = selectedUI->getTrack();
    } else {
        // Find first empty track
        for (size_t i = 0; i < m_trackManager->getTrackCount(); ++i) {
            auto track = m_trackManager->getTrack(i);
            if (track && track->getAudioData().empty()) {
                targetTrack = track;
                break;
            }
        }
    }
    
    if (!targetTrack) {
        Log::warning("No empty track available for paste");
        return;
    }
    
    // Paste the audio data
    uint32_t totalSamples = static_cast<uint32_t>(m_clipboard.audioData.size() / m_clipboard.numChannels);
    uint32_t targetSR = static_cast<uint32_t>(m_trackManager ? m_trackManager->getOutputSampleRate() : m_clipboard.sampleRate);
    targetTrack->setAudioData(m_clipboard.audioData.data(), totalSamples,
                              m_clipboard.sampleRate, m_clipboard.numChannels, targetSR);
    targetTrack->setName(m_clipboard.name);  // Keep original name, no suffix
    targetTrack->setColor(m_clipboard.sourceColor);
    targetTrack->setTrimStart(m_clipboard.trimStart);
    if (m_clipboard.trimEnd >= 0) {
        targetTrack->setTrimEnd(m_clipboard.trimEnd);
    }
    
    // Set position at playhead (or start if no playhead)
    targetTrack->setStartPositionInTimeline(0.0); // TODO: Get actual playhead position
    
    refreshTracks();
    invalidateCache();
    
    Log::info("Pasted clip to track: " + targetTrack->getName());
}

void TrackManagerUI::duplicateSelectedClip() {
    auto* selectedUI = getSelectedTrackUI();
    if (!selectedUI) {
        Log::warning("No track selected for duplicate");
        return;
    }
    
    auto track = selectedUI->getTrack();
    if (!track || track->getAudioData().empty()) {
        Log::warning("Selected track has no audio to duplicate");
        return;
    }
    
    // Create duplicate
    auto duplicatedTrack = track->duplicate();
    if (duplicatedTrack) {
        // Position after original clip
        double originalEnd = track->getStartPositionInTimeline() + track->getDuration();
        duplicatedTrack->setStartPositionInTimeline(originalEnd);
        
        // Add to manager
        m_trackManager->addExistingTrack(duplicatedTrack);
        
        refreshTracks();
        invalidateCache();
        
        Log::info("Duplicated clip: " + track->getName());
    }
}

void TrackManagerUI::deleteSelectedClip() {
    auto* selectedUI = getSelectedTrackUI();
    if (!selectedUI) {
        Log::warning("No track selected for delete");
        return;
    }
    
    auto track = selectedUI->getTrack();
    if (!track || track->getAudioData().empty()) {
        return; // Already empty
    }
    
    // Clear the audio data
    track->clearAudioData();
    
    refreshTracks();
    invalidateCache();
    
    Log::info("Deleted clip from track");
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
    // Update target track and time as mouse moves
    int newTrack = getTrackAtPosition(position.y);
    double rawTime = getTimeAtPosition(position.x);
    double newTime = snapTimeToGrid(rawTime);  // Apply snap to preview
    
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
    
    // Final position calculation with snap-to-grid
    int trackIndex = getTrackAtPosition(position.y);
    double rawTime = std::max(0.0, getTimeAtPosition(position.x));
    double timePosition = snapTimeToGrid(rawTime);
    
    int trackCount = static_cast<int>(m_trackManager->getTrackCount());
    
    // If dragging below last track, target the next available slot
    if (trackIndex >= trackCount) {
        trackIndex = trackCount;
    }
    
    Log::info("[TrackManagerUI] Drop at track " + std::to_string(trackIndex) + 
              ", time " + std::to_string(timePosition) + "s" +
              (m_snapEnabled ? " (snapped)" : ""));
    
    if (trackIndex < 0 || trackIndex > trackCount) {
        result.accepted = false;
        result.message = "Invalid track position";
        clearDropPreview();
        return result;
    }
    
    // Handle audio clip move (existing clip being repositioned)
    if (data.type == NomadUI::DragDataType::AudioClip) {
        // Find the source track
        auto sourceTrack = m_trackManager->getTrack(data.sourceTrackIndex);
        
        // If dropping on a new track slot, create it first
        std::shared_ptr<Track> targetTrack;
        if (trackIndex == trackCount) {
            addTrack("Track " + std::to_string(trackCount + 1));
            targetTrack = m_trackManager->getTrack(trackIndex);
        } else {
            targetTrack = m_trackManager->getTrack(trackIndex);
        }
        
        if (!sourceTrack || !targetTrack) {
            result.accepted = false;
            result.message = "Track not found";
            clearDropPreview();
            return result;
        }
        
        // Same track - just reposition the clip
        if (data.sourceTrackIndex == trackIndex) {
            sourceTrack->setStartPositionInTimeline(timePosition);
            result.accepted = true;
            result.targetTrackIndex = trackIndex;
            result.targetTimePosition = timePosition;
            result.message = "Clip moved to " + std::to_string(timePosition) + "s";
            
            Log::info("[TrackManagerUI] Clip repositioned within track " + sourceTrack->getName() + 
                      " to " + std::to_string(timePosition) + "s");
        }
        // Different track - move clip data to new track
        else {
            // Copy audio data to target track
            const auto& audioData = sourceTrack->getAudioData();
            if (!audioData.empty()) {
                // Get source audio properties
                uint32_t sampleRate = sourceTrack->getSampleRate();
                uint32_t numChannels = sourceTrack->getNumChannels();
                
                // Set audio data on target track
                uint32_t targetSR = static_cast<uint32_t>(m_trackManager ? m_trackManager->getOutputSampleRate() : sampleRate);
                targetTrack->setAudioData(audioData.data(), 
                                          static_cast<uint32_t>(audioData.size() / numChannels),
                                          sampleRate, numChannels, targetSR);
                targetTrack->setStartPositionInTimeline(timePosition);
                targetTrack->setSourcePath(sourceTrack->getSourcePath());
                targetTrack->setColor(sourceTrack->getColor());
                
                // Clear source track (leaves empty track row)
                sourceTrack->clearAudioData();
                
                result.accepted = true;
                result.targetTrackIndex = trackIndex;
                result.targetTimePosition = timePosition;
                result.message = "Clip moved to track " + std::to_string(trackIndex + 1);
                
                Log::info("[TrackManagerUI] Clip moved from track " + std::to_string(data.sourceTrackIndex) + 
                          " to track " + std::to_string(trackIndex) + " at " + std::to_string(timePosition) + "s");
            } else {
                result.accepted = false;
                result.message = "Source clip has no audio data";
            }
        }
        
        // Refresh the UI
        refreshTracks();
        invalidateCache();
        clearDropPreview();
        return result;
    }
    
    // Handle file drop (new file being loaded)
    if (data.type == NomadUI::DragDataType::File) {
        // Validate the audio file before loading
        if (!AudioFileValidator::isValidAudioFile(data.filePath)) {
            result.accepted = false;
            std::string fileType = AudioFileValidator::getAudioFileType(data.filePath);
            if (fileType == "Unknown") {
                result.message = "Unsupported file format. Supported: WAV, MP3, FLAC, OGG, AIFF";
            } else {
                result.message = "Invalid or corrupted " + fileType + " file";
            }
            Log::warning("[TrackManagerUI] File validation failed: " + data.filePath);
            clearDropPreview();
            return result;
        }
        
        // If dropping on a new track slot, create it first
        std::shared_ptr<Track> track;
        if (trackIndex == trackCount) {
            addTrack("Track " + std::to_string(trackCount + 1));
            track = m_trackManager->getTrack(trackIndex);
        } else {
            track = m_trackManager->getTrack(trackIndex);
        }

        if (!track) {
            result.accepted = false;
            result.message = "Track not found";
            clearDropPreview();
            return result;
        }
        
        // Load the audio file into the track
        bool loaded = track->loadAudioFile(data.filePath);
        
        if (loaded) {
            // Set the clip's position in the timeline
            track->setPosition(0.0);  // Reset internal playback position
            track->setStartPositionInTimeline(timePosition);
            
            result.accepted = true;
            result.targetTrackIndex = trackIndex;
            result.targetTimePosition = timePosition;
            result.message = "Sample loaded: " + data.displayName;
            
            Log::info("[TrackManagerUI] Sample loaded into track " + track->getName() + 
                      " at " + std::to_string(timePosition) + "s");
            
            // Refresh the UI to show the new clip
            refreshTracks();
            invalidateCache();
        } else {
            result.accepted = false;
            result.message = "Failed to load audio file";
            Log::error("[TrackManagerUI] Failed to load: " + data.filePath);
        }
        
        clearDropPreview();
        return result;
    }
    
    result.accepted = false;
    result.message = "Unsupported drag type";
    clearDropPreview();
    return result;
}

void TrackManagerUI::clearDropPreview() {
    m_showDropPreview = false;
    m_dropTargetTrack = -1;
    m_dropTargetTime = 0.0;
}

double TrackManagerUI::snapTimeToGrid(double timeInSeconds) const {
    if (!m_snapEnabled) {
        return timeInSeconds;
    }
    
    // Convert time to beats
    double bpm = 120.0;  // TODO: Get actual BPM from transport
    double beatsPerSecond = bpm / 60.0;
    double timeInBeats = timeInSeconds * beatsPerSecond;
    
    // Calculate snap grid size in beats
    // m_snapDivision: 1=bars, 4=beats, 8=8th notes, 16=16th notes
    double snapGridBeats = static_cast<double>(m_beatsPerBar) / m_snapDivision;
    
    // Round to nearest grid line
    double snappedBeats = std::round(timeInBeats / snapGridBeats) * snapGridBeats;
    
    // Convert back to seconds
    double snappedTime = snappedBeats / beatsPerSecond;
    
    return std::max(0.0, snappedTime);
}

// =============================================================================
// Helper Methods for Drop Target
// =============================================================================

int TrackManagerUI::getTrackAtPosition(float y) const {
    NomadUI::NUIRect bounds = getBounds();
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    
    // Get ruler height and track area start
    // MUST match renderTrackManagerDirect layout exactly:
    // header(30) + horizontalScrollbar(15) + ruler(20) = 65
    float headerHeight = 30.0f;
    float horizontalScrollbarHeight = 24.0f;
    float rulerHeight = 20.0f;
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
    float buttonX = themeManager.getComponentDimension("trackControls", "buttonStartX");
    float controlAreaWidth = buttonX + themeManager.getLayoutDimensions().controlButtonWidth + 10;
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
    float buttonX = themeManager.getComponentDimension("trackControls", "buttonStartX");
    float controlAreaWidth = buttonX + themeManager.getLayoutDimensions().controlButtonWidth + 10;
    float gridStartX = bounds.x + controlAreaWidth + 5;
    
    // Calculate track Y position - MUST match layoutTracks() calculation exactly
    // layoutTracks uses: headerHeight(30) + horizontalScrollbarHeight(15) + rulerHeight(20) = 65
    float headerHeight = 30.0f;
    float horizontalScrollbarHeight = 24.0f;
    float rulerHeight = 20.0f;
    float trackAreaStartY = bounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;
    float trackY = trackAreaStartY + (m_dropTargetTrack * (m_trackHeight + m_trackSpacing)) - m_scrollOffset;
    
    // Calculate X position from time
    double bpm = 120.0;
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

} // namespace Audio
} // namespace Nomad
