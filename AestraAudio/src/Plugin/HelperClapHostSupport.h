// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The host side of a CLAP plugin in the sandbox helper (V82-H1 / H3): the extensions a plugin may ask its host for and
// the state they share. Included by HelperClapModule.h only.
//
//  * clap.thread-check   which thread is "main" and which is "audio": audio = inside process(), main = anywhere else
//  * clap.params         the plugin changed or wants a flush. process() runs continuously, so a flush is never needed
//                        on its own: its parameter output arrives with the next block
//  * clap.latency/state  noted and ignored: the app re-reads latency every 64 blocks and saves state on request
//  * clap.gui            the plugin asks for another size
//  * clap.timer-support, clap.posix-fd-support (POSIX only)
//                        what an X11 editor needs to run (and what some plugins use with no editor at all): an
//                        EditorLoopPump the helper's command loop services between commands.

#include "ClapAbiMirror.h"
#include "Plugin/EditorLoopPump.h"

#include <atomic>
#include <cstring>
#include <functional>
#include <mutex>
#ifndef _WIN32
#include <poll.h>
#endif
#include <utility>
#include <vector>

namespace {

using namespace AestraHelper::ClapAbi;

struct ClapHostContext {
    const ClapPlugin* plugin = nullptr; // set as soon as the plugin exists: it may register timers inside init()

    // The plugin's timers and descriptors live as long as the plugin does, editor open or not (some plugins register a
    // timer in init() and depend on it for their own bookkeeping). The helper's command loop runs this pump between
    // commands, so on_timer / on_fd arrive on the thread that loaded the plugin.
    Aestra::Audio::EditorLoopPump pump;

    std::mutex mutex; // guards the lists below
    std::function<void(int, int)> requestResize; // set while an editor is open
    std::vector<std::pair<uint32_t, int>> timers; // CLAP timer id -> pump token
    std::vector<std::pair<int, int>> fds;         // file descriptor -> pump token
    uint32_t nextTimerId = 1;

    std::atomic<bool> callbackRequested{false}; // clap_host.request_callback: call plugin->on_main_thread()

    static bool& inProcess() {
        static thread_local bool flag = false;
        return flag;
    }

    /// Plugin gone: forget everything it had registered.
    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& t : timers) pump.removeTimer(t.second);
        for (const auto& f : fds) pump.removeFd(f.second);
        timers.clear();
        fds.clear();
        requestResize = nullptr;
        plugin = nullptr;
    }

    /// When the next timer is due, in ms (lowers @p timeoutMs, never raises it; -1 means no limit).
    void limitWait(int& timeoutMs) {
        const int wait = pump.msUntilNextTimer();
        if (wait >= 0 && (timeoutMs < 0 || wait < timeoutMs)) timeoutMs = wait;
    }
#ifndef _WIN32
    /// Descriptors to wait on and when the next timer is due (lowers @p timeoutMs, never raises it).
    void pollFds(std::vector<pollfd>& out, int& timeoutMs) {
        for (const int fd : pump.fds()) out.push_back({fd, POLLIN, 0});
        limitWait(timeoutMs);
    }
#endif
    /// Run due timers and ready descriptors. Never blocks.
    void service() {
#ifndef _WIN32
        std::vector<pollfd> ready;
        for (const int fd : pump.fds()) ready.push_back({fd, POLLIN, 0});
        if (!ready.empty() && poll(ready.data(), static_cast<nfds_t>(ready.size()), 0) > 0)
            for (const auto& p : ready)
                if (p.revents & (POLLIN | POLLHUP)) pump.dispatchFd(p.fd);
#endif
        pump.fireDueTimers();
    }
};

inline ClapHostContext* contextOf(const ClapHost* host) {
    return host ? static_cast<ClapHostContext*>(host->hostData) : nullptr;
}

bool hostIsMainThread(const ClapHost*) { return !ClapHostContext::inProcess(); }
bool hostIsAudioThread(const ClapHost*) { return ClapHostContext::inProcess(); }
const ClapHostThreadCheck kHostThreadCheck = {hostIsMainThread, hostIsAudioThread};

void hostParamsRescan(const ClapHost*, uint32_t) {}
void hostParamsClear(const ClapHost*, uint32_t, uint32_t) {}
void hostParamsRequestFlush(const ClapHost*) {}
const ClapHostParams kHostParams = {hostParamsRescan, hostParamsClear, hostParamsRequestFlush};

void hostLatencyChanged(const ClapHost*) {}
const ClapHostLatency kHostLatency = {hostLatencyChanged};
void hostStateMarkDirty(const ClapHost*) {}
const ClapHostState kHostState = {hostStateMarkDirty};

