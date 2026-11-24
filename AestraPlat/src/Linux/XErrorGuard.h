#pragma once

#include <X11/Xlib.h>

namespace Aestra {

// A child window the plugin helper has already destroyed raises BadWindow, and Xlib's default handler exits the app
// with unsaved work in it. Requests go out on the app's own connection and errors come back asynchronously, so the
// handler stays installed until XSync has delivered them.
class XErrorGuard {
public:
    explicit XErrorGuard(Display* display) : m_display(display) { m_previous = XSetErrorHandler(&swallow); }
    ~XErrorGuard() {
        XSync(m_display, False);
        XSetErrorHandler(m_previous);
    }
    XErrorGuard(const XErrorGuard&) = delete;
    XErrorGuard& operator=(const XErrorGuard&) = delete;

private:
    static int swallow(Display*, XErrorEvent*) { return 0; }

    Display* m_display = nullptr;
    int (*m_previous)(Display*, XErrorEvent*) = nullptr;
};

} // namespace Aestra
