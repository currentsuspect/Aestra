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
#include "TransportTypes.h"

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
    /** @brief Construct the window manager and its top-level UI services. */
    AestraWindowManager();
    /** @brief Tear down the native window, renderer, and owned UI hierarchy. */
    ~AestraWindowManager();

    struct WindowConfig {
        /** @brief Window title shown by the platform host. */
        std::string title;
        /** @brief Initial window width. */
        int width;
        /** @brief Initial window height. */
        int height;
        /** @brief Whether the window starts in fullscreen mode. */
        bool fullscreen;
    };

    using TransportAction = Aestra::TransportAction;

    /** @brief Initialize the window, renderer, and root component. */
    bool initialize(const WindowConfig& config);
    /** @brief Shut down the window manager and release owned resources. */
    void shutdown();
    /** @brief Pump platform events once. */
    bool processEvents();
    /** @brief Render the current UI frame. */
    void render();

    /** @brief Get the platform bridge/window wrapper. */
    AestraUI::NUIPlatformBridge* getWindow() { return m_window.get(); }
    /** @brief Get the active renderer. */
    AestraUI::NUIRenderer* getRenderer() { return m_renderer.get(); }
    /** @brief Get the root component attached to the window. */
    AestraRootComponent* getRootComponent() { return m_rootComponent.get(); }

    /// True when some overlay needs rendering every frame regardless of
    /// dirtiness: the performance HUD (needs fresh profiler samples), any
    /// visible dialog, or an open menu. Used by idle frame elision
    /// (labs/perf/idle-frame-elision-spec.md).
    bool requiresContinuousRender() const;
    /** @brief Get the custom window chrome widget. */
    AestraUI::NUICustomWindow* getCustomWindow() { return m_customWindow.get(); }
    /** @brief Get the unified HUD overlay. */
    UnifiedHUD* getUnifiedHUD() { return m_unifiedHUD.get(); }

    /** @brief Get the adaptive-FPS controller. */
    AestraUI::NUIAdaptiveFPS* getAdaptiveFPS() { return m_adaptiveFPS.get(); }

    /** @brief Attach the main content view. */
    void setContent(std::shared_ptr<AestraContent> content);
    /** @brief Attach the settings dialog. */
    void setSettingsDialog(std::shared_ptr<Aestra::SettingsDialog> dialog);
    /** @brief Attach the confirmation dialog. */
    void setConfirmationDialog(std::shared_ptr<Aestra::ConfirmationDialog> dialog);
    /** @brief Attach the recovery dialog. */
    void setRecoveryDialog(std::shared_ptr<Aestra::RecoveryDialog> dialog);
    /** @brief Attach the unified HUD overlay. */
    void setUnifiedHUD(std::shared_ptr<UnifiedHUD> hud);

    /** @brief Get the settings dialog. */
    std::shared_ptr<Aestra::SettingsDialog> getSettingsDialog() { return m_settingsDialog; }
    /** @brief Get the confirmation dialog. */
    std::shared_ptr<Aestra::ConfirmationDialog> getConfirmationDialog() { return m_confirmationDialog; }
    /** @brief Get the recovery dialog. */
    std::shared_ptr<Aestra::RecoveryDialog> getRecoveryDialog() { return m_recoveryDialog; }
    /** @brief Get the export dialog. */
    std::shared_ptr<class ExportDialog> getExportDialog() { return m_exportDialog; }
    /** @brief Attach the export dialog. */
    void setExportDialog(std::shared_ptr<class ExportDialog> dialog);

    /** @brief Attach the main menu bar. */
    void setMenuBar(std::shared_ptr<AestraUI::NUIMenuBar> menuBar);
    /** @brief Show a dropdown context menu anchored to the title area. */
    void showDropdownMenu(std::shared_ptr<AestraUI::NUIContextMenu> menu, float xOffset);
    /** @brief Hide the active dropdown menu if present. */
    void hideActiveMenu();
    /** @brief Check whether any dropdown menu is open. */
    bool isMenuOpen() const;

    /** @brief Update the native window title. */
    void setWindowTitle(const std::string& title);
    /** @brief Toggle fullscreen mode. */
    void toggleFullScreen();
    /** @brief Check whether the native window is fullscreen. */
    bool isFullScreen() const;
    /** @brief Swap the renderer backbuffer to screen. */
    void swapBuffers();

    /** @brief Update the title-bar export progress indicator. */
    void setExportProgress(float progress);  // 0-1, -1 for indeterminate
    /** @brief Toggle the title-bar exporting state. */
    void setExporting(bool exporting);
    /** @brief Set the callback fired when the export button is pressed. */
    void setOnExportRequested(std::function<void()> cb);

    /** @brief Load and prepare custom cursor assets. */
    void initializeCustomCursors();
    /** @brief Render the current custom cursor. */
    void renderCustomCursor();
    /** @brief Update cursor visibility and style. */
    // updateCursorState removed — was dead code, never called.
    // If restoring, check cursorCaptureActive in NUIPlatformBridge first:
    // hidden-cursor drag now owns cursor suppression, not this method.

    // Callbacks setters (forwarded to Bridge)
    void setCloseCallback(std::function<void()> cb) { if (m_window) m_window->setCloseCallback(cb); }
    void setResizeCallback(std::function<void(int, int)> cb) { if (m_window) m_window->setResizeCallback(cb); }
    void setTransportCallback(std::function<void(TransportAction)> cb);
    void setSaveCallback(std::function<void()> cb);
    // ... others handled internally or exposed as needed

    /** @brief Get the last known mouse x position. */
    int getLastMouseX() const { return m_lastMouseX; }
    /** @brief Get the last known mouse y position. */
    int getLastMouseY() const { return m_lastMouseY; }

    /** @brief Get the current modifier-key state. */
    AestraUI::NUIModifiers getKeyModifiers() const { return m_keyModifiers; }
    /** @brief Override the cached modifier-key state. */
    void setKeyModifiers(AestraUI::NUIModifiers mods) { m_keyModifiers = mods; }

    /** @brief Mark the start of a rendered frame. */
    void beginFrame();
    /** @brief Finish the current frame and return suggested sleep time. */
    double endFrame(); // Returns sleep time
    /** @brief Get the most recent frame delta in seconds. */
    double getDeltaTime() const { return m_deltaTime; }

    // Window state capture/restore for persistence (Issue #120)
    struct WindowState {
        /** @brief Stored window x position. */
        int x = 100;
        /** @brief Stored window y position. */
        int y = 100;
        /** @brief Stored window width. */
        int width = 1280;
        /** @brief Stored window height. */
        int height = 720;
        /** @brief Stored maximized-state flag. */
        bool maximized = false;
    };
    /** @brief Capture the current native window placement. */
    WindowState captureWindowState() const;
    /** @brief Restore a previously captured native window placement. */
    void applyWindowState(const WindowState& state);

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
    bool m_confirmationDialogRaised{false};
    std::shared_ptr<Aestra::RecoveryDialog> m_recoveryDialog;
    std::shared_ptr<UnifiedHUD> m_unifiedHUD;
    std::shared_ptr<class ExportDialog> m_exportDialog;

    std::unique_ptr<AestraUI::NUIAdaptiveFPS> m_adaptiveFPS;

    // Menus
    std::shared_ptr<AestraUI::NUIMenuBar> m_menuBar;
    std::shared_ptr<AestraUI::NUIContextMenu> m_activeMenu;
    float m_activeMenuXOffset{-1.0f};

    // Cursor
    bool m_useCustomCursor{true};
    bool m_windowFocused{true};
    std::shared_ptr<AestraUI::NUIIcon> m_cursorArrow;
    std::shared_ptr<AestraUI::NUIIcon> m_cursorHandPointing;
    std::shared_ptr<AestraUI::NUIIcon> m_cursorHand;
    std::shared_ptr<AestraUI::NUIIcon> m_cursorHandGrabbing;
    std::shared_ptr<AestraUI::NUIIcon> m_cursorIBeam;
    std::shared_ptr<AestraUI::NUIIcon> m_cursorResizeH;
    std::shared_ptr<AestraUI::NUIIcon> m_cursorResizeV;
    std::shared_ptr<AestraUI::NUIIcon> m_cursorResizeDiagNESW;
    std::shared_ptr<AestraUI::NUIIcon> m_cursorResizeDiagNWSE;
    AestraUI::NUICursorStyle m_activeCursorStyle{AestraUI::NUICursorStyle::Arrow};

    // Input State
    std::function<void(TransportAction)> m_transportCallback;
    std::function<void()> m_saveCallback;
    // SVG overlay cursor position cache. Updated by AWM mouse move callback.
    // Post-warp divergence from SDL getCursorPosition() is intentional —
    // renderCustomCursor now polls the authoritative platform position
    // (getCursorPosition) rather than this cache. This cache is kept for
    // legacy delta computations and fallback if the platform query fails.
    int m_lastMouseX{0};
    int m_lastMouseY{0};
    AestraUI::NUIModifiers m_keyModifiers{AestraUI::NUIModifiers::None};

    // Frame state
    std::chrono::time_point<std::chrono::high_resolution_clock> m_frameStart;
    double m_deltaTime{0.0};
};
