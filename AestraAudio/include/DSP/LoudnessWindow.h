// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace Aestra {
namespace Audio {

/**
 * @brief Live momentary (400 ms) and short-term (3 s) loudness (EBU R128 / ITU-R BS.1770, V81-M2).
 *
 * Fed the K-weighted energy of each sample (left squared plus right squared) by the audio thread, which is
 * the only writer. Energy is averaged in 100 ms sub-blocks; momentary is the mean of the last four, and
 * short-term the mean of the last thirty. Both read as silence until a full momentary window exists, and the
 * short-term value ramps in over the first three seconds (it averages what it has), so a meter is never
 * dark for three seconds after play.
 *
 * Any thread may read the two getters. A reset is requested from any thread and applied by the writer at
 * its next sub-block boundary, so the window never races the audio thread.
 */
class LoudnessWindow {
public:
    static constexpr float kSilence = -144.0f;
    static constexpr size_t kSubBlocksMomentary = 4;
    static constexpr size_t kSubBlocksShortTerm = 30;

    void prepare(double sampleRate) noexcept {
        m_subLength.store(static_cast<uint32_t>(std::max(1.0, std::round(sampleRate * 0.1))), std::memory_order_relaxed);
        requestReset();
    }

    /// Any thread: forget the history; the readouts drop to silence at once.
    void requestReset() noexcept {
        m_resetRequested.store(true, std::memory_order_release);
        m_momentary.store(kSilence, std::memory_order_relaxed);
        m_shortTerm.store(kSilence, std::memory_order_relaxed);
    }

    /// Audio thread, once per sample.
    inline void add(double kWeightedEnergy) noexcept {
        m_sum += kWeightedEnergy;
        if (++m_count >= m_subLength.load(std::memory_order_relaxed))
            closeSubBlock();
    }

    float momentaryLufs() const noexcept { return m_momentary.load(std::memory_order_relaxed); }
    float shortTermLufs() const noexcept { return m_shortTerm.load(std::memory_order_relaxed); }

    static float energyToLufs(double meanEnergy) noexcept {
        return meanEnergy > 1e-14 ? static_cast<float>(-0.691 + 10.0 * std::log10(meanEnergy)) : kSilence;
    }

private:
    void closeSubBlock() noexcept {
        if (m_resetRequested.exchange(false, std::memory_order_acquire)) {
            m_ring.fill(0.0);
            m_write = 0;
            m_filled = 0;
        }
        m_ring[m_write] = m_sum / static_cast<double>(m_count);
        m_write = (m_write + 1) % kSubBlocksShortTerm;
        m_filled = std::min(m_filled + 1, kSubBlocksShortTerm);
        m_sum = 0.0;
        m_count = 0;

        if (m_filled < kSubBlocksMomentary) {
            m_momentary.store(kSilence, std::memory_order_relaxed);
            m_shortTerm.store(kSilence, std::memory_order_relaxed);
            return;
        }
        m_momentary.store(energyToLufs(meanOfLast(kSubBlocksMomentary)), std::memory_order_relaxed);
        m_shortTerm.store(energyToLufs(meanOfLast(m_filled)), std::memory_order_relaxed);
    }

    double meanOfLast(size_t n) const noexcept {
        double total = 0.0;
        for (size_t i = 0; i < n; ++i)
            total += m_ring[(m_write + kSubBlocksShortTerm - 1 - i) % kSubBlocksShortTerm];
        return total / static_cast<double>(n);
    }

    // Audio thread only.
    std::array<double, kSubBlocksShortTerm> m_ring{};
    size_t m_write{0};
    size_t m_filled{0};
    double m_sum{0.0};
    uint32_t m_count{0};

    std::atomic<uint32_t> m_subLength{4800}; ///< 100 ms; set when the sample rate changes
    std::atomic<bool> m_resetRequested{false};
    std::atomic<float> m_momentary{kSilence};
    std::atomic<float> m_shortTerm{kSilence};
};

} // namespace Audio
} // namespace Aestra
