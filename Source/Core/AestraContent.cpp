// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file AestraContent.cpp
 * @brief Main content area for Aestra - Implementation
 */

// Include panel headers FIRST to define complete types before AestraContent.h forward declarations
#include "AestraContent.h"

#include "../AestraUI/Widgets/PluginBrowserPanel.h"
#include "../AestraUI/Widgets/PluginUIController.h"
#include "../AestraUI/Widgets/UIMixerInspector.h"
#include "../AestraUI/Widgets/UIMixerPanel.h"
#include "AestraHistoryPanel.h"
#include "ArsenalPanel.h"
#include "AudioGraphBuilder.h"
#include "AuditionEngine.h" // Audition Mode backend
#include "AuditionPanel.h"  // Audition Mode UI
#include "Commands/PluginCommands.h"
#include "MidiInputService.h"
#include "MixerChannel.h"
#include "MixerPanel.h"
#include "PatternBrowserPanel.h"
#include "PianoRollPanel.h"
#include "Plugin/AestraDelay.h"
#include "Plugin/AestraLFO.h"
#include "PluginManager.h"
#include "SampleEditorPanel.h"

// AestraUI includes
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Base/NUITextInput.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../AestraUI/Platform/NUIPlatformBridge.h"
#include "../AestraUI/Widgets/TrackColorPalette.h"

// Component includes
#include "AudioVisualizer.h"
#include "FileBrowser.h"
#include "FilePreviewPanel.h"
#include "TrackManagerUI.h"
#include "TransportBar.h"

// Audio includes
#include "../AestraAudio/include/Commands/CommandRegistry.h"
#include "../AestraCore/include/AestraLog.h"
#include "AudioEngine.h"
#include "ChannelSlotMap.h"
#include "ClipSource.h"
#include "MiniAudioDecoder.h"
#include "MixerViewModel.h"
#include "PlaybackGraphController.h"
#include "Plugin/SamplerPlugin.h"
#include "PreviewEngine.h"
#include "TrackManager.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace Aestra;
using namespace AestraUI;
using namespace Aestra::Audio;

namespace {
constexpr float kMinFileBrowserWidth = 300.0f;
constexpr float kMinPatternBrowserWidth = 96.0f;
constexpr float kMinTrackAreaWidth = 420.0f;
constexpr float kResizeHitWidth = 6.0f;

std::vector<float> buildPreviewWaveform(const std::vector<float>& samples, uint32_t channels, size_t targetSize = 128) {
    std::vector<float> waveform(targetSize, 0.0f);
    if (samples.empty() || channels == 0 || targetSize == 0) {
        return waveform;
    }

    const size_t totalFrames = samples.size() / channels;
    if (totalFrames == 0) {
        return waveform;
    }

    const double framesPerBin = static_cast<double>(totalFrames) / static_cast<double>(targetSize);
    for (size_t bin = 0; bin < targetSize; ++bin) {
        const size_t startFrame = static_cast<size_t>(static_cast<double>(bin) * framesPerBin);
        const size_t endFrame =
            std::min(totalFrames, static_cast<size_t>(static_cast<double>(bin + 1) * framesPerBin) + 1);

        float peak = 0.0f;
        for (size_t frame = startFrame; frame < endFrame; ++frame) {
            float sum = 0.0f;
            for (uint32_t ch = 0; ch < channels; ++ch) {
                sum += std::abs(samples[frame * channels + ch]);
            }
            peak = std::max(peak, sum / static_cast<float>(channels));
        }
        waveform[bin] = std::min(1.0f, peak);
    }

    return waveform;
}

std::vector<float> buildEditorWaveform(const std::vector<float>& samples, uint32_t channels, uint32_t sampleLength,
                                       size_t buckets = 1024) {
    std::vector<float> waveform;
    if (samples.empty() || channels == 0 || sampleLength == 0 || buckets == 0) {
        return waveform;
    }

    const size_t framesPerBucket = std::max<size_t>(1, static_cast<size_t>(sampleLength) / buckets);
    waveform.reserve(buckets * 2);
    for (size_t bucket = 0; bucket < buckets; ++bucket) {
        const size_t startFrame = bucket * framesPerBucket;
        const size_t endFrame = std::min<size_t>(static_cast<size_t>(sampleLength), startFrame + framesPerBucket);
        if (startFrame >= endFrame) {
            waveform.push_back(0.0f);
            waveform.push_back(0.0f);
            continue;
        }

        float minV = 1.0f;
        float maxV = -1.0f;
        for (size_t frame = startFrame; frame < endFrame; ++frame) {
            const size_t idx = frame * channels;
            const float mono = (channels > 1) ? 0.5f * (samples[idx] + samples[idx + 1]) : samples[idx];
            minV = std::min(minV, mono);
            maxV = std::max(maxV, mono);
        }
        waveform.push_back(maxV);
        waveform.push_back(minV);
    }
    return waveform;
}
} // namespace

// =============================================================================
// SECTION: Construction
// =============================================================================

AestraContent::~AestraContent() {
    m_musicalTyping.releaseAllNotes();
    if (m_audioEngine) {
        m_audioEngine->setPreviewEngine(nullptr);
    }

    // TrackManager may be shared outside this component; clear the stored owner callback before member teardown.
    if (m_trackManager) {
        m_trackManager->setStopPreviewCallback(nullptr);
    }

    // Cancel any running plugin scan to prevent callbacks from accessing dead pointers
    auto& pm = Aestra::Audio::PluginManager::getInstance();
    pm.getScanner().cancelScan();
    // Note: PluginManager's destructor will join the scan thread

    // Clean up temporary audition files
    for (const auto& file : m_tempFiles) {
        try {
            if (std::filesystem::exists(file)) {
                std::filesystem::remove(file);
                AESTRA_LOG_DEBUG("[AestraContent] Deleted temp audition file: " + file);
            }
        } catch (const std::exception& e) {
            // Log but don't fail cleanup
            AESTRA_LOG_WARNING("[AestraContent] Failed to delete temp file: " + std::string(e.what()));
        }
    }
}

