#include "EffectChain.h"
#include "PluginManager.h"
#include <algorithm>
#include <cstring>

namespace Aestra {
namespace Audio {

EffectChain::EffectChain() = default;
EffectChain::~EffectChain() = default;



// ==============================
// Slot Management
// ==============================

bool EffectChain::insertPlugin(size_t slotIndex, PluginInstancePtr plugin) {
    if (slotIndex >= MAX_SLOTS) {
        return false;
    }
    
    m_slots[slotIndex].plugin = std::move(plugin);
    m_slots[slotIndex].bypassed.store(false);
    m_slots[slotIndex].dryWetMix.store(1.0f);
    
    return true;
}

PluginInstancePtr EffectChain::removePlugin(size_t slotIndex) {
    if (slotIndex >= MAX_SLOTS) {
        return nullptr;
    }
    
    auto plugin = std::move(m_slots[slotIndex].plugin);
    m_slots[slotIndex].plugin = nullptr;
    return plugin;
}

bool EffectChain::movePlugin(size_t fromSlot, size_t toSlot) {
    if (fromSlot >= MAX_SLOTS || toSlot >= MAX_SLOTS) {
        return false;
    }
    
    if (fromSlot == toSlot) {
        return true;
    }
    
    // Can only move to empty slot
    if (!m_slots[toSlot].isEmpty()) {
        return false;
    }
    
    m_slots[toSlot].plugin = std::move(m_slots[fromSlot].plugin);
    m_slots[toSlot].bypassed.store(m_slots[fromSlot].bypassed.load());
    m_slots[toSlot].dryWetMix.store(m_slots[fromSlot].dryWetMix.load());
    
    m_slots[fromSlot].plugin = nullptr;
    m_slots[fromSlot].bypassed.store(false);
    m_slots[fromSlot].dryWetMix.store(1.0f);
    
    return true;
}

bool EffectChain::swapPlugins(size_t slot1, size_t slot2) {
    if (slot1 >= MAX_SLOTS || slot2 >= MAX_SLOTS) {
        return false;
    }
    
    if (slot1 == slot2) {
        return true;
    }
    
    std::swap(m_slots[slot1].plugin, m_slots[slot2].plugin);
    
    bool bypass1 = m_slots[slot1].bypassed.load();
    float mix1 = m_slots[slot1].dryWetMix.load();
    
    m_slots[slot1].bypassed.store(m_slots[slot2].bypassed.load());
    m_slots[slot1].dryWetMix.store(m_slots[slot2].dryWetMix.load());
    
    m_slots[slot2].bypassed.store(bypass1);
    m_slots[slot2].dryWetMix.store(mix1);
    
    return true;
}

PluginInstancePtr EffectChain::getPlugin(size_t slotIndex) const {
    if (slotIndex >= MAX_SLOTS) {
        return nullptr;
    }
    return m_slots[slotIndex].plugin;
}

const EffectSlot* EffectChain::getSlot(size_t slotIndex) const {
    if (slotIndex >= MAX_SLOTS) {
        return nullptr;
    }
    return &m_slots[slotIndex];
}

bool EffectChain::isSlotEmpty(size_t slotIndex) const {
    if (slotIndex >= MAX_SLOTS) {
        return true;
    }
    return m_slots[slotIndex].isEmpty();
}

size_t EffectChain::getFirstEmptySlot() const {
    for (size_t i = 0; i < MAX_SLOTS; ++i) {
        if (m_slots[i].isEmpty()) {
            return i;
        }
    }
    return MAX_SLOTS;
}

size_t EffectChain::getActiveSlotCount() const {
    size_t count = 0;
    for (const auto& slot : m_slots) {
        if (!slot.isEmpty()) {
            ++count;
        }
    }
    return count;
}

void EffectChain::clear() {
    for (auto& slot : m_slots) {
        slot.plugin = nullptr;
        slot.bypassed.store(false);
        slot.dryWetMix.store(1.0f);
    }
}

// ==============================
// Bypass Control
// ==============================

void EffectChain::setSlotBypassed(size_t slotIndex, bool bypassed) {
    if (slotIndex < MAX_SLOTS) {
        m_slots[slotIndex].bypassed.store(bypassed, std::memory_order_release);
    }
}

bool EffectChain::isSlotBypassed(size_t slotIndex) const {
    if (slotIndex >= MAX_SLOTS) {
        return true;
    }
    return m_slots[slotIndex].bypassed.load(std::memory_order_acquire);
}

// ==============================
// Dry/Wet Mix
// ==============================

void EffectChain::setSlotDryWetMix(size_t slotIndex, float mix) {
    if (slotIndex < MAX_SLOTS) {
        m_slots[slotIndex].dryWetMix.store(std::clamp(mix, 0.0f, 1.0f), std::memory_order_release);
    }
}

float EffectChain::getSlotDryWetMix(size_t slotIndex) const {
    if (slotIndex >= MAX_SLOTS) {
        return 1.0f;
    }
    return m_slots[slotIndex].dryWetMix.load(std::memory_order_acquire);
}

// ==============================
// Audio Processing
// ==============================

void EffectChain::prepare(double sampleRate, uint32_t maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    
    // Pre-allocate dry buffer for dry/wet mixing (stereo)
    if (maxBlockSize == 0) return;
    const size_t required = static_cast<size_t>(maxBlockSize) * 2;
    if (m_dryBuffer.size() < required) {
        m_dryBuffer.resize(required);
    }
}

void EffectChain::process(float** buffer, uint32_t numChannels, uint32_t numFrames) {
    // Skip if entire chain is bypassed
    if (m_chainBypassed.load(std::memory_order_acquire)) {
        return;
    }
    
    // Process each slot in sequence
    for (const auto& slot : m_slots) {
        // Skip empty or bypassed slots
        if (slot.isEmpty() || slot.bypassed.load(std::memory_order_acquire)) {
            continue;
        }
        
        auto& plugin = slot.plugin;
        if (!plugin) {
             continue;
        }
        
        if (!plugin->isActive()) {
             // printf("[EffectChain] Plugin exists but inactive!\n");
             continue;
        }
        
        float dryWet = slot.dryWetMix.load(std::memory_order_acquire);
        
        // If fully wet, process directly
        if (dryWet >= 0.999f) {
            // Process in-place
            plugin->process(buffer, buffer, numChannels, numChannels, numFrames);
            // printf("[EffectChain] Processed plugin %s (Wet)\n", plugin->getInfo().name.c_str());
        }
        // If not fully wet, need to blend
        else if (dryWet > 0.001f) {
            const uint32_t blendChannels = (numChannels < 2) ? numChannels : 2;
            const size_t requiredDry = static_cast<size_t>(numFrames) * static_cast<size_t>(blendChannels);
            if (m_dryBuffer.size() < requiredDry) {
                // Not prepared (or prepared for a smaller block). Stay RT-safe: no allocation.
                // Fallback: process fully wet rather than crashing or dropping audio.
                plugin->process(buffer, buffer, numChannels, numChannels, numFrames);
                continue;
            }

            // Save dry signal
            for (uint32_t ch = 0; ch < blendChannels; ++ch) {
                std::memcpy(m_dryBuffer.data() + ch * numFrames, 
                           buffer[ch], 
                           numFrames * sizeof(float));
            }
            
            // Process wet
            plugin->process(buffer, buffer, numChannels, numChannels, numFrames);
            
            // Blend dry/wet
            float wetGain = dryWet;
            float dryGain = 1.0f - dryWet;
            
            for (uint32_t ch = 0; ch < blendChannels; ++ch) {
                const float* dry = m_dryBuffer.data() + ch * numFrames;
                float* wet = buffer[ch];
                
                for (uint32_t i = 0; i < numFrames; ++i) {
                    wet[i] = dry[i] * dryGain + wet[i] * wetGain;
                }
            }
        }
        // dryWet <= 0.001f means fully dry, skip processing
    }
}

// ==============================
// State Management
// ==============================

std::vector<uint8_t> EffectChain::saveState() const {
    std::vector<uint8_t> state;
    
    // Write header
    state.push_back('N');
    state.push_back('E');
    state.push_back('C');  // Aestra Effect Chain
    state.push_back(1);    // Version
    
    // Write slot count
    state.push_back(static_cast<uint8_t>(MAX_SLOTS));
    
    // Write each slot
    for (size_t i = 0; i < MAX_SLOTS; ++i) {
        const auto& slot = m_slots[i];
        
        if (slot.isEmpty()) {
            state.push_back(0); // Empty flag
            continue;
        }
        
        state.push_back(1); // Has plugin flag
        
        // Save plugin ID
        const auto& info = slot.plugin->getInfo();
        uint32_t idLen = static_cast<uint32_t>(info.id.size());
        state.insert(state.end(), 
                    reinterpret_cast<const uint8_t*>(&idLen),
                    reinterpret_cast<const uint8_t*>(&idLen) + sizeof(idLen));
        state.insert(state.end(), info.id.begin(), info.id.end());
        
        // Save bypass state
        state.push_back(slot.bypassed.load() ? 1 : 0);
        
        // Save dry/wet
        float dryWet = slot.dryWetMix.load();
        state.insert(state.end(),
                    reinterpret_cast<const uint8_t*>(&dryWet),
                    reinterpret_cast<const uint8_t*>(&dryWet) + sizeof(dryWet));
        
        // Save plugin state
        auto pluginState = slot.plugin->saveState();
        uint32_t stateLen = static_cast<uint32_t>(pluginState.size());
        state.insert(state.end(),
                    reinterpret_cast<const uint8_t*>(&stateLen),
                    reinterpret_cast<const uint8_t*>(&stateLen) + sizeof(stateLen));
        state.insert(state.end(), pluginState.begin(), pluginState.end());
    }
    
    return state;
}

bool EffectChain::loadState(const std::vector<uint8_t>& state, PluginManager& manager) {
    if (state.size() < 5) {
        return false;
    }
    
    // Check header
    if (state[0] != 'N' || state[1] != 'E' || state[2] != 'C' || state[3] != 1) {
        return false;
    }
    
    uint8_t slotCount = state[4];
    if (slotCount != MAX_SLOTS) {
        return false;
    }
    
    size_t offset = 5;
    
    for (size_t i = 0; i < MAX_SLOTS && offset < state.size(); ++i) {
        uint8_t hasPlugin = state[offset++];
        
        if (!hasPlugin) {
            m_slots[i].plugin = nullptr;
            continue;
        }
        
        if (offset + sizeof(uint32_t) > state.size()) {
            return false;
        }
        
        // Read plugin ID
        uint32_t idLen;
        std::memcpy(&idLen, &state[offset], sizeof(idLen));
        offset += sizeof(idLen);
        
        if (offset + idLen > state.size()) {
            return false;
        }
        
        std::string pluginId(reinterpret_cast<const char*>(&state[offset]), idLen);
        offset += idLen;
        
        // Read bypass state
        bool bypassed = state[offset++] != 0;
        
        // Read dry/wet
        float dryWet;
        std::memcpy(&dryWet, &state[offset], sizeof(dryWet));
        offset += sizeof(dryWet);
        
        // Read plugin state length
        uint32_t stateLen;
        std::memcpy(&stateLen, &state[offset], sizeof(stateLen));
        offset += sizeof(stateLen);
        
        if (offset + stateLen > state.size()) {
            return false;
        }
        
        std::vector<uint8_t> pluginState(state.begin() + offset, state.begin() + offset + stateLen);
        offset += stateLen;
        
        // Create plugin instance
        auto instance = manager.createInstanceById(pluginId);
        if (instance) {
            instance->initialize(m_sampleRate, m_maxBlockSize);
            instance->loadState(pluginState);
            instance->activate();
            
            m_slots[i].plugin = std::move(instance);
            m_slots[i].bypassed.store(bypassed);
            m_slots[i].dryWetMix.store(dryWet);
        }
    }
    
    return true;
}

// ==============================
// Latency
// ==============================

uint32_t EffectChain::getTotalLatency() const {
    uint32_t total = 0;
    
    for (const auto& slot : m_slots) {
        if (!slot.isEmpty() && !slot.bypassed.load() && slot.plugin) {
            total += slot.plugin->getLatencySamples();
        }
    }
    
    return total;
}

void EffectChain::reset() {
    // 1. Temporarily bypass the chain to silence audio input
    bool wasBypassed = m_chainBypassed.exchange(true);

    // 2. Reboot each plugin to clear internal buffers (delay lines, etc.)
    for (auto& slot : m_slots) {
        if (slot.plugin) {
            // Check if active before resetting? 
            if (slot.plugin->isActive()) {
                slot.plugin->deactivate();
                slot.plugin->activate();
            }
        }
    }

    // 3. Restore original bypass state
    m_chainBypassed.store(wasBypassed);
}

} // namespace Audio
} // namespace Aestra
