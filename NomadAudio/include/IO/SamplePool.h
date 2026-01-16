// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Nomad {
namespace Audio {

/**
 * @brief Unique identity for a sample on disk
 *
 * Uses absolute path + modification time to invalidate cache when files change.
 * Two keys are equal only if both path and modification time match.
 */
struct SampleKey {
    std::string filePath;  // Absolute filesystem path
    uint64_t modTime{0};   // Last modification time (epoch-based)

    bool operator==(const SampleKey& other) const noexcept {
        return filePath == other.filePath && modTime == other.modTime;
    }
};

/**
 * @brief Hash functor for SampleKey
 * 
 * Combines path hash and modification time for unordered_map.
 */
struct SampleKeyHasher {
    size_t operator()(const SampleKey& key) const noexcept {
        size_t h = std::hash<std::string>{}(key.filePath);
        // Combine with modTime using a good hash combiner
        h ^= static_cast<size_t>(key.modTime) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return h;
    }
};

/**
 * @brief Shared audio buffer representation
 *
 * Holds decoded PCM float samples and metadata. Lifetime is managed by shared_ptr
 * returned from SamplePool. When all references are dropped, the buffer becomes
 * eligible for garbage collection.
 */
struct AudioBuffer {
    std::vector<float> data;          // Interleaved float samples [-1.0, 1.0]
    uint32_t channels{0};             // Number of channels (e.g., 1=mono, 2=stereo)
    uint32_t sampleRate{0};           // Sample rate in Hz (e.g., 44100)
    uint64_t numFrames{0};            // Total frames = data.size() / channels
    bool isStreaming{false};          // True if backed by streaming source

    // Future extension point for streaming sources
    std::shared_ptr<void> source;

    // Cache management (automatically updated by SamplePool)
    std::atomic<bool> ready{false};              // true when data is valid
    std::atomic<uint64_t> lastAccessTick{0};     // LRU timestamp
    std::string sourcePath;                      // For debugging/reloading
};

/**
 * @brief Thread-safe LRU cache for decoded audio samples
 *
 * Deduplicates audio buffers by file path, automatically loads on cache miss,
 * and evicts least-recently-used entries when memory budget is exceeded.
 * Lifetime is managed through standard shared_ptr semantics.
 */
class SamplePool {
public:
    static SamplePool& getInstance();

    /**
     * @brief Acquire a buffer for the given path
     * 
     * Returns cached buffer if available; otherwise invokes loader to decode.
     * The loader must fill buffer fields (channels, sampleRate, data) and return true on success.
     * 
     * @param path Filesystem path to audio file
     * @param loader Decoding function (called on cache miss)
     * @return shared_ptr to AudioBuffer, or nullptr on failure
     */
    std::shared_ptr<AudioBuffer> acquire(const std::string& path,
                                         const std::function<bool(AudioBuffer&)>& loader = {});

    /**
     * @brief Perform garbage collection
     * 
     * Removes expired buffers and evicts LRU entries until memory budget is met.
     * Called automatically after each acquire(); manual calls are optional.
     */
    void garbageCollect();

    // Memory budget management
    void setMemoryBudget(size_t bytes);
    size_t getMemoryBudget() const { return m_memoryBudget; }
    
    /**
     * @brief Fast cache lookup (path-only, no filesystem stat)
     * 
     * Checks if a buffer for the given path is already cached, without
     * performing any filesystem operations. Returns nullptr on cache miss.
     * This is much faster than acquire() for checking cache hits.
     * 
     * @param path Filesystem path to audio file
     * @return shared_ptr to AudioBuffer if cached, nullptr otherwise
     */
    std::shared_ptr<AudioBuffer> tryGetCached(const std::string& path);
    
    /**
     * @brief Generate a fast cache key (path-only, no mod time)
     * 
     * Returns a key based only on the normalized path, without checking
     * the file's modification time. Useful for quick cache lookups.
     */
    static SampleKey makeKeyFast(const std::string& path);
    
    /**
     * @brief Get current total memory usage (bytes)
     * 
     * Thread-safe read of memory used by all managed buffers (cached + active).
     */
    size_t getMemoryUsage() const { return m_memoryCurrent.load(); }

private:
    SamplePool() = default;
    ~SamplePool() = default;
    SamplePool(const SamplePool&) = delete;
    SamplePool& operator=(const SamplePool&) = delete;

    // Key generation
    static SampleKey makeKey(const std::string& path);

    // Memory calculation
    static size_t calculateBufferBytes(const AudioBuffer& buffer);

    // Internal helpers (require m_mutex to be held)
    void updateMemoryUsageLocked();
    void garbageCollectLocked();

    // Data members
    mutable std::mutex m_mutex;
    std::unordered_map<SampleKey, std::weak_ptr<AudioBuffer>, SampleKeyHasher> m_samples;
    
    size_t m_memoryBudget{0};                         // 0 = unlimited
    std::atomic<size_t> m_memoryCurrent{0};          // Total bytes of all buffers
    
    std::atomic_uint64_t m_accessCounter{0};         // Monotonic LRU ticker
};

} // namespace Audio
} // namespace Nomad