AestraContent::AestraContent()
    : m_musicalTyping([this](uint64_t unitId, uint8_t status, uint8_t data1, uint8_t data2) {
        return m_audioEngine && m_audioEngine->postLiveMidiEvent(unitId, status, data1, data2);
    }) {
    // Create layers
    m_workspaceLayer = std::make_shared<AestraUI::NUIComponent>();
    m_workspaceLayer->setId("WorkspaceLayer");
    addChild(m_workspaceLayer);

    m_overlayLayer = std::make_shared<OverlayLayer>();
    addChild(m_overlayLayer);

    // Create Plugin Controller
    m_pluginController = std::make_shared<PluginUIController>();
    m_pluginController->setPluginManager(&PluginManager::getInstance());
    m_pluginController->setPluginScanner(&PluginManager::getInstance().getScanner());
    m_pluginController->setPopupLayer(m_overlayLayer.get());

    // Scoped subscriptions for playback-critical callbacks
    // Plugin loaded: mark project dirty
    m_connections.add(m_pluginController->pluginLoaded.subscribe([this](const auto&) {
        if (m_trackManager) {
            m_trackManager->markModified();
        }
    }));
    // Effect chain changed: request graph rebuild
    m_connections.add(m_pluginController->effectChainChanged.subscribe([this]() {
        if (m_playbackGraphController) {
            m_playbackGraphController->requestRebuild(TrackManager::GraphDirtyReason::EffectChainChanged);
        }
    }));

    // Create track manager for multi-track functionality
    m_trackManager = std::make_shared<TrackManager>();

    // Initialize Muse command registry with TrackManager dependencies
    Aestra::Audio::CommandRegistry::initialize(m_trackManager.get());

    // TrackManager is owned by AestraContent, and the destructor clears this stored callback before teardown.
    m_trackManager->setStopPreviewCallback([this]() { stopSoundPreview(); });

    addDemoTracks();

    // Defer audio-engine wiring until setAudioEngine() receives the live controller engine.

    // Create track manager UI (add to workspace)
    m_trackManagerUI = std::make_shared<TrackManagerUI>(m_trackManager);

    // Wire up TrackManagerUI internal toggles to centralized authority (v3.1)
    m_trackManagerUI->setOnToggleMixer([this]() { toggleView(Audio::ViewType::Mixer); });
    m_trackManagerUI->setOnTogglePianoRoll([this]() { toggleView(Audio::ViewType::PianoRoll); });
    m_trackManagerUI->setOnOpenPatternInPianoRoll([this](PatternID patternId) { openPatternInPianoRoll(patternId); });
    m_trackManagerUI->setOnPreviewPatternClip([this](PatternID patternId) { startPatternClipPreview(patternId); });
    m_trackManagerUI->setOnStopPatternClipPreview([this]() { stopPatternClipPreview(true); });
    m_trackManagerUI->setOnToggleSequencer([this]() { toggleView(Audio::ViewType::Sequencer); });
    m_trackManagerUI->setOnTogglePlaylist([this]() { toggleView(Audio::ViewType::Playlist); });
    m_trackManagerUI->setOnLoopPresetChanged([this](int preset) {
        if (!m_audioEngine) {
            return;
        }
        if (preset == 0) {
            m_audioEngine->setLoopEnabled(false);
        }
    });
    m_trackManagerUI->setOnLoopRegionUpdate([this](double startBeat, double endBeat) {
        if (!m_audioEngine) {
            return;
        }
        m_audioEngine->setLoopRegion(startBeat, endBeat);
        m_audioEngine->setLoopEnabled(endBeat > startBeat);
    });
    m_trackManagerUI->setOnSelectionMade([this](double startBeat, double endBeat) {
        if (!m_audioEngine) {
            return;
        }
        m_audioEngine->setLoopRegion(startBeat, endBeat);
        m_audioEngine->setLoopEnabled(endBeat > startBeat);
    });

    // Wire TrackManagerUI's graph dirty signal to PlaybackGraphController.
    // TrackManagerUI may emit graphDirty for async plugin operations.
    m_connections.add(m_trackManagerUI->graphDirty.subscribe([this]() {
        if (m_playbackGraphController) {
            m_playbackGraphController->requestRebuild(TrackManager::GraphDirtyReason::EffectChainChanged);
        }
    }));

    // Audition Mode integration - sends track to Audition queue and switches mode
    m_trackManagerUI->setOnSendToAudition([this](uint32_t trackId, const std::string& trackName) {
        // 1. Ensure Audition engine exists
        if (!m_auditionEngine) {
            m_auditionEngine = std::make_shared<Audio::AuditionEngine>();
        }

        // 2. Determine project duration for full track bounce
        double projectDurationBeats = m_trackManager->getPlaylistModel().getTotalDurationBeats();
        if (projectDurationBeats < 0.1)
            projectDurationBeats = 16.0; // Default if empty

        // 3. Generate temporary file path
        auto tempDir = std::filesystem::temp_directory_path();
        auto tempFile =
            tempDir / ("AestraAudition_Track_" + std::to_string(trackId) + "_" +
                       std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".wav");
        std::string tempPath = tempFile.string();

        // 4. Perform bounce (Offline rendering)
        AESTRA_LOG_DEBUG("[AestraContent] Bouncing track " + std::to_string(trackId) + " to " + tempPath);
        bool success =
            m_audioEngine->bounceRangeToWav(0.0, projectDurationBeats, tempPath, static_cast<int32_t>(trackId));

        if (success) {
            m_tempFiles.push_back(tempPath);

            // 5. Add to queue, select it, play it and switch view
            m_auditionEngine->addToQueue(tempPath);

            size_t newIdx = m_auditionEngine->getQueue().size() - 1;
            m_auditionEngine->jumpToTrack(newIdx);
            m_auditionEngine->play();

            setViewFocus(ViewFocus::Audition);
        } else {
            AESTRA_LOG_ERROR("[AestraContent] Failed to bounce track to Audition.");
        }
    });

    m_trackManagerUI->setOnSendSelectionToAudition([this](double startBeat, double endBeat) {
        if (!m_auditionEngine) {
            m_auditionEngine = std::make_shared<Audio::AuditionEngine>();
        }

        // 1. Generate path
        auto tempDir = std::filesystem::temp_directory_path();
        auto tempFile =
            tempDir / ("AestraAudition_Selection_" +
                       std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".wav");
        std::string tempPath = tempFile.string();

        // 2. Perform bounce (trackId -1 means Master Output)
        AESTRA_LOG_DEBUG("[AestraContent] Bouncing selection to " + tempPath);
        bool success = m_audioEngine->bounceRangeToWav(startBeat, endBeat, tempPath, -1);

        if (success) {
            m_tempFiles.push_back(tempPath);

            // 3. Add to queue, select it, play it and switch view
            m_auditionEngine->addToQueue(tempPath);

            size_t newIdx = m_auditionEngine->getQueue().size() - 1;
            m_auditionEngine->jumpToTrack(newIdx);
            m_auditionEngine->play();

            setViewFocus(ViewFocus::Audition);
        } else {
            AESTRA_LOG_ERROR("[AestraContent] Failed to bounce selection to Audition.");
        }
    });
    m_trackManagerUI->setOnClipLibraryChanged([this]() {
        if (m_patternBrowser) {
            m_patternBrowser->refreshClips();
        }
    });

    m_workspaceLayer->addChild(m_trackManagerUI);

    // Create transport bar (Moved to Workspace Layer and Early Init for correct Z-Order behind Sidebars)
    m_transportBar = std::make_shared<Aestra::TransportBar>();
    m_transportBar->setOnToggleView([this](Audio::ViewType view) { toggleView(view); });

    // Wire Transport to AudioEngine (timeline playback)
    m_transportBar->setOnPlay([this]() { handleTransportPlayRequest(); });
    m_transportBar->setOnPause([this]() {
        clearPendingCountIn();
        pauseFromCurrentFocus();
    });
    m_transportBar->setOnStop([this](bool hardStop) {
        clearPendingCountIn();
        stopFromCurrentFocus(hardStop);
        stopSoundPreview();
    });
    m_transportBar->setOnRecord([this](bool recording) {
        if (m_trackManager) {
            bool isRecordArmed = m_trackManager->isRecordArmed();
            if (recording != isRecordArmed) {
                m_trackManager->record();
            }
        }
    });
    m_transportBar->setOnMetronomeToggle([this](bool enabled) {
        if (m_trackManager)
            m_trackManager->enableMetronome(enabled);
    });
    m_transportBar->setOnCountInToggle([this](bool enabled) {
        m_countInEnabled = enabled;
        if (!enabled) {
            clearPendingCountIn();
        }
    });

    // Helper: Stop preview when Audition Queue changes (drop)
    if (m_auditionEngine) {
        m_auditionEngine->setOnQueueUpdated([this]() { stopSoundPreview(); });
    }

    // Add to WORKSPACE layer but we want it ON TOP of sidebars if it's an island?
    // User says "filebrowser hiding the transport".
    // Browsers are added to workspace too.
    // If we want Transport ON TOP, add it LAST.
    // m_workspaceLayer->addChild(m_transportBar); // Deferred to end

    // Browser toggle removed - browser content now starts directly with search/navigation

    // Create file browser (add to workspace)
    m_fileBrowser = std::make_shared<AestraUI::FileBrowser>();
    m_fileBrowser->setOnFileOpened([this](const AestraUI::FileItem& file) {
        AESTRA_LOG_DEBUG("File opened: " + file.path);
        loadSampleIntoSelectedTrack(file.path);
    });
    m_fileBrowser->setOnSoundPreview([this](const AestraUI::FileItem& file) {
        AESTRA_LOG_DEBUG("Sound preview requested: " + file.path);
        playSoundPreview(file);
    });
    m_fileBrowser->setOnNavActionSelected([this](AestraUI::FileBrowser::BrowserNavAction action) {
        const bool isPlugins = (action == AestraUI::FileBrowser::BrowserNavAction::Plugins);
        const bool isPatterns = (action == AestraUI::FileBrowser::BrowserNavAction::Patterns);
        if (m_pluginBrowser) {
            m_pluginBrowser->setVisible(isPlugins);
        }
        if (m_patternBrowser) {
            if (isPatterns) {
                m_patternBrowser->showPatternsTab();
                m_patternBrowser->refreshPatterns();
                m_patternBrowser->setVisible(true);
            } else {
                m_patternBrowser->setVisible(false);
            }
        }
        if (isPlugins && m_fileBrowser && m_pluginBrowser) {
            float navW = m_fileBrowser->getNavPaneWidth();
            auto fbBounds = m_fileBrowser->getBounds();
            float pbWidth = std::max(0.0f, fbBounds.width - navW);
            // Align with file browser content area (below search bar)
            constexpr float searchH = 28.0f;
            m_pluginBrowser->setBounds(
                AestraUI::NUIRect(fbBounds.x + navW, fbBounds.y + searchH, pbWidth, fbBounds.height - searchH));
        }
        onResize(static_cast<int>(getBounds().width), static_cast<int>(getBounds().height));
    });

    m_fileBrowser->setOnSearchTextChanged([this](const std::string& text) {
        if (m_pluginBrowser) {
            m_pluginBrowser->setSearchQuery(text);
        }
    });

    m_workspaceLayer->addChild(m_fileBrowser);

    // Create Plugin Browser
    m_pluginBrowser = std::make_shared<AestraUI::PluginBrowserPanel>();
    m_pluginBrowser->setVisible(false); // Hidden by default

    m_pluginBrowser->setPluginList({}); // Initialize empty, refresh called later
    refreshPluginList();

    // Scan callback
    m_pluginBrowser->setOnScanRequested([this]() {
        auto& pm = Aestra::Audio::PluginManager::getInstance();

        // Prevent spam-clicking: don't start a new scan if one is already running
        if (pm.getScanner().isScanning()) {
            AESTRA_LOG_DEBUG("Scan already in progress, ignoring request.");
            return;
        }

        AESTRA_LOG_DEBUG("Scan requested by user.");

        // Set UI to scanning state
        if (m_pluginBrowser) {
            m_pluginBrowser->setScanning(true, 0.0f);
            m_pluginBrowser->setScanStatus("Scanning...");
        }

        pm.getScanner().scanAsync(
            [this](const std::string& path, int current, int total) {
                // Update progress
                if (m_pluginBrowser && total > 0) {
                    float progress = static_cast<float>(current) / static_cast<float>(total);
                    m_pluginBrowser->setScanning(true, progress);
                    m_pluginBrowser->setScanStatus("Scanning: " + path);
                }
            },
            [this](const std::vector<Aestra::Audio::PluginInfo>& results, bool success) {
                // Clear scanning state
                if (m_pluginBrowser) {
                    m_pluginBrowser->setScanning(false, 0.0f);
                    m_pluginBrowser->setScanStatus("");
                }

                if (success) {
                    // Map results to UI items
                    std::vector<AestraUI::PluginListItem> uiPlugins;
                    for (const auto& p : results) {
                        uiPlugins.push_back(m_pluginController->convertToListItem(p));
                    }

                    if (m_pluginBrowser) {
                        m_pluginBrowser->setPluginList(uiPlugins);
                    }
                    AESTRA_LOG_DEBUG("Scan complete. UI updated with " + std::to_string(results.size()) + " plugins.");
                } else {
                    AESTRA_LOG_ERROR("Plugin scan failed or cancelled.");
                }
            });
    });

    // Plugin load callback (double-click)
    m_pluginBrowser->setOnPluginLoadRequested([this](const AestraUI::PluginListItem& plugin) {
        AESTRA_LOG_DEBUG("Plugin load requested: " + plugin.name + " (" + plugin.typeName + ")");
        if (plugin.typeName == "Effect") {
            loadEffectToSelectedTrack(plugin.id);
        } else if (plugin.typeName == "Instrument") {
            loadInstrumentToArsenal(plugin.id);
        }
    });

    // Bind browser to controller
    m_pluginController->bindBrowser(m_pluginBrowser.get());

    m_workspaceLayer->addChild(m_pluginBrowser);

    // Create file preview panel (add to workspace, below browser)
    m_previewPanel = std::make_shared<AestraUI::FilePreviewPanel>();
    m_previewPanel->setOnPlay([this](const AestraUI::FileItem& file) { playSoundPreview(file); });
    m_previewPanel->setOnStop([this]() { stopSoundPreview(); });
    m_previewPanel->setOnSeek([this](double seconds) { seekSoundPreview(seconds); });
    m_workspaceLayer->addChild(m_previewPanel);

    // Link file selection to preview panel
    m_fileBrowser->setOnFileSelected([this](const AestraUI::FileItem& file) {
        stopSoundPreview();
        if (m_previewPanel) {
            m_previewPanel->setFile(&file);
            const bool filesTabActive = !m_browserToggle || m_browserToggle->getSelectedIndex() == 0;
            m_previewPanel->setVisible(filesTabActive && m_previewPanel->hasFileSelection());
        }
        onResize(static_cast<int>(getBounds().width), static_cast<int>(getBounds().height));
    });
    m_fileBrowser->setOnPathChanged([this](const std::string&) {
        stopSoundPreview();
        if (m_previewPanel) {
            m_previewPanel->clear();
            m_previewPanel->setVisible(false);
        }
        onResize(static_cast<int>(getBounds().width), static_cast<int>(getBounds().height));
    });

    // Create pattern browser panel (add to workspace, side panel)
    m_patternBrowser = std::make_shared<PatternBrowserPanel>(m_trackManager.get());
    m_patternBrowser->setOnPatternSelected([this](PatternID patternId) {
        AESTRA_LOG_TRACE("Pattern selected: " + std::to_string(patternId.value));
        const UnitID editingUnit = resolveEditingUnitForPattern(patternId);
        if (m_sequencerPanel) {
            m_sequencerPanel->setActivePattern(patternId);
            m_sequencerPanel->setSelectedUnit(editingUnit);
        }
        if (m_pianoRollPanel) {
            m_pianoRollPanel->setEditingUnit(editingUnit);
            m_pianoRollPanel->loadPattern(patternId);
        }
    });
    m_patternBrowser->setOnPatternDragStart(
        [this](PatternID patternId) { AESTRA_LOG_TRACE("Pattern drag started: " + std::to_string(patternId.value)); });
    m_patternBrowser->setOnPatternDoubleClick([this](PatternID patternId) {
        AESTRA_LOG_TRACE("Pattern double-clicked: " + std::to_string(patternId.value));
        openPatternInPianoRoll(patternId);
    });
    m_patternBrowser->setOnPatternPreviewRequested([this](PatternID patternId) { startPatternClipPreview(patternId); });
    m_patternBrowser->setOnPatternPlaceOnTimelineRequested([this](PatternID patternId) {
        if (!m_trackManagerUI || !patternId.isValid()) {
            return;
        }
        if (!m_trackManagerUI->placePatternOnTimeline(patternId)) {
            AESTRA_LOG_WARNING("[AestraContent] Failed to place pattern from bin onto timeline.");
        }
    });
    m_patternBrowser->setOnClipPreviewRequested([this](const std::string& filePath) {
        if (filePath.empty()) {
            return;
        }
        if (!m_auditionEngine) {
            m_auditionEngine = std::make_shared<Audio::AuditionEngine>();
            if (m_audioEngine) {
                m_audioEngine->setAuditionEngine(m_auditionEngine.get());
                m_auditionEngine->setSampleRate(static_cast<double>(m_audioEngine->getSampleRate()));
            }
        }
        if (!m_auditionEngine) {
            return;
        }
        m_auditionEngine->addToQueue(filePath);
        const size_t newIndex = m_auditionEngine->getQueue().empty() ? 0 : (m_auditionEngine->getQueue().size() - 1);
        m_auditionEngine->jumpToTrack(newIndex);
        m_auditionEngine->play();
    });
    m_patternBrowser->setOnClipPlaceOnTimelineRequested(
        [this](const std::string& filePath, const std::string& displayName) {
            if (!m_trackManagerUI || filePath.empty()) {
                return;
            }
            if (!m_trackManagerUI->placeFileOnTimeline(filePath, displayName)) {
                AESTRA_LOG_WARNING("[AestraContent] Failed to place clip from bin onto timeline.");
            }
        });
    m_patternBrowser->setOnClipShowInFileBrowserRequested([this](const std::string& filePath) {
        if (!m_fileBrowser || filePath.empty()) {
            return;
        }
        setBrowserVisible(true);
        if (m_browserToggle) {
            m_browserToggle->setSelectedIndex(0);
        }
        m_fileBrowser->selectFile(filePath);
    });
    m_workspaceLayer->addChild(m_patternBrowser);

    // Create panels (add to overlay)
    m_mixerPanel = std::make_shared<MixerPanel>(m_trackManager);
    m_mixerPanel->setVisible(false);
    m_mixerPanel->setOnClose([this]() { toggleView(Audio::ViewType::Mixer); });
    m_mixerPanel->setOnMaximizeToggle(
        [this](bool) { onResize(static_cast<int>(getBounds().width), static_cast<int>(getBounds().height)); });
    m_mixerPanel->setOnDragStart([this](const AestraUI::NUIPoint& pos) { beginPanelDrag(ViewType::Mixer, pos); });
    m_mixerPanel->setOnDragMove([this](const AestraUI::NUIPoint& pos) { updatePanelDrag(ViewType::Mixer, pos); });
    m_mixerPanel->setOnDragEnd([this]() { endPanelDrag(ViewType::Mixer); });
    m_mixerPanel->setMinimumPanelSize(560.0f, 300.0f);
    m_mixerPanel->setOnResizeMove([this](const AestraUI::NUIRect& proposed) {
        const auto allowed = computeAllowedRectForPanels();
        m_viewState.mixerRect = clampRectToAllowed(proposed, allowed);
        if (m_mixerPanel)
            m_mixerPanel->setBounds(m_viewState.mixerRect);
        setDirty(true);
    });
    m_overlayLayer->addChild(m_mixerPanel);
    if (m_platformBridge) {
        m_mixerPanel->setPlatformBridge(m_platformBridge);
    }

    // Create routing map full-panel overlay (launched from mixer inspector minimap)
    m_routingMapPanel = std::make_shared<AestraUI::UIRoutingMap>(AestraUI::UIRoutingMap::Mode::FullPanel);
    m_routingMapPanel->setVisible(false);
    if (m_mixerPanel && m_mixerPanel->getViewModel()) {
        m_routingMapPanel->setViewModel(m_mixerPanel->getViewModel().get());
    }
    m_routingMapPanel->setOnNodeSelected([this](uint32_t nodeId) {
        if (m_mixerPanel) {
            auto mixerUI = m_mixerPanel->getMixerUI();
            if (mixerUI) {
                if (auto* vm = mixerUI->getViewModel()) {
                    vm->setSelectedChannelId(static_cast<int32_t>(nodeId));
                }
                if (auto* inspector = mixerUI->getInspector()) {
                    inspector->setActiveTab(AestraUI::UIMixerInspector::Tab::Sends);
                }
            }
        }
    });
    m_routingMapPanel->setOnDoubleClick([this]() {
        m_previousViewFocus = m_viewFocus;
        setViewFocus(ViewFocus::RoutingMap);
    });
    m_routingMapPanel->setOnCollapse([this]() {
        setViewFocus(m_previousViewFocus);
    });
    m_routingMapPanel->setOnRerouteMain([this](uint32_t sourceId, uint32_t targetId) {
        if (m_mixerPanel) {
            if (auto vm = m_mixerPanel->getViewModel()) {
                vm->setMainOutputDestination(sourceId, targetId);
            }
        }
    });
    m_routingMapPanel->setOnAddSend([this](uint32_t sourceId, uint32_t targetId, bool sidechainOnly) {
        if (m_mixerPanel) {
            if (auto vm = m_mixerPanel->getViewModel()) {
                vm->addSend(sourceId, targetId);
                // Set sidechain flag on the newly appended send
                if (sidechainOnly) {
                    if (auto* ch = vm->getChannelById(sourceId)) {
                        if (!ch->sends.empty()) {
                            int newIdx = static_cast<int>(ch->sends.size()) - 1;
                            vm->setSendSidechainOnly(sourceId, newIdx, true);
                        }
                    }
                }
            }
        }
    });
    m_routingMapPanel->setOnNodeMuteToggle([this](uint32_t channelId) {
        if (m_mixerPanel) {
            if (auto vm = m_mixerPanel->getViewModel()) {
                vm->toggleMute(channelId);
            }
        }
    });
    m_routingMapPanel->setOnNodeSoloToggle([this](uint32_t channelId) {
        if (m_mixerPanel) {
            if (auto vm = m_mixerPanel->getViewModel()) {
                vm->toggleSolo(channelId);
            }
        }
    });
    m_routingMapPanel->setOnRemoveSend([this](uint32_t channelId, int sendIndex) {
        if (m_mixerPanel) {
            if (auto vm = m_mixerPanel->getViewModel()) {
                vm->removeSend(channelId, sendIndex);
            }
        }
    });
    m_routingMapPanel->setOnEditSendLevel([this](uint32_t channelId, int sendIndex, float newDb) {
        if (m_mixerPanel) {
            if (auto vm = m_mixerPanel->getViewModel()) {
                float linearGain = (newDb <= -144.0f) ? 0.0f : std::pow(10.0f, newDb / 20.0f);
                vm->setSendLevel(channelId, sendIndex, linearGain);
            }
        }
    });
    m_overlayLayer->addChild(m_routingMapPanel);

    // Wire inspector minimap double-click to open full routing map
    if (m_mixerPanel) {
        if (auto mixerUI = m_mixerPanel->getMixerUI()) {
            if (auto* inspector = mixerUI->getInspector()) {
                if (auto minimap = inspector->getRoutingMap()) {
                    minimap->setOnDoubleClick([this]() {
                        m_previousViewFocus = m_viewFocus;
                        setViewFocus(ViewFocus::RoutingMap);
                    });
                }
            }
        }
    }

    m_pianoRollPanel = std::make_shared<PianoRollPanel>(m_trackManager);
    if (m_audioEngine) {
        m_pianoRollPanel->setAudioEngine(m_audioEngine);
    }
    m_pianoRollPanel->setVisible(false);

    // Musical typing: QWERTY plays the piano roll's editing unit through the
    // live-MIDI path. The target unit (tag) is captured at note-on, so
    // note-offs reach the right unit even if the editing unit changes mid-hold.
    m_keyboardNoteInput.setTagProvider([this]() -> uint64_t {
        if (m_pianoRollPanel && m_pianoRollPanel->isVisible()) {
            return static_cast<uint64_t>(m_pianoRollPanel->getEditingUnitId());
        }
        return 0;
    });
    m_keyboardNoteInput.setSink([this](uint8_t note, uint8_t velocity, bool on, uint64_t tag) {
        if (m_audioEngine && tag != 0) {
            m_audioEngine->postLiveMidiEvent(tag, on ? uint8_t{0x90} : uint8_t{0x80}, note, velocity);
        }
    });
    m_pianoRollPanel->setOnPatternEdited([this](PatternID patternId) {
        // Pattern already saved by PianoRollPanel before firing this callback.
        // Refresh every surface that can render the same pattern.
        if (m_sequencerPanel) {
            m_sequencerPanel->refreshUnits();
        }
        if (m_patternBrowser) {
            m_patternBrowser->refreshPatterns();
            m_patternBrowser->setSelectedPatternId(patternId, false);
        }
        if (m_trackManagerUI) {
            m_trackManagerUI->refreshTracks();
            m_trackManagerUI->invalidateCache();
        }
        updatePatternLoopLength(patternId);
    });
    m_pianoRollPanel->setOnClose([this]() {
        m_pianoRollPanel->savePattern();
        toggleView(Audio::ViewType::PianoRoll);
    });
    m_pianoRollPanel->setOnMaximizeToggle(
        [this](bool) { onResize(static_cast<int>(getBounds().width), static_cast<int>(getBounds().height)); });
    m_pianoRollPanel->setOnDragStart(
        [this](const AestraUI::NUIPoint& pos) { beginPanelDrag(ViewType::PianoRoll, pos); });
    m_pianoRollPanel->setOnDragMove(
        [this](const AestraUI::NUIPoint& pos) { updatePanelDrag(ViewType::PianoRoll, pos); });
    m_pianoRollPanel->setOnDragEnd([this]() { endPanelDrag(ViewType::PianoRoll); });
    m_pianoRollPanel->setMinimumPanelSize(560.0f, 280.0f);
    m_pianoRollPanel->setOnResizeMove([this](const AestraUI::NUIRect& proposed) {
        const auto allowed = computeAllowedRectForPanels();
        m_viewState.pianoRollRect = clampRectToAllowed(proposed, allowed);
        if (m_pianoRollPanel)
            m_pianoRollPanel->setBounds(m_viewState.pianoRollRect);
        setDirty(true);
    });
    m_overlayLayer->addChild(m_pianoRollPanel);

    // Wire PatternManager to UnitManager and PlaylistModel for pattern cloning
    if (m_trackManager) {
        m_trackManager->getUnitManager().setPatternManager(&m_trackManager->getPatternManager());
        m_trackManager->getPlaylistModel().setPatternManager(&m_trackManager->getPatternManager());
    }

    // Create Arsenal panel
    m_sequencerPanel = std::make_shared<ArsenalPanel>(m_trackManager);
    m_sequencerPanel->setPatternBrowser(m_patternBrowser.get());

    m_sampleEditorPanel = std::make_shared<SampleEditorPanel>(m_trackManager);
    m_sampleEditorPanel->setVisible(false);
    m_sampleEditorPanel->setOnClose([this]() {
        if (m_sampleEditorPanel) {
            m_sampleEditorPanel->setVisible(false);
        }
    });
    m_sampleEditorPanel->setOnMaximizeToggle(
        [this](bool) { onResize(static_cast<int>(getBounds().width), static_cast<int>(getBounds().height)); });
    m_sampleEditorPanel->setOnDragStart([this](const AestraUI::NUIPoint& pos) {
        if (!m_overlayLayer)
            return;
        m_sampleEditorDragging = true;
        m_sampleEditorDragStartMouseOverlay = m_overlayLayer->globalToLocal(pos);
        m_sampleEditorDragStartRect = m_sampleEditorRect;
    });
    m_sampleEditorPanel->setOnDragMove([this](const AestraUI::NUIPoint& pos) {
        if (!m_overlayLayer || !m_sampleEditorDragging || !m_sampleEditorPanel)
            return;
        const AestraUI::NUIPoint currentMouseOverlay = m_overlayLayer->globalToLocal(pos);
        const AestraUI::NUIPoint delta = currentMouseOverlay - m_sampleEditorDragStartMouseOverlay;
        AestraUI::NUIRect proposed = m_sampleEditorDragStartRect;
        proposed.x += delta.x;
        proposed.y += delta.y;
        const AestraUI::NUIRect allowed = computeAllowedRectForPanels();
        m_sampleEditorRect = clampRectToAllowed(proposed, allowed);
        m_sampleEditorPanel->setBounds(m_sampleEditorRect);
        setDirty(true);
    });
    m_sampleEditorPanel->setOnDragEnd([this]() { m_sampleEditorDragging = false; });
    m_sampleEditorPanel->setMinimumPanelSize(480.0f, 320.0f);
    m_sampleEditorPanel->setOnResizeMove([this](const AestraUI::NUIRect& proposed) {
        const auto allowed = computeAllowedRectForPanels();
        m_sampleEditorRect = clampRectToAllowed(proposed, allowed);
        if (m_sampleEditorPanel)
            m_sampleEditorPanel->setBounds(m_sampleEditorRect);
        setDirty(true);
    });
    m_sampleEditorPanel->onADSRChanged = [this](const SampleEditorPanel::ADSRParams& adsr) {
        if (!m_trackManager || !m_sampleEditorUnitId) {
            return;
        }
        auto plugin = m_trackManager->getUnitManager().getUnitPlugin(m_sampleEditorUnitId);
        auto sampler = std::dynamic_pointer_cast<Aestra::Audio::Plugins::SamplerPlugin>(plugin);
        if (!sampler) {
            return;
        }
        sampler->setEnvelope(adsr.attack, adsr.decay, adsr.sustain, adsr.release);
    };
    m_sampleEditorPanel->onLoopPointsChanged = [this](const SampleEditorPanel::LoopPoints& loopPoints) {
        if (!m_trackManager || !m_sampleEditorUnitId) {
            return;
        }
        auto plugin = m_trackManager->getUnitManager().getUnitPlugin(m_sampleEditorUnitId);
        auto sampler = std::dynamic_pointer_cast<Aestra::Audio::Plugins::SamplerPlugin>(plugin);
        if (!sampler) {
            return;
        }
        sampler->setSampleWindow(loopPoints.start, loopPoints.end);
        sampler->setLoopEnabled(loopPoints.mode != SampleEditorPanel::LoopMode::OneShot);
    };
    m_sampleEditorPanel->onPitchTuneChanged = [this](const SampleEditorPanel::PitchTune& pitchTune) {
        if (!m_trackManager || !m_sampleEditorUnitId) {
            return;
        }
        auto plugin = m_trackManager->getUnitManager().getUnitPlugin(m_sampleEditorUnitId);
        auto sampler = std::dynamic_pointer_cast<Aestra::Audio::Plugins::SamplerPlugin>(plugin);
        if (!sampler) {
            return;
        }
        sampler->setCoarseSemitones(static_cast<float>(pitchTune.coarse));
        sampler->setFineTuneCents(pitchTune.fine);
    };
    m_sampleEditorPanel->onVoiceCountChanged = [this](int voiceCount) {
        if (!m_trackManager || !m_sampleEditorUnitId) {
            return;
        }
        auto plugin = m_trackManager->getUnitManager().getUnitPlugin(m_sampleEditorUnitId);
        auto sampler = std::dynamic_pointer_cast<Aestra::Audio::Plugins::SamplerPlugin>(plugin);
        if (!sampler) {
            return;
        }
        sampler->setMaxVoices(voiceCount);
    };
    m_sampleEditorPanel->onMonoModeChanged = [this](bool monoMode) {
        if (!m_trackManager || !m_sampleEditorUnitId) {
            return;
        }
        auto plugin = m_trackManager->getUnitManager().getUnitPlugin(m_sampleEditorUnitId);
        auto sampler = std::dynamic_pointer_cast<Aestra::Audio::Plugins::SamplerPlugin>(plugin);
        if (!sampler) {
            return;
        }
        sampler->setMonoMode(monoMode);
    };
    m_sampleEditorPanel->onNormalizeRequested = [this]() {
        if (!m_trackManager || !m_sampleEditorUnitId) {
            return;
        }
        auto plugin = m_trackManager->getUnitManager().getUnitPlugin(m_sampleEditorUnitId);
        auto sampler = std::dynamic_pointer_cast<Aestra::Audio::Plugins::SamplerPlugin>(plugin);
        if (!sampler) {
            return;
        }
        sampler->normalizeSample();
    };
    m_sampleEditorPanel->onReverseRequested = [this]() {
        if (!m_trackManager || !m_sampleEditorUnitId) {
            return;
        }
        auto plugin = m_trackManager->getUnitManager().getUnitPlugin(m_sampleEditorUnitId);
        auto sampler = std::dynamic_pointer_cast<Aestra::Audio::Plugins::SamplerPlugin>(plugin);
        if (!sampler) {
            return;
        }
        sampler->reverseSample();
    };
    m_sampleEditorPanel->onControlCommitRequested = [this]() {
        if (!m_trackManager || !m_sampleEditorUnitId) {
            return;
        }
        m_trackManager->getUnitManager().captureUnitPluginState(m_sampleEditorUnitId);
        m_trackManager->markModified();
    };
    m_overlayLayer->addChild(m_sampleEditorPanel);

    // Antigravity Bindings (v3.1)
    m_sequencerPanel->setOnRequestEditor([this](UnitID id) {
        if (m_trackManager && m_pluginController) {
            auto& unitManager = m_trackManager->getUnitManager();
            auto plugin = unitManager.getUnitPlugin(id);
            if (plugin) {
                if (std::dynamic_pointer_cast<Aestra::Audio::Plugins::SamplerPlugin>(plugin)) {
                    const auto* unit = unitManager.getUnit(id);
                    if (unit) {
                        openSampleEditorForUnit(id, unit->audioClipPath);
                    }
                    return;
                }
                m_pluginController->openPluginEditor(plugin);
            }
        }
    });
    m_sequencerPanel->setOnRequestPatternEditor([this](PatternID patternId) { openPatternInPianoRoll(patternId); });

    m_sequencerPanel->setOnRequestLoadSample([this](UnitID id) {
        if (!m_fileBrowser)
            return;

        // Show browser if hidden
        if (!m_fileBrowser->isVisible()) {
            m_browserToggle->setSelectedIndex(0); // Select "Files" tab
            m_fileBrowser->setVisible(true);
            if (m_pluginBrowser)
                m_pluginBrowser->setVisible(false);
            if (m_previewPanel)
                m_previewPanel->setVisible(m_previewPanel->hasFileSelection());
            onResize(static_cast<int>(getBounds().width), static_cast<int>(getBounds().height));
        }

        // Set one-shot selection callback
        m_fileBrowser->setOnFileSelected([this, id](const AestraUI::FileItem& file) {
            // 1. Restore default behavior (Preview)
            m_fileBrowser->setOnFileSelected([this](const AestraUI::FileItem& f) {
                stopSoundPreview();
                if (m_previewPanel) {
                    m_previewPanel->setFile(&f);
                    const bool filesTabActive = !m_browserToggle || m_browserToggle->getSelectedIndex() == 0;
                    m_previewPanel->setVisible(filesTabActive && m_previewPanel->hasFileSelection());
                }
                onResize(static_cast<int>(getBounds().width), static_cast<int>(getBounds().height));
            });

            // 2. Perform Load
            AESTRA_LOG_DEBUG("Loading sample into Unit " + std::to_string(id) + ": " + file.path);
            loadSampleIntoUnitAsync(id, file.path, true);
        });
    });
    m_sequencerPanel->setOnRequestSampleEditor([this](UnitID id) {
        if (!m_trackManager) return;
        const auto* unit = m_trackManager->getUnitManager().getUnit(id);
        if (unit && !unit->audioClipPath.empty())
            openSampleEditorForUnit(id, unit->audioClipPath);
    });
    m_sequencerPanel->setOnPluginDropped([this](const std::string& pluginId) { loadInstrumentToArsenal(pluginId); });
    m_sequencerPanel->setOnPluginDroppedToUnit(
        [this](UnitID unitId, const std::string& pluginId) { loadInstrumentIntoArsenalUnit(unitId, pluginId); });
    m_sequencerPanel->setOnSampleDroppedToUnit(
        [this](UnitID unitId, const std::string& samplePath) { loadSampleIntoUnitAsync(unitId, samplePath, true); });
    m_sequencerPanel->setOnSelectedUnitChanged([this](UnitID unitId) {
        if (m_pianoRollPanel) {
            m_pianoRollPanel->setEditingUnit(unitId);
        }
        // Live hardware MIDI follows the selected unit (musical-typing
        // semantics). Every selection path — clicks, refreshes, pattern
        // switches, programmatic setSelectedUnit — fires this callback,
        // so this is the single target-tracking choke point.
        if (m_midiInput) {
            m_midiInput->setTargetUnit(unitId);
        }
        m_musicalTyping.setTargetUnit(unitId);
    });
    m_sequencerPanel->setOnPatternEdited([this](PatternID patternId) {
        if (m_patternBrowser) {
            m_patternBrowser->refreshPatterns();
            m_patternBrowser->setSelectedPatternId(patternId, false);
        }
        if (m_pianoRollPanel && m_pianoRollPanel->getCurrentPatternId() == patternId) {
            m_pianoRollPanel->loadPattern(patternId);
        }
        if (m_trackManagerUI) {
            m_trackManagerUI->refreshTracks();
            m_trackManagerUI->invalidateCache();
        }
        // Update audio engine loop length to match actual pattern length
        updatePatternLoopLength(patternId);
    });
    m_sequencerPanel->setOnActivePatternChanged([this](PatternID patternId) {
        if (!patternId.isValid()) {
            return;
        }
        const UnitID editingUnit = resolveEditingUnitForPattern(patternId);
        if (m_patternBrowser) {
            m_patternBrowser->refreshPatterns();
            m_patternBrowser->setSelectedPatternId(patternId, false);
        }
        if (m_sequencerPanel) {
            m_sequencerPanel->setSelectedUnit(editingUnit);
        }
        if (m_pianoRollPanel) {
            m_pianoRollPanel->setEditingUnit(editingUnit);
            m_pianoRollPanel->loadPattern(patternId);
        }
        // Update audio engine loop length to match actual pattern length
        updatePatternLoopLength(patternId);
    });
    m_sequencerPanel->setOnRequestPlaybackActivation([this]() {
        if (m_viewFocus != ViewFocus::Arsenal) {
            setViewFocus(ViewFocus::Arsenal);
            if (m_viewToggle) {
                m_viewToggle->setSelectedIndex(0);
            }
        } else if (m_audioEngine && m_sequencerPanel) {
            // Use actual pattern length, not step count
            double lengthBeats = getActivePatternLengthBeats();
            m_audioEngine->setPatternPlaybackMode(true, lengthBeats);
        }
    });
    m_sequencerPanel->refreshUnits();
    m_patternBrowser->refreshPatterns();
    if (const PatternID initialPattern = m_sequencerPanel->getActivePatternID(); initialPattern.isValid()) {
        const UnitID editingUnit = resolveEditingUnitForPattern(initialPattern);
        m_patternBrowser->setSelectedPatternId(initialPattern, false);
        m_sequencerPanel->setSelectedUnit(editingUnit);
        m_pianoRollPanel->setEditingUnit(editingUnit);
        m_pianoRollPanel->loadPattern(initialPattern);
    }

    m_sequencerPanel->setVisible(false);
    m_sequencerPanel->unregisterDropTargets();
    m_sequencerPanel->setOnClose([this]() { setArsenalPanelVisible(false); });
    m_sequencerPanel->setOnMaximizeToggle(
        [this](bool) { onResize(static_cast<int>(getBounds().width), static_cast<int>(getBounds().height)); });
    m_sequencerPanel->setOnDragStart(
        [this](const AestraUI::NUIPoint& pos) { beginPanelDrag(ViewType::Sequencer, pos); });
    m_sequencerPanel->setOnDragMove(
        [this](const AestraUI::NUIPoint& pos) { updatePanelDrag(ViewType::Sequencer, pos); });
    m_sequencerPanel->setOnDragEnd([this]() { endPanelDrag(ViewType::Sequencer); });
    m_sequencerPanel->setMinimumPanelSize(520.0f, 260.0f);
    m_sequencerPanel->setOnResizeMove([this](const AestraUI::NUIRect& proposed) {
        const auto allowed = computeAllowedRectForPanels();
        m_viewState.sequencerRect = clampRectToAllowed(proposed, allowed);
        if (m_sequencerPanel)
            m_sequencerPanel->setBounds(m_viewState.sequencerRect);
        setDirty(true);
    });
    m_overlayLayer->addChild(m_sequencerPanel);

    // Create History panel
    m_historyPanel = std::make_shared<Aestra::Audio::AestraHistoryPanel>(m_trackManager);
    m_historyPanel->setVisible(false);
    m_historyPanel->setOnClose([this]() { toggleHistoryPanel(); });
    m_historyPanel->setOnDragStart([this](const AestraUI::NUIPoint& pos) { beginPanelDrag(ViewType::History, pos); });
    m_historyPanel->setOnDragMove([this](const AestraUI::NUIPoint& pos) { updatePanelDrag(ViewType::History, pos); });
    m_historyPanel->setOnDragEnd([this]() { endPanelDrag(ViewType::History); });
    m_historyPanel->setMinimumPanelSize(240.0f, 220.0f);
    m_historyPanel->setOnResizeMove([this](const AestraUI::NUIRect& proposed) {
        const auto allowed = computeAllowedRectForPanels();
        m_viewState.historyRect = clampRectToAllowed(proposed, allowed);
        if (m_historyPanel)
            m_historyPanel->setBounds(m_viewState.historyRect);
        setDirty(true);
    });
    m_historyPanel->setMaximized(false);
    m_historyPanel->setOnMaximizeToggle(
        [this](bool) { onResize(static_cast<int>(getBounds().width), static_cast<int>(getBounds().height)); });
    m_historyPanel->setOnHistoryChanged([this]() {
        if (m_trackManagerUI) {
            m_trackManagerUI->refreshTracks();
            m_trackManagerUI->invalidateCache();
        }
        if (m_mixerPanel)
            m_mixerPanel->refreshChannels();
        if (m_sequencerPanel)
            m_sequencerPanel->refreshUnits();
        m_trackManager->markModified();
    });
    // NOTE: History panel is added LAST so it's on top and receives mouse events first.

    // Wire CommandHistory state-changed callback to refresh the history panel
    if (m_trackManager) {
        m_trackManager->getCommandHistory().addOnStateChanged([this]() {
            // Refresh all panels on any command state change
            if (m_mixerPanel)
                m_mixerPanel->refreshChannels();
            if (m_historyPanel) {
                m_historyPanel->refreshHistory();
            }
        });
    }

    // Transport Bar moved to early initialization (above) for Z-order fix
    // m_transportBar added to m_workspaceLayer there.

    // Create Focus Toggle Buttons
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    const auto& themeProps = theme.getCurrentTheme();
    m_viewToggle =
        std::make_shared<AestraUI::NUISegmentedControl>(std::vector<std::string>{"Arsenal", "Timeline", "Audition"});
    m_viewToggle->setCornerRadius(themeProps.radiusL);             // tokenized: 12.0
    m_viewToggle->setAccentColor(theme.getColor("primary"));        // tokenized: theme accent
    m_viewToggle->setOnSelectionChanged([this](size_t index) {
        ViewFocus newFocus;
        switch (index) {
        case 0:
            newFocus = ViewFocus::Arsenal;
            break;
        case 1:
            newFocus = ViewFocus::Timeline;
            break;
        case 2:
            newFocus = ViewFocus::Audition;
            break;
        default:
            newFocus = ViewFocus::Timeline;
            break;
        }
        setViewFocus(newFocus);

        // Auto-open Arsenal panel when switching TO Arsenal mode
        if (newFocus == ViewFocus::Arsenal) {
            setArsenalPanelVisible(true);
        }
    });

    // Initialize with Timeline mode as default (panel closed)
    m_viewToggle->setSelectedIndex(1); // Select "Timeline"
    setViewFocus(ViewFocus::Timeline);
    setArsenalPanelVisible(false);

    // History panel is now a tab in the segmented control (index 3)

    // Create compact master meters
    m_waveformVisualizer = std::make_shared<AestraUI::AudioVisualizer>();
    m_waveformVisualizer->setMode(AestraUI::AudioVisualizationMode::CompactWaveform);
    m_waveformVisualizer->setShowStereo(true);
    m_overlayLayer->addChild(m_waveformVisualizer);

    m_audioVisualizer = std::make_shared<AestraUI::AudioVisualizer>();
    m_audioVisualizer->setMode(AestraUI::AudioVisualizationMode::CompactMeter);
    m_audioVisualizer->setShowStereo(true);
    m_overlayLayer->addChild(m_audioVisualizer);

    // Add History panel LAST to overlay so it's on top and receives mouse events first
    m_overlayLayer->addChild(m_historyPanel);

    // Initialize preview engine
    m_previewEngine = std::make_unique<PreviewEngine>();
    if (m_audioEngine) {
        m_audioEngine->setPreviewEngine(m_previewEngine.get());
    }
    m_previewIsPlaying = false;
    m_previewDuration = 8.0;

    // Add Transport Bar LAST to ensure it renders on top of sidebars (Z-Order Fix)
    if (m_transportBar) {
        m_workspaceLayer->addChild(m_transportBar);
    }

    syncViewState();
}

