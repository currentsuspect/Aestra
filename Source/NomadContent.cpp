// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file NomadContent.cpp
 * @brief Main content area for NOMAD DAW - Implementation
 */

// Include panel headers FIRST to define complete types before NomadContent.h forward declarations
#include "MixerPanel.h"
#include "PianoRollPanel.h"
#include "ArsenalPanel.h"
#include "PatternBrowserPanel.h"

#include "NomadContent.h"

// NomadUI includes
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Platform/NUIPlatformBridge.h"

// Component includes
#include "FileBrowser.h"
#include "FilePreviewPanel.h"
#include "AudioVisualizer.h"
#include "TransportBar.h"
#include "TrackManagerUI.h"

// Audio includes
#include "../NomadAudio/include/TrackManager.h"
#include "../NomadAudio/include/PreviewEngine.h"
#include "../NomadAudio/include/MiniAudioDecoder.h"
#include "MixerViewModel.h"
#include "../NomadAudio/include/ClipSource.h"
#include "../NomadCore/include/NomadLog.h"

#include <cmath>
#include <fstream>
#include <filesystem>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace Nomad;
using namespace NomadUI;
using namespace Nomad::Audio;

// =============================================================================
// SECTION: Construction
// =============================================================================

NomadContent::NomadContent() {
    // Create layers
    m_workspaceLayer = std::make_shared<NomadUI::NUIComponent>();
    m_workspaceLayer->setId("WorkspaceLayer");
    addChild(m_workspaceLayer);

    m_overlayLayer = std::make_shared<OverlayLayer>();
    addChild(m_overlayLayer);

    // Create track manager for multi-track functionality
    m_trackManager = std::make_shared<TrackManager>();
    addDemoTracks();

    // Create track manager UI (add to workspace)
    m_trackManagerUI = std::make_shared<TrackManagerUI>(m_trackManager);
    
    // Wire up TrackManagerUI internal toggles to centralized authority (v3.1)
    m_trackManagerUI->setOnToggleMixer([this]() { toggleView(Audio::ViewType::Mixer); });
    m_trackManagerUI->setOnTogglePianoRoll([this]() { toggleView(Audio::ViewType::PianoRoll); });
    m_trackManagerUI->setOnToggleSequencer([this]() { toggleView(Audio::ViewType::Sequencer); });
    m_trackManagerUI->setOnTogglePlaylist([this]() { toggleView(Audio::ViewType::Playlist); });
    
    m_workspaceLayer->addChild(m_trackManagerUI);

    // Create file browser (add to workspace)
    m_fileBrowser = std::make_shared<NomadUI::FileBrowser>();
    m_fileBrowser->setOnFileOpened([this](const NomadUI::FileItem& file) {
        Log::info("File opened: " + file.path);
        loadSampleIntoSelectedTrack(file.path);
    });
    m_fileBrowser->setOnSoundPreview([this](const NomadUI::FileItem& file) {
        Log::info("Sound preview requested: " + file.path);
        playSoundPreview(file);
    });

    m_workspaceLayer->addChild(m_fileBrowser);

    // Create file preview panel (add to workspace, below browser)
    m_previewPanel = std::make_shared<NomadUI::FilePreviewPanel>();
    m_previewPanel->setOnPlay([this](const NomadUI::FileItem& file) {
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
    m_fileBrowser->setOnFileSelected([this](const NomadUI::FileItem& file) {
        stopSoundPreview();
        if (m_previewPanel) m_previewPanel->setFile(&file);
    });

    // Create pattern browser panel (add to workspace, FL Studio style side panel)
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
                NomadUI::NUIRect allowed = computeAllowedRectForPanels();
                float editorWidth = std::min(900.0f, allowed.width * 0.8f);
                float editorHeight = std::min(500.0f, allowed.height * 0.7f);
                float editorX = allowed.x + (allowed.width - editorWidth) / 2.0f;
                float editorY = allowed.y + (allowed.height - editorHeight) / 2.0f;
                
                m_viewState.pianoRollRect = NomadUI::NUIRect(editorX, editorY, editorWidth, editorHeight);
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
    m_mixerPanel->setOnDragStart([this](const NomadUI::NUIPoint& pos) { beginPanelDrag(ViewType::Mixer, pos); });
    m_mixerPanel->setOnDragMove([this](const NomadUI::NUIPoint& pos) { updatePanelDrag(ViewType::Mixer, pos); });
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
    m_pianoRollPanel->setOnDragStart([this](const NomadUI::NUIPoint& pos) { beginPanelDrag(ViewType::PianoRoll, pos); });
    m_pianoRollPanel->setOnDragMove([this](const NomadUI::NUIPoint& pos) { updatePanelDrag(ViewType::PianoRoll, pos); });
    m_pianoRollPanel->setOnDragEnd([this]() { endPanelDrag(ViewType::PianoRoll); });
    m_overlayLayer->addChild(m_pianoRollPanel);
    
    // Create Arsenal panel
    m_sequencerPanel = std::make_shared<ArsenalPanel>(m_trackManager);
    m_sequencerPanel->setPatternBrowser(m_patternBrowser.get());
    m_patternBrowser->refreshPatterns();
    
    m_sequencerPanel->setVisible(true);
    m_sequencerPanel->setOnClose([this]() { setViewFocus(ViewFocus::Timeline); });
    m_sequencerPanel->setOnMaximizeToggle([this](bool) { 
        onResize(static_cast<int>(getBounds().width), static_cast<int>(getBounds().height)); 
    });
    m_sequencerPanel->setOnDragStart([this](const NomadUI::NUIPoint& pos) { beginPanelDrag(ViewType::Sequencer, pos); });
    m_sequencerPanel->setOnDragMove([this](const NomadUI::NUIPoint& pos) { updatePanelDrag(ViewType::Sequencer, pos); });
    m_sequencerPanel->setOnDragEnd([this]() { endPanelDrag(ViewType::Sequencer); });
    m_overlayLayer->addChild(m_sequencerPanel);

    // Create transport bar
    m_transportBar = std::make_shared<Nomad::TransportBar>();
    m_transportBar->setOnToggleView([this](Audio::ViewType view) {
        toggleView(view);
    });
    
    // Wire Transport to TrackManager
    m_transportBar->setOnPlay([this]() { if(m_trackManager) m_trackManager->play(); });
    m_transportBar->setOnPause([this]() { if(m_trackManager) m_trackManager->pause(); });
    m_transportBar->setOnStop([this]() { if(m_trackManager) m_trackManager->stop(); });
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
    auto& theme = NomadUI::NUIThemeManager::getInstance();
    m_viewToggle = std::make_shared<NomadUI::NUISegmentedControl>(
        std::vector<std::string>{"Arsenal", "Timeline"}
    );
    m_viewToggle->setCornerRadius(12.0f);
    m_viewToggle->setOnSelectionChanged([this](size_t index) {
        setViewFocus(index == 0 ? ViewFocus::Arsenal : ViewFocus::Timeline);
    });
    
    setViewFocus(ViewFocus::Arsenal);

    // Create compact master meters
    m_waveformVisualizer = std::make_shared<NomadUI::AudioVisualizer>();
    m_waveformVisualizer->setMode(NomadUI::AudioVisualizationMode::ArrangementWaveform);
    m_waveformVisualizer->setShowStereo(true);
    m_overlayLayer->addChild(m_waveformVisualizer);

    m_audioVisualizer = std::make_shared<NomadUI::AudioVisualizer>();
    m_audioVisualizer->setMode(NomadUI::AudioVisualizationMode::CompactMeter);
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

void NomadContent::onUpdate(double dt) {
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
                 }
             }
         }
         tm->setModified(false);
    }
    
    // Poll transport position for waveform visualizer
    if (tm && m_waveformVisualizer) {
        double pos = tm->getUIPosition();
        m_waveformVisualizer->setTransportPosition(pos);
    }
    
    NomadUI::NUIComponent::onUpdate(dt);
}

