// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../../AestraPlat/include/AestraPlatform.h"
#include "NUICursorService.h"
#include "NUITypes.h"
#include <functional>

namespace AestraUI {

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
 * Bridge between AestraPlat and AestraUI
 * Wraps AestraPlat's IPlatformWindow to work with AestraUI's existing API
 */
class NUIPlatformBridge {
public:
    NUIPlatformBridge();
    ~NUIPlatformBridge();

    // Window creation and management (AestraUI-compatible API)
    bool create(const std::string& title, int width, int height, bool startMaximized = false);
    bool create(const Aestra::WindowDesc& desc);  // Full control version
    void destroy();
    void show();
    void hide();
    
    // Main loop
    bool processEvents();  // Returns false when window should close
    void swapBuffers();
    bool isWindowInteractive() const;
    
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
    /// Non-maximized geometry; false when unknown, leaving outputs untouched (#655).
    bool getRestoreBounds(int& x, int& y, int& width, int& height) const;
    void requestClose();  // Request window close through platform abstraction
    
    // Full screen support
    void toggleFullScreen();
    bool isFullScreen() const;
    void enterFullScreen();
    void exitFullScreen();
    
    // OpenGL context
    bool createGLContext();
    bool makeContextCurrent();
    
    // Event callbacks (AestraUI-style - simplified)
    void setMouseMoveCallback(std::function<void(int, int)> callback);
    void setMouseButtonCallback(std::function<void(int, bool)> callback);
    void setMouseWheelCallback(std::function<void(float)> callback);
    void setMousePositionFilter(std::function<void(int&, int&)> callback);
    void setKeyCallback(std::function<void(int, bool)> callback);
    void setKeyCallbackEx(std::function<void(int, bool, bool ctrl, bool shift, bool alt)> callback);
    void setCharCallback(std::function<void(unsigned int)> callback);
    void setResizeCallback(std::function<void(int, int)> callback);
    void setCloseCallback(std::function<void()> callback);
    void setDPIChangeCallback(std::function<void(float)> callback);
    void setFocusCallback(std::function<void(bool focused)> callback);
    void setHitTestCallback(Aestra::HitTestCallback callback);
    
    // AestraUI-specific: Root component
    void setRootComponent(NUIComponent* root) { m_rootComponent = root; }
    NUIComponent* getRootComponent() const { return m_rootComponent; }
    
    // AestraUI-specific: Renderer
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
    NUIPoint getCursorPosition() const;         // Get current absolute cursor position
    void setCursorStyle(NUICursorStyle style);  // Set cursor appearance
    NUICursorStyle getCursorStyle() const;       // Get current cursor style
    
    // Mouse Capture
    void setMouseCapture(bool captured);

    /**
     * Single owner of infinite-drag cursor capture (hide + confine + warp-back).
     * Continuous parameter controls call the begin/end/cancel wrappers below;
     * the service accessor is for state queries (isCaptured, anchor).
     */
    NUICursorService& cursorService() { return m_cursorService; }

