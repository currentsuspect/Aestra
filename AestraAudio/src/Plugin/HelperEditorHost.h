// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// V82-H3 / H8: the sandbox helper's plugin window (Linux, X11; Windows, a Win32 window; VST3 and CLAP). It opens the X11 display, makes the window,
// attaches the plugin's view and then runs the X connection plus whatever descriptors and timers the plugin registered
// through its run loop.
// The app drives it with EDITOR OPEN / CLOSE / STATE; closing the window with the title-bar button is noticed on STATE.
//
// Everything here runs on the helper's command thread, between commands (see HelperLineReader.h): no thread of its own.

#include "Plugin/EditorLoopPump.h"

#include <cstdint>
#include <functional>

#if (defined(__linux__) && defined(AESTRA_HAS_X11)) || defined(_WIN32)
#define AESTRA_HELPER_HAS_EDITORS 1
#endif

namespace AestraHelper {

/// What a plugin format provides for its editor: the helper's window code does not care which format is inside.
/// Every function runs on the window's thread.
struct EditorBackend {
    /// Makes the top-level window: an X11 Window or an HWND, as an integer wide enough for either (an HWND is 64 bits
    /// on Windows, where unsigned long is 32). 0 = no window.
    using MakeWindow = std::function<std::uintptr_t(int width, int height, bool resizable)>;
    /// Create the editor, ask makeWindow for a window of the size it wants, attach to it. False = no editor.
    std::function<bool(const MakeWindow&, Aestra::Audio::EditorLoopPump&, std::function<void(int, int)>)> open;
    std::function<void()> close;                  ///< detach and destroy the editor (the window is destroyed after)
    std::function<bool()> canResize;
    std::function<void(int, int)> resize;         ///< the user dragged the window edge: tell the editor
    std::function<void()> sync;                   ///< called every loop turn: keep the editor's model current
};

} // namespace AestraHelper

#ifdef AESTRA_HAS_VST3
#include "Plugin/VST3Host.h"
#endif

#if defined(__linux__) && defined(AESTRA_HAS_X11)

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
        auto makeWindow = [this, &title](int w, int h, bool resizable) -> std::uintptr_t {
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
            return static_cast<std::uintptr_t>(m_window);
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


} // namespace AestraHelper

#endif // linux && X11

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

namespace AestraHelper {

/// Windows (V82-H8): the plugin's editor in a top-level window the helper owns. Window messages are pumped by the
/// command loop's reader (HelperLineReader.h) on this same thread, the one that loaded the plugin; the window
/// procedure only notes a close or a new size, and service() acts on it between commands, as on X11.
class EditorHost {
public:
    ~EditorHost() { close(); }

