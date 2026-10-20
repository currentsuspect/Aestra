// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// V82-H3: the sandbox helper's plugin window (Linux, X11, VST3). The helper's main thread reads the app's commands, so
// the window lives on a thread of its own: it opens the X11 display, makes the window, attaches the plugin's view and
// then loops on the X connection plus whatever descriptors and timers the plugin registered through its run loop.
// The app drives it with EDITOR OPEN / CLOSE / STATE; closing the window with the title-bar button is noticed on STATE.

#if defined(__linux__) && defined(AESTRA_HAS_VST3) && defined(AESTRA_HAS_X11)

#include "Plugin/EditorLoopPump.h"
#include "Plugin/VST3Host.h"

#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/Xlib.h>
#include <poll.h>

#include <atomic>
#include <cstdio>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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

class EditorHost {
public:
    ~EditorHost() { close(); }

    /// Opens the plugin's editor in a new window. Blocks until it is up (or failed); returns its size.
    bool open(const std::shared_ptr<Aestra::Audio::VST3PluginInstance>& plugin, const std::string& title, int& width,
              int& height) {
        close();
        if (!plugin) return false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_state = State::Opening;
            m_open = false;
        }
        m_stop = false;
        m_plugin = plugin;
        m_thread = std::thread([this, title] { run(title); });
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return m_state != State::Opening; });
        if (m_state != State::Running) {
            lock.unlock();
            m_thread.join();
            m_plugin.reset();
            return false;
        }
        width = m_width;
        height = m_height;
        return true;
    }

    /// Whether the window is still on screen (false after the user closes it, or before open).
    bool isOpen() const { return m_open; }

    void close() {
        if (!m_thread.joinable()) return;
        m_stop = true;
        m_thread.join();
        m_plugin.reset();
        m_open = false;
    }

    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    enum class State { Idle, Opening, Running, Failed };

    void finishOpening(State state) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_state = state;
        }
        m_cv.notify_all();
    }

    void run(const std::string& title) {
        Display* display = XOpenDisplay(nullptr);
        if (!display) {
            finishOpening(State::Failed);
            return;
        }
        XSetErrorHandler(ignoreXError);
        const int screen = DefaultScreen(display);
        Window window = 0;
        Atom wmDelete = XInternAtom(display, "WM_DELETE_WINDOW", False);
        Aestra::Audio::EditorLoopPump pump;

        auto resizeWindow = [&](int w, int h) {
            if (!window) return;
            XResizeWindow(display, window, static_cast<unsigned>(w), static_cast<unsigned>(h));
            XFlush(display);
            m_width = w;
            m_height = h;
        };
        auto makeWindow = [&](int w, int h, bool resizable) -> unsigned long {
            window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, static_cast<unsigned>(w),
                                         static_cast<unsigned>(h), 0, 0, BlackPixel(display, screen));
            if (!window) return 0;
            XStoreName(display, window, title.c_str());
            XSetWMProtocols(display, window, &wmDelete, 1);
            XSelectInput(display, window, StructureNotifyMask | KeyPressMask | KeyReleaseMask | FocusChangeMask);
            if (!resizable) {
                XSizeHints hints{};
                hints.flags = PMinSize | PMaxSize;
                hints.min_width = hints.max_width = w;
                hints.min_height = hints.max_height = h;
                XSetWMNormalHints(display, window, &hints);
            }
            XMapWindow(display, window);
            XFlush(display);
            m_width = w;
            m_height = h;
            return static_cast<unsigned long>(window);
        };

        if (!m_plugin->openEditorX11(makeWindow, pump, resizeWindow)) {
            if (window) XDestroyWindow(display, window);
            XCloseDisplay(display);
            finishOpening(State::Failed);
            return;
        }
        m_open = true;
        finishOpening(State::Running);

        const int xfd = ConnectionNumber(display);
        while (!m_stop && m_open) {
            while (XPending(display) > 0) {
                XEvent event;
                XNextEvent(display, &event);
                if (event.type == ClientMessage && static_cast<Atom>(event.xclient.data.l[0]) == wmDelete) m_open = false;
                else if (event.type == ConfigureNotify && event.xconfigure.window == window && m_plugin->canResizeEditor() &&
                         (event.xconfigure.width != m_width || event.xconfigure.height != m_height)) {
                    // The user dragged the window edge: tell the plugin its view has a new size.
                    m_width = event.xconfigure.width;
                    m_height = event.xconfigure.height;
                    m_plugin->resizeEditor(m_width, m_height);
                }
            }
            std::vector<pollfd> polls;
            polls.push_back({xfd, POLLIN, 0});
            for (const int fd : pump.fds()) polls.push_back({fd, POLLIN, 0});
            int wait = pump.msUntilNextTimer();
            if (wait < 0 || wait > 30) wait = 30; // also the cadence for noticing close()
            poll(polls.data(), static_cast<nfds_t>(polls.size()), wait);
            for (size_t i = 1; i < polls.size(); ++i)
                if (polls[i].revents & (POLLIN | POLLHUP)) pump.dispatchFd(polls[i].fd);
            pump.fireDueTimers();
        }

        m_plugin->closeEditor();
        XDestroyWindow(display, window);
        XCloseDisplay(display);
        m_open = false;
    }

    std::shared_ptr<Aestra::Audio::VST3PluginInstance> m_plugin;
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    State m_state = State::Idle;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_open{false};
    std::atomic<int> m_width{0};
    std::atomic<int> m_height{0};
};

} // namespace AestraHelper

#endif
