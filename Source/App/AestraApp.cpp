// © 2025 Aestra Studios - All Rights Reserved. Licensed for personal & educational use only.
#include "AestraApp.h"
#include "AppLifecycle.h"
#include "ServiceLocator.h"
#include "AestraRootComponent.h"
#include "AudioThreadConstraints.h"
#include "Preferences.h"
#include "../AestraCore/include/AestraUnifiedProfiler.h"
#include "../AestraCore/include/PointerRegistry.h"
#include "FileBrowser.h"
#include "TrackManagerUI.h"
#include "TransportBar.h"
#include "SettingsDialog.h"
#include "AudioSettingsPage.h"
#include "GeneralSettingsPage.h"
#include "MembershipSettingsPage.h"
#include "AppearanceSettingsPage.h"
#include "UnifiedHUD.h"
#include "RecoveryDialog.h"
#include "ConfirmationDialog.h"
#include "../Settings/ExportDialog.h"
#include "PluginManager.h"
#include "AudioGraphBuilder.h"
#include "TakeManager.h"
#include "../../AestraAudio/include/Core/PlaybackGraphController.h"
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

struct StartupTimer {
    const char* name;
    std::chrono::steady_clock::time_point start;
    explicit StartupTimer(const char* n)
        : name(n), start(std::chrono::steady_clock::now()) {}
    ~StartupTimer() {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();
        Log::info(std::string("[Startup] ") + name + ": " + std::to_string(elapsed) + " ms");
    }
};

void syncRecordingProjectPath(const std::shared_ptr<AestraContent>& content, const std::string& projectPath) {
    if (!content) {
        return;
    }
    if (auto trackManager = content->getTrackManager()) {
        trackManager->setRecordingProjectPath(projectPath);
    }
}

std::string takeMenuLabel(const TakeManager::TakeEntry& take) {
    const std::string name = take.name.empty() ? take.id : take.name;
    return take.active ? ("[current] " + name) : name;
}
}

// =============================================================================
// AestraApp Implementation
// =============================================================================

