// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file AestraContent.cpp
 * @brief Main content area for Aestra - Implementation
 */

// Include panel headers FIRST to define complete types before AestraContent.h forward declarations
#include "MixerPanel.h"
#include "PianoRollPanel.h"
#include "ArsenalPanel.h"
#include "PatternBrowserPanel.h"
#include "AuditionPanel.h"                              // Audition Mode UI
#include "AuditionEngine.h"       // Audition Mode backend

#include "AestraContent.h"
#include "../AestraUI/Widgets/PluginBrowserPanel.h"
#include "../AestraUI/Widgets/PluginUIController.h"
#include "../AestraUI/Widgets/UIMixerPanel.h"
#include "../AestraUI/Widgets/UIMixerInspector.h"
#include "PluginManager.h"
#include "MixerChannel.h"

// AestraUI includes
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Platform/NUIPlatformBridge.h"

// Component includes
#include "FileBrowser.h"
#include "FilePreviewPanel.h"
#include "AudioVisualizer.h"
#include "TransportBar.h"
#include "TrackManagerUI.h"

// Audio includes
#include "AudioEngine.h"
#include "TrackManager.h"
#include "PreviewEngine.h"
#include "MiniAudioDecoder.h"
#include "MixerViewModel.h"
#include "ClipSource.h"
#include "../AestraCore/include/AestraLog.h"

#include <cmath>
#include <fstream>
#include <filesystem>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace Aestra;
using namespace AestraUI;
using namespace Aestra::Audio;

// =============================================================================
// SECTION: Construction
// =============================================================================

AestraContent::~AestraContent() {
    // Cancel any running plugin scan to prevent callbacks from accessing dead pointers
    auto& pm = Aestra::Audio::PluginManager::getInstance();
    pm.getScanner().cancelScan();
    // Note: PluginManager's destructor will join the scan thread

    // Clean up temporary audition files
    for (const auto& file : m_tempFiles) {
        try {
            if (std::filesystem::exists(file)) {
                std::filesystem::remove(file);
                Log::info("[AestraContent] Deleted temp audition file: " + file);
            }
        } catch (...) {}
    }
}

