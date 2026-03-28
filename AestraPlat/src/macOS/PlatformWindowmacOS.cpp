#include "PlatformWindowmacOS.h"

#include <SDL2/SDL_syswm.h>
#include <iostream>

namespace Aestra {

PlatformWindowmacOS::PlatformWindowmacOS() {
    // SDL_Init should be called by Platform::initialize()
}

PlatformWindowmacOS::~PlatformWindowmacOS() {
    destroy();
}

bool PlatformWindowmacOS::create(const WindowDesc& desc) {
    // Convert flags
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;
    if (desc.resizable)
        flags |= SDL_WINDOW_RESIZABLE;
    if (desc.startMaximized)
        flags |= SDL_WINDOW_MAXIMIZED;
    if (desc.startFullscreen)
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (!desc.decorated)
        flags |= SDL_WINDOW_BORDERLESS;

    // Position
    int x = (desc.x == -1) ? SDL_WINDOWPOS_CENTERED : desc.x;
    int y = (desc.y == -1) ? SDL_WINDOWPOS_CENTERED : desc.y;

    // GL Attributes - Request 3.3 Core for macOS compatibility
    // Note: macOS only supports Core profile, not Compatibility
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0); // No depth buffer needed for 2D UI
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    
    // Enable multisampling for smoother rendering on Retina displays
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    m_window = SDL_CreateWindow(desc.title.c_str(), x, y, desc.width, desc.height, flags);

    if (!m_window) {
        std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
        return false;
    }

    m_isFullscreen = desc.startFullscreen;

    // Initial DPI check for Retina
    updateDPIScale();

    return true;
}

void PlatformWindowmacOS::destroy() {
    if (m_glContext) {
        SDL_GL_DeleteContext(m_glContext);
        m_glContext = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
}

void PlatformWindowmacOS::updateDPIScale() {
    if (!m_window) return;
    
    int drawableWidth, drawableHeight;
    int windowWidth, windowHeight;
    
    SDL_GL_GetDrawableSize(m_window, &drawableWidth, &drawableHeight);
    SDL_GetWindowSize(m_window, &windowWidth, &windowHeight);
    
    if (windowWidth > 0) {
        float newScale = static_cast<float>(drawableWidth) / windowWidth;
        if (std::abs(newScale - m_dpiScale) > 0.01f) {
            m_dpiScale = newScale;
            if (m_dpiChangeCallback)
                m_dpiChangeCallback(m_dpiScale);
        }
    }
}

bool PlatformWindowmacOS::pollEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            if (m_closeCallback)
                m_closeCallback();
            return false;

        case SDL_WINDOWEVENT:
            if (e.window.windowID == SDL_GetWindowID(m_window)) {
                switch (e.window.event) {
                case SDL_WINDOWEVENT_RESIZED:
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                    if (m_resizeCallback)
                        m_resizeCallback(e.window.data1, e.window.data2);
                    updateDPIScale();
                    break;
                case SDL_WINDOWEVENT_CLOSE:
                    if (m_closeCallback)
                        m_closeCallback();
                    break;
                case SDL_WINDOWEVENT_FOCUS_GAINED:
                    if (m_focusCallback)
                        m_focusCallback(true);
                    break;
                case SDL_WINDOWEVENT_FOCUS_LOST:
                    if (m_focusCallback)
                        m_focusCallback(false);
                    break;
                case SDL_WINDOWEVENT_DISPLAY_CHANGED:
                    updateDPIScale();
                    break;
                }
            }
            break;

        case SDL_MOUSEMOTION:
            if (m_mouseMoveCallback)
                m_mouseMoveCallback(e.motion.x, e.motion.y);
            break;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            if (m_mouseButtonCallback) {
                MouseButton btn = MouseButton::Left;
                if (e.button.button == SDL_BUTTON_RIGHT)
                    btn = MouseButton::Right;
                else if (e.button.button == SDL_BUTTON_MIDDLE)
                    btn = MouseButton::Middle;
                m_mouseButtonCallback(btn, e.type == SDL_MOUSEBUTTONDOWN, e.button.x, e.button.y);
            }
            break;

        case SDL_MOUSEWHEEL:
            if (m_mouseWheelCallback) {
                float delta = e.wheel.preciseY;
                if (delta == 0.0f) // Fallback for older SDL
                    delta = static_cast<float>(e.wheel.y);
                m_mouseWheelCallback(delta);
            }
            break;

        case SDL_KEYDOWN:
        case SDL_KEYUP:
            if (m_keyCallback) {
                KeyCode key = translateKey(e.key.keysym.sym);
                KeyModifiers mods = getModifiers(e.key.keysym.mod);
                m_keyCallback(key, e.type == SDL_KEYDOWN, mods);
            }
            break;

        case SDL_TEXTINPUT:
            if (m_charCallback) {
                // Decode UTF-8 to codepoint
                const unsigned char* p = reinterpret_cast<const unsigned char*>(e.text.text);
                unsigned int codepoint = 0;
                
                if (*p < 0x80) {
                    codepoint = *p;
                } else if ((*p & 0xE0) == 0xC0) {
                    codepoint = (*p & 0x1F) << 6;
                    codepoint |= (*(p + 1) & 0x3F);
                } else if ((*p & 0xF0) == 0xE0) {
                    codepoint = (*p & 0x0F) << 12;
                    codepoint |= (*(p + 1) & 0x3F) << 6;
                    codepoint |= (*(p + 2) & 0x3F);
                } else if ((*p & 0xF8) == 0xF0) {
                    codepoint = (*p & 0x07) << 18;
                    codepoint |= (*(p + 1) & 0x3F) << 12;
                    codepoint |= (*(p + 2) & 0x3F) << 6;
                    codepoint |= (*(p + 3) & 0x3F);
                }
                
                if (codepoint > 0)
                    m_charCallback(codepoint);
            }
            break;
        }
    }
    return true;
}