AestraApp::AestraApp()
    : m_running(false)
    , m_aliveToken(std::make_shared<bool>(true))
{
    // Initialize unified logging
    auto multiLogger = std::make_shared<MultiLogger>(LogLevel::Info);
    multiLogger->addLogger(std::make_shared<ConsoleLogger>(LogLevel::Info));
    std::string logPath = (std::filesystem::current_path() / "aestra_debug.log").string();
    auto fileLogger = std::make_shared<FileLogger>(logPath, LogLevel::Info);
    multiLogger->addLogger(fileLogger);
    m_asyncLogger = std::make_shared<AsyncLogger>(multiLogger);

    Log::init(m_asyncLogger);
    Log::info("Logging initialized to console and " + logPath);

    // Note: m_projectPath initialized lazily in initialize() after Platform is ready
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
        out << std::chrono::system_clock::now().time_since_epoch().count() << "\n";
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

std::string AestraApp::getRecoveryMarkerPath(const std::string& autosavePath) {
    return autosavePath + ".recovery";
}

std::string AestraApp::readCrashFlagToken() {
    std::ifstream in(getCrashFlagPath(), std::ios::binary);
    std::string token;
    std::getline(in, token);
    return token;
}

bool AestraApp::writeRecoveryMarkerForAutosave(const std::string& autosavePath) const {
    if (m_recoverySessionToken.empty()) {
        return false;
    }

    std::ofstream out(getRecoveryMarkerPath(autosavePath), std::ios::binary | std::ios::trunc);
    if (!out) {
        Log::warning("[Recovery] Failed to write autosave recovery marker");
        return false;
    }
    out << m_recoverySessionToken << "\n";
    return true;
}

bool AestraApp::initialize(const std::string& projectPath) {
    StartupTimer totalTimer("Total startup");

    if (!transitionToInitializing()) return false;

    Log::info("Aestra v1.0.0 - Initializing...");

    // Check for crashed session BEFORE initializing platform.
    bool crashedSession = isCrashedSession();
    m_previousRecoverySessionToken = crashedSession ? readCrashFlagToken() : "";

    {
        StartupTimer t("Platform init");
        if (!Platform::initialize()) {
            Log::error("Failed to initialize platform");
            return false;
        }
    }

    // Initialize m_projectPath AFTER Platform is initialized
    m_projectPath = getAutosavePath();

    // ALWAYS write crash flag at session start. It is cleared only on clean
    // shutdown. If the app crashes at any point, the flag persists and the
    // next launch will detect it.
    writeCrashFlag();
    m_recoverySessionToken = readCrashFlagToken();

    // Load preferences and UI state early
    Preferences::instance().load();
    bool autoSaveEnabled = Preferences::instance().autoSaveEnabled;

    UIState uiState;
    uiState.load();

    {
        StartupTimer t("Platform + Window");
        if (!initializePlatformAndWindow(uiState)) return false;
    }
    {
        StartupTimer t("Audio init");
        initializeAudio();
    }
    {
        StartupTimer t("Content init");
        initializeContent();
    }
    {
        StartupTimer t("Autosave init");
        initializeAutosave(autoSaveEnabled);
    }
    {
        StartupTimer t("Recovery dialog");
        buildRecoveryDialog();
    }
    {
        StartupTimer t("Menu bar");
        buildMenuBar();
    }
    {
        StartupTimer t("Plugins init");
        initializePlugins();
    }
    {
        StartupTimer t("Project load/recovery");
        loadOrRecoverProject(projectPath, crashedSession);
    }
    {
        StartupTimer t("UI state restore");
        restoreUIState(uiState);
    }

    return transitionToRunning();
}

bool AestraApp::transitionToInitializing() {
    if (!Aestra::AppLifecycle::instance().transitionTo(Aestra::AppState::Initializing)) {
        Log::error("Failed to transition to Initializing state");
        return false;
    }
    return true;
}

bool AestraApp::initializePlatformAndWindow(const UIState& uiState) {
    AestraWindowManager::WindowConfig winConfig;
    winConfig.title = "Aestra v1.0";
    winConfig.width = uiState.windowWidth;
    winConfig.height = uiState.windowHeight;
    winConfig.fullscreen = false;

    if (!m_windowManager->initialize(winConfig)) {
        Log::error("Failed to initialize Window Manager");
        return false;
    }

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
    return true;
}

bool AestraApp::initializeAudio() {
    if (!m_audioController->initialize()) {
        Log::warning("Audio Controller initialization failed (continuing without audio)");
        return false;
    }
    // Heavy stream open/start is deferred to finalizeAudioSetup() after the
    // window is visible, for instant startup perception.
    return true;
}

void AestraApp::initializeContent() {
    m_content = std::make_shared<AestraContent>();
    m_content->setPlatformBridge(m_windowManager->getWindow());
    m_content->setAudioStatus(m_audioController->isInitialized());
    if (m_audioController->getEngine()) {
        m_content->setAudioEngine(m_audioController->getEngine());
    }
    syncRecordingProjectPath(m_content, m_projectPath);

    m_windowManager->setContent(m_content);
    m_audioController->setContent(m_content);
}

void AestraApp::initializeAutosave(bool enabled) {
    Aestra::Audio::AutosaveManager::Config config;
    config.enabled = enabled;
    config.autosaveInterval = std::chrono::seconds(60);
    config.autosavePathOverride = getAutosavePath();
    config.onAutosaveCommitted = [this](const std::string& autosavePath) -> bool {
        return writeRecoveryMarkerForAutosave(autosavePath);
    };
    config.serializer = [this](std::string& outData) -> bool {
        if (!m_content || !m_content->getTrackManager()) return false;
        const double tempo = (m_audioController && m_audioController->getEngine())
            ? static_cast<double>(m_audioController->getEngine()->getBPM())
            : (m_content->getTransportBar() ? static_cast<double>(m_content->getTransportBar()->getTempo()) : 120.0);
        const double playhead = m_content->getTrackManager()->getPosition();
        auto ser = ProjectSerializer::serialize(m_content->getTrackManager(), tempo, playhead, 0);
        if (!ser.ok) return false;
        outData = std::move(ser.contents);
        return true;
    };
    m_autoSaveManager.initialize(m_projectPath, std::move(config));
}

void AestraApp::buildRecoveryDialog() {
    m_windowManager->setRecoveryDialog(std::make_shared<RecoveryDialog>());
}

void AestraApp::buildSettingsAndDialogs() {
    StartupTimer t("SettingsAndDialogs build");
    auto settingsDialog = std::make_shared<SettingsDialog>();
    auto generalPage = std::make_shared<GeneralSettingsPage>();
    generalPage->setOnAutoSaveToggled([this](bool enabled) {
        m_autoSaveManager.setEnabled(enabled);
        Log::info(std::string("[AutoSave] ") + (enabled ? "Enabled" : "Disabled"));
    });
    settingsDialog->addPage(generalPage);

    auto audioPage = std::make_shared<AudioSettingsPage>(
        m_audioController->getDeviceManager(),
        m_audioController->getEngine()
    );
    audioPage->setOnStreamRestore([this]() {
         m_audioController->closeStream();
         m_audioStreamReady = false;
         m_audioConfigSynced = false;
         if (m_audioController->openDefaultStream(nullptr)) {
             m_audioController->startStream();
         }
    });
    settingsDialog->addPage(audioPage);
    auto membershipPage = std::make_shared<MembershipSettingsPage>();
    membershipPage->setOnSignOutConfirmed([settingsDialog]() {
        settingsDialog->hide();
        // TODO: Transition app to activation / sign-in screen
    });
    settingsDialog->addPage(membershipPage);
    settingsDialog->addPage(std::make_shared<AppearanceSettingsPage>());
    settingsDialog->setBounds(AestraUI::NUIRect(0, 0, 950, 600));

    m_windowManager->setSettingsDialog(settingsDialog);
    m_windowManager->setConfirmationDialog(std::make_shared<ConfirmationDialog>());

    auto exportDialog = std::make_shared<ExportDialog>();
    m_windowManager->setExportDialog(exportDialog);

    auto unifiedHUD = std::make_shared<UnifiedHUD>(m_windowManager->getAdaptiveFPS());
    unifiedHUD->setVisible(false);
    unifiedHUD->setAudioEngine(m_audioController->getEngine());
    m_windowManager->setUnifiedHUD(unifiedHUD);
}

void AestraApp::ensureSettingsAndDialogs() {
    if (!m_windowManager->getSettingsDialog()) {
        buildSettingsAndDialogs();
    }
}

void AestraApp::buildMenuBar() {
    auto menuBar = std::make_shared<AestraUI::NUIMenuBar>();

    // File Menu
    menuBar->addItem("File", [this]() {
        auto menu = std::make_shared<AestraUI::NUIContextMenu>();

        menu->addItem("New Project", [this]() {
            if (m_content && m_content->getTrackManager()) m_content->getTrackManager()->stop();
            if (m_content) m_content->resetToDefaultProject();
            m_projectPath = getAutosavePath();
            reinitAutosaveManager();
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

        menu->addItem("Save", [this]() { saveCurrentProject(); });

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
                    reinitAutosaveManager();
                    if (saveProject()) {
                        Log::info("Project saved as: " + pickedPath);
                    }
                }
            }
        });

        auto takesMenu = std::make_shared<AestraUI::NUIContextMenu>();
        const bool hasProjectContext = m_content && m_content->getTrackManager() && !m_projectPath.empty();
        if (!hasProjectContext) {
            auto emptyItem = std::make_shared<AestraUI::NUIContextMenuItem>("No Active Project");
            emptyItem->setEnabled(false);
            takesMenu->addItem(emptyItem);
        } else {
            takesMenu->addItem("Create Take from Current State", [this]() {
                if (!createTakeFromCurrentProject()) {
                    Log::error("Failed to create Take");
                }
            });
            takesMenu->addItem("Save Active Take", [this]() {
                if (!saveProject()) {
                    Log::error("Failed to save active Take");
                }
            });
            takesMenu->addSeparator();

            const auto manifest = TakeManager::loadManifest(m_projectPath);
            if (!manifest.ok) {
                if (manifest.errorMessage == "No Takes manifest") {
                    auto emptyItem = std::make_shared<AestraUI::NUIContextMenuItem>("No Takes Yet");
                    emptyItem->setEnabled(false);
                    takesMenu->addItem(emptyItem);
                } else {
                    Log::warning("[Takes] Could not load manifest: " + manifest.errorMessage);
                    auto errItem = std::make_shared<AestraUI::NUIContextMenuItem>("Error loading takes");
                    errItem->setEnabled(false);
                    takesMenu->addItem(errItem);
                }
            } else {
                size_t count = 0;
                for (const auto& take : manifest.takes) {
                    if (count++ >= 20)
                        break;
                    auto item = std::make_shared<AestraUI::NUIContextMenuItem>(takeMenuLabel(take));
                    item->setEnabled(!take.active);
                    item->setOnClick([this, takeId = take.id]() {
                        auto result = switchToTake(takeId);
                        if (!result.ok) {
                            Log::error("Failed to switch Take: " + takeId + " (" + result.errorMessage + ")");
                        }
                    });
                    takesMenu->addItem(item);
                }
            }
        }

        menu->addSubmenu("Takes", takesMenu);

        auto historyMenu = std::make_shared<AestraUI::NUIContextMenu>();
        const auto historyEntries = ProjectSerializer::listHistory(m_projectPath);
        if (historyEntries.empty()) {
            auto emptyItem = std::make_shared<AestraUI::NUIContextMenuItem>("No Save History");
            emptyItem->setEnabled(false);
            historyMenu->addItem(emptyItem);
        } else {
            size_t count = 0;
            for (const auto& entry : historyEntries) {
                if (count++ >= 10) break;
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
        menu->addItem("Export Audio...", [this]() { startExport(); });
        menu->addSeparator();
        menu->addItem("Settings...", [this]() {
            ensureSettingsAndDialogs();
            if (m_windowManager->getSettingsDialog()) {
                m_windowManager->getSettingsDialog()->show();
            }
        });
        menu->addSeparator();
        menu->addItem("Exit", [this]() { requestClose(); });

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
        undoItem->setText(canUndo ? trackMgr->getCommandHistory().getUndoName() : "Undo");
        undoItem->setOnClick([this]() {
            if (m_content && m_content->getTrackManager()) {
                m_content->getTrackManager()->getCommandHistory().undo();
            }
        });
        menu->addItem(undoItem);

        auto redoItem = std::make_shared<AestraUI::NUIContextMenuItem>();
        redoItem->setShortcut("Ctrl+Y");
        redoItem->setEnabled(canRedo);
        redoItem->setText(canRedo ? trackMgr->getCommandHistory().getRedoName() : "Redo");
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
            if (m_windowManager) m_windowManager->toggleFullScreen();
        });
        menu->addSeparator();
        menu->addItem("Show Timeline", [this]() {
            Log::info("Show Timeline - Not yet fully implemented");
        });
        menu->addItem("Show Arsenal", [this]() {
            Log::info("Show Arsenal - Not yet fully implemented");
        });
        menu->addItem("Show History  Ctrl+H", [this]() {
            if (m_content) m_content->toggleHistoryPanel();
        });

        m_windowManager->showDropdownMenu(menu, 100.0f);
    });

    // Help Menu
    menuBar->addItem("Help", [this]() {
        auto menu = std::make_shared<AestraUI::NUIContextMenu>();
        menu->addItem("About Aestra", []() {
            Log::info("Aestra Help: About requested");
        });
        m_windowManager->showDropdownMenu(menu, 145.0f);
    });

    menuBar->setBounds(AestraUI::NUIRect(10.0f, 4.0f, 230.0f, 24.0f));
    m_windowManager->setMenuBar(menuBar);

    setupCallbacks();
    m_windowManager->initializeCustomCursors();
    connectAudioToUI();
    m_running = true;
}

