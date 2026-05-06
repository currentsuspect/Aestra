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
#include "../AestraAudio/include/Models/UnitManager.h"
#include <memory>
#include <string>
#include <chrono>

// Forward declarations - AestraUI
namespace AestraUI {
    class NUIRenderer;
    class NUIPlatformBridge;
    enum class NUICursorStyle;
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
    class SampleEditorPanel;
    class PatternBrowserPanel;
    class WindowPanel;
    class AestraHistoryPanel;
    class AuditionEngine;  // For Audition Mode
    class PlaybackGraphController;
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
    /** @brief Create the main application workspace component. */
    AestraContent();
    /** @brief Tear down the workspace and owned panels. */
    ~AestraContent();
    
    /**
     * @brief Persisted overlay-panel state for the current workspace.
     */
    struct ViewState {
        /** @brief True when the mixer overlay is open. */
        bool mixerOpen = false;
        /** @brief True when the piano-roll overlay is open. */
        bool pianoRollOpen = false;
        /** @brief True when the Arsenal overlay is open. */
        bool sequencerOpen = false;
        /** @brief True when the playlist/timeline view is the active overlay. */
        bool playlistActive = true;

        /** @brief Mixer overlay bounds in overlay-local coordinates. */
        AestraUI::NUIRect mixerRect = {0, 0, 800, 400};
        /** @brief Piano-roll overlay bounds in overlay-local coordinates. */
        AestraUI::NUIRect pianoRollRect = {0, 0, 800, 450};
        /** @brief Arsenal overlay bounds in overlay-local coordinates. */
        AestraUI::NUIRect sequencerRect = {0, 0, 600, 300};
        AestraUI::NUIRect historyRect = {0, 80, 280, 460};

        /** @brief True while an overlay panel is being dragged. */
        bool isDragging = false;
        /** @brief View currently being dragged. */
        Aestra::Audio::ViewType draggingView = Aestra::Audio::ViewType::Playlist;
        /** @brief Mouse origin in overlay coordinates for the active drag. */
        AestraUI::NUIPoint dragStartMouseOverlay = {0, 0};
        /** @brief Panel rectangle captured at drag start. */
        AestraUI::NUIRect dragStartRect = {0, 0, 0, 0};
    };

    /** @brief Advance workspace state and child-panel updates. */
    void onUpdate(double dt) override;
    /** @brief Render the workspace and active overlays. */
    void onRender(AestraUI::NUIRenderer& renderer) override;
    /** @brief Relayout the workspace after a host resize. */
    void onResize(int width, int height) override;
    /** @brief Handle workspace mouse interactions (including dock resize drags). */
    bool onMouseEvent(const AestraUI::NUIMouseEvent& event) override;
    /** @brief Handle global keyboard shortcuts for the workspace. */
    bool onKeyEvent(const AestraUI::NUIKeyEvent& event) override; // [NEW] Global shortcuts

    /** @brief Open or close a specific workspace overlay. */
    void setViewOpen(Aestra::Audio::ViewType view, bool open);
    /** @brief Toggle visibility for a specific workspace overlay. */
    void toggleView(Aestra::Audio::ViewType view);
    /** @brief Toggle visibility of the left browser area. */
    void toggleFileBrowser();
    /** @brief Synchronize overlay state into owned child views. */
    void syncViewState();
    
    /** @brief Get the active browser column width. */
    float getBrowserWidth() const;
    /** @brief Set the active browser column width. */
    void setBrowserWidth(float width);
    /** @brief Check whether the left browser area is visible. */
    bool isBrowserVisible() const;
    /** @brief Show or hide the left browser area. */
    void setBrowserVisible(bool visible);
    /** @brief Check whether the mixer panel is visible. */
    bool isMixerVisible() const;
    /** @brief Show or hide the mixer panel. */
    void setMixerVisible(bool visible);
    /** @brief Set the active workspace mode. */
    void setViewFocus(ViewFocus focus);
    /** @brief Get the active workspace mode. */
    ViewFocus getViewFocus() const { return m_viewFocus; }
    
    /** @brief Explicitly show or hide the Arsenal panel. */
    void setArsenalPanelVisible(bool visible);
    /** @brief Toggle the Arsenal panel regardless of active mode. */
    void toggleArsenalPanel();
    /** @brief Toggle the History panel visibility. */
    void toggleHistoryPanel();

