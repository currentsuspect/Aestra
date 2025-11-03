// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../NomadAudio/include/TrackManager.h"
#include "TrackUIComponent.h"
#include "PianoRollPanel.h"
#include "MixerPanel.h"
#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Core/NUIScrollbar.h"
#include "../NomadUI/Core/NUIButton.h"
#include "../NomadUI/Core/NUIIcon.h"
#include <memory>
#include <vector>

namespace Nomad {
namespace Audio {

/**
 * @brief UI wrapper for TrackManager
 *
 * Provides visual track management interface with:
 * - Track layout and scrolling
 * - Add/remove track functionality
 * - Visual timeline integration
 */
class TrackManagerUI : public NomadUI::NUIComponent {
public:
    TrackManagerUI(std::shared_ptr<TrackManager> trackManager);
    ~TrackManagerUI() override;

    std::shared_ptr<TrackManager> getTrackManager() const { return m_trackManager; }

    // Track Management
    void addTrack(const std::string& name = "");
    void refreshTracks();
    
    // Solo coordination (exclusive solo behavior)
    void onTrackSoloToggled(TrackUIComponent* soloedTrack);
    
    // Piano Roll Panel
    void togglePianoRoll();  // Show/hide piano roll panel
    
    // Mixer Panel
    void toggleMixer();  // Show/hide mixer panel

protected:
    void onRender(NomadUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const NomadUI::NUIMouseEvent& event) override;
    
    // ðŸ”¥ VIEWPORT CULLING: Override to only render visible tracks
    void renderChildren(NomadUI::NUIRenderer& renderer);

private:
    std::shared_ptr<TrackManager> m_trackManager;
    std::vector<std::shared_ptr<TrackUIComponent>> m_trackUIComponents;

    // UI Layout
    int m_trackHeight{80};
    int m_trackSpacing{5};
    float m_scrollOffset{0.0f};
    
    // Timeline/Ruler settings
    float m_pixelsPerBeat{50.0f};      // Horizontal zoom level
    float m_timelineScrollOffset{0.0f}; // Horizontal scroll position
    int m_beatsPerBar{4};               // Time signature numerator
    int m_subdivision{4};               // Grid subdivision (4 = 16th notes)
    
    // UI Components
    std::shared_ptr<NomadUI::NUIScrollbar> m_scrollbar;
    std::shared_ptr<NomadUI::NUIScrollbar> m_horizontalScrollbar;
    std::shared_ptr<NomadUI::NUIButton> m_addTrackButton;
    std::shared_ptr<NomadUI::NUIIcon> m_closeIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_minimizeIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_maximizeIcon;
    NomadUI::NUIRect m_closeIconBounds;
    NomadUI::NUIRect m_minimizeIconBounds;
    NomadUI::NUIRect m_maximizeIconBounds;
    
    // Hover states for icons
    bool m_closeIconHovered = false;
    bool m_minimizeIconHovered = false;
    bool m_maximizeIconHovered = false;
    
    // Playhead dragging state
    bool m_isDraggingPlayhead = false;

        // Piano Roll Panel (can dock at bottom or maximize to full view)
    std::shared_ptr<PianoRollPanel> m_pianoRollPanel;
    bool m_showPianoRoll{false};  // Hidden by default
    float m_pianoRollHeight{300.0f};  // Default height when docked
    
    // Mixer Panel (can dock on right or maximize to full view)
    std::shared_ptr<MixerPanel> m_mixerPanel;
    bool m_showMixer{false};  // Hidden by default
    float m_mixerWidth{400.0f};  // Default width when docked

    // ⚡ MULTI-LAYER CACHING SYSTEM for 60+ FPS
    
    // Layer 1: Static Background (grid, ruler ticks)
    uint32_t m_backgroundTextureId = 0;
    int m_backgroundCachedWidth = 0;
    int m_backgroundCachedHeight = 0;
    float m_backgroundCachedZoom = 0.0f;
    bool m_backgroundNeedsUpdate = true;
    
    // Layer 2: Track Controls (buttons, labels - semi-static)
    uint32_t m_controlsTextureId = 0;
    bool m_controlsNeedsUpdate = true;
    
    // Layer 3: Waveforms (per-track FBO caching)
    struct TrackCache {
        uint32_t textureId = 0;
        bool needsUpdate = true;
        double lastContentHash = 0; // Simple hash to detect content changes
    };
    std::vector<TrackCache> m_trackCaches;
    
    // Dirty flags for smart invalidation
    bool m_playheadMoved = false;        // Only redraw playhead overlay
    bool m_metersChanged = false;        // Only redraw audio meters
    
    void updateBackgroundCache(NomadUI::NUIRenderer& renderer);
    void updateControlsCache(NomadUI::NUIRenderer& renderer);
    void updateTrackCache(NomadUI::NUIRenderer& renderer, size_t trackIndex);
    void invalidateAllCaches();
    void invalidateCache(); // Keep for compatibility

    void layoutTracks();
    void onAddTrackClicked();
    void updateTrackPositions();
    void updateScrollbar();
    void updateHorizontalScrollbar();
    void onScroll(double position);
    void onHorizontalScroll(double position);
    void deselectAllTracks();
    void renderTimeRuler(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& rulerBounds);
    void renderPlayhead(NomadUI::NUIRenderer& renderer);
    
    // Calculate maximum timeline extent based on samples
    double getMaxTimelineExtent() const;
};

} // namespace Audio
} // namespace Nomad