void AestraApp::initializePlugins() {
    if (Aestra::Audio::PluginManager::getInstance().initialize()) {
        Log::info("Plugin Manager initialized");
        Aestra::Audio::PluginManager::getInstance().getScanner().addDefaultSearchPaths();
    }
}

void AestraApp::loadOrRecoverProject(const std::string& projectPath, bool crashedSession) {
    if (!projectPath.empty() && std::filesystem::exists(projectPath)) {
        const std::string previousProjectPath = m_projectPath;
        m_projectPath = projectPath;
        auto result = loadProject();
        if (result.ok) {
            reinitAutosaveManager();
            syncRecordingProjectPath(m_content, m_projectPath);
            if (result.ui) applyUIState(*result.ui);
        } else {
            m_projectPath = previousProjectPath;
        }
        return;
    }

    std::string autosavePath = getAutosavePath();
    std::string timestamp;

    const std::string recoveryMarkerPath = getRecoveryMarkerPath(autosavePath);

    if (crashedSession && !m_previousRecoverySessionToken.empty() &&
        RecoveryDialog::detectAutosave(autosavePath, recoveryMarkerPath, m_previousRecoverySessionToken, timestamp)) {
        Log::info("[Recovery] Crash detected, showing recovery dialog");
        m_pendingAutosavePath = autosavePath;
        m_recoveryHandled = false;

        if (auto recoveryDialog = m_windowManager->getRecoveryDialog()) {
            auto alive = m_aliveToken;
            recoveryDialog->show(autosavePath, [this, alive, autosavePath](Aestra::RecoveryResponse response) {
                if (!*alive) return;
                m_recoveryHandled = true;
                if (response == Aestra::RecoveryResponse::Recover) {
                    const std::string previousProjectPath = m_projectPath;
                    m_projectPath = autosavePath;
                    auto result = loadProject();
                    if (result.ok) {
                        reinitAutosaveManager();
                        syncRecordingProjectPath(m_content, m_projectPath);
                        if (result.ui) applyUIState(*result.ui);
                        Log::info("[Recovery] Autosave recovered successfully");
                    } else {
                        m_projectPath = previousProjectPath;
                        Log::error("[Recovery] Failed to load autosave");
                        if (m_content) m_content->resetToDefaultProject();
                    }
                } else if (response == Aestra::RecoveryResponse::Discard) {
                    std::error_code ec;
                    std::filesystem::remove(autosavePath, ec);
                    std::filesystem::remove(getRecoveryMarkerPath(autosavePath), ec);
                    if (ec) {
                        Log::warning("[Recovery] Failed to remove autosave: " + ec.message());
                    } else {
                        Log::info("[Recovery] Autosave discarded");
                    }
                    if (m_content) m_content->resetToDefaultProject();
                    m_projectPath = getAutosavePath();
                    reinitAutosaveManager();
                    syncRecordingProjectPath(m_content, m_projectPath);
                }
            });
            Log::info("[Recovery] Showing recovery dialog for autosave");
        } else {
            Log::warning("[Recovery] RecoveryDialog not available — discarding autosave");
            std::error_code ec;
            std::filesystem::remove(autosavePath, ec);
            std::filesystem::remove(recoveryMarkerPath, ec);
            m_recoveryHandled = true;
        }
    } else {
        m_recoveryHandled = true;
    }
}

