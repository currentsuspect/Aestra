// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "StreamingDecoder.h"
#include "NomadLog.h"
#include "PathUtils.h"
#include "MiniAudioDecoder.h"

#if defined(NOMAD_USE_MINIAUDIO)
#include "miniaudio.h"
#endif

#include <algorithm>
#include <cstring>
#include <filesystem>

// Cache prefetch support
#if defined(_MSC_VER) || defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #include <xmmintrin.h>  // _mm_prefetch
    #define NOMAD_HAS_PREFETCH 1
    #define NOMAD_PREFETCH(addr) _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_T0)
#elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
    #define NOMAD_HAS_PREFETCH 1
    #define NOMAD_PREFETCH(addr) __builtin_prefetch(addr, 0, 3)
#else
    #define NOMAD_PREFETCH(addr) ((void)0)
#endif

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
    
    // Prefetch: Prime the cache before memcpy (reduces latency on large buffers)
    const size_t prefetchStride = 64; // Cache line size
    const float* srcPtr = &m_buffer[readIdx * samplesPerFrame];
    for (size_t offset = 0; offset < firstPart * samplesPerFrame * sizeof(float); offset += prefetchStride) {
        NOMAD_PREFETCH(reinterpret_cast<const char*>(srcPtr) + offset);
    }
    
    std::memcpy(samples, 
                srcPtr,
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
    
    // Check file existence
    if (!std::filesystem::exists(makeUnicodePath(path))) {
        if (m_onError) m_onError("File not found: " + path);
        return false;
    }

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
    
    // Delete any pending buffer from previous stop
    if (m_pendingDelete) {
        delete m_pendingDelete;
        m_pendingDelete = nullptr;
    }
    
    // Atomically swap out the current buffer and delete it
    // Safe because thread is joined and no RT reads can happen in Idle state
    AudioRingBuffer* old = m_ringBuffer.exchange(nullptr, std::memory_order_acq_rel);
    delete old;
    
    m_state.store(State::Idle, std::memory_order_release);
}

size_t StreamingDecoder::read(float* output, size_t numFrames) {
    if (!isReady()) {
        // Not ready - output silence
        std::memset(output, 0, numFrames * 2 * sizeof(float));  // Assume stereo safety
        return 0;
    }
    
    // Lock-free: atomically load the ring buffer pointer
    AudioRingBuffer* rb = m_ringBuffer.load(std::memory_order_acquire);
    if (!rb) {
        uint32_t channels = m_ringBufferChannels.load(std::memory_order_relaxed);
        if (channels == 0) channels = 2;
        std::memset(output, 0, numFrames * channels * sizeof(float));
        return 0;
    }
    
    size_t framesRead = rb->read(output, numFrames);
    
    // Fill remainder with silence if buffer underrun
    if (framesRead < numFrames) {
        size_t channels = rb->channels();
        // Zero out remaining frames * channels
        std::memset(output + framesRead * channels, 0, 
                    (numFrames - framesRead) * channels * sizeof(float));
    }
    
    return framesRead;
}

