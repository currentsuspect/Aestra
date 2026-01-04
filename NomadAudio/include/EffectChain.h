// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PluginHost.h"
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace Nomad {
namespace Audio {

// Forward declaration
class PluginManager;

/**
 * @brief Single slot in an effect chain
 */
struct EffectSlot {
    PluginInstancePtr plugin;       ///< Plugin instance (nullptr = empty)
    std::atomic<bool> bypassed{false};  ///< Bypass this slot
    std::atomic<float> dryWetMix{1.0f}; ///< 0.0 = dry only, 1.0 = wet only
    
    bool isEmpty() const { return plugin == nullptr; }
};

/**
 * @brief Effect chain for insert effects on a mixer channel
 * 
 * An EffectChain contains a fixed number of slots that hold plugin instances.
 * Audio flows through each non-empty slot in sequence. Slots can be bypassed
 * individually and have dry/wet mix controls.
 * 
 * Thread Safety:
 * - Slot modification (insert/remove) should be done from main thread
 * - process() is RT-safe and called from audio thread
 * - Bypass and dry/wet can be changed from any thread (atomic)
 * 
 * Processing Flow:
 * @code
 *   Input -> Slot 0 -> Slot 1 -> ... -> Slot N -> Output
 *            (if not empty/bypassed)
 * @endcode
 * 
 * Usage:
 * @code
 *   EffectChain chain;
 *   chain.insertPlugin(0, reverbPlugin);
 *   chain.insertPlugin(1, compressorPlugin);
 *   
 *   // In audio callback:
 *   chain.process(buffer, 2, numFrames);
 * @endcode
 */
class EffectChain {
public:
    static constexpr size_t MAX_SLOTS = 10;
    
    EffectChain();
    ~EffectChain();
    
    // Non-copyable
    EffectChain(const EffectChain&) = delete;
    EffectChain& operator=(const EffectChain&) = delete;
    
    // ==============================
    // Slot Management
    // ==============================
    
    /**
     * @brief Insert plugin into slot
     * 
     * @param slotIndex Slot index (0 to MAX_SLOTS-1)
     * @param plugin Plugin instance to insert
     * @return true if inserted successfully
     */
    bool insertPlugin(size_t slotIndex, PluginInstancePtr plugin);
    
    /**
     * @brief Remove plugin from slot
     * 
     * @param slotIndex Slot index
     * @return The removed plugin, or nullptr if slot was empty
     */
    PluginInstancePtr removePlugin(size_t slotIndex);
    
    /**
     * @brief Move plugin from one slot to another
     * 
     * @param fromSlot Source slot index
     * @param toSlot Destination slot index
     * @return true if move was successful
     */
    bool movePlugin(size_t fromSlot, size_t toSlot);
    
    /**
     * @brief Swap plugins between two slots
     */
    bool swapPlugins(size_t slot1, size_t slot2);
    
    /**
     * @brief Get plugin in slot
     * @return Plugin instance or nullptr if empty
     */
    PluginInstancePtr getPlugin(size_t slotIndex) const;
    
    /**
     * @brief Get slot information
     * @return Pointer to slot, or nullptr if invalid index
     */
    const EffectSlot* getSlot(size_t slotIndex) const;
    
    /**
     * @brief Check if slot is empty
     */
    bool isSlotEmpty(size_t slotIndex) const;
    
    /**
     * @brief Get first empty slot index
     * @return Slot index, or MAX_SLOTS if all full
     */
    size_t getFirstEmptySlot() const;
    
    /**
     * @brief Get number of active (non-empty) slots
     */
    size_t getActiveSlotCount() const;
    
    /**
     * @brief Clear all slots
     */
    void clear();
    
    // ==============================
    // Bypass Control
    // ==============================
    
    /**
     * @brief Set bypass state for a slot
     */
    void setSlotBypassed(size_t slotIndex, bool bypassed);
    
    /**
     * @brief Check if slot is bypassed
     */
    bool isSlotBypassed(size_t slotIndex) const;
    
    /**
     * @brief Bypass entire chain
     */
    void setChainBypassed(bool bypassed) { m_chainBypassed.store(bypassed); }
    bool isChainBypassed() const { return m_chainBypassed.load(); }
    
    // ==============================
    // Dry/Wet Mix
    // ==============================
    
    /**
     * @brief Set dry/wet mix for a slot (0.0 = dry, 1.0 = wet)
     */
    void setSlotDryWetMix(size_t slotIndex, float mix);
    
    /**
     * @brief Get dry/wet mix for a slot
     */
    float getSlotDryWetMix(size_t slotIndex) const;
    
    // ==============================
    // Audio Processing (RT-safe)
    // ==============================
    
    /**
     * @brief Prepare for processing
     * 
     * Call before audio processing starts. Allocates internal buffers.
     * 
     * @param sampleRate Audio sample rate
     * @param maxBlockSize Maximum frames per process() call
     */
    void prepare(double sampleRate, uint32_t maxBlockSize);
    
    /**
     * @brief Process audio through the effect chain
     * 
     * Processes audio in-place through all non-bypassed slots.
     * This method is RT-safe.
     * 
     * @param buffer Array of channel buffers (will be modified in-place)
     * @param numChannels Number of channels (typically 2 for stereo)
     * @param numFrames Number of frames to process
     */
    void process(float** buffer, uint32_t numChannels, uint32_t numFrames);
    
    // ==============================
    // State Management
    // ==============================
    
    /**
     * @brief Save entire chain state
     * @return Binary state data
     */
    std::vector<uint8_t> saveState() const;
    
    /**
     * @brief Load entire chain state
     * @param state State data from saveState()
     * @param manager PluginManager for recreating instances
     * @return true on success
     */
    bool loadState(const std::vector<uint8_t>& state, PluginManager& manager);
    
    // ==============================
    // Latency
    // ==============================
    
    /**
     * @brief Get total chain latency in samples
     * 
     * Sum of latencies from all active plugins.
     */
    uint32_t getTotalLatency() const;
    
private:
    std::array<EffectSlot, MAX_SLOTS> m_slots;
    std::atomic<bool> m_chainBypassed{false};
    
    // Pre-allocated buffers for dry/wet mixing
    std::vector<float> m_dryBuffer;
    uint32_t m_maxBlockSize = 0;
    double m_sampleRate = 44100.0;
};

} // namespace Audio
} // namespace Nomad