void NomadContent::onRender(NomadUI::NUIRenderer& renderer) {
    NomadUI::NUIRect bounds = getBounds();
    float width = bounds.width;
    float height = bounds.height;

    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    float transportHeight = layout.transportBarHeight;

    NomadUI::NUIColor textColor = themeManager.getColor("textPrimary");
    NomadUI::NUIColor accentColor = themeManager.getColor("accentCyan");
    NomadUI::NUIColor statusColor = m_audioActive ?
        themeManager.getColor("accentLime") : themeManager.getColor("error");

    NomadUI::NUIRect contentArea(bounds.x, bounds.y + transportHeight,
                                 width, height - transportHeight);
    renderer.fillRect(contentArea, themeManager.getColor("backgroundPrimary"));
    renderer.strokeRect(contentArea, 1, themeManager.getColor("border"));

    renderChildren(renderer);
}

void NomadContent::onResize(int width, int height) {
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();

    NomadUI::NUIRect contentBounds = getBounds();

    if (m_workspaceLayer) m_workspaceLayer->setBounds(contentBounds);
    if (m_overlayLayer) m_overlayLayer->setBounds(contentBounds);

    if (m_transportBar) {
        m_transportBar->setBounds(NomadUI::NUIAbsolute(contentBounds, 0, 0,
            contentBounds.width, 60.0f));
    }
    
    if (m_viewToggle) {
        auto rootBounds = getBounds();
        float toggleWidth = 160.0f;
        float toggleHeight = 24.0f;
        float yPos = 4.0f;
        float centerX = rootBounds.width / 2.0f;
        float startX = centerX - toggleWidth / 2.0f;
        
        m_viewToggle->setBounds(NomadUI::NUIRect(startX, yPos, toggleWidth, toggleHeight));
    }
    
    if (m_scopeLabel) {
        float labelX = 280.0f + 85.0f;
        float labelY = 15.0f;
        m_scopeLabel->setBounds(NomadUI::NUIAbsolute(contentBounds, 
            labelX, labelY, 150.0f, 30.0f));
    }

    float fileBrowserWidth = 0;
    float sidebarHeight = height - layout.transportBarHeight;
    float patternBrowserWidth = 0;
    
    if (m_fileBrowser) {
        fileBrowserWidth = std::min(layout.fileBrowserWidth, width * 0.20f);
        float fbHeight = sidebarHeight;
        
        if (m_previewPanel) {
           float previewHeight = 90.0f;
           fbHeight -= previewHeight;
           
           m_previewPanel->setBounds(NomadUI::NUIAbsolute(contentBounds, 0, layout.transportBarHeight + fbHeight,
               fileBrowserWidth, previewHeight));
        }
        
        m_fileBrowser->setBounds(NomadUI::NUIAbsolute(contentBounds, 0, layout.transportBarHeight,
                                                   fileBrowserWidth, fbHeight));
    }
    
    if (m_patternBrowser) {
        patternBrowserWidth = std::min(layout.fileBrowserWidth * 0.8f, width * 0.15f);
        float patternBrowserX = fileBrowserWidth;
        m_patternBrowser->setBounds(NomadUI::NUIAbsolute(contentBounds, patternBrowserX, layout.transportBarHeight,
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
            m_waveformVisualizer->setBounds(NomadUI::NUIAbsolute(contentBounds, xStart, vuY, waveformWidth, visualizerHeight));
            xStart += waveformWidth + gap;
        }
        if (m_audioVisualizer) {
            m_audioVisualizer->setBounds(NomadUI::NUIAbsolute(contentBounds, xStart, vuY, meterWidth, visualizerHeight));
        }
    }

    if (m_trackManagerUI) {
        float trackAreaX = fileBrowserWidth + patternBrowserWidth;
        float trackAreaWidth = width - trackAreaX;
        float trackAreaHeight = height - layout.transportBarHeight;
        m_trackManagerUI->setBounds(NomadUI::NUIAbsolute(contentBounds, trackAreaX, layout.transportBarHeight, trackAreaWidth, trackAreaHeight));
    }

    NomadUI::NUIRect allowed = computeAllowedRectForPanels();
    NomadUI::NUIRect maxRect = computeMaximizedRect();

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

    NomadUI::NUIComponent::onResize(width, height);
}

// =============================================================================
// SECTION: View Management
// =============================================================================

void NomadContent::setViewOpen(Audio::ViewType view, bool open) {
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
            NomadUI::NUIRect allowed = computeAllowedRectForPanels();
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

void NomadContent::toggleView(Audio::ViewType view) {
    if (view == Audio::ViewType::Sequencer) {
        if (m_viewFocus == ViewFocus::Arsenal) {
            setViewFocus(ViewFocus::Timeline);
        } else {
            setViewFocus(ViewFocus::Arsenal);
        }
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

void NomadContent::toggleFileBrowser() {
    if (m_fileBrowser) {
        bool isVisible = m_fileBrowser->isVisible();
        m_fileBrowser->setVisible(!isVisible);
        Log::info("File Browser toggled: " + std::string(!isVisible ? "VISIBLE" : "HIDDEN"));
        onResize(static_cast<int>(getBounds().width), static_cast<int>(getBounds().height));
    }
}

void NomadContent::syncViewState() {
    if (m_transportBar) {
        m_transportBar->setViewToggled(Audio::ViewType::Mixer, m_viewState.mixerOpen);
        m_transportBar->setViewToggled(Audio::ViewType::PianoRoll, m_viewState.pianoRollOpen);
        m_transportBar->setViewToggled(Audio::ViewType::Sequencer, m_viewState.sequencerOpen);
        m_transportBar->setViewToggled(Audio::ViewType::Playlist, m_viewState.playlistActive);
    }
}

void NomadContent::setViewFocus(ViewFocus focus) {
    bool wasPlaying = (m_transportBar && m_transportBar->getState() == TransportState::Playing);
    
    m_viewFocus = focus;
    
    if (focus == ViewFocus::Arsenal) {
        if (m_sequencerPanel) {
            if (!m_sequencerPanel->isVisible()) {
                m_sequencerPanel->setOpacity(0.0f);
            }
            m_sequencerPanel->setVisible(true);
            m_sequencerPanel->setOpacity(1.0f);
            m_viewState.sequencerOpen = true;
        }
    } else {
        if (m_sequencerPanel) {
            m_sequencerPanel->setVisible(false);
            m_sequencerPanel->setOpacity(1.0f);
            m_viewState.sequencerOpen = false;
        }
    }
    
    if (m_transportBar) {
        m_transportBar->setViewToggled(Audio::ViewType::Sequencer, focus == ViewFocus::Arsenal);
    }
    
    if (wasPlaying && m_transportBar) {
        Log::info("[Focus] Hot-swapping playback mode");
        m_transportBar->stop();
        m_transportBar->play();
    }
}

// =============================================================================
// SECTION: Panel Physics & Constraints
// =============================================================================

NomadUI::NUIRect NomadContent::computeSafeRect() const {
    NomadUI::NUIRect bounds = getBounds();
    
    float transportHeight = 60.0f;
    if (m_transportBar) transportHeight = m_transportBar->getHeight();
    
    NomadUI::NUIRect safe = bounds;
    safe.y += transportHeight;
    safe.height -= transportHeight;
    
    float padding = 0.0f;
    safe.x += padding;
    safe.y += padding;
    safe.width -= (padding * 2);
    safe.height -= (padding * 2);
    
    return safe;
}

NomadUI::NUIRect NomadContent::computeAllowedRectForPanels() const {
    NomadUI::NUIRect safe = computeSafeRect();
    
    if (m_fileBrowser && m_fileBrowser->isVisible()) {
        NomadUI::NUIRect browserRect = m_fileBrowser->getBounds();
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

NomadUI::NUIRect NomadContent::computeMaximizedRect() const {
    NomadUI::NUIRect bounds = getBounds();
    
    float transportHeight = 60.0f;
    if (m_transportBar) transportHeight = m_transportBar->getHeight();
    
    NomadUI::NUIRect maxRect = bounds;
    maxRect.y += transportHeight;
    maxRect.height -= transportHeight;
    
    if (m_fileBrowser && m_fileBrowser->isVisible()) {
        NomadUI::NUIRect browserRect = m_fileBrowser->getBounds();
        float browserEdge = browserRect.width;
        
        if (browserEdge > maxRect.x) {
           float shift = browserEdge - maxRect.x;
           maxRect.x = browserEdge;
           maxRect.width -= shift; 
        }
    }
    
    return maxRect;
}

NomadUI::NUIRect NomadContent::clampRectToAllowed(NomadUI::NUIRect panel, const NomadUI::NUIRect& allowed) const {
    float x = std::clamp(panel.x, allowed.x, allowed.right() - panel.width);
    float y = std::clamp(panel.y, allowed.y, allowed.bottom() - panel.height);
    
    float w = panel.width;
    float h = panel.height;
    
    if (w > allowed.width) w = allowed.width;
    if (h > allowed.height) h = allowed.height;
    
    x = std::clamp(x, allowed.x, allowed.right() - w);
    y = std::clamp(y, allowed.y, allowed.bottom() - h);

    return NomadUI::NUIRect(x, y, w, h);
}

// =============================================================================
// SECTION: Panel Drag Handlers
// =============================================================================

void NomadContent::beginPanelDrag(Audio::ViewType view, const NomadUI::NUIPoint& mouseScreen) {
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

void NomadContent::updatePanelDrag(Audio::ViewType view, const NomadUI::NUIPoint& mouseScreen) {
    if (!m_viewState.isDragging || !m_overlayLayer || view != m_viewState.draggingView) return;
    
    NomadUI::NUIPoint currentMouseOverlay = m_overlayLayer->globalToLocal(mouseScreen);
    NomadUI::NUIPoint delta = currentMouseOverlay - m_viewState.dragStartMouseOverlay;
    
    NomadUI::NUIRect proposed = m_viewState.dragStartRect;
    proposed.x += delta.x;
    proposed.y += delta.y;
    
    NomadUI::NUIRect allowed = computeAllowedRectForPanels();
    NomadUI::NUIRect finalRect = clampRectToAllowed(proposed, allowed);
    
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

void NomadContent::endPanelDrag(Audio::ViewType view) {
     m_viewState.isDragging = false;
     Log::info("Ended dragging panel: " + std::to_string(static_cast<int>(view)));
}

// =============================================================================
// SECTION: Getters
// =============================================================================

void NomadContent::setAudioStatus(bool active) {
    m_audioActive = active;
}

Nomad::TransportBar* NomadContent::getTransportBar() {
    return m_transportBar.get();
}

std::shared_ptr<NomadUI::AudioVisualizer> NomadContent::getAudioVisualizer() {
    return m_audioVisualizer;
}

std::shared_ptr<NomadUI::AudioVisualizer> NomadContent::getWaveformVisualizer() {
    return m_waveformVisualizer;
}

PreviewEngine* NomadContent::getPreviewEngine() { 
    return m_previewEngine.get(); 
}

std::shared_ptr<TrackManager> NomadContent::getTrackManager() {
    return m_trackManager;
}

std::shared_ptr<TrackManagerUI> NomadContent::getTrackManagerUI() {
    return m_trackManagerUI;
}

std::shared_ptr<NomadUI::NUISegmentedControl> NomadContent::getViewToggle() { 
    return m_viewToggle; 
}

PatternID NomadContent::getActivePatternID() const {
    if (m_sequencerPanel) {
        return m_sequencerPanel->getActivePatternID();
    }
    return PatternID();
}

std::shared_ptr<NomadUI::FileBrowser> NomadContent::getFileBrowser() const { 
    return m_fileBrowser; 
}

void NomadContent::setPlatformBridge(NomadUI::NUIPlatformBridge* bridge) {
    m_platformBridge = bridge;
    if (m_trackManagerUI) {
        m_trackManagerUI->setPlatformWindow(bridge);
    }
}

// =============================================================================
// SECTION: Demo & Testing
// =============================================================================

void NomadContent::addDemoTracks() {
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

bool NomadContent::generateTestWavFile(const std::string& filename, float frequency, double duration) {
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

void NomadContent::playSoundPreview(const NomadUI::FileItem& file) {
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

void NomadContent::stopSoundPreview() {
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

void NomadContent::loadSampleIntoSelectedTrack(const std::string& filePath) {
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

void NomadContent::updateSoundPreview() {
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

void NomadContent::seekSoundPreview(double seconds) {
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

bool NomadContent::isPlayingPreview() const { 
    return m_previewIsPlaying; 
}

void NomadContent::updatePreviewPlayhead() {
    if (m_previewPanel && m_previewEngine) {
        m_previewPanel->setDuration(m_previewEngine->getDuration());
        m_previewPanel->setPlayheadPosition(m_previewEngine->getPlaybackPosition());
    }
}
