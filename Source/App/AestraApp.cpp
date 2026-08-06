// © 2025 Aestra Studios - All Rights Reserved. Licensed for personal & educational use only.
#include "AestraApp.h"
#include "MuseHostVerbs.h"
#include "MuseProjectLoadReport.h"
#include "AppLifecycle.h"
#include "CrashFlagPath.h"
#include "ServiceLocator.h"
#include "AestraRootComponent.h"
#include "Preferences.h"
#include "../AestraCore/include/AestraFile.h"
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
#include "../Panels/TakesPanel.h"
#include "../../AestraAudio/include/Core/PlaybackGraphController.h"
#include "../../AestraAudio/include/IO/AudioExporter.h"

#include <iostream>
#include <fstream>
#include <chrono>
#include "../Core/AudioSettingsStore.h"
#include "PlaylistMixer.h"
#include "ClipResampler.h"
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

    // Project document paths are initialized lazily after Platform is ready.
    m_windowManager = std::make_unique<AestraWindowManager>();
    m_audioController = std::make_unique<AestraAudioController>();
}

AestraApp::~AestraApp() {
    shutdown();
}

std::optional<std::string> AestraApp::getAppDataPath() {
    IPlatformUtils* utils = Platform::getUtils();
    if (!utils) {
        // Deliberately no fallback. This used to return the process working
        // directory, which is a perfectly valid-looking path that happens to
        // be the wrong one — callers could not tell, and neither could the
        // log. That is what made both #675 defects silent. There is no
        // portable-mode feature in the tree that wants a working-directory
        // app-data location, so failing is the honest answer (#676).
        Log::warning("[AppData] Platform utilities unavailable; no app-data path can be resolved");
        return std::nullopt;
    }
    std::string appDataDir = utils->getAppDataPath("Aestra");
    std::error_code ec;
    if (!std::filesystem::create_directories(appDataDir, ec) && ec) {
        // create_directories() returns false without setting ec when the
        // directory already exists, so a set ec is a genuine failure. A
        // directory we cannot create is one we cannot write to either.
        Log::warning("[AppData] Cannot create app-data directory " + appDataDir + ": " + ec.message());
        return std::nullopt;
    }
    return appDataDir;
}

namespace {
/// Join a file name onto the app-data directory, propagating "unresolved".
std::optional<std::string> appDataFile(const std::optional<std::string>& appDataDir, const char* fileName) {
    if (!appDataDir) {
        return std::nullopt;
    }
    return (std::filesystem::path(*appDataDir) / fileName).string();
}
} // namespace

std::optional<std::string> AestraApp::getAutosavePath() {
    return appDataFile(getAppDataPath(), "autosave.aes");
}

std::optional<std::string> AestraApp::getLegacyAutosavePath() {
    return appDataFile(getAppDataPath(), "autosave.Aestraproj");
}

std::optional<std::string> AestraApp::getCrashFlagPath() {
    return appDataFile(getAppDataPath(), "crash_flag");
}

std::string AestraApp::autosavePathOrEmpty() {
    const auto path = getAutosavePath();
    if (!path) {
        Log::warning("[Autosave] No app-data path could be resolved; autosave is disabled for this document");
        return {};
    }
    return *path;
}

std::optional<std::string> AestraApp::activeCrashFlagPath() {
    // Prefer the path resolved while platform utilities were alive. Resolving
    // fresh is only correct before priming; afterwards the platform may be
    // gone and there is no substitute path to offer.
    if (CrashFlagPath::isPrimed()) {
        return CrashFlagPath::get();
    }
    return getCrashFlagPath();
}

void AestraApp::writeCrashFlag() {
    // initialize() primes this immediately after Platform::initialize(). Prime
    // here only as a fallback for callers that reach the write without going
    // through startup — re-resolving unconditionally would let detection use
    // one path while the write and the clear use another (#675).
    if (!CrashFlagPath::isPrimed()) {
        const auto resolved = getCrashFlagPath();
        if (!resolved) {
            Log::warning("[CrashDetection] Cannot resolve crash flag path; no crash flag written this session");
            return;
        }
        CrashFlagPath::prime(*resolved);
    }
    const std::string flagPath = CrashFlagPath::get();
    std::ofstream out(flagPath, std::ios::trunc);
    if (out) {
        out << std::chrono::system_clock::now().time_since_epoch().count() << "\n";
        // The flag is a crash sentinel: without fsync a hard crash or power loss
        // right after startup can lose it to the OS cache — exactly the case
        // crash detection exists for (issue #284).
        if (!Aestra::syncOfstream(out, flagPath)) {
            // Best-effort: the unsynced flag stays on disk, but don't log success.
            Log::warning("[CrashDetection] Failed to sync crash flag: " + flagPath);
            out.close();
        } else {
            out.close();
            Log::info("[CrashDetection] Wrote crash flag: " + flagPath);
        }
    } else {
        Log::warning("[CrashDetection] Failed to write crash flag: " + flagPath);
    }
}

void AestraApp::clearCrashFlag() {
    // Use the path retained at startup. Resolving it here would go through
    // getAppDataPath(), which falls back to the working directory once
    // Platform::shutdown() has released the platform utilities — the flag would
    // then never be found and never be cleared (#675).
    //
    // Not primed means writeCrashFlag() never ran, so there is nothing this
    // process wrote to clear. Say so loudly rather than resolving a path that
    // is wrong by construction at this point in shutdown.
    if (!CrashFlagPath::isPrimed()) {
        Log::warning("[CrashDetection] Crash flag path was never resolved; nothing to clear");
        return;
    }
    const std::string& flagPath = CrashFlagPath::get();
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
    const auto flagPath = activeCrashFlagPath();
    if (!flagPath) {
        // Unresolvable is not the same as clean, but there is nothing to read
        // and no substitute worth inventing. Say so rather than reporting a
        // confident "no crash" derived from the wrong directory (#676).
        Log::warning("[CrashDetection] Cannot resolve crash flag path; treating this session as not crashed");
        return false;
    }
    std::error_code ec;
    return std::filesystem::exists(*flagPath, ec);
}

