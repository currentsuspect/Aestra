// © 2025 Aestra Studios - All Rights Reserved. Licensed for personal & educational use only.
#include "AestraApp.h"
#include "AppLifecycle.h"
#include "ServiceLocator.h"
#include "AestraRootComponent.h"
#include "AudioThreadConstraints.h"
#include "Preferences.h"
#include "UIState.h"
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
#include "../Settings/ExportDialog.h"
#include "PluginManager.h"
#include "AudioGraphBuilder.h"
#include "../../AestraAudio/include/IO/AudioExporter.h"

#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <cmath>
#include <algorithm>
#include <filesystem>

using namespace Aestra;
using namespace AestraUI;
using namespace Aestra::Audio;

namespace {
void syncRecordingProjectPath(const std::shared_ptr<AestraContent>& content, const std::string& projectPath) {
    if (!content) {
        return;
    }
    if (auto trackManager = content->getTrackManager()) {
        trackManager->setRecordingProjectPath(projectPath);
    }
}
}

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

std::string AestraApp::getCrashFlagPath() {
    return (std::filesystem::path(getAppDataPath()) / "crash_flag").string();
}

void AestraApp::writeCrashFlag() {
    std::string flagPath = getCrashFlagPath();
    std::error_code ec;
    std::ofstream out(flagPath, std::ios::trunc);
    if (out) {
        out << std::chrono::system_clock::now().time_since_epoch().count();
        out.close();
        Log::info("[CrashDetection] Wrote crash flag: " + flagPath);
    } else {
        Log::warning("[CrashDetection] Failed to write crash flag: " + flagPath);
    }
}

void AestraApp::clearCrashFlag() {
    std::string flagPath = getCrashFlagPath();
    std::error_code ec;
    if (std::filesystem::exists(flagPath, ec)) {
        std::filesystem::remove(flagPath, ec);
        if (!ec) {
            Log::info("[CrashDetection] Cleared crash flag");
        } else {
            Log::warning("[CrashDetection] Failed to clear crash flag: " + ec.message());
        }
    }
}

bool AestraApp::isCrashedSession() {
    std::string flagPath = getCrashFlagPath();
    std::error_code ec;
    return std::filesystem::exists(flagPath, ec);
}