// =============================================================================
// SECTION: Lifecycle
// =============================================================================

void AestraContent::onUpdate(double dt) {
    drainMainThreadTasks();
    updatePendingCountIn();

    // Drive preview state: clears the pending-load spinner once the decode
    // is ready, feeds playhead/duration to the panel, and stops at the end.
    // (This existed but was never called — the panel's spinner and progress
    // were frozen because nothing pumped the preview engine state.)
    updateSoundPreview();

    // Sync the mixer view-model from the engine — but only while the mixer is
    // visible and only when what it displays has actually changed. This block
    // used to run every frame unconditionally AND redundantly (refreshChannels
    // already calls syncFromEngine), so it re-dirtied the panel ~2x/frame even
    // with the mixer closed, keeping the app off idle and hot with a plugin.
    auto tm = getTrackManager();
    if (tm && m_mixerPanel && m_mixerPanel->isVisible() && m_mixerPanel->getViewModel()) {
        // Cheap fingerprint of the displayed state: structural generation plus
        // per-channel identity/state/level. Quantized levels so float noise
        // doesn't force a resync; live values (automation, drags) still flip it.
        uint64_t fp = tm->graphRebuildRequestGeneration() * 1099511628211ull;
        const size_t channelCount = tm->getChannelCount();
        fp = fp * 31 + channelCount;
        for (size_t i = 0; i < channelCount; ++i) {
            auto* ch = tm->getChannel(i);
            if (!ch) continue;
            uint64_t h = ch->getChannelId();
            h = h * 31 + ch->getColor();
            h = h * 31 + (static_cast<uint64_t>(ch->isMuted())
                          | (static_cast<uint64_t>(ch->isSoloed()) << 1)
                          | (static_cast<uint64_t>(ch->isArmed()) << 2)
                          | (static_cast<uint64_t>(ch->isMonitoringEnabled()) << 3));
            h = h * 31 + static_cast<uint64_t>(std::lround(ch->getVolume() * 1000.0f) & 0xFFFFF);
            h = h * 31 + static_cast<uint64_t>(std::lround((ch->getPan() + 1.0f) * 1000.0f) & 0xFFFFF);
            for (unsigned char c : ch->getName()) h = h * 31 + c;
            fp ^= h + 0x9e3779b97f4a7c15ull + (fp << 6) + (fp >> 2);
        }

        if (fp != m_lastMixerFingerprint) {
            m_lastMixerFingerprint = fp;
            m_mixerPanel->refreshChannels(); // calls syncFromEngine internally

            // Force refresh the rack display if we have one bound
            if (m_pluginController) {
                auto mixerUI = m_mixerPanel->getMixerUI();
                if (mixerUI) {
                    auto inspector = mixerUI->getInspector();
                    if (inspector && inspector->getEffectRack()) {
                        m_pluginController->refreshRackDisplay(inspector->getEffectRack().get());
                    }
                }
            }
        }
    }

    // Update Plugin UI Binding (Effect Rack in Inspector)
    if (m_mixerPanel && m_pluginController) {
        auto viewModel = m_mixerPanel->getViewModel();
        if (viewModel) {
            auto selectedCh = viewModel->getSelectedChannel();
            uint32_t selectedId = selectedCh ? selectedCh->id : 0xFFFFFFFFu;

            if (selectedId != m_lastSelectedChannelId) {
                m_lastSelectedChannelId = selectedId;

                // Unbind previous if any
                // (bindEffectRack currently handles one rack at a time if we reuse the pointer,
                // but let's be safe and assume we need to manage it)

                if (selectedCh) {
                    auto* channel = selectedCh->channel;
                    if (channel) {
                        auto mixerUI = m_mixerPanel->getMixerUI();
                        if (mixerUI) {
                            auto inspector = mixerUI->getInspector();
                            if (inspector && inspector->getEffectRack()) {
                                m_pluginController->bindEffectRack(inspector->getEffectRack().get(),
                                                                   &channel->getEffectChain());
                            }
                        }
                    }
                }
            }
        }
    }

    // Update Mixer Meters (Real-time) — only while the mixer is visible; meters
    // aren't shown otherwise, so smoothing them off-screen is wasted work.
    if (tm && m_mixerPanel && m_mixerPanel->isVisible()) {
        auto snapshots = tm->getMeterSnapshots();
        if (snapshots) {
            auto viewModel = m_mixerPanel->getViewModel();
            if (viewModel) {
                if (m_audioEngine) {
                    const auto source = m_audioEngine->getPreviewDuckSource();
                    std::string sourceLabel;
                    if (source == Audio::AudioEngine::PreviewDuckSource::BrowserPreview) {
                        sourceLabel = "PREVIEW";
                    } else if (source == Audio::AudioEngine::PreviewDuckSource::Audition) {
                        sourceLabel = "AUDITION";
                    } else if (source == Audio::AudioEngine::PreviewDuckSource::ArsenalPreview) {
                        sourceLabel = "ARSENAL";
                    }
                    viewModel->setPreviewDuckState(m_audioEngine->getPreviewDuckGain(), sourceLabel);
                }
                viewModel->updateMeters(*snapshots, dt);
            }
        }
    }

    if (m_audioEngine && (m_audioVisualizer || m_waveformVisualizer)) {
        const uint32_t historyFrames = m_audioEngine->getWaveformHistoryCapacity();
        if (historyFrames > 0) {
            const size_t requiredSamples = static_cast<size_t>(historyFrames) * 2;
            if (m_transportWaveformScratch.size() != requiredSamples) {
                m_transportWaveformScratch.resize(requiredSamples);
            }

            const uint32_t copiedFrames =
                m_audioEngine->copyWaveformHistory(m_transportWaveformScratch.data(), historyFrames);
            if (copiedFrames > 0) {
                if (m_waveformVisualizer) {
                    m_waveformVisualizer->setInterleavedWaveform(m_transportWaveformScratch.data(), copiedFrames);
                }
                if (m_audioVisualizer) {
                    m_audioVisualizer->setInterleavedWaveform(m_transportWaveformScratch.data(), copiedFrames);
                }
            }
        }
    }

    // Poll transport position for waveform visualizer
    if (tm && m_waveformVisualizer) {
        double pos = tm->getUIPosition();
        m_waveformVisualizer->setTransportPosition(pos);
    }

    // Update Arsenal panel progress (if visible)
    if (tm && m_sequencerPanel && m_sequencerPanel->isVisible()) {
        m_sequencerPanel->repaint();
    }

    AestraUI::NUIComponent::onUpdate(dt);
}

void AestraContent::onRender(AestraUI::NUIRenderer& renderer) {
    AestraUI::NUIRect bounds = getBounds();
    float width = bounds.width;
    float height = bounds.height;

    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    float transportHeight = layout.transportBarHeight;

    AestraUI::NUIColor textColor = themeManager.getColor("textPrimary");
    AestraUI::NUIColor accentColor = themeManager.getColor("accentCyan");
    AestraUI::NUIColor statusColor =
        m_audioActive ? themeManager.getColor("accentLime") : themeManager.getColor("error");

    AestraUI::NUIRect contentArea(bounds.x, bounds.y + transportHeight, width, height - transportHeight);
    renderer.fillRect(contentArea, themeManager.getColor("backgroundPrimary"));
    const auto contentBorder = themeManager.getColor("border");
    renderer.drawLine({contentArea.x, contentArea.y}, {contentArea.x, contentArea.bottom()}, 1.0f, contentBorder);
    renderer.drawLine({contentArea.right(), contentArea.y}, {contentArea.right(), contentArea.bottom()}, 1.0f,
                      contentBorder);
    renderer.drawLine({contentArea.x, contentArea.bottom()}, {contentArea.right(), contentArea.bottom()}, 1.0f,
                      contentBorder);

    // Resize affordance rails for docked browser dividers.
    const auto drawDividerRail = [&renderer, &themeManager](const auto& component) {
        if (!component || !component->isVisible()) {
            return;
        }
        const auto edge = component->getGlobalBounds();
        const float x = std::round(edge.right()) + 0.5f;
        const float y0 = edge.y + 12.0f;
        const float y1 = edge.bottom() - 12.0f;
        if (y1 <= y0) {
            return;
        }

        const auto base = themeManager.getColor("border").withAlpha(0.60f);
        const auto accent = themeManager.getColor("primary").withAlpha(0.52f);
        renderer.drawLine({x, y0}, {x, y1}, 1.0f, base);
        const float cy = (y0 + y1) * 0.5f;
        const AestraUI::NUIRect handle{x - 3.0f, cy - 18.0f, 7.0f, 36.0f};
        renderer.fillRoundedRect(handle, 3.5f, themeManager.getColor("backgroundSecondary").withAlpha(0.78f));
        renderer.strokeRoundedRect(handle, 3.5f, 1.0f, accent.withAlpha(0.55f));
        for (int i = -1; i <= 1; ++i) {
            const float gy = cy + static_cast<float>(i) * 7.0f;
            renderer.drawLine({x - 1.5f, gy}, {x + 1.5f, gy}, 1.0f, accent.withAlpha(0.82f));
        }
    };

    if (m_workspaceLayer && m_workspaceLayer->isVisible()) {
        m_workspaceLayer->onRender(renderer);
    }

    if (m_fileBrowser && m_fileBrowser->isVisible()) {
        drawDividerRail(m_fileBrowser);
    } else if (m_pluginBrowser && m_pluginBrowser->isVisible()) {
        drawDividerRail(m_pluginBrowser);
    }
    if (m_patternBrowser && m_patternBrowser->isVisible()) {
        drawDividerRail(m_patternBrowser);
    }

    if (m_overlayLayer && m_overlayLayer->isVisible()) {
        m_overlayLayer->onRender(renderer);
    }
}

