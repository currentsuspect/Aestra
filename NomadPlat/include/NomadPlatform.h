// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../../NomadCore/include/NomadConfig.h"
#include <string>
#include <functional>

namespace Nomad {

// =============================================================================
// Platform Window Description
// =============================================================================
struct WindowDesc {
    std::string title = "NOMAD";
    int width = 1280;
    int height = 720;
    int x = -1;  // -1 = center
    int y = -1;  // -1 = center
    bool resizable = true;
    bool decorated = true;
    bool startMaximized = false;
    bool startFullscreen = false;
};

// =============================================================================
// Input Event Types
// =============================================================================
enum class MouseButton {
    Left = 0,
    Right = 1,
    Middle = 2
};

enum class KeyCode {
    Unknown = 0,
    // Letters
    A = 65, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    // Numbers
    Num0 = 48, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    // Function keys
    F1 = 112, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    // Special keys
    Escape = 27,
    Tab = 9,
    CapsLock = 20,
    Shift = 16,
    Control = 17,
    Alt = 18,
    Space = 32,
    Enter = 13,
    Backspace = 8,
    Delete = 46,
    Insert = 45,
    Home = 36,
    End = 35,
    PageUp = 33,
    PageDown = 34,
    Left = 37,
    Up = 38,
    Right = 39,
    Down = 40
};

struct KeyModifiers {
    bool shift = false;
    bool control = false;
    bool alt = false;
    bool super = false;  // Windows key / Command key
};

// =============================================================================
// Platform Window Interface
// =============================================================================
class IPlatformWindow {
public:
    virtual ~IPlatformWindow() = default;

    // Window lifecycle
    virtual bool create(const WindowDesc& desc) = 0;
    virtual void destroy() = 0;
    virtual bool isValid() const = 0;

    // Event processing
    virtual bool pollEvents() = 0;  // Returns false when window should close
    virtual void swapBuffers() = 0;

    // Window properties
    virtual void setTitle(const std::string& title) = 0;
    virtual void setSize(int width, int height) = 0;
    virtual void getSize(int& width, int& height) const = 0;
    virtual void setPosition(int x, int y) = 0;
    virtual void getPosition(int& x, int& y) const = 0;

    // Window state
    virtual void show() = 0;
    virtual void hide() = 0;
    virtual void minimize() = 0;
    virtual void maximize() = 0;
    virtual void restore() = 0;
    virtual bool isMaximized() const = 0;
    virtual bool isMinimized() const = 0;
    virtual void requestClose() = 0;  // Request window close (triggers close callback)

    // Fullscreen
    virtual void setFullscreen(bool fullscreen) = 0;
    virtual bool isFullscreen() const = 0;

    // OpenGL context
    virtual bool createGLContext() = 0;
    virtual bool makeContextCurrent() = 0;
    virtual void setVSync(bool enabled) = 0;

    // Native handles (platform-specific)
    virtual void* getNativeHandle() const = 0;
    virtual void* getNativeDisplayHandle() const = 0;

    // DPI support
    virtual float getDPIScale() const = 0;
    
    // Cursor control
    // IMPORTANT: All platform window implementations (Win32, X11, Cocoa) MUST override this method.
    // Expected behavior: Show/hide cursor immediately with no delay.
    // Thread requirements: MUST be called from the same thread that created the window (window thread).
    // Cross-thread calls will cause cursor display count desynchronization and broken cursor state.
    // Implementation should update cursor visibility state immediately and persist across window state changes.
    virtual void setCursorVisible(bool visible) = 0;
    
    // Modifier key state query (for wheel events that need modifier info)
    virtual KeyModifiers getCurrentModifiers() const = 0;

    // Event callbacks
    virtual void setMouseMoveCallback(std::function<void(int x, int y)> callback) = 0;
    virtual void setMouseButtonCallback(std::function<void(MouseButton button, bool pressed, int x, int y)> callback) = 0;
    virtual void setMouseWheelCallback(std::function<void(float delta)> callback) = 0;
    virtual void setKeyCallback(std::function<void(KeyCode key, bool pressed, const KeyModifiers& mods)> callback) = 0;
    virtual void setCharCallback(std::function<void(unsigned int codepoint)> callback) = 0;
    virtual void setResizeCallback(std::function<void(int width, int height)> callback) = 0;
    virtual void setCloseCallback(std::function<void()> callback) = 0;
    virtual void setFocusCallback(std::function<void(bool focused)> callback) = 0;
    virtual void setDPIChangeCallback(std::function<void(float dpiScale)> callback) = 0;
};

// =============================================================================
// Platform Utilities Interface
// =============================================================================
class IPlatformUtils {
public:
    virtual ~IPlatformUtils() = default;

    // Time
    virtual double getTime() const = 0;  // High-resolution time in seconds
    virtual void sleep(int milliseconds) const = 0;

    // File dialogs
    virtual std::string openFileDialog(const std::string& title, const std::string& filter) const = 0;
    virtual std::string saveFileDialog(const std::string& title, const std::string& filter) const = 0;
    virtual std::string selectFolderDialog(const std::string& title) const = 0;

    // Clipboard
    virtual void setClipboardText(const std::string& text) const = 0;
    virtual std::string getClipboardText() const = 0;

    // System info
    virtual std::string getPlatformName() const = 0;
    virtual int getProcessorCount() const = 0;
    virtual size_t getSystemMemory() const = 0;  // In bytes
    
    // Paths
    virtual std::string getAppDataPath(const std::string& appName) const = 0;  // Returns platform-specific app data directory
};

// =============================================================================
// Platform Factory
// =============================================================================
class Platform {
public:
    // Create platform-specific window
    static IPlatformWindow* createWindow();

    // Get platform utilities
    static IPlatformUtils* getUtils();

    // Initialize/shutdown platform
    static bool initialize();
    static void shutdown();

    // Threading
    enum class ThreadPriority {
        Low,
        Normal,
        High,
        RealtimeAudio // Maps to MMCSS "Pro Audio" on Windows
    };

    // Set priority for the CURRENT thread
    static bool setCurrentThreadPriority(ThreadPriority priority);

private:
    static IPlatformUtils* s_utils;

    // RAII scope for Realtime Audio threads (MMCSS on Windows)
    // Usage: Create this ONLY on the main audio callback thread.
    // WARNING: Do NOT create this in a loop or per-callback! Create once per thread lifetime.
    class AudioThreadScope {
    public:
        AudioThreadScope();
        ~AudioThreadScope();

        // Prevent copying
        AudioThreadScope(const AudioThreadScope&) = delete;
        AudioThreadScope& operator=(const AudioThreadScope&) = delete;

        bool isValid() const { return m_valid; }

    private:
        void* m_handle = nullptr; // Windows: HANDLE (MMCSS)
        bool m_valid = false;
    };};

} // namespace Nomad
