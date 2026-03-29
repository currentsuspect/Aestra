// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file AestraContent.h
 * @brief Main content area for Aestra
 * 
 * This class manages the primary workspace including:
 * - Track manager and timeline
 * - File browser and preview
 * - Mixer, Piano Roll, and Arsenal panels
 * - View focus and panel positioning
 */

#pragma once

#include "../AestraUI/Core/NUIComponent.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "NUISegmentedControl.h"
#include "NUILabel.h"
#include "ViewTypes.h"
#include "TransportBar.h"
#include "PatternSource.h"
#include "OverlayLayer.h"
#include <memory>
#include <string>
#include <chrono>

// Forward declarations - AestraUI
namespace AestraUI {
    class NUIRenderer;
    class NUIPlatformBridge;
    class FileBrowser;
    class FilePreviewPanel;
    class FileItem;
    class AudioVisualizer;
    class PluginUIController;
}

// Forward declarations - Aestra::Audio (includes panel classes)
namespace Aestra::Audio {
    class AudioEngine;
    class TrackManager;
    class TrackManagerUI;
    class PreviewEngine;
    class MixerPanel;
    class PianoRollPanel;
    class ArsenalPanel;
    class PatternBrowserPanel;
    class WindowPanel;
    class AuditionEngine;  // For Audition Mode
}

namespace Aestra {
    class AuditionPanel;   // For Audition Mode UI
}

namespace AestraUI {
    class PluginBrowserPanel;
}

/**
 * @brief View focus - which part of the UI is emphasized
 * Arsenal, Timeline, and Audition are the three main modes
 */
enum class ViewFocus {
    Arsenal,   // Pattern construction/sound design
    Timeline,  // Arrangement/composition
    Audition   // Album listening/reference/DSP preview
};

/**
 * @brief Playback scope - what the transport will play
 * Makes playback intent explicit to prevent user confusion
 */
enum class PlaybackScope {
    Pattern,      // Play active pattern (looped)
    Arrangement,  // Play timeline arrangement
    Selection,    // Future: play selected clips
    LoopRegion    // Future: play loop region
};

/**
 * @brief Main content area for Aestra
 */
class AestraContent : public AestraUI::NUIComponent {
public:
    AestraContent();
    ~AestraContent();
    
    struct ViewState {
        bool mixerOpen = false;
        bool pianoRollOpen = false;
        bool sequencerOpen = false;
        bool playlistActive = true;

        // Canonical Panel Positions (Overlay-local coordinates)
        AestraUI::NUIRect mixerRect = {0, 0, 800, 400};
        AestraUI::NUIRect pianoRollRect = {0, 0, 800, 450};
        AestraUI::NUIRect sequencerRect = {0, 0, 600, 300};

        // Temporary Drag State
        bool isDragging = false;
        Aestra::Audio::ViewType draggingView = Aestra::Audio::ViewType::Playlist;
        AestraUI::NUIPoint dragStartMouseOverlay = {0, 0};
        AestraUI::NUIRect dragStartRect = {0, 0, 0, 0};
    };

