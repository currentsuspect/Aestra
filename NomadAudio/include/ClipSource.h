// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "TimeTypes.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Nomad {
namespace Audio {

// Forward declarations
class WaveformCache;

// =============================================================================
// ClipSourceID - Unique identifier for audio sources
// =============================================================================

/**
 * @brief Unique identifier for a ClipSource
 * 
 * Used to reference audio data without holding shared_ptr in hot paths.
 */
struct ClipSourceID {
    uint32_t value = 0;
    
    bool isValid() const { return value != 0; }
    bool operator==(const ClipSourceID& other) const { return value == other.value; }
    bool operator!=(const ClipSourceID& other) const { return value != other.value; }
    bool operator<(const ClipSourceID& other) const { return value < other.value; }
    
    struct Hash {
        size_t operator()(const ClipSourceID& id) const { return std::hash<uint32_t>()(id.value); }
    };
};

// =============================================================================
// AudioBufferData - Raw PCM audio in memory
// =============================================================================

/**
 * @brief Raw audio sample data in memory
 * 
 * This is the lowest-level representation of audio data.
 * Stored as interleaved float samples [-1.0, 1.0].
 */
struct AudioBufferData {
    std::vector<float> interleavedData;    ///< Interleaved audio samples
    uint32_t sampleRate = 0;               ///< Original sample rate in Hz
    uint32_t numChannels = 0;              ///< Number of channels (1=mono, 2=stereo)
    SampleIndex numFrames = 0;             ///< Total number of frames (samples per channel)
    
    /// Calculate total size in bytes
    size_t sizeInBytes() const {
        return interleavedData.size() * sizeof(float);
    }
    
    /// Get duration in seconds
    double durationSeconds() const {
        if (sampleRate == 0) return 0.0;
        return samplesToSeconds(numFrames, static_cast<double>(sampleRate));
    }
    
    /// Check if buffer is valid and ready for use
    bool isValid() const {
        return !interleavedData.empty() && 
               numChannels > 0 && 
               sampleRate > 0 &&
               numFrames > 0 &&
               interleavedData.size() == static_cast<size_t>(numFrames * numChannels);
    }
    
    /// Get a pointer to the start of a specific frame
    const float* getFramePtr(SampleIndex frameIndex) const {
        if (frameIndex < 0 || frameIndex >= numFrames) return nullptr;
        return interleavedData.data() + (frameIndex * numChannels);
    }
    
    /// Get sample value at specific frame and channel
    float getSample(SampleIndex frame, uint32_t channel) const {
        if (frame < 0 || frame >= numFrames || channel >= numChannels) return 0.0f;
        return interleavedData[static_cast<size_t>(frame * numChannels + channel)];
    }
};

// =============================================================================
// ClipSource - The logical audio source wrapper
// =============================================================================

/**
 * @brief Logical wrapper around audio data
 * 
 * ClipSource represents a single audio file/buffer that can be referenced
 * by multiple AudioClips. This separation allows:
 * - Multiple clips to share the same audio data
 * - Future support for disk streaming vs RAM
 * - Non-destructive operations (reverse, pitch shift) without duplicating data
 * - Efficient memory management through shared ownership
 * 
 * The ClipSource is owned by the project's SourceManager, and clips
 * hold only a ClipSourceID reference.
 */
class ClipSource {
public:
    ClipSource() = default;
    explicit ClipSource(ClipSourceID id, const std::string& name = "");
    ~ClipSource() = default;
    
    // Non-copyable, movable
    ClipSource(const ClipSource&) = delete;
    ClipSource& operator=(const ClipSource&) = delete;
    ClipSource(ClipSource&&) = default;
    ClipSource& operator=(ClipSource&&) = default;
    
    // === Identity ===
    ClipSourceID getID() const { return m_id; }
    void setID(ClipSourceID id) { m_id = id; }
    
    const std::string& getName() const { return m_name; }
    void setName(const std::string& name) { m_name = name; }
    
    const std::string& getFilePath() const { return m_filePath; }
    void setFilePath(const std::string& path) { m_filePath = path; }
    
    // === Audio Data ===
    const std::shared_ptr<AudioBufferData>& getBuffer() const { return m_buffer; }
    void setBuffer(std::shared_ptr<AudioBufferData> buffer);
    
    /// Check if audio data is loaded and ready
    bool isReady() const { return m_buffer && m_buffer->isValid(); }
    
    /// Get audio properties (delegates to buffer)
    uint32_t getSampleRate() const { return m_buffer ? m_buffer->sampleRate : 0; }
    uint32_t getNumChannels() const { return m_buffer ? m_buffer->numChannels : 0; }
    SampleIndex getNumFrames() const { return m_buffer ? m_buffer->numFrames : 0; }
    double getDurationSeconds() const { return m_buffer ? m_buffer->durationSeconds() : 0.0; }
    
    /// Get raw pointer for RT access (caller must ensure lifetime)
    const AudioBufferData* getRawBuffer() const { return m_buffer.get(); }
    
    // === Waveform Cache ===
    void setWaveformCache(std::shared_ptr<WaveformCache> cache) { m_waveformCache = std::move(cache); }
    std::shared_ptr<WaveformCache> getWaveformCache() const { return m_waveformCache; }
    
    // === Metadata ===
    void setFileModTime(uint64_t modTime) { m_fileModTime = modTime; }
    uint64_t getFileModTime() const { return m_fileModTime; }
    
    /// Mark source as streaming (future: disk streaming support)
    void setStreaming(bool streaming) { m_isStreaming = streaming; }
    bool isStreaming() const { return m_isStreaming; }

private:
    ClipSourceID m_id;
    std::string m_name;
    std::string m_filePath;
    
    std::shared_ptr<AudioBufferData> m_buffer;
    std::shared_ptr<WaveformCache> m_waveformCache;
    
    uint64_t m_fileModTime = 0;
    bool m_isStreaming = false;
};

// =============================================================================
// SourceManager - Manages all ClipSources in a project
// =============================================================================

/**
 * @brief Central manager for all audio sources in a project
 * 
 * Provides:
 * - Unique ID generation for sources
 * - Source lookup by ID or file path
 * - Deduplication of files (same file = same source)
 * - Lifetime management
 * 
 * This runs on the UI/Engine thread, never on the RT thread.
 */
class SourceManager {
public:
    SourceManager();
    ~SourceManager();
    
    // Non-copyable
    SourceManager(const SourceManager&) = delete;
    SourceManager& operator=(const SourceManager&) = delete;
    
    /// Create a new source with generated ID
    ClipSourceID createSource(const std::string& name = "");
    
    /// Create or get existing source for a file path
    ClipSourceID getOrCreateSource(const std::string& filePath);
    
    /// Get source by ID
    ClipSource* getSource(ClipSourceID id);
    const ClipSource* getSource(ClipSourceID id) const;
    
    /// Get source by file path
    ClipSource* getSourceByPath(const std::string& filePath);
    
    /// Remove a source (only if no clips reference it)
    bool removeSource(ClipSourceID id);
    
    /// Get all source IDs
    std::vector<ClipSourceID> getAllSourceIDs() const;
    
    /// Get source count
    size_t getSourceCount() const;
    
    /// Clear all sources
    void clear();
    
    /// Get total memory usage of all loaded sources
    size_t getTotalMemoryUsage() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Audio
} // namespace Nomad
