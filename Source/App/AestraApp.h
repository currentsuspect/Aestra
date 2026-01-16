// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraWindowManager.h"
#include "AestraAudioController.h"
#include "AestraContent.h"
#include "ProjectSerializer.h"
#include "../AestraCore/include/AestraLog.h"

#include <memory>
#include <string>
#include <future>
#include <atomic>
#include <filesystem>

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
    Aestra::Audio::AudioEngine* getAudioEngine() const { return m_audioController ? m_audioController->getEngine() : nullptr; }

    // Helpers exposed for easier refactoring
    static std::string getAppDataPath();
    static std::string getAutosavePath();
    static std::string getLegacyAutosavePath();

private:
    // Glue logic
    void setupCallbacks();
    void connectAudioToUI();

    // Project management
    void requestClose();
    void saveCurrentProject();
    ProjectSerializer::LoadResult loadProject();
    bool saveProject();
    ProjectSerializer::UIState captureUIState() const;
    void applyUIState(const ProjectSerializer::UIState& state);
    void updateWindowTitle();  // Updates title bar with project name and dirty indicator
    
private:
    std::unique_ptr<AestraWindowManager> m_windowManager;
    std::unique_ptr<AestraAudioController> m_audioController;
    
    std::shared_ptr<AestraContent> m_content;
    std::shared_ptr<Aestra::ILogger> m_asyncLogger;
    
    bool m_running;
    bool m_pendingClose{false};
    std::string m_projectPath;

    // Auto-save
    std::atomic<bool> m_autoSaveEnabled{true};
    std::atomic<bool> m_autoSaveInFlight{false};
    std::future<bool> m_autoSaveFuture;
    
    // Window title state (to avoid updating every frame)
    std::string m_lastWindowTitle;
    bool m_lastModifiedState{false};
};
