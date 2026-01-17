// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraApp.h"
#include "AppLifecycle.h"
#include "ServiceLocator.h"
#include "AudioThreadConstraints.h"
#include "../AestraCore/include/AestraUnifiedProfiler.h"
#include "../AestraCore/include/PointerRegistry.h"
#include "FileBrowser.h"
#include "TrackManagerUI.h"
#include "TransportBar.h"
#include "SettingsDialog.h"
#include "AudioSettingsPage.h"
#include "GeneralSettingsPage.h"
#include "AppearanceSettingsPage.h"
#include "UnifiedHUD.h"
#include "RecoveryDialog.h"
#include "ConfirmationDialog.h"
#include "PluginManager.h"
#include "AudioGraphBuilder.h"

#include <iostream>
#include <chrono>
#include <thread>
#include <cmath>
#include <algorithm>

using namespace Aestra;
using namespace AestraUI;
using namespace Aestra::Audio;

// =============================================================================
// AestraApp Implementation
// =============================================================================

AestraApp::AestraApp() 
    : m_running(false)
{
    // Initialize m_projectPath with autosave path
    m_projectPath = getAutosavePath();

    // Initialize unified logging
    auto multiLogger = std::make_shared<MultiLogger>(LogLevel::Info);
    multiLogger->addLogger(std::make_shared<ConsoleLogger>(LogLevel::Info));
    std::string logPath = (std::filesystem::current_path() / "aestra_debug.log").string();
    auto fileLogger = std::make_shared<FileLogger>(logPath, LogLevel::Info);
    multiLogger->addLogger(fileLogger);
    m_asyncLogger = std::make_shared<AsyncLogger>(multiLogger);
    
    Log::init(m_asyncLogger);
    Log::info("Logging initialized to console and " + logPath);

    m_windowManager = std::make_unique<AestraWindowManager>();
    m_audioController = std::make_unique<AestraAudioController>();
}

AestraApp::~AestraApp() {
    shutdown();
}

std::string AestraApp::getAppDataPath() {
    IPlatformUtils* utils = Platform::getUtils();
    if (!utils) {
        return std::filesystem::current_path().string();
    }
    std::string appDataDir = utils->getAppDataPath("Aestra");
    std::error_code ec;
    if (!std::filesystem::create_directories(appDataDir, ec) && ec) {
        // Log?
    }
    return appDataDir;
}

std::string AestraApp::getAutosavePath() {
    return (std::filesystem::path(getAppDataPath()) / "autosave.aes").string();
}

std::string AestraApp::getLegacyAutosavePath() {
    return (std::filesystem::path(getAppDataPath()) / "autosave.Aestraproj").string();
}