void AestraContent::onResize(int width, int height) {
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();

    AestraUI::NUIRect contentBounds = getBounds();

    if (m_workspaceLayer)
        m_workspaceLayer->setBounds(contentBounds);
    if (m_overlayLayer)
        m_overlayLayer->setBounds(contentBounds);

    // DYNAMIC LAYOUT: Timeline-first hierarchy
    const float transportHeight = layout.transportBarHeight;
    float sidebarTopY = transportHeight; // Keep primary content aligned under transport

    bool isAuditionMode = (m_viewFocus == ViewFocus::Audition);

    if (isAuditionMode) {
        sidebarTopY = 0.0f;
    }

    // Transport Bar Positioning
    if (m_transportBar) {
        if (isAuditionMode) {
            // Hide transport bar physically (prevent mouse hits)
            m_transportBar->setBounds(AestraUI::NUIRect(0, -100, 0, 0));
        } else {
            m_transportBar->setBounds(AestraUI::NUIAbsolute(contentBounds, 0, 0, contentBounds.width, transportHeight));
        }
    }

    if (m_viewToggle) {
        // The view toggle is parented to the custom title bar (32px tall by default).
        // Center it horizontally in the window and vertically in the title bar.
        constexpr float kViewToggleWidth = 310.0f;
        constexpr float kViewToggleHeight = 24.0f;
        constexpr float kTitleBarHeight = 32.0f;
        const float yPos = std::round((kTitleBarHeight - kViewToggleHeight) * 0.5f);

        const auto rootBounds = getBounds();
        const float centerX = rootBounds.width * 0.5f;
        const float startX = std::round(centerX - kViewToggleWidth * 0.5f);

        m_viewToggle->setBounds(AestraUI::NUIRect(startX, yPos, kViewToggleWidth, kViewToggleHeight));
    }

    if (m_scopeLabel) {
        constexpr float kScopeLabelWidth = 150.0f;
        constexpr float kScopeLabelHeight = 30.0f;
        const float labelY = 15.0f;
        float labelX = 365.0f;
        if (m_viewToggle) {
            const auto toggleBounds = m_viewToggle->getBounds();
            labelX = toggleBounds.right() + 12.0f;
        }
        labelX = std::min(labelX, std::max(0.0f, contentBounds.width - kScopeLabelWidth - 10.0f));
        m_scopeLabel->setBounds(
            AestraUI::NUIAbsolute(contentBounds, labelX, labelY, kScopeLabelWidth, kScopeLabelHeight));
    }

    const float maxFileBrowserWidth = std::max(kMinFileBrowserWidth, std::min(width * 0.42f, 560.0f));
    const float computedFileBrowserWidth = std::clamp(width * 0.24f, kMinFileBrowserWidth, maxFileBrowserWidth);
    if (m_fileBrowserWidthPref <= 0.0f) {
        m_fileBrowserWidthPref = computedFileBrowserWidth;
    }

    const size_t browserTab = m_browserToggle ? m_browserToggle->getSelectedIndex() : 0;
    const auto activeNavAction =
        m_fileBrowser ? m_fileBrowser->getActiveNavAction() : AestraUI::FileBrowser::BrowserNavAction::Sounds;
    const bool legacyPatternTabActive = browserTab == 2;
    const bool patternNavActive = activeNavAction == AestraUI::FileBrowser::BrowserNavAction::Patterns;
    const bool browserPatternTabActive = legacyPatternTabActive || patternNavActive;
    const bool clipsTabActive = browserTab == 3;

    const bool compactPatternDock = m_patternBrowser && m_patternBrowser->usesCompactRail();
    const float expandedPatternWidth = std::clamp(width * 0.11f, 152.0f, 208.0f);
    const float compactPatternWidth = std::clamp(width * 0.070f, kMinPatternBrowserWidth, 124.0f);
    const float computedPatternBrowserWidth = compactPatternDock ? compactPatternWidth : expandedPatternWidth;
    if (m_patternBrowserWidthPref <= 0.0f) {
        m_patternBrowserWidthPref = computedPatternBrowserWidth;
    }

    const bool patternRailVisible =
        m_patternBrowser && m_patternBrowser->isVisible() && !browserPatternTabActive && !clipsTabActive;
    const float minPatternWidth = patternRailVisible ? kMinPatternBrowserWidth : 0.0f;
    const float maxPatternWidth =
        patternRailVisible ? std::max(minPatternWidth, std::min(width * 0.24f, 280.0f)) : 0.0f;
    const float maxFileByTrack = std::max(kMinFileBrowserWidth, width - kMinTrackAreaWidth - minPatternWidth);
    const float fileBrowserWidth =
        std::clamp(m_fileBrowserWidthPref, kMinFileBrowserWidth, std::min(maxFileBrowserWidth, maxFileByTrack));
    const float maxPatternByTrack = std::max(minPatternWidth, width - kMinTrackAreaWidth - fileBrowserWidth);
    const float patternBrowserWidth = patternRailVisible ? std::clamp(m_patternBrowserWidthPref, minPatternWidth,
                                                                      std::min(maxPatternWidth, maxPatternByTrack))
                                                         : 0.0f;

    m_fileBrowserWidthPref = fileBrowserWidth;
    if (patternRailVisible) {
        m_patternBrowserWidthPref = patternBrowserWidth;
    }

    const bool filesTabActive = browserTab == 0 && !browserPatternTabActive;
    const bool pluginBrowserVisible = m_pluginBrowser && m_pluginBrowser->isVisible();
    const bool showPreviewDock = m_previewPanel && filesTabActive && m_fileBrowser && m_fileBrowser->isVisible() &&
                                 !pluginBrowserVisible && m_previewPanel->hasFileSelection();
    if (m_previewPanel) {
        m_previewPanel->setVisible(showPreviewDock);
    }

    // Browser toggle removed - browser panes now start directly at sidebarTopY without toggle offset

    if (m_fileBrowser) {
        float fbTop = sidebarTopY;
        float fbHeight = height - fbTop;

        if (showPreviewDock) {
            // Full browser width: the dock previously started at the nav-pane
            // edge, leaving an unpainted (black) strip under the nav pane.
            const float previewHeight = 68.0f;
            fbHeight -= previewHeight;
            m_previewPanel->setBounds(
                AestraUI::NUIAbsolute(contentBounds, 0, fbTop + fbHeight, fileBrowserWidth, previewHeight));
        }

        m_fileBrowser->setBounds(AestraUI::NUIAbsolute(contentBounds, 0, fbTop, fileBrowserWidth, fbHeight));
    }

    if (m_pluginBrowser) {
        bool isPlugins = m_fileBrowser && m_fileBrowser->getActiveNavAction() == AestraUI::FileBrowser::BrowserNavAction::Plugins;
        if (isPlugins && m_fileBrowser->isVisible()) {
            float navW = m_fileBrowser->getNavPaneWidth();
            float pbLeft = navW;
            float pbWidth = std::max(0.0f, fileBrowserWidth - navW);
            // Align with file browser content area (below search bar)
            constexpr float searchH = 28.0f;
            float pbTop = sidebarTopY + searchH;
            float pbHeight = height - pbTop;
            m_pluginBrowser->setBounds(AestraUI::NUIAbsolute(contentBounds, pbLeft, pbTop - contentBounds.y, pbWidth, pbHeight));
            m_pluginBrowser->setVisible(true);
        } else {
            float pbTop = sidebarTopY;
            float pbHeight = height - pbTop;
            m_pluginBrowser->setBounds(AestraUI::NUIAbsolute(contentBounds, 0, pbTop - contentBounds.y, fileBrowserWidth, pbHeight));
            m_pluginBrowser->setVisible(false);
        }
    }

    if (m_patternBrowser) {
        if (legacyPatternTabActive || clipsTabActive) {
            float clipTop = sidebarTopY;
            m_patternBrowser->setBounds(
                AestraUI::NUIAbsolute(contentBounds, 0.0f, clipTop, fileBrowserWidth, height - clipTop));
        } else if (patternNavActive && m_fileBrowser) {
            const float navW = m_fileBrowser->getNavPaneWidth();
            constexpr float searchH = 28.0f;
            const float patternTop = sidebarTopY + searchH;
            const float patternWidth = std::max(0.0f, fileBrowserWidth - navW);
            m_patternBrowser->setBounds(
                AestraUI::NUIAbsolute(contentBounds, navW, patternTop, patternWidth, height - patternTop));
        } else {
            float patternBrowserX = fileBrowserWidth;
            float pbY = isAuditionMode ? 0.0f : transportHeight;
            float pbHeight = height - pbY;

            m_patternBrowser->setBounds(
                AestraUI::NUIAbsolute(contentBounds, patternBrowserX, pbY, patternBrowserWidth, pbHeight));
        }
    }

    if (m_audioVisualizer || m_waveformVisualizer) {
        const float meterWidth = 60.0f;
        const float waveformWidth = 150.0f;
        const float visualizerHeight = 40.0f;
        const float gap = 6.0f;
        float vuY = (transportHeight - visualizerHeight) / 2.0f;

        float totalWidth = meterWidth;
        if (m_waveformVisualizer)
            totalWidth += waveformWidth + gap;

        float xStart = width - totalWidth - layout.panelMargin;
        if (m_waveformVisualizer) {
            m_waveformVisualizer->setBounds(
                AestraUI::NUIAbsolute(contentBounds, xStart, vuY, waveformWidth, visualizerHeight));
            xStart += waveformWidth + gap;
        }
        if (m_audioVisualizer) {
            m_audioVisualizer->setBounds(
                AestraUI::NUIAbsolute(contentBounds, xStart, vuY, meterWidth, visualizerHeight));
        }
    }

    if (m_trackManagerUI) {
        float trackAreaX = fileBrowserWidth + patternBrowserWidth;
        float trackAreaWidth = width - trackAreaX;
        float trackAreaHeight = height - transportHeight;
        m_trackManagerUI->setBounds(
            AestraUI::NUIAbsolute(contentBounds, trackAreaX, transportHeight, trackAreaWidth, trackAreaHeight));

        // AuditionPanel uses the same content area BUT ignores transport height
        if (m_auditionPanel) {
            float auditionTop = 0; // Full height
            float auditionHeight = height;

            // Keep file browser visible (it's on the left)
            // But Audition panel should probably start AFTER file browser

            // IMPORTANT: If PatternBrowser is hidden (width=0), fileBrowserWidth is the only offset.
            // patternBrowserWidth is calculated above as 0 if m_patternBrowser is nullptr or hidden?
            // Wait, lines 634 checks 'if (m_patternBrowser)'. I need to ensure it accounts for visibility.

            // Let's rely on the previous calculations. If I hid PatternBrowser in setViewFocus,
            // does onResize know?
            // Lines 634-639 calculate patternBrowserWidth based on m_patternBrowser existence, NOT visibility.
            // I should modify that block first or override here.

            // Overriding for Audition Mode
            if (m_viewFocus == ViewFocus::Audition) {
                m_auditionPanel->setBounds(
                    AestraUI::NUIAbsolute(contentBounds, fileBrowserWidth, 0, width - fileBrowserWidth, height));
            } else {
                m_auditionPanel->setBounds(
                    AestraUI::NUIAbsolute(contentBounds, trackAreaX, transportHeight, trackAreaWidth, trackAreaHeight));
            }
        }
    }

    AestraUI::NUIRect allowed = computeAllowedRectForPanels();
    AestraUI::NUIRect maxRect = computeMaximizedRect();

    if (m_mixerPanel && m_mixerPanel->isVisible()) {
        if (m_mixerPanel->isMaximized()) {
            m_mixerPanel->setBounds(maxRect);
        } else {
            m_viewState.mixerRect = clampRectToAllowed(m_viewState.mixerRect, allowed);
            m_mixerPanel->setBounds(m_viewState.mixerRect);
        }
    }

    if (m_routingMapPanel && m_routingMapPanel->isVisible()) {
        m_routingMapPanel->setBounds(allowed);
    }

    if (m_pianoRollPanel && m_pianoRollPanel->isVisible()) {
        if (m_pianoRollPanel->isMaximized()) {
            m_pianoRollPanel->setBounds(maxRect);
        } else {
            m_viewState.pianoRollRect = clampRectToAllowed(m_viewState.pianoRollRect, allowed);
            m_pianoRollPanel->setBounds(m_viewState.pianoRollRect);
        }
    }

    if (m_sequencerPanel && m_sequencerPanel->isVisible()) {
        if (m_sequencerPanel->isMaximized()) {
            m_sequencerPanel->setBounds(maxRect);
        } else {
            m_viewState.sequencerRect = clampRectToAllowed(m_viewState.sequencerRect, allowed);
            m_sequencerPanel->setBounds(m_viewState.sequencerRect);
        }
    }

    if (m_historyPanel && m_historyPanel->isVisible()) {
        if (m_historyPanel->isMaximized()) {
            m_historyPanel->setBounds(maxRect);
        } else {
            m_viewState.historyRect = clampRectToAllowed(m_viewState.historyRect, allowed);
            m_historyPanel->setBounds(m_viewState.historyRect);
        }
    }

    if (m_sampleEditorPanel && m_sampleEditorPanel->isVisible()) {
        if (m_sampleEditorRect.x == 0.0f && m_sampleEditorRect.y == 0.0f) {
            m_sampleEditorRect.width = std::min(640.0f, allowed.width);
            m_sampleEditorRect.height = std::min(430.0f, allowed.height);
            m_sampleEditorRect.x = allowed.x + (allowed.width - m_sampleEditorRect.width) * 0.5f;
            m_sampleEditorRect.y = allowed.y + (allowed.height - m_sampleEditorRect.height) * 0.5f;
        }
        m_sampleEditorRect = clampRectToAllowed(m_sampleEditorRect, allowed);
        m_sampleEditorPanel->setBounds(m_sampleEditorRect);
    }

    if (m_workspaceLayer) {
        m_workspaceLayer->setBounds(NUIRect(0, 0, static_cast<float>(width), static_cast<float>(height)));
    }
    if (m_overlayLayer) {
        m_overlayLayer->setBounds(NUIRect(0, 0, static_cast<float>(width), static_cast<float>(height)));
    }

    AestraUI::NUIComponent::onResize(width, height);
}

bool AestraContent::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    if (!isVisible() || !isEnabled()) {
        return false;
    }

    if (!m_browserResizing && event.pressed && m_overlayLayer && m_overlayLayer->onMouseEvent(event)) {
        return true;
    }

    if (m_browserResizing) {
        if (event.released && event.button == AestraUI::NUIMouseButton::Left) {
            m_browserResizing = false;
            m_browserResizeTarget = BrowserResizeTarget::None;
            return true;
        }

        if (event.button == AestraUI::NUIMouseButton::None) {
            updateBrowserResizeDrag(event.position);
            return true;
        }

        return true;
    }

    if (event.pressed && event.button == AestraUI::NUIMouseButton::Left) {
        const BrowserResizeTarget target = hitTestBrowserResizeTarget(event.position);
        if (target != BrowserResizeTarget::None) {
            m_browserResizing = true;
            m_browserResizeTarget = target;
            m_browserResizeStartX = event.position.x;
            m_browserResizeStartFileWidth = std::max(kMinFileBrowserWidth, m_fileBrowserWidthPref);
            m_browserResizeStartPatternWidth = std::max(kMinPatternBrowserWidth, m_patternBrowserWidthPref);
            return true;
        }
    }

    return AestraUI::NUIComponent::onMouseEvent(event);
}

// =============================================================================
// SECTION: View Management
// =============================================================================

void AestraContent::setViewOpen(Audio::ViewType view, bool open) {
    bool changed = false;
    switch (view) {
    case Audio::ViewType::Mixer:
        if (m_viewState.mixerOpen != open) {
            m_viewState.mixerOpen = open;
            if (m_mixerPanel) {
                m_mixerPanel->setVisible(open);
                if (open)
                    m_mixerPanel->refreshChannels();
            }
            changed = true;
        }
        break;
    case Audio::ViewType::PianoRoll:
        if (m_viewState.pianoRollOpen != open) {
            m_viewState.pianoRollOpen = open;
            if (m_pianoRollPanel) {
                m_pianoRollPanel->setVisible(open);
                if (open)
                    m_pianoRollPanel->bringToFront();
            }
            changed = true;
        }
        break;
    case Audio::ViewType::Sequencer:
        if (m_viewState.sequencerOpen != open) {
            m_viewState.sequencerOpen = open;
            if (m_sequencerPanel) {
                m_sequencerPanel->setVisible(open);
                if (open) {
                    m_sequencerPanel->registerDropTargets(true);
                    m_sequencerPanel->bringToFront();
                } else {
                    m_sequencerPanel->unregisterDropTargets();
                }
            }
            changed = true;
        }
        break;
    case Audio::ViewType::Playlist:
        if (m_viewState.playlistActive != open) {
            m_viewState.playlistActive = open;
            if (m_trackManagerUI)
                m_trackManagerUI->setVisible(open);
            changed = true;
        }
        break;
    }

    if (changed) {
        AESTRA_LOG_DEBUG("[ViewState] View changed: " + std::to_string(static_cast<int>(view)) + " -> " +
                         (open ? "OPEN" : "CLOSED"));
        syncViewState();

        if (open) {
            AestraUI::NUIRect allowed = computeAllowedRectForPanels();
            switch (view) {
            case Audio::ViewType::Mixer:
                m_viewState.mixerRect = clampRectToAllowed(m_viewState.mixerRect, allowed);
                if (m_mixerPanel)
                    m_mixerPanel->setBounds(m_viewState.mixerRect);
                break;
            case Audio::ViewType::PianoRoll:
                m_viewState.pianoRollRect = clampRectToAllowed(m_viewState.pianoRollRect, allowed);
                if (m_pianoRollPanel)
                    m_pianoRollPanel->setBounds(m_viewState.pianoRollRect);
                break;
            case Audio::ViewType::Sequencer:
                m_viewState.sequencerRect = clampRectToAllowed(m_viewState.sequencerRect, allowed);
                if (m_sequencerPanel) {
                    m_sequencerPanel->setBounds(m_viewState.sequencerRect);
                    m_sequencerPanel->registerDropTargets(true);
                }
                break;
            default:
                break;
            }
        }

        onResize(static_cast<int>(getBounds().width), static_cast<int>(getBounds().height));
        setDirty(true);
    }
}