bool AestraApp::initialize(const std::string& projectPath) {
    if (!Aestra::AppLifecycle::instance().transitionTo(Aestra::AppState::Initializing)) {
        Log::error("Failed to transition to Initializing state");
        return false;
    }

    Log::info("Aestra v1.0.0 - Initializing...");

    writeCrashFlag();

    if (!Platform::initialize()) {
        Log::error("Failed to initialize platform");
        return false;
    }

    // Load preferences and UI state early
    Preferences::instance().load();
    m_autoSaveEnabled.store(Preferences::instance().autoSaveEnabled, std::memory_order_relaxed);

    UIState uiState;
    uiState.load();

    // Initialize Window with persisted state
    AestraWindowManager::WindowConfig winConfig;
    winConfig.title = "Aestra v1.0";
    winConfig.width = uiState.windowWidth;
    winConfig.height = uiState.windowHeight;
    winConfig.fullscreen = false; // Default

    if (!m_windowManager->initialize(winConfig)) {
        Log::error("Failed to initialize Window Manager");
        return false;
    }

    // Apply persisted window position/maximized state (Issue #120)
    AestraWindowManager::WindowState windowState;
    windowState.x = uiState.windowX;
    windowState.y = uiState.windowY;
    windowState.width = uiState.windowWidth;
    windowState.height = uiState.windowHeight;
    windowState.maximized = uiState.maximized;
    m_windowManager->applyWindowState(windowState);

    Log::info("[UIState] Applied window state: " + std::to_string(uiState.windowWidth) + "x" +
              std::to_string(uiState.windowHeight) + " at (" + std::to_string(uiState.windowX) + "," +
              std::to_string(uiState.windowY) + ") maximized=" + (uiState.maximized ? "true" : "false"));

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
    syncRecordingProjectPath(m_content, m_projectPath);

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

    auto exportDialog = std::make_shared<ExportDialog>();
    m_windowManager->setExportDialog(exportDialog);

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
            syncRecordingProjectPath(m_content, m_projectPath);
            m_lastWindowTitle.clear();
            Log::info("New project created");
        });

        menu->addItem("Open Project...", [this]() {
            if (auto* utils = Aestra::Platform::getUtils()) {
                const std::string filter = std::string("Aestra Project\0*.aes\0All Files\0*.*\0",
                                                       sizeof("Aestra Project\0*.aes\0All Files\0*.*\0") - 1);
                const std::string pickedPath = utils->openFileDialog("Open Project", filter);
                if (!pickedPath.empty() && std::filesystem::exists(pickedPath)) {
                    auto result = loadProjectFromPath(pickedPath);
                    if (!result.ok) {
                        Log::error("Failed to load project: " + pickedPath + " (" + result.errorMessage + ")");
                    }
                }
            }
        });

        menu->addSeparator();

        menu->addItem("Save", [this]() {
            saveCurrentProject();
        });

        menu->addItem("Save As...", [this]() {
            if (auto* utils = Aestra::Platform::getUtils()) {
                Aestra::IPlatformUtils::SaveFileDialogOptions options;
                options.title = "Save Project As";
                options.filter = std::string("Aestra Project\0*.aes\0All Files\0*.*\0",
                                             sizeof("Aestra Project\0*.aes\0All Files\0*.*\0") - 1);
                options.defaultPath = m_projectPath.empty() ? getAutosavePath() : m_projectPath;
                options.defaultExtension = "aes";
                const std::string pickedPath = utils->saveFileDialog(options);
                if (!pickedPath.empty()) {
                    m_projectPath = pickedPath;
                    if (saveProject()) {
                        Log::info("Project saved as: " + pickedPath);
                    }
                }
            }
        });

        auto historyMenu = std::make_shared<AestraUI::NUIContextMenu>();
        const auto historyEntries = ProjectSerializer::listHistory(m_projectPath);
        if (historyEntries.empty()) {
            auto emptyItem = std::make_shared<AestraUI::NUIContextMenuItem>("No Save History");
            emptyItem->setEnabled(false);
            historyMenu->addItem(emptyItem);
        } else {
            size_t count = 0;
            for (const auto& entry : historyEntries) {
                if (count++ >= 10) {
                    break;
                }
                historyMenu->addItem(entry.label, [this, snapshotPath = entry.path]() {
                    auto result = loadProjectFromPath(snapshotPath, m_projectPath);
                    if (!result.ok) {
                        Log::error("Failed to restore snapshot: " + snapshotPath + " (" + result.errorMessage + ")");
                    }
                });
            }
        }

        menu->addSubmenu("Project History", historyMenu);

        menu->addSeparator();

        menu->addItem("Export Audio...", [this]() {
            startExport();
        });

        menu->addSeparator();

        menu->addItem("Settings...", [this]() {
            if (m_windowManager->getSettingsDialog()) {
                m_windowManager->getSettingsDialog()->show();
            }
        });

        menu->addSeparator();

        menu->addItem("Exit", [this]() {
            requestClose();
        });

        m_windowManager->showDropdownMenu(menu, 10.0f);
    });
    // Edit Menu
    menuBar->addItem("Edit", [this]() {
        auto menu = std::make_shared<AestraUI::NUIContextMenu>();

        auto trackMgr = m_content ? m_content->getTrackManager() : nullptr;
        bool canUndo = trackMgr && trackMgr->getCommandHistory().canUndo();
        bool canRedo = trackMgr && trackMgr->getCommandHistory().canRedo();

        auto undoItem = std::make_shared<AestraUI::NUIContextMenuItem>();
        undoItem->setShortcut("Ctrl+Z");
        undoItem->setEnabled(canUndo);
        if (canUndo) {
            std::string undoName = trackMgr->getCommandHistory().getUndoName();
            undoItem->setText(undoName.empty() ? "Undo" : "Undo: " + undoName);
        } else {
            undoItem->setText("Undo");
        }
        undoItem->setOnClick([this]() {
            if (m_content && m_content->getTrackManager()) {
                m_content->getTrackManager()->getCommandHistory().undo();
            }
        });
        menu->addItem(undoItem);

        auto redoItem = std::make_shared<AestraUI::NUIContextMenuItem>();
        redoItem->setShortcut("Ctrl+Y");
        redoItem->setEnabled(canRedo);
        if (canRedo) {
            std::string redoName = trackMgr->getCommandHistory().getRedoName();
            redoItem->setText(redoName.empty() ? "Redo" : "Redo: " + redoName);
        } else {
            redoItem->setText("Redo");
        }
        redoItem->setOnClick([this]() {
            if (m_content && m_content->getTrackManager()) {
                m_content->getTrackManager()->getCommandHistory().redo();
            }
        });
        menu->addItem(redoItem);

        menu->addSeparator();

        menu->addItem("Cut", [this]() {
            if (auto tmUI = m_content ? m_content->getTrackManagerUI() : nullptr) {
                tmUI->cutSelectedClip();
            }
        });

        menu->addItem("Copy", [this]() {
            if (auto tmUI = m_content ? m_content->getTrackManagerUI() : nullptr) {
                tmUI->copySelectedClip();
            }
        });

        menu->addItem("Paste", [this]() {
            if (auto tmUI = m_content ? m_content->getTrackManagerUI() : nullptr) {
                tmUI->pasteClipboardAtCursor();
            }
        });

        menu->addSeparator();

        menu->addItem("Delete", [this]() {
            if (auto tmUI = m_content ? m_content->getTrackManagerUI() : nullptr) {
                tmUI->deleteSelectedClip();
            }
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
        const std::string previousProjectPath = m_projectPath;
        m_projectPath = projectPath;
        auto result = loadProject();
        if (result.ok) {
            syncRecordingProjectPath(m_content, m_projectPath);
            if (result.ui) applyUIState(*result.ui);
        } else {
            m_projectPath = previousProjectPath;
        }
    } else {
        std::string autosavePath = getAutosavePath();
        std::string timestamp;
        bool crashedSession = isCrashedSession();

        if (crashedSession && RecoveryDialog::detectAutosave(autosavePath, timestamp)) {
            // [SEC-RTM-007] Verify temporal consistency: the autosave file must have
            // been written within a reasonable window of the crash flag. A large delta
            // suggests a planted attack (pre-seeded crash flag + malicious autosave).
            bool timeConsistent = true;
            {
                std::error_code ecFlag, ecAuto;
                auto flagMtime = std::filesystem::last_write_time(
                    std::filesystem::path(getCrashFlagPath()), ecFlag);
                auto saveMtime = std::filesystem::last_write_time(
                    std::filesystem::path(autosavePath), ecAuto);
                if (!ecFlag && !ecAuto) {
                    auto deltaSec = std::abs(
                        std::chrono::duration_cast<std::chrono::seconds>(
                            saveMtime - flagMtime).count());
                    constexpr int kMaxRecoveryDeltaSec = 300; // 5 minutes
                    if (deltaSec > kMaxRecoveryDeltaSec) {
                        timeConsistent = false;
                        Log::warning("[Recovery] Crash flag and autosave mtime delta = " +
                                     std::to_string(deltaSec) + "s (limit: " +
                                     std::to_string(kMaxRecoveryDeltaSec) +
                                     "s) — discarding as possible pre-seeded attack");
                        std::error_code rmEc;
                        std::filesystem::remove(autosavePath, rmEc);
                        std::filesystem::remove(std::filesystem::path(getCrashFlagPath()), rmEc);
                        m_recoveryHandled = true;
                    }
                }
            }

            if (timeConsistent) {
            Log::info("[Recovery] Crash detected, showing recovery dialog");
            // Store the path for later use and show the recovery dialog
            m_pendingAutosavePath = autosavePath;
            m_recoveryHandled = false;

            if (auto recoveryDialog = m_windowManager->getRecoveryDialog()) {
                recoveryDialog->show(autosavePath, [this, autosavePath](Aestra::RecoveryResponse response) {
                    m_recoveryHandled = true;
                    if (response == Aestra::RecoveryResponse::Recover) {
                        // User chose to recover - load the autosave
                        const std::string previousProjectPath = m_projectPath;
                        m_projectPath = autosavePath;
                        auto result = loadProject();
                        if (result.ok) {
                            syncRecordingProjectPath(m_content, m_projectPath);
                            if (result.ui) applyUIState(*result.ui);
                            Log::info("[Recovery] Autosave recovered successfully");
                        } else {
                            m_projectPath = previousProjectPath;
                            Log::error("[Recovery] Failed to load autosave");
                            // Fall back to empty project
                            if (m_content) m_content->resetToDefaultProject();
                        }
                    } else if (response == Aestra::RecoveryResponse::Discard) {
                        // User chose to discard - remove autosave and start fresh
                        std::error_code ec;
                        std::filesystem::remove(autosavePath, ec);
                        if (ec) {
                            Log::warning("[Recovery] Failed to remove autosave: " + ec.message());
                        } else {
                            Log::info("[Recovery] Autosave discarded");
                        }
                        if (m_content) m_content->resetToDefaultProject();
                        m_projectPath = getAutosavePath();
                        syncRecordingProjectPath(m_content, m_projectPath);
                    }
                });
                Log::info("[Recovery] Showing recovery dialog for autosave");
            } else {
                // [SEC-RTM-015] RecoveryDialog unavailable — do NOT silently load
                // the autosave, as it may have been planted by a local attacker.
                // Instead, discard it and start with a fresh project.
                Log::warning("[Recovery] RecoveryDialog not available — discarding "
                             "autosave for safety (possible pre-seeded crash recovery attack)");
                std::error_code ec;
                std::filesystem::remove(autosavePath, ec);
                if (ec) {
                    Log::warning("[Recovery] Failed to remove suspicious autosave: " + ec.message());
                }
                m_recoveryHandled = true;
            }
            } // if (timeConsistent)
        } else {
            // No autosave found - mark recovery as "handled" (nothing to do)
            m_recoveryHandled = true;
        }
    }

    // Apply persisted panel layout state (Issue #120)
    // This happens after all UI components are initialized
    if (m_content) {
        m_content->setBrowserVisible(uiState.browserVisible);
        m_content->setBrowserWidth(uiState.browserWidth);
        m_content->setMixerVisible(uiState.mixerVisible);
        Log::info("[UIState] Applied panel state: browserVisible=" + std::string(uiState.browserVisible ? "true" : "false") +
                  ", browserWidth=" + std::to_string(uiState.browserWidth) +
                  ", mixerVisible=" + std::string(uiState.mixerVisible ? "true" : "false"));
    }

    // Apply file browser state (Issue #120: expanded folders + last browsed path)
    if (m_content && m_content->getFileBrowser()) {
        auto fileBrowser = m_content->getFileBrowser();

        // Restore last browsed path if valid
        if (!uiState.lastBrowsedPath.empty() && std::filesystem::exists(uiState.lastBrowsedPath)) {
            fileBrowser->setCurrentPath(uiState.lastBrowsedPath);
            Log::info("[UIState] Restored file browser path: " + uiState.lastBrowsedPath);
        }

        // Restore expanded folders
        if (!uiState.expandedFolders.empty()) {
            std::vector<std::string> folders(uiState.expandedFolders.begin(), uiState.expandedFolders.end());
            fileBrowser->expandFolders(folders);
            Log::info("[UIState] Restored expanded folders: " + std::to_string(folders.size()));
        }
    }

    // Issue #120: Apply persisted track view zoom/scroll state
    if (m_content && m_content->getTrackManagerUI()) {
        auto trackManagerUI = m_content->getTrackManagerUI();
        // Only apply if values differ from defaults (user has customized)
        if (uiState.horizontalZoom != 1.0f || uiState.scrollPositionX != 0.0f || uiState.scrollPositionY != 0.0f) {
            trackManagerUI->setHorizontalZoom(uiState.horizontalZoom);
            trackManagerUI->setHorizontalScroll(uiState.scrollPositionX);
            trackManagerUI->setVerticalScroll(uiState.scrollPositionY);
            Log::info("[UIState] Applied track view state: zoom=" + std::to_string(uiState.horizontalZoom) +
                      ", hScroll=" + std::to_string(uiState.scrollPositionX) +
                      ", vScroll=" + std::to_string(uiState.scrollPositionY));
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
            Log::info("[AestraApp] TrackManager audio config synced. SampleRate=" + std::to_string(config.sampleRate) +
                      ", InputChannels=" + std::to_string(config.numInputChannels) +
                      ", OutputChannels=" + std::to_string(config.numOutputChannels));

            auto meterBuffer = std::make_shared<Audio::MeterSnapshotBuffer>();
            m_audioController->getEngine()->setMeterSnapshots(meterBuffer);
            m_content->getTrackManager()->setMeterSnapshots(meterBuffer);
            m_audioController->getEngine()->setContinuousParams(m_content->getTrackManager()->getContinuousParams());

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
        // Play / pause / stop / metronome are owned by AestraContent so transport-aware
        // features like count-in, preview stop, and deferred capture all go through one path.

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

    m_windowManager->setOnExportRequested([this]() {
        startExport();
    });

    m_windowManager->setTransportCallback([this](AestraWindowManager::TransportAction action) {
        if (!m_content) return;
        using Action = AestraWindowManager::TransportAction;

        if (action == Action::Play) {
            m_content->requestTransportPlay();
        }
        else if (action == Action::Pause) {
            m_content->pauseFromCurrentFocus();
        }
        else if (action == Action::Stop) {
            m_content->stopFromCurrentFocus(false);
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
                } else if (engine->isTransportPlaying() && tm->isPlaying()) {
                    double realTime = engine->getPositionSeconds();
                    tm->syncPositionFromEngine(realTime);
                }

                if (m_content->getTransportBar()) {
                   auto* transportBar = m_content->getTransportBar();
                   transportBar->setPosition(tm->getPosition());
                   transportBar->syncTransportState(tm->isPlaying(), tm->isPaused(), tm->isRecordArmed());
                }
                updateWindowTitle();
            }

            // Rebuild graph check
            if (m_audioController->getEngine() && m_content && m_content->getTrackManager() &&
                m_content->getTrackManager()->consumeGraphDirty()) {
                    auto graph = AudioGraphBuilder::buildFromTrackManager(*m_content->getTrackManager(), m_audioController->getSampleRate());
                    m_audioController->getEngine()->setGraph(graph);
                    if (auto slotMap = m_content->getTrackManager()->getChannelSlotMapShared()) {
                        m_audioController->getEngine()->setChannelSlotMap(slotMap);
                    }
                    m_audioController->getEngine()->setContinuousParams(m_content->getTrackManager()->getContinuousParams());
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

    clearCrashFlag();

    // Save preferences and UI state (Issue #120)
    Preferences::instance().save();

    // Capture current window/panel states and save UI state
    UIState uiState;
    if (m_windowManager) {
        // Capture window geometry from actual window
        auto windowState = m_windowManager->captureWindowState();
        uiState.windowX = windowState.x;
        uiState.windowY = windowState.y;
        uiState.windowWidth = windowState.width;
        uiState.windowHeight = windowState.height;
        uiState.maximized = windowState.maximized;

        Log::info("[UIState] Captured window state: " + std::to_string(windowState.width) + "x" +
                  std::to_string(windowState.height) + " at (" + std::to_string(windowState.x) + "," +
                  std::to_string(windowState.y) + ") maximized=" + (windowState.maximized ? "true" : "false"));
    }

    // Capture panel states from AestraContent (Issue #120)
    if (m_content) {
        uiState.browserVisible = m_content->isBrowserVisible();
        uiState.browserWidth = m_content->getBrowserWidth();
        uiState.mixerVisible = m_content->isMixerVisible();

        Log::info("[UIState] Captured panel state: browserVisible=" +
                  std::string(uiState.browserVisible ? "true" : "false") +
                  ", browserWidth=" + std::to_string(uiState.browserWidth) +
                  ", mixerVisible=" + std::string(uiState.mixerVisible ? "true" : "false"));
    }

    // Capture file browser state (Issue #120: expanded folders + last selected path)
    if (m_content && m_content->getFileBrowser()) {
        auto fileBrowser = m_content->getFileBrowser();

        // Get expanded folders
        auto expanded = fileBrowser->getExpandedFolders();
        uiState.expandedFolders.clear();
        for (const auto& path : expanded) {
            uiState.expandedFolders.insert(path);
        }

        // Get current path (last browsed)
        uiState.lastBrowsedPath = fileBrowser->getCurrentPath();

        Log::info("[UIState] Captured file browser state: expandedFolders=" + 
                  std::to_string(uiState.expandedFolders.size()) +
                  ", lastPath=" + uiState.lastBrowsedPath);
    }

    // Issue #120: Capture track view zoom/scroll state
    if (m_content && m_content->getTrackManagerUI()) {
        auto trackManagerUI = m_content->getTrackManagerUI();
        uiState.horizontalZoom = trackManagerUI->getHorizontalZoom();
        uiState.scrollPositionX = trackManagerUI->getHorizontalScroll();
        uiState.scrollPositionY = trackManagerUI->getVerticalScroll();
        Log::info("[UIState] Captured track view state: zoom=" + std::to_string(uiState.horizontalZoom) +
                  ", hScroll=" + std::to_string(uiState.scrollPositionX) +
                  ", vScroll=" + std::to_string(uiState.scrollPositionY));
    }

    uiState.save();

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
    auto trackManager = m_content ? m_content->getTrackManager() : nullptr;
    if (!trackManager || !trackManager->isModified()) {
        m_running = false;
        return;
    }

    if (!m_windowManager) {
        return;
    }

    auto dialog = m_windowManager->getConfirmationDialog();
    if (!dialog) {
        return;
    }

    if (auto* root = m_windowManager->getRootComponent()) {
        dialog->setBounds(root->getBounds());
    }

    dialog->show("Unsaved Changes",
                 "You have unsaved changes. Save before closing?",
                 [this](Aestra::DialogResponse response) {
                     switch (response) {
                     case Aestra::DialogResponse::Save:
                         if (saveProject()) {
                             m_running = false;
                         }
                         break;
                     case Aestra::DialogResponse::DontSave:
                         m_running = false;
                         break;
                     case Aestra::DialogResponse::Cancel:
                     case Aestra::DialogResponse::None:
                     default:
                         break;
                     }
                 });
}

void AestraApp::saveCurrentProject() {
    saveProject();
}

ProjectSerializer::LoadResult AestraApp::loadProjectFromPath(const std::string& path, const std::string& activeProjectPathOverride) {
    ProjectSerializer::LoadResult result;
    if (path.empty()) {
        result.errorMessage = "Project path is empty";
        return result;
    }

    const std::string previousProjectPath = m_projectPath;
    m_projectPath = activeProjectPathOverride.empty() ? path : activeProjectPathOverride;

    result = ProjectSerializer::load(path, m_content ? m_content->getTrackManager() : nullptr);
    if (!result.ok) {
        m_projectPath = previousProjectPath;
        Log::error("Failed to load project: " + path + " (" + result.errorMessage + ")");
        return result;
    }

    if (m_audioController && m_audioController->getEngine()) {
        auto* engine = m_audioController->getEngine();
        engine->setTransportPlaying(false);
        engine->setBPM(static_cast<float>(result.tempo));
        const double sampleRate = std::max(1.0, static_cast<double>(engine->getSampleRate()));
        engine->setGlobalSamplePos(static_cast<uint64_t>(std::max(0.0, result.playhead) * sampleRate));
    }

    if (m_content) {
        if (auto trackManager = m_content->getTrackManager()) {
            trackManager->setPosition(result.playhead);
            trackManager->setPlayStartPosition(result.playhead);
            trackManager->setModified(false);
        }

        if (auto transportBar = m_content->getTransportBar()) {
            transportBar->setTempo(static_cast<float>(result.tempo));
            transportBar->setPosition(result.playhead);
        }

        m_content->refreshProjectViews();
    }

    if (m_audioController && m_audioController->getEngine() && m_content && m_content->getTrackManager()) {
        auto graph = AudioGraphBuilder::buildFromTrackManager(*m_content->getTrackManager(),
                                                              m_audioController->getSampleRate());
        m_audioController->getEngine()->setGraph(graph);
        m_content->getTrackManager()->rebuildAndPushSnapshot();
    }

    syncRecordingProjectPath(m_content, m_projectPath);
    if (result.ui) {
        applyUIState(*result.ui);
    }
    updateWindowTitle();
    Log::info("Project loaded into app state from " + path);
    return result;
}

ProjectSerializer::LoadResult AestraApp::loadProject() {
    ProjectSerializer::LoadResult result;

    if (!m_content || !m_content->getTrackManager()) {
        result.errorMessage = "No active project context";
        return result;
    }

    if (m_projectPath.empty()) {
        result.errorMessage = "Project path is empty";
        return result;
    }

    return loadProjectFromPath(m_projectPath);
}

bool AestraApp::saveProject() {
    if (!m_content || !m_content->getTrackManager()) {
        Log::warning("Cannot save project without an active TrackManager");
        return false;
    }

    if (m_projectPath.empty()) {
        m_projectPath = getAutosavePath();
    }

    const double tempo = (m_audioController && m_audioController->getEngine())
        ? static_cast<double>(m_audioController->getEngine()->getBPM())
        : (m_content->getTransportBar() ? static_cast<double>(m_content->getTransportBar()->getTempo()) : 120.0);
    const double playhead = m_content->getTrackManager()->getPosition();
    const auto uiState = captureUIState();

    const bool ok = ProjectSerializer::save(m_projectPath,
                                            m_content->getTrackManager(),
                                            tempo,
                                            playhead,
                                            &uiState);
    if (ok) {
        m_content->getTrackManager()->setModified(false);
        syncRecordingProjectPath(m_content, m_projectPath);
        updateWindowTitle();
    }
    return ok;
}

ProjectSerializer::UIState AestraApp::captureUIState() const {
    ProjectSerializer::UIState state;

    if (m_windowManager) {
        if (auto settingsDialog = m_windowManager->getSettingsDialog()) {
            state.settingsDialogVisible = settingsDialog->isVisible();
            state.settingsDialogActivePage = settingsDialog->getActivePageID();
        }
    }

    return state;
}

void AestraApp::applyUIState(const ProjectSerializer::UIState& state) {
    if (!m_windowManager) {
        return;
    }

    if (auto settingsDialog = m_windowManager->getSettingsDialog()) {
        if (!state.settingsDialogActivePage.empty()) {
            settingsDialog->setActivePage(state.settingsDialogActivePage);
        }
        if (state.settingsDialogVisible) {
            settingsDialog->show();
        } else {
            settingsDialog->hide();
        }
    }
}

void AestraApp::updateWindowTitle() {
    // Logic moved here or delegated to WindowManager::setWindowTitle
    std::string title = "Aestra";
    if (m_projectPath.size()) title = m_projectPath + " - Aestra";
    m_windowManager->setWindowTitle(title);
}

void AestraApp::startExport() {
    if (!m_content || !m_content->getTrackManager()) {
        Log::warning("No project loaded for export");
        return;
    }
    auto& engine = Aestra::Audio::AudioEngine::getInstance();
    auto& trackMgr = *m_content->getTrackManager();
    auto exportDialog = m_windowManager->getExportDialog();
    if (exportDialog) {
        exportDialog->show(m_projectPath, engine, trackMgr);
    }
}