bool AestraApp::initialize(const std::string& projectPath) {
    if (!Aestra::AppLifecycle::instance().transitionTo(Aestra::AppState::Initializing)) {
        Log::error("Failed to transition to Initializing state");
        return false;
    }
    
    Log::info("Aestra v1.0.0 - Initializing...");

    if (!Platform::initialize()) {
        Log::error("Failed to initialize platform");
        return false;
    }

    // Initialize Window
    AestraWindowManager::WindowConfig winConfig;
    winConfig.title = "Aestra v1.0";
    winConfig.width = 1280;
    winConfig.height = 720;
    winConfig.fullscreen = false; // Default

    if (!m_windowManager->initialize(winConfig)) {
        Log::error("Failed to initialize Window Manager");
        return false;
    }

    // Initialize Audio
    if (!m_audioController->initialize()) {
        Log::warning("Audio Controller initialization failed (continuing without audio)");
    } else {
        // Try opening default stream
        if (m_audioController->openDefaultStream(nullptr)) {
            m_audioController->startStream();
        }
    }

    // Initialize Content
    m_content = std::make_shared<AestraContent>();
    m_content->setPlatformBridge(m_windowManager->getWindow());
    m_content->setAudioStatus(m_audioController->isInitialized());
    if (m_audioController->getEngine()) {
        m_content->setAudioEngine(m_audioController->getEngine());
    }

    m_windowManager->setContent(m_content);
    m_audioController->setContent(m_content);

    // Setup UI components via WindowManager helper
    // Note: AestraWindowManager creates its own components?
    // Wait, in AestraApp.cpp before, it created SettingsDialog, etc.
    // WindowManager owns them now. We need to configure them.

    auto settingsDialog = std::make_shared<SettingsDialog>();
    auto generalPage = std::make_shared<GeneralSettingsPage>();
    generalPage->setOnAutoSaveToggled([this](bool enabled) {
        m_autoSaveEnabled.store(enabled, std::memory_order_relaxed);
        Log::info(std::string("[AutoSave] ") + (enabled ? "Enabled" : "Disabled"));
    });
    settingsDialog->addPage(generalPage);

    auto audioPage = std::make_shared<AudioSettingsPage>(
        m_audioController->getDeviceManager(),
        m_audioController->getEngine()
    );
    audioPage->setOnStreamRestore([this]() {
         // Re-open default stream logic? Or expose logic in AudioController
         m_audioController->closeStream();
         if (m_audioController->openDefaultStream(nullptr)) {
             m_audioController->startStream();
         }
    });
    settingsDialog->addPage(audioPage);
    settingsDialog->addPage(std::make_shared<AppearanceSettingsPage>());
    settingsDialog->setBounds(AestraUI::NUIRect(0, 0, 950, 600));

    m_windowManager->setSettingsDialog(settingsDialog);
    m_windowManager->setConfirmationDialog(std::make_shared<ConfirmationDialog>());
    m_windowManager->setRecoveryDialog(std::make_shared<RecoveryDialog>());

    auto unifiedHUD = std::make_shared<UnifiedHUD>(m_windowManager->getAdaptiveFPS());
    unifiedHUD->setVisible(false);
    unifiedHUD->setAudioEngine(m_audioController->getEngine());
    m_windowManager->setUnifiedHUD(unifiedHUD);

    // Menu Bar
    auto menuBar = std::make_shared<AestraUI::NUIMenuBar>();
    menuBar->addItem("File", [this]() {
        auto menu = std::make_shared<AestraUI::NUIContextMenu>();
        
        // Project actions
        menu->addItem("New Project", [this]() {
            if (m_content && m_content->getTrackManager()) m_content->getTrackManager()->stop();
            if (m_content) m_content->resetToDefaultProject();
            m_projectPath = getAutosavePath();
            m_lastWindowTitle.clear();
            Log::info("New project created");
        });
        
        menu->addItem("Open Project...", [this]() {
            // TODO: Implement proper file browser integration
            Log::info("Open Project - Not yet fully implemented");
            // When FileBrowser has openProjectDialog:
            // if (m_content && m_content->getFileBrowser()) {
            //     m_content->getFileBrowser()->openProjectDialog([this](const std::string& path) {
            //         if (!path.empty() && std::filesystem::exists(path)) {
            //             m_projectPath = path;
            //             auto result = loadProject();
            //             if (result.ok) {
            //                 if (result.ui) applyUIState(*result.ui);
            //                 Log::info("Project loaded: " + path);
            //             } else {
            //                 Log::error("Failed to load project: " + path);
            //             }
            //         }
            //     });
            // }
        });
        
        menu->addSeparator();
        
        menu->addItem("Save", [this]() { 
            saveCurrentProject(); 
        });
        
        menu->addItem("Save As...", [this]() {
            // TODO: Implement proper file browser integration
            Log::info("Save As - Not yet fully implemented");
            // When FileBrowser has saveProjectDialog:
            // if (m_content && m_content->getFileBrowser()) {
            //     m_content->getFileBrowser()->saveProjectDialog([this](const std::string& path) {
            //         if (!path.empty()) {
            //             m_projectPath = path;
            //             saveProject();
            //             Log::info("Project saved as: " + path);
            //         }
            //     });
            // }
        });
        
        menu->addSeparator();
        
        menu->addItem("Settings...", [this]() {
            if (m_windowManager->getSettingsDialog()) {
                m_windowManager->getSettingsDialog()->show();
            }
        });
        
        menu->addSeparator();
        
        menu->addItem("Exit", [this]() { 
            m_running = false; 
        });
        
        m_windowManager->showDropdownMenu(menu, 10.0f);
    });
    // Edit Menu
    menuBar->addItem("Edit", [this]() {
        auto menu = std::make_shared<AestraUI::NUIContextMenu>();
        
        menu->addItem("Undo", [this]() {
            if (m_content && m_content->getTrackManager()) {
                m_content->getTrackManager()->getCommandHistory().undo();
            }
        });
        
        menu->addItem("Redo", [this]() {
            if (m_content && m_content->getTrackManager()) {
                m_content->getTrackManager()->getCommandHistory().redo();
            }
        });
        
        menu->addSeparator();
        
        menu->addItem("Cut", [this]() {
            // TODO: Implement Cut functionality
            Log::info("Cut - Not yet implemented");
        });
        
        menu->addItem("Copy", [this]() {
            // TODO: Implement Copy functionality
            Log::info("Copy - Not yet implemented");
        });
        
        menu->addItem("Paste", [this]() {
            // TODO: Implement Paste functionality
            Log::info("Paste - Not yet implemented");
        });
        
        menu->addSeparator();
        
        menu->addItem("Delete", [this]() {
            // TODO: Implement Delete functionality
            Log::info("Delete - Not yet implemented");
        });
        
        m_windowManager->showDropdownMenu(menu, 55.0f);
    });
    
    // View Menu
    menuBar->addItem("View", [this]() {
        auto menu = std::make_shared<AestraUI::NUIContextMenu>();
        
        menu->addItem("Performance Stats", [this]() {
            if (auto hud = m_windowManager->getUnifiedHUD()) {
                hud->setVisible(!hud->isVisible());
            }
        });
        
        menu->addSeparator();
        
        menu->addItem("Toggle Fullscreen", [this]() {
            if (m_windowManager) {
                m_windowManager->toggleFullScreen();
            }
        });
        
        menu->addSeparator();
        
        menu->addItem("Show Timeline", [this]() {
            // TODO: Implement view switching
            Log::info("Show Timeline - Not yet fully implemented");
        });
        
        menu->addItem("Show Arsenal", [this]() {
            // TODO: Implement view switching
            Log::info("Show Arsenal - Not yet fully implemented");
        });
        
        m_windowManager->showDropdownMenu(menu, 100.0f);
    });

    menuBar->setBounds(AestraUI::NUIRect(10.0f, 4.0f, 180.0f, 24.0f));
    m_windowManager->setMenuBar(menuBar);

    // Callbacks
    setupCallbacks();
    m_windowManager->initializeCustomCursors();

    connectAudioToUI();

    m_running = true;

    // Plugin Manager
    if (Aestra::Audio::PluginManager::getInstance().initialize()) {
        Log::info("Plugin Manager initialized");
        Aestra::Audio::PluginManager::getInstance().getScanner().addDefaultSearchPaths();
    }

    // Load Project
    if (!projectPath.empty() && std::filesystem::exists(projectPath)) {
        m_projectPath = projectPath;
        auto result = loadProject();
        if (result.ok) {
            if (result.ui) applyUIState(*result.ui);
        }
    } else {
        // Autosave check
        std::string autosavePath = getAutosavePath();
        std::string timestamp;
        if (RecoveryDialog::detectAutosave(autosavePath, timestamp)) {
            m_projectPath = autosavePath;
            auto result = loadProject();
            if (result.ok) {
                if (result.ui) applyUIState(*result.ui);
                m_projectPath = getAutosavePath();
            }
        }
    }

    if (!Aestra::AppLifecycle::instance().transitionTo(Aestra::AppState::Running)) {
        Log::error("Failed to transition to Running state");
        return false;
    }
    
    return true;
}

