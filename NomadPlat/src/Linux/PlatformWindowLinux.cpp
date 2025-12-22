#include "PlatformWindowLinux.h"
#include <iostream>
#include <SDL2/SDL_syswm.h>

namespace Nomad {

PlatformWindowLinux::PlatformWindowLinux() {
    // SDL_Init should be called by Platform::initialize()
}

PlatformWindowLinux::~PlatformWindowLinux() {
    destroy();
}

bool PlatformWindowLinux::create(const WindowDesc& desc) {
    // Convert flags
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;
    if (desc.resizable) flags |= SDL_WINDOW_RESIZABLE;
    if (desc.startMaximized) flags |= SDL_WINDOW_MAXIMIZED;
    if (desc.startFullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (!desc.decorated) flags |= SDL_WINDOW_BORDERLESS;

    // Position
    int x = (desc.x == -1) ? SDL_WINDOWPOS_CENTERED : desc.x;
    int y = (desc.y == -1) ? SDL_WINDOWPOS_CENTERED : desc.y;

    // GL Attributes - Request generic 3.3 Core, can be upgraded by user config if needed
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    m_window = SDL_CreateWindow(desc.title.c_str(), x, y, desc.width, desc.height, flags);

    if (!m_window) {
        std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
        return false;
    }

    m_isFullscreen = desc.startFullscreen;

    // Initial DPI check
    int dw, dh;
    SDL_GL_GetDrawableSize(m_window, &dw, &dh);
    int w, h;
    SDL_GetWindowSize(m_window, &w, &h);
    if (w > 0) {
        m_dpiScale = (float)dw / w;
    }

    return true;
}

void PlatformWindowLinux::destroy() {
    if (m_glContext) {
        SDL_GL_DeleteContext(m_glContext);
        m_glContext = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
}

bool PlatformWindowLinux::pollEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                if (m_closeCallback) m_closeCallback();
                return false; // Or let the app decide via callback

            case SDL_WINDOWEVENT:
                if (e.window.windowID == SDL_GetWindowID(m_window)) {
                    switch (e.window.event) {
                        case SDL_WINDOWEVENT_RESIZED:
                        case SDL_WINDOWEVENT_SIZE_CHANGED:
                            if (m_resizeCallback) m_resizeCallback(e.window.data1, e.window.data2);
                            {
                                // Check DPI change
                                int dw, dh;
                                SDL_GL_GetDrawableSize(m_window, &dw, &dh);
                                float newScale = (float)dw / e.window.data1;
                                if (abs(newScale - m_dpiScale) > 0.01f) {
                                    m_dpiScale = newScale;
                                    if (m_dpiChangeCallback) m_dpiChangeCallback(m_dpiScale);
                                }
                            }
                            break;
                        case SDL_WINDOWEVENT_CLOSE:
                            if (m_closeCallback) m_closeCallback();
                            break;
                        case SDL_WINDOWEVENT_FOCUS_GAINED:
                            if (m_focusCallback) m_focusCallback(true);
                            break;
                        case SDL_WINDOWEVENT_FOCUS_LOST:
                            if (m_focusCallback) m_focusCallback(false);
                            break;
                    }
                }
                break;

            case SDL_MOUSEMOTION:
                if (m_mouseMoveCallback) m_mouseMoveCallback(e.motion.x, e.motion.y);
                break;

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                if (m_mouseButtonCallback) {
                    MouseButton btn = MouseButton::Left;
                    if (e.button.button == SDL_BUTTON_RIGHT) btn = MouseButton::Right;
                    else if (e.button.button == SDL_BUTTON_MIDDLE) btn = MouseButton::Middle;
                    m_mouseButtonCallback(btn, e.type == SDL_MOUSEBUTTONDOWN, e.button.x, e.button.y);
                }
                break;

            case SDL_MOUSEWHEEL:
                if (m_mouseWheelCallback) m_mouseWheelCallback(e.wheel.y); // Vertical scroll
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
                    // Primitive UTF-8 decoding for single codepoint (simplified)
                    // In real app, might need full UTF8 decoder, but SDL gives char array
                    // For now pass the first byte if ASCII, or handle full utf8
                    // Nomads might expect unicode. e.text.text is char[32] utf8
                    // We'll just cast first byte for now if we don't have a decoder handy, 
                    // or implement a quick one.
                     unsigned char* p = (unsigned char*)e.text.text;
                     if (*p < 0x80) m_charCallback(*p);
                     // TODO: Proper UTF8 decoding
                }
                break;
        }
    }
    return true;
}

void PlatformWindowLinux::swapBuffers() {
    if (m_window) SDL_GL_SwapWindow(m_window);
}

void PlatformWindowLinux::setTitle(const std::string& title) {
    if (m_window) SDL_SetWindowTitle(m_window, title.c_str());
}

void PlatformWindowLinux::setSize(int width, int height) {
    if (m_window) SDL_SetWindowSize(m_window, width, height);
}

void PlatformWindowLinux::getSize(int& width, int& height) const {
    if (m_window) SDL_GetWindowSize(m_window, &width, &height);
}