    bool open(EditorBackend backend, const std::string& title, int& width, int& height) {
        close();
        if (!backend.open || !registerClass()) return false;
        m_backend = std::move(backend);
        m_closeRequested = false;
        m_sizePending = false;

        auto resizeWindow = [this](int w, int h) {
            if (!m_window) return;
            RECT r{0, 0, w, h};
            AdjustWindowRect(&r, static_cast<DWORD>(GetWindowLongPtrW(m_window, GWL_STYLE)), FALSE);
            m_ignoreSize = true; // our own resize, not the user's
            SetWindowPos(m_window, nullptr, 0, 0, r.right - r.left, r.bottom - r.top,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            m_ignoreSize = false;
            m_width = w;
            m_height = h;
        };
        auto makeWindow = [this, &title](int w, int h, bool resizable) -> std::uintptr_t {
            const DWORD style = resizable ? WS_OVERLAPPEDWINDOW
                                          : (WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX));
            RECT r{0, 0, w, h};
            AdjustWindowRect(&r, style, FALSE);
            const std::wstring wideTitle = widen(title);
            m_window = CreateWindowExW(0, kClassName, wideTitle.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT,
                                       r.right - r.left, r.bottom - r.top, nullptr, nullptr,
                                       GetModuleHandleW(nullptr), nullptr);
            if (!m_window) return 0;
            SetWindowLongPtrW(m_window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
            ShowWindow(m_window, SW_SHOWNORMAL);
            SetForegroundWindow(m_window); // the user asked for it: bring it up, not behind Aestra
            m_width = w;
            m_height = h;
            return reinterpret_cast<std::uintptr_t>(m_window);
        };

        if (!m_backend.open(makeWindow, m_pump, resizeWindow)) {
            teardown(false);
            return false;
        }
        width = m_width;
        height = m_height;
        return true;
    }

    bool isOpen() const { return m_window != nullptr; }
    int width() const { return m_width; }
    int height() const { return m_height; }

    void close() { teardown(true); }

    /// Lowers @p timeoutMs to the plugin's next timer, and to 30 ms while open so the editor's model keeps up (sync).
    void limitWait(int& timeoutMs) {
        if (!m_window) return;
        int wait = m_pump.msUntilNextTimer();
        if (wait < 0 || wait > 30) wait = 30;
        if (timeoutMs < 0 || wait < timeoutMs) timeoutMs = wait;
    }

    /// Acts on what the window procedure noted, runs due timers and syncs the editor. Never blocks.
    void service() {
        if (!m_window) return;
        if (m_closeRequested) {
            teardown(true); // the user closed the window
            return;
        }
        if (m_sizePending) {
            m_sizePending = false;
            if (m_backend.canResize && m_backend.canResize() && (m_pendingW != m_width || m_pendingH != m_height)) {
                m_width = m_pendingW;
                m_height = m_pendingH;
                m_backend.resize(m_width, m_height);
            }
        }
        m_pump.fireDueTimers();
        if (m_backend.sync) m_backend.sync();
    }

private:
    static constexpr const wchar_t* kClassName = L"AestraPluginHostEditor";

    static std::wstring widen(const std::string& utf8) {
        const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
        if (n <= 0) return L"Plugin";
        std::wstring out(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), n);
        out.resize(static_cast<size_t>(n - 1));
        return out;
    }

    static bool registerClass() {
        static const bool registered = [] {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof wc;
            wc.lpfnWndProc = &EditorHost::windowProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
            wc.lpszClassName = kClassName;
            return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        }();
        return registered;
    }

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<EditorHost*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self) {
            if (msg == WM_CLOSE) {
                self->m_closeRequested = true; // torn down in service(), after the plugin lets go of the window
                return 0;
            }
            if (msg == WM_SIZE && wParam != SIZE_MINIMIZED && !self->m_ignoreSize) {
                self->m_pendingW = LOWORD(lParam);
                self->m_pendingH = HIWORD(lParam);
                self->m_sizePending = true;
            }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void teardown(bool closeBackend) {
        if (!m_window) return;
        if (closeBackend && m_backend.close) m_backend.close();
        SetWindowLongPtrW(m_window, GWLP_USERDATA, 0);
        DestroyWindow(m_window);
        m_window = nullptr;
        m_backend = {};
    }

    EditorBackend m_backend;
    Aestra::Audio::EditorLoopPump m_pump;
    HWND m_window = nullptr;
    int m_width = 0;
    int m_height = 0;
    bool m_closeRequested = false;
    bool m_sizePending = false;
    bool m_ignoreSize = false;
    int m_pendingW = 0;
    int m_pendingH = 0;
};

} // namespace AestraHelper

#endif // _WIN32

#if defined(AESTRA_HELPER_HAS_EDITORS) && defined(AESTRA_HAS_VST3)
namespace AestraHelper {
/// The VST3 plugin's editor, for EditorHost.
inline EditorBackend vst3EditorBackend(std::shared_ptr<Aestra::Audio::VST3PluginInstance> plugin) {
    EditorBackend b;
    b.open = [plugin](const EditorBackend::MakeWindow& makeWindow, Aestra::Audio::EditorLoopPump& pump,
                      std::function<void(int, int)> resize) { return plugin->openEditorEmbedded(makeWindow, pump, std::move(resize)); };
    b.close = [plugin] { plugin->closeEditor(); };
    b.canResize = [plugin] { return plugin->canResizeEditor(); };
    b.resize = [plugin](int w, int h) { plugin->resizeEditor(w, h); };
    b.sync = [plugin] { plugin->pumpControllerSync(); };
    return b;
}
} // namespace AestraHelper
#endif