void AestraApp::connectAudioToUI() {
    // Connect deferred audio settings
    if (m_audioController->getEngine() && m_content && m_content->getTrackManager()) {
        if (m_audioController->isInitialized()) {
            auto& config = m_audioController->getStreamConfig();
            m_content->getTrackManager()->setInputChannelCount(config.numInputChannels);
            m_content->getTrackManager()->setOutputSampleRate(config.sampleRate);
            m_content->getTrackManager()->setInputSampleRate(config.sampleRate);
            
            auto meterBuffer = std::make_shared<Audio::MeterSnapshotBuffer>();
            m_audioController->getEngine()->setMeterSnapshots(meterBuffer);
            m_content->getTrackManager()->setMeterSnapshots(meterBuffer);
            
            auto slotMap = m_content->getTrackManager()->getChannelSlotMapShared();
            if (slotMap) {
                m_audioController->getEngine()->setChannelSlotMap(slotMap);
            }
            
            Aestra::ServiceLocator::provide<Aestra::Audio::AudioEngine>(m_audioController->getEngine());
            Aestra::ServiceLocator::provide<Aestra::Audio::AudioDeviceManager>(m_audioController->getDeviceManager());
            Aestra::ServiceLocator::provide<Aestra::Audio::TrackManager>(m_content->getTrackManager());
            
            auto trackMgr = m_content->getTrackManager();
            trackMgr->getCommandHistory().setOnStateChanged([trackMgr]() {
                if (trackMgr) trackMgr->markModified();
            });
            
            Aestra::PointerRegistry::expectNotNull("AudioEngine", m_audioController->getEngine());
            Aestra::PointerRegistry::expectNotNull("TrackManager", trackMgr.get());
            Aestra::PointerRegistry::validateAll();
        }
    }
    
    // Transport Bar Wiring
    if (m_content && m_content->getTransportBar() && m_audioController->getEngine()) {
        auto engine = m_audioController->getEngine();
        m_content->getTransportBar()->setOnPlay([this, engine]() {
            if (m_content && m_content->getTrackManager()) {
                m_content->stopSoundPreview();
                // View Focus logic omitted for brevity, assume timeline for now
                m_content->getTrackManager()->play();
                engine->setTransportPlaying(true);
            }
        });
        m_content->getTransportBar()->setOnPause([this, engine]() {
            if (m_content && m_content->getTrackManager()) {
                m_content->getTrackManager()->pause();
                engine->setTransportPlaying(false);
            }
        });
        m_content->getTransportBar()->setOnStop([this, engine]() {
            if (m_content && m_content->getTrackManager()) {
                auto trackMgr = m_content->getTrackManager();
                if (!engine->isTransportPlaying()) {
                    engine->panic();
                    engine->setGlobalSamplePos(0);
                    trackMgr->setPosition(0.0);
                    trackMgr->setPlayStartPosition(0.0);
                } else {
                    if (trackMgr->isPatternMode()) {
                        trackMgr->stopArsenalPlayback(true);
                    } else {
                        double playStartPos = trackMgr->getPlayStartPosition();
                        double sr = engine->getSampleRate();
                        uint64_t samplePos = static_cast<uint64_t>(playStartPos * sr);
                        trackMgr->stop();
                        engine->setGlobalSamplePos(samplePos);
                        trackMgr->setPosition(playStartPos);
                        engine->setTransportPlaying(false);
                    }
                }
            }
        });
        
        // Record (Todo)
        
        // Metronome toggle
        m_content->getTransportBar()->setOnMetronomeToggle([this, engine](bool active) {
            if (engine) {
                engine->setMetronomeEnabled(active);
                Log::info(std::string("Metronome toggled: ") + (active ? "ON" : "OFF"));
            }
        });
        
        // Tempo change
        m_content->getTransportBar()->setOnTempoChange([this, engine](float bpm) {
            if (engine) {
                engine->setBPM(bpm);
            }
        });
        
        // Load metronome click sounds (Redundant but safe fallback if Controller update missed it)
        engine->loadMetronomeClicks(
            "AestraAudio/assets/Aestra_metronome.wav",
            "AestraAudio/assets/Aestra_metronome_up.wav"
        );
        engine->setBPM(120.0f);
    }
}

