// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../../AestraCore/include/AestraConfig.h"

#include <functional>
#include <string>

namespace Aestra {

// =============================================================================
// Platform Window Description
// =============================================================================
struct WindowDesc {
    /** @brief Window title shown by the host platform. */
    std::string title = "AESTRA";
    /** @brief Initial window width in logical pixels. */
    int width = 1280;
    /** @brief Initial window height in logical pixels. */
    int height = 720;
    /** @brief Initial x position, or -1 to center on the active display. */
    int x = -1; // -1 = center
    /** @brief Initial y position, or -1 to center on the active display. */
    int y = -1; // -1 = center
    /** @brief Whether the host platform should expose resize affordances. */
    bool resizable = true;
    /** @brief Whether the native window frame and controls are shown. */
    bool decorated = true;
    /** @brief Whether the window should start maximized. */
    bool startMaximized = false;
    /** @brief Whether the window should start in fullscreen mode. */
    bool startFullscreen = false;
};

// =============================================================================
// Input Event Types
// =============================================================================
enum class MouseButton { Left = 0, Right = 1, Middle = 2 };

enum class KeyCode {
    Unknown = 0,
    // Letters
    A = 65,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    // Numbers
    Num0 = 48,
    Num1,
    Num2,
    Num3,
    Num4,
    Num5,
    Num6,
    Num7,
    Num8,
    Num9,
    // Function keys
    F1 = 112,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
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
    /** @brief True while the Shift key is held. */
    bool shift = false;
    /** @brief True while the Control key is held. */
    bool control = false;
    /** @brief True while the Alt/Option key is held. */
    bool alt = false;
    /** @brief True while the platform super key is held. */
    bool super = false; // Windows key / Command key
    /** @brief True while Caps Lock is active. */
    bool capsLock = false;
};

// Hit Test Result (Generic)
enum class HitTestResult {
    Client,  // Client area (interactive)
    Caption, // Title bar (draggable, double-click to maximize)
    ResizeTop,
    ResizeBottom,
    ResizeLeft,
    ResizeRight,
    ResizeTopLeft,
    ResizeTopRight,
    ResizeBottomLeft,
    ResizeBottomRight,
    CloseButton,
    MaxButton,
    MinButton, // (Optional: Platform handled buttons)
    Nowhere,   // Transparent/Pass-through
    Default    // Use platform default logic
};

using HitTestCallback = std::function<HitTestResult(int x, int y)>;

// =============================================================================
// Platform Window Interface
// =============================================================================
class IPlatformWindow {
public:
    virtual ~IPlatformWindow() = default;

    /** @brief Create the native window from a logical description. */
    virtual bool create(const WindowDesc& desc) = 0;
    /** @brief Destroy the native window and any attached graphics context. */
    virtual void destroy() = 0;
    /** @brief Check whether the native window handle is valid. */
    virtual bool isValid() const = 0;

    /** @brief Pump platform events once. Returns false when the app should exit. */
    virtual bool pollEvents() = 0; // Returns false when window should close
    /** @brief Present the current OpenGL backbuffer. */
    virtual void swapBuffers() = 0;

    /** @brief Update the native window title. */
    virtual void setTitle(const std::string& title) = 0;
    /** @brief Resize the window in logical pixels. */
    virtual void setSize(int width, int height) = 0;
    /** @brief Query the current window size in logical pixels. */
    virtual void getSize(int& width, int& height) const = 0;
    /** @brief Move the native window to a screen position. */
    virtual void setPosition(int x, int y) = 0;
    /** @brief Query the current window position. */
    virtual void getPosition(int& x, int& y) const = 0;

    /** @brief Show the window. */
    virtual void show() = 0;
    /** @brief Hide the window. */
    virtual void hide() = 0;
    /** @brief Minimize the window. */
    virtual void minimize() = 0;
    /** @brief Maximize the window. */
    virtual void maximize() = 0;
    /** @brief Restore the window from minimized or maximized state. */
    virtual void restore() = 0;
    /** @brief Check whether the window is currently maximized. */
    virtual bool isMaximized() const = 0;
    /** @brief Check whether the window is currently minimized. */
    virtual bool isMinimized() const = 0;
    /** @brief Ask the windowing backend to close the window gracefully. */
    virtual void requestClose() = 0; // Request window close (triggers close callback)

    /** @brief Toggle fullscreen mode. */
    virtual void setFullscreen(bool fullscreen) = 0;
    /** @brief Check whether fullscreen mode is active. */
    virtual bool isFullscreen() const = 0;

    /** @brief Create the native OpenGL context for this window. */
    virtual bool createGLContext() = 0;
    /** @brief Make this window's OpenGL context current on the calling thread. */
    virtual bool makeContextCurrent() = 0;
    /** @brief Enable or disable swap-interval synchronization. */
    virtual void setVSync(bool enabled) = 0;

