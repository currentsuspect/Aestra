// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioProcessor.h"
#include <cmath>
#include <algorithm>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Nomad {
namespace Audio {

// =============================================================================
// AudioProcessor
// =============================================================================

AudioProcessor::AudioProcessor()
    : m_gain(1.0f)
    , m_pan(0.0f)
    , m_muted(false)
{
}

bool AudioProcessor::sendCommand(const AudioCommandMessage& message) {
    return m_commandQueue.push(message);
}

void AudioProcessor::processCommands() {
    AudioCommandMessage message;
    
    // Process all pending commands
    while (m_commandQueue.pop(message)) {
        handleCommand(message);
    }
}

void AudioProcessor::handleCommand(const AudioCommandMessage& message) {
    switch (message.command) {
        case AudioCommand::SetGain: {
            m_gain.store(message.value1, std::memory_order_release);
            break;
        }

        case AudioCommand::SetPan: {
            float clampedPan = (message.value1 < -1.0f) ? -1.0f : (message.value1 > 1.0f) ? 1.0f : message.value1;
            m_pan.store(clampedPan, std::memory_order_release);
            break;
        }

        case AudioCommand::Mute: {
            m_muted.store(true, std::memory_order_release);
            break;
        }

        case AudioCommand::Unmute: {
            m_muted.store(false, std::memory_order_release);
            break;
        }

        case AudioCommand::Reset: {
            m_gain.store(1.0f, std::memory_order_release);
            m_pan.store(0.0f, std::memory_order_release);
            m_muted.store(false, std::memory_order_release);
            break;
        }

        default:
            break;
    }
}

// =============================================================================
// AudioBufferManager
// =============================================================================

AudioBufferManager::AudioBufferManager()
    : m_maxBufferSize(MAX_BUFFER_SIZE)
    , m_maxChannels(MAX_CHANNELS)
{
    // Allocate buffer
    size_t totalSize = m_maxBufferSize * m_maxChannels;
    m_buffer = new float[totalSize];
    clear();
}

AudioBufferManager::~AudioBufferManager() {
    delete[] m_buffer;
}

float* AudioBufferManager::allocate(uint32_t numFrames, uint32_t numChannels) {
    if (numFrames > m_maxBufferSize || numChannels > m_maxChannels) {
        return nullptr;
    }
    return m_buffer;
}

void AudioBufferManager::clear() {
    size_t totalSize = m_maxBufferSize * m_maxChannels;
    std::memset(m_buffer, 0, totalSize * sizeof(float));
}

// =============================================================================
// TestToneGenerator
// =============================================================================

TestToneGenerator::TestToneGenerator(double sampleRate)
    : AudioProcessor()
    , m_frequency(440.0)
    , m_phase(0.0)
    , m_sampleRate(sampleRate)
{
}

void TestToneGenerator::process(
    float* outputBuffer,
    const float* inputBuffer,
    uint32_t numFrames,
    double streamTime
) {
    (void)inputBuffer;
    (void)streamTime;
    
    // Process any pending commands
    processCommands();
    
    // Get current parameters (atomic reads)
    float gain = m_gain.load(std::memory_order_acquire);
    float pan = m_pan.load(std::memory_order_acquire);
    bool muted = m_muted.load(std::memory_order_acquire);
    double frequency = m_frequency.load(std::memory_order_acquire);
    double phase = m_phase.load(std::memory_order_acquire);
    
    // Calculate pan gains (constant power panning)
    float leftGain = std::cos((pan + 1.0f) * 3.14159265358979323846 * 0.25f);
    float rightGain = std::sin((pan + 1.0f) * 3.14159265358979323846 * 0.25f);
    
    // Generate audio
    for (uint32_t i = 0; i < numFrames; ++i) {
        float sample = 0.0f;
        
        if (!muted) {
            // Generate sine wave
            sample = static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * phase) * gain * 0.3);
            
            // Advance phase
            phase += frequency / m_sampleRate;
            if (phase >= 1.0) {
                phase -= 1.0;
            }
        }
        
        // Apply panning and write to output (stereo)
        outputBuffer[i * 2 + 0] = sample * leftGain;  // Left
        outputBuffer[i * 2 + 1] = sample * rightGain; // Right
    }
    
    // Store phase back (atomic write)
    m_phase.store(phase, std::memory_order_release);
}

void TestToneGenerator::setFrequency(double frequency) {
    // Send command to audio thread
    AudioCommandMessage msg;
    msg.command = AudioCommand::None; // Custom command
    m_frequency.store(frequency, std::memory_order_release);
}

} // namespace Audio
} // namespace Nomad
