#include "EffectChain.h"

#include "PluginManager.h"
#include "RealtimeThreadGuard.h"
#include "AestraLog.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Aestra {
namespace Audio {

namespace {
bool processPluginNoexcept(IPluginInstance& plugin, const float* const* inputs, float** outputs,
                           uint32_t numInputChannels, uint32_t numOutputChannels, uint32_t numFrames) noexcept {
    try {
        plugin.process(inputs, outputs, numInputChannels, numOutputChannels, numFrames);
        return true;
    } catch (...) {
        return false;
    }
}

bool buffersAreFinite(float** buffer, uint32_t numChannels, uint32_t numFrames) noexcept {
    for (uint32_t ch = 0; ch < numChannels; ++ch) {
        float* channel = buffer[ch];
        if (!channel) {
            continue;
        }
        for (uint32_t i = 0; i < numFrames; ++i) {
            if (!std::isfinite(channel[i])) {
                return false;
            }
        }
    }
    return true;
}

void clearFloatBuffers(float** buffer, uint32_t numChannels, uint32_t numFrames) noexcept {
    for (uint32_t ch = 0; ch < numChannels; ++ch) {
        if (buffer[ch]) {
            std::fill(buffer[ch], buffer[ch] + numFrames, 0.0f);
        }
    }
}

bool isFaultBypassed(const std::shared_ptr<EffectSlotFaultState>& faultState) noexcept {
    return faultState && faultState->bypassedByNonFiniteOutput.load(std::memory_order_acquire);
}

void clearFaultState(const std::shared_ptr<EffectSlotFaultState>& faultState) noexcept {
    if (!faultState) {
        return;
    }
    faultState->bypassedByNonFiniteOutput.store(false, std::memory_order_release);
    faultState->nonFiniteOutputCount.store(0, std::memory_order_release);
}

void markNonFiniteOutputFault(const std::shared_ptr<EffectSlotFaultState>& faultState) noexcept {
    if (!faultState) {
        return;
    }
    faultState->nonFiniteOutputCount.fetch_add(1, std::memory_order_acq_rel);
    faultState->bypassedByNonFiniteOutput.store(true, std::memory_order_release);
}

void restoreDryChannels(float** buffer, const float* dryBuffer, uint32_t numChannels, uint32_t blendChannels,
                        uint32_t numFrames) noexcept {
    for (uint32_t ch = 0; ch < blendChannels; ++ch) {
        std::memcpy(buffer[ch], dryBuffer + ch * numFrames, numFrames * sizeof(float));
    }
    for (uint32_t ch = blendChannels; ch < numChannels; ++ch) {
        if (buffer[ch]) {
            std::fill(buffer[ch], buffer[ch] + numFrames, 0.0f);
        }
    }
}
} // namespace

EffectChain::EffectChain() = default;
EffectChain::~EffectChain() = default;

// ==============================
// Snapshot Publication (Pass 2)
// ==============================

void EffectChain::publishSnapshot() {
    auto snapshot = std::make_shared<EffectChainSnapshot>(m_slots);
    snapshot->isChainBypassed = m_chainBypassed.load(std::memory_order_acquire);
    m_currentSnapshot = snapshot;
}

// ==============================
// Slot Management
// ==============================

bool EffectChain::insertPlugin(size_t slotIndex, PluginInstancePtr plugin) {
    if (reportRealtimeMisuse("EffectChain::insertPlugin")) {
        return false;
    }
    if (slotIndex >= MAX_SLOTS) {
        return false;
    }

    if (plugin) {
        if (m_maxBlockSize > 0 && m_sampleRate > 0.0) {
            plugin->initialize(m_sampleRate, m_maxBlockSize);
        }
        if (!plugin->isActive()) {
            plugin->activate();
        }
        // Prewarm getInfo() here, off the RT path. Several implementations
        // (e.g. BuiltInPlugins::samplerInfo) return a function-local static
        // whose FIRST call constructs std::string fields — a heap allocation
        // plus a __cxa_guard acquire. EffectChainSnapshot::process() calls
        // getInfo() per slot on the audio thread, so without this the first
        // block after an insert pays that cost on the RT thread (measured:
        // 1x31-byte alloc; RTAllocationTrapTest, issue #432).
        (void)plugin->getInfo();
    }

    m_slots[slotIndex].plugin = std::move(plugin);
    m_slots[slotIndex].bypassed.store(false);
    m_slots[slotIndex].dryWetMix.store(1.0f);
    m_slots[slotIndex].faultState = std::make_shared<EffectSlotFaultState>();

    publishSnapshot();
    if (m_onLatencyChanged) {
        m_onLatencyChanged();
    }
    return true;
}

PluginInstancePtr EffectChain::removePlugin(size_t slotIndex) {
    if (reportRealtimeMisuse("EffectChain::removePlugin")) {
        return nullptr;
    }
    if (slotIndex >= MAX_SLOTS) {
        return nullptr;
    }

    auto plugin = std::move(m_slots[slotIndex].plugin);
    m_slots[slotIndex].plugin = nullptr;
    m_slots[slotIndex].faultState = std::make_shared<EffectSlotFaultState>();

    publishSnapshot();
    if (m_onLatencyChanged) {
        m_onLatencyChanged();
    }
    return plugin;
}

bool EffectChain::movePlugin(size_t fromSlot, size_t toSlot) {
    if (reportRealtimeMisuse("EffectChain::movePlugin")) {
        return false;
    }
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
    m_slots[toSlot].faultState = std::move(m_slots[fromSlot].faultState);
    if (!m_slots[toSlot].faultState) {
        m_slots[toSlot].faultState = std::make_shared<EffectSlotFaultState>();
    }

    m_slots[fromSlot].plugin = nullptr;
    m_slots[fromSlot].bypassed.store(false);
    m_slots[fromSlot].dryWetMix.store(1.0f);
    m_slots[fromSlot].faultState = std::make_shared<EffectSlotFaultState>();

    publishSnapshot();
    return true;
}

bool EffectChain::swapPlugins(size_t slot1, size_t slot2) {
    if (reportRealtimeMisuse("EffectChain::swapPlugins")) {
        return false;
    }
    if (slot1 >= MAX_SLOTS || slot2 >= MAX_SLOTS) {
        return false;
    }

    if (slot1 == slot2) {
        return true;
    }

    std::swap(m_slots[slot1].plugin, m_slots[slot2].plugin);
    std::swap(m_slots[slot1].faultState, m_slots[slot2].faultState);

    bool bypass1 = m_slots[slot1].bypassed.load();
    float mix1 = m_slots[slot1].dryWetMix.load();

    m_slots[slot1].bypassed.store(m_slots[slot2].bypassed.load());
    m_slots[slot1].dryWetMix.store(m_slots[slot2].dryWetMix.load());

    m_slots[slot2].bypassed.store(bypass1);
    m_slots[slot2].dryWetMix.store(mix1);

    publishSnapshot();
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
    if (reportRealtimeMisuse("EffectChain::clear")) {
        return;
    }
    for (auto& slot : m_slots) {
        slot.plugin = nullptr;
        slot.bypassed.store(false);
        slot.dryWetMix.store(1.0f);
        slot.faultState = std::make_shared<EffectSlotFaultState>();
    }
    publishSnapshot();
}

// ==============================
// Bypass Control
// ==============================

void EffectChain::setSlotBypassed(size_t slotIndex, bool bypassed) {
    if (slotIndex < MAX_SLOTS) {
        if (!bypassed) {
            clearFaultState(m_slots[slotIndex].faultState);
        }
        m_slots[slotIndex].bypassed.store(bypassed, std::memory_order_release);
        if (!reportRealtimeMisuse("EffectChain::setSlotBypassed")) {
            publishSnapshot();
            if (m_onLatencyChanged) {
                m_onLatencyChanged();
            }
        }
    }
}

bool EffectChain::isSlotBypassed(size_t slotIndex) const {
    if (slotIndex >= MAX_SLOTS) {
        return true;
    }
    return m_slots[slotIndex].bypassed.load(std::memory_order_acquire) ||
           isFaultBypassed(m_slots[slotIndex].faultState);
}

bool EffectChain::isSlotBypassedByNonFiniteOutput(size_t slotIndex) const {
    if (slotIndex >= MAX_SLOTS) {
        return false;
    }
    return isFaultBypassed(m_slots[slotIndex].faultState);
}

void EffectChain::setChainBypassed(bool bypassed) {
    m_chainBypassed.store(bypassed, std::memory_order_release);
    if (!reportRealtimeMisuse("EffectChain::setChainBypassed")) {
        publishSnapshot();
    }
}

uint64_t EffectChain::getSlotNonFiniteOutputCount(size_t slotIndex) const {
    if (slotIndex >= MAX_SLOTS || !m_slots[slotIndex].faultState) {
        return 0;
    }
    return m_slots[slotIndex].faultState->nonFiniteOutputCount.load(std::memory_order_acquire);
}

// ==============================
// Dry/Wet Mix
// ==============================

void EffectChain::setSlotDryWetMix(size_t slotIndex, float mix) {
    if (slotIndex < MAX_SLOTS) {
        m_slots[slotIndex].dryWetMix.store(std::clamp(mix, 0.0f, 1.0f), std::memory_order_release);
        if (!reportRealtimeMisuse("EffectChain::setSlotDryWetMix")) {
            publishSnapshot();
        }
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
    if (maxBlockSize == 0)
        return;
    const size_t required = static_cast<size_t>(maxBlockSize) * 2;
    if (m_dryBuffer.size() < required) {
        m_dryBuffer.resize(required);
    }

    for (auto& slot : m_slots) {
        if (!slot.plugin) {
            continue;
        }
        slot.plugin->initialize(m_sampleRate, m_maxBlockSize);
        if (!slot.plugin->isActive()) {
            slot.plugin->activate();
        }
    }

    // Initialize snapshot for Pass 2
    publishSnapshot();
}

void EffectChain::process(float** buffer, uint32_t numChannels, uint32_t numFrames, const float* const* sidechainInputs,
                          uint32_t numSidechainChannels) {
    // Skip if entire chain is bypassed
    if (m_chainBypassed.load(std::memory_order_acquire)) {
        return;
    }

    // Process each slot in sequence
    for (const auto& slot : m_slots) {
        // Skip empty or bypassed slots
        if (slot.isEmpty() || slot.bypassed.load(std::memory_order_acquire) || isFaultBypassed(slot.faultState)) {
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

        const PluginInfo& pluginInfo = plugin->getInfo();
        const bool isBuiltInComp = (pluginInfo.id == "com.Aestrastudios.comp");
        const uint32_t requestedInputChannels = numChannels + numSidechainChannels;
        const bool canUseSidechain = sidechainInputs && numSidechainChannels > 0 &&
                                     (pluginInfo.numAudioInputs >= requestedInputChannels || isBuiltInComp);
        std::array<const float*, 4> inputChannels{};
        const float* const* processInputs = reinterpret_cast<const float* const*>(buffer);
        uint32_t processInputChannels = numChannels;
        if (canUseSidechain) {
            for (uint32_t ch = 0; ch < numChannels && ch < inputChannels.size(); ++ch) {
                inputChannels[ch] = buffer[ch];
            }
            for (uint32_t ch = 0; ch < numSidechainChannels && (numChannels + ch) < inputChannels.size(); ++ch) {
                inputChannels[numChannels + ch] = sidechainInputs[ch];
            }
            processInputs = inputChannels.data();
            processInputChannels = isBuiltInComp
                                       ? requestedInputChannels
                                       : std::min<uint32_t>(pluginInfo.numAudioInputs, requestedInputChannels);
        }

        // If fully wet, process directly
        if (dryWet >= 0.999f) {
            const bool processed =
                processPluginNoexcept(*plugin, processInputs, buffer, processInputChannels, numChannels, numFrames);
            if (!processed) {
                clearFloatBuffers(buffer, numChannels, numFrames);
            } else if (!buffersAreFinite(buffer, numChannels, numFrames)) {
                markNonFiniteOutputFault(slot.faultState);
                clearFloatBuffers(buffer, numChannels, numFrames);
            }
        }
        // If not fully wet, need to blend
        else if (dryWet > 0.001f) {
            const uint32_t blendChannels = (numChannels < 2) ? numChannels : 2;
            const size_t requiredDry = static_cast<size_t>(numFrames) * static_cast<size_t>(blendChannels);
            if (m_dryBuffer.size() < requiredDry) {
                // Not prepared (or prepared for a smaller block). Stay RT-safe: no allocation.
                const bool processed =
                    processPluginNoexcept(*plugin, processInputs, buffer, processInputChannels, numChannels, numFrames);
                if (!processed) {
                    clearFloatBuffers(buffer, numChannels, numFrames);
                } else if (!buffersAreFinite(buffer, numChannels, numFrames)) {
                    markNonFiniteOutputFault(slot.faultState);
                    clearFloatBuffers(buffer, numChannels, numFrames);
                }
                continue;
            }

            // Save dry signal
            for (uint32_t ch = 0; ch < blendChannels; ++ch) {
                std::memcpy(m_dryBuffer.data() + ch * numFrames, buffer[ch], numFrames * sizeof(float));
            }

            const bool processed =
                processPluginNoexcept(*plugin, processInputs, buffer, processInputChannels, numChannels, numFrames);
            if (!processed) {
                restoreDryChannels(buffer, m_dryBuffer.data(), numChannels, blendChannels, numFrames);
                continue;
            }
            if (!buffersAreFinite(buffer, numChannels, numFrames)) {
                markNonFiniteOutputFault(slot.faultState);
                restoreDryChannels(buffer, m_dryBuffer.data(), numChannels, blendChannels, numFrames);
                continue;
            }

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
// EffectChainSnapshot Processing (Pass 3)
// ==============================

void EffectChainSnapshot::process(float** buffer, uint32_t numChannels, uint32_t numFrames,
                                  const float* const* sidechainInputs, uint32_t numSidechainChannels,
                                  float* dryBuffer) const {
    // Skip if entire chain is bypassed
    if (isChainBypassed) {
        return;
    }

    // Process each slot in sequence
    for (size_t slotIdx = 0; slotIdx < MAX_SLOTS; ++slotIdx) {
        const auto& slot = m_slots[slotIdx];

        // Skip empty or bypassed slots
        if (slot.isEmpty() || slot.bypassed || isFaultBypassed(slot.faultState)) {
            continue;
        }

        auto& plugin = slot.plugin;
        if (!plugin) {
            continue;
        }

        if (!plugin->isActive()) {
            continue;
        }

        float dryWet = slot.dryWetMix;

        const PluginInfo& pluginInfo = plugin->getInfo();
        const bool isBuiltInComp = (pluginInfo.id == "com.Aestrastudios.comp");
        const uint32_t requestedInputChannels = numChannels + numSidechainChannels;
        const bool canUseSidechain = sidechainInputs && numSidechainChannels > 0 &&
                                     (pluginInfo.numAudioInputs >= requestedInputChannels || isBuiltInComp);
        std::array<const float*, 4> inputChannels{};
        const float* const* processInputs = reinterpret_cast<const float* const*>(buffer);
        uint32_t processInputChannels = numChannels;
        if (canUseSidechain) {
            for (uint32_t ch = 0; ch < numChannels && ch < inputChannels.size(); ++ch) {
                inputChannels[ch] = buffer[ch];
            }
            for (uint32_t ch = 0; ch < numSidechainChannels && (numChannels + ch) < inputChannels.size(); ++ch) {
                inputChannels[numChannels + ch] = sidechainInputs[ch];
            }
            processInputs = inputChannels.data();
            processInputChannels = isBuiltInComp
                                       ? requestedInputChannels
                                       : std::min<uint32_t>(pluginInfo.numAudioInputs, requestedInputChannels);
        }

        // If fully wet, process directly
        if (dryWet >= 0.999f) {
            const bool processed =
                processPluginNoexcept(*plugin, processInputs, buffer, processInputChannels, numChannels, numFrames);
            if (!processed) {
                clearFloatBuffers(buffer, numChannels, numFrames);
            } else if (!buffersAreFinite(buffer, numChannels, numFrames)) {
                markNonFiniteOutputFault(slot.faultState);
                clearFloatBuffers(buffer, numChannels, numFrames);
            }
        }
        // If not fully wet, need to blend
        else if (dryWet > 0.001f) {
            const uint32_t blendChannels = (numChannels < 2) ? numChannels : 2;

            // Save dry signal into caller-provided buffer
            for (uint32_t ch = 0; ch < blendChannels; ++ch) {
                std::memcpy(dryBuffer + ch * numFrames, buffer[ch], numFrames * sizeof(float));
            }

            const bool processed =
                processPluginNoexcept(*plugin, processInputs, buffer, processInputChannels, numChannels, numFrames);
            if (!processed) {
                restoreDryChannels(buffer, dryBuffer, numChannels, blendChannels, numFrames);
                continue;
            }
            if (!buffersAreFinite(buffer, numChannels, numFrames)) {
                markNonFiniteOutputFault(slot.faultState);
                restoreDryChannels(buffer, dryBuffer, numChannels, blendChannels, numFrames);
                continue;
            }

            // Blend dry/wet
            float wetGain = dryWet;
            float dryGain = 1.0f - dryWet;

            for (uint32_t ch = 0; ch < blendChannels; ++ch) {
                const float* dry = dryBuffer + ch * numFrames;
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
    state.push_back('C'); // Aestra Effect Chain
    state.push_back(1);   // Version

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
        state.insert(state.end(), reinterpret_cast<const uint8_t*>(&idLen),
                     reinterpret_cast<const uint8_t*>(&idLen) + sizeof(idLen));
        state.insert(state.end(), info.id.begin(), info.id.end());

        // Save bypass state
        state.push_back(slot.bypassed.load() ? 1 : 0);

        // Save dry/wet
        float dryWet = slot.dryWetMix.load();
        state.insert(state.end(), reinterpret_cast<const uint8_t*>(&dryWet),
                     reinterpret_cast<const uint8_t*>(&dryWet) + sizeof(dryWet));

        // Save plugin state
        auto pluginState = slot.plugin->saveState();
        uint32_t stateLen = static_cast<uint32_t>(pluginState.size());
        state.insert(state.end(), reinterpret_cast<const uint8_t*>(&stateLen),
                     reinterpret_cast<const uint8_t*>(&stateLen) + sizeof(stateLen));
        state.insert(state.end(), pluginState.begin(), pluginState.end());
    }

    return state;
}

bool EffectChain::loadState(const std::vector<uint8_t>& state, PluginManager& manager) {
    if (reportRealtimeMisuse("EffectChain::loadState")) {
        return false;
    }
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
            m_slots[i].faultState = std::make_shared<EffectSlotFaultState>();
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

        if (offset + 1 + sizeof(float) > state.size()) {
            return false;
        }

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
            if (!instance->loadState(pluginState)) {
                Aestra::Log::warning("[EffectChain] Failed to load state for plugin slot " + std::to_string(i) +
                                     " — using default state");
            }
            instance->activate();

            m_slots[i].plugin = std::move(instance);
            m_slots[i].bypassed.store(bypassed);
            m_slots[i].dryWetMix.store(dryWet);
            m_slots[i].faultState = std::make_shared<EffectSlotFaultState>();
        }
    }

    publishSnapshot();
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
    if (reportRealtimeMisuse("EffectChain::reset")) {
        return;
    }
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

    publishSnapshot();
}

std::shared_ptr<const EffectChainSnapshot> EffectChain::getSnapshot() const {
    return m_currentSnapshot;
}

std::shared_ptr<const EffectChainSnapshot> EffectChain::createSnapshot() const {
    if (reportRealtimeMisuse("EffectChain::createSnapshot")) {
        return nullptr;
    }
    auto snapshot = std::make_shared<EffectChainSnapshot>(m_slots);
    return snapshot;
}

} // namespace Audio
} // namespace Aestra
