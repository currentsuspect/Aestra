// X input focus handling for PlatformWindowLinux: the AESTRA_INPUT_TRACE diagnosis lines and taking focus back from
// an embedded plugin editor. Split out so PlatformWindowLinux.cpp stays about the window.

#include "PlatformWindowLinux.h"

#include "AestraInputTrace.h"

#include <SDL2/SDL_syswm.h>
#include <string>

#if defined(SDL_VIDEO_DRIVER_X11)
#include "XInputFocus.h"
#endif

namespace Aestra {

namespace {
#if defined(SDL_VIDEO_DRIVER_X11)
bool x11Handles(SDL_Window* window, Display*& display, ::Window& x11Window) {
    display = nullptr;
    x11Window = 0;
    if (!window) return false;
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_X11) return false;
    display = info.info.x11.display;
    x11Window = info.info.x11.window;
    return display && x11Window;
}
#endif
} // namespace

std::string PlatformWindowLinux::describeInputFocus() const {
#if defined(SDL_VIDEO_DRIVER_X11)
    Display* display = nullptr;
    ::Window win = 0;
    if (x11Handles(m_window, display, win)) return XInputFocus::describe(XInputFocus::query(display, win));
#endif
    return "xfocus=unavailable";
}

void PlatformWindowLinux::traceInputEvent(const SDL_Event& e) {
    using namespace InputTrace;
    if (!enabled()) return;
    const bool ours = m_window && SDL_GetWindowID(m_window) == (e.type == SDL_WINDOWEVENT ? e.window.windowID : 0);
    switch (e.type) {
    case SDL_WINDOWEVENT: {
        const char* what = nullptr;
        switch (e.window.event) {
        case SDL_WINDOWEVENT_FOCUS_GAINED: what = "focus GAINED"; break;
        case SDL_WINDOWEVENT_FOCUS_LOST: what = "focus LOST"; break;
        case SDL_WINDOWEVENT_ENTER: what = "mouse ENTER window"; break;
        case SDL_WINDOWEVENT_LEAVE: what = "mouse LEAVE window"; break;
        default: break;
        }
        if (what && allow("sdl-window", 20))
            log(std::string(what) + (ours ? "" : " (other window)") + " | " + describeInputFocus());
        break;
    }
    case SDL_KEYDOWN:
        if (allow("key", 10))
            log(std::string("key RECEIVED by Aestra sym=") + SDL_GetKeyName(e.key.keysym.sym) +
                " repeat=" + std::to_string(e.key.repeat) + " | " + describeInputFocus());
        break;
    case SDL_MOUSEBUTTONDOWN: {
        const bool underChild = pressUnderChild(e.button.x, e.button.y);
        if (allow("press", 20))
            log("press btn=" + std::to_string(e.button.button) + " at (" + std::to_string(e.button.x) + "," +
                std::to_string(e.button.y) + ") sdlWindow=" + std::to_string(e.button.windowID) +
                (e.button.windowID == SDL_GetWindowID(m_window) ? "(Aestra)" : "(OTHER)") +
                " which=" + std::to_string(e.button.which) +
                " synthetic=" + (e.button.which == SDL_TOUCH_MOUSEID ? "touch" : "no") +
                " clicks=" + std::to_string(e.button.clicks) + " underMappedChild=" + (underChild ? "YES" : "no") +
                " sdlKbdFocusIsAestra=" + (SDL_GetKeyboardFocus() == m_window ? "yes" : "NO") +
                " | " + describeInputFocus());
        break;
    }
    default: break;
    }
}

void PlatformWindowLinux::traceFocusOwnerChange() {
#if defined(SDL_VIDEO_DRIVER_X11)
    if (!InputTrace::enabled()) return;
    Display* display = nullptr;
    ::Window win = 0;
    if (!x11Handles(m_window, display, win)) return;
    const auto snap = XInputFocus::query(display, win);
    if (snap.window == m_lastTracedFocusWindow) return;
    m_lastTracedFocusWindow = snap.window;
    InputTrace::log("X input focus CHANGED | " + XInputFocus::describe(snap));
#endif
}

bool PlatformWindowLinux::pressUnderChild(int x, int y) const {
#if defined(SDL_VIDEO_DRIVER_X11)
    Display* display = nullptr;
    ::Window win = 0;
    return x11Handles(m_window, display, win) && XInputFocus::childUnder(display, win, x, y);
#else
    (void)x; (void)y;
    return false;
#endif
}

} // namespace Aestra
