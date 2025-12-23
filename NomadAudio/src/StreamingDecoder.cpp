// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "StreamingDecoder.h"
#include "NomadLog.h"
#include "PathUtils.h"
#include "MiniAudioDecoder.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace Nomad {
namespace Audio {

// ============================================================================
// AudioRingBuffer Implementation
// ============================================================================

AudioRingBuffer::AudioRingBuffer(size_t capacityFrames, uint32_t numChannels)
    : m_capacityFrames(capacityFrames)
    , m_numChannels(numChannels) {
    // Allocate interleaved buffer
    m_buffer.resize(capacityFrames * numChannels, 0.0f);
}

size_t AudioRingBuffer::write(const float* samples, size_t numFrames) {
    const size_t writeIdx = m_writeIndex.load(std::memory_order_relaxed);
    const size_t readIdx = m_readIndex.load(std::memory_order_acquire);
    
    // Calculate available write space (leave 1 frame gap to distinguish full from empty)
    size_t available = (readIdx + m_capacityFrames - writeIdx - 1) % m_capacityFrames;
    size_t toWrite = std::min(numFrames, available);
    
    if (toWrite == 0) return 0;
    
    const size_t samplesPerFrame = m_numChannels;
    
    // Write in up to two parts (may wrap around)
    size_t firstPart = std::min(toWrite, m_capacityFrames - writeIdx);
    std::memcpy(&m_buffer[writeIdx * samplesPerFrame], 
                samples, 
                firstPart * samplesPerFrame * sizeof(float));
    
    if (toWrite > firstPart) {
        size_t secondPart = toWrite - firstPart;
        std::memcpy(&m_buffer[0], 
                    samples + firstPart * samplesPerFrame,
                    secondPart * samplesPerFrame * sizeof(float));
    }
    
    // Update write index with release semantics
    m_writeIndex.store((writeIdx + toWrite) % m_capacityFrames, std::memory_order_release);
    
    return toWrite;
}

size_t AudioRingBuffer::read(float* samples, size_t numFrames) {
    const size_t readIdx = m_readIndex.load(std::memory_order_relaxed);
    const size_t writeIdx = m_writeIndex.load(std::memory_order_acquire);
    
    // Calculate available read data
    size_t available = (writeIdx + m_capacityFrames - readIdx) % m_capacityFrames;
    size_t toRead = std::min(numFrames, available);
    
    if (toRead == 0) return 0;
    
    const size_t samplesPerFrame = m_numChannels;
    
    // Read in up to two parts (may wrap around)
    size_t firstPart = std::min(toRead, m_capacityFrames - readIdx);
    std::memcpy(samples, 
                &m_buffer[readIdx * samplesPerFrame],
                firstPart * samplesPerFrame * sizeof(float));
    
    if (toRead > firstPart) {
        size_t secondPart = toRead - firstPart;
        std::memcpy(samples + firstPart * samplesPerFrame,
                    &m_buffer[0],
                    secondPart * samplesPerFrame * sizeof(float));
    }
    
    // Update read index with release semantics
    m_readIndex.store((readIdx + toRead) % m_capacityFrames, std::memory_order_release);
    
    return toRead;
}

size_t AudioRingBuffer::peek(float* samples, size_t numFrames) const {
    const size_t readIdx = m_readIndex.load(std::memory_order_relaxed);
    const size_t writeIdx = m_writeIndex.load(std::memory_order_acquire);
    
    size_t available = (writeIdx + m_capacityFrames - readIdx) % m_capacityFrames;
    size_t toPeek = std::min(numFrames, available);
    
    if (toPeek == 0) return 0;
    
    const size_t samplesPerFrame = m_numChannels;
    
    size_t firstPart = std::min(toPeek, m_capacityFrames - readIdx);
    std::memcpy(samples, 
                &m_buffer[readIdx * samplesPerFrame],
                firstPart * samplesPerFrame * sizeof(float));
    
    if (toPeek > firstPart) {
        size_t secondPart = toPeek - firstPart;
        std::memcpy(samples + firstPart * samplesPerFrame,
                    &m_buffer[0],
                    secondPart * samplesPerFrame * sizeof(float));
    }
    
    return toPeek;
}

size_t AudioRingBuffer::availableRead() const {
    const size_t readIdx = m_readIndex.load(std::memory_order_relaxed);
    const size_t writeIdx = m_writeIndex.load(std::memory_order_acquire);
    return (writeIdx + m_capacityFrames - readIdx) % m_capacityFrames;
}

size_t AudioRingBuffer::availableWrite() const {
    const size_t writeIdx = m_writeIndex.load(std::memory_order_relaxed);
    const size_t readIdx = m_readIndex.load(std::memory_order_acquire);
    return (readIdx + m_capacityFrames - writeIdx - 1) % m_capacityFrames;
}

void AudioRingBuffer::clear() {
    m_writeIndex.store(0, std::memory_order_relaxed);
    m_readIndex.store(0, std::memory_order_release);
}

// ============================================================================
// StreamingDecoder Implementation
// ============================================================================

StreamingDecoder::StreamingDecoder() = default;

StreamingDecoder::~StreamingDecoder() {
    stop();
}

bool StreamingDecoder::start(const std::string& path, double bufferSizeSeconds, double targetLatencyMs) {
    // Stop any existing stream
    stop();
    
    m_state.store(State::Starting, std::memory_order_release);
    m_stopRequested.store(false, std::memory_order_relaxed);
    m_decodedFrames.store(0, std::memory_order_relaxed);
    m_totalFrames.store(0, std::memory_order_relaxed);
    
    // Launch decode thread
    m_decodeThread = std::thread(&StreamingDecoder::decodeThreadFunc, this, path, targetLatencyMs);
    
    return true;
}

void StreamingDecoder::stop() {
    m_stopRequested.store(true, std::memory_order_release);
    
    if (m_decodeThread.joinable()) {
        m_decodeThread.join();
    }
    
    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        m_ringBuffer.reset();
    }
    
