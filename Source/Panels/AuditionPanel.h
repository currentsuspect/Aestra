// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

/**
 * @file AuditionPanel.h
 * @brief Main UI panel for Audition Mode - the "Spotify for musicians" experience
 * 
 * ARCHITECTURE OVERVIEW:
 * ----------------------
 * This panel is the visual layer for Audition Mode. It follows NOMAD's component pattern:
 * 
 * 1. AuditionPanel (this file) - The UI container
 *    ├── Waveform display (top area)
 *    ├── Track info (title, artist)
 *    ├── Progress bar with seek
 *    ├── Transport controls (play/pause/next/prev)
 *    ├── Queue list (drag-to-reorder)
 *    └── DSP preset selector
 * 
 * 2. AuditionEngine (NomadAudio) - The audio backend
 *    - Handles actual playback
 *    - Manages queue state
 *    - Applies DSP chain
 * 
 * The panel OBSERVES the engine using callbacks and SENDS commands via method calls.
 * This separation keeps the UI responsive (audio never blocks).
 */

#include "../NomadUI/Core/NUIComponent.h"
#include "NUILabel.h"
#include "NUIButton.h"   // Buttons are in Widgets/
#include "NUISlider.h"      // Slider is in Core/
#include "AuditionEngine.h"
#include "../NomadUI/Core/NUIDragDrop.h"    // Added for drag and drop support
#include "../NomadUI/Graphics/NUISVGParser.h" // For SVG Icons

#include <memory>
#include <functional>
#include <vector>

namespace Nomad {

/**
 * @brief Audition Mode panel - immersive listening experience
 * 
 * Design goals:
 * - "Psychologically out of the DAW" - no mixer, no meters
 * - Spotify-like aesthetic with dark theme
 * - Drag files from browser → queue
 * - Quick A/B comparison with DSP presets
 */
class AuditionPanel : public NomadUI::NUIComponent, public NomadUI::IDropTarget {
public:
    explicit AuditionPanel(std::shared_ptr<Audio::AuditionEngine> engine);
    ~AuditionPanel() override;
    
    // ===== NUIComponent Overrides =====
    // Note: Signatures must exactly match base class
    
    void onRender(NomadUI::NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;  // Base uses double, not float
    void onResize(int width, int height) override;
    bool onMouseEvent(const NomadUI::NUIMouseEvent& event) override;
    bool onKeyEvent(const NomadUI::NUIKeyEvent& event) override;
    
    // ===== IDropTarget Implementation =====
    NomadUI::DropFeedback onDragEnter(const NomadUI::DragData& data, const NomadUI::NUIPoint& position) override;
    NomadUI::DropFeedback onDragOver(const NomadUI::DragData& data, const NomadUI::NUIPoint& position) override;
    void onDragLeave() override;
    NomadUI::DropResult onDrop(const NomadUI::DragData& data, const NomadUI::NUIPoint& position) override;
    NomadUI::NUIRect getDropBounds() const override;
    
    // ===== Queue Management =====
    
    /// Add a file to the queue (called when user drags from file browser)
    void addFileToQueue(const std::string& filePath, bool isReference = false);
    
    /// Add a timeline track to the queue
    void addTimelineTrack(uint32_t trackId, const std::string& trackName);
    
    // ===== Drop Zone =====
    
    /// Check if point is within the queue drop zone
    bool isInDropZone(float x, float y) const;

private:
    // ===== Child Components =====
    
    // Track info
    std::shared_ptr<NomadUI::NUILabel> m_trackTitle;
    std::shared_ptr<NomadUI::NUILabel> m_trackArtist;
    
    // Transport
    std::shared_ptr<NomadUI::NUIButton> m_playPauseButton;
    std::shared_ptr<NomadUI::NUIButton> m_prevButton;
    std::shared_ptr<NomadUI::NUIButton> m_nextButton;
    std::shared_ptr<NomadUI::NUIButton> m_shuffleButton;
    std::shared_ptr<NomadUI::NUIButton> m_repeatButton;
    
    // Progress/Seek
    std::shared_ptr<NomadUI::NUISlider> m_progressSlider;
    std::shared_ptr<NomadUI::NUILabel> m_currentTime;
    std::shared_ptr<NomadUI::NUILabel> m_totalTime;
    std::shared_ptr<NomadUI::NUISlider> m_volumeSlider;
    
    // DSP
    std::shared_ptr<NomadUI::NUIButton> m_dspPresetButton;
    std::shared_ptr<NomadUI::NUIButton> m_abToggleButton;
    
    // Engine Reference
    std::shared_ptr<Audio::AuditionEngine> m_engine;
    
    // Waveform rendering
    std::vector<float> m_waveformData;  // Cached waveform peaks for current track
    float m_waveformZoom{1.0f};
    
    // State
    float m_animationTime{0.0f};
    bool m_isDraggingSeekbar{false};
    bool m_isHoveringQueue{false};
    bool m_dropTargetRegistered{false};
    bool m_isScrubbingWaveform{false}; // New: Soundcloud-style scrubbing
    int m_hoveredQueueIndex{-1};       // New: Queue hover state
    
    // Visuals
    NomadUI::NUIColor m_currentHeaderColor{0.1f, 0.1f, 0.1f, 1.0f}; // Cached for waveform gradient
    
    // Cover Art
    uint32_t m_coverArtTextureId{0};       // OpenGL texture ID for current cover art
    int m_coverArtWidth{0};
    int m_coverArtHeight{0};
    std::string m_currentTrackId;          // To detect track changes
    
    // SVGs (Icons)
    std::shared_ptr<NomadUI::NUISVGDocument> m_svgPlay;
    std::shared_ptr<NomadUI::NUISVGDocument> m_svgPause;
    std::shared_ptr<NomadUI::NUISVGDocument> m_svgPrev;
    std::shared_ptr<NomadUI::NUISVGDocument> m_svgNext;
    
    // Layout Logic
    void setupComponents();
    void layoutComponents();
    
    // Rendering Sub-routines
    void renderWaveform(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& area);
    void renderQueue(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& area);
    
    // Helpers
    void updateFromEngine();
    std::string formatTime(double seconds) const;
};

} // namespace Nomad