void PlatformWindowmacOS::swapBuffers() {
    if (m_window)
        SDL_GL_SwapWindow(m_window);
}

void PlatformWindowmacOS::setTitle(const std::string& title) {
    if (m_window)
        SDL_SetWindowTitle(m_window, title.c_str());
}

void PlatformWindowmacOS::setSize(int width, int height) {
    if (m_window)
        SDL_SetWindowSize(m_window, width, height);
}

void PlatformWindowmacOS::getSize(int& width, int& height) const {
    if (m_window)
        SDL_GetWindowSize(m_window, &width, &height);
}

void PlatformWindowmacOS::setPosition(int x, int y) {
    if (m_window)
        SDL_SetWindowPosition(m_window, x, y);
}

void PlatformWindowmacOS::getPosition(int& x, int& y) const {
    if (m_window)
        SDL_GetWindowPosition(m_window, &x, &y);
}

void PlatformWindowmacOS::show() {
    if (m_window)
        SDL_ShowWindow(m_window);
}

void PlatformWindowmacOS::hide() {
    if (m_window)
        SDL_HideWindow(m_window);
}

void PlatformWindowmacOS::minimize() {
    if (m_window)
        SDL_MinimizeWindow(m_window);
}

void PlatformWindowmacOS::maximize() {
    if (m_window)
        SDL_MaximizeWindow(m_window);
}

void PlatformWindowmacOS::restore() {
    if (m_window)
        SDL_RestoreWindow(m_window);
}

bool PlatformWindowmacOS::isMaximized() const {
    if (!m_window)
        return false;
    Uint32 flags = SDL_GetWindowFlags(m_window);
    return (flags & SDL_WINDOW_MAXIMIZED) != 0;
}

bool PlatformWindowmacOS::isMinimized() const {
    if (!m_window)
        return false;
    Uint32 flags = SDL_GetWindowFlags(m_window);
    return (flags & SDL_WINDOW_MINIMIZED) != 0;
}

void PlatformWindowmacOS::requestClose() {
    if (m_closeCallback)
        m_closeCallback();
}

void PlatformWindowmacOS::setFullscreen(bool fullscreen) {
    if (m_window) {
        SDL_SetWindowFullscreen(m_window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        m_isFullscreen = fullscreen;
    }
}

bool PlatformWindowmacOS::isFullscreen() const {
    return m_isFullscreen;
}

bool PlatformWindowmacOS::createGLContext() {
    if (!m_window)
        return false;
    m_glContext = SDL_GL_CreateContext(m_window);
    if (!m_glContext) {
        std::cerr << "SDL_GL_CreateContext Error: " << SDL_GetError() << std::endl;
        return false;
    }
    return true;
}

bool PlatformWindowmacOS::makeContextCurrent() {
    if (!m_window || !m_glContext)
        return false;
    return SDL_GL_MakeCurrent(m_window, m_glContext) == 0;
}

void PlatformWindowmacOS::setVSync(bool enabled) {
    SDL_GL_SetSwapInterval(enabled ? 1 : 0);
}

void* PlatformWindowmacOS::getNativeHandle() const {
    if (!m_window)
        return nullptr;
    
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(m_window, &info)) {
#if defined(SDL_VIDEO_DRIVER_COCOA)
        return info.info.cocoa.window;
#endif
    }
    return nullptr;
}

