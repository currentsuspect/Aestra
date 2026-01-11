// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file NomadContent.h
 * @brief Main content area for NOMAD DAW
 * 
 * This class manages the primary workspace including:
 * - Track manager and timeline
 * - File browser and preview
 * - Mixer, Piano Roll, and Arsenal panels
 * - View focus and panel positioning
 */

#pragma once

#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Widgets/NUISegmentedControl.h"
#include "../NomadUI/Core/NUILabel.h"
#include "ViewTypes.h"
#include "TransportBar.h"
#include "../NomadAudio/include/PatternSource.h"
#include "OverlayLayer.h"
#include <memory>
#include <string>
#include <chrono>

// Forward declarations - NomadUI
namespace NomadUI {
    class NUIRenderer;
    class NUIPlatformBridge;
    class FileBrowser;
    class FilePreviewPanel;
    class FileItem;
    class AudioVisualizer;
    class PluginUIController;
}

// Forward declarations - Nomad::Audio (includes panel classes)
namespace Nomad::Audio {
    class AudioEngine;
    class TrackManager;
    class TrackManagerUI;
    class PreviewEngine;
    class MixerPanel;
    class PianoRollPanel;
    class ArsenalPanel;
    class ArsenalPanel;
    class PatternBrowserPanel;
    class WindowPanel;
}

namespace NomadUI {
    class PluginBrowserPanel;
}

/**
 * @brief View focus - which part of the UI is emphasized
 * Arsenal and Timeline coexist; one is focused, the other is backgrounded
 */
enum class ViewFocus {
    Arsenal,   // Pattern construction/sound design
    Timeline   // Arrangement/composition
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
 * @brief Main content area for NOMAD DAW
 */
class NomadContent : public NomadUI::NUIComponent {
public:
    NomadContent();
    ~NomadContent();
    
    struct ViewState {
        bool mixerOpen = false;
        bool pianoRollOpen = false;
        bool sequencerOpen = false;
        bool playlistActive = true;

        // Canonical Panel Positions (Overlay-local coordinates)
        NomadUI::NUIRect mixerRect = {0, 0, 800, 400};
        NomadUI::NUIRect pianoRollRect = {0, 0, 800, 450};
        NomadUI::NUIRect sequencerRect = {0, 0, 600, 300};

        // Temporary Drag State
        bool isDragging = false;
        Nomad::Audio::ViewType draggingView = Nomad::Audio::ViewType::Playlist;
        NomadUI::NUIPoint dragStartMouseOverlay = {0, 0};
        NomadUI::NUIRect dragStartRect = {0, 0, 0, 0};
    };

    // Lifecycle
    void onUpdate(double dt) override;
    void onRender(NomadUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;

    // View Management
    void setViewOpen(Nomad::Audio::ViewType view, bool open);
    void toggleView(Nomad::Audio::ViewType view);
    void toggleFileBrowser();
    void syncViewState();
    void setViewFocus(ViewFocus focus);
    ViewFocus getViewFocus() const { return m_viewFocus; }
    
    // Arsenal Panel Visibility (independent of mode)
    void setArsenalPanelVisible(bool visible);
    void toggleArsenalPanel();

    // Panel Physics & Constraints
    NomadUI::NUIRect computeSafeRect() const;
    NomadUI::NUIRect computeAllowedRectForPanels() const;
    NomadUI::NUIRect computeMaximizedRect() const;
    NomadUI::NUIRect clampRectToAllowed(NomadUI::NUIRect panel, const NomadUI::NUIRect& allowed) const;

    // Panel Drag Handlers
    void beginPanelDrag(Nomad::Audio::ViewType view, const NomadUI::NUIPoint& mouseScreen);
    void updatePanelDrag(Nomad::Audio::ViewType view, const NomadUI::NUIPoint& mouseScreen);
    void endPanelDrag(Nomad::Audio::ViewType view);

    // Getters
    void setAudioStatus(bool active);
    Nomad::TransportBar* getTransportBar();
    std::shared_ptr<NomadUI::AudioVisualizer> getAudioVisualizer();
    std::shared_ptr<NomadUI::AudioVisualizer> getWaveformVisualizer();
    Nomad::Audio::PreviewEngine* getPreviewEngine();
    std::shared_ptr<Nomad::Audio::TrackManager> getTrackManager();
    std::shared_ptr<Nomad::Audio::TrackManagerUI> getTrackManagerUI();
    std::shared_ptr<NomadUI::NUISegmentedControl> getViewToggle();
    Nomad::Audio::PatternID getActivePatternID() const;
    std::shared_ptr<NomadUI::FileBrowser> getFileBrowser() const;

    // Platform
    void setPlatformBridge(NomadUI::NUIPlatformBridge* bridge);
    void setAudioEngine(Nomad::Audio::AudioEngine* engine) { m_audioEngine = engine; }

    // Project Management
    void resetToDefaultProject();  // Clear and recreate default tracks
    
    // Demo/Testing
    void addDemoTracks();
    bool generateTestWavFile(const std::string& filename, float frequency, double duration);

    // Initial Plugin UI Population
    void refreshPluginList();

    // Sound Preview
    void playSoundPreview(const NomadUI::FileItem& file);
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
    std::shared_ptr<NomadUI::NUIComponent> m_workspaceLayer;
    std::shared_ptr<OverlayLayer> m_overlayLayer;

    std::shared_ptr<Nomad::TransportBar> m_transportBar;
    
    // View focus toggle (Arsenal/Timeline segmented control)
    std::shared_ptr<NomadUI::NUISegmentedControl> m_viewToggle;
    std::shared_ptr<NomadUI::NUILabel> m_scopeLabel;
    

    
    // Browser section
    std::shared_ptr<NomadUI::NUISegmentedControl> m_browserToggle;
    std::shared_ptr<NomadUI::FileBrowser> m_fileBrowser;
    std::shared_ptr<NomadUI::PluginBrowserPanel> m_pluginBrowser;
    std::shared_ptr<NomadUI::FilePreviewPanel> m_previewPanel;
    std::shared_ptr<Nomad::Audio::PatternBrowserPanel> m_patternBrowser;
    std::shared_ptr<NomadUI::AudioVisualizer> m_audioVisualizer;
    std::shared_ptr<NomadUI::AudioVisualizer> m_waveformVisualizer;
    std::shared_ptr<Nomad::Audio::TrackManager> m_trackManager;
    std::shared_ptr<Nomad::Audio::TrackManagerUI> m_trackManagerUI;
    NomadUI::NUIPlatformBridge* m_platformBridge = nullptr;
    Nomad::Audio::AudioEngine* m_audioEngine = nullptr;
    
    std::shared_ptr<Nomad::Audio::MixerPanel> m_mixerPanel;
    std::shared_ptr<Nomad::Audio::PianoRollPanel> m_pianoRollPanel;
    std::shared_ptr<Nomad::Audio::ArsenalPanel> m_sequencerPanel;
    std::shared_ptr<NomadUI::PluginUIController> m_pluginController;

    std::unique_ptr<Nomad::Audio::PreviewEngine> m_previewEngine;
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
