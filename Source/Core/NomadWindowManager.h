// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <memory>
#include <string>
#include <atomic>
#include <functional>

#include "../NomadPlat/include/NomadPlatform.h"
#include "../NomadUI/Core/NUIComponent.h"
#include "NUICustomWindow.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Core/NUIAdaptiveFPS.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "../NomadUI/Platform/NUIPlatformBridge.h"
#include "NUIMenuBar.h"
#include "NUIContextMenu.h"

// Forward declarations
namespace Nomad {
    class SettingsDialog;
    class ConfirmationDialog;
    class RecoveryDialog;
}
class UnifiedHUD;
class NomadRootComponent;
class NomadContent;

class NomadWindowManager {
public:
    NomadWindowManager();
    ~NomadWindowManager();

    struct WindowConfig {
        std::string title;
        int width;
        int height;
        bool fullscreen;
    };

    enum class TransportAction { Stop, Play, Pause };

    bool initialize(const WindowConfig& config);
    void shutdown();
    bool processEvents();
    void render();

    // Accessors
    NomadUI::NUIPlatformBridge* getWindow() { return m_window.get(); }
    NomadUI::NUIRenderer* getRenderer() { return m_renderer.get(); }
    NomadRootComponent* getRootComponent() { return m_rootComponent.get(); }
    NomadUI::NUICustomWindow* getCustomWindow() { return m_customWindow.get(); }
    UnifiedHUD* getUnifiedHUD() { return m_unifiedHUD.get(); }

    NomadUI::NUIAdaptiveFPS* getAdaptiveFPS() { return m_adaptiveFPS.get(); }

    // UI State Management
    void setContent(std::shared_ptr<NomadContent> content);
    void setSettingsDialog(std::shared_ptr<Nomad::SettingsDialog> dialog);
    void setConfirmationDialog(std::shared_ptr<Nomad::ConfirmationDialog> dialog);
    void setRecoveryDialog(std::shared_ptr<Nomad::RecoveryDialog> dialog);
    void setUnifiedHUD(std::shared_ptr<UnifiedHUD> hud);

    // Menus
    void setMenuBar(std::shared_ptr<NomadUI::NUIMenuBar> menuBar);
    void showDropdownMenu(std::shared_ptr<NomadUI::NUIContextMenu> menu, float xOffset);
    void hideActiveMenu();
    bool isMenuOpen() const;

    // Window Control
    void setWindowTitle(const std::string& title);
    void toggleFullScreen();
    bool isFullScreen() const;
    void swapBuffers();

    // Cursor
    void initializeCustomCursors();
    void renderCustomCursor();
    void updateCursorState(bool focused, NomadUI::NUICursorStyle style);

    // Callbacks setters (forwarded to Bridge)
    void setCloseCallback(std::function<void()> cb) { if (m_window) m_window->setCloseCallback(cb); }
    void setResizeCallback(std::function<void(int, int)> cb) { if (m_window) m_window->setResizeCallback(cb); }
    void setTransportCallback(std::function<void(TransportAction)> cb) { m_transportCallback = cb; }
    // ... others handled internally or exposed as needed

    // Mouse Tracking
    int getLastMouseX() const { return m_lastMouseX; }
    int getLastMouseY() const { return m_lastMouseY; }

    NomadUI::NUIModifiers getKeyModifiers() const { return m_keyModifiers; }
    void setKeyModifiers(NomadUI::NUIModifiers mods) { m_keyModifiers = mods; }

    // Frame timing
    void beginFrame();
    double endFrame(); // Returns sleep time
    double getDeltaTime() const { return m_deltaTime; }

private:
    std::unique_ptr<NomadUI::NUIPlatformBridge> m_window;
    std::unique_ptr<NomadUI::NUIRenderer> m_renderer;

    std::shared_ptr<NomadRootComponent> m_rootComponent;
    std::shared_ptr<NomadUI::NUICustomWindow> m_customWindow;

    // Weak pointers to content? No, WindowManager effectively owns the view hierarchy.
    // But NomadContent is the model/view-controller hybrid.
    // For now shared_ptr is fine as NomadApp holds it too.
    std::shared_ptr<NomadContent> m_content;

    std::shared_ptr<Nomad::SettingsDialog> m_settingsDialog;
    std::shared_ptr<Nomad::ConfirmationDialog> m_confirmationDialog;
    std::shared_ptr<Nomad::RecoveryDialog> m_recoveryDialog;
    std::shared_ptr<UnifiedHUD> m_unifiedHUD;

    std::unique_ptr<NomadUI::NUIAdaptiveFPS> m_adaptiveFPS;

    // Menus
    std::shared_ptr<NomadUI::NUIMenuBar> m_menuBar;
    std::shared_ptr<NomadUI::NUIContextMenu> m_activeMenu;
    float m_activeMenuXOffset{-1.0f};

    // Cursor
    bool m_useCustomCursor{true};
    bool m_windowFocused{true};
    std::shared_ptr<NomadUI::NUIIcon> m_cursorArrow;
    std::shared_ptr<NomadUI::NUIIcon> m_cursorHand;
    std::shared_ptr<NomadUI::NUIIcon> m_cursorIBeam;
    std::shared_ptr<NomadUI::NUIIcon> m_cursorResizeH;
    std::shared_ptr<NomadUI::NUIIcon> m_cursorResizeV;
    NomadUI::NUICursorStyle m_activeCursorStyle{NomadUI::NUICursorStyle::Arrow};

    // Input State
    std::function<void(TransportAction)> m_transportCallback;
    int m_lastMouseX{0};
    int m_lastMouseY{0};
    NomadUI::NUIModifiers m_keyModifiers{NomadUI::NUIModifiers::None};

    // Frame state
    std::chrono::time_point<std::chrono::high_resolution_clock> m_frameStart;
    double m_deltaTime{0.0};
};