void AestraApp::restoreUIState(const UIState& uiState) {
    if (m_content) {
        m_content->setBrowserVisible(uiState.browserVisible);
        m_content->setBrowserWidth(uiState.browserWidth);
        m_content->setMixerVisible(uiState.mixerVisible);
        Log::info("[UIState] Applied panel state: browserVisible=" +
                  std::string(uiState.browserVisible ? "true" : "false") +
                  ", browserWidth=" + std::to_string(uiState.browserWidth) +
                  ", mixerVisible=" + std::string(uiState.mixerVisible ? "true" : "false"));
    }

    if (m_content && m_content->getFileBrowser()) {
        auto fileBrowser = m_content->getFileBrowser();
        if (!uiState.lastBrowsedPath.empty() && std::filesystem::exists(uiState.lastBrowsedPath)) {
            fileBrowser->setCurrentPath(uiState.lastBrowsedPath);
            Log::info("[UIState] Restored file browser path: " + uiState.lastBrowsedPath);
        }
        if (!uiState.expandedFolders.empty()) {
            std::vector<std::string> folders(uiState.expandedFolders.begin(), uiState.expandedFolders.end());
            fileBrowser->expandFolders(folders);
            Log::info("[UIState] Restored expanded folders: " + std::to_string(folders.size()));
        }
    }

    if (m_content && m_content->getTrackManagerUI()) {
        auto trackManagerUI = m_content->getTrackManagerUI();
        if (uiState.horizontalZoom != 1.0f || uiState.scrollPositionX != 0.0f || uiState.scrollPositionY != 0.0f) {
            trackManagerUI->setHorizontalZoom(uiState.horizontalZoom);
            trackManagerUI->setHorizontalScroll(uiState.scrollPositionX);
            trackManagerUI->setVerticalScroll(uiState.scrollPositionY);
            Log::info("[UIState] Applied track view state: zoom=" + std::to_string(uiState.horizontalZoom) +
                      ", hScroll=" + std::to_string(uiState.scrollPositionX) +
                      ", vScroll=" + std::to_string(uiState.scrollPositionY));
        }
    }
}

