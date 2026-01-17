// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <memory>
#include <string>
#include <atomic>
#include <functional>

#include "../AestraPlat/include/AestraPlatform.h"
#include "../AestraUI/Core/NUIComponent.h"
#include "NUICustomWindow.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Core/NUIAdaptiveFPS.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../AestraUI/Platform/NUIPlatformBridge.h"
#include "NUIMenuBar.h"
#include "NUIContextMenu.h"

// Forward declarations
namespace Aestra {
    class SettingsDialog;
    class ConfirmationDialog;
    class RecoveryDialog;
}
class UnifiedHUD;
class AestraRootComponent;
class AestraContent;

class AestraWindowManager {
public:
    AestraWindowManager();
    ~AestraWindowManager();

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
    AestraUI::NUIPlatformBridge* getWindow() { return m_window.get(); }
    AestraUI::NUIRenderer* getRenderer() { return m_renderer.get(); }
    AestraRootComponent* getRootComponent() { return m_rootComponent.get(); }
    AestraUI::NUICustomWindow* getCustomWindow() { return m_customWindow.get(); }
    UnifiedHUD* getUnifiedHUD() { return m_unifiedHUD.get(); }

    AestraUI::NUIAdaptiveFPS* getAdaptiveFPS() { return m_adaptiveFPS.get(); }

    // UI State Management
    void setContent(std::shared_ptr<AestraContent> content);
    void setSettingsDialog(std::shared_ptr<Aestra::SettingsDialog> dialog);
    void setConfirmationDialog(std::shared_ptr<Aestra::ConfirmationDialog> dialog);
    void setRecoveryDialog(std::shared_ptr<Aestra::RecoveryDialog> dialog);
    void setUnifiedHUD(std::shared_ptr<UnifiedHUD> hud);

    // Dialogs getters
    std::shared_ptr<Aestra::SettingsDialog> getSettingsDialog() { return m_settingsDialog; }

    // Menus
    void setMenuBar(std::shared_ptr<AestraUI::NUIMenuBar> menuBar);
    void showDropdownMenu(std::shared_ptr<AestraUI::NUIContextMenu> menu, float xOffset);
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
    void updateCursorState(bool focused, AestraUI::NUICursorStyle style);

    // Callbacks setters (forwarded to Bridge)
    void setCloseCallback(std::function<void()> cb) { if (m_window) m_window->setCloseCallback(cb); }
    void setResizeCallback(std::function<void(int, int)> cb) { if (m_window) m_window->setResizeCallback(cb); }
    void setTransportCallback(std::function<void(TransportAction)> cb) { m_transportCallback = cb; }
    // ... others handled internally or exposed as needed

    // Mouse Tracking
    int getLastMouseX() const { return m_lastMouseX; }
    int getLastMouseY() const { return m_lastMouseY; }

    AestraUI::NUIModifiers getKeyModifiers() const { return m_keyModifiers; }
    void setKeyModifiers(AestraUI::NUIModifiers mods) { m_keyModifiers = mods; }

    // Frame timing
    void beginFrame();
    double endFrame(); // Returns sleep time
    double getDeltaTime() const { return m_deltaTime; }

private:
    std::unique_ptr<AestraUI::NUIPlatformBridge> m_window;
    std::unique_ptr<AestraUI::NUIRenderer> m_renderer;

    std::shared_ptr<AestraRootComponent> m_rootComponent;
    std::shared_ptr<AestraUI::NUICustomWindow> m_customWindow;

    // Weak pointers to content? No, WindowManager effectively owns the view hierarchy.
    // But AestraContent is the model/view-controller hybrid.
    // For now shared_ptr is fine as AestraApp holds it too.
    std::shared_ptr<AestraContent> m_content;

    std::shared_ptr<Aestra::SettingsDialog> m_settingsDialog;
    std::shared_ptr<Aestra::ConfirmationDialog> m_confirmationDialog;
    std::shared_ptr<Aestra::RecoveryDialog> m_recoveryDialog;
    std::shared_ptr<UnifiedHUD> m_unifiedHUD;

    std::unique_ptr<AestraUI::NUIAdaptiveFPS> m_adaptiveFPS;

    // Menus
    std::shared_ptr<AestraUI::NUIMenuBar> m_menuBar;
    std::shared_ptr<AestraUI::NUIContextMenu> m_activeMenu;
    float m_activeMenuXOffset{-1.0f};

    // Cursor
    bool m_useCustomCursor{true};
    bool m_windowFocused{true};
    std::shared_ptr<AestraUI::NUIIcon> m_cursorArrow;
    std::shared_ptr<AestraUI::NUIIcon> m_cursorHand;
    std::shared_ptr<AestraUI::NUIIcon> m_cursorIBeam;
    std::shared_ptr<AestraUI::NUIIcon> m_cursorResizeH;
    std::shared_ptr<AestraUI::NUIIcon> m_cursorResizeV;
    AestraUI::NUICursorStyle m_activeCursorStyle{AestraUI::NUICursorStyle::Arrow};

    // Input State
    std::function<void(TransportAction)> m_transportCallback;
    int m_lastMouseX{0};
    int m_lastMouseY{0};
    AestraUI::NUIModifiers m_keyModifiers{AestraUI::NUIModifiers::None};

    // Frame state
    std::chrono::time_point<std::chrono::high_resolution_clock> m_frameStart;
    double m_deltaTime{0.0};
};
