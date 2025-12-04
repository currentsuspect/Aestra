// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../../include/NomadPlatform.h"
#include <Windows.h>

namespace Nomad {

class PlatformWindowWin32 : public IPlatformWindow {
public:
    PlatformWindowWin32();
    ~PlatformWindowWin32() override;

    // Static cleanup method for window class (public for platform shutdown access)
    static void unregisterWindowClass();

    // IPlatformWindow implementation
    bool create(const WindowDesc& desc) override;
    void destroy() override;
    bool isValid() const override { return m_hwnd != nullptr; }

    bool pollEvents() override;
    void swapBuffers() override;

    void setTitle(const std::string& title) override;
    void setSize(int width, int height) override;
    void getSize(int& width, int& height) const override;
    void setPosition(int x, int y) override;
    void getPosition(int& x, int& y) const override;

    void show() override;
    void hide() override;
    void minimize() override;
    void maximize() override;
    void restore() override;
    bool isMaximized() const override;
    bool isMinimized() const override;

    void setFullscreen(bool fullscreen) override;
    bool isFullscreen() const override { return m_isFullscreen; }

    bool createGLContext() override;
    bool makeContextCurrent() override;
    void setVSync(bool enabled) override;

    void* getNativeHandle() const override { return m_hwnd; }
    void* getNativeDisplayHandle() const override { return m_hdc; }

    float getDPIScale() const override { return m_dpiScale; }
    
    KeyModifiers getCurrentModifiers() const override { return getKeyModifiers(); }

    void setMouseMoveCallback(std::function<void(int, int)> callback) override { m_mouseMoveCallback = callback; }
    void setMouseButtonCallback(std::function<void(MouseButton, bool, int, int)> callback) override { m_mouseButtonCallback = callback; }
    void setMouseWheelCallback(std::function<void(float)> callback) override { m_mouseWheelCallback = callback; }
    void setKeyCallback(std::function<void(KeyCode, bool, const KeyModifiers&)> callback) override { m_keyCallback = callback; }
    void setCharCallback(std::function<void(unsigned int)> callback) override { m_charCallback = callback; }
    void setResizeCallback(std::function<void(int, int)> callback) override { m_resizeCallback = callback; }
    void setCloseCallback(std::function<void()> callback) override { m_closeCallback = callback; }
    void setFocusCallback(std::function<void(bool)> callback) override { m_focusCallback = callback; }
    void setDPIChangeCallback(std::function<void(float)> callback) override { m_dpiChangeCallback = callback; }

private:
    // Window procedure
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // Helper methods
    bool registerWindowClass();
    bool setupPixelFormat();
    KeyCode translateKeyCode(WPARAM wParam, LPARAM lParam);
    KeyModifiers getKeyModifiers() const;

    // Window handles
    HWND m_hwnd;
    HDC m_hdc;
    HGLRC m_hglrc;

    // Window state
    std::string m_title;
    int m_width;
    int m_height;
    bool m_shouldClose;
    bool m_isFullscreen;
    float m_dpiScale;

    // Fullscreen restore state
    WINDOWPLACEMENT m_wpPrev;
    DWORD m_styleBackup;

    // Event callbacks
    std::function<void(int, int)> m_mouseMoveCallback;
    std::function<void(MouseButton, bool, int, int)> m_mouseButtonCallback;
    std::function<void(float)> m_mouseWheelCallback;
    std::function<void(KeyCode, bool, const KeyModifiers&)> m_keyCallback;
    std::function<void(unsigned int)> m_charCallback;
    std::function<void(int, int)> m_resizeCallback;
    std::function<void()> m_closeCallback;
    std::function<void(bool)> m_focusCallback;
    std::function<void(float)> m_dpiChangeCallback;

    // Static members
    static const wchar_t* WINDOW_CLASS_NAME;
    static bool s_classRegistered;
    static HICON s_hLargeIcon;
    static HICON s_hSmallIcon;
};

} // namespace Nomad