bool AestraApp::transitionToRunning() {
    if (!Aestra::AppLifecycle::instance().transitionTo(Aestra::AppState::Running)) {
        Log::error("Failed to transition to Running state");
        return false;
    }
    return true;
}

void AestraApp::connectAudioToUI() {
    // Connect deferred audio settings (idempotent — safe to call again after stream opens)
    if (m_audioController->getEngine() && m_content && m_content->getTrackManager()) {
        if (m_audioController->isInitialized() && !m_audioConfigSynced) {
            m_audioConfigSynced = true;
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
            trackMgr->setOnModified([this]() {
                m_autoSaveManager.markDirty();
            });

            Aestra::PointerRegistry::expectNotNull("AudioEngine", m_audioController->getEngine());
            Aestra::PointerRegistry::expectNotNull("TrackManager", trackMgr.get());
            Aestra::PointerRegistry::validateAll();
        }
    }

    // Transport Bar Wiring
    if (m_content && m_content->getTransportBar() && m_audioController->getEngine()) {
        // Play / pause / stop / metronome are owned by AestraContent so transport-aware
        // features like count-in, preview stop, and deferred capture all go through one path.

        // Tempo change — re-resolve the engine inside the callback to avoid capturing
        // a raw pointer that could dangle if the audio controller is destroyed.
        m_content->getTransportBar()->setOnTempoChange([this](float bpm) {
            if (auto* engine = m_audioController ? m_audioController->getEngine() : nullptr) {
                engine->setBPM(bpm);
            }
            if (m_content) {
                m_content->setPluginTempo(bpm);
            }
        });

        // Load metronome click sounds (Redundant but safe fallback if Controller update missed it)
        if (auto* engine = m_audioController->getEngine()) {
            engine->loadMetronomeClicks(
                "AestraAudio/assets/Aestra_metronome.wav",
                "AestraAudio/assets/Aestra_metronome_up.wav"
            );
            engine->setBPM(120.0f);
        }
    }
}