    /** @brief Compute the safe workspace rectangle after chrome and sidebars. */
    AestraUI::NUIRect computeSafeRect() const;
    /** @brief Get the x coordinate of the visible browser edge. */
    float getVisibleBrowserEdge() const;
    /** @brief Compute the rectangle panels are allowed to occupy. */
    AestraUI::NUIRect computeAllowedRectForPanels() const;
    /** @brief Compute the maximized overlay rectangle. */
    AestraUI::NUIRect computeMaximizedRect() const;
    /** @brief Clamp an overlay rectangle to the current allowed bounds. */
    AestraUI::NUIRect clampRectToAllowed(AestraUI::NUIRect panel, const AestraUI::NUIRect& allowed) const;
    /** @brief Resolve resize cursor style for floating panel edges at a mouse position. */
    AestraUI::NUICursorStyle getPanelResizeCursorStyle(const AestraUI::NUIPoint& mouseScreen) const;

    /** @brief Begin dragging an overlay panel. */
    void beginPanelDrag(Aestra::Audio::ViewType view, const AestraUI::NUIPoint& mouseScreen);
    /** @brief Update the active overlay-panel drag. */
    void updatePanelDrag(Aestra::Audio::ViewType view, const AestraUI::NUIPoint& mouseScreen);
    /** @brief End the active overlay-panel drag. */
    void endPanelDrag(Aestra::Audio::ViewType view);

    /** @brief Update the transport/UI audio-active state. */
    void setAudioStatus(bool active);
    /** @brief Get the transport bar widget. */
    Aestra::TransportBar* getTransportBar();
    /** @brief Get the compact transport VU visualizer. */
    std::shared_ptr<AestraUI::AudioVisualizer> getAudioVisualizer();
    /** @brief Get the transport waveform visualizer. */
    std::shared_ptr<AestraUI::AudioVisualizer> getWaveformVisualizer();
    /** @brief Get the preview engine used for browser audition. */
    Aestra::Audio::PreviewEngine* getPreviewEngine();
    /** @brief Get the shared track manager. */
    std::shared_ptr<Aestra::Audio::TrackManager> getTrackManager();
    /** @brief Get the track/timeline UI component. */
    std::shared_ptr<Aestra::Audio::TrackManagerUI> getTrackManagerUI();
    /** @brief Get the top-level mode toggle. */
    std::shared_ptr<AestraUI::NUISegmentedControl> getViewToggle();
    /** @brief Get the currently active pattern identifier. */
    Aestra::Audio::PatternID getActivePatternID() const;
    /** @brief Get the file-browser widget. */
    std::shared_ptr<AestraUI::FileBrowser> getFileBrowser() const;
    /** @brief Request timeline-aware transport play/start behavior. */
    void requestTransportPlay();
    /** @brief Start playback based on the current focus mode. */
    void playFromCurrentFocus();
    /** @brief Stop playback based on the current focus mode. */
    void stopFromCurrentFocus(bool hardStop = false);
    /** @brief Pause playback based on the current focus mode. */
    void pauseFromCurrentFocus();

    /** @brief Bind the platform bridge used by file dialogs and native helpers. */
    void setPlatformBridge(AestraUI::NUIPlatformBridge* bridge);
    /** @brief Bind the live audio engine used by transport-aware panels. */
    void setAudioEngine(Aestra::Audio::AudioEngine* engine);
    /** @brief Propagate transport tempo to tempo-aware internal plugins. */
    void setPluginTempo(float bpm);
    /** @brief Get the playback graph controller for canonical graph invalidation. */
    Aestra::Audio::PlaybackGraphController* getPlaybackGraphController() const;

    /** @brief Reset the workspace back to the default starter project. */
    void resetToDefaultProject();  // Clear and recreate default tracks
    
    /** @brief Populate the project with demo tracks for testing. */
    void addDemoTracks();
    /** @brief Generate a temporary sine-wave WAV file for testing. */
    bool generateTestWavFile(const std::string& filename, float frequency, double duration);

    /** @brief Refresh the visible plugin list in the browser. */
    void refreshPluginList();
    /** @brief Refresh track/pattern/arsenal UI after an external project load. */
    void refreshProjectViews();

    /** @brief Start preview playback for a file-browser item. */
    void playSoundPreview(const AestraUI::FileItem& file);
    /** @brief Stop the active file-browser preview. */
    void stopSoundPreview();
    /** @brief Load a sample into the currently selected track. */
    void loadSampleIntoSelectedTrack(const std::string& filePath);
    /** @brief Advance preview-state bookkeeping. */
    void updateSoundPreview();
    /** @brief Seek inside the active file preview. */
    void seekSoundPreview(double seconds);
    /** @brief Check whether file preview playback is active. */
    bool isPlayingPreview() const;
    /** @brief Update the preview playhead visible in the UI. */
    void updatePreviewPlayhead();
    