std::string AestraApp::getRecoveryMarkerPath(const std::string& autosavePath) {
    return autosavePath + ".recovery";
}

std::string AestraApp::readCrashFlagToken() {
    const auto flagPath = activeCrashFlagPath();
    if (!flagPath) {
        return {};
    }
    std::ifstream in(*flagPath, std::ios::binary);
    std::string token;
    std::getline(in, token);
    return token;
}

void AestraApp::writeRecoveryMarkerForAutosave(const std::string& autosavePath,
                                               const std::string& canonicalProjectPath) const {
    if (m_recoverySessionToken.empty()) {
        return;
    }

    std::ofstream out(getRecoveryMarkerPath(autosavePath), std::ios::binary | std::ios::trunc);
    if (!out) {
        Log::warning("[Recovery] Failed to write autosave recovery marker");
        return;
    }
    out << m_recoverySessionToken << "\n";
    out << canonicalProjectPath << "\n";
}

std::string AestraApp::readRecoveryOriginalProjectPath(const std::string& recoveryMarkerPath,
                                                       const std::string& expectedSessionToken) {
    if (expectedSessionToken.empty()) {
        return {};
    }

    std::ifstream marker(recoveryMarkerPath, std::ios::binary);
    std::string markerToken;
    if (!std::getline(marker, markerToken) || markerToken != expectedSessionToken) {
        return {};
    }

    std::string originalProjectPath;
    std::getline(marker, originalProjectPath);
    return originalProjectPath;
}

bool AestraApp::initialize(const std::string& projectPath) {
    StartupTimer totalTimer("Total startup");

    if (!transitionToInitializing()) return false;

    Log::info("Aestra v1.0.0 - Initializing...");

    {
        StartupTimer t("Platform init");
        if (!Platform::initialize()) {
            Log::error("Failed to initialize platform");
            return false;
        }
    }

    // Resolve the crash-flag path ONCE, now that platform utilities exist, and
    // reuse it for every crash-flag operation in this process (#675).
    //
    // Every one of those operations previously called getAppDataPath()
    // independently, and that function silently falls back to the process
    // working directory whenever platform utilities are absent. Detection used
    // to run BEFORE Platform::initialize() and clearing runs AFTER
    // Platform::shutdown(), so both sat in exactly that window: the flag was
    // written to app-data, then looked for in the working directory. Clean
    // exits never cleared it, and — worse — a real crash was never detected,
    // because the read missed the file too. Proven by launching with a flag in
    // the working directory only: recovery fired.
    //
    // Detection therefore moved below platform init. Nothing between the old
    // and new positions consumed the result.
    if (const auto crashFlagPath = getCrashFlagPath()) {
        CrashFlagPath::prime(*crashFlagPath);
    } else {
        Log::error("[CrashDetection] Cannot resolve crash flag path at startup; "
                   "crash detection and recovery are unavailable this session");
    }

    const bool crashedSession = isCrashedSession();
    m_previousRecoverySessionToken = crashedSession ? readCrashFlagToken() : "";

    // An app-data autosave is recovery state, never an implicit canonical project.
    m_documentState.startUntitled(autosavePathOrEmpty());

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

    startMuseSocketIfConfigured();

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
    // #655: the persisted state decides this. It used to be forced true inside
    // the window manager, so a window saved un-maximized still came back
    // maximized and the stored size was never visible.
    winConfig.startMaximized = uiState.maximized;

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
    m_content->setMidiInput(m_audioController->getMidiInput());
    syncRecordingProjectPath(m_content, m_documentState.canonicalPath());

    m_windowManager->setContent(m_content);
    m_audioController->setContent(m_content);

    // Forward project dirty state to autosave. TrackManager marks itself modified
    // from its own command history (see its constructor); this is the only hop the
    // application layer owns. It lives here, not in connectAudioToUI(), because a
    // failed audio device must not cost the user their unsaved-changes tracking.
    if (auto trackMgr = m_content->getTrackManager()) {
        trackMgr->setOnModified([this]() { m_autoSaveManager.markDirty(); });
    }

    wireTakesPanel();

    // Performance HUD is created eagerly: it must exist independently of the
    // lazily built settings dialogs, or F12 / View → Performance Stats are
    // silent no-ops until the user happens to open Settings or Export first.
    // The HUD itself is lightweight; the "heavy — deferred" rationale in
    // buildSettingsAndDialogs() applies to the dialogs, not to this.
    auto unifiedHUD = std::make_shared<UnifiedHUD>(m_windowManager->getAdaptiveFPS());
    unifiedHUD->setVisible(false);
    unifiedHUD->setAudioEngine(m_audioController->getEngine());
    m_windowManager->setUnifiedHUD(unifiedHUD);
}

std::chrono::seconds AestraApp::resolveAutosaveInterval() {
    // ONE place decides this. Three numbers used to describe the autosave
    // interval and disagreed: Preferences said 300, AutosaveManager's own
    // default said 30, and initializeAutosave hard-coded 60 — which won,
    // because the other two were never consulted.
    //
    // reinitAutosaveManager() hard-coded 60 a second time, so a user's stored
    // interval was silently discarded whenever the autosave manager was rebuilt
    // (project load, save-as). Both sites now resolve through here.
    //
    // Clamped because this is user-editable JSON on disk, and the corrected
    // value is written back so the file and the running config agree.
    const int persisted = Preferences::instance().autoSaveIntervalSeconds;
    const int seconds = std::clamp(persisted, 10, 3600);
    if (seconds != persisted) {
        Log::warning("[AutoSave] autoSaveIntervalSeconds " + std::to_string(persisted) +
                     " out of range; using " + std::to_string(seconds));
        Preferences::instance().autoSaveIntervalSeconds = seconds;
    }
    return std::chrono::seconds(seconds);
}

