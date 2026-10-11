// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraPlatform.h"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace Aestra {

/**
 * @brief Run a native file/folder picker without blocking the UI thread.
 *
 * IPlatformUtils' pickers block until the user answers. On Linux that is an
 * XDG-portal round trip or an external process: waiting on the UI thread stops
 * event dispatch and repainting, and compositors flag the app as hung. So the
 * picker runs on a DETACHED worker and only its result comes back.
 *
 * Lifetime rules (the reason this exists instead of per-dialog std::async):
 *  - Nothing ever waits for the worker. A std::future from std::async blocks in
 *    its destructor, which turns "close the window while a picker is open"
 *    into a hang until the user dismisses the picker.
 *  - The worker holds Platform::getUtilsShared(), never the raw getUtils()
 *    pointer, which Platform::shutdown() frees while a picker may still be up.
 *  - The worker touches only the shared hand-off state below, never the caller.
 *
 * On Windows the common dialogs are owned by the calling (UI) thread and pump
 * its message loop themselves, so the picker runs inline there; the result
 * still arrives through the same path, one frame later.
 */
using FileDialogCall = std::function<std::string(IPlatformUtils&)>;

/// Start @p call; @p deliver receives the result (empty = cancelled or no
/// picker) on the worker thread. It must only hand the result to the UI thread.
/// Returns false when the platform layer is unavailable (deliver is not called).
inline bool runFileDialogDetached(FileDialogCall call, std::function<void(std::string)> deliver) {
    std::shared_ptr<IPlatformUtils> utils = Platform::getUtilsShared();
    if (!utils || !call || !deliver) return false;
#if defined(_WIN32)
    deliver(call(*utils));
#else
    std::thread([utils = std::move(utils), call = std::move(call), deliver = std::move(deliver)]() {
        deliver(call(*utils));
    }).detach();
#endif
    return true;
}

/**
 * @brief A picker owned by a UI component that polls it from onUpdate().
 *
 * Destroying the owner while the picker is open is safe and immediate: the
 * worker keeps the shared state alive and its late result is dropped.
 */
class PendingFileDialog {
public:
    /// False if a picker from this owner is already open, or no platform layer.
    bool start(FileDialogCall call) {
        if (isPending()) return false;
        auto state = std::make_shared<State>();
        const bool started = runFileDialogDetached(std::move(call), [state](std::string result) {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->result = std::move(result);
            state->done = true;
        });
        if (started) m_state = std::move(state);
        return started;
    }

    bool isPending() const { return m_state != nullptr; }

    /// On the UI thread: the picker's result once it has closed (exactly once),
    /// std::nullopt while it is still open or when none was started.
    std::optional<std::string> takeResult() {
        if (!m_state) return std::nullopt;
        std::string result;
        {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            if (!m_state->done) return std::nullopt;
            result = std::move(m_state->result);
        }
        m_state.reset();
        return result;
    }

private:
    struct State {
        std::mutex mutex;
        bool done = false;
        std::string result;
    };
    std::shared_ptr<State> m_state;
};

} // namespace Aestra
