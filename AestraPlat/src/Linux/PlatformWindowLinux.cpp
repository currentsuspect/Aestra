#include "PlatformWindowLinux.h"

#include <SDL2/SDL_syswm.h>
#include <iostream>
#if defined(SDL_VIDEO_DRIVER_X11)
#include <X11/Xlib.h>
#endif

namespace Aestra {

namespace {
bool getX11WindowInfo(SDL_Window* window, Display*& display, ::Window& x11Window) {
    display = nullptr;
    x11Window = 0;
    if (!window) return false;

    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info)) return false;
    if (info.subsystem != SDL_SYSWM_X11) return false;

    display = info.info.x11.display;
    x11Window = info.info.x11.window;
    return (display != nullptr && x11Window != 0);
}
} // namespace

PlatformWindowLinux::PlatformWindowLinux() {
    // SDL_Init should be called by Platform::initialize()
}

PlatformWindowLinux::~PlatformWindowLinux() {
    destroy();
}

bool PlatformWindowLinux::create(const WindowDesc& desc) {
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

    // GL Attributes - Request generic 3.3 Core, can be upgraded by user config if needed
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    m_window = SDL_CreateWindow(desc.title.c_str(), x, y, desc.width, desc.height, flags);

    if (!m_window) {
        std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
        return false;
    }

    m_isFullscreen = desc.startFullscreen;
    m_isWindowVisible = true;
    m_isWindowMapped = true;
    m_maximized = desc.startMaximized;

    // Seed the restore geometry from what was requested. Without this, a session
    // that starts maximized and is never un-maximized has no restore bounds to
    // save, and the maximized size would win by default (#655).
    m_restoreX = (x == SDL_WINDOWPOS_CENTERED) ? 0 : x;
    m_restoreY = (y == SDL_WINDOWPOS_CENTERED) ? 0 : y;
    m_restoreW = desc.width;
    m_restoreH = desc.height;
    m_hasRestoreBounds = desc.width > 0 && desc.height > 0;

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
            if (m_closeCallback) {
                m_closeCallback();
                return true;
            }
            return false;

        case SDL_WINDOWEVENT:
            if (e.window.windowID == SDL_GetWindowID(m_window)) {
                switch (e.window.event) {
                case SDL_WINDOWEVENT_MAXIMIZED:
                    m_maximized = true;
                    break;
                case SDL_WINDOWEVENT_MOVED:
                    rememberRestoreBoundsIfNormal();
                    break;
                case SDL_WINDOWEVENT_RESIZED:
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                    rememberRestoreBoundsIfNormal();
                    if (m_resizeCallback)
                        m_resizeCallback(e.window.data1, e.window.data2);
                    {
                        // Check DPI change
                        int dw, dh;
                        SDL_GL_GetDrawableSize(m_window, &dw, &dh);
                        float newScale = (float)dw / e.window.data1;
                        if (abs(newScale - m_dpiScale) > 0.01f) {
                            m_dpiScale = newScale;
                            if (m_dpiChangeCallback)
                                m_dpiChangeCallback(m_dpiScale);
                        }
                    }
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
                case SDL_WINDOWEVENT_SHOWN:
                    m_isWindowVisible = true;
                    m_isWindowMapped = true;
                    break;
                case SDL_WINDOWEVENT_HIDDEN:
                    m_isWindowVisible = false;
                    m_isWindowMapped = false;
                    break;
                case SDL_WINDOWEVENT_MINIMIZED:
                    m_isWindowMapped = false;
                    break;
                case SDL_WINDOWEVENT_RESTORED:
                    m_maximized = false;
                    if (m_isWindowVisible) {
                        m_isWindowMapped = true;
                    }
                    rememberRestoreBoundsIfNormal();
                    break;
                case SDL_WINDOWEVENT_ENTER:
                    if (m_mouseEnterCallback)
                        m_mouseEnterCallback();
                    break;
                case SDL_WINDOWEVENT_LEAVE:
                    if (m_mouseLeaveCallback)
                        m_mouseLeaveCallback();
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
                // Preserve high-resolution touchpad deltas. SDL's integer x/y
                // fields may round a fractional gesture to zero, which leaves
                // drag scrollbars working while two-finger scrolling appears dead.
#if SDL_VERSION_ATLEAST(2, 0, 18)
                const float verticalDelta = e.wheel.preciseY;
                const float horizontalDelta = e.wheel.preciseX;
#else
                const float verticalDelta = static_cast<float>(e.wheel.y);
                const float horizontalDelta = static_cast<float>(e.wheel.x);
#endif
                float delta = verticalDelta;
                // Use magnitude comparison to detect horizontal vs vertical gestures
                // This handles touchpads that may report both axes for the same gesture
                const bool horizontalGesture = (std::abs(horizontalDelta) > std::abs(verticalDelta));
                if (horizontalGesture) {
                    // SDL wheel.x > 0 means scroll right, map to negative delta so consumers
                    // that do target -= wheelDelta move view content to the right.
                    delta = -horizontalDelta;
                    m_syntheticShiftForHorizontalWheel = true;
                }
                if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
                    delta = -delta;
                m_mouseWheelCallback(delta);
                m_syntheticShiftForHorizontalWheel = false;
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
                // Primitive UTF-8 decoding for single codepoint (simplified)
                // In real app, might need full UTF8 decoder, but SDL gives char array
                // For now pass the first byte if ASCII, or handle full utf8
                // Aestras might expect unicode. e.text.text is char[32] utf8
                // We'll just cast first byte for now if we don't have a decoder handy,
                // or implement a quick one.
                unsigned char* p = (unsigned char*)e.text.text;
                if (*p < 0x80)
                    m_charCallback(*p);
                // TODO: Proper UTF8 decoding
            }
            break;
        }
    }
    return true;
}