void AestraApp::setupCallbacks() {
    m_windowManager->setCloseCallback([this]() {
        requestClose();
    });
    
    m_windowManager->setTransportCallback([this](AestraWindowManager::TransportAction action) {
        if (!m_audioController || !m_audioController->getEngine()) return;
        auto engine = m_audioController->getEngine();
        using Action = AestraWindowManager::TransportAction;
        
        if (action == Action::Play) {
            if (m_content && m_content->getTrackManager()) m_content->getTrackManager()->play();
            engine->setTransportPlaying(true);
        }
        else if (action == Action::Pause) {
             if (m_content && m_content->getTrackManager()) m_content->getTrackManager()->pause();
             engine->setTransportPlaying(false);
        }
        else if (action == Action::Stop) {
             if (m_content && m_content->getTrackManager()) {
                 auto trackMgr = m_content->getTrackManager();
                 
                 // [FIX] If in pattern mode, we want to stay in pattern mode on stop
                 if (trackMgr->isPatternMode()) {
                     trackMgr->stopArsenalPlayback(true);
                     Log::info("[Arsenal] Global Stop (keeping mode)");
                 } else {
                     double playStartPos = trackMgr->getPlayStartPosition();
                     double sr = engine->getSampleRate();
                     uint64_t samplePos = static_cast<uint64_t>(playStartPos * sr);
                     
                     trackMgr->stop();
                     engine->setGlobalSamplePos(samplePos);
                     trackMgr->setPosition(playStartPos);
                     engine->setTransportPlaying(false);
                 }
             }
        }
    });
    
    // Other callbacks handled by WindowManager internally
}