AestraContent::AestraContent() {
    // Audio Engine Init
    auto& audioEngine = Aestra::Audio::AudioEngine::getInstance();

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

    m_pluginController->setOnPluginLoaded([this](const std::string&, int) {
        if (m_trackManager) m_trackManager->markModified();
    });

    // Create track manager for multi-track functionality
    m_trackManager = std::make_shared<TrackManager>();
    addDemoTracks();

    // Antigravity Dependencies (v3.1) - Correctly wired in AestraContent
    {
        auto& audioEngine = Aestra::Audio::AudioEngine::getInstance();
        audioEngine.setUnitManager(&m_trackManager->getUnitManager());
        audioEngine.setPatternPlaybackEngine(&m_trackManager->getPatternPlaybackEngine());

        // [FIX] Wire command sink so TrackManager's transport calls (play/pause/stop) 
        // actually reach the AudioEngine's real-time queue.
        m_trackManager->setCommandSink([&audioEngine](const AudioQueueCommand& cmd) {
            audioEngine.commandQueue().push(cmd);
        });
    }

    // Create track manager UI (add to workspace)
    m_trackManagerUI = std::make_shared<TrackManagerUI>(m_trackManager);
    
    // Wire up TrackManagerUI internal toggles to centralized authority (v3.1)
    m_trackManagerUI->setOnToggleMixer([this]() { toggleView(Audio::ViewType::Mixer); });
    m_trackManagerUI->setOnTogglePianoRoll([this]() { toggleView(Audio::ViewType::PianoRoll); });
    m_trackManagerUI->setOnToggleSequencer([this]() { toggleView(Audio::ViewType::Sequencer); });
    m_trackManagerUI->setOnTogglePlaylist([this]() { toggleView(Audio::ViewType::Playlist); });
    
    // Audition Mode integration - sends track to Audition queue and switches mode
    m_trackManagerUI->setOnSendToAudition([this](uint32_t trackId, const std::string& trackName) {
        // 1. Ensure Audition engine exists
        if (!m_auditionEngine) {
            m_auditionEngine = std::make_shared<Audio::AuditionEngine>();
        }
        
        // 2. Determine project duration for full track bounce
        double projectDurationBeats = m_trackManager->getPlaylistModel().getTotalDurationBeats();
        if (projectDurationBeats < 0.1) projectDurationBeats = 16.0; // Default if empty
        
        // 3. Generate temporary file path
        auto tempDir = std::filesystem::temp_directory_path();
        auto tempFile = tempDir / ("AestraAudition_Track_" + std::to_string(trackId) + "_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".wav");
        std::string tempPath = tempFile.string();
        
        // 4. Perform bounce (Offline rendering)
        Log::info("[AestraContent] Bouncing track " + std::to_string(trackId) + " to " + tempPath);
        bool success = m_audioEngine->bounceRangeToWav(0.0, projectDurationBeats, tempPath, static_cast<int32_t>(trackId));
        
        if (success) {
            m_tempFiles.push_back(tempPath);
            
            // 5. Add to queue, select it, play it and switch view
            m_auditionEngine->addToQueue(tempPath);
            
            size_t newIdx = m_auditionEngine->getQueue().size() - 1;
            m_auditionEngine->jumpToTrack(newIdx);
            m_auditionEngine->play();
            
            setViewFocus(ViewFocus::Audition);
        } else {
            Log::error("[AestraContent] Failed to bounce track to Audition.");
        }
    });

    m_trackManagerUI->setOnSendSelectionToAudition([this](double startBeat, double endBeat) {
        if (!m_auditionEngine) {
            m_auditionEngine = std::make_shared<Audio::AuditionEngine>();
        }
        
        // 1. Generate path
        auto tempDir = std::filesystem::temp_directory_path();
        auto tempFile = tempDir / ("AestraAudition_Selection_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".wav");
        std::string tempPath = tempFile.string();
        
        // 2. Perform bounce (trackId -1 means Master Output)
        Log::info("[AestraContent] Bouncing selection to " + tempPath);
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
            Log::error("[AestraContent] Failed to bounce selection to Audition.");
        }
    });
    
    m_workspaceLayer->addChild(m_trackManagerUI);

    // Create Browser Toggle (Files | Plugins)
    m_browserToggle = std::make_shared<AestraUI::NUISegmentedControl>(
        std::vector<std::string>{"Files", "Plugins"}
    );
    m_browserToggle->setCornerRadius(8.0f);
    m_browserToggle->setOnSelectionChanged([this](size_t index) {
        bool showFiles = (index == 0);
        if (m_fileBrowser) m_fileBrowser->setVisible(showFiles);
        if (m_pluginBrowser) m_pluginBrowser->setVisible(!showFiles);
        // Preview panel only relevant for files currently
        if (m_previewPanel) m_previewPanel->setVisible(showFiles);
        
        // Re-layout
        onResize(static_cast<int>(getBounds().width), static_cast<int>(getBounds().height));
    });
    m_workspaceLayer->addChild(m_browserToggle);

    // Create file browser (add to workspace)
    m_fileBrowser = std::make_shared<AestraUI::FileBrowser>();
    m_fileBrowser->setOnFileOpened([this](const AestraUI::FileItem& file) {
        Log::info("File opened: " + file.path);
        loadSampleIntoSelectedTrack(file.path);
    });
    m_fileBrowser->setOnSoundPreview([this](const AestraUI::FileItem& file) {
        Log::info("Sound preview requested: " + file.path);
        playSoundPreview(file);
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
            Log::info("Scan already in progress, ignoring request.");
            return;
        }
        
        Log::info("Scan requested by user.");
        
        // Set UI to scanning state
        if (m_pluginBrowser) {
            m_pluginBrowser->setScanning(true, 0.0f);
            m_pluginBrowser->setScanStatus("Scanning...");
        }
        
        pm.getScanner().scanAsync([this](const std::string& path, int current, int total) {
             // Update progress
             if (m_pluginBrowser && total > 0) {
                 float progress = static_cast<float>(current) / static_cast<float>(total);
                 m_pluginBrowser->setScanning(true, progress);
                 m_pluginBrowser->setScanStatus("Scanning: " + path);
             }
        }, [this](const std::vector<Aestra::Audio::PluginInfo>& results, bool success) {
            // Clear scanning state
            if (m_pluginBrowser) {
                m_pluginBrowser->setScanning(false, 0.0f);
                m_pluginBrowser->setScanStatus("");
            }
            
            if (success) {
                // Map results to UI items
                std::vector<AestraUI::PluginListItem> uiPlugins;
                for (const auto& p : results) {
                    AestraUI::PluginListItem item;
                    item.id = p.id;
                    item.name = p.name;
                    item.vendor = p.vendor;
                    item.version = p.version;
                    item.category = p.category;
                    item.formatStr = (p.format == Aestra::Audio::PluginFormat::VST3) ? "VST3" : "CLAP";
                    item.typeName = (p.type == Aestra::Audio::PluginType::Instrument) ? "Instrument" : "Effect";
                    uiPlugins.push_back(item);
                }
                
                if (m_pluginBrowser) {
                    m_pluginBrowser->setPluginList(uiPlugins);
                }
                Log::info("Scan complete. UI updated with " + std::to_string(results.size()) + " plugins.");
            } else {
                Log::error("Plugin scan failed or cancelled.");
            }
        });
    });

    // Plugin load callback (double-click)
    m_pluginBrowser->setOnPluginLoadRequested([this](const AestraUI::PluginListItem& plugin) {
        Log::info("Plugin load requested: " + plugin.name + " (" + plugin.typeName + ")");
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
    m_previewPanel->setOnPlay([this](const AestraUI::FileItem& file) {
        playSoundPreview(file);
    });
    m_previewPanel->setOnStop([this]() {
        stopSoundPreview();
    });
    m_previewPanel->setOnSeek([this](double seconds) {
        seekSoundPreview(seconds);
    });
    m_workspaceLayer->addChild(m_previewPanel);
    
    // Link file selection to preview panel
    m_fileBrowser->setOnFileSelected([this](const AestraUI::FileItem& file) {
        stopSoundPreview();
        if (m_previewPanel) m_previewPanel->setFile(&file);
    });

    // Create pattern browser panel (add to workspace, side panel)
    m_patternBrowser = std::make_shared<PatternBrowserPanel>(m_trackManager.get());
    m_patternBrowser->setOnPatternSelected([this](PatternID patternId) {
        Log::info("Pattern selected: " + std::to_string(patternId.value));
        if (m_sequencerPanel) {
            m_sequencerPanel->setActivePattern(patternId);
        }
    });
    m_patternBrowser->setOnPatternDragStart([this](PatternID patternId) {
        Log::info("Pattern drag started: " + std::to_string(patternId.value));
    });
    m_patternBrowser->setOnPatternDoubleClick([this](PatternID patternId) {
        Log::info("Pattern double-clicked: " + std::to_string(patternId.value));
        
        if (m_pianoRollPanel && m_trackManager) {
            auto& pm = m_trackManager->getPatternManager();
            auto pattern = pm.getPattern(patternId);
            
            if (pattern && pattern->isMidi()) {
                AestraUI::NUIRect allowed = computeAllowedRectForPanels();
                float editorWidth = std::min(900.0f, allowed.width * 0.8f);
                float editorHeight = std::min(500.0f, allowed.height * 0.7f);
                float editorX = allowed.x + (allowed.width - editorWidth) / 2.0f;
                float editorY = allowed.y + (allowed.height - editorHeight) / 2.0f;
                
                m_viewState.pianoRollRect = AestraUI::NUIRect(editorX, editorY, editorWidth, editorHeight);
                m_pianoRollPanel->loadPattern(patternId);
                setViewOpen(Audio::ViewType::PianoRoll, true);
            }
        }
    });
    m_workspaceLayer->addChild(m_patternBrowser);

    // Create panels (add to overlay)
    m_mixerPanel = std::make_shared<MixerPanel>(m_trackManager);
    m_mixerPanel->setVisible(false);
    m_mixerPanel->setOnClose([this]() { toggleView(Audio::ViewType::Mixer); });
    m_mixerPanel->setOnMaximizeToggle([this](bool) {
        onResize(static_cast<int>(getBounds().width), static_cast<int>(getBounds().height));
    });
    m_mixerPanel->setOnDragStart([this](const AestraUI::NUIPoint& pos) { beginPanelDrag(ViewType::Mixer, pos); });
    m_mixerPanel->setOnDragMove([this](const AestraUI::NUIPoint& pos) { updatePanelDrag(ViewType::Mixer, pos); });
    m_mixerPanel->setOnDragEnd([this]() { endPanelDrag(ViewType::Mixer); });
    m_overlayLayer->addChild(m_mixerPanel);

    m_pianoRollPanel = std::make_shared<PianoRollPanel>(m_trackManager);
    m_pianoRollPanel->setVisible(false);
    m_pianoRollPanel->setOnClose([this]() { 
        m_pianoRollPanel->savePattern();
        toggleView(Audio::ViewType::PianoRoll); 
    });
    m_pianoRollPanel->setOnMaximizeToggle([this](bool) { 
        onResize(static_cast<int>(getBounds().width), static_cast<int>(getBounds().height)); 
    });
    m_pianoRollPanel->setOnDragStart([this](const AestraUI::NUIPoint& pos) { beginPanelDrag(ViewType::PianoRoll, pos); });
    m_pianoRollPanel->setOnDragMove([this](const AestraUI::NUIPoint& pos) { updatePanelDrag(ViewType::PianoRoll, pos); });
    m_pianoRollPanel->setOnDragEnd([this]() { endPanelDrag(ViewType::PianoRoll); });
    m_overlayLayer->addChild(m_pianoRollPanel);
    
    // Create Arsenal panel
    m_sequencerPanel = std::make_shared<ArsenalPanel>(m_trackManager);
    m_sequencerPanel->setPatternBrowser(m_patternBrowser.get());
    
    // Antigravity Bindings (v3.1)
    m_sequencerPanel->setOnRequestEditor([this](UnitID id) {
        if (m_trackManager && m_pluginController) {
            auto plugin = m_trackManager->getUnitManager().getUnitPlugin(id);
            if (plugin) {
                m_pluginController->openPluginEditor(plugin);
            }
        }
    });

    m_sequencerPanel->setOnRequestLoadSample([this](UnitID id) {
        if (!m_fileBrowser) return;
        
        // Show browser if hidden
        if (!m_fileBrowser->isVisible()) {
            m_browserToggle->setSelectedIndex(0); // Select "Files" tab
            m_fileBrowser->setVisible(true);
            if (m_pluginBrowser) m_pluginBrowser->setVisible(false);
            if (m_previewPanel) m_previewPanel->setVisible(true);
            onResize(static_cast<int>(getBounds().width), static_cast<int>(getBounds().height));
        }

        // Set one-shot selection callback
        m_fileBrowser->setOnFileSelected([this, id](const AestraUI::FileItem& file) {
             // 1. Restore default behavior (Preview)
             m_fileBrowser->setOnFileSelected([this](const AestraUI::FileItem& f) {
                 stopSoundPreview();
                 if (m_previewPanel) m_previewPanel->setFile(&f);
             });
             
             // 2. Perform Load
             Log::info("Loading sample into Unit " + std::to_string(id) + ": " + file.path);
             if (m_trackManager) {
                 m_trackManager->getUnitManager().setUnitAudioClip(id, file.path);
             }
        });
    });
    m_patternBrowser->refreshPatterns();
    
    m_sequencerPanel->setVisible(true);
    m_sequencerPanel->setOnClose([this]() { setArsenalPanelVisible(false); });
    m_sequencerPanel->setOnMaximizeToggle([this](bool) { 
        onResize(static_cast<int>(getBounds().width), static_cast<int>(getBounds().height)); 
    });
    m_sequencerPanel->setOnDragStart([this](const AestraUI::NUIPoint& pos) { beginPanelDrag(ViewType::Sequencer, pos); });
    m_sequencerPanel->setOnDragMove([this](const AestraUI::NUIPoint& pos) { updatePanelDrag(ViewType::Sequencer, pos); });
    m_sequencerPanel->setOnDragEnd([this]() { endPanelDrag(ViewType::Sequencer); });
    m_overlayLayer->addChild(m_sequencerPanel);

    // Create transport bar
    m_transportBar = std::make_shared<Aestra::TransportBar>();
    m_transportBar->setOnToggleView([this](Audio::ViewType view) {
        toggleView(view);
    });
    
    // Helper: Stop preview when Audition Queue changes (drop)
    if (m_auditionEngine) {
        m_auditionEngine->setOnQueueUpdated([this]() {
            stopSoundPreview();
        });
    }
    
    // Wire Transport to TrackManager
    m_transportBar->setOnPlay([this]() { if(m_trackManager) m_trackManager->play(); });
    m_transportBar->setOnPause([this]() { if(m_trackManager) m_trackManager->pause(); });
    m_transportBar->setOnStop([this]() { 
        if(m_trackManager) m_trackManager->stop();
        if(m_auditionEngine && m_viewFocus == ViewFocus::Audition) m_auditionEngine->stop();
        stopSoundPreview();
        m_audioEngine->panic(); // Ensure silence
    });
    m_transportBar->setOnRecord([this](bool recording) { 
        if(m_trackManager) {
            // Robust toggle: Check backend state to prevent accidental restarts or desync
            bool isEngineRecording = m_trackManager->isRecording();
            
            // Only trigger action if the requested UI state differs from Engine state
            if (recording != isEngineRecording) {
                m_trackManager->record();
            }
        }
    });
    
    m_transportBar->setOnMetronomeToggle([this](bool enabled) {
        if(m_trackManager) m_trackManager->enableMetronome(enabled);
    });

    m_overlayLayer->addChild(m_transportBar);
    
    // Create Focus Toggle Buttons
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    m_viewToggle = std::make_shared<AestraUI::NUISegmentedControl>(
        std::vector<std::string>{"Arsenal", "Timeline", "Audition"}
    );
    m_viewToggle->setCornerRadius(12.0f);
    m_viewToggle->setOnSelectionChanged([this](size_t index) {
        ViewFocus newFocus;
        switch (index) {
            case 0: newFocus = ViewFocus::Arsenal; break;
            case 1: newFocus = ViewFocus::Timeline; break;
            case 2: newFocus = ViewFocus::Audition; break;
            default: newFocus = ViewFocus::Timeline; break;
        }
        setViewFocus(newFocus);
        
        // Auto-open Arsenal panel when switching TO Arsenal mode
        if (newFocus == ViewFocus::Arsenal) {
            setArsenalPanelVisible(true);
        }
    });
    
    // Initialize with Timeline mode as default (panel closed)
    m_viewToggle->setSelectedIndex(1);  // Select "Timeline"
    setViewFocus(ViewFocus::Timeline);
    setArsenalPanelVisible(false);

    // Create compact master meters
    m_waveformVisualizer = std::make_shared<AestraUI::AudioVisualizer>();
    m_waveformVisualizer->setMode(AestraUI::AudioVisualizationMode::ArrangementWaveform);
    m_waveformVisualizer->setShowStereo(true);
    m_overlayLayer->addChild(m_waveformVisualizer);

    m_audioVisualizer = std::make_shared<AestraUI::AudioVisualizer>();
    m_audioVisualizer->setMode(AestraUI::AudioVisualizationMode::CompactMeter);
    m_audioVisualizer->setShowStereo(true);
    m_overlayLayer->addChild(m_audioVisualizer);

    // Initialize preview engine
    m_previewEngine = std::make_unique<PreviewEngine>();
    m_previewIsPlaying = false;
    m_previewDuration = 8.0;
    
    syncViewState();
}

// =============================================================================
// SECTION: Lifecycle
// =============================================================================

void AestraContent::onUpdate(double dt) {
    // Sync track changes to MixerViewModel
    auto tm = getTrackManager();
    if (tm && tm->isModified()) {
         if (m_mixerPanel) {
             auto viewModel = m_mixerPanel->getViewModel();
             if (viewModel) {
                 auto slotMap = tm->getChannelSlotMapRaw();
                 if (slotMap) {
                    viewModel->syncFromEngine(*tm, *slotMap);
                    m_mixerPanel->refreshChannels();
                    Log::info("Refreshed mixer channels from engine update");
                    
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
         }
         tm->setModified(false);
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
                    auto channel = selectedCh->channel.lock();
                    if (channel) {
                        auto mixerUI = m_mixerPanel->getMixerUI();
                        if (mixerUI) {
                            auto inspector = mixerUI->getInspector();
                            if (inspector && inspector->getEffectRack()) {
                                m_pluginController->bindEffectRack(inspector->getEffectRack().get(), &channel->getEffectChain());
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Update Mixer Meters (Real-time)
    if (tm && m_mixerPanel) {
        auto viewModel = m_mixerPanel->getViewModel();
        if (viewModel) {
            auto snapshots = tm->getMeterSnapshots();
            if (snapshots) {
                viewModel->updateMeters(*snapshots, dt);
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
    AestraUI::NUIColor statusColor = m_audioActive ?
        themeManager.getColor("accentLime") : themeManager.getColor("error");

    AestraUI::NUIRect contentArea(bounds.x, bounds.y + transportHeight,
                                 width, height - transportHeight);
    renderer.fillRect(contentArea, themeManager.getColor("backgroundPrimary"));
    renderer.strokeRect(contentArea, 1, themeManager.getColor("border"));

    renderChildren(renderer);
}

void AestraContent::onResize(int width, int height) {
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();

    AestraUI::NUIRect contentBounds = getBounds();

    if (m_workspaceLayer) m_workspaceLayer->setBounds(contentBounds);
    if (m_overlayLayer) m_overlayLayer->setBounds(contentBounds);

    // DYNAMIC LAYOUT: Calculate effective top margin
    float effectiveTransportHeight = layout.transportBarHeight;
    bool isAuditionMode = (m_viewFocus == ViewFocus::Audition);
    
    if (isAuditionMode) {
        effectiveTransportHeight = 0.0f;
    }

    // Transport Bar Positioning
    if (m_transportBar) {
        if (isAuditionMode) {
            // Hide transport bar physically (prevent mouse hits)
            m_transportBar->setBounds(AestraUI::NUIRect(0, -100, 0, 0));
        } else {
            m_transportBar->setBounds(AestraUI::NUIAbsolute(contentBounds, 0, 0,
                contentBounds.width, layout.transportBarHeight));
        }
    }
    
    if (m_viewToggle) {
        auto rootBounds = getBounds();
        float toggleWidth = 160.0f;
        float toggleHeight = 24.0f;
        float yPos = 4.0f;
        
        float centerX = rootBounds.width / 2.0f;
        float startX = centerX - toggleWidth / 2.0f;
        
        m_viewToggle->setBounds(AestraUI::NUIRect(startX, yPos, toggleWidth, toggleHeight));
    }
    
    if (m_scopeLabel) {
        float labelX = 280.0f + 85.0f;
        float labelY = 15.0f;
        m_scopeLabel->setBounds(AestraUI::NUIAbsolute(contentBounds, 
            labelX, labelY, 150.0f, 30.0f));
    }

    float fileBrowserWidth = 0;
    float sidebarHeight = height - effectiveTransportHeight;
    float patternBrowserWidth = 0;
    
    if (m_browserToggle) {
        // Toggle bar above browser
        float toggleHeight = 30.0f;
        float toggleY = effectiveTransportHeight;
        float browserWidth = std::min(layout.fileBrowserWidth, width * 0.20f);
        
        m_browserToggle->setBounds(AestraUI::NUIAbsolute(contentBounds, 0, toggleY, browserWidth, toggleHeight));
    }

    if (m_fileBrowser) {
        fileBrowserWidth = std::min(layout.fileBrowserWidth, width * 0.20f);
        float toggleHeight = 30.0f;
        float fbTop = effectiveTransportHeight + toggleHeight;
        float fbHeight = sidebarHeight - toggleHeight;
        
        if (m_previewPanel && m_previewPanel->isVisible()) {
           float previewHeight = 90.0f;
           fbHeight -= previewHeight;
           
           m_previewPanel->setBounds(AestraUI::NUIAbsolute(contentBounds, 0, fbTop + fbHeight,
               fileBrowserWidth, previewHeight));
        }
        
        m_fileBrowser->setBounds(AestraUI::NUIAbsolute(contentBounds, 0, fbTop,
                                                   fileBrowserWidth, fbHeight));
    }
    
    if (m_pluginBrowser) {
        // Occupies same space as file browser + preview panel
        float toggleHeight = 30.0f;
        float pbTop = effectiveTransportHeight + toggleHeight;
        float pbHeight = sidebarHeight - toggleHeight;
        // recalculate width locally just in case
        float pbWidth = std::min(layout.fileBrowserWidth, width * 0.20f);
        
        m_pluginBrowser->setBounds(AestraUI::NUIAbsolute(contentBounds, 0, pbTop, pbWidth, pbHeight));
    }
    
    // Ensure fileBrowserWidth is consistent even if m_fileBrowser is mysteriously absent (unlikely but safe)
    if (fileBrowserWidth <= 0.1f) {
        fileBrowserWidth = std::min(layout.fileBrowserWidth, width * 0.20f);
    }
    
    if (m_patternBrowser) {
        patternBrowserWidth = std::min(layout.fileBrowserWidth * 0.8f, width * 0.15f);
        float patternBrowserX = fileBrowserWidth;
        m_patternBrowser->setBounds(AestraUI::NUIAbsolute(contentBounds, patternBrowserX, effectiveTransportHeight,
                                                        patternBrowserWidth, sidebarHeight));
    }

    if (m_audioVisualizer || m_waveformVisualizer) {
        const float meterWidth = 60.0f;
        const float waveformWidth = 150.0f;
        const float visualizerHeight = 40.0f;
        const float gap = 6.0f;
        float vuY = (layout.transportBarHeight - visualizerHeight) / 2.0f;

        float totalWidth = meterWidth;
        if (m_waveformVisualizer) totalWidth += waveformWidth + gap;

        float xStart = width - totalWidth - layout.panelMargin;
        if (m_waveformVisualizer) {
            m_waveformVisualizer->setBounds(AestraUI::NUIAbsolute(contentBounds, xStart, vuY, waveformWidth, visualizerHeight));
            xStart += waveformWidth + gap;
        }
        if (m_audioVisualizer) {
            m_audioVisualizer->setBounds(AestraUI::NUIAbsolute(contentBounds, xStart, vuY, meterWidth, visualizerHeight));
        }
    }

    if (m_trackManagerUI) {
        float trackAreaX = fileBrowserWidth + patternBrowserWidth;
        float trackAreaWidth = width - trackAreaX;
        float trackAreaHeight = height - layout.transportBarHeight;
        m_trackManagerUI->setBounds(AestraUI::NUIAbsolute(contentBounds, trackAreaX, layout.transportBarHeight, trackAreaWidth, trackAreaHeight));
        
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
                 m_auditionPanel->setBounds(AestraUI::NUIAbsolute(contentBounds, fileBrowserWidth, 0, 
                    width - fileBrowserWidth, height));
            } else {
                 m_auditionPanel->setBounds(AestraUI::NUIAbsolute(contentBounds, trackAreaX, layout.transportBarHeight, trackAreaWidth, trackAreaHeight));
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

    if (m_workspaceLayer) {
        m_workspaceLayer->setBounds(NUIRect(0, 0, static_cast<float>(width), static_cast<float>(height)));
    }
    if (m_overlayLayer) {
        m_overlayLayer->setBounds(NUIRect(0, 0, static_cast<float>(width), static_cast<float>(height)));
    }

    AestraUI::NUIComponent::onResize(width, height);
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
                    if (open) m_mixerPanel->refreshChannels();
                }
                changed = true;
            }
            break;
        case Audio::ViewType::PianoRoll:
            if (m_viewState.pianoRollOpen != open) {
                m_viewState.pianoRollOpen = open;
                if (m_pianoRollPanel) m_pianoRollPanel->setVisible(open);
                changed = true;
            }
            break;
        case Audio::ViewType::Sequencer:
            if (m_viewState.sequencerOpen != open) {
                m_viewState.sequencerOpen = open;
                if (m_sequencerPanel) m_sequencerPanel->setVisible(open);
                changed = true;
            }
            break;
        case Audio::ViewType::Playlist:
            if (m_viewState.playlistActive != open) {
                m_viewState.playlistActive = open;
                if (m_trackManagerUI) m_trackManagerUI->setVisible(open);
                changed = true;
            }
            break;
    }

    if (changed) {
        Log::info("[ViewState] View changed: " + std::to_string(static_cast<int>(view)) + " -> " + (open ? "OPEN" : "CLOSED"));
        syncViewState();
        
        if (open) {
            AestraUI::NUIRect allowed = computeAllowedRectForPanels();
            switch (view) {
                case Audio::ViewType::Mixer:
                    m_viewState.mixerRect = clampRectToAllowed(m_viewState.mixerRect, allowed);
                    if (m_mixerPanel) m_mixerPanel->setBounds(m_viewState.mixerRect);
                    break;
                case Audio::ViewType::PianoRoll:
                    m_viewState.pianoRollRect = clampRectToAllowed(m_viewState.pianoRollRect, allowed);
                    if (m_pianoRollPanel) m_pianoRollPanel->setBounds(m_viewState.pianoRollRect);
                    break;
                case Audio::ViewType::Sequencer:
                    m_viewState.sequencerRect = clampRectToAllowed(m_viewState.sequencerRect, allowed);
                    if (m_sequencerPanel) m_sequencerPanel->setBounds(m_viewState.sequencerRect);
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
        case Audio::ViewType::Mixer: current = m_viewState.mixerOpen; break;
        case Audio::ViewType::PianoRoll: current = m_viewState.pianoRollOpen; break;
        case Audio::ViewType::Playlist: current = m_viewState.playlistActive; break;
        default: break;
    }
    setViewOpen(view, !current);
}

void AestraContent::toggleFileBrowser() {
    if (m_fileBrowser) {
        bool isVisible = m_fileBrowser->isVisible();
        m_fileBrowser->setVisible(!isVisible);
        Log::info("File Browser toggled: " + std::string(!isVisible ? "VISIBLE" : "HIDDEN"));
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

void AestraContent::setViewFocus(ViewFocus focus) {
    // Guard against re-entrancy (setSelectedIndex triggers callback which calls setViewFocus)
    static bool isUpdating = false;
    if (isUpdating) return;
    isUpdating = true;
    
    bool wasPlaying = (m_transportBar && m_transportBar->getState() == TransportState::Playing);
    ViewFocus previousFocus = m_viewFocus;
    
    m_viewFocus = focus;
    
    // Handle mode transitions
    if (m_audioEngine) {
        // === ENTERING ARSENAL ===
        if (focus == ViewFocus::Arsenal) {
            // Default to 4 bars (16 beats) if panel not ready
            int stepCount = m_sequencerPanel ? m_sequencerPanel->getStepCount() : 16;
            double lengthBeats = stepCount * 0.25;
            
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
            if (m_auditionPanel) m_auditionPanel->setVisible(false);
            if (m_trackManagerUI) m_trackManagerUI->setVisible(true);
        }
        // === ENTERING TIMELINE ===
        else if (focus == ViewFocus::Timeline) {
            // Stop any running playback from previous mode
            if (previousFocus == ViewFocus::Arsenal) {
                if (m_trackManager && m_trackManager->isPlaying()) {
                    m_trackManager->stop();
                }
                m_audioEngine->panic(); // Kill all voices/ring-outs
            }
            
            // Restore playback position
            m_audioEngine->setPatternPlaybackMode(false, 4.0);
            m_audioEngine->setAuditionModeEnabled(false);
            
            if (m_trackManager) {
                m_trackManager->setPosition(m_savedTimelinePosition);
            }
            
            if (m_trackManagerUI) {
                m_trackManagerUI->setPatternMode(false);
                m_trackManagerUI->setFollowPlayhead(true); // Resume scrolling
            }
            
            // Hide Audition panel if it exists (returning from Audition)
            if (m_auditionPanel) m_auditionPanel->setVisible(false);
            if (m_trackManagerUI) m_trackManagerUI->setVisible(true);
        }
        // === ENTERING AUDITION ===
        else if (focus == ViewFocus::Audition) {
            // Stop main DAW playback - Audition has its own engine
            if (m_trackManager && m_trackManager->isPlaying()) {
                m_trackManager->stop();
            }
            m_audioEngine->panic(); // Silence all DAW audio
            m_audioEngine->setPatternPlaybackMode(false, 4.0);
            
            // Stop any file browser preview
            stopSoundPreview();
            if (m_previewEngine) m_previewEngine->stop();
            
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
            
            // Enable Exclusive Audition Mode (bypasses main DAW graph)
            m_audioEngine->setAuditionModeEnabled(true);
            
            if (!m_auditionPanel) {
                m_auditionPanel = std::make_shared<AuditionPanel>(m_auditionEngine);
                // Fix Z-Order: Add to workspace layer (below overlay/transport), not root
                if (m_workspaceLayer) {
                     m_workspaceLayer->addChild(m_auditionPanel);
                } else {
                     addChild(m_auditionPanel);
                }
            }
            
            // Show the Audition panel, hide DAW panels
            m_auditionPanel->setVisible(true);
            if (m_trackManagerUI) m_trackManagerUI->setVisible(false);
            if (m_sequencerPanel) m_sequencerPanel->setVisible(false);
            
            // POLISH: Hide Transport, Pattern Browser, and Visualizers for immersion
            if (m_transportBar) m_transportBar->setVisible(false);
            if (m_patternBrowser) m_patternBrowser->setVisible(false);
            if (m_waveformVisualizer) m_waveformVisualizer->setVisible(false);
            if (m_audioVisualizer) m_audioVisualizer->setVisible(false);
            
            Log::info("[ViewFocus] Entering Audition Mode");
        } 
        
        // GLOBAL VISIBILITY STATE MANAGEMENT
        // Ensure UI elements are correctly shown/hidden based on mode
        bool isAudition = (focus == ViewFocus::Audition);
        
        if (m_auditionPanel) m_auditionPanel->setVisible(isAudition);
        
        if (!isAudition) {
             // Returning to DAW Mode - Restore UI
             if (m_transportBar) m_transportBar->setVisible(true);
             if (m_patternBrowser) m_patternBrowser->setVisible(true);
             if (m_waveformVisualizer) m_waveformVisualizer->setVisible(true);
             if (m_audioVisualizer) m_audioVisualizer->setVisible(true);
        } else {
             // Audition Mode - Hide Distractions
             if (m_transportBar) m_transportBar->setVisible(false);
             if (m_patternBrowser) m_patternBrowser->setVisible(false);
             if (m_waveformVisualizer) m_waveformVisualizer->setVisible(false);
             if (m_audioVisualizer) m_audioVisualizer->setVisible(false);
        }
        
        // Sync segment control to reflect the new focus
        if (m_viewToggle) {
            size_t idx = (focus == ViewFocus::Arsenal) ? 0 : (focus == ViewFocus::Timeline) ? 1 : 2;
            m_viewToggle->setSelectedIndex(idx);
        }
        
        // Force layout update immediately to apply new visibility and margins
        onResize(getBounds().width, getBounds().height);
    }
    
    // Update transport bar mode indicator (mode only, no panel visibility)
    if (m_transportBar) {
        m_transportBar->setViewToggled(Audio::ViewType::Sequencer, focus == ViewFocus::Arsenal);
    }
    
    // Hot-swap playback if needed (only for Arsenal/Timeline swap, not Audition)
    if (wasPlaying && m_transportBar && focus != ViewFocus::Audition && previousFocus != ViewFocus::Audition) {
        Log::info("[Focus] Hot-swapping playback mode");
        m_transportBar->stop();
        m_transportBar->play();
    }
    
    isUpdating = false;
}

void AestraContent::setArsenalPanelVisible(bool visible) {
    if (!m_sequencerPanel) return;
    
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
        
        if (!m_sequencerPanel->isVisible()) {
            m_sequencerPanel->setOpacity(0.0f);
        }
        m_sequencerPanel->setVisible(true);
        m_sequencerPanel->setOpacity(1.0f);
        m_sequencerPanel->setBounds(m_viewState.sequencerRect);
    } else {
        m_sequencerPanel->setVisible(false);
        m_sequencerPanel->setOpacity(1.0f);
    }
    m_viewState.sequencerOpen = visible;
}

void AestraContent::toggleArsenalPanel() {
    setArsenalPanelVisible(!m_viewState.sequencerOpen);
}

// =============================================================================
// SECTION: Panel Physics & Constraints
// =============================================================================

AestraUI::NUIRect AestraContent::computeSafeRect() const {
    AestraUI::NUIRect bounds = getBounds();
    
    float transportHeight = 60.0f;
    if (m_transportBar) transportHeight = m_transportBar->getHeight();
    
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

AestraUI::NUIRect AestraContent::computeAllowedRectForPanels() const {
    AestraUI::NUIRect safe = computeSafeRect();
    
    if (m_fileBrowser && m_fileBrowser->isVisible()) {
        AestraUI::NUIRect browserRect = m_fileBrowser->getBounds();
        float margin = 0.0f;
        float browserEdge = browserRect.width + margin;
        
        if (browserEdge > safe.x) {
           float shift = browserEdge - safe.x;
           safe.x = browserEdge;
           safe.width -= shift; 
        }
        
        if (safe.width < 100.0f) safe.width = 100.0f;
    }
    
    return safe;
}

AestraUI::NUIRect AestraContent::computeMaximizedRect() const {
    AestraUI::NUIRect bounds = getBounds();
    
    float transportHeight = 60.0f;
    if (m_transportBar) transportHeight = m_transportBar->getHeight();
    
    AestraUI::NUIRect maxRect = bounds;
    maxRect.y += transportHeight;
    maxRect.height -= transportHeight;
    
    if (m_fileBrowser && m_fileBrowser->isVisible()) {
        AestraUI::NUIRect browserRect = m_fileBrowser->getBounds();
        float browserEdge = browserRect.width;
        
        if (browserEdge > maxRect.x) {
           float shift = browserEdge - maxRect.x;
           maxRect.x = browserEdge;
           maxRect.width -= shift; 
        }
    }
    
    return maxRect;
}

AestraUI::NUIRect AestraContent::clampRectToAllowed(AestraUI::NUIRect panel, const AestraUI::NUIRect& allowed) const {
    float x = std::clamp(panel.x, allowed.x, allowed.right() - panel.width);
    float y = std::clamp(panel.y, allowed.y, allowed.bottom() - panel.height);
    
    float w = panel.width;
    float h = panel.height;
    
    if (w > allowed.width) w = allowed.width;
    if (h > allowed.height) h = allowed.height;
    
    x = std::clamp(x, allowed.x, allowed.right() - w);
    y = std::clamp(y, allowed.y, allowed.bottom() - h);

    return AestraUI::NUIRect(x, y, w, h);
}

// =============================================================================
// SECTION: Panel Drag Handlers
// =============================================================================

void AestraContent::beginPanelDrag(Audio::ViewType view, const AestraUI::NUIPoint& mouseScreen) {
    if (!m_overlayLayer) return;
    
    m_viewState.isDragging = true;
    m_viewState.draggingView = view;
    m_viewState.dragStartMouseOverlay = m_overlayLayer->globalToLocal(mouseScreen);
    
    switch (view) {
        case Audio::ViewType::Mixer: m_viewState.dragStartRect = m_viewState.mixerRect; break;
        case Audio::ViewType::PianoRoll: m_viewState.dragStartRect = m_viewState.pianoRollRect; break;
        case Audio::ViewType::Sequencer: m_viewState.dragStartRect = m_viewState.sequencerRect; break;
        default: break;
    }
    
    Log::info("Started dragging panel: " + std::to_string(static_cast<int>(view)));
}

void AestraContent::updatePanelDrag(Audio::ViewType view, const AestraUI::NUIPoint& mouseScreen) {
    if (!m_viewState.isDragging || !m_overlayLayer || view != m_viewState.draggingView) return;
    
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
            if (m_mixerPanel) m_mixerPanel->setBounds(finalRect);
            break;
        case Audio::ViewType::PianoRoll:
            m_viewState.pianoRollRect = finalRect;
            if (m_pianoRollPanel) m_pianoRollPanel->setBounds(finalRect);
            break;
        case Audio::ViewType::Sequencer:
            m_viewState.sequencerRect = finalRect;
            if (m_sequencerPanel) m_sequencerPanel->setBounds(finalRect);
            break;
        default: break;
    }
    
    setDirty(true);
}

void AestraContent::endPanelDrag(Audio::ViewType view) {
     m_viewState.isDragging = false;
     Log::info("Ended dragging panel: " + std::to_string(static_cast<int>(view)));
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

std::shared_ptr<AestraUI::FileBrowser> AestraContent::getFileBrowser() const { 
    return m_fileBrowser; 
}

void AestraContent::setPlatformBridge(AestraUI::NUIPlatformBridge* bridge) {
    m_platformBridge = bridge;
    if (m_trackManagerUI) {
        m_trackManagerUI->setPlatformWindow(bridge);
    }
}

// =============================================================================
// SECTION: Demo & Testing
// =============================================================================

void AestraContent::resetToDefaultProject() {
    Log::info("resetToDefaultProject() - clearing and recreating default state");
    
    if (!m_trackManager) return;
    
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
    
    Log::info("resetToDefaultProject() completed");
}

void AestraContent::addDemoTracks() {
    Log::info("addDemoTracks() called - starting demo track creation (v3.0)");

    if (!m_trackManager) return;
    auto& playlist = m_trackManager->getPlaylistModel();

    const int DEFAULT_TRACK_COUNT = 50;
    
    for (int i = 1; i <= DEFAULT_TRACK_COUNT; ++i) {
        std::string name = "Track " + std::to_string(i);
        PlaylistLaneID laneId = playlist.createLane(name);
        m_trackManager->addChannel(name);
        
        if (auto* lane = playlist.getLane(laneId)) {
            if (i % 3 == 1) lane->colorRGBA = 0xFFbb86fc;
            else if (i % 3 == 2) lane->colorRGBA = 0xFF00bcd4;
            else lane->colorRGBA = 0xFF9a9aa3;

            if (i == 1) {
                AutomationCurve vol("Volume", AutomationTarget::Volume);
                vol.setDefaultValue(0.8);
                vol.addPoint(0.0, 0.5, 0.5f);
                vol.addPoint(4.0, 1.0, 0.5f);
                vol.addPoint(8.0, 0.2, 0.5f);
                vol.addPoint(12.0, 0.8, 0.5f);
                lane->automationCurves.push_back(vol);
            }
        }
    }

    if (m_trackManagerUI) {
        m_trackManagerUI->refreshTracks();
    }
    Log::info("addDemoTracks() completed - created " + std::to_string(DEFAULT_TRACK_COUNT) + " lanes/channels");
}

bool AestraContent::generateTestWavFile(const std::string& filename, float frequency, double duration) {
    Log::info("generateTestWavFile called for: " + filename);

    std::ifstream checkFile(filename);
    if (checkFile) {
        checkFile.close();
        Log::info("File already exists: " + filename);
        return true;
    }
    checkFile.close();

    Log::info("Generating test WAV file: " + filename + " (" + std::to_string(frequency) + " Hz, " + std::to_string(duration) + "s)");

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
        Log::error("Failed to create test WAV file: " + filename);
        return false;
    }

    wavFile.write(reinterpret_cast<char*>(&header), sizeof(header));
    wavFile.write(reinterpret_cast<char*>(audioData.data()), audioData.size() * sizeof(int16_t));
    wavFile.close();

    std::ifstream verifyFile(filename, std::ios::binary);
    if (verifyFile) {
        verifyFile.seekg(0, std::ios::end);
        verifyFile.close();
        Log::info("Test WAV file generated successfully: " + filename);
        return true;
    } else {
        Log::error("Failed to verify WAV file creation: " + filename);
        return false;
    }
}

// =============================================================================
// SECTION: Sound Preview
// =============================================================================

void AestraContent::playSoundPreview(const AestraUI::FileItem& file) {
    Log::info("Playing sound preview for: " + file.path);

    stopSoundPreview();

    if (!m_previewEngine) {
        Log::error("Preview engine not initialized");
        return;
    }

    m_previewDuration = 8.0;
    auto result = m_previewEngine->play(file.path, -6.0f, m_previewDuration);
    
    if (result == PreviewResult::Success || result == PreviewResult::Pending) {
        m_previewIsPlaying = true;
        m_previewStartTime = std::chrono::steady_clock::now();
        m_currentPreviewFile = file.path;
        
        if (result == PreviewResult::Success) {
            Log::info("Sound preview started (cache hit)");
            if (m_previewPanel) m_previewPanel->setPlaying(true);
        } else {
            if (m_previewPanel) {
                m_previewPanel->setLoading(true);
            }
            Log::info("Sound preview pending (async decode)");
        }
    } else {
        Log::warning("Failed to load preview audio: " + file.path);
    }
}

void AestraContent::stopSoundPreview() {
    if (m_previewPanel) {
        m_previewPanel->setLoading(false);
        m_previewPanel->setPlaying(false);
    }
    
    if (m_previewEngine && m_previewIsPlaying) {
        Log::info("stopSoundPreview called - wasPlaying: true");
        m_previewEngine->stop();
        m_previewIsPlaying = false;
        Log::info("Sound preview stopped (file path preserved)");
    }
}

void AestraContent::loadSampleIntoSelectedTrack(const std::string& filePath) {
    Log::info("=== Loading sample into arrangement: " + filePath + " ===");
    
    stopSoundPreview();
    
    if (!m_trackManager) {
        Log::error("TrackManager not initialized");
        return;
    }
    
    auto& sourceManager = m_trackManager->getSourceManager();
    ClipSourceID sourceId = sourceManager.getOrCreateSource(filePath);
    
    if (!sourceId.isValid()) {
        Log::error("Failed to load sample source: " + filePath);
        return;
    }
    
    ClipSource* source = sourceManager.getSource(sourceId);
    if (source && !source->isReady()) {
        Log::info("Decoding source in Main: " + filePath);
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
        Log::error("Failed to decode or ready sample source: " + filePath);
        return;
    }

    double durationSeconds = source->getDurationSeconds();
    double durationBeats = m_trackManager->getPlaylistModel().secondsToBeats(durationSeconds);
    
    auto& patternManager = m_trackManager->getPatternManager();
    AudioSlicePayload payload;
    payload.audioSourceId = sourceId;
    payload.slices.push_back({0.0, static_cast<double>(source->getNumFrames())});
    
    std::string patternName = std::filesystem::path(filePath).filename().string();
    PatternID patternId = patternManager.createAudioPattern(patternName, durationBeats, payload);
    
    if (!patternId.isValid()) {
        Log::error("Failed to create pattern for sample");
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
    
    playlist.addClipFromPattern(targetLaneId, patternId, startBeat, durationBeats);

    if (m_trackManagerUI) {
        m_trackManagerUI->refreshTracks();
        m_trackManagerUI->invalidateCache();
    }
    
    Log::info("Sample loaded into arrangement via v3.0 architecture");
}

void AestraContent::updateSoundPreview() {
    if (m_previewEngine && m_previewIsPlaying) {
        if (m_previewEngine->isBufferReady() && m_previewPanel) {
            m_previewPanel->setLoading(false);
            if (m_previewEngine->isPlaying()) m_previewPanel->setPlaying(true);
        }
        
        if (!m_previewEngine->isPlaying()) {
            stopSoundPreview();
        } else {
            auto currentTime = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(currentTime - m_previewStartTime);
            if (elapsed.count() >= m_previewDuration) {
                stopSoundPreview();
            }
        }
    }
}

void AestraContent::seekSoundPreview(double seconds) {
    if (m_previewEngine) {
        bool engineIsPlaying = m_previewEngine->isPlaying();
        
        m_previewDuration = 300.0; 

        if ((!m_previewIsPlaying || !engineIsPlaying) && !m_currentPreviewFile.empty()) {
            m_previewEngine->play(m_currentPreviewFile, -6.0f, m_previewDuration);
            m_previewIsPlaying = true;
            m_previewStartTime = std::chrono::steady_clock::now();
            if (m_previewPanel) m_previewPanel->setPlaying(true);
        }
        m_previewEngine->seek(seconds);
    }
}

bool AestraContent::isPlayingPreview() const { 
    return m_previewIsPlaying; 
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
    if (!m_trackManager) return;
    
    // 2. Get first MixerChannel (track selection not implemented yet)
    // TODO: Add track selection UI and pass selected index here
    size_t trackIndex = 0;
    auto channel = m_trackManager->getChannel(trackIndex);
    if (!channel) {
        Log::error("Cannot load effect: No mixer channel at index " + std::to_string(trackIndex));
        return;
    }
    
    // 3. Create plugin instance via PluginManager
    auto& pm = Aestra::Audio::PluginManager::getInstance();
    auto instance = pm.createInstanceById(pluginId);
    if (!instance) {
        Log::error("Failed to create plugin instance for: " + pluginId);
        return;
    }
    
    // 4. Insert into first available effect chain slot
    auto& chain = channel->getEffectChain();
    size_t slot = chain.getFirstEmptySlot();
    if (slot < Aestra::Audio::EffectChain::MAX_SLOTS) {
        chain.insertPlugin(slot, std::move(instance));
        m_trackManager->markModified();
        Log::info("Loaded effect to Track " + std::to_string(trackIndex + 1) + " slot " + std::to_string(slot));
    } else {
        Log::warning("No empty effect slots on Track " + std::to_string(trackIndex + 1));
    }
}

void AestraContent::loadInstrumentToArsenal(const std::string& pluginId) {
    // 1. Get UnitManager from TrackManager
    if (!m_trackManager) return;
    auto& unitManager = m_trackManager->getUnitManager();
    
    // 2. Find plugin info to get the name
    auto& pm = Aestra::Audio::PluginManager::getInstance();
    const auto* pluginInfo = pm.findPlugin(pluginId);
    std::string unitName = "Plugin Synth";
    if (pluginInfo) {
        unitName = pluginInfo->name;
    }
    
    // 3. Create new Unit with the plugin name
    UnitID newUnit = unitManager.createUnit(unitName);
    
    // TODO: Future - attach plugin instance to unit for audio processing
    // For now, just create the unit placeholder
    
    // 4. Refresh Arsenal UI
    if (m_sequencerPanel) {
        m_sequencerPanel->refreshUnits();
    }
    
    Log::info("Loaded instrument '" + unitName + "' to Arsenal as Unit " + std::to_string(newUnit));
}

void AestraContent::refreshPluginList() {
    if (!m_pluginBrowser) return;

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
        item.formatStr = (p.format == Aestra::Audio::PluginFormat::VST3) ? "VST3" : "CLAP";
        item.typeName = (p.type == Aestra::Audio::PluginType::Instrument) ? "Instrument" : "Effect";
        uiPlugins.push_back(item);
    }
    m_pluginBrowser->setPluginList(uiPlugins);
    Aestra::Log::info("Refreshed plugin list UI: " + std::to_string(uiPlugins.size()) + " plugins found.");
}

