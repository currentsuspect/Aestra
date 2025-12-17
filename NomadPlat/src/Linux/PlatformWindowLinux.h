#pragma once
#include "../../include/NomadPlatform.h"
#include <SDL2/SDL.h>
#include <string>

namespace Nomad {

class PlatformWindowLinux : public IPlatformWindow {
public:
    PlatformWindowLinux();
    ~PlatformWindowLinux() override;

    // Window lifecycle
    bool create(const WindowDesc& desc) override;
    void destroy() override;
    bool isValid() const override { return m_window != nullptr; }

    // Event processing
    bool pollEvents() override;
    void swapBuffers() override;

    // Window properties
    void setTitle(const std::string& title) override;
    void setSize(int width, int height) override;
    void getSize(int& width, int& height) const override;
    void setPosition(int x, int y) override;
    void getPosition(int& x, int& y) const override;

    // Window state
    void show() override;
    void hide() override;
    void minimize() override;
    void maximize() override;
    void restore() override;
    bool isMaximized() const override;
    bool isMinimized() const override;
    void requestClose() override;

    // Fullscreen
    void setFullscreen(bool fullscreen) override;
    bool isFullscreen() const override;

    // OpenGL context
    bool createGLContext() override;
    bool makeContextCurrent() override;
    void setVSync(bool enabled) override;

    // Native handles
    void* getNativeHandle() const override;
    void* getNativeDisplayHandle() const override;

    // DPI support
    float getDPIScale() const override;
    
    // Cursor control
    void setCursorVisible(bool visible) override;
    
    // Modifier key state query
    KeyModifiers getCurrentModifiers() const override;

    // Event callbacks
    void setMouseMoveCallback(std::function<void(int x, int y)> callback) override { m_mouseMoveCallback = callback; }
    void setMouseButtonCallback(std::function<void(MouseButton button, bool pressed, int x, int y)> callback) override { m_mouseButtonCallback = callback; }
    void setMouseWheelCallback(std::function<void(float delta)> callback) override { m_mouseWheelCallback = callback; }
    void setKeyCallback(std::function<void(KeyCode key, bool pressed, const KeyModifiers& mods)> callback) override { m_keyCallback = callback; }
    void setCharCallback(std::function<void(unsigned int codepoint)> callback) override { m_charCallback = callback; }
    void setResizeCallback(std::function<void(int width, int height)> callback) override { m_resizeCallback = callback; }
    void setCloseCallback(std::function<void()> callback) override { m_closeCallback = callback; }
    void setFocusCallback(std::function<void(bool focused)> callback) override { m_focusCallback = callback; }
    void setDPIChangeCallback(std::function<void(float dpiScale)> callback) override { m_dpiChangeCallback = callback; }

private:
    SDL_Window* m_window = nullptr;
    SDL_GLContext m_glContext = nullptr;
    bool m_isFullscreen = false;
    float m_dpiScale = 1.0f;

    // Callbacks
    std::function<void(int, int)> m_mouseMoveCallback;
    std::function<void(MouseButton, bool, int, int)> m_mouseButtonCallback;
    std::function<void(float)> m_mouseWheelCallback;
    std::function<void(KeyCode, bool, const KeyModifiers&)> m_keyCallback;
    std::function<void(unsigned int)> m_charCallback;
    std::function<void(int, int)> m_resizeCallback;
    std::function<void()> m_closeCallback;
    std::function<void(bool)> m_focusCallback;
    std::function<void(float)> m_dpiChangeCallback;

    // Helpers
    KeyCode translateKey(SDL_Keycode key);
    KeyModifiers getModifiers(Uint16 mod);
};

} // namespace Nomad
