// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../NomadPlat/include/NomadPlatform.h"
#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Core/NUICustomWindow.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Core/NUIAdaptiveFPS.h"
#include "../NomadUI/Core/NUIFrameProfiler.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "../NomadUI/Platform/NUIPlatformBridge.h"
#include "../NomadUI/Widgets/NUIMenuBar.h"
#include "../NomadUI/Core/NUIContextMenu.h"
#include "../NomadAudio/include/NomadAudio.h"
#include "../NomadAudio/include/AudioEngine.h"
#include "NomadRootComponent.h"
#include "NomadContent.h"
#include "SettingsDialog.h"
#include "ConfirmationDialog.h"
#include "RecoveryDialog.h"
#include "AudioSettingsPage.h"
#include "GeneralSettingsPage.h"
#include "UnifiedHUD.h"
#include "ProjectSerializer.h"
#include "../NomadCore/include/NomadLog.h"

#include <memory>
#include <string>
#include <future>
#include <atomic>
#include <filesystem>

// Forward declarations
namespace NomadUI {
    class NUIAdaptiveFPS;
    class NUIContextMenu;
    class NUIMenuBar;
    class NUIIcon;
}

namespace Nomad {
    class SettingsDialog;
    class ConfirmationDialog;
    class RecoveryDialog;
    class AudioSettingsPage;
    class GeneralSettingsPage;
}

class UnifiedHUD;
class NomadRootComponent;
class NomadContent;

/**
 * @brief Main application class
 * 
 * Manages the lifecycle of all NOMAD subsystems and the main event loop.
 */
class NomadApp {
public:
    NomadApp();
    ~NomadApp();

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
    std::shared_ptr<NomadContent> getNomadContent() const { return m_content; }
    Nomad::Audio::AudioEngine* getAudioEngine() const { return m_audioEngine.get(); }

    // Helpers exposed for easier refactoring (could be private if callback logic was internal)
    static std::string getAppDataPath();
    static std::string getAutosavePath();
    static std::string getLegacyAutosavePath();

private:
    void render();
    void renderCustomCursor();
    void initializeCustomCursors();
    void setupCallbacks();

    // Menu actions
    void showFileMenu();
    void showEditMenu();
    void showViewMenu();
    void showDropdownMenu(std::shared_ptr<NomadUI::NUIContextMenu> menu, float xOffset);

    // Project management
    void requestClose();
    void saveCurrentProject();
    ProjectSerializer::LoadResult loadProject();
    bool saveProject();
    ProjectSerializer::UIState captureUIState() const;
    void applyUIState(const ProjectSerializer::UIState& state);
    void updateWindowTitle();  // Updates title bar with project name and dirty indicator
    
    // Helpers
    void checkKeyModifiers(bool pressed, int key);

    // Audio callback
    static int audioCallback(float* outputBuffer, const float* inputBuffer,
                             uint32_t nFrames, double streamTime, void* userData);

private:
    std::unique_ptr<NomadUI::NUIPlatformBridge> m_window;
    std::unique_ptr<NomadUI::NUIRenderer> m_renderer;
    std::unique_ptr<Nomad::Audio::AudioDeviceManager> m_audioManager;
    std::unique_ptr<Nomad::Audio::AudioEngine> m_audioEngine;
    
    std::shared_ptr<NomadRootComponent> m_rootComponent;
    std::shared_ptr<NomadUI::NUICustomWindow> m_customWindow;
    std::shared_ptr<NomadContent> m_content;
    
    std::shared_ptr<Nomad::SettingsDialog> m_settingsDialog;
    std::shared_ptr<Nomad::AudioSettingsPage> m_audioSettingsPage; // Accessed by audio callback
    std::shared_ptr<Nomad::GeneralSettingsPage> m_generalSettingsPage;
    std::shared_ptr<Nomad::ConfirmationDialog> m_confirmationDialog;
    std::shared_ptr<Nomad::RecoveryDialog> m_recoveryDialog;
    std::shared_ptr<UnifiedHUD> m_unifiedHUD;
    
    std::unique_ptr<NomadUI::NUIAdaptiveFPS> m_adaptiveFPS;
    NomadUI::NUIFrameProfiler m_profiler;  // Legacy profiler
    
    bool m_running;
    bool m_audioInitialized;
    bool m_pendingClose{false};
    Nomad::Audio::AudioStreamConfig m_mainStreamConfig;
    std::string m_projectPath;

    // Auto-save
    std::atomic<bool> m_autoSaveEnabled{true};
    std::atomic<bool> m_autoSaveInFlight{false};
    std::future<bool> m_autoSaveFuture;
    
    NomadUI::NUIModifiers m_keyModifiers{NomadUI::NUIModifiers::None};
    
    // Mouse tracking
    int m_lastMouseX{0};
    int m_lastMouseY{0};
    
    // Window title state (to avoid updating every frame)
    std::string m_lastWindowTitle;
    bool m_lastModifiedState{false};
    
    // Active dropdown menu
    std::shared_ptr<NomadUI::NUIContextMenu> m_activeMenu;
    float m_activeMenuXOffset{-1.0f};  // X position of active menu for toggle detection
    
    // Menu bar
    std::shared_ptr<NomadUI::NUIMenuBar> m_menuBar;
    
    // Custom cursor
    bool m_useCustomCursor{true};
    bool m_windowFocused{true};
    std::shared_ptr<NomadUI::NUIIcon> m_cursorArrow;
    std::shared_ptr<NomadUI::NUIIcon> m_cursorHand;
    std::shared_ptr<NomadUI::NUIIcon> m_cursorIBeam;
    std::shared_ptr<NomadUI::NUIIcon> m_cursorResizeH;
    std::shared_ptr<NomadUI::NUIIcon> m_cursorResizeV;
    NomadUI::NUICursorStyle m_activeCursorStyle{NomadUI::NUICursorStyle::Arrow};
    std::shared_ptr<Nomad::ILogger> m_asyncLogger;
};
