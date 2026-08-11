// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <atomic>
#include <cstdint>

namespace Aestra::Audio::detail {

// One atomic word owns admission to RT-visible buffer storage. The high bit is
// reserved for a control-thread configuration transaction; the remaining bits
// count callbacks that have already entered processBlock().
inline constexpr uint32_t kBufferConfigOwnedBit = uint32_t{1} << 31;
inline constexpr uint32_t kProcessBlockCountMask = ~kBufferConfigOwnedBit;

inline bool tryEnterProcessBlock(std::atomic<uint32_t>& state) noexcept {
    uint32_t observed = state.load(std::memory_order_acquire);
    for (;;) {
        if ((observed & kBufferConfigOwnedBit) != 0) {
            return false;
        }
        if ((observed & kProcessBlockCountMask) == kProcessBlockCountMask) {
            return false;
        }
        if (state.compare_exchange_weak(observed, observed + 1, std::memory_order_acquire,
                                        std::memory_order_relaxed)) {
            return true;
        }
    }
}

inline void leaveProcessBlock(std::atomic<uint32_t>& state) noexcept {
    state.fetch_sub(1, std::memory_order_release);
}

inline bool tryBeginBufferConfig(std::atomic<uint32_t>& state) noexcept {
    uint32_t expected = 0;
    return state.compare_exchange_strong(expected, kBufferConfigOwnedBit, std::memory_order_acq_rel,
                                         std::memory_order_acquire);
}

inline void endBufferConfig(std::atomic<uint32_t>& state) noexcept {
    state.store(0, std::memory_order_release);
}

} // namespace Aestra::Audio::detail