void AestraContent::toggleView(Audio::ViewType view) {
    if (view == Audio::ViewType::Sequencer) {
        // Toggle panel visibility only, don't change mode
        toggleArsenalPanel();
        return;
    }

    bool current = false;
    switch (view) {
    case Audio::ViewType::Mixer:
        current = m_viewState.mixerOpen;
        break;
    case Audio::ViewType::PianoRoll:
        current = m_viewState.pianoRollOpen;
        break;
    case Audio::ViewType::Playlist:
        current = m_viewState.playlistActive;
        break;
    default:
        break;
    }
    setViewOpen(view, !current);
}

void AestraContent::toggleFileBrowser() {
    if (m_fileBrowser) {
        bool isVisible = m_fileBrowser->isVisible();
        m_fileBrowser->setVisible(!isVisible);
        if (m_pluginBrowser) {
            bool showPlugins = !isVisible && m_fileBrowser->getActiveNavAction() == AestraUI::FileBrowser::BrowserNavAction::Plugins;
            m_pluginBrowser->setVisible(showPlugins);
        }
        AESTRA_LOG_DEBUG("File Browser toggled: " + std::string(!isVisible ? "VISIBLE" : "HIDDEN"));
        onResize(static_cast<int>(getBounds().width), static_cast<int>(getBounds().height));
    }
}

void AestraContent::syncViewState() {
    if (m_transportBar) {
        m_transportBar->setViewToggled(Audio::ViewType::Mixer, m_viewState.mixerOpen);
        m_transportBar->setViewToggled(Audio::ViewType::PianoRoll, m_viewState.pianoRollOpen);
        m_transportBar->setViewToggled(Audio::ViewType::Sequencer, m_viewState.sequencerOpen);
        m_transportBar->setViewToggled(Audio::ViewType::Playlist, m_viewState.playlistActive);
    }
}

// =============================================================================
// SECTION: Panel State Persistence (Issue #120)
// =============================================================================

float AestraContent::getBrowserWidth() const {
    if (m_fileBrowserWidthPref > 0.0f) {
        return m_fileBrowserWidthPref;
    }
    if (m_fileBrowser) {
        return m_fileBrowser->getBounds().width;
    }
    return 0.0f;
}

void AestraContent::setBrowserWidth(float width) {
    if (width > 0.0f) {
        m_fileBrowserWidthPref = width;
        onResize(static_cast<int>(getBounds().width), static_cast<int>(getBounds().height));
    }
}

bool AestraContent::isBrowserVisible() const {
    return m_fileBrowser ? m_fileBrowser->isVisible() : false;
}

void AestraContent::setBrowserVisible(bool visible) {
    if (m_fileBrowser) {
        bool currentlyVisible = m_fileBrowser->isVisible();
        if (currentlyVisible != visible) {
            m_fileBrowser->setVisible(visible);
            // Also update plugin browser and preview panel visibility
            if (m_pluginBrowser) {
                if (!visible) {
                    m_pluginBrowser->setVisible(false);
                } else {
                    bool showPlugins = m_fileBrowser->getActiveNavAction() == AestraUI::FileBrowser::BrowserNavAction::Plugins;
                    m_pluginBrowser->setVisible(showPlugins);
                }
            }
            if (m_previewPanel) {
                m_previewPanel->setVisible(visible && m_fileBrowser->getActiveNavAction() != AestraUI::FileBrowser::BrowserNavAction::Plugins && m_previewPanel->hasFileSelection());
            }
            onResize(static_cast<int>(getBounds().width), static_cast<int>(getBounds().height));
            AESTRA_LOG_DEBUG("[PanelState] Browser visibility set to: " + std::string(visible ? "VISIBLE" : "HIDDEN"));
        }
    }
}

bool AestraContent::isMixerVisible() const {
    return m_mixerPanel ? m_mixerPanel->isVisible() : false;
}

void AestraContent::setMixerVisible(bool visible) {
    setViewOpen(Audio::ViewType::Mixer, visible);
}

void AestraContent::setViewFocus(ViewFocus focus) {
    // Guard against re-entrancy (setSelectedIndex triggers callback which calls setViewFocus)
    static bool isUpdating = false;
    if (isUpdating)
        return;
    isUpdating = true;

    bool wasPlaying = (m_transportBar && m_transportBar->getState() == TransportState::Playing);
    ViewFocus previousFocus = m_viewFocus;

    m_viewFocus = focus;

    auto applyOverlayPanelVisibility = [this](bool auditionMode) {
        if (auditionMode) {
            if (m_mixerPanel)
                m_mixerPanel->setVisible(false);
            if (m_pianoRollPanel)
                m_pianoRollPanel->setVisible(false);
            if (m_sequencerPanel) {
                m_sequencerPanel->setVisible(false);
                m_sequencerPanel->unregisterDropTargets();
            }
            return;
        }

        if (m_mixerPanel)
            m_mixerPanel->setVisible(m_viewState.mixerOpen);
        if (m_pianoRollPanel)
            m_pianoRollPanel->setVisible(m_viewState.pianoRollOpen);
        if (m_sequencerPanel) {
            m_sequencerPanel->setVisible(m_viewState.sequencerOpen);
            if (m_viewState.sequencerOpen) {
                m_sequencerPanel->registerDropTargets(true);
            } else {
                m_sequencerPanel->unregisterDropTargets();
            }
        }
    };

    // Handle mode transitions
    if (m_audioEngine) {
        // === ENTERING ARSENAL ===
        if (focus == ViewFocus::Arsenal) {
            stopPatternClipPreview(false);
            // Use actual pattern length from the active pattern
            double lengthBeats = getActivePatternLengthBeats();

            // Save current timeline position before switching context
            if (m_trackManager) {
                m_savedTimelinePosition = m_trackManager->getPosition();
            }

            m_audioEngine->setPatternPlaybackMode(true, lengthBeats);
            m_audioEngine->setAuditionModeEnabled(false);

            // UI: Hide timeline playhead and FREEZE usage
            if (m_trackManagerUI) {
                m_trackManagerUI->setPatternMode(true);
                m_trackManagerUI->setFollowPlayhead(false); // Stop scrolling
            }

            // Hide Audition panel if it exists (returning from Audition)
            if (m_auditionPanel)
                m_auditionPanel->setVisible(false);
            if (m_trackManagerUI)
                m_trackManagerUI->setVisible(true);
        }
        // === ENTERING TIMELINE ===
        else if (focus == ViewFocus::Timeline) {
            stopPatternClipPreview(false);
            // Stop any running playback from previous mode
            if (previousFocus == ViewFocus::Arsenal) {
                if (m_trackManager && m_trackManager->isPatternMode()) {
                    m_trackManager->stopArsenalPlayback(false);
                } else if (m_trackManager && m_trackManager->isPlaying()) {
                    m_trackManager->stop();
                }
                m_audioEngine->panic(); // Kill all voices/ring-outs
            }

            // Restore playback position
            m_audioEngine->setPatternPlaybackMode(false, 4.0);
            // Reinstall the owner-bound preview callback after returning to timeline mode. The destructor clears it.
            if (m_trackManager) {
                m_trackManager->setStopPreviewCallback([this]() { stopSoundPreview(); });
            }

            m_audioEngine->setAuditionModeEnabled(false);

            if (m_trackManager) {
                m_trackManager->setPosition(m_savedTimelinePosition);
                m_trackManager->setPlayStartPosition(m_savedTimelinePosition);
            }

            if (m_trackManagerUI) {
                m_trackManagerUI->setPatternMode(false);
                m_trackManagerUI->setFollowPlayhead(true); // Resume scrolling
            }

            // Hide Audition panel if it exists (returning from Audition)
            if (m_auditionPanel)
                m_auditionPanel->setVisible(false);
            if (m_trackManagerUI)
                m_trackManagerUI->setVisible(true);
        }
        // === ENTERING AUDITION ===
        else if (focus == ViewFocus::Audition) {
            stopPatternClipPreview(false);
            // Stop main DAW playback - Audition has its own engine
            if (m_trackManager && m_trackManager->isPatternMode()) {
                m_trackManager->stopArsenalPlayback(false);
            } else if (m_trackManager && m_trackManager->isPlaying()) {
                m_trackManager->stop();
            }
            m_audioEngine->panic(); // Silence all DAW audio
            m_audioEngine->setPatternPlaybackMode(false, 4.0);

            // Stop any file browser preview
            stopSoundPreview();
            if (m_previewEngine)
                m_previewEngine->stop();

            // Save timeline position for when we return
            if (m_trackManager && previousFocus == ViewFocus::Timeline) {
                m_savedTimelinePosition = m_trackManager->getPosition();
            }

            if (m_trackManagerUI) {
                m_trackManagerUI->setPatternMode(false);
                m_trackManagerUI->setFollowPlayhead(false); // Freeze timeline
            }

            // === CREATE AUDITION PANEL (lazy init) ===
            if (!m_auditionEngine) {
                m_auditionEngine = std::make_shared<Audio::AuditionEngine>();
            }

            // ALWAYS ensure it's registered with the main engine
            m_audioEngine->setAuditionEngine(m_auditionEngine.get());

            // Sync sample rate from audio engine to audition engine
            m_auditionEngine->setSampleRate(static_cast<double>(m_audioEngine->getSampleRate()));

            // Enable Exclusive Audition Mode (bypasses main DAW graph)
            m_audioEngine->setAuditionModeEnabled(true);

            if (!m_auditionPanel) {
                m_auditionPanel = std::make_shared<AuditionPanel>(m_auditionEngine);
                m_auditionPanel->setOnPlayRequest([this]() { stopSoundPreview(); });
                m_auditionPanel->setOnActiveTrackPathChanged([this](const std::string& path) {
                    if (m_fileBrowser) {
                        m_fileBrowser->setActivePlaybackPath(path);
                    }
                });
                if (m_fileBrowser) {
                    std::string activePath;
                    if (m_auditionEngine && m_auditionEngine->isPlaying()) {
                        auto current = m_auditionEngine->getCurrentItem();
                        if (current)
                            activePath = current->filePath;
                    }
                    m_fileBrowser->setActivePlaybackPath(activePath);
                }
                // Fix Z-Order: Add to workspace layer (below overlay/transport), not root
                if (m_workspaceLayer) {
                    m_workspaceLayer->addChild(m_auditionPanel);
                } else {
                    addChild(m_auditionPanel);
                }
            }

            // Show the Audition panel, hide DAW panels
            m_auditionPanel->setVisible(true);
            if (m_trackManagerUI)
                m_trackManagerUI->setVisible(false);
            applyOverlayPanelVisibility(true);

            // POLISH: Hide Transport, Pattern Browser, and Visualizers for immersion
            if (m_transportBar)
                m_transportBar->setVisible(false);
            if (m_patternBrowser)
                m_patternBrowser->setVisible(false);
            if (m_waveformVisualizer)
                m_waveformVisualizer->setVisible(false);
            if (m_audioVisualizer)
                m_audioVisualizer->setVisible(false);

            AESTRA_LOG_DEBUG("[ViewFocus] Entering Audition Mode");
        }
        // === ENTERING ROUTING MAP ===
        else if (focus == ViewFocus::RoutingMap) {
            // Routing map is an overlay; preserve existing DAW state
            if (m_routingMapPanel) {
                AestraUI::NUIRect allowed = computeAllowedRectForPanels();
                m_routingMapPanel->setBounds(allowed);
                m_routingMapPanel->setVisible(true);
                m_routingMapPanel->bringToFront();
                m_routingMapPanel->setDirty(true);
            }
            // Keep mixer visible so user still sees context
            if (m_mixerPanel && m_viewState.mixerOpen) {
                m_mixerPanel->setVisible(true);
            }
            AESTRA_LOG_DEBUG("[ViewFocus] Entering Routing Map");
        }

        // GLOBAL VISIBILITY STATE MANAGEMENT
        // Ensure UI elements are correctly shown/hidden based on mode
        bool isAudition = (focus == ViewFocus::Audition);
        bool isRoutingMap = (focus == ViewFocus::RoutingMap);

        if (m_auditionPanel)
            m_auditionPanel->setVisible(isAudition);

        if (m_routingMapPanel) {
            if (isRoutingMap) {
                m_routingMapPanel->setVisible(true);
            } else if (previousFocus == ViewFocus::RoutingMap) {
                m_routingMapPanel->setVisible(false);
            }
        }

        if (!isAudition && !isRoutingMap) {
            // Returning to DAW Mode - Restore UI
            if (m_transportBar)
                m_transportBar->setVisible(true);
            const size_t browserTab = m_browserToggle ? m_browserToggle->getSelectedIndex() : 0;
            const bool patternNavActive =
                m_fileBrowser && m_fileBrowser->getActiveNavAction() == AestraUI::FileBrowser::BrowserNavAction::Patterns;
            const bool showBrowserPatternPane =
                focus == ViewFocus::Timeline && (browserTab == 2 || browserTab == 3 || patternNavActive);
            if (m_patternBrowser)
                m_patternBrowser->setVisible(showBrowserPatternPane);
            if (m_waveformVisualizer)
                m_waveformVisualizer->setVisible(true);
            if (m_audioVisualizer)
                m_audioVisualizer->setVisible(true);
            applyOverlayPanelVisibility(false);
        } else if (isAudition) {
            // Audition Mode - Hide Distractions
            if (m_transportBar)
                m_transportBar->setVisible(false);
            if (m_patternBrowser)
                m_patternBrowser->setVisible(false);
            if (m_waveformVisualizer)
                m_waveformVisualizer->setVisible(false);
            if (m_audioVisualizer)
                m_audioVisualizer->setVisible(false);
            applyOverlayPanelVisibility(true);
        }

        // Sync segment control to reflect the new focus
        if (m_viewToggle) {
            size_t idx = 0;
            if (focus == ViewFocus::Arsenal) idx = 0;
            else if (focus == ViewFocus::Timeline) idx = 1;
            else if (focus == ViewFocus::Audition) idx = 2;
            // RoutingMap doesn't map to a toggle segment; leave prior selection
            if (focus != ViewFocus::RoutingMap) {
                m_viewToggle->setSelectedIndex(idx);
            }
        }

        // Force layout update immediately to apply new visibility and margins
        onResize(getBounds().width, getBounds().height);
    }

    // Update transport bar mode indicator (mode only, no panel visibility)
    if (m_transportBar) {
        m_transportBar->setViewToggled(Audio::ViewType::Sequencer, focus == ViewFocus::Arsenal);
    }

    // Hot-swap playback if needed (only for Arsenal/Timeline swap, not Audition or RoutingMap)
    bool isRoutingMapTransition = (focus == ViewFocus::RoutingMap || previousFocus == ViewFocus::RoutingMap);
    if (wasPlaying && m_transportBar && !isRoutingMapTransition &&
        focus != ViewFocus::Audition && previousFocus != ViewFocus::Audition) {
        AESTRA_LOG_DEBUG("[Focus] Hot-swapping playback mode");
        m_transportBar->stop();
        m_transportBar->play();
    }

    isUpdating = false;
}

void AestraContent::setArsenalPanelVisible(bool visible) {
    if (!m_sequencerPanel)
        return;

    const bool wasVisible = m_sequencerPanel->isVisible();
    if (wasVisible == visible && m_viewState.sequencerOpen == visible) {
        return;
    }

    if (visible) {
        // Calculate initial position on first show (if position is at origin)
        if (m_viewState.sequencerRect.x == 0 && m_viewState.sequencerRect.y == 0) {
            AestraUI::NUIRect safe = computeSafeRect();
            // Position below title bar with some margin
            float titleBarHeight = 35.0f;
            float margin = 10.0f;
            m_viewState.sequencerRect.x = margin;
            m_viewState.sequencerRect.y = titleBarHeight + safe.y + margin;

            // Clamp to allowed area
            AestraUI::NUIRect allowed = computeAllowedRectForPanels();
            m_viewState.sequencerRect = clampRectToAllowed(m_viewState.sequencerRect, allowed);
        }

        m_viewState.sequencerRect = clampRectToAllowed(m_viewState.sequencerRect, computeAllowedRectForPanels());
        m_sequencerPanel->setVisible(true);
        m_sequencerPanel->setBounds(m_viewState.sequencerRect);
        m_sequencerPanel->refreshUnits();
        m_sequencerPanel->registerDropTargets(true);
    } else {
        m_sequencerPanel->setVisible(false);
        m_sequencerPanel->unregisterDropTargets();
    }

    m_viewState.sequencerOpen = visible;
    syncViewState();
    onResize(static_cast<int>(getBounds().width), static_cast<int>(getBounds().height));
    setDirty(true);
}

void AestraContent::toggleArsenalPanel() {
    setArsenalPanelVisible(!m_viewState.sequencerOpen);
}

// =============================================================================
// SECTION: Panel Physics & Constraints
// =============================================================================

AestraUI::NUIRect AestraContent::computeSafeRect() const {
    AestraUI::NUIRect bounds = getBounds();

    float transportHeight = AestraUI::NUIThemeManager::getInstance().getLayoutDimensions().transportBarHeight;
    if (m_transportBar)
        transportHeight = m_transportBar->getHeight();

    AestraUI::NUIRect safe = bounds;
    safe.y += transportHeight;
    safe.height -= transportHeight;

    float padding = 0.0f;
    safe.x += padding;
    safe.y += padding;
    safe.width -= (padding * 2);
    safe.height -= (padding * 2);

    return safe;
}

float AestraContent::getVisibleBrowserEdge() const {
    float browserEdge = 0.0f;

    if (m_fileBrowser && m_fileBrowser->isVisible()) {
        browserEdge = std::max(browserEdge, m_fileBrowser->getBounds().right());
    }

    if (m_pluginBrowser && m_pluginBrowser->isVisible()) {
        browserEdge = std::max(browserEdge, m_pluginBrowser->getBounds().right());
    }

    return browserEdge;
}

AestraUI::NUIRect AestraContent::computeAllowedRectForPanels() const {
    AestraUI::NUIRect safe = computeSafeRect();

    float browserEdge = getVisibleBrowserEdge();
    if (browserEdge > safe.x) {
        float shift = browserEdge - safe.x;
        safe.x = browserEdge;
        safe.width -= shift;
    }

    if (safe.width < 100.0f)
        safe.width = 100.0f;
    if (safe.height < 100.0f)
        safe.height = 100.0f;

    return safe;
}

AestraUI::NUIRect AestraContent::computeMaximizedRect() const {
    AestraUI::NUIRect bounds = getBounds();

    float transportHeight = AestraUI::NUIThemeManager::getInstance().getLayoutDimensions().transportBarHeight;
    if (m_transportBar)
        transportHeight = m_transportBar->getHeight();

    AestraUI::NUIRect maxRect = bounds;
    maxRect.y += transportHeight;
    maxRect.height -= transportHeight;

    float browserEdge = getVisibleBrowserEdge();
    if (browserEdge > maxRect.x) {
        float shift = browserEdge - maxRect.x;
        maxRect.x = browserEdge;
        maxRect.width -= shift;
    }

    if (maxRect.width < 100.0f)
        maxRect.width = 100.0f;
    if (maxRect.height < 100.0f)
        maxRect.height = 100.0f;

    return maxRect;
}

AestraUI::NUIRect AestraContent::clampRectToAllowed(AestraUI::NUIRect panel, const AestraUI::NUIRect& allowed) const {
    // Clamp panel size to allowed area first, so bounds are valid for position clamp
    float w = std::max(100.0f, panel.width);
    float h = std::max(100.0f, panel.height);
    if (w > allowed.width)
        w = allowed.width;
    if (h > allowed.height)
        h = allowed.height;

    float x = std::clamp(panel.x, allowed.x, std::max(allowed.x, allowed.right() - w));
    float y = std::clamp(panel.y, allowed.y, std::max(allowed.y, allowed.bottom() - h));

    x = std::clamp(x, allowed.x, std::max(allowed.x, allowed.right() - w));
    y = std::clamp(y, allowed.y, std::max(allowed.y, allowed.bottom() - h));

    return AestraUI::NUIRect(x, y, w, h);
}