    /** @brief Retrieve the platform-specific window handle. */
    virtual void* getNativeHandle() const = 0;
    /** @brief Retrieve the platform-specific display handle when applicable. */
    virtual void* getNativeDisplayHandle() const = 0;

    /** @brief Query the current DPI scale factor for the window. */
    virtual float getDPIScale() const = 0;

    // Cursor control
    // IMPORTANT: All platform window implementations (Win32, X11, Cocoa) MUST override this method.
    // Expected behavior: Show/hide cursor immediately with no delay.
    // Thread requirements: MUST be called from the same thread that created the window (window thread).
    virtual void setCursorVisible(bool visible) = 0;

    /** @brief Warp the cursor to a screen-space position. */
    virtual void setCursorPosition(int x, int y) = 0;

    /** @brief Capture or release the mouse for drag operations outside the window. */
    virtual void setMouseCapture(bool captured) {}

    /** @brief Query current modifier-key state for wheel and gesture events. */
    virtual KeyModifiers getCurrentModifiers() const = 0;

    /** @brief Set custom hit-testing for frameless window chrome. */
    virtual void setHitTestCallback(HitTestCallback callback) = 0;
    /** @brief Set the mouse-move callback. */
    virtual void setMouseMoveCallback(std::function<void(int x, int y)> callback) = 0;
    /** @brief Set the mouse-button callback. */
    virtual void
    setMouseButtonCallback(std::function<void(MouseButton button, bool pressed, int x, int y)> callback) = 0;
    /** @brief Set the mouse-wheel callback. */
    virtual void setMouseWheelCallback(std::function<void(float delta)> callback) = 0;
    /** @brief Set the key-press callback. */
    virtual void setKeyCallback(std::function<void(KeyCode key, bool pressed, const KeyModifiers& mods)> callback) = 0;
    /** @brief Set the text-input callback. */
    virtual void setCharCallback(std::function<void(unsigned int codepoint)> callback) = 0;
    /** @brief Set the resize callback. */
    virtual void setResizeCallback(std::function<void(int width, int height)> callback) = 0;
    /** @brief Set the close-request callback. */
    virtual void setCloseCallback(std::function<void()> callback) = 0;
    /** @brief Set the focus-change callback. */
    virtual void setFocusCallback(std::function<void(bool focused)> callback) = 0;
    /** @brief Set the DPI-change callback. */
    virtual void setDPIChangeCallback(std::function<void(float dpiScale)> callback) = 0;
};

// =============================================================================
// Platform Utilities Interface
// =============================================================================
class IPlatformUtils {
public:
    virtual ~IPlatformUtils() = default;

    struct SaveFileDialogOptions {
        /** @brief Window title presented by the native save dialog. */
        std::string title;
        /** @brief Filter string or pattern description understood by the platform backend. */
        std::string filter;
        /** @brief Initial full path or directory suggested to the save dialog. */
        std::string defaultPath;
        /** @brief Default extension appended when the backend supports it. */
        std::string defaultExtension;
    };

    /** @brief Get high-resolution wall-clock time in seconds. */
    virtual double getTime() const = 0; // High-resolution time in seconds
    /** @brief Sleep the current thread for the requested number of milliseconds. */
    virtual void sleep(int milliseconds) const = 0;

    /** @brief Open a native file-open dialog. */
    virtual std::string openFileDialog(const std::string& title, const std::string& filter) const = 0;
    /** @brief Open a native file-save dialog. */
    virtual std::string saveFileDialog(const SaveFileDialogOptions& options) const = 0;
    /** @brief Compatibility overload for simple save dialogs. */
    std::string saveFileDialog(const std::string& title, const std::string& filter) const {
        return saveFileDialog(SaveFileDialogOptions{title, filter, "", ""});
    }
    /** @brief Open a native folder-selection dialog. */
    virtual std::string selectFolderDialog(const std::string& title) const = 0;

    /** @brief Replace clipboard contents with UTF-8 text. */
    virtual void setClipboardText(const std::string& text) const = 0;
    /** @brief Read UTF-8 text from the clipboard. */
    virtual std::string getClipboardText() const = 0;

    /** @brief Get a short platform name used for diagnostics. */
    virtual std::string getPlatformName() const = 0;
    /** @brief Get the number of logical processors available to the process. */
    virtual int getProcessorCount() const = 0;
    /** @brief Get total system memory in bytes. */
    virtual size_t getSystemMemory() const = 0; // In bytes

    /** @brief Get the platform-specific application data directory for an app name. */
    virtual std::string
    getAppDataPath(const std::string& appName) const = 0; // Returns platform-specific app data directory
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
    static bool isInitialized();

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
    };
};

} // namespace Aestra