    // Lifecycle
    void onUpdate(double dt) override;
    void onRender(AestraUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    bool onKeyEvent(const AestraUI::NUIKeyEvent& event) override; // [NEW] Global shortcuts

    // View Management
    void setViewOpen(Aestra::Audio::ViewType view, bool open);
    void toggleView(Aestra::Audio::ViewType view);
    void toggleFileBrowser();
    void syncViewState();
    
    // Panel State Persistence (Issue #120)
    float getBrowserWidth() const;
    void setBrowserWidth(float width);
    bool isBrowserVisible() const;
    void setBrowserVisible(bool visible);
    bool isMixerVisible() const;
    void setMixerVisible(bool visible);
    void setViewFocus(ViewFocus focus);
    ViewFocus getViewFocus() const { return m_viewFocus; }
    
    // Arsenal Panel Visibility (independent of mode)
    void setArsenalPanelVisible(bool visible);
    void toggleArsenalPanel();

    // Panel Physics & Constraints
    AestraUI::NUIRect computeSafeRect() const;
    AestraUI::NUIRect computeAllowedRectForPanels() const;
    AestraUI::NUIRect computeMaximizedRect() const;
    AestraUI::NUIRect clampRectToAllowed(AestraUI::NUIRect panel, const AestraUI::NUIRect& allowed) const;

    // Panel Drag Handlers
    void beginPanelDrag(Aestra::Audio::ViewType view, const AestraUI::NUIPoint& mouseScreen);
    void updatePanelDrag(Aestra::Audio::ViewType view, const AestraUI::NUIPoint& mouseScreen);
    void endPanelDrag(Aestra::Audio::ViewType view);

    // Getters
    void setAudioStatus(bool active);
    Aestra::TransportBar* getTransportBar();
    std::shared_ptr<AestraUI::AudioVisualizer> getAudioVisualizer();
    std::shared_ptr<AestraUI::AudioVisualizer> getWaveformVisualizer();
    Aestra::Audio::PreviewEngine* getPreviewEngine();
    std::shared_ptr<Aestra::Audio::TrackManager> getTrackManager();
    std::shared_ptr<Aestra::Audio::TrackManagerUI> getTrackManagerUI();
    std::shared_ptr<AestraUI::NUISegmentedControl> getViewToggle();
    Aestra::Audio::PatternID getActivePatternID() const;
    std::shared_ptr<AestraUI::FileBrowser> getFileBrowser() const;

    // Platform
    void setPlatformBridge(AestraUI::NUIPlatformBridge* bridge);
    void setAudioEngine(Aestra::Audio::AudioEngine* engine);

    // Project Management
    void resetToDefaultProject();  // Clear and recreate default tracks
    
    // Demo/Testing
    void addDemoTracks();
    bool generateTestWavFile(const std::string& filename, float frequency, double duration);

    // Initial Plugin UI Population
    void refreshPluginList();

    // Sound Preview
    void playSoundPreview(const AestraUI::FileItem& file);
    void stopSoundPreview();
    void loadSampleIntoSelectedTrack(const std::string& filePath);
    void updateSoundPreview();
    void seekSoundPreview(double seconds);
    bool isPlayingPreview() const;
    void updatePreviewPlayhead();
    
    // Plugin Loading
    void loadEffectToSelectedTrack(const std::string& pluginId);
    void loadInstrumentToArsenal(const std::string& pluginId);

private:
    std::shared_ptr<AestraUI::NUIComponent> m_workspaceLayer;
    std::shared_ptr<OverlayLayer> m_overlayLayer;

    std::shared_ptr<Aestra::TransportBar> m_transportBar;
    
    // View focus toggle (Arsenal/Timeline segmented control)
    std::shared_ptr<AestraUI::NUISegmentedControl> m_viewToggle;
    std::shared_ptr<AestraUI::NUILabel> m_scopeLabel;
    

    
    // Browser section
    std::shared_ptr<AestraUI::NUISegmentedControl> m_browserToggle;
    std::shared_ptr<AestraUI::FileBrowser> m_fileBrowser;
    std::shared_ptr<AestraUI::PluginBrowserPanel> m_pluginBrowser;
    std::shared_ptr<AestraUI::FilePreviewPanel> m_previewPanel;
    std::shared_ptr<Aestra::Audio::PatternBrowserPanel> m_patternBrowser;
    std::shared_ptr<AestraUI::AudioVisualizer> m_audioVisualizer;
    std::shared_ptr<AestraUI::AudioVisualizer> m_waveformVisualizer;
    std::shared_ptr<Aestra::Audio::TrackManager> m_trackManager;
    std::shared_ptr<Aestra::Audio::TrackManagerUI> m_trackManagerUI;
    AestraUI::NUIPlatformBridge* m_platformBridge = nullptr;
    Aestra::Audio::AudioEngine* m_audioEngine = nullptr;
    
    std::shared_ptr<Aestra::Audio::MixerPanel> m_mixerPanel;
    std::shared_ptr<Aestra::Audio::PianoRollPanel> m_pianoRollPanel;
    std::shared_ptr<Aestra::Audio::ArsenalPanel> m_sequencerPanel;
    std::shared_ptr<AestraUI::PluginUIController> m_pluginController;
    
    // Temp files for Audition (v4.0)
    std::vector<std::string> m_tempFiles;
    
    // Audition Mode
    std::shared_ptr<Aestra::Audio::AuditionEngine> m_auditionEngine;
    std::shared_ptr<Aestra::AuditionPanel> m_auditionPanel;

    std::unique_ptr<Aestra::Audio::PreviewEngine> m_previewEngine;
    bool m_spaceShortcutLatched{false};
    bool m_audioActive = false;
    
    // View state
    ViewState m_viewState;
    ViewFocus m_viewFocus = ViewFocus::Timeline;
    uint32_t m_lastSelectedChannelId = 0xFFFFFFFFu;
    
    // Sound preview state
    bool m_previewIsPlaying = false;
    std::chrono::steady_clock::time_point m_previewStartTime{};
    double m_previewDuration = 300.0;
    std::string m_currentPreviewFile;
    
    // Playback state persistence
    double m_savedTimelinePosition = 0.0;
};