void AestraApp::finalizeAudioSetup() {
    if (m_audioStreamReady) return;
    StartupTimer t("Deferred audio stream start");
    // Pass nullptr so openDefaultStream falls back to 'this' (the controller)
    // as the audio-callback userData, which the callback expects.
    if (m_audioController->openDefaultStream(nullptr)) {
        m_audioController->startStream();
        if (m_content) {
            m_content->setAudioStatus(true);
        }
        connectAudioToUI(); // Sync configs now that stream is open
        m_audioStreamReady = true;
    }
}

void AestraApp::setupCallbacks() {
    m_windowManager->setCloseCallback([this]() {
        requestClose();
    });

    m_windowManager->setOnExportRequested([this]() {
        startExport();
    });

    m_windowManager->setSaveCallback([this]() {
        saveCurrentProject();
    });

    m_windowManager->setTransportCallback([this](Aestra::TransportAction action) {
        if (!m_content) return;

        if (action == Aestra::TransportAction::Play) {
            m_content->requestTransportPlay();
        }
        else if (action == Aestra::TransportAction::Pause) {
            m_content->pauseFromCurrentFocus();
        }
        else if (action == Aestra::TransportAction::Stop) {
            m_content->stopFromCurrentFocus(false);
        }
    });

    // Other callbacks handled by WindowManager internally
}

void AestraApp::run() {
    m_windowManager->render(); // Initial render — window visible NOW

    // Complete heavy audio init after window is already showing,
    // so the UI feels instant even if the stream takes a second.
    finalizeAudioSetup();

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

            // Rebuild graph check - uses PlaybackGraphController for canonical drain
            if (m_audioController->getEngine() && m_content) {
                auto* controller = m_content->getPlaybackGraphController();
                if (controller) {
                    controller->drainIfDirty(m_audioController->getSampleRate());
                }
            }
        }

        {
            AESTRA_ZONE("Render_Prep");
            m_windowManager->render();
        }

        // Autosave is handled entirely by AutosaveManager's background thread,
        // triggered via markDirty() from TrackManager::setOnModified().

        // Non-realtime audio work (graph rebuilds, plugin state commits) should
        // happen before frame pacing so it doesn't eat into the sleep budget.
        if (m_audioController && m_audioController->getEngine()) {
            m_audioController->getEngine()->performNonRealtimeMaintenance();
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

    // Invalidate lifetime token so any async callbacks that fire during teardown
    // bail out before touching partially-destroyed members.
    if (m_aliveToken) *m_aliveToken = false;

    // Emergency autosave before clearing crash flag — if shutdown crashes after this,
    // the autosave is still available for recovery on next launch.
    if (m_content && m_content->getTrackManager() && m_content->getTrackManager()->isModified()) {
        Log::info("[SHUTDOWN] Emergency autosave before shutdown...");
        m_autoSaveManager.forceAutosave();
    }
    m_autoSaveManager.shutdown();

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

    Aestra::Audio::PluginManager::getInstance().shutdown();

    m_audioController->shutdown();
    m_windowManager->shutdown();

    Platform::shutdown();
    Log::info("Aestra shutdown complete");
    Aestra::AppLifecycle::instance().transitionTo(Aestra::AppState::Terminated);

    // Clear crash flag LAST — if anything above crashes, the flag persists.
    // Also clear ServiceLocator last so shutdown paths can still resolve services.
    Aestra::ServiceLocator::clear();
    clearCrashFlag();
    Log::shutdown();
}