void* PlatformWindowmacOS::getNativeDisplayHandle() const {
    // macOS doesn't use display handles like X11
    return nullptr;
}

float PlatformWindowmacOS::getDPIScale() const {
    return m_dpiScale;
}

void PlatformWindowmacOS::setCursorVisible(bool visible) {
    SDL_ShowCursor(visible ? SDL_ENABLE : SDL_DISABLE);
}

void PlatformWindowmacOS::setCursorPosition(int x, int y) {
    SDL_WarpMouseGlobal(x, y);
}

void PlatformWindowmacOS::setMouseCapture(bool captured) {
    m_mouseCaptured = captured;
    SDL_SetWindowGrab(m_window, captured ? SDL_TRUE : SDL_FALSE);
    SDL_SetRelativeMouseMode(captured ? SDL_TRUE : SDL_FALSE);
}

KeyModifiers PlatformWindowmacOS::getCurrentModifiers() const {
    return getModifiers(SDL_GetModState());
}

// Helpers
KeyCode PlatformWindowmacOS::translateKey(SDL_Keycode key) {
    // Map SDL keys to Aestra KeyCode
    if (key >= 'a' && key <= 'z')
        return static_cast<KeyCode>(static_cast<int>(KeyCode::A) + (key - 'a'));
    if (key >= '0' && key <= '9')
        return static_cast<KeyCode>(static_cast<int>(KeyCode::Num0) + (key - '0'));

    switch (key) {
    case SDLK_ESCAPE:
        return KeyCode::Escape;
    case SDLK_TAB:
        return KeyCode::Tab;
    case SDLK_SPACE:
        return KeyCode::Space;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return KeyCode::Enter;
    case SDLK_BACKSPACE:
        return KeyCode::Backspace;
    case SDLK_DELETE:
        return KeyCode::Delete;
    case SDLK_INSERT:
        return KeyCode::Insert;
    case SDLK_HOME:
        return KeyCode::Home;
    case SDLK_END:
        return KeyCode::End;
    case SDLK_PAGEUP:
        return KeyCode::PageUp;
    case SDLK_PAGEDOWN:
        return KeyCode::PageDown;
    case SDLK_UP:
        return KeyCode::Up;
    case SDLK_DOWN:
        return KeyCode::Down;
    case SDLK_LEFT:
        return KeyCode::Left;
    case SDLK_RIGHT:
        return KeyCode::Right;
    case SDLK_CAPSLOCK:
        return KeyCode::CapsLock;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
        return KeyCode::Shift;
    case SDLK_LCTRL:
    case SDLK_RCTRL:
        return KeyCode::Control;
    case SDLK_LALT:
    case SDLK_RALT:
        return KeyCode::Alt;
    case SDLK_LGUI:
    case SDLK_RGUI:
        return KeyCode::Unknown;
    case SDLK_F1:
        return KeyCode::F1;
    case SDLK_F2:
        return KeyCode::F2;
    case SDLK_F3:
        return KeyCode::F3;
    case SDLK_F4:
        return KeyCode::F4;
    case SDLK_F5:
        return KeyCode::F5;
    case SDLK_F6:
        return KeyCode::F6;
    case SDLK_F7:
        return KeyCode::F7;
    case SDLK_F8:
        return KeyCode::F8;
    case SDLK_F9:
        return KeyCode::F9;
    case SDLK_F10:
        return KeyCode::F10;
    case SDLK_F11:
        return KeyCode::F11;
    case SDLK_F12:
        return KeyCode::F12;
    default:
        return KeyCode::Unknown;
    }
}

KeyModifiers PlatformWindowmacOS::getModifiers(Uint16 mod) const {
    KeyModifiers m;
    m.shift = (mod & KMOD_SHIFT) != 0;
    m.control = (mod & KMOD_CTRL) != 0;
    m.alt = (mod & KMOD_ALT) != 0;
    m.super = (mod & KMOD_GUI) != 0;
    m.capsLock = (mod & KMOD_CAPS) != 0;
    return m;
}

} // namespace Aestra
