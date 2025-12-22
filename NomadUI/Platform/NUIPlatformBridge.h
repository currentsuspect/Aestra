// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../../NomadPlat/include/NomadPlatform.h"
#include "../Core/NUITypes.h"
#include <functional>

namespace NomadUI {

// Forward declarations
class NUIRenderer;
class NUIComponent;

/**
 * Cursor styles for setCursorStyle()
 */
enum class NUICursorStyle {
    Arrow,          // Default arrow cursor
    Hand,           // Pointing hand (for clickable elements)
    IBeam,          // Text input cursor
    Wait,           // Loading/busy cursor (hourglass/spinner)
    WaitArrow,      // Arrow with loading indicator
    Crosshair,      // Precision crosshair
    ResizeNS,       // North-South resize (vertical)
    ResizeEW,       // East-West resize (horizontal)
    ResizeNESW,     // Diagonal resize (NE-SW)
    ResizeNWSE,     // Diagonal resize (NW-SE)
    ResizeAll,      // Move/all directions
    NotAllowed,     // Disabled/not allowed
    Grab,           // Open hand (ready to grab)
    Grabbing,       // Closed hand (currently grabbing)
    Hidden          // No cursor visible
};

/**
 * Bridge between NomadPlat and NomadUI
 * Wraps NomadPlat's IPlatformWindow to work with NomadUI's existing API
 */
class NUIPlatformBridge {
public:
    NUIPlatformBridge();
    ~NUIPlatformBridge();

    // Window creation and management (NomadUI-compatible API)
    bool create(const std::string& title, int width, int height, bool startMaximized = false);
    bool create(const Nomad::WindowDesc& desc);  // Full control version
    void destroy();
    void show();
    void hide();
    
    // Main loop
    bool processEvents();  // Returns false when window should close
    void swapBuffers();
    
    // Window properties
    void setTitle(const std::string& title);
    void setSize(int width, int height);
    void getSize(int& width, int& height) const;
    void setPosition(int x, int y);
    void getPosition(int& x, int& y) const;
    
    // Window controls
    void minimize();
    void maximize();
    void restore();
    bool isMaximized() const;
    void requestClose();  // Request window close through platform abstraction
    
    // Full screen support
    void toggleFullScreen();
    bool isFullScreen() const;
    void enterFullScreen();
    void exitFullScreen();
    
    // OpenGL context
    bool createGLContext();
    bool makeContextCurrent();
    
    // Event callbacks (NomadUI-style - simplified)
    void setMouseMoveCallback(std::function<void(int, int)> callback);
    void setMouseButtonCallback(std::function<void(int, bool)> callback);
    void setMouseWheelCallback(std::function<void(float)> callback);
    void setKeyCallback(std::function<void(int, bool)> callback);
    void setKeyCallbackEx(std::function<void(int, bool, bool ctrl, bool shift, bool alt)> callback);
    void setResizeCallback(std::function<void(int, int)> callback);
    void setCloseCallback(std::function<void()> callback);
    void setDPIChangeCallback(std::function<void(float)> callback);
    void setFocusCallback(std::function<void(bool focused)> callback);
    
    // NomadUI-specific: Root component
    void setRootComponent(NUIComponent* root) { m_rootComponent = root; }
    NUIComponent* getRootComponent() const { return m_rootComponent; }
    
    // NomadUI-specific: Renderer
    void setRenderer(NUIRenderer* renderer) { m_renderer = renderer; }
    NUIRenderer* getRenderer() const { return m_renderer; }
    
    // Native handles
    void* getNativeHandle() const;
    void* getNativeDeviceContext() const;
    void* getNativeGLContext() const;

    // DPI support
    float getDPIScale() const;
    
    // Cursor control
    void setCursorVisible(bool visible);
    void setCursorPosition(int x, int y);
    void setCursorStyle(NUICursorStyle style);  // Set cursor appearance
    NUICursorStyle getCursorStyle() const;       // Get current cursor style
    
    // Mouse Capture
    void setMouseCapture(bool captured);

private:
    // Convert NomadPlat events to NomadUI events
    void setupEventBridges();
    int convertMouseButton(Nomad::MouseButton button);
    int convertKeyCode(Nomad::KeyCode key);
    NUIModifiers convertModifiers(const Nomad::KeyModifiers& mods);

    // NomadPlat window
    Nomad::IPlatformWindow* m_window;
    
    // NomadUI-specific state
    NUIComponent* m_rootComponent;
    NUIRenderer* m_renderer;
    
    // Mouse position tracking for wheel events
    int m_lastMouseX;
    int m_lastMouseY;
    
    // Cursor style tracking
    NUICursorStyle m_currentCursorStyle = NUICursorStyle::Arrow;
    
    // NomadUI-style callbacks
    std::function<void(int, int)> m_mouseMoveCallback;
    std::function<void(int, bool)> m_mouseButtonCallback;
    std::function<void(float)> m_mouseWheelCallback;
    std::function<void(int, bool)> m_keyCallback;
    std::function<void(int, bool, bool, bool, bool)> m_keyCallbackEx;
    std::function<void(int, int)> m_resizeCallback;
    std::function<void()> m_closeCallback;
    std::function<void(float)> m_dpiChangeCallback;
    std::function<void(bool)> m_focusCallback;
};

} // namespace NomadUI
