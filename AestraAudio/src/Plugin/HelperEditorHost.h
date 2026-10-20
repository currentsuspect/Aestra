// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// V82-H3: the sandbox helper's plugin window (Linux, X11; VST3 and CLAP). It opens the X11 display, makes the window,
// attaches the plugin's view and then runs the X connection plus whatever descriptors and timers the plugin registered
// through its run loop.
// The app drives it with EDITOR OPEN / CLOSE / STATE; closing the window with the title-bar button is noticed on STATE.
//
// Everything here runs on the helper's command thread, between commands (see HelperLineReader.h): no thread of its own.

#if defined(__linux__) && defined(AESTRA_HAS_X11)

#include "Plugin/EditorLoopPump.h"
#ifdef AESTRA_HAS_VST3
#include "Plugin/VST3Host.h"
#endif

#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/Xlib.h>
#include <poll.h>

#include <atomic>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace AestraHelper {

// Xlib's default error handler exits the process. A plugin's editor makes plenty of requests we do not control (a bad
// atom, a window it already destroyed), and none of them is worth losing the user's session over: log it and go on.
inline int ignoreXError(Display*, XErrorEvent* e) {
    static std::atomic<int> logged{0};
    if (logged++ < 5) // a plugin can repeat the same one every frame
        std::fprintf(stderr, "AestraPluginHost: X error %d (request %d) ignored\n", e->error_code, e->request_code);
    return 0;
}

/// What a plugin format provides for its editor: the helper's window code does not care which format is inside.
/// Every function runs on the window's thread.
struct EditorBackend {
    using MakeWindow = std::function<unsigned long(int width, int height, bool resizable)>;
    /// Create the editor, ask makeWindow for a window of the size it wants, attach to it. False = no X11 editor.
    std::function<bool(const MakeWindow&, Aestra::Audio::EditorLoopPump&, std::function<void(int, int)>)> open;
    std::function<void()> close;                  ///< detach and destroy the editor (the window is destroyed after)
    std::function<bool()> canResize;
    std::function<void(int, int)> resize;         ///< the user dragged the window edge: tell the editor
    std::function<void()> sync;                   ///< called every loop turn: keep the editor's model current
};

class EditorHost {
public:
    ~EditorHost() { close(); }

    /// Opens the plugin's editor in a new window, on the calling thread. Returns its size. The calling thread is the
    /// helper's command thread, which is also the thread that loaded the plugin: a plugin that insists its window code
    /// runs on "the main thread" (CLAP's thread-check, Surge's included) finds it is. The same thread must then call
    /// pollFds() and service() while the editor is open; the command loop does.
    bool open(EditorBackend backend, const std::string& title, int& width, int& height) {
        close();
        if (!backend.open) return false;
        m_display = XOpenDisplay(nullptr);
        if (!m_display) return false;
        XSetErrorHandler(ignoreXError);
        m_screen = DefaultScreen(m_display);
        m_wmDelete = XInternAtom(m_display, "WM_DELETE_WINDOW", False);
        m_backend = std::move(backend);

        auto resizeWindow = [this](int w, int h) {
            if (!m_window) return;
            XResizeWindow(m_display, m_window, static_cast<unsigned>(w), static_cast<unsigned>(h));
            XFlush(m_display);
            m_width = w;
            m_height = h;
        };
        auto makeWindow = [this, &title](int w, int h, bool resizable) -> unsigned long {
            m_window = XCreateSimpleWindow(m_display, RootWindow(m_display, m_screen), 0, 0, static_cast<unsigned>(w),
                                           static_cast<unsigned>(h), 0, 0, BlackPixel(m_display, m_screen));
            if (!m_window) return 0;
            XStoreName(m_display, m_window, title.c_str());
            XSetWMProtocols(m_display, m_window, &m_wmDelete, 1);
            XSelectInput(m_display, m_window, StructureNotifyMask | KeyPressMask | KeyReleaseMask | FocusChangeMask);
            if (!resizable) {
                XSizeHints hints{};
                hints.flags = PMinSize | PMaxSize;
                hints.min_width = hints.max_width = w;
                hints.min_height = hints.max_height = h;
                XSetWMNormalHints(m_display, m_window, &hints);
            }
            XMapWindow(m_display, m_window);
            XFlush(m_display);
            m_width = w;
            m_height = h;
            return static_cast<unsigned long>(m_window);
        };

        if (!m_backend.open(makeWindow, m_pump, resizeWindow)) {
            teardown(false);
            return false;
        }
        width = m_width;
        height = m_height;
        return true;
    }