    /** @brief Load an effect plugin onto the selected track. */
    void loadEffectToSelectedTrack(const std::string& pluginId);
    /** @brief Load an instrument plugin into Arsenal. */
    void loadInstrumentToArsenal(const std::string& pluginId);
    /** @brief Load an instrument plugin into a specific Arsenal unit. */
    void loadInstrumentIntoArsenalUnit(Aestra::Audio::UnitID unitId, const std::string& pluginId);
    /** @brief Open a pattern in the piano-roll editor. */
    void openPatternInPianoRoll(Aestra::Audio::PatternID patternId);

private:
    Aestra::Audio::UnitID resolveEditingUnitForPattern(Aestra::Audio::PatternID patternId) const;
    ViewFocus resolveTransportFocus() const;
    bool isTransportRolling() const;
    void handleTransportPlayRequest();
    void clearPendingCountIn();
    void updatePendingCountIn();
    void startPatternClipPreview(Aestra::Audio::PatternID patternId);
    void stopPatternClipPreview(bool restoreTimelineUi);
    enum class BrowserResizeTarget {
        None,
        FileRail,
        PatternRail
    };
    BrowserResizeTarget hitTestBrowserResizeTarget(const AestraUI::NUIPoint& mouseScreen) const;
    void updateBrowserResizeDrag(const AestraUI::NUIPoint& mouseScreen);

    // Pattern loop length helpers
    double getActivePatternLengthBeats() const;
    void updatePatternLoopLength(Aestra::Audio::PatternID patternId);

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
    float m_fileBrowserWidthPref{-1.0f};
    float m_patternBrowserWidthPref{-1.0f};
    bool m_browserResizing{false};
    BrowserResizeTarget m_browserResizeTarget{BrowserResizeTarget::None};
    float m_browserResizeStartX{0.0f};
    float m_browserResizeStartFileWidth{0.0f};
    float m_browserResizeStartPatternWidth{0.0f};
    std::shared_ptr<AestraUI::AudioVisualizer> m_audioVisualizer;
    std::shared_ptr<AestraUI::AudioVisualizer> m_waveformVisualizer;
    std::shared_ptr<Aestra::Audio::TrackManager> m_trackManager;
    std::shared_ptr<Aestra::Audio::TrackManagerUI> m_trackManagerUI;
    AestraUI::NUIPlatformBridge* m_platformBridge = nullptr;
    Aestra::Audio::AudioEngine* m_audioEngine = nullptr;
    
    std::shared_ptr<Aestra::Audio::MixerPanel> m_mixerPanel;
    std::shared_ptr<Aestra::Audio::PianoRollPanel> m_pianoRollPanel;
    std::shared_ptr<Aestra::Audio::ArsenalPanel> m_sequencerPanel;
    std::shared_ptr<Aestra::Audio::AestraHistoryPanel> m_historyPanel;
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
    std::vector<float> m_transportWaveformScratch;
    
    // Playback state persistence
    double m_savedTimelinePosition = 0.0;
    bool m_patternClipPreviewActive{false};
    Aestra::Audio::PatternID m_previewPatternId{};
    bool m_countInEnabled{false};
    bool m_pendingCountIn{false};
    bool m_forcedMetronomeForCountIn{false};
    double m_pendingCountInTargetSeconds{0.0};

    std::shared_ptr<Aestra::Audio::SampleEditorPanel> m_sampleEditorPanel;
    AestraUI::NUIRect m_sampleEditorRect{0.0f, 0.0f, 640.0f, 430.0f};

    // Playback graph controller - single authoritative drain for graph rebuilds
    std::unique_ptr<Aestra::Audio::PlaybackGraphController> m_playbackGraphController;
    Aestra::Audio::UnitID m_sampleEditorUnitId{0};
    bool m_sampleEditorDragging{false};
    AestraUI::NUIPoint m_sampleEditorDragStartMouseOverlay{0.0f, 0.0f};
    AestraUI::NUIRect m_sampleEditorDragStartRect{0.0f, 0.0f, 0.0f, 0.0f};

    void openSampleEditorForUnit(Aestra::Audio::UnitID unitId, const std::string& samplePath);
    void syncSampleEditorToUnit(Aestra::Audio::UnitID unitId);
};