AestraContent::BrowserResizeTarget
AestraContent::hitTestBrowserResizeTarget(const AestraUI::NUIPoint& mouseScreen) const {
    const auto isNearRightEdge = [&mouseScreen](const auto& component) {
        if (!component || !component->isVisible()) {
            return false;
        }
        const auto global = component->getGlobalBounds();
        if (mouseScreen.y < global.y || mouseScreen.y > global.bottom()) {
            return false;
        }
        return std::abs(mouseScreen.x - global.right()) <= kResizeHitWidth;
    };

    if (isNearRightEdge(m_patternBrowser)) {
        return BrowserResizeTarget::PatternRail;
    }
    if (isNearRightEdge(m_fileBrowser) || isNearRightEdge(m_pluginBrowser) || isNearRightEdge(m_previewPanel)) {
        return BrowserResizeTarget::FileRail;
    }
    return BrowserResizeTarget::None;
}

void AestraContent::updateBrowserResizeDrag(const AestraUI::NUIPoint& mouseScreen) {
    const float deltaX = mouseScreen.x - m_browserResizeStartX;

    if (m_browserResizeTarget == BrowserResizeTarget::FileRail) {
        m_fileBrowserWidthPref = std::max(kMinFileBrowserWidth, m_browserResizeStartFileWidth + deltaX);
    } else if (m_browserResizeTarget == BrowserResizeTarget::PatternRail) {
        m_patternBrowserWidthPref = std::max(kMinPatternBrowserWidth, m_browserResizeStartPatternWidth + deltaX);
    }

    onResize(static_cast<int>(getBounds().width), static_cast<int>(getBounds().height));
    setDirty(true);
}

AestraUI::NUICursorStyle AestraContent::getPanelResizeCursorStyle(const AestraUI::NUIPoint& mouseScreen) const {
    if (m_browserResizing && m_browserResizeTarget != BrowserResizeTarget::None) {
        return AestraUI::NUICursorStyle::ResizeEW;
    }

    const auto resolveStyle = [&mouseScreen](const auto& panel) {
        if (!panel || !panel->isVisible()) {
            return AestraUI::NUICursorStyle::Arrow;
        }
        return panel->getResizeCursorStyleForPoint(mouseScreen);
    };

    const auto historyStyle = resolveStyle(m_historyPanel);
    if (historyStyle != AestraUI::NUICursorStyle::Arrow)
        return historyStyle;

    const auto sequencerStyle = resolveStyle(m_sequencerPanel);
    if (sequencerStyle != AestraUI::NUICursorStyle::Arrow)
        return sequencerStyle;

    const auto sampleStyle = resolveStyle(m_sampleEditorPanel);
    if (sampleStyle != AestraUI::NUICursorStyle::Arrow)
        return sampleStyle;

    const auto pianoStyle = resolveStyle(m_pianoRollPanel);
    if (pianoStyle != AestraUI::NUICursorStyle::Arrow)
        return pianoStyle;

    const auto mixerStyle = resolveStyle(m_mixerPanel);
    if (mixerStyle != AestraUI::NUICursorStyle::Arrow)
        return mixerStyle;

    if (hitTestBrowserResizeTarget(mouseScreen) != BrowserResizeTarget::None) {
        return AestraUI::NUICursorStyle::ResizeEW;
    }

    return AestraUI::NUICursorStyle::Arrow;
}

// =============================================================================
// SECTION: Panel Drag Handlers
// =============================================================================

void AestraContent::beginPanelDrag(Audio::ViewType view, const AestraUI::NUIPoint& mouseScreen) {
    if (!m_overlayLayer)
        return;

    m_viewState.isDragging = true;
    m_viewState.draggingView = view;
    m_viewState.dragStartMouseOverlay = m_overlayLayer->globalToLocal(mouseScreen);

    switch (view) {
    case Audio::ViewType::Mixer:
        m_viewState.dragStartRect = m_viewState.mixerRect;
        break;
    case Audio::ViewType::PianoRoll:
        m_viewState.dragStartRect = m_viewState.pianoRollRect;
        break;
    case Audio::ViewType::Sequencer:
        m_viewState.dragStartRect = m_viewState.sequencerRect;
        break;
    case Audio::ViewType::History:
        m_viewState.dragStartRect = m_viewState.historyRect;
        break;
    default:
        break;
    }

    AESTRA_LOG_TRACE("Started dragging panel: " + std::to_string(static_cast<int>(view)));
}

void AestraContent::updatePanelDrag(Audio::ViewType view, const AestraUI::NUIPoint& mouseScreen) {
    if (!m_viewState.isDragging || !m_overlayLayer || view != m_viewState.draggingView)
        return;

    AestraUI::NUIPoint currentMouseOverlay = m_overlayLayer->globalToLocal(mouseScreen);
    AestraUI::NUIPoint delta = currentMouseOverlay - m_viewState.dragStartMouseOverlay;

    AestraUI::NUIRect proposed = m_viewState.dragStartRect;
    proposed.x += delta.x;
    proposed.y += delta.y;

    AestraUI::NUIRect allowed = computeAllowedRectForPanels();
    AestraUI::NUIRect finalRect = clampRectToAllowed(proposed, allowed);

    switch (view) {
    case Audio::ViewType::Mixer:
        m_viewState.mixerRect = finalRect;
        if (m_mixerPanel)
            m_mixerPanel->setBounds(finalRect);
        break;
    case Audio::ViewType::PianoRoll:
        m_viewState.pianoRollRect = finalRect;
        if (m_pianoRollPanel)
            m_pianoRollPanel->setBounds(finalRect);
        break;
    case Audio::ViewType::Sequencer:
        m_viewState.sequencerRect = finalRect;
        if (m_sequencerPanel)
            m_sequencerPanel->setBounds(finalRect);
        break;
    case Audio::ViewType::History:
        m_viewState.historyRect = finalRect;
        if (m_historyPanel)
            m_historyPanel->setBounds(finalRect);
        break;
    default:
        break;
    }

    setDirty(true);
}

void AestraContent::endPanelDrag(Audio::ViewType view) {
    m_viewState.isDragging = false;
    AESTRA_LOG_TRACE("Ended dragging panel: " + std::to_string(static_cast<int>(view)));
}

// =============================================================================
// SECTION: Getters
// =============================================================================

void AestraContent::setAudioStatus(bool active) {
    m_audioActive = active;
}

Aestra::TransportBar* AestraContent::getTransportBar() {
    return m_transportBar.get();
}

std::shared_ptr<AestraUI::AudioVisualizer> AestraContent::getAudioVisualizer() {
    return m_audioVisualizer;
}

std::shared_ptr<AestraUI::AudioVisualizer> AestraContent::getWaveformVisualizer() {
    return m_waveformVisualizer;
}

PreviewEngine* AestraContent::getPreviewEngine() {
    return m_previewEngine.get();
}

std::shared_ptr<TrackManager> AestraContent::getTrackManager() {
    return m_trackManager;
}

std::shared_ptr<TrackManagerUI> AestraContent::getTrackManagerUI() {
    return m_trackManagerUI;
}

std::shared_ptr<AestraUI::NUISegmentedControl> AestraContent::getViewToggle() {
    return m_viewToggle;
}

PatternID AestraContent::getActivePatternID() const {
    if (m_sequencerPanel) {
        return m_sequencerPanel->getActivePatternID();
    }
    return PatternID();
}

void AestraContent::clearPendingCountIn() {
    if (m_trackManager) {
        m_trackManager->clearDeferredRecordingStartBeat();
        m_trackManager->clearDisplayPositionOverride();
        m_trackManager->clearNextCapturePlacementStartBeat();
    }
    if (m_audioEngine) {
        m_audioEngine->stopMetronomeCountIn();
    }

    if (m_forcedMetronomeForCountIn && m_trackManager) {
        m_trackManager->enableMetronome(false);
        if (m_audioEngine) {
            m_audioEngine->setMetronomeEnabled(false);
        }
        if (m_transportBar) {
            m_transportBar->setMetronomeActive(false);
        }
    }

    m_forcedMetronomeForCountIn = false;
    m_pendingCountIn = false;
    m_pendingCountInTargetSeconds = 0.0;
}

void AestraContent::startPatternClipPreview(PatternID patternId) {
    if (!m_trackManager || !patternId.isValid()) {
        return;
    }

    if (m_patternClipPreviewActive && m_previewPatternId == patternId && m_trackManager->isPlaying()) {
        return;
    }

    if (m_patternClipPreviewActive) {
        stopPatternClipPreview(false);
    }

    if (m_trackManager->isPlaying() && !m_trackManager->isPatternMode()) {
        m_savedTimelinePosition = m_trackManager->getPosition();
        m_trackManager->stop();
    }

    m_patternClipPreviewActive = true;
    m_previewPatternId = patternId;

    if (m_sequencerPanel) {
        m_sequencerPanel->setActivePattern(patternId);
        m_sequencerPanel->setSelectedUnit(resolveEditingUnitForPattern(patternId));
    }

    updatePatternLoopLength(patternId);

    if (m_trackManagerUI) {
        m_trackManagerUI->setPatternMode(true);
        m_trackManagerUI->setFollowPlayhead(false);
    }
    if (m_audioEngine) {
        m_audioEngine->setAuditionModeEnabled(false);
    }

    m_trackManager->preparePatternForArsenal(patternId);
    m_trackManager->playPatternInArsenal(patternId);
}

void AestraContent::stopPatternClipPreview(bool restoreTimelineUi) {
    if (!m_patternClipPreviewActive && m_viewFocus == ViewFocus::Arsenal) {
        return;
    }

    if (m_trackManager && m_trackManager->isPatternMode() && m_viewFocus != ViewFocus::Arsenal) {
        m_trackManager->stopArsenalPlayback(false);
        m_trackManager->setPosition(m_savedTimelinePosition);
    }

    if (m_audioEngine && m_viewFocus != ViewFocus::Arsenal) {
        m_audioEngine->setPatternPlaybackMode(false, 4.0);
    }

    if (restoreTimelineUi && m_trackManagerUI && m_viewFocus != ViewFocus::Arsenal) {
        m_trackManagerUI->setPatternMode(false);
        m_trackManagerUI->setFollowPlayhead(true);
    }

    m_patternClipPreviewActive = false;
    m_previewPatternId = {};
}

ViewFocus AestraContent::resolveTransportFocus() const {
    if (m_viewToggle) {
        switch (m_viewToggle->getSelectedIndex()) {
        case 0:
            return ViewFocus::Arsenal;
        case 2:
            return ViewFocus::Audition;
        case 1:
        default:
            return ViewFocus::Timeline;
        }
    }
    return m_viewFocus;
}

bool AestraContent::isTransportRolling() const {
    if (m_pendingCountIn || (m_audioEngine && m_audioEngine->isMetronomeCountInActive())) {
        return true;
    }

    const bool trackManagerPlaying = m_trackManager && m_trackManager->isPlaying();
    const bool transportBarPlaying = m_transportBar && m_transportBar->getState() == TransportState::Playing;
    return trackManagerPlaying || transportBarPlaying;
}

void AestraContent::updatePendingCountIn() {
    if (!m_pendingCountIn || !m_trackManager) {
        return;
    }

    if (m_audioEngine && m_audioEngine->isMetronomeCountInActive()) {
        return;
    }

    m_trackManager->clearDisplayPositionOverride();
    m_trackManager->clearNextCapturePlacementStartBeat();
    if (m_forcedMetronomeForCountIn) {
        m_trackManager->enableMetronome(false);
        if (m_audioEngine) {
            m_audioEngine->setMetronomeEnabled(false);
        }
        if (m_transportBar) {
            m_transportBar->setMetronomeActive(false);
        }
        m_forcedMetronomeForCountIn = false;
    }
    m_pendingCountIn = false;
    m_pendingCountInTargetSeconds = 0.0;
    m_trackManager->play();
    stopSoundPreview();
}

double AestraContent::getActivePatternLengthBeats() const {
    if (!m_sequencerPanel || !m_trackManager) {
        return 16.0; // Default fallback: 4 bars
    }
    PatternID pid = m_sequencerPanel->getActivePatternID();
    if (!pid.isValid()) {
        return 16.0;
    }
    auto* pattern = m_trackManager->getPatternManager().getPattern(pid);
    if (pattern && pattern->lengthBeats > 0.0) {
        return pattern->lengthBeats;
    }
    return 16.0; // Fallback
}

void AestraContent::updatePatternLoopLength(PatternID patternId) {
    if (!m_audioEngine || !m_trackManager || !patternId.isValid()) {
        return;
    }
    auto* pattern = m_trackManager->getPatternManager().getPattern(patternId);
    if (!pattern) {
        return;
    }
    double lengthBeats = std::max(16.0, pattern->lengthBeats);
    m_audioEngine->setPatternPlaybackMode(true, lengthBeats);
}

void AestraContent::handleTransportPlayRequest() {
    if (resolveTransportFocus() != ViewFocus::Timeline || !m_trackManager) {
        playFromCurrentFocus();
        return;
    }

    if (!isTransportRolling()) {
        if (m_audioEngine) {
            m_audioEngine->stopMetronomeCountIn();
        }
        m_trackManager->clearDeferredRecordingStartBeat();
        m_trackManager->clearDisplayPositionOverride();
        m_trackManager->clearNextCapturePlacementStartBeat();
        m_pendingCountIn = false;
        m_pendingCountInTargetSeconds = 0.0;
    }

    if (!m_countInEnabled || !m_trackManager->isRecordArmed() || !m_trackManager->hasArmedTracks()) {
        clearPendingCountIn();
        playFromCurrentFocus();
        return;
    }

    const int beatsPerBar = m_transportBar ? std::max(1, m_transportBar->getTimeSignature()) : 4;
    const double requestedStartSeconds = std::max(0.0, m_trackManager->getPosition());

    if (isTransportRolling()) {
        return;
    }

    m_pendingCountIn = true;
    m_pendingCountInTargetSeconds = requestedStartSeconds;
    m_trackManager->setPlayStartPosition(requestedStartSeconds);
    m_trackManager->setPosition(requestedStartSeconds);
    m_trackManager->setDisplayPositionOverride(requestedStartSeconds);

    if (m_audioEngine && !m_audioEngine->isMetronomeEnabled()) {
        m_trackManager->enableMetronome(true);
        m_audioEngine->setMetronomeEnabled(true);
        if (m_transportBar) {
            m_transportBar->setMetronomeActive(true);
        }
        m_forcedMetronomeForCountIn = true;
    } else {
        m_forcedMetronomeForCountIn = false;
    }

    if (m_audioEngine) {
        m_audioEngine->stopMetronomeCountIn();
        m_audioEngine->startMetronomeCountIn(static_cast<uint32_t>(beatsPerBar));
    } else {
        m_pendingCountIn = false;
        playFromCurrentFocus();
    }
}

void AestraContent::requestTransportPlay() {
    handleTransportPlayRequest();
}

void AestraContent::playFromCurrentFocus() {
    const ViewFocus focus = resolveTransportFocus();
    if (m_patternClipPreviewActive && focus != ViewFocus::Arsenal) {
        stopPatternClipPreview(true);
    }

    if (focus == ViewFocus::Audition) {
        if (m_auditionEngine) {
            if (!m_auditionEngine->isPlaying()) {
                stopSoundPreview();
            }
            m_auditionEngine->togglePlayPause();
        }
        return;
    }

    if (focus == ViewFocus::Arsenal) {
        if (!m_trackManager || !m_sequencerPanel) {
            return;
        }

        const PatternID activePattern = m_sequencerPanel->getActivePatternID();
        if (!activePattern.isValid()) {
            AESTRA_LOG_ERROR("[Arsenal] Cannot start playback without an active pattern");
            return;
        }

        if (m_audioEngine) {
            m_audioEngine->setPatternPlaybackMode(true, getActivePatternLengthBeats());
        }

        AESTRA_LOG_DEBUG("[Arsenal] Focus-aware play scheduling pattern " + std::to_string(activePattern.value));
        m_trackManager->playPatternInArsenal(activePattern);
        return;
    }

    if (m_trackManager) {
        m_trackManager->play();
    }
}

void AestraContent::stopFromCurrentFocus(bool hardStop) {
    const ViewFocus focus = resolveTransportFocus();
    if (m_patternClipPreviewActive && focus != ViewFocus::Arsenal) {
        stopPatternClipPreview(true);
    }

    if (focus == ViewFocus::Audition) {
        if (m_auditionEngine) {
            m_auditionEngine->stop();
        }
        return;
    }

    if (focus == ViewFocus::Arsenal) {
        if (m_trackManager) {
            AESTRA_LOG_DEBUG("[Arsenal] Focus-aware stop");
            m_trackManager->stopArsenalPlayback(true);
        }
        if (hardStop && m_audioEngine) {
            m_audioEngine->panic();
        }
        return;
    }

    if (m_trackManager) {
        m_trackManager->stop();
    }
    if (hardStop && m_audioEngine) {
        m_audioEngine->panic();
    }
    if (hardStop && m_trackManager) {
        m_trackManager->setPlayStartPosition(0.0);
        m_trackManager->setPosition(0.0);
        m_trackManager->clearDisplayPositionOverride();
        if (m_audioEngine) {
            m_audioEngine->setGlobalSamplePos(0);
        }
    }
}

void AestraContent::pauseFromCurrentFocus() {
    const ViewFocus focus = resolveTransportFocus();
    if (focus == ViewFocus::Audition) {
        if (m_auditionEngine) {
            m_auditionEngine->togglePlayPause();
        }
        return;
    }

    if (focus == ViewFocus::Arsenal) {
        // In Arsenal one-shot workflow, pause should hard-cut active sample voices.
        stopFromCurrentFocus(true);
        return;
    }

    if (m_trackManager) {
        m_trackManager->pause();
    }
}

void AestraContent::openPatternInPianoRoll(PatternID patternId) {
    if (!m_pianoRollPanel || !m_trackManager || !patternId.isValid()) {
        return;
    }

    auto& pm = m_trackManager->getPatternManager();
    auto pattern = pm.getPattern(patternId);
    if (!pattern || !pattern->isMidi()) {
        return;
    }

    if (m_sequencerPanel) {
        m_sequencerPanel->setActivePattern(patternId);
        m_sequencerPanel->setSelectedUnit(resolveEditingUnitForPattern(patternId));
    }

    m_pianoRollPanel->setEditingUnit(resolveEditingUnitForPattern(patternId));

    AestraUI::NUIRect allowed = computeAllowedRectForPanels();
    float editorWidth = std::min(900.0f, allowed.width * 0.8f);
    float editorHeight = std::min(500.0f, allowed.height * 0.7f);
    float editorX = allowed.x + (allowed.width - editorWidth) / 2.0f;
    float editorY = allowed.y + (allowed.height - editorHeight) / 2.0f;

    m_viewState.pianoRollRect = AestraUI::NUIRect(editorX, editorY, editorWidth, editorHeight);
    m_pianoRollPanel->loadPattern(patternId);
    setViewOpen(Audio::ViewType::PianoRoll, true);
}

Aestra::Audio::UnitID AestraContent::resolveEditingUnitForPattern(PatternID patternId) const {
    if (!m_trackManager || !patternId.isValid()) {
        return 0;
    }

    const auto& unitManager = m_trackManager->getUnitManager();
    for (const auto unitId : unitManager.getAllUnitIDs()) {
        const auto* unit = unitManager.getUnit(unitId);
        if (unit && unit->defaultPatternId == patternId) {
            return unitId;
        }
    }

    if (m_sequencerPanel) {
        return m_sequencerPanel->getSelectedUnitId();
    }

    return 0;
}

std::shared_ptr<AestraUI::FileBrowser> AestraContent::getFileBrowser() const {
    return m_fileBrowser;
}

void AestraContent::setPlatformBridge(AestraUI::NUIPlatformBridge* bridge) {
    m_platformBridge = bridge;
    if (m_trackManagerUI) {
        m_trackManagerUI->setPlatformWindow(bridge);
    }
    if (m_pluginController) {
        m_pluginController->setPlatformBridge(bridge);
    }
    if (m_mixerPanel) {
        m_mixerPanel->setPlatformBridge(bridge);
    }
    if (m_pianoRollPanel) {
        m_pianoRollPanel->setPlatformBridge(bridge);
    }
}