    /// Whether the window is still on screen (false after the user closes it, or before open).
    bool isOpen() const { return m_display != nullptr; }
    int width() const { return m_width; }
    int height() const { return m_height; }

    void close() { teardown(true); }

    /// What the command loop should wait on besides its own input: the X connection, and the plugin's descriptors.
    /// @p timeoutMs is lowered to when the plugin's next timer is due (never raised).
    void pollFds(std::vector<pollfd>& out, int& timeoutMs) {
        if (!m_display) return;
        out.push_back({ConnectionNumber(m_display), POLLIN, 0});
        for (const int fd : m_pump.fds()) out.push_back({fd, POLLIN, 0});
        int wait = m_pump.msUntilNextTimer();
        if (wait < 0 || wait > 30) wait = 30; // also how soon the user closing the window is noticed
        if (timeoutMs < 0 || wait < timeoutMs) timeoutMs = wait;
    }

    /// Run whatever is ready: window events, the plugin's descriptors and due timers. Never blocks.
    void service() {
        if (!m_display) return;
        while (m_display && XPending(m_display) > 0) {
            XEvent event;
            XNextEvent(m_display, &event);
            if (event.type == ClientMessage && static_cast<Atom>(event.xclient.data.l[0]) == m_wmDelete) {
                teardown(true); // the user closed the window
                return;
            }
            if (event.type == ConfigureNotify && event.xconfigure.window == m_window && m_backend.canResize() &&
                (event.xconfigure.width != m_width || event.xconfigure.height != m_height)) {
                // The user dragged the window edge: tell the plugin its view has a new size.
                m_width = event.xconfigure.width;
                m_height = event.xconfigure.height;
                m_backend.resize(m_width, m_height);
            }
        }
        std::vector<pollfd> ready;
        for (const int fd : m_pump.fds()) ready.push_back({fd, POLLIN, 0});
        if (!ready.empty() && poll(ready.data(), static_cast<nfds_t>(ready.size()), 0) > 0)
            for (const auto& p : ready)
                if (p.revents & (POLLIN | POLLHUP)) m_pump.dispatchFd(p.fd);
        m_pump.fireDueTimers();
        if (m_backend.sync) m_backend.sync(); // what the processor reported, to the window
    }

private:
    void teardown(bool closeBackend) {
        if (!m_display) return;
        if (closeBackend && m_backend.close) m_backend.close();
        if (m_window) XDestroyWindow(m_display, m_window);
        XCloseDisplay(m_display);
        m_display = nullptr;
        m_window = 0;
        m_backend = {};
    }

    EditorBackend m_backend;
    Aestra::Audio::EditorLoopPump m_pump;
    Display* m_display = nullptr;
    Window m_window = 0;
    Atom m_wmDelete = 0;
    int m_screen = 0;
    int m_width = 0;
    int m_height = 0;
};

#ifdef AESTRA_HAS_VST3
/// The VST3 plugin's editor, for EditorHost.
inline EditorBackend vst3EditorBackend(std::shared_ptr<Aestra::Audio::VST3PluginInstance> plugin) {
    EditorBackend b;
    b.open = [plugin](const EditorBackend::MakeWindow& makeWindow, Aestra::Audio::EditorLoopPump& pump,
                      std::function<void(int, int)> resize) { return plugin->openEditorX11(makeWindow, pump, std::move(resize)); };
    b.close = [plugin] { plugin->closeEditor(); };
    b.canResize = [plugin] { return plugin->canResizeEditor(); };
    b.resize = [plugin](int w, int h) { plugin->resizeEditor(w, h); };
    b.sync = [plugin] { plugin->pumpControllerSync(); };
    return b;
}
#endif

} // namespace AestraHelper

#endif
