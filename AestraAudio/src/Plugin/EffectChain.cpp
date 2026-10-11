#include "EffectChain.h"

#include "PluginManager.h"
#include "RealtimeThreadGuard.h"
#include "AestraLog.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <unordered_set>
#include <cmath>
#include <cstring>

namespace Aestra {
namespace Audio {

namespace {
// Process-wide plugin-instance id counter (#667). Atomic because chains on
// different channels are mutated from the main/control thread but nothing
// guarantees a single thread across a session; the RT thread never touches it.
std::atomic<uint64_t> g_nextPluginInstanceId{1};
}  // namespace

uint64_t mintPluginInstanceId() {
    uint64_t id = g_nextPluginInstanceId.fetch_add(1, std::memory_order_relaxed);
    // Wrap guard, mirroring the unit-id counter (#528). Exhausting 2^64 ids is not
    // reachable in practice, but returning 0 would silently mean "no instance" and
    // un-address every curve pointing at this slot, so refuse to produce it.
    if (id == 0) {
        g_nextPluginInstanceId.store(1, std::memory_order_relaxed);
        id = 1;
    }
    return id;
}

void reserveMintedPluginInstanceId(uint64_t seenId) {
    if (seenId == 0) {
        return;
    }
    // UINT64_MAX is never reserved, matching the house policy already stated for
    // source ids (SourceManager.h) and pattern ids (PatternManager.h): advancing
    // the counter past it wraps to zero, and the next mint then takes its wrap
    // branch and hands out 1 — an id almost certainly already in use. Refusing
    // here keeps the counter monotonic no matter what a caller passes, so the
    // guarantee does not depend on every call site remembering the boundary.
    if (seenId == std::numeric_limits<uint64_t>::max()) {
        return;
    }
    uint64_t expected = g_nextPluginInstanceId.load(std::memory_order_relaxed);
    while (expected <= seenId) {
        if (g_nextPluginInstanceId.compare_exchange_weak(expected, seenId + 1,
                                                         std::memory_order_relaxed)) {
            return;
        }
        // expected is refreshed by compare_exchange_weak; loop re-tests it.
    }
}

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

// Appends one occupied-slot record in the v2 layout shared by the live,
// missing-placeholder (#647) and crashed-occupant (#931) cases, so the three
// can never drift apart byte-wise.
void appendOccupiedSlotRecord(std::vector<uint8_t>& out, uint64_t instanceId, const std::string& pluginId,
                              bool bypassed, float dryWetMix, const std::vector<uint8_t>& pluginState) {
    out.push_back(1); // Has plugin flag
    out.insert(out.end(), reinterpret_cast<const uint8_t*>(&instanceId),
               reinterpret_cast<const uint8_t*>(&instanceId) + sizeof(instanceId));
    const uint32_t idLen = static_cast<uint32_t>(pluginId.size());
    out.insert(out.end(), reinterpret_cast<const uint8_t*>(&idLen),
               reinterpret_cast<const uint8_t*>(&idLen) + sizeof(idLen));
    out.insert(out.end(), pluginId.begin(), pluginId.end());
    out.push_back(bypassed ? 1 : 0);
    out.insert(out.end(), reinterpret_cast<const uint8_t*>(&dryWetMix),
               reinterpret_cast<const uint8_t*>(&dryWetMix) + sizeof(dryWetMix));
    const uint32_t stateLen = static_cast<uint32_t>(pluginState.size());
    out.insert(out.end(), reinterpret_cast<const uint8_t*>(&stateLen),
               reinterpret_cast<const uint8_t*>(&stateLen) + sizeof(stateLen));
    out.insert(out.end(), pluginState.begin(), pluginState.end());
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

EffectChain::EffectChain() {
    growTo(kInitialSlots);
}
EffectChain::~EffectChain() = default;

bool EffectChain::growTo(size_t count) {
    if (count > kMaxSlots) {
        return false;
    }
    size_t n = m_slotCount.load(std::memory_order_relaxed);
    while (n < count) {
        // A slot allocated by an earlier, larger chain is reused (it was emptied
        // when the chain shrank); otherwise allocate it. Either way the address
        // is stable from here on. Publish the larger count only after the slot
        // exists, so no reader can see an index without a slot behind it.
        if (!m_slots[n]) {
            m_slots[n] = std::make_unique<EffectSlot>();
        }
        ++n;
        m_slotCount.store(n, std::memory_order_release);
    }
    return true;
}

// ==============================
// Snapshot Publication (Pass 2)
// ==============================

void EffectChain::publishSnapshot() {
    auto snapshot = std::make_shared<EffectChainSnapshot>(*this);
    snapshot->isChainBypassed = m_chainBypassed.load(std::memory_order_acquire);
    m_currentSnapshot = snapshot;
}

// ==============================
// Slot Management
// ==============================

bool EffectChain::insertPlugin(size_t slotIndex, PluginInstancePtr plugin, uint64_t preservedInstanceId) {
    if (reportRealtimeMisuse("EffectChain::insertPlugin")) {
        return false;
    }
    // Growth is the only way past the initial slots: an index just beyond the end
    // extends the chain to hold it (V8-S3), up to kMaxSlots.
    if (slotIndex >= kMaxSlots || !growTo(slotIndex + 1)) {
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

    // Whether the CALLER handed us a plugin — checked before the move, which
    // leaves `plugin` null either way.
    const bool receivedPlugin = plugin != nullptr;
    at(slotIndex).plugin = std::move(plugin);
    // The slot now genuinely holds what the caller put here, so any retained
    // missing-plugin record for it is superseded (#647).
    at(slotIndex).clearMissingPlugin();
    // A different plugin instance occupies this slot, so it is a different
    // identity — mint a fresh one rather than inheriting whatever was here
    // (#667). Inheriting would silently hand the previous plugin's automation to
    // its replacement, which is the same class of defect as the positional
    // addressing this id exists to remove.
    //
    // The exception is a caller re-seating an occupant that already HAD an
    // identity and, to the user, never stopped being the same plugin. Undo of a
    // removal is that case: minting there would mean Ctrl+Z silently orphaned
    // every curve pointing at the plugin it just brought back. Reserve the id
    // first so one restored from outside this process can never be minted again.
    //
    // A preserved id is honoured only if it can actually be held uniquely. Two
    // cases fall back to a fresh mint, matching how SourceManager treats a
    // requested source id: UINT64_MAX, which cannot be reserved without wrapping
    // the counter to zero, and an id already live in this chain, which would put
    // the same identity in two slots. Falling back is right rather than failing
    // the insert — the plugin still belongs in the slot, it just cannot keep a
    // name that is unusable or taken.
    if (receivedPlugin && preservedInstanceId != 0
        && preservedInstanceId != std::numeric_limits<uint64_t>::max()
        && findSlotByInstanceId(preservedInstanceId) == kNoSlot) {
        reserveMintedPluginInstanceId(preservedInstanceId);
        at(slotIndex).instanceId = preservedInstanceId;
    } else {
        at(slotIndex).instanceId = receivedPlugin ? mintPluginInstanceId() : 0;
    }
    at(slotIndex).bypassed.store(false);
    at(slotIndex).dryWetMix.store(1.0f);
    at(slotIndex).faultState = std::make_shared<EffectSlotFaultState>();

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
    if (slotIndex >= slotCount()) {
        return nullptr;
    }

    auto plugin = std::move(at(slotIndex).plugin);
    at(slotIndex).plugin = nullptr;
    // Removing a slot removes it, placeholder included (#647).
    at(slotIndex).clearMissingPlugin();
    // The instance is gone, so its identity goes with it (#667). Anything still
    // addressing this id now resolves to nothing, which is the honest outcome —
    // far better than resolving to whatever occupies the index next.
    at(slotIndex).instanceId = 0;
    at(slotIndex).faultState = std::make_shared<EffectSlotFaultState>();

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
    if (fromSlot >= slotCount() || toSlot >= kMaxSlots || !growTo(toSlot + 1)) {
        return false;
    }

    if (fromSlot == toSlot) {
        return true;
    }

    // Can only move to a slot nothing else claims. A placeholder claims its slot
    // (#647) — moving onto it would silently destroy a retained record.
    if (at(toSlot).isOccupied()) {
        return false;
    }

    at(toSlot).plugin = std::move(at(fromSlot).plugin);
    // The identity travels with the instance (#667). This is the whole point of
    // the id: a move changes which index holds the plugin and nothing else, so
    // automation addressed to it keeps resolving to the same plugin.
    at(toSlot).instanceId = at(fromSlot).instanceId;
    at(toSlot).missingPluginId = std::move(at(fromSlot).missingPluginId);
    at(toSlot).missingPluginState = std::move(at(fromSlot).missingPluginState);
    at(toSlot).bypassed.store(at(fromSlot).bypassed.load());
    at(toSlot).dryWetMix.store(at(fromSlot).dryWetMix.load());
    at(toSlot).faultState = std::move(at(fromSlot).faultState);
    if (!at(toSlot).faultState) {
        at(toSlot).faultState = std::make_shared<EffectSlotFaultState>();
    }

    at(fromSlot).plugin = nullptr;
    at(fromSlot).clearMissingPlugin();
    at(fromSlot).instanceId = 0;  // vacated — no instance lives here now (#667)
    at(fromSlot).bypassed.store(false);
    at(fromSlot).dryWetMix.store(1.0f);
    at(fromSlot).faultState = std::make_shared<EffectSlotFaultState>();

    publishSnapshot();
    return true;
}

bool EffectChain::swapPlugins(size_t slot1, size_t slot2) {
    if (reportRealtimeMisuse("EffectChain::swapPlugins")) {
        return false;
    }
    if (slot1 >= slotCount() || slot2 >= slotCount()) {
        return false;
    }

    if (slot1 == slot2) {
        return true;
    }

    std::swap(at(slot1).plugin, at(slot2).plugin);
    // Identities swap with their instances (#667) — see movePlugin.
    std::swap(at(slot1).instanceId, at(slot2).instanceId);
    std::swap(at(slot1).missingPluginId, at(slot2).missingPluginId);
    std::swap(at(slot1).missingPluginState, at(slot2).missingPluginState);
    std::swap(at(slot1).faultState, at(slot2).faultState);

    bool bypass1 = at(slot1).bypassed.load();
    float mix1 = at(slot1).dryWetMix.load();

    at(slot1).bypassed.store(at(slot2).bypassed.load());
    at(slot1).dryWetMix.store(at(slot2).dryWetMix.load());

    at(slot2).bypassed.store(bypass1);
    at(slot2).dryWetMix.store(mix1);

    publishSnapshot();
    return true;
}

PluginInstancePtr EffectChain::getPlugin(size_t slotIndex) const {
    if (slotIndex >= slotCount()) {
        return nullptr;
    }
    return at(slotIndex).plugin;
}

const EffectSlot* EffectChain::getSlot(size_t slotIndex) const {
    if (slotIndex >= slotCount()) {
        return nullptr;
    }
    return &at(slotIndex);
}

bool EffectChain::isSlotEmpty(size_t slotIndex) const {
    if (slotIndex >= slotCount()) {
        return true;
    }
    // "Free to use", not the RT predicate: a retained missing-plugin record
    // claims its slot (#647).
    return !at(slotIndex).isOccupied();
}

uint64_t EffectChain::getSlotInstanceId(size_t slotIndex) const {
    if (slotIndex >= slotCount()) {
        return 0;
    }
    return at(slotIndex).instanceId;
}

size_t EffectChain::findSlotByInstanceId(uint64_t instanceId) const {
    // Id 0 means "no instance". Unoccupied slots carry 0, so matching on it would
    // resolve every un-addressed curve to the first empty slot (#667).
    if (instanceId == 0) {
        return kNoSlot;
    }
    const size_t count = slotCount();
    for (size_t i = 0; i < count; ++i) {
        if (at(i).instanceId == instanceId) {
            return i;
        }
    }
    return kNoSlot;
}

size_t EffectChain::getFirstEmptySlot() const {
    const size_t count = slotCount();
    for (size_t i = 0; i < count; ++i) {
        if (!at(i).isOccupied()) {
            return i;
        }
    }
    // Every current slot is taken: the next index is where insertPlugin grows to.
    return count < kMaxSlots ? count : kNoSlot;
}

size_t EffectChain::getMissingPluginCount() const {
    size_t count = 0;
    for (size_t i_ = 0, n_ = slotCount(); i_ < n_; ++i_) {
        const auto& slot = at(i_);
        if (slot.hasMissingPlugin()) {
            ++count;
        }
    }
    return count;
}

std::string EffectChain::getMissingPluginId(size_t slotIndex) const {
    if (slotIndex >= slotCount()) {
        return {};
    }
    return at(slotIndex).missingPluginId;
}

size_t EffectChain::getActiveSlotCount() const {
    size_t count = 0;
    for (size_t i_ = 0, n_ = slotCount(); i_ < n_; ++i_) {
        const auto& slot = at(i_);
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
    for (size_t i_ = 0, n_ = slotCount(); i_ < n_; ++i_) {
        auto& slot = at(i_);
        slot.plugin = nullptr;
        slot.clearMissingPlugin();
        slot.instanceId = 0;  // every instance is gone, so every identity is (#667)
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
    if (slotIndex < slotCount()) {
        if (!bypassed) {
            clearFaultState(at(slotIndex).faultState);
        }
        at(slotIndex).bypassed.store(bypassed, std::memory_order_release);
        if (!reportRealtimeMisuse("EffectChain::setSlotBypassed")) {
            publishSnapshot();
            if (m_onLatencyChanged) {
                m_onLatencyChanged();
            }
        }
    }
}

bool EffectChain::isSlotBypassed(size_t slotIndex) const {
    if (slotIndex >= slotCount()) {
        return true;
    }
    return at(slotIndex).bypassed.load(std::memory_order_acquire) ||
           isFaultBypassed(at(slotIndex).faultState);
}

bool EffectChain::isSlotBypassedByNonFiniteOutput(size_t slotIndex) const {
    if (slotIndex >= slotCount()) {
        return false;
    }
    return isFaultBypassed(at(slotIndex).faultState);
}

void EffectChain::setChainBypassed(bool bypassed) {
    m_chainBypassed.store(bypassed, std::memory_order_release);
    if (!reportRealtimeMisuse("EffectChain::setChainBypassed")) {
        publishSnapshot();
    }
}

uint64_t EffectChain::getSlotNonFiniteOutputCount(size_t slotIndex) const {
    if (slotIndex >= slotCount() || !at(slotIndex).faultState) {
        return 0;
    }
    return at(slotIndex).faultState->nonFiniteOutputCount.load(std::memory_order_acquire);
}

// ==============================
// Dry/Wet Mix
// ==============================

void EffectChain::setSlotDryWetMix(size_t slotIndex, float mix) {
    if (slotIndex < slotCount()) {
        at(slotIndex).dryWetMix.store(std::clamp(mix, 0.0f, 1.0f), std::memory_order_release);
        if (!reportRealtimeMisuse("EffectChain::setSlotDryWetMix")) {
            publishSnapshot();
        }
    }
}

float EffectChain::getSlotDryWetMix(size_t slotIndex) const {
    if (slotIndex >= slotCount()) {
        return 1.0f;
    }
    return at(slotIndex).dryWetMix.load(std::memory_order_acquire);
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

    for (size_t i_ = 0, n_ = slotCount(); i_ < n_; ++i_) {
        auto& slot = at(i_);
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
    for (size_t i_ = 0, n_ = slotCount(); i_ < n_; ++i_) {
        const auto& slot = at(i_);
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
    for (size_t slotIdx = 0; slotIdx < m_slots.size(); ++slotIdx) {
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
    state.push_back('C');                  // Aestra Effect Chain magic
    // How many slots to write. At least the initial ten, so a project that never
    // grew its chain is saved exactly as before (v2, count 10). Past that, only
    // up to the last occupied slot: trailing empty slots are an artefact of the
    // chain having once grown, not something worth keeping in the file. A chain
    // that does reach past ten is written as v3 so an older build refuses it
    // cleanly instead of reading a count it was never taught.
    size_t writeCount = kInitialSlots;
    for (size_t i = slotCount(); i > kInitialSlots; --i) {
        if (at(i - 1).isOccupied()) {
            writeCount = i;
            break;
        }
    }
    state.push_back(writeCount > kInitialSlots ? kStateFormatVersionLong : kStateFormatVersion);
    state.push_back(static_cast<uint8_t>(writeCount));

    // Write each slot
    for (size_t i = 0; i < writeCount; ++i) {
        const auto& slot = at(i);

        // A slot holding a placeholder for a plugin that would not load is NOT
        // empty (#647). Re-emit the retained record verbatim so a load/save on a
        // machine missing the plugin preserves it rather than deleting it. The
        // layout is identical to the live-plugin case below, so the file format
        // is unchanged and a machine that has the plugin loads it normally.
        if (slot.hasMissingPlugin()) {
            // v2: the placeholder's identity travels with it (Contract I6).
            const bool bypassed = slot.bypassed.load();
            const float dryWet = slot.dryWetMix.load();
            appendOccupiedSlotRecord(state, slot.instanceId, slot.missingPluginId, bypassed, dryWet,
                                     slot.missingPluginState);
            continue;
        }

        if (slot.isEmpty()) {
            state.push_back(0); // Empty flag
            continue;
        }

        // A crashed occupant still holds its slot, but its saveState() cannot be
        // trusted: a dead helper answers with an empty blob, which would cement
        // the crash into the project on the next save (#931). Emit the #647
        // record layout with the last good blob instead, so a reload restores
        // the plugin (when available) with its pre-crash state.
        if (slot.plugin->isCrashed()) {
            const auto& crashedInfo = slot.plugin->getInfo();
            static const std::vector<uint8_t> kEmptyFallback;
            const std::vector<uint8_t>& preserved =
                (slot.lastGoodInstanceId == slot.instanceId && !slot.lastGoodPluginState.empty())
                    ? slot.lastGoodPluginState
                    : kEmptyFallback;
            const bool bypassed = slot.bypassed.load();
            const float dryWet = slot.dryWetMix.load();
            appendOccupiedSlotRecord(state, slot.instanceId, crashedInfo.id, bypassed, dryWet, preserved);
            continue;
        }

        // v2: the instance identity travels with the instance (Contract I4).
        // A successful non-empty capture refreshes the last-good cache (#931),
        // keyed by instance id so a replaced occupant can never inherit it.
        const auto& info = slot.plugin->getInfo();
        auto pluginState = slot.plugin->saveState();
        if (!pluginState.empty()) {
            slot.lastGoodPluginState = pluginState;
            slot.lastGoodInstanceId = slot.instanceId;
        }
        const bool bypassed = slot.bypassed.load();
        const float dryWet = slot.dryWetMix.load();
        appendOccupiedSlotRecord(state, slot.instanceId, info.id, bypassed, dryWet, pluginState);
    }

    return state;
}

bool EffectChain::loadState(const std::vector<uint8_t>& state, PluginManager& manager, LoadReport* outReport) {
    if (reportRealtimeMisuse("EffectChain::loadState")) {
        return false;
    }
    if (state.size() < 5) {
        return false;
    }

    // Check magic separately from version so a version mismatch is diagnosable
    // and future formats have a migration point (rather than being rejected as
    // if the data were not an effect chain at all).
    if (state[0] != 'N' || state[1] != 'E' || state[2] != 'C') {
        return false;
    }

    const uint8_t version = state[3];
    if (version == 0 || version > kStateFormatVersionLong) {
        // Unknown/future format: refuse rather than misparse. When a v2 layout is
        // added, dispatch here (e.g. `if (version >= 2) return loadStateV2(...)`)
        // while keeping the v1 path below so older chains still load.
        Aestra::Log::warning("[EffectChain] Unsupported effect-chain state version " + std::to_string(version) +
                             " (this build supports up to " + std::to_string(kStateFormatVersionLong) +
                             "); skipping restore.");
        return false;
    }

    // v1/v2 always carry exactly the initial ten slots. v3 (V8-S3) carries the
    // grown count, anywhere from the initial ten up to the ceiling.
    const uint8_t savedSlots = state[4];
    const bool countValid = version < kStateFormatVersionLong
                                ? savedSlots == kInitialSlots
                                : (savedSlots >= kInitialSlots && savedSlots <= kMaxSlots);
    if (!countValid) {
        return false;
    }

    // Size the chain to the payload: grow to hold it, and empty (then drop from
    // the count) anything past it, so a load always yields exactly what was saved.
    // Slot objects stay allocated, so any address a reader already holds stays
    // valid; growTo reuses them.
    growTo(savedSlots);
    for (size_t extra = savedSlots; extra < slotCount(); ++extra) {
        auto& dead = at(extra);
        dead.plugin = nullptr;
        dead.clearMissingPlugin();
        dead.instanceId = 0;
        dead.bypassed.store(false);
        dead.dryWetMix.store(1.0f);
        dead.faultState = std::make_shared<EffectSlotFaultState>();
    }
    m_slotCount.store(savedSlots, std::memory_order_release);

    size_t offset = 5;

    std::unordered_set<uint64_t> seenLoadedIds;
    const auto resolveSlotIdentity = [&](uint64_t wireId, size_t slotIndex) -> uint64_t {
        if (version >= 2 && wireId != 0) {
            // UINT64_MAX is the sentinel reserveMintedPluginInstanceId refuses
            // by design; a payload carrying it is corrupt, same as a duplicate.
            if (wireId == std::numeric_limits<uint64_t>::max()) {
                Aestra::Log::warning("[EffectChain] reserved instance id " + std::to_string(wireId) +
                                     " in v2 state on slot " + std::to_string(slotIndex) +
                                     "; minting a fresh identity");
                const uint64_t fresh = mintPluginInstanceId();
                seenLoadedIds.insert(fresh);
                return fresh;
            }
            if (!seenLoadedIds.insert(wireId).second) {
                // Duplicate id in a v2 payload is corrupt: mint a fresh identity
                // so the two slots never share one (Contract I5).
                Aestra::Log::warning("[EffectChain] duplicate instance id " + std::to_string(wireId) +
                                     " in v2 state on slot " + std::to_string(slotIndex) +
                                     "; minting a fresh identity");
                const uint64_t fresh = mintPluginInstanceId();
                seenLoadedIds.insert(fresh);
                return fresh;
            }
            reserveMintedPluginInstanceId(wireId);
            return wireId;
        }
        if (version >= 2) {
            Aestra::Log::warning("[EffectChain] v2 state carries no instance id on slot " +
                                 std::to_string(slotIndex) + "; minting (corrupt payload)");
        }
        const uint64_t fresh = mintPluginInstanceId();
        seenLoadedIds.insert(fresh);
        return fresh;
    };

    for (size_t i = 0; i < savedSlots && offset < state.size(); ++i) {
        uint8_t hasPlugin = state[offset++];

        uint64_t wireInstanceId = 0;
        if (hasPlugin && version >= 2) {
            if (offset + sizeof(uint64_t) > state.size()) {
                return false;
            }
            std::memcpy(&wireInstanceId, &state[offset], sizeof(wireInstanceId));
            offset += sizeof(wireInstanceId);
        }

        if (!hasPlugin) {
            at(i).plugin = nullptr;
            at(i).clearMissingPlugin();
            // Loading into a chain that was already populated must not leave the
            // previous occupant's identity behind on a now-empty slot, or a
            // lookup for that id would resolve to a slot holding nothing (#667).
            at(i).instanceId = 0;
            at(i).faultState = std::make_shared<EffectSlotFaultState>();
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

        // Read dry/wet. Guard against a corrupted/NaN blob: a non-finite mix
        // would multiply into the audio path. Fall back to fully-wet (the
        // default) and clamp to the valid [0,1] range the setter enforces.
        float dryWet;
        std::memcpy(&dryWet, &state[offset], sizeof(dryWet));
        offset += sizeof(dryWet);
        if (!std::isfinite(dryWet)) {
            dryWet = 1.0f;
        } else {
            dryWet = std::clamp(dryWet, 0.0f, 1.0f);
        }

        // Read plugin state length
        uint32_t stateLen;
        std::memcpy(&stateLen, &state[offset], sizeof(stateLen));
        offset += sizeof(stateLen);

        if (offset + stateLen > state.size()) {
            return false;
        }

        std::vector<uint8_t> pluginState(state.begin() + offset, state.begin() + offset + stateLen);
        offset += stateLen;

        // Create plugin instance.
        //
        // Two ways for a slot to end up with nothing live to run: the plugin
        // cannot be created at all, or it is created and then REJECTS its own
        // saved state. Both leave the user's settings unapplied, so both take
        // the placeholder path below rather than a live instance on defaults.
        //
        // That distinction is the whole point of #1014. The previous behaviour
        // kept the instance live after a rejection, which meant the slot ran
        // with default parameters and the next save wrote those defaults over
        // the project's settings -- the data loss was not just unreported, it
        // was guaranteed on the first save. The placeholder path stores the
        // opaque state exactly as it came off the wire and re-emits it on save,
        // so the settings survive and the slot resolves when the project is
        // loaded again.
        auto instance = manager.createInstanceById(pluginId);
        bool stateRejected = false;
        if (instance) {
            instance->initialize(m_sampleRate, m_maxBlockSize);
            if (!instance->loadState(pluginState)) {
                // Installed, but the blob is not something it will accept. The
                // diagnosis is more specific than "missing" and is reported as
                // such; the recovery is the same.
                stateRejected = true;
                instance.reset();
            }
        }

        if (instance) {
            instance->activate();

            at(i).plugin = std::move(instance);
            at(i).clearMissingPlugin();
        } else {
            // The plugin is unavailable, or its state was unreadable. Retain
            // the record instead of dropping it (#647) — dropping it here is
            // what made the next save erase the slot permanently and silently.
            //
            // The opaque state is stored exactly as it came off the wire; it is
            // never interpreted or normalised, so repeated load/save cycles
            // cannot progressively mutate it. bypass and dry/wet are stored
            // post-sanitisation, which is what a live plugin round-trips to as
            // well, so both paths converge after one cycle and stay fixed.
            at(i).plugin = nullptr;
            at(i).missingPluginId = pluginId;
            at(i).missingPluginState = std::move(pluginState);

            if (stateRejected) {
                if (outReport) {
                    outReport->unreadableState.push_back(pluginId);
                }
                Aestra::Log::warning("[EffectChain] Plugin " + pluginId + " at slot " + std::to_string(i) +
                                     " rejected its saved state — the slot is left empty rather than run on"
                                     " defaults, and its state is preserved on save");
            } else {
                if (outReport) {
                    outReport->missingPlugins.push_back(pluginId);
                }
                Aestra::Log::warning("[EffectChain] Plugin unavailable for slot " + std::to_string(i) +
                                     ": " + pluginId +
                                     " — slot state retained and will be preserved on save");
            }
        }
        // A slot is an occupant either way, so identity and mix are resolved
        // once, after the branch: v2 restores the persisted identity (reserving
        // it against future mints); v1 predates identity and mints. A v2 payload
        // with a missing/duplicate id mints with a diagnostic (Contract I5).
        // Automation addressed to a plugin that failed to load has to survive
        // the round trip exactly as the placeholder does (Contract I6).
        at(i).instanceId = resolveSlotIdentity(wireInstanceId, i);
        at(i).bypassed.store(bypassed);
        at(i).dryWetMix.store(dryWet);
        at(i).faultState = std::make_shared<EffectSlotFaultState>();
    }

    publishSnapshot();
    return true;
}

// ==============================
// Latency
// ==============================

uint32_t EffectChain::getTotalLatency() const {
    uint32_t total = 0;

    for (size_t i_ = 0, n_ = slotCount(); i_ < n_; ++i_) {
        const auto& slot = at(i_);
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
    for (size_t i_ = 0, n_ = slotCount(); i_ < n_; ++i_) {
        auto& slot = at(i_);
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
    auto snapshot = std::make_shared<EffectChainSnapshot>(*this);
    return snapshot;
}

} // namespace Audio
} // namespace Aestra
