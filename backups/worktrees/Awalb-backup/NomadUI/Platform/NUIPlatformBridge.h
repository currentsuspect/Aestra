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
    void setResizeCallback(std::function<void(int, int)> callback);
    void setCloseCallback(std::function<void()> callback);
    void setDPIChangeCallback(std::function<void(float)> callback);
    
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

private:
    // Convert NomadPlat events to NomadUI events
    void setupEventBridges();
    int convertMouseButton(Nomad::MouseButton button);
    int convertKeyCode(Nomad::KeyCode key);

    // NomadPlat window
    Nomad::IPlatformWindow* m_window;
    
    // NomadUI-specific state
    NUIComponent* m_rootComponent;
    NUIRenderer* m_renderer;
    
    // Mouse position tracking for wheel events
    int m_lastMouseX;
    int m_lastMouseY;
    
    // NomadUI-style callbacks
    std::function<void(int, int)> m_mouseMoveCallback;
    std::function<void(int, bool)> m_mouseButtonCallback;
    std::function<void(float)> m_mouseWheelCallback;
    std::function<void(int, bool)> m_keyCallback;
    std::function<void(int, int)> m_resizeCallback;
    std::function<void()> m_closeCallback;
    std::function<void(float)> m_dpiChangeCallback;
};

} // namespace NomadUI