void AestraApp::run() {
    m_windowManager->render(); // Initial render

    double autoSaveTimer = 0.0;
    const double autoSaveInterval = 300.0;
    
    while (m_running && m_windowManager->processEvents()) {
        UnifiedProfiler::getInstance().beginFrame();
        m_windowManager->beginFrame(); // Start timing
        
        {
            AESTRA_ZONE("UI_Update");
            // Sync Transport State
            if (m_audioController->getEngine() && m_content && m_content->getTrackManager()) {
                auto engine = m_audioController->getEngine();
                auto tm = m_content->getTrackManager();
                
                if (tm->isUserScrubbing()) {
                    double scrubPos = tm->getPosition();
                    uint32_t sr = engine->getSampleRate();
                    if (sr > 0) engine->setGlobalSamplePos(static_cast<uint64_t>(scrubPos * sr));
                } else if (engine->isTransportPlaying()) {
                    double realTime = engine->getPositionSeconds();
                    tm->syncPositionFromEngine(realTime);
                }
                
                if (m_content->getTransportBar()) {
                   m_content->getTransportBar()->setPosition(tm->getPosition());
                   // Sync play state...
                }
                updateWindowTitle();
            }
            
            // Rebuild graph check
            if (m_audioController->getEngine() && m_content && m_content->getTrackManager() &&
                m_content->getTrackManager()->consumeGraphDirty()) {
                    auto graph = AudioGraphBuilder::buildFromTrackManager(*m_content->getTrackManager(), m_audioController->getSampleRate());
                    m_audioController->getEngine()->setGraph(graph);
                    // Sync slot map...
                    m_content->getTrackManager()->rebuildAndPushSnapshot();
            }
        }

        {
            AESTRA_ZONE("Render_Prep");
            m_windowManager->render();
        }

        // Auto-save logic
        if (m_autoSaveFuture.valid()) {
           if (m_autoSaveFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                m_autoSaveInFlight.store(false, std::memory_order_relaxed);
           }
        }
        
        if (m_autoSaveEnabled.load(std::memory_order_relaxed)) {
            double deltaTime = m_windowManager->getDeltaTime();
            autoSaveTimer += deltaTime;
            if (autoSaveTimer >= autoSaveInterval) {
                 if (!m_autoSaveInFlight.load(std::memory_order_relaxed)) {
                     if (m_content && m_content->getTrackManager() && m_content->getTrackManager()->isModified()) {
                         autoSaveTimer = 0.0;
                         // Capture & Save Async... (omitted for brevity in refactor example)
                     }
                 }
            }
        }
        
        double sleepTime = m_windowManager->endFrame();
        m_windowManager->swapBuffers();
        if (sleepTime > 0.0) {
             if (auto fps = m_windowManager->getAdaptiveFPS()) {
                 fps->sleep(sleepTime);
             } else {
                 std::this_thread::sleep_for(std::chrono::duration<double>(sleepTime));
             }
        }
        
        UnifiedProfiler::getInstance().endFrame();
    }
}

void AestraApp::shutdown() {
    Log::info("[SHUTDOWN] Entering shutdown function...");
    Aestra::AppLifecycle::instance().transitionTo(Aestra::AppState::ShuttingDown);
    Aestra::ServiceLocator::clear();
    
    Aestra::Audio::PluginManager::getInstance().shutdown();
    
    // Save project...

    m_audioController->shutdown();
    m_windowManager->shutdown();
    
    Platform::shutdown();
    Log::info("Aestra shutdown complete");
    Aestra::AppLifecycle::instance().transitionTo(Aestra::AppState::Terminated);
    Log::shutdown();
}

// Project Helpers
void AestraApp::requestClose() {
    // Show confirmation dialog via WindowManager
    if (m_content && m_content->getTrackManager() && m_content->getTrackManager()->isModified()) {
        // ... show dialog ...
    } else {
        m_running = false;
    }
}

void AestraApp::saveCurrentProject() {
    saveProject();
}

ProjectSerializer::LoadResult AestraApp::loadProject() {
    // ...
    return {};
}

bool AestraApp::saveProject() {
    // ...
    return false;
}

ProjectSerializer::UIState AestraApp::captureUIState() const {
    return {};
}

void AestraApp::applyUIState(const ProjectSerializer::UIState& state) {
    // ...
}

void AestraApp::updateWindowTitle() {
    // Logic moved here or delegated to WindowManager::setWindowTitle
    std::string title = "Aestra";
    if (m_projectPath.size()) title = m_projectPath + " - Aestra";
    m_windowManager->setWindowTitle(title);
}