void PlatformWindowLinux::swapBuffers() {
    if (m_window)
        SDL_GL_SwapWindow(m_window);
}

void PlatformWindowLinux::setTitle(const std::string& title) {
    if (m_window)
        SDL_SetWindowTitle(m_window, title.c_str());
}

void PlatformWindowLinux::setSize(int width, int height) {
    if (m_window)
        SDL_SetWindowSize(m_window, width, height);
}

void PlatformWindowLinux::getSize(int& width, int& height) const {
    if (m_window)
        SDL_GetWindowSize(m_window, &width, &height);
}

void PlatformWindowLinux::setPosition(int x, int y) {
    if (m_window)
        SDL_SetWindowPosition(m_window, x, y);
}

void PlatformWindowLinux::getPosition(int& x, int& y) const {
    if (m_window)
        SDL_GetWindowPosition(m_window, &x, &y);
}

void PlatformWindowLinux::show() {
    if (m_window) {
#if defined(SDL_VIDEO_DRIVER_X11)
        Display* display = nullptr;
        ::Window x11Window = 0;
        if (getX11WindowInfo(m_window, display, x11Window)) {
            XMapWindow(display, x11Window);
            XFlush(display);
        }
#endif
        SDL_ShowWindow(m_window);
        m_isWindowVisible = true;
        m_isWindowMapped = true;
    }
}

void PlatformWindowLinux::requestFocus() {
    if (m_window) {
#if defined(SDL_VIDEO_DRIVER_X11)
        Display* display = nullptr;
        ::Window x11Window = 0;
        if (getX11WindowInfo(m_window, display, x11Window)) {
            XRaiseWindow(display, x11Window);
            XSetInputFocus(display, x11Window, RevertToNone, CurrentTime);
            XFlush(display);
        }
#endif
        SDL_RaiseWindow(m_window);
    }
}

void PlatformWindowLinux::hide() {
    if (m_window) {
#if defined(SDL_VIDEO_DRIVER_X11)
        Display* display = nullptr;
        ::Window x11Window = 0;
        if (getX11WindowInfo(m_window, display, x11Window)) {
            XUnmapWindow(display, x11Window);
            XFlush(display);
        }
#endif
        SDL_HideWindow(m_window);
        m_isWindowVisible = false;
        m_isWindowMapped = false;
    }
}

void PlatformWindowLinux::minimize() {
    if (m_window)
        SDL_MinimizeWindow(m_window);
}

void PlatformWindowLinux::maximize() {
    if (m_window) {
        rememberRestoreBoundsIfNormal(); // capture before the rect becomes the maximized one
        SDL_MaximizeWindow(m_window);
        m_maximized = true;
    }
}

void PlatformWindowLinux::restore() {
    if (m_window) {
        SDL_RestoreWindow(m_window);
        m_maximized = false;
    }
}

bool PlatformWindowLinux::isMaximized() const {
    if (!m_window)
        return false;
    // Union of the tracked state and the SDL flag. The flag alone missed
    // WM-initiated maximize (#655): a window with _NET_WM_STATE_MAXIMIZED_VERT
    // and _HORZ set reported false, so shutdown persisted "not maximized"
    // together with the maximized dimensions.
    if (m_maximized)
        return true;
    Uint32 flags = SDL_GetWindowFlags(m_window);
    return (flags & SDL_WINDOW_MAXIMIZED) != 0;
}

bool PlatformWindowLinux::getRestoreBounds(int& x, int& y, int& width, int& height) const {
    if (!m_hasRestoreBounds)
        return false;
    x = m_restoreX;
    y = m_restoreY;
    width = m_restoreW;
    height = m_restoreH;
    return true;
}

void PlatformWindowLinux::rememberRestoreBoundsIfNormal() {
    if (!m_window || isMaximized() || m_isFullscreen)
        return;
    int w = 0, h = 0, x = 0, y = 0;
    SDL_GetWindowSize(m_window, &w, &h);
    SDL_GetWindowPosition(m_window, &x, &y);
    if (w <= 0 || h <= 0)
        return;
    m_restoreX = x;
    m_restoreY = y;
    m_restoreW = w;
    m_restoreH = h;
    m_hasRestoreBounds = true;
}

bool PlatformWindowLinux::isMinimized() const {
    if (!m_window)
        return false;
    Uint32 flags = SDL_GetWindowFlags(m_window);
    return (flags & SDL_WINDOW_MINIMIZED) != 0;
}

