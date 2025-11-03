// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUIPlatformBridge.h"
#include "../Core/NUITypes.h"
#include "../Core/NUIComponent.h"
#include "../Graphics/NUIRenderer.h"
#include "../../NomadCore/include/NomadLog.h"

namespace NomadUI {

NUIPlatformBridge::NUIPlatformBridge()
    : m_window(nullptr)
    , m_rootComponent(nullptr)
    , m_renderer(nullptr)
    , m_lastMouseX(0)
    , m_lastMouseY(0)
{
    // Initialize NomadPlat if not already done
    Nomad::Platform::initialize();
    
    // Create platform window
    m_window = Nomad::Platform::createWindow();
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
    Nomad::WindowDesc desc;
    desc.title = title;
    desc.width = width;
    desc.height = height;
    desc.startMaximized = startMaximized;
    return create(desc);
}

bool NUIPlatformBridge::create(const Nomad::WindowDesc& desc) {
    if (!m_window) return false;

    if (!m_window->create(desc)) {
        return false;
    }

    // Create OpenGL context automatically (for NomadUI compatibility)
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
    // Mouse move
    m_window->setMouseMoveCallback([this](int x, int y) {
        // Store mouse position for wheel events
        m_lastMouseX = x;
        m_lastMouseY = y;
        
        if (m_mouseMoveCallback) {
            m_mouseMoveCallback(x, y);
        }
        
        // Forward to root component for hover effects
        if (m_rootComponent) {
            NUIMouseEvent event;
            event.position = {static_cast<float>(x), static_cast<float>(y)};
            event.button = NUIMouseButton::None;
            event.pressed = false;
            event.released = false;
            event.wheelDelta = 0.0f;
            m_rootComponent->onMouseEvent(event);
        }
    });

    // Mouse button
    m_window->setMouseButtonCallback([this](Nomad::MouseButton button, bool pressed, int x, int y) {
        // Store mouse position for wheel events
        m_lastMouseX = x;
        m_lastMouseY = y;
        
        if (m_mouseButtonCallback) {
            m_mouseButtonCallback(convertMouseButton(button), pressed);
        }
        
        // Forward to root component for NomadUI event handling
        if (m_rootComponent) {
            NUIMouseEvent event;
            event.position = {static_cast<float>(x), static_cast<float>(y)};
            // Map button
            switch (button) {
                case Nomad::MouseButton::Left: event.button = NUIMouseButton::Left; break;
                case Nomad::MouseButton::Right: event.button = NUIMouseButton::Right; break;
                case Nomad::MouseButton::Middle: event.button = NUIMouseButton::Middle; break;
                default: event.button = NUIMouseButton::None; break;
            }
            event.pressed = pressed;
            event.released = !pressed;
            event.wheelDelta = 0.0f;
            m_rootComponent->onMouseEvent(event);
        }
    });

    // Mouse wheel
    m_window->setMouseWheelCallback([this](float delta) {
        if (m_mouseWheelCallback) {
            m_mouseWheelCallback(delta);
        }
        
        // Forward to root component for NomadUI event handling
        if (m_rootComponent) {
            NUIMouseEvent event;
            event.position = {static_cast<float>(m_lastMouseX), static_cast<float>(m_lastMouseY)};
            event.button = NUIMouseButton::None;
            event.pressed = false;
            event.released = false;
            event.wheelDelta = delta;
            m_rootComponent->onMouseEvent(event);
        }
    });

    // Key
    m_window->setKeyCallback([this](Nomad::KeyCode key, bool pressed, const Nomad::KeyModifiers& mods) {
        if (m_keyCallback) {
            m_keyCallback(convertKeyCode(key), pressed);
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
}

// =============================================================================
// Event Conversion
// =============================================================================

int NUIPlatformBridge::convertMouseButton(Nomad::MouseButton button) {
    return static_cast<int>(button);
}

int NUIPlatformBridge::convertKeyCode(Nomad::KeyCode key) {
    return static_cast<int>(key);
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
    return m_window ? m_window->pollEvents() : false;
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

void NUIPlatformBridge::setKeyCallback(std::function<void(int, bool)> callback) {
    m_keyCallback = callback;
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
    // Note: NomadPlat doesn't expose GL context handle directly
    // This is fine - NomadUI doesn't actually need it
    return nullptr;
}

// =============================================================================
// DPI Support
// =============================================================================

float NUIPlatformBridge::getDPIScale() const {
    return m_window ? m_window->getDPIScale() : 1.0f;
}

} // namespace NomadUI