void PlatformWindowLinux::setPosition(int x, int y) {
    if (m_window) SDL_SetWindowPosition(m_window, x, y);
}

void PlatformWindowLinux::getPosition(int& x, int& y) const {
    if (m_window) SDL_GetWindowPosition(m_window, &x, &y);
}

void PlatformWindowLinux::show() {
    if (m_window) SDL_ShowWindow(m_window);
}

void PlatformWindowLinux::hide() {
    if (m_window) SDL_HideWindow(m_window);
}

void PlatformWindowLinux::minimize() {
    if (m_window) SDL_MinimizeWindow(m_window);
}

void PlatformWindowLinux::maximize() {
    if (m_window) SDL_MaximizeWindow(m_window);
}

void PlatformWindowLinux::restore() {
    if (m_window) SDL_RestoreWindow(m_window);
}

bool PlatformWindowLinux::isMaximized() const {
    if (!m_window) return false;
    Uint32 flags = SDL_GetWindowFlags(m_window);
    return (flags & SDL_WINDOW_MAXIMIZED) != 0;
}

bool PlatformWindowLinux::isMinimized() const {
    if (!m_window) return false;
    Uint32 flags = SDL_GetWindowFlags(m_window);
    return (flags & SDL_WINDOW_MINIMIZED) != 0;
}

void PlatformWindowLinux::requestClose() {
    if (m_closeCallback) m_closeCallback();
}

void PlatformWindowLinux::setFullscreen(bool fullscreen) {
    if (m_window) {
        SDL_SetWindowFullscreen(m_window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        m_isFullscreen = fullscreen;
    }
}

bool PlatformWindowLinux::isFullscreen() const {
    return m_isFullscreen;
}

bool PlatformWindowLinux::createGLContext() {
    if (!m_window) return false;
    m_glContext = SDL_GL_CreateContext(m_window);
    if (!m_glContext) {
         std::cerr << "SDL_GL_CreateContext Error: " << SDL_GetError() << std::endl;
         return false;
    }
    return true;
}

bool PlatformWindowLinux::makeContextCurrent() {
    if (!m_window || !m_glContext) return false;
    return SDL_GL_MakeCurrent(m_window, m_glContext) == 0;
}

void PlatformWindowLinux::setVSync(bool enabled) {
    SDL_GL_SetSwapInterval(enabled ? 1 : 0);
}

void* PlatformWindowLinux::getNativeHandle() const {
    if (!m_window) return nullptr;
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(m_window, &info)) {
        if (info.subsystem == SDL_SYSWM_X11) {
            return (void*)(uintptr_t)info.info.x11.window;
        }
    }
    return nullptr;
}

void* PlatformWindowLinux::getNativeDisplayHandle() const {
     if (!m_window) return nullptr;
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(m_window, &info)) {
        if (info.subsystem == SDL_SYSWM_X11) {
            return (void*)info.info.x11.display;
        }
    }
    return nullptr;
}

float PlatformWindowLinux::getDPIScale() const {
    return m_dpiScale;
}

void PlatformWindowLinux::setCursorVisible(bool visible) {
    SDL_ShowCursor(visible ? SDL_ENABLE : SDL_DISABLE);
}

KeyModifiers PlatformWindowLinux::getCurrentModifiers() const {
    return getModifiers(SDL_GetModState());
}

// Helpers
KeyCode PlatformWindowLinux::translateKey(SDL_Keycode key) {
    // Map SDL keys to Nomad KeyCode
    if (key >= 'a' && key <= 'z') return (KeyCode)((int)KeyCode::A + (key - 'a'));
    if (key >= '0' && key <= '9') return (KeyCode)((int)KeyCode::Num0 + (key - '0'));
    
    switch (key) {
        case SDLK_ESCAPE: return KeyCode::Escape;
        case SDLK_TAB: return KeyCode::Tab;
        case SDLK_SPACE: return KeyCode::Space;
        case SDLK_RETURN: return KeyCode::Enter;
        case SDLK_BACKSPACE: return KeyCode::Backspace;
        case SDLK_DELETE: return KeyCode::Delete;
        case SDLK_UP: return KeyCode::Up;
        case SDLK_DOWN: return KeyCode::Down;
        case SDLK_LEFT: return KeyCode::Left;
        case SDLK_RIGHT: return KeyCode::Right;
        case SDLK_LSHIFT: case SDLK_RSHIFT: return KeyCode::Shift;
        case SDLK_LCTRL: case SDLK_RCTRL: return KeyCode::Control;
        case SDLK_LALT: case SDLK_RALT: return KeyCode::Alt;
        default: return KeyCode::Unknown;
    }
}

KeyModifiers PlatformWindowLinux::getModifiers(Uint16 mod) {
    KeyModifiers m;
    m.shift = (mod & KMOD_SHIFT);
    m.control = (mod & KMOD_CTRL);
    m.alt = (mod & KMOD_ALT);
    m.super = (mod & KMOD_GUI);
    return m;
}

} // namespace Nomad