void PlatformWindowLinux::requestClose() {
    if (m_closeCallback)
        m_closeCallback();
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
    if (!m_window)
        return false;
    m_glContext = SDL_GL_CreateContext(m_window);
    if (!m_glContext) {
        std::cerr << "SDL_GL_CreateContext Error: " << SDL_GetError() << std::endl;
        return false;
    }
    return true;
}

bool PlatformWindowLinux::makeContextCurrent() {
    if (!m_window || !m_glContext)
        return false;
    return SDL_GL_MakeCurrent(m_window, m_glContext) == 0;
}

void PlatformWindowLinux::setVSync(bool enabled) {
    SDL_GL_SetSwapInterval(enabled ? 1 : 0);
}

void* PlatformWindowLinux::getNativeHandle() const {
    if (!m_window)
        return nullptr;
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
    if (!m_window)
        return nullptr;
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

void PlatformWindowLinux::setCursorPosition(int x, int y) {
    if (m_window) {
        SDL_WarpMouseInWindow(m_window, x, y); // window-relative, matches getCursorPosition()
    }
}

void PlatformWindowLinux::getCursorPosition(int& x, int& y) const {
    SDL_GetMouseState(&x, &y);
}

void PlatformWindowLinux::setMouseCapture(bool captured) {
    // No-op — cursor capture handled via warp in NUISlider
    // SDL_SetRelativeMouseMode removed: focus-dependent, unreliable
    (void)captured;
}

void PlatformWindowLinux::setCursorClip(bool clipped) {
    if (m_window) {
        if (clipped) {
            int w, h;
            SDL_GetWindowSize(m_window, &w, &h);
            SDL_Rect rect = {0, 0, w, h};
            SDL_SetWindowMouseRect(m_window, &rect);
        } else {
            SDL_SetWindowMouseRect(m_window, nullptr);
        }
    }
}

void PlatformWindowLinux::setCursorClipRect(int x, int y, int w, int h) {
    if (m_window) {
        SDL_Rect rect = {x, y, w, h}; // window-relative, matches SDL_WarpMouseInWindow
        SDL_SetWindowMouseRect(m_window, &rect);
    }
}

KeyModifiers PlatformWindowLinux::getCurrentModifiers() const {
    KeyModifiers mods = getModifiers(SDL_GetModState());
    if (m_syntheticShiftForHorizontalWheel) {
        mods.shift = true;
    }
    return mods;
}

// Helpers
KeyCode PlatformWindowLinux::translateKey(SDL_Keycode key) {
    // Map SDL keys to Aestra KeyCode
    if (key >= 'a' && key <= 'z')
        return (KeyCode)((int)KeyCode::A + (key - 'a'));
    if (key >= '0' && key <= '9')
        return (KeyCode)((int)KeyCode::Num0 + (key - '0'));

    switch (key) {
    case SDLK_ESCAPE:
        return KeyCode::Escape;
    case SDLK_TAB:
        return KeyCode::Tab;
    case SDLK_CAPSLOCK:
        return KeyCode::CapsLock;
    case SDLK_SPACE:
        return KeyCode::Space;
    case SDLK_RETURN:
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
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
        return KeyCode::Shift;
    case SDLK_LCTRL:
    case SDLK_RCTRL:
        return KeyCode::Control;
    case SDLK_LALT:
    case SDLK_RALT:
        return KeyCode::Alt;
    case SDLK_a: case SDLK_b: case SDLK_c: case SDLK_d: case SDLK_e:
    case SDLK_f: case SDLK_g: case SDLK_h: case SDLK_i: case SDLK_j:
    case SDLK_k: case SDLK_l: case SDLK_m: case SDLK_n: case SDLK_o:
    case SDLK_p: case SDLK_q: case SDLK_r: case SDLK_s: case SDLK_t:
    case SDLK_u: case SDLK_v: case SDLK_w: case SDLK_x: case SDLK_y:
    case SDLK_z:
        return static_cast<KeyCode>(static_cast<int>(KeyCode::A) + (key - SDLK_a));
    case SDLK_0: case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4:
    case SDLK_5: case SDLK_6: case SDLK_7: case SDLK_8: case SDLK_9:
        return static_cast<KeyCode>(static_cast<int>(KeyCode::Num0) + (key - SDLK_0));
    case SDLK_F1: case SDLK_F2: case SDLK_F3: case SDLK_F4:
    case SDLK_F5: case SDLK_F6: case SDLK_F7: case SDLK_F8:
    case SDLK_F9: case SDLK_F10: case SDLK_F11: case SDLK_F12:
        return static_cast<KeyCode>(static_cast<int>(KeyCode::F1) + (key - SDLK_F1));
    default:
        return KeyCode::Unknown;
    }
}

KeyModifiers PlatformWindowLinux::getModifiers(Uint16 mod) const {
    KeyModifiers m;
    m.shift = (mod & KMOD_SHIFT);
    m.control = (mod & KMOD_CTRL);
    m.alt = (mod & KMOD_ALT);
    m.super = (mod & KMOD_GUI);
    m.capsLock = (mod & KMOD_CAPS);
    return m;
}

} // namespace Aestra