void StreamingDecoder::decodeThreadFunc(const std::string& path, double targetLatencyMs) {
#if defined(NOMAD_USE_MINIAUDIO)
    if (!openDecoder(path)) {
        m_state.store(State::Error, std::memory_order_release);
        if (m_onError) {
            m_onError("Failed to open decoder for: " + path);
        }
        return;
    }
    
    auto* decoder = static_cast<ma_decoder*>(m_decoderHandle);
    
    // Get format info
    uint32_t sampleRate = decoder->outputSampleRate;
    uint32_t channels = decoder->outputChannels;
    ma_uint64 lengthFrames = 0;
    if (ma_decoder_get_length_in_pcm_frames(decoder, &lengthFrames) != MA_SUCCESS) {
        lengthFrames = 0; // Unknown length
    }
    
    double duration = (sampleRate > 0 && lengthFrames > 0) ? 
                      static_cast<double>(lengthFrames) / sampleRate : 0.0;
    
    m_sampleRate.store(sampleRate, std::memory_order_relaxed);
    m_channels.store(channels, std::memory_order_relaxed);
    m_ringBufferChannels.store(channels, std::memory_order_relaxed);  // Cache for RT fallback
    m_duration.store(duration, std::memory_order_relaxed);
    m_totalFrames.store(lengthFrames, std::memory_order_relaxed);
    
    // Create ring buffer (2 seconds capacity)
    size_t bufferFrames = static_cast<size_t>(sampleRate * 2.0);
    AudioRingBuffer* newBuffer = new AudioRingBuffer(bufferFrames, channels);
    
    // Atomically publish to RT thread (release semantics ensure buffer is fully constructed)
    m_ringBuffer.store(newBuffer, std::memory_order_release);
    
    // Determine pre-buffer amount based on latency target
    size_t preBufferFrames = static_cast<size_t>((targetLatencyMs / 1000.0) * sampleRate);
    if (preBufferFrames < 1024) preBufferFrames = 1024; // Minimum start threshold
    
    m_state.store(State::Streaming, std::memory_order_release);
    
    // Notify ready once we have format info
    // Ideally we might wait until we have some data, but format is known now.
    // Let's pre-buffer a bit first to ensure smooth start.
    
    const size_t chunkSize = kDecodeChunkFrames;
    size_t totalDecoded = 0;
    bool readySignaled = false;
    
    while (!m_stopRequested.load(std::memory_order_relaxed)) {
        // Check available space (lock-free)
        AudioRingBuffer* rb = m_ringBuffer.load(std::memory_order_acquire);
        size_t availableWrite = rb ? rb->availableWrite() : 0;
        
        if (availableWrite < chunkSize) {
            // Buffer full, sleep
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        
        // Decode chunk
        // We decode directly into a temp buffer then write to ring buffer
        // Or we could try to decode directly to ring buffer pointer if we exposed it, 
        // but AudioRingBuffer wraps. Simpler to use temp buffer.
        
        // Use stack buffer for chunk (4096 frames * max 8 channels * float) ~ 128KB
        // Should be fine on stack or use thread_local vector
        static thread_local std::vector<float> chunkBuffer;
        if (chunkBuffer.size() < chunkSize * channels) chunkBuffer.resize(chunkSize * channels);
        
        // Decode into temp buffer
        ma_uint64 framesRead = 0;
        ma_result result = ma_decoder_read_pcm_frames(decoder, chunkBuffer.data(), chunkSize, &framesRead);
        
        if (result != MA_SUCCESS && result != MA_AT_END) {
             // Error reading
             break; 
        }
        
        if (framesRead > 0) {
            AudioRingBuffer* rb = m_ringBuffer.load(std::memory_order_acquire);
            if (rb) {
                rb->write(chunkBuffer.data(), static_cast<size_t>(framesRead));
            }
            
            totalDecoded += framesRead;
            m_decodedFrames.store(totalDecoded, std::memory_order_relaxed);
        }
        
        // Signal ready if we crossed pre-buffer threshold
        if (!readySignaled && totalDecoded >= preBufferFrames) {
            readySignaled = true;
            if (m_onReady) m_onReady(sampleRate, channels, duration);
        }
        
        if (result == MA_AT_END || framesRead == 0) {
            // EOF
            break;
        }
    }
    
    // Ensure we signal ready if file was shorter than pre-buffer
    if (!readySignaled) {
        readySignaled = true;
        if (m_onReady) m_onReady(sampleRate, channels, duration);
    }
    
    m_state.store(State::Complete, std::memory_order_release);
    
    if (m_onComplete) m_onComplete();
    
    closeDecoder();
    
#else
    // Fallback if miniaudio disabled
    m_state.store(State::Error, std::memory_order_release);
    if (m_onError) m_onError("Miniaudio disabled, cannot stream.");
#endif
}

bool StreamingDecoder::openDecoder(const std::string& path) {
#if defined(NOMAD_USE_MINIAUDIO)
    auto* decoder = new ma_decoder;
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    
#ifdef _WIN32
    ma_result result = ma_decoder_init_file_w(pathStringToWide(path).c_str(), &config, decoder);
#else
    ma_result result = ma_decoder_init_file(path.c_str(), &config, decoder);
#endif

    if (result != MA_SUCCESS) {
        delete decoder;
        return false;
    }
    
    m_decoderHandle = decoder;
    return true;
#else
    return false;
#endif
}

void StreamingDecoder::closeDecoder() {
#if defined(NOMAD_USE_MINIAUDIO)
    if (m_decoderHandle) {
        auto* decoder = static_cast<ma_decoder*>(m_decoderHandle);
        ma_decoder_uninit(decoder);
        delete decoder;
        m_decoderHandle = nullptr;
    }
#endif
}

size_t StreamingDecoder::decodeChunk(size_t /*targetFrames*/) {
    // Helper moved inside thread func for simplified logic with stack buffer
    return 0;
}

} // namespace Audio
} // namespace Nomad