void hostGuiResizeHintsChanged(const ClapHost*) {}
bool hostGuiRequestResize(const ClapHost* host, uint32_t width, uint32_t height) {
    auto* ctx = contextOf(host);
    if (!ctx) return false;
    std::function<void(int, int)> resize;
    {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        resize = ctx->requestResize;
    }
    if (!resize) return false;
    resize(static_cast<int>(width), static_cast<int>(height));
    return true;
}
bool hostGuiRequestShow(const ClapHost*) { return true; }  // the window is already shown
bool hostGuiRequestHide(const ClapHost*) { return false; } // the user closes the window; a plugin does not hide it
void hostGuiClosed(const ClapHost*, bool) {}
const ClapHostGui kHostGui = {hostGuiResizeHintsChanged, hostGuiRequestResize, hostGuiRequestShow, hostGuiRequestHide,
                              hostGuiClosed};

bool hostRegisterTimer(const ClapHost* host, uint32_t periodMs, uint32_t* timerId) {
    auto* ctx = contextOf(host);
    if (!ctx || !timerId) return false;
    std::lock_guard<std::mutex> lock(ctx->mutex);
    if (!ctx->plugin) return false;
    const uint32_t id = ctx->nextTimerId++;
    const int token = ctx->pump.addTimer(std::chrono::milliseconds(periodMs < 1 ? 1 : periodMs), [ctx, id]() {
        // The extension is looked up when the timer fires: the plugin may have asked for it before it could answer.
        const ClapPlugin* plugin = ctx->plugin;
        const auto* ext = plugin && plugin->getExtension
                              ? static_cast<const ClapPluginTimerSupport*>(plugin->getExtension(plugin, kClapExtTimerSupport))
                              : nullptr;
        if (ext && ext->onTimer) ext->onTimer(plugin, id);
    });
    ctx->timers.emplace_back(id, token);
    *timerId = id;
    return true;
}
bool hostUnregisterTimer(const ClapHost* host, uint32_t timerId) {
    auto* ctx = contextOf(host);
    if (!ctx) return false;
    std::lock_guard<std::mutex> lock(ctx->mutex);
    for (auto it = ctx->timers.begin(); it != ctx->timers.end(); ++it) {
        if (it->first == timerId) {
            ctx->pump.removeTimer(it->second);
            ctx->timers.erase(it);
            return true;
        }
    }
    return false;
}
const ClapHostTimerSupport kHostTimerSupport = {hostRegisterTimer, hostUnregisterTimer};

// Descriptors are watched for readability. A plugin that also asks to be told when one is writable gets read events
// only; editors that need write readiness are not known (the VST3 run loop has no such thing either).
bool hostRegisterFd(const ClapHost* host, int fd, uint32_t) {
    auto* ctx = contextOf(host);
    if (!ctx) return false;
    std::lock_guard<std::mutex> lock(ctx->mutex);
    if (!ctx->plugin) return false;
    const int token = ctx->pump.addFd(fd, [ctx, fd]() {
        const ClapPlugin* plugin = ctx->plugin;
        const auto* ext = plugin && plugin->getExtension
                              ? static_cast<const ClapPluginPosixFdSupport*>(plugin->getExtension(plugin, kClapExtPosixFdSupport))
                              : nullptr;
        if (ext && ext->onFd) ext->onFd(plugin, fd, kClapPosixFdRead);
    });
    ctx->fds.emplace_back(fd, token);
    return true;
}
bool hostModifyFd(const ClapHost*, int, uint32_t) { return true; }
bool hostUnregisterFd(const ClapHost* host, int fd) {
    auto* ctx = contextOf(host);
    if (!ctx) return false;
    std::lock_guard<std::mutex> lock(ctx->mutex);
    bool found = false;
    for (auto it = ctx->fds.begin(); it != ctx->fds.end();) {
        if (it->first == fd) {
            ctx->pump.removeFd(it->second);
            it = ctx->fds.erase(it);
            found = true;
        } else {
            ++it;
        }
    }
    return found;
}
const ClapHostPosixFdSupport kHostPosixFdSupport = {hostRegisterFd, hostModifyFd, hostUnregisterFd};

const void* hostGetExtension(const ClapHost* host, const char* id) {
    if (!id) return nullptr;
    if (!std::strcmp(id, kClapExtThreadCheck)) return &kHostThreadCheck;
    if (!std::strcmp(id, kClapExtHostParams)) return &kHostParams;
    if (!std::strcmp(id, kClapExtHostLatency)) return &kHostLatency;
    if (!std::strcmp(id, kClapExtHostState)) return &kHostState;
    if (!std::strcmp(id, kClapExtGui)) return &kHostGui;
    if (!std::strcmp(id, kClapExtTimerSupport)) return &kHostTimerSupport;
#ifndef _WIN32
    // File descriptors are a POSIX idea: CLAP offers posix-fd-support only there.
    if (!std::strcmp(id, kClapExtPosixFdSupport)) return &kHostPosixFdSupport;
#endif
    (void)host;
    return nullptr;
}

void hostRequestRestart(const ClapHost*) {}
void hostRequestProcess(const ClapHost*) {} // process() runs every block regardless
void hostRequestCallback(const ClapHost* host) {
    if (auto* ctx = contextOf(host)) ctx->callbackRequested.store(true, std::memory_order_release);
}

} // namespace
