// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUIPlatformBridge.h"
#include <cstdio>
#include "NUITypes.h"
#include "NUIComponent.h"
#include "NUIRenderer.h"
#include "../../AestraCore/include/AestraLog.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <utility>

namespace AestraUI {

NUIPlatformBridge::NUIPlatformBridge()
    : m_window(nullptr)
    , m_rootComponent(nullptr)
    , m_renderer(nullptr)
    , m_lastMouseX(0)
    , m_lastMouseY(0)
{
    // Initialize AestraPlat if not already done
    Aestra::Platform::initialize();
    
    // Create platform window
    m_window = Aestra::Platform::createWindow();
}

NUIPlatformBridge::~NUIPlatformBridge() {
    destroy();
    if (m_window) {
        delete m_window;
        m_window = nullptr;
    }
}

// =============================================================================
// Window Creation
// =============================================================================

bool NUIPlatformBridge::create(const std::string& title, int width, int height, bool startMaximized) {
    Aestra::WindowDesc desc;
    desc.title = title;
    desc.width = width;
    desc.height = height;
    desc.startMaximized = startMaximized;
    return create(desc);
}

bool NUIPlatformBridge::create(const Aestra::WindowDesc& desc) {
    if (!m_window) return false;

    if (!m_window->create(desc)) {
        return false;
    }

    // Create OpenGL context automatically (for AestraUI compatibility)
    if (!m_window->createGLContext()) {
        return false;
    }

    if (!m_window->makeContextCurrent()) {
        return false;
    }

    setupEventBridges();
    return true;
}

void NUIPlatformBridge::destroy() {
    if (m_window) {
        m_window->destroy();
    }
}

// =============================================================================
// Event Bridges
// =============================================================================

void NUIPlatformBridge::setupEventBridges() {
    m_capsLockLatched = m_window && m_window->getCurrentModifiers().capsLock;
    // CALLBACK ORDER CONTRACT: This bridge handler fires FIRST on every mouse move
    // (cache update, flag checks, cursorCaptured setup). Then the external
    // m_mouseMoveCallback (set by AestraWindowManager) fires SECOND.
    // Do not flatten or reorder — bridge state must be current before
    // AWM routing logic reads it. AWM registration is at line 194 of
    // AestraWindowManager.cpp.
    m_window->setMouseMoveCallback([this](int x, int y) {
        if (m_mousePositionFilter) {
            m_mousePositionFilter(x, y);
        }

        if (!isWindowInteractive()) {
            return;
        }

        // Calculate delta from last position
        // Note: When SDL_SetRelativeMouseMode is enabled, x/y are already relative deltas
        // In that case, this calculation will give us the change in relative motion (usually zero or small)
        // which is acceptable for components that don't use cursor capture
        float deltaX = static_cast<float>(x - m_lastMouseX);
        float deltaY = static_cast<float>(y - m_lastMouseY);

        // Store mouse position for wheel events and delta calculation
        m_lastMouseX = x;
        m_lastMouseY = y;

        const bool rootInputBlocked = m_rootInputBlockedCallback && m_rootInputBlockedCallback();

        if (m_mouseMoveCallback) {
            // Skip external callback during cursor capture to prevent hover effects,
            // cursor icon changes, and tooltip triggers in AestraWindowManager.
            if (m_currentCursorStyle != NUICursorStyle::Hidden) {
                m_mouseMoveCallback(x, y);
            }
        }

        if (rootInputBlocked) {
            return;
        }

        // Forward to root component for hover effects
        if (m_rootComponent) {
            NUIMouseEvent event;
            event.type = NUIMouseEventType::Move;
            event.button = NUIMouseButton::None;
            event.pressed = false;
            event.released = false;
            event.wheelDelta = 0.0f;
            event.cursorCaptured = (m_currentCursorStyle == NUICursorStyle::Hidden);
            if (m_window) {
                auto mods = m_window->getCurrentModifiers();
                mods.capsLock = mods.capsLock || m_capsLockLatched;
                event.modifiers = convertModifiers(mods);
            }

            if (m_cursorService.isCaptured() && m_cursorCaptureOwner) {
                // Captured: the service owns delta. It computes the semantic
                // drag delta from raw physical motion and recenters the pointer
                // to the anchor (so it can never reach the window edge). The
                // OS pointer is a pure implementation detail here — the owner
                // sees an anchored position and a service-computed delta only.
                const auto d = m_cursorService.feedPhysicalMotion(x, y);
                event.position = {static_cast<float>(m_cursorService.anchorX()),
                                  static_cast<float>(m_cursorService.anchorY())};
                event.delta = {static_cast<float>(d.dx), static_cast<float>(d.dy)};
                // Routed dispatch: ONLY the capture owner sees motion.
                NUIComponent::dispatchMouseEvent(m_cursorCaptureOwner, event);
            } else {
                event.position = {static_cast<float>(x), static_cast<float>(y)};
                event.delta = {deltaX, deltaY};
                NUIComponent::dispatchMouseEvent(m_rootComponent, event);
            }
        }
    });

    // Mouse button
    m_window->setMouseButtonCallback([this](Aestra::MouseButton button, bool pressed, int x, int y) {
        if (m_mousePositionFilter) {
            m_mousePositionFilter(x, y);
        }

        if (!isWindowInteractive()) {
            return;
        }

        // Store mouse position for wheel events
        m_lastMouseX = x;
        m_lastMouseY = y;
        
        // Snapshot modal ownership before the external callback. A button
        // release may close the modal; that same release must still never be
        // forwarded to the application underneath it.
        const bool rootInputBlocked = m_rootInputBlockedCallback && m_rootInputBlockedCallback();

        if (m_mouseButtonCallback) {
            m_mouseButtonCallback(convertMouseButton(button), pressed);
        }

        if (rootInputBlocked) {
            return;
        }
        
        // Forward to root component for AestraUI event handling
        if (m_rootComponent) {
            NUIMouseEvent event;
            event.type = pressed ? NUIMouseEventType::Down : NUIMouseEventType::Up;
            event.position = {static_cast<float>(x), static_cast<float>(y)};
            // Map button
            switch (button) {
            case Aestra::MouseButton::Left:
                event.button = NUIMouseButton::Left;
                break;
            case Aestra::MouseButton::Right:
                event.button = NUIMouseButton::Right;
                break;
            case Aestra::MouseButton::Middle:
                event.button = NUIMouseButton::Middle;
                break;
            default:
                event.button = NUIMouseButton::None;
                break;
            }
            event.pressed = pressed;
            event.released = !pressed;
            event.wheelDelta = 0.0f;
            event.cursorCaptured = (m_currentCursorStyle == NUICursorStyle::Hidden);
            if (m_window) {
                auto mods = m_window->getCurrentModifiers();
                mods.capsLock = mods.capsLock || m_capsLockLatched;
                event.modifiers = convertModifiers(mods);
            }
            if (m_cursorService.isCaptured() && m_cursorCaptureOwner) {
                NUIComponent::dispatchMouseEvent(m_cursorCaptureOwner, event);
            } else {
                NUIComponent::dispatchMouseEvent(m_rootComponent, event);
            }
        }
    });

    // Mouse wheel
    m_window->setMouseWheelCallback([this](float delta) {
        if (!isWindowInteractive()) {
            return;
        }

        if (m_rootInputBlockedCallback && m_rootInputBlockedCallback()) {
            return;
        }

        if (m_mouseWheelCallback) {
            m_mouseWheelCallback(delta);
        }
        
        // Forward to root component for AestraUI event handling
        if (m_rootComponent) {
            NUIMouseEvent event;
            event.type = NUIMouseEventType::Scroll;
            event.position = m_cursorService.isCaptured()
                ? NUIPoint{static_cast<float>(m_cursorService.anchorX()),
                           static_cast<float>(m_cursorService.anchorY())}
                : NUIPoint{static_cast<float>(m_lastMouseX), static_cast<float>(m_lastMouseY)};
            event.button = NUIMouseButton::None;
            event.pressed = false;
            event.released = false;
            event.wheelDelta = delta;
            event.cursorCaptured = (m_currentCursorStyle == NUICursorStyle::Hidden);
            // Query current modifier state for Shift+scroll zoom support
            if (m_window) {
                auto mods = m_window->getCurrentModifiers();
                mods.capsLock = mods.capsLock || m_capsLockLatched;
                event.modifiers = convertModifiers(mods);
            }
            // Intentional: wheel during capture adjusts the CAPTURED control
            // (anchored position above); it must never leak to whatever sits
            // under the hidden physical pointer.
            if (m_cursorService.isCaptured() && m_cursorCaptureOwner) {
                NUIComponent::dispatchMouseEvent(m_cursorCaptureOwner, event);
            } else {
                NUIComponent::dispatchMouseEvent(m_rootComponent, event);
            }
        }
    });

    // Key
    m_window->setKeyCallback([this](Aestra::KeyCode key, bool pressed, const Aestra::KeyModifiers& mods) {
        if (key == Aestra::KeyCode::CapsLock && pressed) {
            m_capsLockLatched = !m_capsLockLatched;
        }
        if (mods.capsLock) {
            m_capsLockLatched = true;
        }
        if (m_keyCallback) {
            m_keyCallback(convertKeyCode(key), pressed);
        }
        if (m_keyCallbackEx) {
            m_keyCallbackEx(convertKeyCode(key), pressed, mods.control, mods.shift, mods.alt);
        }
    });

    m_window->setCharCallback([this](unsigned int codepoint) {
        if (m_rootInputBlockedCallback && m_rootInputBlockedCallback()) {
            return;
        }
        if (m_charCallback) {
            m_charCallback(codepoint);
        }
    });

    // Resize
    m_window->setResizeCallback([this](int width, int height) {
        if (m_resizeCallback) {
            m_resizeCallback(width, height);
        }
        
        // Update root component bounds on resize
        if (m_rootComponent) {
            m_rootComponent->setBounds(NUIRect(0, 0, width, height));
        }
        
        // Update renderer viewport
        if (m_renderer) {
            m_renderer->resize(width, height);
        }
    });

    // Close
    m_window->setCloseCallback([this]() {
        if (m_closeCallback) {
            m_closeCallback();
        }
    });

    // DPI change
    m_window->setDPIChangeCallback([this](float dpiScale) {
        if (m_dpiChangeCallback) {
            m_dpiChangeCallback(dpiScale);
        }
        
        // Update renderer if needed
        if (m_renderer) {
            // Renderer can handle DPI scaling internally
            int width, height;
            m_window->getSize(width, height);
            m_renderer->resize(width, height);
        }
    });

    // Window focus
    m_window->setFocusCallback([this](bool focused) {
        // Focus loss mid-capture: the release event may never arrive and a
        // warp can no longer land. Cancel (unhide in place, drop confinement)
        // BEFORE forwarding, so downstream focus handling sees a sane cursor.
        if (!focused && m_cursorService.isCaptured()) {
            // Cancel FIRST (unhide in place, drop confinement, NO warp — a
            // warp cannot land without focus), THEN let the owner tear down
            // its drag state with a synthetic release at the anchor. Because
            // the service is already idle, the owner's normal release path
            // calling endCursorCapture() is a harmless no-op — the no-warp
            // guarantee of focus-loss cancellation survives the owner's own
            // cleanup logic.
            const int anchorX = m_cursorService.anchorX();
            const int anchorY = m_cursorService.anchorY();
            NUIComponent* owner = m_cursorCaptureOwner;
            cancelCursorCapture();
            if (owner) {
                NUIMouseEvent event;
                event.type = NUIMouseEventType::Up;
                event.position = {static_cast<float>(anchorX), static_cast<float>(anchorY)};
                event.button = NUIMouseButton::Left;
                event.pressed = false;
                event.released = true;
                event.wheelDelta = 0.0f;
                event.cursorCaptured = true;
                event.synthetic = true; // "stop", not "accept" — see NUIMouseEvent
                NUIComponent::dispatchMouseEvent(owner, event);
            }
        }
        if (m_focusCallback) {
            m_focusCallback(focused);
        }
    });
}

// =============================================================================
// Event Conversion
// =============================================================================

int NUIPlatformBridge::convertMouseButton(Aestra::MouseButton button) {
    return static_cast<int>(button);
}

int NUIPlatformBridge::convertKeyCode(Aestra::KeyCode key) {
    return static_cast<int>(key);
}

NUIModifiers NUIPlatformBridge::convertModifiers(const Aestra::KeyModifiers& mods) {
    NUIModifiers result = NUIModifiers::None;
    if (mods.shift) result = result | NUIModifiers::Shift;
    if (mods.control) result = result | NUIModifiers::Ctrl;
    if (mods.alt) result = result | NUIModifiers::Alt;
    if (mods.super) result = result | NUIModifiers::Super;
    if (mods.capsLock) result = result | NUIModifiers::CapsLock;
    return result;
}

// =============================================================================
// Window Management
// =============================================================================

void NUIPlatformBridge::show() {
    if (m_window) m_window->show();
}

void NUIPlatformBridge::hide() {
    if (m_window) m_window->hide();
}

bool NUIPlatformBridge::processEvents() {
    // Post-capture style resolution: after end/cancel the cursor reappeared
    // somewhere (knob center, thumb, or wherever cancel left it), but no real
    // motion has occurred — a resize handle or text field under that point
    // would show a stale Arrow until the mouse moves. Dispatch one synthetic
    // Move at the restored position so hover and cursor style re-resolve.
    // Deferred to here (not done inside end/cancel) so the tree is never
    // re-entered from within the owner's own release handling.
    if (m_pendingStyleResolve && !m_cursorService.isCaptured()) {
        m_pendingStyleResolve = false;
        if (m_rootComponent) {
            NUIMouseEvent event;
            event.type = NUIMouseEventType::Move;
            event.position = getCursorPosition();
            event.button = NUIMouseButton::None;
            event.synthetic = true;
            if (m_window) {
                auto mods = m_window->getCurrentModifiers();
                mods.capsLock = mods.capsLock || m_capsLockLatched;
                event.modifiers = convertModifiers(mods);
            }
            NUIComponent::dispatchMouseEvent(m_rootComponent, event);
        }
    }
    return m_window ? m_window->pollEvents() : false;
}

bool NUIPlatformBridge::isWindowInteractive() const {
    return m_window && m_window->isVisible() && m_window->isMapped();
}

void NUIPlatformBridge::swapBuffers() {
    if (m_window) m_window->swapBuffers();
}

// =============================================================================
// Window Properties
// =============================================================================

void NUIPlatformBridge::setTitle(const std::string& title) {
    if (m_window) m_window->setTitle(title);
}

void NUIPlatformBridge::setSize(int width, int height) {
    if (m_window) m_window->setSize(width, height);
}

void NUIPlatformBridge::getSize(int& width, int& height) const {
    if (m_window) m_window->getSize(width, height);
}

void NUIPlatformBridge::setPosition(int x, int y) {
    if (m_window) m_window->setPosition(x, y);
}

void NUIPlatformBridge::getPosition(int& x, int& y) const {
    if (m_window) m_window->getPosition(x, y);
}

// =============================================================================
// Window Controls
// =============================================================================

void NUIPlatformBridge::minimize() {
    if (m_window) m_window->minimize();
}

void NUIPlatformBridge::maximize() {
    if (m_window) m_window->maximize();
}

void NUIPlatformBridge::restore() {
    if (m_window) m_window->restore();
}

bool NUIPlatformBridge::isMaximized() const {
    return m_window ? m_window->isMaximized() : false;
}

bool NUIPlatformBridge::getRestoreBounds(int& x, int& y, int& width, int& height) const {
    return m_window ? m_window->getRestoreBounds(x, y, width, height) : false;
}

void NUIPlatformBridge::requestClose() {
    if (m_window) {
        m_window->requestClose();
    }
}

// =============================================================================
// Fullscreen
// =============================================================================

void NUIPlatformBridge::toggleFullScreen() {
    if (m_window) {
        m_window->setFullscreen(!m_window->isFullscreen());
    }
}

bool NUIPlatformBridge::isFullScreen() const {
    return m_window ? m_window->isFullscreen() : false;
}

void NUIPlatformBridge::enterFullScreen() {
    if (m_window) m_window->setFullscreen(true);
}

void NUIPlatformBridge::exitFullScreen() {
    if (m_window) m_window->setFullscreen(false);
}

// =============================================================================
// OpenGL Context
// =============================================================================

bool NUIPlatformBridge::createGLContext() {
    return m_window ? m_window->createGLContext() : false;
}

bool NUIPlatformBridge::makeContextCurrent() {
    return m_window ? m_window->makeContextCurrent() : false;
}

// =============================================================================
// Event Callbacks
// =============================================================================

void NUIPlatformBridge::setMouseMoveCallback(std::function<void(int, int)> callback) {
    m_mouseMoveCallback = callback;
}

void NUIPlatformBridge::setMouseButtonCallback(std::function<void(int, bool)> callback) {
    m_mouseButtonCallback = callback;
}

void NUIPlatformBridge::setMouseWheelCallback(std::function<void(float)> callback) {
    m_mouseWheelCallback = callback;
}

void NUIPlatformBridge::setRootInputBlockedCallback(std::function<bool()> callback) {
    m_rootInputBlockedCallback = std::move(callback);
}

void NUIPlatformBridge::setMousePositionFilter(std::function<void(int&, int&)> callback) {
    m_mousePositionFilter = std::move(callback);
}

void NUIPlatformBridge::setKeyCallback(std::function<void(int, bool)> callback) {
    m_keyCallback = callback;
}

void NUIPlatformBridge::setKeyCallbackEx(std::function<void(int, bool, bool ctrl, bool shift, bool alt)> callback) {
    m_keyCallbackEx = callback;
}

void NUIPlatformBridge::setCharCallback(std::function<void(unsigned int)> callback) {
    m_charCallback = std::move(callback);
}

void NUIPlatformBridge::setResizeCallback(std::function<void(int, int)> callback) {
    m_resizeCallback = callback;
}

void NUIPlatformBridge::setCloseCallback(std::function<void()> callback) {
    m_closeCallback = callback;
}

void NUIPlatformBridge::setDPIChangeCallback(std::function<void(float)> callback) {
    m_dpiChangeCallback = callback;
}

void NUIPlatformBridge::setFocusCallback(std::function<void(bool)> callback) {
    m_focusCallback = callback;
}

void NUIPlatformBridge::setHitTestCallback(Aestra::HitTestCallback callback) {
    if (m_window) {
        m_window->setHitTestCallback(callback);
    }
}

// =============================================================================
// Native Handles
// =============================================================================

void* NUIPlatformBridge::getNativeHandle() const {
    return m_window ? m_window->getNativeHandle() : nullptr;
}

void* NUIPlatformBridge::getNativeDeviceContext() const {
    return m_window ? m_window->getNativeDisplayHandle() : nullptr;
}

void* NUIPlatformBridge::getNativeGLContext() const {
    // Note: AestraPlat doesn't expose GL context handle directly
    // This is fine - AestraUI doesn't actually need it
    return nullptr;
}

// =============================================================================
// DPI Support
// =============================================================================

float NUIPlatformBridge::getDPIScale() const {
    return m_window ? m_window->getDPIScale() : 1.0f;
}

void NUIPlatformBridge::setCursorVisible(bool visible) {
    if (m_window) {
        m_window->setCursorVisible(visible);
    }
}

void NUIPlatformBridge::setCursorPosition(int x, int y) {
    if (m_window) {
        m_window->setCursorPosition(x, y);
    }
    m_lastMouseX = x;
    m_lastMouseY = y;
}

NUIPoint NUIPlatformBridge::getCursorPosition() const {
    // While captured, the logical cursor is pinned to the capture anchor —
    // the physical pointer only exists to produce deltas.
    if (m_cursorService.isCaptured()) {
        return NUIPoint(static_cast<float>(m_cursorService.anchorX()),
                        static_cast<float>(m_cursorService.anchorY()));
    }
    int x = 0, y = 0;
    if (m_window) {
        m_window->getCursorPosition(x, y);
    }
    return NUIPoint(static_cast<float>(x), static_cast<float>(y));
}

void NUIPlatformBridge::beginCursorCapture(NUIComponent* owner, NUICursorRestorePolicy policy, int x, int y) {
    // Owner is registered only if the service accepts the capture (it refuses
    // reentrant begins during an end/cancel transition) — routing must never
    // point at an owner whose capture never started.
    if (m_cursorService.beginDragCapture(policy, x, y)) {
        m_cursorCaptureOwner = owner;
    }
}

void NUIPlatformBridge::endCursorCapture(int x, int y) {
    m_cursorService.endDragCapture(x, y);
    m_cursorCaptureOwner = nullptr;
    m_pendingStyleResolve = true;
}

void NUIPlatformBridge::cancelCursorCapture() {
    m_cursorService.cancelDragCapture();
    m_cursorCaptureOwner = nullptr;
    m_pendingStyleResolve = true;
}

void NUIPlatformBridge::setMouseCapture(bool captured) {
    if (m_window) {
        m_window->setMouseCapture(captured);
    }
}

void NUIPlatformBridge::setCursorStyle(NUICursorStyle style) {
    // Style-steal guard: while a drag capture is active, widgets under the
    // hidden wandering pointer must not change the cursor style — doing so
    // would unhide/unclip mid-drag and break the capture. The service itself
    // bypasses this via applyCursorStyle (see CursorHostImpl).
    if (m_cursorService.isCaptured() && style != NUICursorStyle::Hidden) {
        return;
    }
    applyCursorStyle(style);
}

void NUIPlatformBridge::applyCursorStyle(NUICursorStyle style) {
    m_currentCursorStyle = style;
    
#ifdef _WIN32
    HCURSOR cursor = NULL;

    // Use system cursor IDs directly - LoadCursor auto-selects A/W
    switch (style) {
        case NUICursorStyle::Arrow:      cursor = ::LoadCursor(NULL, IDC_ARROW); break;
        case NUICursorStyle::Hand:       cursor = ::LoadCursor(NULL, IDC_HAND); break;
        case NUICursorStyle::IBeam:      cursor = ::LoadCursor(NULL, IDC_IBEAM); break;
        case NUICursorStyle::Wait:       cursor = ::LoadCursor(NULL, IDC_WAIT); break;
        case NUICursorStyle::WaitArrow:  cursor = ::LoadCursor(NULL, IDC_APPSTARTING); break;
        case NUICursorStyle::Crosshair:  cursor = ::LoadCursor(NULL, IDC_CROSS); break;
        case NUICursorStyle::ResizeNS:   cursor = ::LoadCursor(NULL, IDC_SIZENS); break;
        case NUICursorStyle::ResizeEW:   cursor = ::LoadCursor(NULL, IDC_SIZEWE); break;
        case NUICursorStyle::ResizeNESW: cursor = ::LoadCursor(NULL, IDC_SIZENESW); break;
        case NUICursorStyle::ResizeNWSE: cursor = ::LoadCursor(NULL, IDC_SIZENWSE); break;
        case NUICursorStyle::ResizeAll:  cursor = ::LoadCursor(NULL, IDC_SIZEALL); break;
        case NUICursorStyle::NotAllowed: cursor = ::LoadCursor(NULL, IDC_NO); break;
        case NUICursorStyle::Grab:       cursor = ::LoadCursor(NULL, IDC_HAND); break;
        case NUICursorStyle::Grabbing:   cursor = :: LoadCursor(NULL, IDC_HAND); break;
        case NUICursorStyle::Hidden:
            NUIComponent::setCursorCaptureActive(true);
            // Don't re-assert a whole-window clip while a drag capture owns the
            // pointer — the service confines to a small anchor rect (see
            // hostSetPointerGrab); a whole-window clip here would clobber it.
            if (m_window && !m_cursorService.isCaptured()) m_window->setCursorClip(true);
            ::SetCursor(NULL);
            return;
        default: cursor = ::LoadCursor(NULL, IDC_ARROW); break;
    }

    if (cursor) {
        ::SetCursor(cursor);
    }
    if (m_window) m_window->setCursorClip(false);
    NUIComponent::setCursorCaptureActive(false);
#else
    // Linux/macOS: handle Hidden style via SDL
    if (style == NUICursorStyle::Hidden) {
        NUIComponent::setCursorCaptureActive(true);
        if (m_window) {
            m_window->setCursorVisible(false);
            // See Win32 branch: the service owns a small anchor-rect clip during
            // capture; don't clobber it with a whole-window clip.
            if (!m_cursorService.isCaptured()) m_window->setCursorClip(true);
        }
        return;
    }
    if (m_window) m_window->setCursorClip(false);
    NUIComponent::setCursorCaptureActive(false);
#endif
}

NUICursorStyle NUIPlatformBridge::getCursorStyle() const {
    return m_currentCursorStyle;
}

} // namespace AestraUI