void AestraContent::setMidiInput(Aestra::Audio::MidiInputService* midiInput) {
    m_midiInput = midiInput;
    // Push the current selection so a controller plays the right unit even if
    // the user never re-selects after startup.
    if (m_midiInput && m_sequencerPanel) {
        m_midiInput->setTargetUnit(m_sequencerPanel->getSelectedUnitId());
    }
}

void AestraContent::setAudioEngine(Aestra::Audio::AudioEngine* engine) {
    m_musicalTyping.releaseAllNotes();
    // Detach preview from old engine before overwriting m_audioEngine
    if (m_audioEngine && m_previewEngine) {
        m_audioEngine->setPreviewEngine(nullptr);
    }
    m_audioEngine = engine;
    m_musicalTyping.setTargetUnit(m_sequencerPanel ? m_sequencerPanel->getSelectedUnitId() : 0);
    Aestra::Audio::CommandRegistry::setAudioEngine(engine);
    if (m_audioEngine && m_previewEngine) {
        m_audioEngine->setPreviewEngine(m_previewEngine.get());
    }
    if (m_pianoRollPanel) {
        m_pianoRollPanel->setAudioEngine(m_audioEngine);
    }

    // Initialize the playback graph controller when engine is available
    if (m_audioEngine && m_trackManager) {
        m_playbackGraphController = std::make_unique<Aestra::Audio::PlaybackGraphController>();
        m_playbackGraphController->setTrackManager(m_trackManager.get());
        m_playbackGraphController->setAudioEngine(m_audioEngine);
    }
    if (m_audioEngine && m_trackManager) {
        m_audioEngine->setUnitManager(&m_trackManager->getUnitManager());
        m_audioEngine->setPatternPlaybackEngine(&m_trackManager->getPatternPlaybackEngine());
        m_audioEngine->setContinuousParams(m_trackManager->getContinuousParams());
        if (auto slotMap = m_trackManager->getChannelSlotMapShared()) {
            m_audioEngine->setChannelSlotMap(slotMap);
        }
        m_trackManager->setCommandSink([this](const AudioQueueCommand& cmd) {
            if (m_audioEngine) {
                if (cmd.type == AudioQueueCommandType::SetTransportState) {
                    m_audioEngine->commandQueue().push(cmd);
                    if (m_trackManager) {
                        const double engineSampleRate =
                            std::max(1.0, static_cast<double>(m_audioEngine->getSampleRate()));
                        const double positionSeconds = static_cast<double>(cmd.samplePos) / engineSampleRate;
                        m_trackManager->onTransportStateApplied(cmd.value1 != 0.0f, positionSeconds);
                    }
                } else {
                    m_audioEngine->commandQueue().push(cmd);
                }
            }
        });
    }
    AESTRA_LOG_DEBUG("AestraContent::setAudioEngine called - Initializing View State");
    // Ensure correct initial state now that engine is valid
    setViewFocus(ViewFocus::Timeline);
}

Aestra::Audio::PlaybackGraphController* AestraContent::getPlaybackGraphController() const {
    return m_playbackGraphController.get();
}

// =============================================================================
// SECTION: Demo & Testing
// =============================================================================

void AestraContent::resetToDefaultProject() {
    AESTRA_LOG_DEBUG("resetToDefaultProject() - clearing and recreating default state");

    if (!m_trackManager)
        return;

    // Clear existing state
    auto& playlist = m_trackManager->getPlaylistModel();
    auto& sourceManager = m_trackManager->getSourceManager();

    // Suppress UI refresh during clear to avoid flicker
    playlist.clear();
    sourceManager.clear();
    m_trackManager->clearAllChannels();

    // Recreate default tracks
    addDemoTracks();

    // Reset modified flag
    m_trackManager->setModified(false);

    AESTRA_LOG_DEBUG("resetToDefaultProject() completed");
}

void AestraContent::addDemoTracks() {
    AESTRA_LOG_DEBUG("addDemoTracks() called - starting demo track creation (v3.0)");

    if (!m_trackManager)
        return;
    auto& playlist = m_trackManager->getPlaylistModel();

    const int DEFAULT_TRACK_COUNT = 50;

    for (int i = 1; i <= DEFAULT_TRACK_COUNT; ++i) {
        std::string name = "Track " + std::to_string(i);
        PlaylistLaneID laneId = playlist.createLane(name);
        m_trackManager->addChannel(name);

        if (auto* lane = playlist.getLane(laneId)) {
            // Cycle the shared track palette so the lane strip matches the
            // name ink and mixer tint derived from the same index.
            lane->colorRGBA = AestraUI::TRACK_PALETTE[(i - 1) % AestraUI::PALETTE_SIZE];

            if (i == 1) {
                AutomationCurve vol("Volume", AutomationTarget::Volume);
                vol.setDefaultValue(0.8);
                double samplesPerBeat = (48000.0 * 60.0) / 120.0; // Demo values
                vol.addPoint(0.0, 0.5, samplesPerBeat, 0.5f);
                vol.addPoint(4.0, 1.0, samplesPerBeat, 0.5f);
                vol.addPoint(8.0, 0.2, samplesPerBeat, 0.5f);
                vol.addPoint(12.0, 0.8, samplesPerBeat, 0.5f);
                lane->automationCurves.push_back(vol);
            }
        }
    }

    if (m_trackManagerUI) {
        m_trackManagerUI->refreshTracks();
    }
    AESTRA_LOG_DEBUG("addDemoTracks() completed - created " + std::to_string(DEFAULT_TRACK_COUNT) + " lanes/channels");
}

bool AestraContent::generateTestWavFile(const std::string& filename, float frequency, double duration) {
    AESTRA_LOG_DEBUG("generateTestWavFile called for: " + filename);

    std::ifstream checkFile(filename);
    if (checkFile) {
        checkFile.close();
        AESTRA_LOG_DEBUG("File already exists: " + filename);
        return true;
    }
    checkFile.close();

    AESTRA_LOG_DEBUG("Generating test WAV file: " + filename + " (" + std::to_string(frequency) + " Hz, " +
                     std::to_string(duration) + "s)");

    const uint32_t sampleRate = 44100;
    const uint16_t numChannels = 2;
    const uint16_t bitsPerSample = 16;
    const uint32_t totalSamples = static_cast<uint32_t>(sampleRate * duration * numChannels);

    struct WavHeader {
        char riff[4] = {'R', 'I', 'F', 'F'};
        uint32_t fileSize;
        char wave[4] = {'W', 'A', 'V', 'E'};
        char fmt[4] = {'f', 'm', 't', ' '};
        uint32_t fmtSize = 16;
        uint16_t audioFormat = 1;
        uint16_t numChannels;
        uint32_t sampleRate;
        uint32_t byteRate;
        uint16_t blockAlign;
        uint16_t bitsPerSample;
        char data[4] = {'d', 'a', 't', 'a'};
        uint32_t dataSize;
    } header;

    header.numChannels = numChannels;
    header.sampleRate = sampleRate;
    header.byteRate = sampleRate * numChannels * (bitsPerSample / 8);
    header.blockAlign = numChannels * (bitsPerSample / 8);
    header.bitsPerSample = bitsPerSample;
    header.dataSize = totalSamples * (bitsPerSample / 8);
    header.fileSize = 36 + header.dataSize;

    std::vector<int16_t> audioData(totalSamples);

    for (uint32_t i = 0; i < totalSamples / numChannels; ++i) {
        double phase = 2.0 * M_PI * frequency * i / sampleRate;
        int16_t sample = static_cast<int16_t>(30000 * sin(phase));
        audioData[i * numChannels + 0] = sample;
        audioData[i * numChannels + 1] = sample;
    }

    std::ofstream wavFile(filename, std::ios::binary);
    if (!wavFile) {
        AESTRA_LOG_ERROR("Failed to create test WAV file: " + filename);
        return false;
    }

    wavFile.write(reinterpret_cast<char*>(&header), sizeof(header));
    wavFile.write(reinterpret_cast<char*>(audioData.data()), audioData.size() * sizeof(int16_t));
    wavFile.close();

    std::ifstream verifyFile(filename, std::ios::binary);
    if (verifyFile) {
        verifyFile.seekg(0, std::ios::end);
        verifyFile.close();
        AESTRA_LOG_DEBUG("Test WAV file generated successfully: " + filename);
        return true;
    } else {
        AESTRA_LOG_ERROR("Failed to verify WAV file creation: " + filename);
        return false;
    }
}

// =============================================================================
// SECTION: Sound Preview
// =============================================================================

void AestraContent::playSoundPreview(const AestraUI::FileItem& file) {
    AESTRA_LOG_DEBUG("Playing sound preview for: " + file.path);

    stopSoundPreview();

    if (!m_previewEngine) {
        AESTRA_LOG_ERROR("Preview engine not initialized");
        return;
    }

    m_previewDuration = 8.0;
    auto result = m_previewEngine->play(file.path, 0.0f, m_previewDuration);

    if (result == PreviewResult::Success || result == PreviewResult::Pending) {
        m_previewIsPlaying = true;
        m_previewStartTime = std::chrono::steady_clock::now();
        m_currentPreviewFile = file.path;
        if (m_fileBrowser)
            m_fileBrowser->setActivePlaybackPath(file.path);

        if (m_previewPanel)
            m_previewPanel->setPlaying(true);

        if (result == PreviewResult::Pending) {
            if (m_previewPanel) {
                m_previewPanel->setLoading(true);
            }
            AESTRA_LOG_DEBUG("Sound preview pending (async decode)");
        } else {
            AESTRA_LOG_DEBUG("Sound preview started (cache hit)");
        }
    } else {
        AESTRA_LOG_WARNING("Failed to load preview audio: " + file.path);
    }
}

void AestraContent::stopSoundPreview() {
    AESTRA_LOG_TRACE("stopSoundPreview() called");
    if (m_previewPanel) {
        m_previewPanel->setLoading(false);
        m_previewPanel->setPlaying(false);
    }

    if (m_previewEngine) {
        if (m_previewIsPlaying)
            AESTRA_LOG_TRACE("Stopping preview engine...");
        m_previewEngine->stop();
    }
    m_previewIsPlaying = false;
    if (m_fileBrowser && (!m_auditionEngine || !m_auditionEngine->isPlaying())) {
        m_fileBrowser->setActivePlaybackPath("");
    }
    AESTRA_LOG_TRACE("Sound preview stopped");
}

void AestraContent::enqueueMainThreadTask(std::function<void()> task) {
    if (!task) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mainThreadTasksMutex);
    m_mainThreadTasks.push_back(std::move(task));
}

void AestraContent::drainMainThreadTasks() {
    std::vector<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(m_mainThreadTasksMutex);
        tasks.swap(m_mainThreadTasks);
    }

    for (auto& task : tasks) {
        if (task) {
            task();
        }
    }
}

void AestraContent::loadSampleIntoUnitAsync(UnitID unitId, const std::string& samplePath, bool openEditorWhenReady) {
    if (!m_trackManager || unitId == 0 || samplePath.empty()) {
        return;
    }

    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(m_sampleUnitLoadGenerationsMutex);
        generation = ++m_sampleUnitLoadGenerations[unitId];
    }
    auto weakSelf = weak_from_this();

    if (m_trackManager->getUnitManager().getUnit(unitId)) {
        std::string filename = std::filesystem::path(samplePath).stem().string();
        if (!filename.empty()) {
            m_trackManager->getUnitManager().setUnitName(unitId, filename);
        }
        m_trackManager->getUnitManager().setUnitEnabled(unitId, true);
    }
    if (m_sequencerPanel) {
        m_sequencerPanel->setSelectedUnit(unitId);
        m_sequencerPanel->refreshUnits();
    }

    std::thread([weakSelf, generation, unitId, samplePath, openEditorWhenReady]() {
        std::vector<float> decodedData;
        uint32_t sampleRate = 0;
        uint32_t numChannels = 0;
        const bool decoded = decodeAudioFile(samplePath, decodedData, sampleRate, numChannels);
        const uint32_t sampleLength =
            (decoded && numChannels > 0) ? static_cast<uint32_t>(decodedData.size() / numChannels) : 0;
        std::vector<float> editorWaveform =
            decoded ? buildEditorWaveform(decodedData, numChannels, sampleLength) : std::vector<float>();

        std::vector<float> previewAudio;
        uint32_t previewRate = 0;
        uint32_t previewChannels = 0;
        constexpr uint64_t kPreviewMaxFrames = 48000 * 24;
        constexpr double kPreviewMaxSeconds = static_cast<double>(kPreviewMaxFrames) / 48000.0;
        std::vector<float> previewWaveform;
        double durationSeconds = 0.0;

        if (decodeAudioPreview(samplePath, previewAudio, previewRate, previewChannels, kPreviewMaxSeconds)) {
            previewWaveform = buildPreviewWaveform(previewAudio, previewChannels);
            if (previewRate > 0 && previewChannels > 0) {
                durationSeconds =
                    static_cast<double>(previewAudio.size()) / static_cast<double>(previewRate * previewChannels);
            }
        }
        if (durationSeconds <= 0.0 && sampleRate > 0 && numChannels > 0) {
            durationSeconds = static_cast<double>(decodedData.size()) / static_cast<double>(sampleRate * numChannels);
        }

        auto self = std::dynamic_pointer_cast<AestraContent>(weakSelf.lock());
        if (!self) {
            return;
        }

        self->enqueueMainThreadTask([weakSelf, generation, unitId, samplePath, openEditorWhenReady, decoded,
                                     decodedData = std::move(decodedData), sampleRate, numChannels,
                                     sampleLength, editorWaveform = std::move(editorWaveform),
                                     previewWaveform = std::move(previewWaveform), durationSeconds]() mutable {
            auto self = std::dynamic_pointer_cast<AestraContent>(weakSelf.lock());
            if (!self || !self->m_trackManager) {
                return;
            }
            {
                std::lock_guard<std::mutex> lock(self->m_sampleUnitLoadGenerationsMutex);
                const auto generationIt = self->m_sampleUnitLoadGenerations.find(unitId);
                if (generationIt == self->m_sampleUnitLoadGenerations.end() || generationIt->second != generation) {
                    return;
                }
            }

            auto& unitManager = self->m_trackManager->getUnitManager();
            if (!decoded ||
                !unitManager.setUnitAudioClipFromDecoded(unitId, samplePath, std::move(decodedData), sampleRate,
                                                         numChannels, std::move(previewWaveform), durationSeconds)) {
                AESTRA_LOG_ERROR("Failed to load sample into Unit " + std::to_string(unitId) + ": " + samplePath);
                return;
            }

            if (self->m_sequencerPanel) {
                self->m_sequencerPanel->setSelectedUnit(unitId);
                self->m_sequencerPanel->refreshUnits();
            }
            if (openEditorWhenReady) {
                self->m_sampleEditorUnitId = unitId;
                self->syncSampleEditorToUnit(unitId);
                if (self->m_sampleEditorPanel) {
                    self->m_sampleEditorPanel->loadPreparedSample(samplePath, static_cast<double>(sampleRate),
                                                                  sampleLength, std::move(editorWaveform));
                    self->m_sampleEditorPanel->setVisible(true);
                    self->m_sampleEditorPanel->bringToFront();
                    self->m_sampleEditorPanel->setDirty(true);
                }
                self->onResize(static_cast<int>(self->getBounds().width), static_cast<int>(self->getBounds().height));
            }
            AESTRA_LOG_DEBUG("Sample loaded into Unit " + std::to_string(unitId) + " asynchronously: " + samplePath);
        });
    }).detach();
}

void AestraContent::loadSampleIntoSelectedTrack(const std::string& filePath) {
    AESTRA_LOG_DEBUG("=== Loading sample into arrangement: " + filePath + " ===");

    stopSoundPreview();

    if (!m_trackManager) {
        AESTRA_LOG_ERROR("TrackManager not initialized");
        return;
    }

    auto& sourceManager = m_trackManager->getSourceManager();
    ClipSourceID sourceId = sourceManager.getOrCreateSource(filePath);

    if (!sourceId.isValid()) {
        AESTRA_LOG_ERROR("Failed to load sample source: " + filePath);
        return;
    }

    ClipSource* source = sourceManager.getSource(sourceId);
    if (source && !source->isReady()) {
        AESTRA_LOG_DEBUG("Decoding source in Main: " + filePath);
        std::vector<float> decodedData;
        uint32_t sampleRate = 0;
        uint32_t numChannels = 0;
        if (decodeAudioFile(filePath, decodedData, sampleRate, numChannels)) {
            auto buffer = std::make_shared<AudioBufferData>();
            buffer->interleavedData = std::move(decodedData);
            buffer->sampleRate = sampleRate;
            buffer->numChannels = numChannels;
            buffer->numFrames = buffer->interleavedData.size() / numChannels;
            source->setBuffer(buffer);
        }
    }

    if (!source || !source->isReady()) {
        AESTRA_LOG_ERROR("Failed to decode or ready sample source: " + filePath);
        return;
    }

    double durationSeconds = source->getDurationSeconds();
    double durationBeats = m_trackManager->getPlaylistModel().secondsToBeats(durationSeconds);

    auto& patternManager = m_trackManager->getPatternManager();
    AudioSlicePayload payload;
    payload.audioSourceId = sourceId;
    payload.durationSeconds = durationSeconds;
    AudioSlice slice;
    slice.startOffset = 0.0;
    slice.duration = durationSeconds;
    slice.startSamples = 0.0;
    slice.lengthSamples = static_cast<double>(source->getNumFrames());
    payload.slices.push_back(slice);

    std::string patternName = std::filesystem::path(filePath).filename().string();
    PatternID patternId = patternManager.createAudioPattern(patternName, durationBeats, payload);

    if (!patternId.isValid()) {
        AESTRA_LOG_ERROR("Failed to create pattern for sample");
        return;
    }

    PlaylistLaneID targetLaneId;
    if (m_trackManagerUI) {
        auto* selectedUI = m_trackManagerUI->getSelectedTrackUI();
        if (selectedUI) {
            targetLaneId = selectedUI->getLaneId();
        }
    }

    auto& playlist = m_trackManager->getPlaylistModel();
    if (!targetLaneId.isValid()) {
        if (playlist.getLaneCount() == 0) {
            targetLaneId = playlist.createLane("Sample Lane");
        } else {
            targetLaneId = playlist.getLaneId(0);
        }
    }

    double playheadPositionSeconds = m_transportBar ? m_transportBar->getPosition() : 0.0;
    double startBeat = m_trackManager->getPlaylistModel().secondsToBeats(playheadPositionSeconds);

    Aestra::Audio::ClipInstance clip;
    clip.id = Aestra::Audio::ClipInstanceID::generate();
    clip.patternId = patternId;
    clip.sourceId = patternId.value;
    clip.startBeat = startBeat;
    clip.durationBeats = durationBeats;
    clip.durationSeconds = durationSeconds;
    clip.name = patternName;
    const auto clipId = playlist.addClip(targetLaneId, clip);
    if (!clipId.isValid()) {
        patternManager.removePattern(patternId);
        AESTRA_LOG_ERROR("Failed to add sample clip to arrangement; removed orphan pattern");
        return;
    }

    if (m_trackManagerUI) {
        m_trackManagerUI->refreshTracks();
        m_trackManagerUI->invalidateCache();
    }

    AESTRA_LOG_DEBUG("Sample loaded into arrangement via v3.0 architecture");
}

void AestraContent::syncSampleEditorToUnit(UnitID unitId) {
    if (!m_trackManager || !m_sampleEditorPanel || !unitId) {
        return;
    }
    auto plugin = m_trackManager->getUnitManager().getUnitPlugin(unitId);
    auto sampler = std::dynamic_pointer_cast<Aestra::Audio::Plugins::SamplerPlugin>(plugin);
    if (!sampler) {
        return;
    }

    SampleEditorPanel::ADSRParams adsr;
    adsr.attack = sampler->getAttack();
    adsr.decay = sampler->getDecay();
    adsr.sustain = sampler->getSustain();
    adsr.release = sampler->getRelease();
    m_sampleEditorPanel->setADSR(adsr);

    SampleEditorPanel::LoopPoints loop;
    loop.start = sampler->getLoopStartNorm();
    loop.end = sampler->getLoopEndNorm();
    loop.mode = sampler->isLoopEnabled() ? SampleEditorPanel::LoopMode::Loop : SampleEditorPanel::LoopMode::OneShot;
    m_sampleEditorPanel->setLoopPoints(loop);

    SampleEditorPanel::PitchTune pitch;
    pitch.coarse = static_cast<int>(std::round(sampler->getCoarseSemitones()));
    pitch.fine = sampler->getFineTuneCents();
    m_sampleEditorPanel->setPitchTune(pitch);
    m_sampleEditorPanel->setVoiceCount(sampler->getMaxVoices());
    m_sampleEditorPanel->setMonoMode(sampler->isMonoMode());
}

