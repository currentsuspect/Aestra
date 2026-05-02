// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <atomic>
#include <cassert>

namespace Aestra {
namespace Audio {

using RealtimeMisuseHandler = void (*)(const char* apiName) noexcept;

inline thread_local int g_realtimeAudioThreadDepth = 0;
inline std::atomic<RealtimeMisuseHandler> g_realtimeMisuseHandler{nullptr};

inline bool isRealtimeAudioThread() noexcept {
    return g_realtimeAudioThreadDepth > 0;
}

inline RealtimeMisuseHandler setRealtimeMisuseHandler(RealtimeMisuseHandler handler) noexcept {
    return g_realtimeMisuseHandler.exchange(handler, std::memory_order_acq_rel);
}

inline bool reportRealtimeMisuse(const char* apiName) noexcept {
    if (!isRealtimeAudioThread()) {
        return false;
    }

    if (auto handler = g_realtimeMisuseHandler.load(std::memory_order_acquire)) {
        handler(apiName);
    } else {
#ifndef NDEBUG
        assert(false && "Non-real-time API called from the audio thread");
#endif
    }

    return true;
}

class ScopedRealtimeAudioThread {
public:
    ScopedRealtimeAudioThread() noexcept { ++g_realtimeAudioThreadDepth; }
    ~ScopedRealtimeAudioThread() noexcept { --g_realtimeAudioThreadDepth; }

    ScopedRealtimeAudioThread(const ScopedRealtimeAudioThread&) = delete;
    ScopedRealtimeAudioThread& operator=(const ScopedRealtimeAudioThread&) = delete;
};

} // namespace Audio
} // namespace Aestra
