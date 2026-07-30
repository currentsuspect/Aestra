// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../AestraCore/include/AestraLog.h"
#include "../Core/UIState.h"
#include "AestraAudioController.h"
#include "AestraContent.h"
#include "AestraWindowManager.h"
#include "AutosaveManager.h"
#include "Commands/MuseService.h"
#include "Commands/MuseSocketServer.h"
#include "ProjectDocumentState.h"
#include "ProjectSerializer.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

/**
 * @brief Main application class
 *
 * Manages the lifecycle of all AESTRA subsystems and the main event loop.
 * Refactored to delegate to AestraWindowManager and AestraAudioController.
 */
class AestraApp {
public:
    AestraApp();
    ~AestraApp();

    /**
     * @brief Initialize all subsystems
     * @param projectPath Optional path to project file to load on startup
     */
    bool initialize(const std::string& projectPath = "");

    /**
     * @brief Main application loop
     */
    void run();

    /**
     * @brief Shutdown all subsystems
     */
    void shutdown();

    /**
     * @brief Access content manager (needed for audio callbacks)
     */
    std::shared_ptr<AestraContent> getAestraContent() const { return m_content; }
    Aestra::Audio::AudioEngine* getAudioEngine() const {
        return m_audioController ? m_audioController->getEngine() : nullptr;
    }

    // Helpers exposed for easier refactoring
    static std::string getAppDataPath();
    static std::string getAutosavePath();
    static std::string getLegacyAutosavePath();
    static std::string getCrashFlagPath();
    static void writeCrashFlag();
    static void clearCrashFlag();
    static bool isCrashedSession();
    static std::string activeCrashFlagPath();

private:
    enum class ProjectLoadSource { Canonical, Recovery, Snapshot };

    // Initialization helpers (extracted from initialize() for readability)
    bool transitionToInitializing();
    bool initializePlatformAndWindow(const Aestra::UIState& uiState);
    bool initializeAudio();
    void initializeContent();
    /// The one place the autosave interval is decided (#646). Both the initial
    /// setup and reinitAutosaveManager() resolve through this.
    std::chrono::seconds resolveAutosaveInterval();
    void initializeAutosave(bool enabled);
    void buildRecoveryDialog(); // lightweight — needed during startup
    // Idle frame elision (labs/perf/idle-frame-elision-spec.md)
    bool shouldRenderThisFrame();
    std::chrono::steady_clock::time_point m_lastPresentedFrame{};

    void buildSettingsAndDialogs();  // heavy — deferred until first open
    void ensureSettingsAndDialogs(); // lazy guard
    void buildMenuBar();
    void initializePlugins();
    void loadOrRecoverProject(const std::string& projectPath, bool crashedSession);
    void restoreUIState(const Aestra::UIState& uiState);
    bool transitionToRunning();

    // Glue logic
    void setupCallbacks();
    void connectAudioToUI();
    void finalizeAudioSetup(); // Deferred after window shown for instant startup
    void applyPersistedEngineSettings(); // #649: startup owns DSP config, not the dialog

    // Project management
    void requestClose();
    bool saveCurrentProject();
    bool saveProjectAs();
    ProjectSerializer::LoadResult loadProjectFromPath(const std::string& path,
                                                      ProjectLoadSource source = ProjectLoadSource::Canonical,
                                                      const std::string& canonicalPath = "");
    ProjectSerializer::LoadResult applyLoadedProject(const std::string& path,
                                                     ProjectLoadSource source,
                                                     const std::string& canonicalPath,
                                                     ProjectSerializer::LoadResult result);
    ProjectSerializer::LoadResult loadProject();
    bool saveProject();
    bool saveProjectToPath(const std::string& path, bool establishCanonical = false);
    ProjectSerializer::SerializeResult
    serializeCurrentProject(int indentSpaces, const ProjectSerializer::UIState* uiState = nullptr) const;
    bool saveActiveTakeSnapshot(const ProjectSerializer::UIState* uiState = nullptr);
    bool createTakeFromCurrentProject();
    ProjectSerializer::LoadResult switchToTake(const std::string& takeId);
    bool branchFromTake(const std::string& takeId);
    void wireTakesPanel();
    void reinitAutosaveManager();
    ProjectSerializer::UIState captureUIState() const;
    void applyUIState(const ProjectSerializer::UIState& state);
    void updateWindowTitle();
    void startExport();
    static std::string getRecoveryMarkerPath(const std::string& autosavePath);
    static std::string readCrashFlagToken();
    static std::string readRecoveryOriginalProjectPath(const std::string& recoveryMarkerPath,
                                                       const std::string& expectedSessionToken);
    void writeRecoveryMarkerForAutosave(const std::string& autosavePath, const std::string& canonicalProjectPath) const;

private:
    std::unique_ptr<AestraWindowManager> m_windowManager;
    std::unique_ptr<AestraAudioController> m_audioController;

    // Muse socket entry (opt-in via AESTRA_MUSE_PORT): agents drive the live
    // session through the same command system as the UI. Socket IO runs on
    // its own thread; requests execute on the main thread once per frame.
    std::unique_ptr<Aestra::Audio::MuseService> m_museService;
    std::unique_ptr<Aestra::Audio::MuseSocketServer> m_museSocketServer;
    void startMuseSocketIfConfigured();

    std::shared_ptr<AestraContent> m_content;
    std::shared_ptr<Aestra::ILogger> m_asyncLogger;

    bool m_running;
    bool m_pendingClose{false};
    Aestra::ProjectDocumentState m_documentState;

    // Auto-save
    Aestra::Audio::AutosaveManager m_autoSaveManager;

    // Window title state (to avoid updating every frame)
    std::string m_lastWindowTitle;
    bool m_lastModifiedState{false};

    // Recovery state (for deferred project loading during startup)
    std::string m_pendingAutosavePath;
    std::string m_recoverySessionToken;
    std::string m_previousRecoverySessionToken;
    bool m_recoveryHandled{false};

    // Lifetime token — set to false during shutdown so async callbacks can bail
    std::shared_ptr<bool> m_aliveToken;

    // Startup optimization flags
    bool m_audioStreamReady = false;
    bool m_audioConfigSynced = false;
    bool m_rtStateLogged = false;
};