void AestraContent::openSampleEditorForUnit(UnitID unitId, const std::string& samplePath) {
    if (!m_sampleEditorPanel || !m_trackManager || !unitId) {
        return;
    }
    m_sampleEditorUnitId = unitId;
    syncSampleEditorToUnit(unitId);
    m_sampleEditorPanel->loadSample(samplePath);
    m_sampleEditorPanel->setVisible(true);
    m_sampleEditorPanel->bringToFront();
    onResize(static_cast<int>(getBounds().width), static_cast<int>(getBounds().height));
    m_sampleEditorPanel->setDirty(true);
}

void AestraContent::updateSoundPreview() {
    if (m_previewEngine && m_previewIsPlaying) {
        if (m_previewEngine->isBufferReady() && m_previewPanel) {
            m_previewPanel->setLoading(false);
            if (m_previewEngine->isPlaying())
                m_previewPanel->setPlaying(true);
        }

        if (!m_previewEngine->isPlaying()) {
            if (m_previewPanel) {
                m_previewPanel->onPreviewEnded();
            }
            if (!m_previewEngine->isPlaying()) {
                stopSoundPreview();
            }
        } else {
            auto currentTime = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(currentTime - m_previewStartTime);
            if (elapsed.count() >= m_previewDuration) {
                stopSoundPreview();
            }
        }
    }
    updatePreviewPlayhead();
}

void AestraContent::seekSoundPreview(double seconds) {
    if (m_previewEngine) {
        bool engineIsPlaying = m_previewEngine->isPlaying();

        m_previewDuration = 300.0;

        if ((!m_previewIsPlaying || !engineIsPlaying) && !m_currentPreviewFile.empty()) {
            m_previewEngine->play(m_currentPreviewFile, 0.0f, m_previewDuration);
            m_previewIsPlaying = true;
            m_previewStartTime = std::chrono::steady_clock::now();
            if (m_previewPanel)
                m_previewPanel->setPlaying(true);
        }
        m_previewEngine->seek(seconds);
    }
}

bool AestraContent::isPlayingPreview() const {
    return m_previewIsPlaying;
}

bool AestraContent::hasRealtimePlaybackVisuals() const {
    if (m_previewIsPlaying)
        return true;
    if (m_auditionEngine && m_auditionEngine->isPlaying())
        return true;
    return false;
}

void AestraContent::updatePreviewPlayhead() {
    if (m_previewPanel && m_previewEngine) {
        m_previewPanel->setDuration(m_previewEngine->getDuration());
        m_previewPanel->setPlayheadPosition(m_previewEngine->getPlaybackPosition());
    }
}

// =============================================================================
// SECTION: Plugin Loading
// =============================================================================

void AestraContent::loadEffectToSelectedTrack(const std::string& pluginId) {
    // 1. Get TrackManager
    if (!m_trackManager)
        return;

    // 2. Resolve the target channel: the selected track's mixer channel when a
    //    track is selected (mirrors the sample-drop selection path), otherwise
    //    fall back to the first channel. The shared_ptr keeps the selected
    //    channel alive for the duration of this call.
    MixerChannel* channel = nullptr;
    std::shared_ptr<MixerChannel> selectedChannel;
    if (m_trackManagerUI) {
        if (auto* selectedTrack = m_trackManagerUI->getSelectedTrackUI()) {
            selectedChannel = selectedTrack->getMixerChannel();
            channel = selectedChannel.get();
        }
    }
    if (!channel) {
        channel = m_trackManager->getChannel(0);
    }
    if (!channel) {
        AESTRA_LOG_ERROR("Cannot load effect: no mixer channel available");
        return;
    }

    // 3. Create plugin instance via PluginManager
    auto& pm = Aestra::Audio::PluginManager::getInstance();
    auto instance = pm.createInstanceById(pluginId);
    if (!instance) {
        AESTRA_LOG_ERROR("Failed to create plugin instance for: " + pluginId);
        return;
    }

    if (!instance->initialize(pm.getDefaultSampleRate(), pm.getDefaultBlockSize())) {
        AESTRA_LOG_ERROR("Failed to initialize effect instance for: " + pluginId);
        return;
    }
    if (auto delay = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraDelay>(instance)) {
        const float bpm = m_audioEngine ? m_audioEngine->getBPM() : 120.0f;
        delay->setBPM(bpm);
    }
    if (auto lfo = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraLFO>(instance)) {
        const float bpm = m_audioEngine ? m_audioEngine->getBPM() : 120.0f;
        lfo->setBPM(bpm);
    }
    instance->activate();

    // 4. Insert into first available effect chain slot (via CommandHistory for undo)
    auto& chain = channel->getEffectChain();
    chain.prepare(pm.getDefaultSampleRate(), pm.getDefaultBlockSize());
    size_t slot = chain.getFirstEmptySlot();
    if (slot < Aestra::Audio::EffectChain::MAX_SLOTS) {
        m_trackManager->getCommandHistory().pushAndExecute(
            std::make_shared<Aestra::Audio::AddPluginCommand>(*channel, slot, std::move(instance)));
        // The playback graph only picks up chain changes on rebuild (which also
        // re-prepares chains with the live sample rate/block size) — without this,
        // the plugin never processes already-playing tracks.
        m_trackManager->requestAudioGraphRebuild(Aestra::Audio::GraphDirtyReason::EffectChainChanged);
        AESTRA_LOG_DEBUG("Loaded effect to channel '" + channel->getName() + "' slot " + std::to_string(slot));
    } else {
        AESTRA_LOG_WARNING("No empty effect slots on channel '" + channel->getName() + "'");
    }
}

void AestraContent::setPluginTempo(float bpm) {
    auto& pluginManager = Aestra::Audio::PluginManager::getInstance();
    for (const auto& instance : pluginManager.getActiveInstances()) {
        if (auto delay = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraDelay>(instance)) {
            delay->setBPM(bpm);
        }
        if (auto lfo = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraLFO>(instance)) {
            lfo->setBPM(bpm);
        }
    }
}

void AestraContent::loadInstrumentToArsenal(const std::string& pluginId) {
    // 1. Get UnitManager from TrackManager
    if (!m_trackManager)
        return;
    auto& unitManager = m_trackManager->getUnitManager();

    // 2. Find plugin info to get the name
    auto& pm = Aestra::Audio::PluginManager::getInstance();
    const auto* pluginInfo = pm.findPlugin(pluginId);
    std::string unitName = "Plugin Synth";
    if (pluginInfo) {
        unitName = pluginInfo->name;
    }

    // 3. Create and initialize plugin instance
    auto instance = pm.createInstanceById(pluginId);
    if (!instance) {
        AESTRA_LOG_ERROR("Failed to create instrument instance for Arsenal: " + pluginId);
        return;
    }

    if (!instance->initialize(pm.getDefaultSampleRate(), pm.getDefaultBlockSize())) {
        AESTRA_LOG_ERROR("Failed to initialize instrument instance for Arsenal: " + pluginId);
        return;
    }

    // 4. Create new Unit with the plugin name and attach plugin for audio processing
    UnitID newUnit = unitManager.createUnit(unitName, UnitGroup::Synth);
    unitManager.clearUnitTimelineLane(newUnit);
    unitManager.setUnitEnabled(newUnit, true);
    unitManager.attachPlugin(newUnit, pluginId, instance);
    unitManager.captureUnitPluginState(newUnit);

    // 5. Refresh Arsenal UI
    if (m_sequencerPanel) {
        m_sequencerPanel->refreshUnits();
    }

    AESTRA_LOG_DEBUG("Loaded instrument '" + unitName + "' to Arsenal as Unit " + std::to_string(newUnit));
}

void AestraContent::loadInstrumentIntoArsenalUnit(UnitID unitId, const std::string& pluginId) {
    if (!m_trackManager)
        return;
    auto& unitManager = m_trackManager->getUnitManager();

    auto* unit = unitManager.getUnit(unitId);
    if (!unit) {
        AESTRA_LOG_ERROR("Cannot attach instrument to missing Arsenal Unit " + std::to_string(unitId));
        return;
    }

    auto& pm = Aestra::Audio::PluginManager::getInstance();
    const auto* pluginInfo = pm.findPlugin(pluginId);
    std::string unitName = pluginInfo ? pluginInfo->name : "Plugin Synth";

    auto instance = pm.createInstanceById(pluginId);
    if (!instance) {
        AESTRA_LOG_ERROR("Failed to create instrument instance for Arsenal Unit " + std::to_string(unitId) + ": " +
                         pluginId);
        return;
    }

    if (!instance->initialize(pm.getDefaultSampleRate(), pm.getDefaultBlockSize())) {
        AESTRA_LOG_ERROR("Failed to initialize instrument instance for Arsenal Unit " + std::to_string(unitId) + ": " +
                         pluginId);
        return;
    }

    unitManager.setUnitName(unitId, unitName);
    unitManager.setUnitGroup(unitId, UnitGroup::Synth);
    unitManager.clearUnitTimelineLane(unitId);
    unitManager.setUnitEnabled(unitId, true);
    unitManager.attachPlugin(unitId, pluginId, instance);
    unitManager.captureUnitPluginState(unitId);

    AESTRA_LOG_DEBUG("Attached instrument '" + unitName + "' to Arsenal Unit " + std::to_string(unitId));
}

void AestraContent::refreshPluginList() {
    if (!m_pluginBrowser)
        return;

    auto& pm = Aestra::Audio::PluginManager::getInstance();
    std::vector<AestraUI::PluginListItem> uiPlugins;
    // Map internal PluginInfo to UI item
    for (const auto& p : pm.getScanner().getScannedPlugins()) {
        AestraUI::PluginListItem item;
        item.id = p.id;
        item.name = p.name;
        item.vendor = p.vendor;
        item.version = p.version;
        item.category = p.category;
        item.formatStr = (p.format == Aestra::Audio::PluginFormat::VST3) ? "VST3" : "CLAP (Exp.)";
        item.typeName = (p.type == Aestra::Audio::PluginType::Instrument) ? "Instrument" : "Effect";
        uiPlugins.push_back(item);
    }
    m_pluginBrowser->setPluginList(uiPlugins);
    AESTRA_LOG_DEBUG("Refreshed plugin list UI: " + std::to_string(uiPlugins.size()) + " plugins found.");
}

void AestraContent::refreshProjectViews() {
    if (m_trackManagerUI) {
        m_trackManagerUI->refreshTracks();
        m_trackManagerUI->invalidateCache();
        m_trackManagerUI->buildAllWaveformCaches();
    }

    if (m_mixerPanel) {
        m_mixerPanel->refreshChannels();
        // Select the master channel so the inspector effect rack binds on next update
        auto viewModel = m_mixerPanel->getViewModel();
        if (viewModel && viewModel->getSelectedChannelId() < 0) {
            if (auto* ch = viewModel->getMaster()) {
                viewModel->setSelectedChannelId(static_cast<int32_t>(ch->id));
                m_lastSelectedChannelId = 0xFFFFFFFFu;
            }
        }
    }

    if (m_patternBrowser) {
        m_patternBrowser->refreshPatterns();
    }

    if (m_sequencerPanel) {
        m_sequencerPanel->refreshUnits();
        const PatternID activePattern = m_sequencerPanel->getActivePatternID();
        if (activePattern.isValid()) {
            const UnitID editingUnit = resolveEditingUnitForPattern(activePattern);
            if (m_patternBrowser) {
                m_patternBrowser->setSelectedPatternId(activePattern, false);
            }
            m_sequencerPanel->setSelectedUnit(editingUnit);
            if (m_pianoRollPanel) {
                m_pianoRollPanel->setEditingUnit(editingUnit);
                m_pianoRollPanel->loadPattern(activePattern);
            }
        }
    }

    setDirty(true);
}

void AestraContent::toggleHistoryPanel() {
    if (!m_historyPanel)
        return;
    bool show = !m_historyPanel->isVisible();
    m_historyPanel->setVisible(show);
    if (show) {
        // Position below transport bar, constrained to content area
        auto root = getBounds();
        float w = 280.0f;
        float h = std::min(500.0f, root.height * 0.65f);
        float x = root.width * 0.5f;
        float y = 80.0f;
        m_viewState.historyRect = AestraUI::NUIRect(x, y, w, h);
        m_historyPanel->setBounds(m_viewState.historyRect);
        m_historyPanel->refreshHistory();
    }
    onResize(static_cast<int>(getBounds().width), static_cast<int>(getBounds().height));
}

// Global Shortcuts
bool AestraContent::onKeyEvent(const AestraUI::NUIKeyEvent& event) {
    // A note release must win even if focus moved after its note-on; otherwise
    // an editable field can consume the key-up and leave a stuck voice.
    if (event.released && m_musicalTyping.handleKeyEvent(event)) {
        if (m_transportBar) {
            m_transportBar->setMusicalTypingStatus(m_musicalTyping.isEnabled(), m_musicalTyping.displayOctave());
        }
        return true;
    }
    if (event.keyCode == AestraUI::NUIKeyCode::Space && event.released) {
        m_spaceShortcutLatched = false;
        return true;
    }

    // Musical-typing note-offs need key RELEASES, which nothing else consumes —
    // handle them before the press-only early-return. Runs even if the piano
    // roll closed mid-hold: releases only match keys that produced a note-on.
    if (event.released && m_keyboardNoteInput.handleKeyEvent(event))
        return true;

    if (!event.pressed)
        return false;

    // Forward to piano roll when visible — its local undo/redo
    // and note shortcuts must take priority over global handlers
    if (m_pianoRollPanel && m_pianoRollPanel->isVisible()) {
        if (m_pianoRollPanel->handleKeyEvent(event))
            return true;

        // Musical typing: unconsumed plain keys play the editing unit live.
        // After the piano roll's own shortcuts, before global handlers.
        // (Focused text inputs never reach here — the window manager gives the
        // focused widget first refusal.)
        if (m_keyboardNoteInput.handleKeyEvent(event))
            return true;
    }

    // Global Undo/Redo — intercept BEFORE any panel processes it
    // This prevents text inputs from capturing Ctrl+Z and ensures
    // undo/redo works regardless of which panel has focus
    if (event.modifiers & AestraUI::NUIModifiers::Ctrl) {
        bool performed = false;

        if (m_trackManager) {
            if ((event.keyCode == AestraUI::NUIKeyCode::Z && (event.modifiers & AestraUI::NUIModifiers::Shift)) ||
                event.keyCode == AestraUI::NUIKeyCode::Y) {
                performed = m_trackManager->getCommandHistory().redo();
                AESTRA_LOG_DEBUG("[AestraContent] Ctrl+Shift+Z/Y pressed, redo=" + std::to_string(performed));
            } else if (event.keyCode == AestraUI::NUIKeyCode::H) {
                toggleHistoryPanel();
                return true;
            } else if (event.keyCode == AestraUI::NUIKeyCode::Z) {
                performed = m_trackManager->getCommandHistory().undo();
                AESTRA_LOG_DEBUG("[AestraContent] Ctrl+Z pressed, undo=" + std::to_string(performed));
            } else {
                AESTRA_LOG_DEBUG("[AestraContent] Ctrl+Key: code=" + std::to_string(static_cast<int>(event.keyCode)));
            }
        }

        if (performed) {
            // Refresh ALL panels — not just timeline
            if (m_trackManagerUI) {
                m_trackManagerUI->refreshTracks();
                m_trackManagerUI->invalidateCache();
            }
            if (m_mixerPanel)
                m_mixerPanel->refreshChannels();
            if (m_sequencerPanel)
                m_sequencerPanel->refreshUnits();
            m_trackManager->markModified();
            return true; // Consume the event — don't pass to text inputs
        }
    }

    // Debug log
    if (event.keyCode == AestraUI::NUIKeyCode::Space) {
        if (m_spaceShortcutLatched) {
            return true;
        }
        m_spaceShortcutLatched = true;
        AESTRA_LOG_DEBUG("[AestraContent] Spacebar pressed. ViewFocus: " +
                         std::to_string(static_cast<int>(m_viewFocus)));
    }

    // Escape closes the routing map overlay if it's open
    if (event.keyCode == AestraUI::NUIKeyCode::Escape) {
        if (m_viewFocus == ViewFocus::RoutingMap) {
            setViewFocus(m_previousViewFocus);
            return true;
        }
    }

    // Spacebar follows the top-level selected mode first. Overlay/panel focus can
    // drift (for example while editing piano roll with Arsenal assets visible),
    // but the segmented mode reflects the user's actual transport context.
    if (event.keyCode == AestraUI::NUIKeyCode::Space) {
        ViewFocus transportFocus = m_viewFocus;
        if (m_viewToggle) {
            switch (m_viewToggle->getSelectedIndex()) {
            case 0:
                transportFocus = ViewFocus::Arsenal;
                break;
            case 2:
                transportFocus = ViewFocus::Audition;
                break;
            case 1:
            default:
                transportFocus = ViewFocus::Timeline;
                break;
            }
        }

        if (transportFocus == ViewFocus::Audition && m_auditionPanel && m_auditionPanel->isVisible()) {
            // If Audition Panel didn't handle it (maybe lost focus?), try to toggle play/pause on engine directly
            if (m_auditionEngine) {
                // Ensure preview stops
                if (!m_auditionEngine->isPlaying())
                    stopSoundPreview();
                m_auditionEngine->togglePlayPause();
                return true;
            }
        } else {
            if (m_transportBar) {
                if (isTransportRolling()) {
                    clearPendingCountIn();
                    m_transportBar->stop();
                } else {
                    m_transportBar->play();
                }
                return true;
            }
        }
    }

    // Standard text inputs own letter keys. Most dispatch paths already give
    // focused widgets first refusal; this explicit guard also protects the
    // root-component path, which routes global shortcuts before focus.
    if (dynamic_cast<AestraUI::NUITextInput*>(AestraUI::NUIComponent::getFocusedComponent()) == nullptr &&
        m_musicalTyping.handleKeyEvent(event)) {
        if (m_transportBar) {
            m_transportBar->setMusicalTypingStatus(m_musicalTyping.isEnabled(), m_musicalTyping.displayOctave());
        }
        return true;
    }

    return false;
}

void AestraContent::releaseMusicalTypingNotes() {
    m_musicalTyping.releaseAllNotes();
}

Aestra::Audio::CommandResult AestraContent::executeMuseCommand(const std::string& input) {
    if (!m_trackManager) {
        Aestra::Audio::CommandResult err;
        err.status = Aestra::Audio::CommandStatus::ExecutionError;
        err.message = "no track manager available";
        return err;
    }

    auto& history = m_trackManager->getCommandHistory();
    Aestra::Audio::CommandResult result = m_commandParser.parse(input, history);

    if (m_sessionLog) {
        // Build resolved args map from the parsed input
        std::unordered_map<std::string, std::string> resolvedArgs;
        std::istringstream stream(input);
        std::string token;
        bool first = true;
        while (stream >> token) {
            if (first) {
                first = false;
                continue;
            }
            if (token.size() >= 3 && token[0] == '-' && token[1] == '-') {
                std::string key = token.substr(2);
                if (stream >> token) {
                    resolvedArgs[key] = token;
                }
            }
        }
        m_sessionLog->append(result, resolvedArgs, input);
    }

    return result;
}

void AestraContent::setMuseSessionDirectory(const std::string& path) {
    m_sessionLog = std::make_unique<Aestra::Audio::SessionLog>(path + "/muse_commands.jsonl");
}