// Project Helpers
void AestraApp::requestClose() {
    auto trackManager = m_content ? m_content->getTrackManager() : nullptr;
    if (!trackManager || !trackManager->isModified()) {
        m_running = false;
        return;
    }

    // Emergency autosave before showing close dialog — if the app is force-killed
    // while the dialog is open, the autosave is still recoverable.
    if (trackManager && trackManager->isModified()) {
        Log::info("[Close] Emergency autosave before close dialog...");
        m_autoSaveManager.forceAutosave();
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
                     case Aestra::DialogResponse::DontSave: {
                         // User explicitly chose not to save — remove the autosave
                         // so it isn't offered for recovery on the next launch.
                         std::error_code ec;
                         std::filesystem::remove(getAutosavePath(), ec);
                         if (ec) {
                             Log::warning("[Close] Failed to remove autosave on Discard: " + ec.message());
                         }
                         m_running = false;
                         break;
                     }
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
        if (m_content) {
            m_content->setPluginTempo(static_cast<float>(result.tempo));
        }
        const double sampleRate = std::max(1.0, static_cast<double>(engine->getSampleRate()));
        engine->setGlobalSamplePos(static_cast<uint64_t>(std::max(0.0, result.playhead) * sampleRate));
    }

    if (m_content) {
        if (auto trackManager = m_content->getTrackManager()) {
            trackManager->setPosition(result.playhead);
            trackManager->setPlayStartPosition(result.playhead);
            trackManager->getCommandHistory().clear();
            trackManager->setModified(false);
        }

        if (auto transportBar = m_content->getTransportBar()) {
            transportBar->setTempo(static_cast<float>(result.tempo));
            transportBar->setPosition(result.playhead);
        }

        m_content->refreshProjectViews();
    }

    // Project load - use PlaybackGraphController if available, otherwise fallback
    if (m_audioController && m_audioController->getEngine() && m_content) {
        auto* controller = m_content->getPlaybackGraphController();
        if (controller) {
            controller->requestRebuild(Aestra::Audio::PlaybackGraphController::GraphDirtyReason::ProjectLoaded);
            controller->drainIfDirty(m_audioController->getSampleRate());
        } else if (m_content->getTrackManager()) {
            auto graph = AudioGraphBuilder::buildFromTrackManager(*m_content->getTrackManager());
            m_audioController->getEngine()->setGraph(graph);
            m_content->getTrackManager()->rebuildAndPushSnapshot();
        }
    }

    syncRecordingProjectPath(m_content, m_projectPath);
    reinitAutosaveManager();
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
    if (m_projectPath.empty()) {
        m_projectPath = getAutosavePath();
    }
    return saveProjectToPath(m_projectPath);
}

ProjectSerializer::SerializeResult AestraApp::serializeCurrentProject(int indentSpaces,
                                                                      const ProjectSerializer::UIState* uiState) const {
    ProjectSerializer::SerializeResult result;
    if (!m_content || !m_content->getTrackManager()) {
        return result;
    }

    const double tempo = (m_audioController && m_audioController->getEngine())
        ? static_cast<double>(m_audioController->getEngine()->getBPM())
        : (m_content->getTransportBar() ? static_cast<double>(m_content->getTransportBar()->getTempo()) : 120.0);
    const double playhead = m_content->getTrackManager()->getPosition();

    return ProjectSerializer::serialize(m_content->getTrackManager(), tempo, playhead, indentSpaces, uiState);
}

bool AestraApp::saveProjectToPath(const std::string& path) {
    if (!m_content || !m_content->getTrackManager()) {
        Log::warning("Cannot save project without an active TrackManager");
        return false;
    }

    if (path.empty()) {
        Log::warning("Cannot save project to empty path");
        return false;
    }

    const double tempo = (m_audioController && m_audioController->getEngine())
        ? static_cast<double>(m_audioController->getEngine()->getBPM())
        : (m_content->getTransportBar() ? static_cast<double>(m_content->getTransportBar()->getTempo()) : 120.0);
    const double playhead = m_content->getTrackManager()->getPosition();
    const auto uiState = captureUIState();

    const bool ok = ProjectSerializer::save(path,
                                            m_content->getTrackManager(),
                                            tempo,
                                            playhead,
                                            &uiState);
    if (ok && path == m_projectPath) {
        if (saveActiveTakeSnapshot(&uiState)) {
            m_content->getTrackManager()->setModified(false);
            m_autoSaveManager.markClean();
            syncRecordingProjectPath(m_content, m_projectPath);
            updateWindowTitle();
        } else {
            Log::warning("[Takes] Project saved but take snapshot failed, keeping dirty state");
            return false;
        }
    }
    return ok;
}

bool AestraApp::saveActiveTakeSnapshot(const ProjectSerializer::UIState* uiState) {
    if (m_projectPath.empty() || !m_content || !m_content->getTrackManager()) {
        return false;
    }

    ProjectSerializer::UIState capturedState;
    const ProjectSerializer::UIState* state = uiState;
    if (!state) {
        capturedState = captureUIState();
        state = &capturedState;
    }

    auto ser = serializeCurrentProject(2, state);
    if (!ser.ok || ser.contents.empty()) {
        Log::warning("[Takes] Could not serialize active Take");
        return false;
    }

    auto result = TakeManager::saveActiveTake(m_projectPath, ser.contents);
    if (!result.ok) {
        Log::warning("[Takes] Could not save active Take: " + result.errorMessage);
        return false;
    }
    return true;
}

