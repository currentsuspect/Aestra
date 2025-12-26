// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "MixerBus.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace Nomad {
namespace Audio {

// Constants
static constexpr float PI = 3.14159265358979323846f;
static constexpr float SQRT2_OVER_2 = 0.70710678118654752440f; // sqrt(2)/2

MixerBus::MixerBus(const char* name, uint32_t numChannels)
    : m_name(name)
    , m_numChannels(numChannels)
    , m_gain(1.0f)
    , m_pan(0.0f)
    , m_muted(false)
    , m_soloed(false)
    , m_currentGain(1.0f)
    , m_currentPan(0.0f)
{
}

void MixerBus::process(float* buffer, uint32_t numFrames)
{
    // Load targets (atomic)
    const float targetGain = m_gain.load(std::memory_order_acquire);
    const float targetPan = m_pan.load(std::memory_order_acquire);
    const bool muted = m_muted.load(std::memory_order_acquire);

    // If muted, clear buffer and reset smoothing state to prevent jumps on unmute
    if (muted) {
        clear(buffer, numFrames);
        m_currentGain = targetGain;
        m_currentPan = targetPan;
        return;
    }

    // Handle mono vs stereo
    if (m_numChannels == 1) {
        // Mono: just apply gain with smoothing
        const float gainStart = m_currentGain;
        const float gainEnd = targetGain;
        const float gainDelta = (gainEnd - gainStart) / static_cast<float>(numFrames);
        
        float current = gainStart;
        for (uint32_t i = 0; i < numFrames; ++i) {
            buffer[i] *= current;
            current += gainDelta;
        }
    }
    else if (m_numChannels == 2) {
        // Stereo: apply gain and pan with smoothing
        const float panStart = m_currentPan;
        const float panEnd = targetPan;
        const float gainStart = m_currentGain;
        const float gainEnd = targetGain;
        
        const float panDelta = (panEnd - panStart) / static_cast<float>(numFrames);
        const float gainDelta = (gainEnd - gainStart) / static_cast<float>(numFrames);

        float currentPan = panStart;
        float currentGain = gainStart;

        for (uint32_t i = 0; i < numFrames; ++i) {
            float leftGain, rightGain;
            calculatePanGains(currentPan, leftGain, rightGain);
            
            leftGain *= currentGain;
            rightGain *= currentGain;

            const uint32_t leftIdx = i * 2;
            const uint32_t rightIdx = i * 2 + 1;
            
            buffer[leftIdx] *= leftGain;
            buffer[rightIdx] *= rightGain;
            
            currentPan += panDelta;
            currentGain += gainDelta;
        }
    }
    
    // Snap to target at end of block
    m_currentGain = targetGain;
    m_currentPan = targetPan;
}

void MixerBus::mixInto(float* output, const float* input, uint32_t numFrames)
{
    // Load targets (atomic)
    const float targetGain = m_gain.load(std::memory_order_acquire);
    const float targetPan = m_pan.load(std::memory_order_acquire);
    const bool muted = m_muted.load(std::memory_order_acquire);

    // If muted, don't mix anything
    if (muted) {
        m_currentGain = targetGain;
        m_currentPan = targetPan;
        return;
    }

    // Handle mono vs stereo
    if (m_numChannels == 1) {
        const float gainStart = m_currentGain;
        const float gainEnd = targetGain;
        const float gainDelta = (gainEnd - gainStart) / static_cast<float>(numFrames);
        
        float current = gainStart;
        for (uint32_t i = 0; i < numFrames; ++i) {
            output[i] += input[i] * current;
            current += gainDelta;
        }
    }
    else if (m_numChannels == 2) {
        // Stereo: mix with gain and pan smoothing
        const float panStart = m_currentPan;
        const float panEnd = targetPan;
        const float gainStart = m_currentGain;
        const float gainEnd = targetGain;
        
        const float panDelta = (panEnd - panStart) / static_cast<float>(numFrames);
        const float gainDelta = (gainEnd - gainStart) / static_cast<float>(numFrames);

        float currentPan = panStart;
        float currentGain = gainStart;

        for (uint32_t i = 0; i < numFrames; ++i) {
            float leftGain, rightGain;
            calculatePanGains(currentPan, leftGain, rightGain);
            
            leftGain *= currentGain;
            rightGain *= currentGain;

            const uint32_t leftIdx = i * 2;
            const uint32_t rightIdx = i * 2 + 1;
            
            output[leftIdx] += input[leftIdx] * leftGain;
            output[rightIdx] += input[rightIdx] * rightGain;
            
            currentPan += panDelta;
            currentGain += gainDelta;
        }
    }
    
    // Snap to target
    m_currentGain = targetGain;
    m_currentPan = targetPan;
}

void MixerBus::clear(float* buffer, uint32_t numFrames)
{
    const size_t numSamples = numFrames * m_numChannels;
    std::memset(buffer, 0, numSamples * sizeof(float));
}

void MixerBus::setGain(float gain)
{
    // Clamp gain to reasonable range [0.0, 2.0]
    gain = std::max(0.0f, std::min(2.0f, gain));
    m_gain.store(gain, std::memory_order_release);
}

void MixerBus::setPan(float pan)
{
    // Clamp pan to [-1.0, 1.0]
    pan = std::max(-1.0f, std::min(1.0f, pan));
    m_pan.store(pan, std::memory_order_release);
}

void MixerBus::setMute(bool mute)
{
    m_muted.store(mute, std::memory_order_release);
}

void MixerBus::setSolo(bool solo)
{
    m_soloed.store(solo, std::memory_order_release);
}

void MixerBus::calculatePanGains(float pan, float& leftGain, float& rightGain) const
{
    // Constant power panning law
    // pan: -1.0 (left) to 1.0 (right)
    // Uses sin/cos for constant power
    
    const float angle = (pan + 1.0f) * 0.25f * PI; // Map [-1, 1] to [0, PI/2]
    
    leftGain = std::cos(angle);
    rightGain = std::sin(angle);
}

// SimpleMixer implementation

SimpleMixer::SimpleMixer()
{
}

size_t SimpleMixer::addBus(const char* name, uint32_t numChannels)
{
    m_buses.push_back(std::make_unique<MixerBus>(name, numChannels));
    return m_buses.size() - 1;
}

MixerBus* SimpleMixer::getBus(size_t index)
{
    if (index >= m_buses.size()) {
        return nullptr;
    }
    return m_buses[index].get();
}

void SimpleMixer::process(float* masterOutput, const float** inputs, uint32_t numFrames)
{
    // Clear master output
    std::memset(masterOutput, 0, numFrames * 2 * sizeof(float)); // Assume stereo master

    // Check if any bus is soloed
    bool anySoloed = false;
    for (const auto& bus : m_buses) {
        if (bus->isSoloed()) {
            anySoloed = true;
            break;
        }
    }

    // Mix each bus into master
    for (size_t i = 0; i < m_buses.size(); ++i) {
        auto& bus = m_buses[i];
        
        // Skip if muted
        if (bus->isMuted()) {
            continue;
        }

        // If any bus is soloed, only process soloed buses
        if (anySoloed && !bus->isSoloed()) {
            continue;
        }

        // Mix this bus into master
        if (inputs[i] != nullptr) {
            bus->mixInto(masterOutput, inputs[i], numFrames);
        }
    }
}

void SimpleMixer::reset()
{
    m_buses.clear();
}

} // namespace Audio
} // namespace Nomad