    /**
     * Begin an infinite-drag capture owned by @p owner. While captured:
     *  - motion/button/wheel events route ONLY to the owner (the rest of the
     *    tree neither hit-tests nor hovers under the hidden pointer),
     *  - external setCursorStyle calls are ignored (no style-steal can unhide
     *    or unclip mid-drag),
     *  - the logical cursor position is pinned to the capture anchor.
     */
    void beginCursorCapture(NUIComponent* owner, NUICursorRestorePolicy policy, int x, int y);
    /** End the capture, restoring at (x, y) per the begin policy. */
    void endCursorCapture(int x, int y);
    /** Abort without warping (focus loss, owner teardown). */
    void cancelCursorCapture();
    /** True iff @p c owns the currently active capture (for owner-specific teardown). */
    bool isCursorCaptureOwner(const NUIComponent* c) const {
        return m_cursorService.isCaptured() && m_cursorCaptureOwner == c;
    }

private:
    // NUICursorHost backing for m_cursorService: hide/show ride the existing
    // style channel (which already clips/unclips and stamps cursorCaptured on
    // events); the explicit grab call is belt-and-braces confinement so the
    // service's contract holds even if the style path changes.
    class CursorHostImpl : public NUICursorHost {
    public:
        explicit CursorHostImpl(NUIPlatformBridge& bridge) : m_bridge(bridge) {}
        void hostHideCursor() override { m_bridge.applyCursorStyle(NUICursorStyle::Hidden); }
        void hostShowCursor() override { m_bridge.applyCursorStyle(NUICursorStyle::Arrow); }
        void hostWarpCursor(int x, int y) override { m_bridge.setCursorPosition(x, y); }
        void hostSetPointerGrab(bool grabbed) override {
            if (!m_bridge.m_window) return;
            if (grabbed) {
                // Confine to a SMALL rect around the anchor, not the whole
                // window — the title bar and in-window panels are inside the
                // window, so a whole-window clip lets the hidden pointer roam
                // over them (foreign hover, escape from the control). The
                // per-frame recenter keeps the pointer at the anchor; this rect
                // is the hard backstop that locks it to the control.
                const int ax = m_bridge.m_cursorService.anchorX();
                const int ay = m_bridge.m_cursorService.anchorY();
                constexpr int kHalf = 8; // 16x16 px lock box around the anchor
                m_bridge.m_window->setCursorClipRect(ax - kHalf, ay - kHalf, kHalf * 2, kHalf * 2);
            } else {
                m_bridge.m_window->setCursorClip(false);
            }
        }

    private:
        NUIPlatformBridge& m_bridge;
    };

    // Convert AestraPlat events to AestraUI events
    void setupEventBridges();
    int convertMouseButton(Aestra::MouseButton button);
    int convertKeyCode(Aestra::KeyCode key);
    NUIModifiers convertModifiers(const Aestra::KeyModifiers& mods);

    // AestraPlat window
    Aestra::IPlatformWindow* m_window;
    
    // AestraUI-specific state
    NUIComponent* m_rootComponent;
    NUIRenderer* m_renderer;
    
    // Mouse position tracking for wheel events
    int m_lastMouseX;
    int m_lastMouseY;
    bool m_capsLockLatched = false;
    
    // Cursor style tracking
    NUICursorStyle m_currentCursorStyle = NUICursorStyle::Arrow;

    // Cursor capture service (declaration order: host before service — the
    // service holds a reference to the host).
    CursorHostImpl m_cursorHost{*this};
    NUICursorService m_cursorService{m_cursorHost};
    // Component that owns the active capture; all mouse events route here
    // while the service is captured.
    NUIComponent* m_cursorCaptureOwner = nullptr;
    // Set when a capture ends/cancels: next processEvents() dispatches one
    // synthetic Move at the restored cursor position so hover and cursor
    // style re-resolve from where the cursor actually reappeared.
    bool m_pendingStyleResolve = false;

    // The style channel with no capture guard — used by the cursor host so the
    // service itself can hide/unhide while external calls are locked out.
    void applyCursorStyle(NUICursorStyle style);
    
    // AestraUI-style callbacks
    std::function<void(int, int)> m_mouseMoveCallback;
    std::function<void(int, bool)> m_mouseButtonCallback;
    std::function<void(float)> m_mouseWheelCallback;
    std::function<void(int&, int&)> m_mousePositionFilter;
    std::function<void(int, bool)> m_keyCallback;
    std::function<void(int, bool, bool, bool, bool)> m_keyCallbackEx;
    std::function<void(unsigned int)> m_charCallback;
    std::function<void(int, int)> m_resizeCallback;
    std::function<void()> m_closeCallback;
    std::function<void(float)> m_dpiChangeCallback;
    std::function<void(bool)> m_focusCallback;
};

} // namespace AestraUI