bool AestraApp::createTakeFromCurrentProject() {
    if (m_projectPath.empty() || !m_content || !m_content->getTrackManager()) {
        return false;
    }

    if (!saveProject()) {
        return false;
    }

    const auto uiState = captureUIState();
    auto ser = serializeCurrentProject(2, &uiState);
    if (!ser.ok || ser.contents.empty()) {
        Log::warning("[Takes] Could not serialize project while creating Take");
        return false;
    }

    const auto manifest = TakeManager::loadManifest(m_projectPath);
    const std::string takeName = "Take " + std::to_string(manifest.ok ? manifest.takes.size() + 1 : 2);
    auto result = TakeManager::createTake(m_projectPath, ser.contents, takeName);
    if (!result.ok) {
        Log::warning("[Takes] Could not create Take: " + result.errorMessage);
        return false;
    }

    Log::info("[Takes] Active Take: " + result.take.name);
    return true;
}

ProjectSerializer::LoadResult AestraApp::switchToTake(const std::string& takeId) {
    ProjectSerializer::LoadResult result;
    if (takeId.empty()) {
        result.errorMessage = "Take id is empty";
        return result;
    }
    if (m_projectPath.empty()) {
        result.errorMessage = "Project path is empty";
        return result;
    }

    if (!saveProject()) {
        result.errorMessage = "Could not save the current Take before switching";
        return result;
    }

    const auto manifest = TakeManager::loadManifest(m_projectPath);
    if (!manifest.ok) {
        result.errorMessage = manifest.errorMessage;
        return result;
    }

    const auto* take = manifest.findTake(takeId);
    if (!take) {
        result.errorMessage = "Take not found: " + takeId;
        return result;
    }

    const std::string snapshotPath = TakeManager::resolveSnapshotPath(m_projectPath, *take);
    if (snapshotPath.empty() || !std::filesystem::exists(snapshotPath)) {
        result.errorMessage = "Take snapshot is missing: " + snapshotPath;
        return result;
    }

    result = loadProjectFromPath(snapshotPath, m_projectPath);
    if (!result.ok) {
        return result;
    }

    auto activeResult = TakeManager::setActiveTake(m_projectPath, takeId);
    if (!activeResult.ok) {
        auto rollback = loadProjectFromPath(m_projectPath, m_projectPath);
        if (!rollback.ok) {
            Log::error("[Takes] CRITICAL: Failed to rollback after failed Take switch: " + rollback.errorMessage);
        }
        result.ok = false;
        result.errorMessage = activeResult.errorMessage;
        return result;
    }

    if (!saveProject()) {
        auto rollback = loadProjectFromPath(m_projectPath, m_projectPath);
        if (!rollback.ok) {
            Log::error("[Takes] CRITICAL: Failed to rollback after failed mirror save: " + rollback.errorMessage);
        }
        result.ok = false;
        result.errorMessage = "Switched Take but could not mirror it to the canonical project file";
        return result;
    }

    Log::info("[Takes] Switched to Take: " + activeResult.take.name);
    return result;
}

void AestraApp::reinitAutosaveManager() {
    Aestra::Audio::AutosaveManager::Config config;
    config.enabled = m_autoSaveManager.isEnabled();
    config.autosaveInterval = std::chrono::seconds(60);
    config.autosavePathOverride = getAutosavePath();
    config.onAutosaveCommitted = [this](const std::string& autosavePath) -> bool {
        return writeRecoveryMarkerForAutosave(autosavePath);
    };
    config.serializer = [this](std::string& outData) -> bool {
        if (!m_content || !m_content->getTrackManager()) return false;
        const double tempo = (m_audioController && m_audioController->getEngine())
            ? static_cast<double>(m_audioController->getEngine()->getBPM())
            : (m_content->getTransportBar() ? static_cast<double>(m_content->getTransportBar()->getTempo()) : 120.0);
        const double playhead = m_content->getTrackManager()->getPosition();
        auto ser = ProjectSerializer::serialize(m_content->getTrackManager(), tempo, playhead, 0);
        if (!ser.ok) return false;
        outData = std::move(ser.contents);
        return true;
    };
    m_autoSaveManager.initialize(m_projectPath, std::move(config));
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
    auto* engine = m_audioController ? m_audioController->getEngine() : nullptr;
    if (!engine) {
        Log::warning("No audio engine available for export");
        return;
    }
    ensureSettingsAndDialogs();
    auto& trackMgr = *m_content->getTrackManager();
    auto exportDialog = m_windowManager->getExportDialog();
    if (exportDialog) {
        exportDialog->show(m_projectPath, *engine, trackMgr);
    }
}
