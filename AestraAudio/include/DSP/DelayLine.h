#pragma once

#include <array>
#include <cstdint>
#include <algorithm>

namespace Aestra::Audio {

/**
 * @brief Fixed-capacity delay line for RT-safe plugin delay compensation
 * 
 * Uses a ring buffer with compile-time capacity. No allocations after construction.
 * Thread-safe for single writer, single reader (RT audio thread).
 * 
 * Maximum delay: 16384 samples (~340ms @ 48kHz, ~170ms @ 96kHz)
 */
template<typename T, size_t Capacity>
class DelayLine {
public:
    DelayLine() = default;
    
    /**
     * @brief Set delay in samples (must be <= Capacity)
     * NOT RT-SAFE: Call from main thread only
     */
    void setDelay(uint32_t delaySamples) noexcept {
        m_delay = (delaySamples <= Capacity) ? delaySamples : Capacity;
        reset();
    }
    
    /**
     * @brief Reset delay line to silence
     * NOT RT-SAFE: Call from main thread only
     */
    void reset() noexcept {
        m_buffer.fill(T{0});
        m_writePos = 0;
    }
    
    /**
     * @brief Process a single sample through the delay line
     * RT-SAFE: No allocations, deterministic
     */
    inline T process(T input) noexcept {
        // Write input
        m_buffer[m_writePos] = input;
        
        // Calculate read position (writePos - delay)
        uint32_t readPos = (m_writePos + Capacity - m_delay) % Capacity;
        
        // Read delayed output
        T output = m_buffer[readPos];
        
        // Advance write position
        m_writePos = (m_writePos + 1) % Capacity;
        
        return output;
    }
    
    /**
     * @brief Process a block of samples
     * RT-SAFE: No allocations, deterministic
     */
    void processBlock(const T* input, T* output, uint32_t numSamples) noexcept {
        for (uint32_t i = 0; i < numSamples; ++i) {
            output[i] = process(input[i]);
        }
    }
    
    uint32_t getDelay() const noexcept { return m_delay; }
    uint32_t getCapacity() const noexcept { return Capacity; }
    
private:
    std::array<T, Capacity> m_buffer{};
    uint32_t m_writePos{0};
    uint32_t m_delay{0};
};

} // namespace Aestra::Audio
