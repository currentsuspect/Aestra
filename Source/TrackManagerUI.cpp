// © 2025 Nomad Studios All Rights Reserved. Licensed for personal & educational use only.
#include "TrackManagerUI.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "../NomadCore/include/NomadLog.h"

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
    m_horizontalScrollbar->setOnScroll([this](double position) {
        onHorizontalScroll(position);
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
}

TrackManagerUI::~TrackManagerUI() {
    // ⚡ Texture cleanup handled by renderer shutdown
    // Note: NUIRenderer is not a singleton, so manual texture cleanup in destructor
    // is not feasible. The renderer will clean up all textures on shutdown.
    // Texture IDs: m_backgroundTextureId, m_controlsTextureId, m_trackCaches[].textureId
    // are managed by the renderer lifecycle and don't need explicit deletion here.
    Log::info("TrackManagerUI destroyed");
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
    if (m_addTrackButton) {
        float buttonSize = 30.0f;
        m_addTrackButton->setBounds(NUIAbsolute(bounds, 0.0f, 0.0f, buttonSize, buttonSize));
    }
    
    // Calculate total content height
    float rulerHeight = 20.0f; // Time ruler height
    float horizontalScrollbarHeight = 15.0f;
    float totalContentHeight = m_trackUIComponents.size() * (m_trackHeight + m_trackSpacing);
    
    // If a panel is maximized, it takes over the ENTIRE area (including title bar)
    if (pianoRollMaximized || mixerMaximized) {
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
        } else if (mixerMaximized) {
            m_mixerPanel->setBounds(NUIAbsolute(bounds, 0, 0, bounds.width, bounds.height));
            m_mixerPanel->setVisible(true);
            m_mixerPanel->onResize(static_cast<int>(bounds.width), static_cast<int>(bounds.height));
            
            // Hide other panels when one is maximized
            if (m_pianoRollPanel) m_pianoRollPanel->setVisible(false);
        }
        
        return; // Skip normal layout
    }
    
    // Normal layout (no maximized panels)
    // Show track controls
    if (m_addTrackButton) m_addTrackButton->setVisible(true);
    if (m_scrollbar) m_scrollbar->setVisible(true);
    if (m_horizontalScrollbar) m_horizontalScrollbar->setVisible(true);
    
    // Show all tracks
    for (auto& trackUI : m_trackUIComponents) {
        trackUI->setVisible(true);
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
    
    float viewportHeight = bounds.height - headerHeight - horizontalScrollbarHeight - rulerHeight - pianoRollSpace;
    
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

    // Layout track UI components (reserve space for mixer on right)
    for (size_t i = 0; i < m_trackUIComponents.size(); ++i) {
        auto trackUI = m_trackUIComponents[i];
        float trackWidth = bounds.width - scrollbarWidth - mixerSpace; // Account for scrollbar and mixer
        
        trackUI->setBounds(NUIAbsolute(bounds, 0, currentY, trackWidth, m_trackHeight));
        currentY += m_trackHeight + m_trackSpacing;
    }
    
    // Layout piano roll panel at bottom (full width - independent of mixer)
    if (m_pianoRollPanel && m_showPianoRoll) {
        float panelWidth = bounds.width;  // Full width, not constrained by mixer
        float panelHeight = m_pianoRollPanel->isMinimized() ? m_pianoRollPanel->getTitleBarHeight() : m_pianoRollHeight;
        float pianoY = bounds.height - panelHeight;
        
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
    bool mixerMaximized = m_showMixer && m_mixerPanel && m_mixerPanel->isMaximized();
    
    if (pianoRollMaximized && m_pianoRollPanel) {
        m_pianoRollPanel->onRender(renderer);
        return;
    }
    
    if (mixerMaximized && m_mixerPanel) {
        m_mixerPanel->onRender(renderer);
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
    
    // Render scrollbars OUTSIDE cache (they interact with mouse)
    if (m_addTrackButton && m_addTrackButton->isVisible()) m_addTrackButton->onRender(renderer);
    if (m_horizontalScrollbar && m_horizontalScrollbar->isVisible()) m_horizontalScrollbar->onRender(renderer);
    if (m_scrollbar && m_scrollbar->isVisible()) m_scrollbar->onRender(renderer);
    
    // Render panels OUTSIDE cache (they are dynamic/interactive)
    if (m_pianoRollPanel && m_showPianoRoll && m_pianoRollPanel->isVisible()) {
        m_pianoRollPanel->onRender(renderer);
    }
    if (m_mixerPanel && m_showMixer && m_mixerPanel->isVisible()) {
        m_mixerPanel->onRender(renderer);
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
    
    // Update scrollbar range dynamically
    updateHorizontalScrollbar();

    // Calculate available width for header elements (excluding mixer if visible)
    float headerAvailableWidth = bounds.width;
    if (m_showMixer && m_mixerPanel && !mixerMaximized) {
        float mixerPanelWidth = m_mixerPanel->isMinimized() ? m_mixerPanel->getTitleBarHeight() : m_mixerWidth;
        headerAvailableWidth -= mixerPanelWidth;
    }
    
    // Draw track count - positioned in top-right corner of available space with proper margin
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
    float headerHeight = 30.0f;
    NomadUI::NUIRect headerRect(bounds.x, bounds.y, headerWidth, headerHeight);
    renderer.fillRect(headerRect, bgColor);
    renderer.strokeRect(headerRect, 1, borderColor);
    
    // Draw time ruler below header and horizontal scrollbar (20px tall)
    float rulerHeight = 20.0f;
    float horizontalScrollbarHeight = 15.0f;
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
            child == m_pianoRollPanel || child == m_mixerPanel) {
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
        
        // If it's a track, only render if visible in viewport
        if (isTrackUI) {
            if (trackIndex >= firstVisibleTrack && trackIndex < lastVisibleTrack) {
                child->onRender(renderer);
            }
        } else {
            // Not a track UI, render normally (other UI elements)
            child->onRender(renderer);
        }
    }
}

void TrackManagerUI::onResize(int width, int height) {
    // Update cached dimensions before layout/cache update
    m_backgroundCachedWidth = width;
    m_backgroundCachedHeight = height;
    m_backgroundNeedsUpdate = true;
    
    layoutTracks();
    NomadUI::NUIComponent::onResize(width, height);
}

bool TrackManagerUI::onMouseEvent(const NomadUI::NUIMouseEvent& event) {
    NomadUI::NUIRect bounds = getBounds();
    NomadUI::NUIPoint localPos(event.position.x - bounds.x, event.position.y - bounds.y);
    
    // Check for mouse wheel zoom on ruler
    float headerHeight = 30.0f;
    float rulerHeight = 20.0f;
    float horizontalScrollbarHeight = 15.0f;
    NomadUI::NUIRect rulerRect(0, headerHeight + horizontalScrollbarHeight, bounds.width, rulerHeight);
    
    if (event.wheelDelta != 0.0f && rulerRect.contains(localPos)) {
        // Zoom in/out with mouse wheel
        float oldZoom = m_pixelsPerBeat;
        float zoomDelta = event.wheelDelta > 0 ? 1.2f : 0.8f; // 20% zoom steps
        m_pixelsPerBeat *= zoomDelta;
        
        // Clamp zoom level (min 10 pixels per beat, max 200 pixels per beat)
        m_pixelsPerBeat = std::max(10.0f, std::min(200.0f, m_pixelsPerBeat));
        
        // Sync zoom to all tracks
        for (auto& trackUI : m_trackUIComponents) {
            trackUI->setPixelsPerBeat(m_pixelsPerBeat);
            trackUI->setBeatsPerBar(m_beatsPerBar);
        }
        
        // Update scrollbar range for new zoom level
        updateHorizontalScrollbar();
        
        // No clamping needed - infinite timeline with culling handles everything
        
        // Sync scroll offset to all tracks
        for (auto& trackUI : m_trackUIComponents) {
            trackUI->setTimelineScrollOffset(m_timelineScrollOffset);
        }
        
        m_cacheInvalidated = true;  // Invalidate cache when zoom changes
        Log::info("Zoom: " + std::to_string(m_pixelsPerBeat) + " pixels per beat");
        return true;
    }
    
    // PLAYHEAD SCRUBBING: Click and drag on ruler to scrub playback position
    if (event.pressed && event.button == NomadUI::NUIMouseButton::Left && rulerRect.contains(localPos)) {
        // Start dragging playhead
        m_isDraggingPlayhead = true;
    }
    
    // Handle playhead dragging (continuous scrub)
    if (m_isDraggingPlayhead) {
        // Stop dragging on mouse release
        if (event.released && event.button == NomadUI::NUIMouseButton::Left) {
            m_isDraggingPlayhead = false;
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
    
    // Mouse wheel vertical scroll on track grid area
    float trackAreaY = headerHeight + horizontalScrollbarHeight + rulerHeight;
    NomadUI::NUIRect trackAreaRect(0, trackAreaY, bounds.width, bounds.height - trackAreaY);
    
    if (event.wheelDelta != 0.0f && trackAreaRect.contains(localPos)) {
        // Vertical scrolling
        float scrollSpeed = 60.0f; // pixels per wheel notch
        float scrollDelta = -event.wheelDelta * scrollSpeed;
        
        m_scrollOffset += scrollDelta;
        
        // Clamp scroll offset
        float viewportHeight = bounds.height - headerHeight - rulerHeight - horizontalScrollbarHeight;
        float totalContentHeight = m_trackUIComponents.size() * (m_trackHeight + m_trackSpacing);
        float maxScroll = std::max(0.0f, totalContentHeight - viewportHeight);
        m_scrollOffset = std::max(0.0f, std::min(m_scrollOffset, maxScroll));
        
        // Update scrollbar
        if (m_scrollbar) {
            m_scrollbar->setCurrentRange(m_scrollOffset, viewportHeight);
        }
        
        layoutTracks(); // Re-layout tracks
        m_cacheInvalidated = true;  // Invalidate cache when vertical scroll changes
        return true;
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
    float horizontalScrollbarHeight = 15.0f;
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
    
    // Dynamic timeline range - grows with scroll position for unbounded timeline
    // Always show at least 3 screens worth of content, expanding as needed
    float minTimelineWidth = gridWidth * 3.0f;  // Minimum: 3 screens
    float currentViewEnd = m_timelineScrollOffset + gridWidth;
    float totalTimelineWidth = std::max(minTimelineWidth, currentViewEnd * 1.5f);  // 50% padding beyond current position
    
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
    
    // Add padding to prevent drawing at the very edges (software clipping)
    float textRightPadding = 10.0f;   // Strict right clipping for text
    float textRightEdge = gridStartX + gridWidth - textRightPadding;
    
    // Asymmetric culling: tight on left (prevent bleeding), generous on right (smooth scrolling)
    float leftPadding = 0.5f;  // Tight - prevent bleeding into controls (adjust to taste: 0.5-5.0)
    float rightPadding = pixelsPerBar;  // Generous - smooth scrolling fade
    
    // Draw vertical ticks - dynamically based on visible bars and scroll offset
    for (int bar = startBar; bar <= endBar; ++bar) {
        // Calculate x position accounting for scroll offset
        float x = gridStartX + (bar * pixelsPerBar) - m_timelineScrollOffset;
        
        // Asymmetric culling - tight on left, generous on right
        if (x < gridStartX - leftPadding || x > gridStartX + gridWidth + rightPadding) {
            continue;
        }
        
        // Bar number (1-based)
        std::string barText = std::to_string(bar + 1);
        
        // Use timer-style vertical centering (accounts for baseline positioning)
        float fontSize = 10.0f;
        float textY = rulerBounds.y + (rulerBounds.height - fontSize) / 2.0f + fontSize * 0.75f;
        
        // Only draw text if it won't bleed off the right edge
        // Allow it to appear from the left (even partially) so "1" shows up early
        float textX = x + 2;
        float textWidth = fontSize * barText.length() * 0.6f; // Rough estimate
        // Only check right edge - allow text to start appearing from left
        if (textX + textWidth <= textRightEdge) {
            renderer.drawText(barText, 
                            NomadUI::NUIPoint(textX, textY),
                            fontSize, accentColor);
        }
        
        // Bar tick line
        renderer.drawLine(
            NomadUI::NUIPoint(x, rulerBounds.y),
            NomadUI::NUIPoint(x, rulerBounds.y + rulerBounds.height),
            1.0f,
            accentColor
        );
        
        // Beat ticks within the bar
        for (int beat = 1; beat < beatsPerBar; ++beat) {
            float beatX = x + (beat * m_pixelsPerBeat);
            
            // Same asymmetric padding for beat lines
            if (beatX < gridStartX - leftPadding || beatX > gridStartX + gridWidth + rightPadding) {
                continue;
            }
            
            
            renderer.drawLine(
                NomadUI::NUIPoint(beatX, rulerBounds.y + rulerBounds.height / 2),
                NomadUI::NUIPoint(beatX, rulerBounds.y + rulerBounds.height),
                1.0f,
                textColor.withAlpha(0.3f)
            );
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
    float horizontalScrollbarHeight = 15.0f;
    float rulerHeight = 20.0f;
    float playheadStartY = bounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;
    
    float pianoRollSpace = 0.0f;
    if (m_showPianoRoll && m_pianoRollPanel) {
        pianoRollSpace = m_pianoRollPanel->isMinimized() ? m_pianoRollPanel->getTitleBarHeight() + 5.0f : m_pianoRollHeight + 5.0f;
    }
    float mixerSpace = m_showMixer ? m_mixerWidth + 5.0f : 0.0f;
    float playheadEndX = m_showMixer ? bounds.x + bounds.width - mixerSpace : bounds.x + bounds.width - scrollbarWidth;
    float playheadEndY = bounds.y + bounds.height - scrollbarWidth - pianoRollSpace;
    
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
    float horizontalScrollbarHeight = 15.0f;
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

} // namespace Audio
} // namespace Nomad