    m_state.store(State::Idle, std::memory_order_release);
}

size_t StreamingDecoder::read(float* output, size_t numFrames) {
    if (!isReady()) {
        // Not ready - output silence
        std::memset(output, 0, numFrames * 2 * sizeof(float));  // Assume stereo
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    if (!m_ringBuffer) {
        std::memset(output, 0, numFrames * 2 * sizeof(float));
        return 0;
    }
    
    size_t framesRead = m_ringBuffer->read(output, numFrames);
    
    // Fill remainder with silence if buffer underrun
    if (framesRead < numFrames) {
        size_t channels = m_ringBuffer->channels();
        std::memset(output + framesRead * channels, 0, 
                    (numFrames - framesRead) * channels * sizeof(float));
    }
    
    return framesRead;
}

void StreamingDecoder::decodeThreadFunc(const std::string& path, double targetLatencyMs) {
    // Use existing decoder infrastructure (full decode, then stream from buffer)
    // This is a simpler approach that reuses the existing decode logic
    // For true progressive decode, we'd need a streaming decoder API
    
    std::vector<float> audioData;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    
    // Decode entire file (using existing infrastructure)
    bool success = decodeAudioFile(path, audioData, sampleRate, channels);
    
    if (!success || audioData.empty()) {
        m_state.store(State::Error, std::memory_order_release);
        if (m_onError) {
            m_onError("Failed to decode audio file: " + path);
        }
        return;
    }
    
    // Check for cancellation
    if (m_stopRequested.load(std::memory_order_relaxed)) {
        return;
    }
    
    uint64_t totalFrames = audioData.size() / channels;
    double duration = static_cast<double>(totalFrames) / sampleRate;
    
    m_sampleRate.store(sampleRate, std::memory_order_relaxed);
    m_channels.store(channels, std::memory_order_relaxed);
    m_duration.store(duration, std::memory_order_relaxed);
    m_totalFrames.store(totalFrames, std::memory_order_relaxed);
    
    // Create ring buffer (2 seconds capacity)
    size_t bufferFrames = static_cast<size_t>(sampleRate * 2.0);
    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        m_ringBuffer = std::make_unique<AudioRingBuffer>(bufferFrames, channels);
    }
    
    // Signal ready for playback immediately (we have all the data)
    m_state.store(State::Complete, std::memory_order_release);
    m_decodedFrames.store(totalFrames, std::memory_order_relaxed);
    
    if (m_onReady) {
        m_onReady(sampleRate, channels, duration);
    }
    
    Log::info("StreamingDecoder: Decode complete, " + 
              std::to_string(totalFrames) + " frames (" + 
              std::to_string(duration) + " sec)");
    
    // Fill ring buffer progressively from decoded data
    size_t writePos = 0;
    const size_t chunkSize = kDecodeChunkFrames;
    
    while (writePos < totalFrames && !m_stopRequested.load(std::memory_order_relaxed)) {
        size_t framesToWrite = std::min(chunkSize, totalFrames - writePos);
        
        // Check how much space is available
        size_t availableWrite = 0;
        {
            std::lock_guard<std::mutex> lock(m_bufferMutex);
            if (m_ringBuffer) {
                availableWrite = m_ringBuffer->availableWrite();
            }
        }
        
        if (availableWrite < framesToWrite) {
            // Buffer is full, wait for consumer to read
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        
        // Write to ring buffer
        {
            std::lock_guard<std::mutex> lock(m_bufferMutex);
            if (m_ringBuffer) {
                m_ringBuffer->write(&audioData[writePos * channels], framesToWrite);
            }
        }
        
        writePos += framesToWrite;
    }
    
    if (m_onComplete) {
        m_onComplete();
    }
}

bool StreamingDecoder::openDecoder(const std::string& /*path*/) {
    // Not used in this simplified implementation
    return false;
}

void StreamingDecoder::closeDecoder() {
    // Not used in this simplified implementation
}

size_t StreamingDecoder::decodeChunk(size_t /*targetFrames*/) {
    // Not used in this simplified implementation
    return 0;
}

} // namespace Audio
} // namespace Nomad