void AestraApp::initializeAutosave(bool enabled) {
    Aestra::Audio::AutosaveManager::Config config;
    config.enabled = enabled;
    // Three numbers used to describe this interval and disagreed: Preferences
    // said 300, AutosaveManager's own default said 30, and this line hard-coded
    // 60 — which won, because the other two were never consulted. Preferences is
    // the persisted authority; clamped because it is user-editable JSON on disk.
    config.autosaveInterval = resolveAutosaveInterval();
    config.captureSnapshotOnCallingThread = true;
    config.autosavePathOverride = m_documentState.autosavePath();
    const std::string canonicalProjectPath = m_documentState.canonicalPath();
    config.onAutosaveCommitted = [this, canonicalProjectPath](const std::string& autosavePath) {
        writeRecoveryMarkerForAutosave(autosavePath, canonicalProjectPath);
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
    m_autoSaveManager.initialize(m_documentState.canonicalPath(), std::move(config));
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
        // Persist as well as apply. Startup reads Preferences::autoSaveEnabled
        // (see run()), but nothing ever wrote it back, so turning autosave off
        // held for the session and silently came back on at the next launch.
        // Preferences::save() runs at shutdown, so recording it here is enough.
        Preferences::instance().autoSaveEnabled = enabled;
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
         // Stream is restarting: let the RT-scheduling state be re-reported
         // for the new stream (the audio thread re-verifies on first callback).
         m_rtStateLogged = false;
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

    // Performance HUD is created in initializeContent() — it must not be tied
    // to this lazily built path (see note there).
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
            clearProjectLoadReport();
            m_documentState.startUntitled(autosavePathOrEmpty());
            reinitAutosaveManager();
            syncRecordingProjectPath(m_content, m_documentState.canonicalPath());
            m_lastWindowTitle.clear();
            updateWindowTitle();
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

        menu->addItem("Save As...", [this]() { saveProjectAs(); });

        menu->addSeparator();

        // Takes = "which version of the work do I want?" — opens the Takes
        // workspace panel (create/name/open/duplicate/branch project versions).
        menu->addItem("Takes", [this]() {
            if (m_content) m_content->toggleTakesPanel();
        });

        // History = "what actions did I perform?" — opens the command-timeline
        // panel wired to the undo/redo system.
        menu->addItem("History  Ctrl+H", [this]() {
            if (m_content) m_content->toggleHistoryPanel();
        });

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
        menu->addItem("Show Takes", [this]() {
            if (m_content) m_content->toggleTakesPanel();
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
        if (m_content) {
            // AestraContent is constructed before PluginManager loads its cache.
            // Refresh now so cached third-party plugins are visible immediately
            // instead of leaving the browser on its built-in-only bootstrap list.
            m_content->refreshPluginList();
        }
    }
}

void AestraApp::loadOrRecoverProject(const std::string& projectPath, bool crashedSession) {
    const bool hasRequestedProject = !projectPath.empty() && std::filesystem::exists(projectPath);
    const std::string autosavePath = m_documentState.autosavePath();
    std::string timestamp;
    const std::string recoveryMarkerPath = getRecoveryMarkerPath(autosavePath);
    std::vector<std::string> recoveryCandidates;
    const bool primaryAutosaveDetected =
        crashedSession && !m_previousRecoverySessionToken.empty() &&
        RecoveryDialog::detectAutosave(autosavePath, recoveryMarkerPath, m_previousRecoverySessionToken, timestamp);
    if (primaryAutosaveDetected) {
        recoveryCandidates.push_back(autosavePath);
    }
    if (crashedSession && !m_previousRecoverySessionToken.empty()) {
        for (auto& backup : Aestra::Audio::AutosaveManager::listBackupsForAutosavePath(autosavePath)) {
            recoveryCandidates.push_back(std::move(backup));
        }
    }

    if (!recoveryCandidates.empty()) {
        const std::string recoveryDisplayPath = recoveryCandidates.front();
        Log::info("[Recovery] Crash detected, showing recovery dialog");
        m_pendingAutosavePath = recoveryDisplayPath;
        m_recoveryHandled = false;

        if (auto recoveryDialog = m_windowManager->getRecoveryDialog()) {
            auto alive = m_aliveToken;
            const std::string originalProjectPath =
                readRecoveryOriginalProjectPath(recoveryMarkerPath, m_previousRecoverySessionToken);
            recoveryDialog->show(recoveryDisplayPath,
                                 [this, alive, autosavePath, projectPath, hasRequestedProject, originalProjectPath,
                                  recoveryCandidates](Aestra::RecoveryResponse response) {
                if (!*alive)
                    return;
                m_recoveryHandled = true;
                m_pendingAutosavePath.clear();
                if (response == Aestra::RecoveryResponse::Recover) {
                    beginProjectLoadAttempt();
                    auto selected = ProjectSerializer::loadFirstValid(
                        recoveryCandidates, m_content ? m_content->getTrackManager() : nullptr, originalProjectPath);
                    auto result = std::move(selected.result);
                    recordProjectLoadAttempt(result, ProjectLoadSource::Recovery);
                    if (result.ok) {
                        result = applyLoadedProject(selected.loadedPath, ProjectLoadSource::Recovery,
                                                    originalProjectPath, std::move(result));
                        if (m_content && m_content->getTrackManager()) {
                            m_content->getTrackManager()->markModified();
                        }
                        m_autoSaveManager.markDirty();
                        updateWindowTitle();
                        Log::info("[Recovery] Autosave recovered successfully from: " + selected.loadedPath);
                    } else {
                        Log::error("[Recovery] Failed to load autosave");
                        if (m_content)
                            m_content->resetToDefaultProject();
                        clearProjectLoadReport();
                        m_documentState.startUntitled(autosavePathOrEmpty());
                        reinitAutosaveManager();
                        syncRecordingProjectPath(m_content, m_documentState.canonicalPath());
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
                    if (hasRequestedProject) {
                        auto result = loadProjectFromPath(projectPath);
                        if (!result.ok) {
                            Log::error("Failed to load requested project after discarding recovery: " + projectPath);
                        }
                    } else {
                        if (m_content)
                            m_content->resetToDefaultProject();
                        clearProjectLoadReport();
                        m_documentState.startUntitled(autosavePathOrEmpty());
                        reinitAutosaveManager();
                        syncRecordingProjectPath(m_content, m_documentState.canonicalPath());
                        updateWindowTitle();
                    }
                }
            });
            Log::info("[Recovery] Showing recovery dialog for autosave");
        } else {
            Log::warning("[Recovery] RecoveryDialog not available — discarding autosave");
            std::error_code ec;
            std::filesystem::remove(autosavePath, ec);
            std::filesystem::remove(recoveryMarkerPath, ec);
            m_recoveryHandled = true;
            if (hasRequestedProject) {
                loadProjectFromPath(projectPath);
            } else {
                clearProjectLoadReport();
            }
        }
        return;
    }

    m_recoveryHandled = true;
    if (hasRequestedProject) {
        auto result = loadProjectFromPath(projectPath);
        if (!result.ok) {
            Log::error("Failed to load requested project: " + projectPath + " (" + result.errorMessage + ")");
        }
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
            // Dirty tracking (command history -> markModified -> autosave) is wired
            // in initializeContent(); it must not depend on the audio device coming up.
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
                if (auto trackManager = m_content->getTrackManager()) {
                    trackManager->getPlaylistModel().setBPM(bpm);
                    trackManager->getTimelineClock().setTempo(bpm);
                    trackManager->getPatternPlaybackEngine().rewindScheduledInstances();
                }
                if (auto trackManagerUI = m_content->getTrackManagerUI()) {
                    trackManagerUI->invalidateCache();
                }
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
        // Establish the effective DSP configuration BEFORE the stream starts
        // producing callbacks. This is not merely tidier ordering: setThreadCount
        // replaces the engine's thread pool outright
        // (m_threadPool = std::make_unique<RealTimeThreadPool>(count)), so running
        // it against a live stream races the audio thread that is using that pool.
        applyPersistedEngineSettings();

        m_audioController->startStream();
        if (m_content) {
            m_content->setAudioStatus(true);
        }
        connectAudioToUI(); // Sync configs now that stream is open
        m_audioStreamReady = true;

        // Re-wire the performance HUD in case the engine wasn't constructed
        // yet when the HUD was created in initializeContent() (e.g. audio init
        // failed at startup and recovered here). Idempotent when already set.
        if (auto* hud = m_windowManager->getUnifiedHUD()) {
            hud->setAudioEngine(m_audioController->getEngine());
        }
    }
}

// Apply the persisted DSP configuration once the engine exists (#649).
//
// These settings used to be applied by AudioSettingsPage's constructor, reached
// lazily through ensureSettingsAndDialogs() — so whether the engine ran with the
// user's safety-limiter, dither and multi-threading choices depended on whether
// they had opened the Settings dialog this session. The safety limiter in
// particular defaults to ON (AudioEngine.h), so a user who turned it off got it
// back every launch and lost it again mid-session on opening Settings.
//
// Startup owns this now. The Settings dialog edits and displays; it does not
// initialise.
void AestraApp::applyPersistedEngineSettings() {
    auto* engine = m_audioController ? m_audioController->getEngine() : nullptr;
    if (!engine) {
        return;
    }

    const Aestra::AudioSettings saved = Aestra::loadAudioSettings();

    if (Aestra::isSet(saved.masterLimiter)) {
        engine->setSafetyLimiterEnabled(saved.masterLimiter != 0);
    }
    if (Aestra::isSet(saved.dcRemoval)) {
        engine->setDCRemovalEnabled(saved.dcRemoval != 0);
    }
    if (Aestra::isSet(saved.dithering)) {
        if (Aestra::isPlausibleDitheringMode(saved.dithering)) {
            engine->setDitheringMode(static_cast<Aestra::Audio::DitheringMode>(saved.dithering));
        } else {
            Log::warning("[Audio] dithering " + std::to_string(saved.dithering) +
                         " is not a valid mode; leaving the engine default");
        }
    }
    if (Aestra::isSet(saved.previewDuckingDb)) {
        engine->setPreviewDuckingAttenuationDb(static_cast<float>(saved.previewDuckingDb));
    }
    if (Aestra::isSet(saved.resampling) && !Aestra::isPlausibleResamplingIndex(saved.resampling)) {
        Log::warning("[Audio] resampling " + std::to_string(saved.resampling) +
                     " is not a valid quality index; leaving the engine default");
    } else if (Aestra::isSet(saved.resampling)) {
        // Index-to-quality mapping mirrors AudioSettingsPage::applyChanges. It is
        // duplicated rather than shared only because the page cannot be linked
        // headlessly; unifying it belongs with the page rewire.
        using Aestra::Audio::Interpolators::InterpolationQuality;
        InterpolationQuality engineQ = InterpolationQuality::Sinc16;
        Aestra::Audio::ClipResamplingQuality globalQ = Aestra::Audio::ClipResamplingQuality::High;
        switch (saved.resampling) {
            case 0: engineQ = InterpolationQuality::Cubic;  globalQ = Aestra::Audio::ClipResamplingQuality::Fast;     break;
            case 1: engineQ = InterpolationQuality::Sinc32; globalQ = Aestra::Audio::ClipResamplingQuality::Draft;    break;
            case 2: engineQ = InterpolationQuality::Cubic;  globalQ = Aestra::Audio::ClipResamplingQuality::Standard; break;
            case 3: engineQ = InterpolationQuality::Sinc64; globalQ = Aestra::Audio::ClipResamplingQuality::High;     break;
            default: break; // unreachable: guarded by isPlausibleResamplingIndex above
        }
        engine->setInterpolationQuality(engineQ);
        Aestra::Audio::PlaylistMixer::setResamplingQuality(globalQ);
    }
    if (Aestra::isSet(saved.multiThreading)) {
        const bool enabled = saved.multiThreading != 0;
        engine->setMultiThreadingEnabled(enabled);
        if (enabled && Aestra::isSet(saved.threads)) {
            const unsigned hw = std::thread::hardware_concurrency();
            const int maxThreads = hw > 0 ? static_cast<int>(hw) : 8;
            const int clamped = std::max(1, std::min(saved.threads, maxThreads));
            if (clamped != saved.threads) {
                Log::warning("[Audio] threads " + std::to_string(saved.threads) + " out of range; using " +
                             std::to_string(clamped));
            }
            engine->setThreadCount(clamped);
        }
    }

    Log::info("[Audio] Applied persisted engine settings at startup");
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

                // Playback needs the 60 FPS target for smooth playhead/meters;
                // when stopped this decays back to the idle target via the
                // governor's timeout.
                if (auto* fps = m_windowManager->getAdaptiveFPS()) {
                    fps->setAudioVisualizationActive(tm->isPlaying());
                }
            }

            // Rebuild graph check - uses PlaybackGraphController for canonical drain
            if (m_audioController->getEngine() && m_content) {
                auto* controller = m_content->getPlaybackGraphController();
                if (controller) {
                    controller->drainIfDirty(m_audioController->getSampleRate());
                }
            }

            // Muse socket: execute agent requests on the main thread, exactly
            // like user edits (shared undo history, shared command system).
            if (m_museSocketServer && m_museService) {
                if (m_museSocketServer->processPending(*m_museService) > 0) {
                    // Defeat idle frame elision so agent edits show up.
                    if (auto* root = m_windowManager->getRootComponent()) {
                        root->setDirty(true);
                    }
                }
            }
        }

        // ---- Idle frame elision (labs/perf/idle-frame-elision-spec.md) ----
        // Events and updates above always run at full cadence (input latency
        // is untouched); only the render+swap work is elided when nothing can
        // have changed on screen. A ~3 fps heartbeat keeps the window alive.
        const bool presentThisFrame = shouldRenderThisFrame();

        if (presentThisFrame) {
            AESTRA_ZONE("Render_Prep");
            // Consume the pending-invalidation bit at render START: everything
            // dirty up to this point is included in this frame; anything that
            // dirties DURING render survives and triggers the next frame.
            // (Clearing after present would eat mid-render invalidations; the
            // skip path never clears.)
            if (auto* root = m_windowManager->getRootComponent()) {
                root->setDirty(false);
            }
            m_windowManager->render();
        }

        // Capture the mutable project graph only on its owning application
        // thread. AutosaveManager commits the resulting immutable string on its
        // worker, so active edits never race ProjectSerializer traversal.
        m_autoSaveManager.captureSnapshotIfDue();

        // Non-realtime audio work (graph rebuilds, plugin state commits) should
        // happen before frame pacing so it doesn't eat into the sleep budget.
        if (m_audioController && m_audioController->getEngine()) {
            m_audioController->getEngine()->performNonRealtimeMaintenance();
        }

        double sleepTime = m_windowManager->endFrame();
        if (presentThisFrame) {
            m_windowManager->swapBuffers();
            m_lastPresentedFrame = std::chrono::steady_clock::now();
        }
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

bool AestraApp::shouldRenderThisFrame() {
    // Conservative v1 gate: render unless EVERY skip condition holds
    // (dirty == false && realtime_visuals == false && input_recent == false).

    // Pending invalidation — any component's setDirty(true) propagates here.
    auto* root = m_windowManager->getRootComponent();
    if (!root || root->isDirty())
        return true;

    // Input recency — the adaptive FPS governor already tracks this (fed by
    // mouse/key callbacks); align with its idle timeout.
    auto* fps = m_windowManager->getAdaptiveFPS();
    if (!fps || fps->getIdleTime() < 2.0)
        return true;

    // Realtime visuals — transport (playhead/meters), record-arm input
    // monitoring, file preview, and Audition playback.
    if (m_audioController && m_audioController->getEngine() && m_audioController->getEngine()->isTransportPlaying()) {
        return true;
    }
    if (m_content) {
        if (auto tm = m_content->getTrackManager()) {
            if (tm->isPlaying() || tm->isRecordArmed())
                return true;
        }
        if (m_content->hasRealtimePlaybackVisuals())
            return true;
    }

    // Overlays that animate or need fresh samples (HUD, dialogs, menus).
    if (m_windowManager->requiresContinuousRender())
        return true;

    // Deeply idle: heartbeat only (~3.3 fps) so caret blink, tooltips and any
    // unsignaled change still surface within ~300 ms.
    const auto now = std::chrono::steady_clock::now();
    const double sinceLastPresent = std::chrono::duration<double>(now - m_lastPresentedFrame).count();
    return sinceLastPresent >= 0.3;
}

void AestraApp::startMuseSocketIfConfigured() {
    const char* portEnv = std::getenv("AESTRA_MUSE_PORT");
    if (!portEnv || !*portEnv) return;

    if (!m_content || !m_content->getTrackManager() || !m_audioController ||
        !m_audioController->getEngine()) {
        Log::warning("[MuseSocket] AESTRA_MUSE_PORT set but session not ready; not starting");
        return;
    }

    int port = 0;
    try {
        port = std::stoi(portEnv);
    } catch (const std::exception&) {
        port = -1;
    }
    if (port < 0 || port > 65535) {
        Log::warning(std::string("[MuseSocket] invalid AESTRA_MUSE_PORT: ") + portEnv);
        return;
    }

    m_museService = std::make_unique<Aestra::Audio::MuseService>(
        m_content->getTrackManager().get(), m_audioController->getEngine());
    if (m_projectLoadReport) {
        m_museService->setProjectLoadReport(*m_projectLoadReport);
    }

    // Requests are executed from the frame pump (see processPending in UI_Update),
    // so UI-affine host verbs can be honoured here. Headless processes leave this
    // false and refuse them with a reason rather than running host code on a
    // thread that does not own the state it touches.
    m_museService->setHostUiThreadAvailable(true);
    registerMuseHostVerbs(*m_museService, *m_content);

    m_museSocketServer = std::make_unique<Aestra::Audio::MuseSocketServer>();
    std::string error;
    if (!m_museSocketServer->start(static_cast<uint16_t>(port), error)) {
        Log::warning("[MuseSocket] failed to start: " + error);
        m_museSocketServer.reset();
        m_museService.reset();
    }
}

void AestraApp::shutdown() {
    Log::info("[SHUTDOWN] Entering shutdown function...");
    Aestra::AppLifecycle::instance().transitionTo(Aestra::AppState::ShuttingDown);

    // Invalidate lifetime token so any async callbacks that fire during teardown
    // bail out before touching partially-destroyed members.
    if (m_aliveToken) *m_aliveToken = false;

    // Stop the Muse socket before anything it references tears down.
    if (m_museSocketServer) m_museSocketServer->stop();

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

    if (m_audioController) {
        m_audioController->stopStream();
        m_audioController->closeStream();
    }

    // AestraContent owns UI-facing preview state and detaches it from AudioEngine
    // in its destructor. Destroy content while the engine still exists.
    if (m_windowManager) {
        m_windowManager->shutdown();
    }
    m_content.reset();

    Aestra::Audio::PluginManager::getInstance().shutdown();

    if (m_audioController) {
        m_audioController->shutdown();
    }

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
    if (m_pendingClose) {
        if (m_windowManager) {
            auto dialog = m_windowManager->getConfirmationDialog();
            if (dialog && dialog->isDialogVisible()) {
                if (auto* root = m_windowManager->getRootComponent()) {
                    dialog->setBounds(root->getBounds());
                    root->removeChild(dialog);
                    root->addChild(dialog);
                }
                return;
            }
        }
        m_pendingClose = false;
    }

    auto trackManager = m_content ? m_content->getTrackManager() : nullptr;
    if (!trackManager || !trackManager->isModified()) {
        m_running = false;
        return;
    }

    m_pendingClose = true;

    // Emergency autosave before showing close dialog — if the app is force-killed
    // while the dialog is open, the autosave is still recoverable.
    if (trackManager && trackManager->isModified()) {
        Log::info("[Close] Emergency autosave before close dialog...");
        m_autoSaveManager.forceAutosave();
    }

    if (!m_windowManager) {
        m_pendingClose = false;
        return;
    }

    auto dialog = m_windowManager->getConfirmationDialog();
    if (!dialog) {
        dialog = std::make_shared<ConfirmationDialog>();
        m_windowManager->setConfirmationDialog(dialog);
    }

    if (auto* root = m_windowManager->getRootComponent()) {
        dialog->setBounds(root->getBounds());
        root->removeChild(dialog);
        root->addChild(dialog);
    }

    dialog->show("Unsaved Changes",
                 "You have unsaved changes. Save before closing?",
                 [this](Aestra::DialogResponse response) {
                     switch (response) {
                     case Aestra::DialogResponse::Save:
                         if (saveCurrentProject()) {
                             m_running = false;
                         } else {
                             m_pendingClose = false;
                         }
                         break;
                     case Aestra::DialogResponse::DontSave: {
                         // User explicitly chose not to save — remove the autosave
                         // so it isn't offered for recovery on the next launch.
                         if (const auto autosavePath = getAutosavePath()) {
                             std::error_code ec;
                             std::filesystem::remove(*autosavePath, ec);
                             if (ec) {
                                 Log::warning("[Close] Failed to remove autosave on Discard: " + ec.message());
                             }
                         } else {
                             // Never delete based on a guessed path.
                             Log::warning("[Close] Cannot resolve autosave path; nothing removed on Discard");
                         }
                         m_running = false;
                         break;
                     }
                     case Aestra::DialogResponse::Cancel:
                     case Aestra::DialogResponse::None:
                     default:
                         m_pendingClose = false;
                         break;
                     }
                 });
}

bool AestraApp::saveCurrentProject() {
    return m_documentState.requiresSaveAs() ? saveProjectAs() : saveProject();
}

bool AestraApp::saveProjectAs() {
    auto* utils = Aestra::Platform::getUtils();
    if (!utils) {
        Log::warning("Cannot save project: platform file dialog is unavailable");
        return false;
    }

    Aestra::IPlatformUtils::SaveFileDialogOptions options;
    options.title = "Save Project As";
    options.filter =
        std::string("Aestra Project\0*.aes\0All Files\0*.*\0", sizeof("Aestra Project\0*.aes\0All Files\0*.*\0") - 1);
    options.defaultPath = m_documentState.canonicalPath();
    options.defaultExtension = "aes";
    const std::string pickedPath = utils->saveFileDialog(options);
    if (pickedPath.empty()) {
        return false;
    }

    const bool ok = saveProjectToPath(pickedPath, true);
    if (ok) {
        Log::info("Project saved as: " + pickedPath);
    }
    return ok;
}

ProjectSerializer::LoadResult AestraApp::loadProjectFromPath(const std::string& path, ProjectLoadSource source,
                                                             const std::string& canonicalPath) {
    beginProjectLoadAttempt();
    ProjectSerializer::LoadResult result;
    if (path.empty()) {
        result.errorMessage = "Project path is empty";
        recordProjectLoadAttempt(result, source);
        return result;
    }

    const std::string assetBasePath =
        source == ProjectLoadSource::Canonical || canonicalPath.empty() ? path : canonicalPath;
    result = ProjectSerializer::load(path, m_content ? m_content->getTrackManager() : nullptr, assetBasePath);
    recordProjectLoadAttempt(result, source);
    if (!result.ok) {
        Log::error("Failed to load project: " + path + " (" + result.errorMessage + ")");
        return result;
    }

    return applyLoadedProject(path, source, canonicalPath, std::move(result));
}

void AestraApp::beginProjectLoadAttempt() {
    m_projectLoadReport.reset();
    if (m_museService) m_museService->clearProjectLoadReport();
}

void AestraApp::recordProjectLoadAttempt(const ProjectSerializer::LoadResult& result,
                                        ProjectLoadSource source) {
    Aestra::MuseProjectLoadOrigin origin = Aestra::MuseProjectLoadOrigin::Canonical;
    switch (source) {
    case ProjectLoadSource::Canonical:
        break;
    case ProjectLoadSource::Recovery:
        origin = Aestra::MuseProjectLoadOrigin::Recovery;
        break;
    case ProjectLoadSource::Snapshot:
        origin = Aestra::MuseProjectLoadOrigin::Snapshot;
        break;
    }

    m_projectLoadReport = Aestra::makeMuseProjectLoadReport(result, origin);
    if (m_museService) m_museService->setProjectLoadReport(*m_projectLoadReport);
}

void AestraApp::clearProjectLoadReport() {
    beginProjectLoadAttempt();
}

ProjectSerializer::LoadResult AestraApp::applyLoadedProject(const std::string& path, ProjectLoadSource source,
                                                            const std::string& canonicalPath,
                                                            ProjectSerializer::LoadResult result) {

    // Snapshot callers may pass the current canonical path by reference.
    // Resolve it before mutating the document state.
    const std::string resolvedCanonicalPath =
        canonicalPath.empty() ? m_documentState.canonicalPath() : canonicalPath;

    switch (source) {
    case ProjectLoadSource::Canonical:
        m_documentState.openCanonical(path);
        break;
    case ProjectLoadSource::Recovery:
        m_documentState.recoverFrom(path, resolvedCanonicalPath);
        break;
    case ProjectLoadSource::Snapshot:
        m_documentState.restoreSnapshot(path, resolvedCanonicalPath);
        break;
    }
    if (result.integrity == ProjectSerializer::LoadIntegrity::Mismatch) {
        m_documentState.protectCanonicalFromOverwrite();
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
            trackManager->getPlaylistModel().setBPM(result.tempo);
            trackManager->getTimelineClock().setTempo(result.tempo);
            trackManager->getPatternPlaybackEngine().rewindScheduledInstances();
            trackManager->setPosition(result.playhead);
            trackManager->setPlayStartPosition(result.playhead);
            trackManager->getCommandHistory().clear();
            // Only an in-memory transformation requires a save. Merely
            // advancing an older no-op schema stamp must not create a false
            // dirty prompt (#662).
            trackManager->setModified(result.requiresSaveAfterLoad());
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

    syncRecordingProjectPath(m_content, m_documentState.canonicalPath());
    reinitAutosaveManager();
    if (result.ui) {
        applyUIState(*result.ui);
    }
    // Loading may change the project (and its takes manifest) out from under
    // an open Takes panel — re-pull the providers.
    if (m_content) {
        if (auto takesPanel = m_content->getTakesPanel(); takesPanel && takesPanel->isVisible()) {
            takesPanel->refreshTakes();
        }
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

    if (m_documentState.canonicalPath().empty()) {
        result.errorMessage = "Project path is empty";
        return result;
    }

    return loadProjectFromPath(m_documentState.canonicalPath());
}

bool AestraApp::saveProject() {
    if (m_documentState.requiresSaveAs()) {
        Log::warning("Cannot use Save without a canonical project path");
        return false;
    }
    return saveProjectToPath(m_documentState.canonicalPath());
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

bool AestraApp::saveProjectToPath(const std::string& path, bool establishCanonical) {
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
    if (ok && establishCanonical) {
        m_documentState.adoptCanonical(path);
        reinitAutosaveManager();
    }

    if (ok && path == m_documentState.canonicalPath()) {
        m_documentState.adoptCanonical(path);
        if (saveActiveTakeSnapshot(&uiState)) {
            m_content->getTrackManager()->setModified(false);
            m_autoSaveManager.markClean();
            syncRecordingProjectPath(m_content, m_documentState.canonicalPath());
            updateWindowTitle();
        } else {
            Log::warning("[Takes] Project saved but take snapshot failed, keeping dirty state");
            return false;
        }
    }
    // A save updates the active take's timestamp — keep an open Takes panel honest.
    if (ok) {
        if (auto takesPanel = m_content->getTakesPanel(); takesPanel && takesPanel->isVisible()) {
            takesPanel->refreshTakes();
        }
    }
    return ok;
}

bool AestraApp::saveActiveTakeSnapshot(const ProjectSerializer::UIState* uiState) {
    if (m_documentState.canonicalPath().empty() || !m_content || !m_content->getTrackManager()) {
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

    auto result = TakeManager::saveActiveTake(m_documentState.canonicalPath(), ser.contents);
    if (!result.ok) {
        Log::warning("[Takes] Could not save active Take: " + result.errorMessage);
        return false;
    }
    return true;
}

bool AestraApp::createTakeFromCurrentProject() {
    if (m_documentState.canonicalPath().empty() || !m_content || !m_content->getTrackManager()) {
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

    const auto manifest = TakeManager::loadManifest(m_documentState.canonicalPath());
    const std::string takeName = "Take " + std::to_string(manifest.ok ? manifest.takes.size() + 1 : 2);
    auto result = TakeManager::createTake(m_documentState.canonicalPath(), ser.contents, takeName);
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
    if (m_documentState.canonicalPath().empty()) {
        result.errorMessage = "Project path is empty";
        return result;
    }

    if (!saveProject()) {
        result.errorMessage = "Could not save the current Take before switching";
        return result;
    }

    const auto manifest = TakeManager::loadManifest(m_documentState.canonicalPath());
    if (!manifest.ok) {
        result.errorMessage = manifest.errorMessage;
        return result;
    }

    const auto* take = manifest.findTake(takeId);
    if (!take) {
        result.errorMessage = "Take not found: " + takeId;
        return result;
    }

    const std::string snapshotPath = TakeManager::resolveSnapshotPath(m_documentState.canonicalPath(), *take);
    if (snapshotPath.empty() || !std::filesystem::exists(snapshotPath)) {
        result.errorMessage = "Take snapshot is missing: " + snapshotPath;
        return result;
    }

    result = loadProjectFromPath(snapshotPath, ProjectLoadSource::Snapshot, m_documentState.canonicalPath());
    if (!result.ok) {
        return result;
    }

    auto activeResult = TakeManager::setActiveTake(m_documentState.canonicalPath(), takeId);
    if (!activeResult.ok) {
        auto rollback = loadProjectFromPath(m_documentState.canonicalPath());
        if (!rollback.ok) {
            Log::error("[Takes] CRITICAL: Failed to rollback after failed Take switch: " + rollback.errorMessage);
        }
        result.ok = false;
        result.errorMessage = activeResult.errorMessage;
        return result;
    }

    if (!saveProject()) {
        auto rollback = loadProjectFromPath(m_documentState.canonicalPath());
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

bool AestraApp::branchFromTake(const std::string& takeId) {
    if (m_documentState.canonicalPath().empty() || takeId.empty()) {
        return false;
    }

    // Preserve the working state before anything else touches the manifest.
    if (!saveProject()) {
        Log::warning("[Takes] Could not save the current Take before branching");
        return false;
    }

    const auto manifest = TakeManager::loadManifest(m_documentState.canonicalPath());
    const auto* source = manifest.ok ? manifest.findTake(takeId) : nullptr;
    const std::string branchName = source ? (source->name + " Branch") : "";

    auto dup = TakeManager::duplicateTake(m_documentState.canonicalPath(), takeId, branchName);
    if (!dup.ok) {
        Log::warning("[Takes] Could not branch from Take: " + dup.errorMessage);
        return false;
    }

    auto result = switchToTake(dup.take.id);
    if (!result.ok) {
        // Don't leave a half-made branch behind: the duplicate is not active
        // (the failed switch rolled back), so it can be deleted safely.
        auto rollback = TakeManager::deleteTake(m_documentState.canonicalPath(), dup.take.id);
        Log::error("[Takes] Branch created but could not switch to it: " + result.errorMessage +
                   (rollback.ok ? " (branch rolled back)" : " (rollback also failed: " + rollback.errorMessage + ")"));
        return false;
    }
    return true;
}

void AestraApp::wireTakesPanel() {
    if (!m_content) {
        return;
    }
    auto takesPanel = m_content->getTakesPanel();
    if (!takesPanel) {
        return;
    }

    takesPanel->setTakesProvider([this]() -> TakeManager::Manifest {
        if (m_documentState.canonicalPath().empty()) {
            TakeManager::Manifest manifest;
            manifest.errorMessage = "No Takes manifest";
            return manifest;
        }
        return TakeManager::loadManifest(m_documentState.canonicalPath());
    });

    takesPanel->setSnapshotsProvider([this]() {
        std::vector<Aestra::Audio::TakesPanel::SnapshotEntry> entries;
        if (m_documentState.canonicalPath().empty()) {
            return entries;
        }
        for (const auto& entry : ProjectSerializer::listHistory(m_documentState.canonicalPath())) {
            entries.push_back({entry.path, entry.label});
        }
        return entries;
    });

    takesPanel->setOnCreateTake([this]() { return createTakeFromCurrentProject(); });
    takesPanel->setOnSaveActiveTake([this]() { return saveProject(); });

    takesPanel->setOnOpenTake([this](const std::string& takeId) {
        auto result = switchToTake(takeId);
        if (!result.ok) {
            Log::error("[Takes] Failed to open Take: " + takeId + " (" + result.errorMessage + ")");
        }
        return result.ok;
    });

    takesPanel->setOnRenameTake([this](const std::string& takeId, const std::string& name) {
        if (m_documentState.canonicalPath().empty()) {
            return false;
        }
        auto result = TakeManager::renameTake(m_documentState.canonicalPath(), takeId, name);
        if (!result.ok) {
            Log::warning("[Takes] Could not rename Take: " + result.errorMessage);
        }
        return result.ok;
    });

    takesPanel->setOnDuplicateTake([this](const std::string& takeId) {
        if (m_documentState.canonicalPath().empty()) {
            return false;
        }
        auto result = TakeManager::duplicateTake(m_documentState.canonicalPath(), takeId);
        if (!result.ok) {
            Log::warning("[Takes] Could not duplicate Take: " + result.errorMessage);
        }
        return result.ok;
    });

    takesPanel->setOnDeleteTake([this](const std::string& takeId) {
        if (m_documentState.canonicalPath().empty()) {
            return false;
        }
        auto result = TakeManager::deleteTake(m_documentState.canonicalPath(), takeId);
        if (!result.ok) {
            Log::warning("[Takes] Could not delete Take: " + result.errorMessage);
        }
        return result.ok;
    });

    takesPanel->setOnBranchTake([this](const std::string& takeId) { return branchFromTake(takeId); });

    takesPanel->setOnRestoreSnapshot([this](const std::string& path) {
        // Save the current take first so a snapshot restore never silently
        // destroys the working state.
        if (!saveProject()) {
            Log::warning("[Takes] Could not save the current Take before restoring a snapshot");
            return false;
        }
        auto result = loadProjectFromPath(path, ProjectLoadSource::Snapshot, m_documentState.canonicalPath());
        if (!result.ok) {
            Log::error("Failed to restore snapshot: " + path + " (" + result.errorMessage + ")");
            return false;
        }
        // The project file still holds the pre-restore save; the restored
        // state is unsaved work until the user commits it.
        if (m_content && m_content->getTrackManager()) {
            m_content->getTrackManager()->markModified();
        }
        return true;
    });
}

void AestraApp::reinitAutosaveManager() {
    Aestra::Audio::AutosaveManager::Config config;
    config.enabled = m_autoSaveManager.isEnabled();
    config.autosaveInterval = resolveAutosaveInterval();
    config.captureSnapshotOnCallingThread = true;
    config.autosavePathOverride = m_documentState.autosavePath();
    const std::string canonicalProjectPath = m_documentState.canonicalPath();
    config.onAutosaveCommitted = [this, canonicalProjectPath](const std::string& autosavePath) {
        writeRecoveryMarkerForAutosave(autosavePath, canonicalProjectPath);
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
    m_autoSaveManager.initialize(m_documentState.canonicalPath(), std::move(config));
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
    m_windowManager->setWindowTitle(m_documentState.windowTitle());
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
        exportDialog->show(m_documentState.canonicalPath(), *engine, trackMgr);
    }
}
