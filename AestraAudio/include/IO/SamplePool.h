// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <condition_variable>

namespace Aestra {
namespace Audio {

struct SampleKey {
    std::string filePath;
    uint64_t modTime{0};

    bool operator==(const SampleKey& other) const noexcept {
        return filePath == other.filePath && modTime == other.modTime;
    }
};

struct SampleKeyHasher {
    size_t operator()(const SampleKey& key) const noexcept {
        size_t h = std::hash<std::string>{}(key.filePath);
        h ^= static_cast<size_t>(key.modTime) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return h;
    }
};

struct AudioBuffer {
    std::vector<float> data;
    uint32_t channels{0};
    uint32_t sampleRate{0};
    uint64_t numFrames{0};
    bool isStreaming{false};

    std::shared_ptr<void> source;

    std::atomic<bool> ready{false};
    std::atomic<uint64_t> lastAccessTick{0};
    std::string sourcePath;
};

class SamplePool {
public:
    static SamplePool& getInstance();

    /**
     * @brief Configure the background thread-pool size.
     *
     * Must be called before the first getInstance() invocation.
     * If never called (or called with 0), the pool auto-detects
     * based on hardware concurrency (capped at 8, minimum 2).
     */
    static void setThreadPoolSize(size_t numThreads);
    static size_t getThreadPoolSize();

    /**
     * @brief Synchronously load or retrieve a cached sample.
     *
     * @note NOT realtime-safe. This path stats the file on the calling thread,
     *       which may block on disk I/O. Call from a non-RT thread.
     */
    std::shared_ptr<AudioBuffer> acquire(const std::string& path, const std::function<bool(AudioBuffer&)>& loader = {});

    /**
     * @brief Asynchronously load a sample via the thread pool.
     *
     * @note Cache-hit returns immediately (no I/O). Cache-miss schedules
     *       loading on a background worker. Safe to call from any thread,
     *       but the returned future should not be waited on in a RT path.
     */
    std::shared_ptr<std::future<std::shared_ptr<AudioBuffer>>>
    acquireAsync(const std::string& path, const std::function<bool(AudioBuffer&)>& loader);

    void garbageCollect();

    void setMemoryBudget(size_t bytes);
    size_t getMemoryBudget() const { return m_memoryBudget; }

    std::shared_ptr<AudioBuffer> tryGetCached(const std::string& path);

    static SampleKey makeKeyFast(const std::string& path);
    static SampleKey makeKey(const std::string& path);

    size_t getMemoryUsage() const { return m_memoryCurrent.load(); }

    void invalidatePath(const std::string& path);

    void touchPath(const std::string& path);

private:
    SamplePool();
    ~SamplePool();

    static size_t calculateBufferBytes(const AudioBuffer& buffer);

    void updateMemoryUsageLocked();
    void garbageCollectLocked();

    void workerThread();

    size_t computeThreadPoolSize() const;

    // --- Internal cache entry metadata ---
    struct CacheMeta {
        uint64_t lastKnownModTime{0};
        uint64_t lastAccessTick{0};
        size_t bufferBytes{0};
        std::list<std::string>::iterator lruIt;
    };

    // --- In-flight loading key ---
    struct InflightKey {
        std::string path;
        uint64_t modTime{0};
        bool operator==(const InflightKey& other) const noexcept {
            return path == other.path && modTime == other.modTime;
        }
    };
    struct InflightKeyHash {
        size_t operator()(const InflightKey& key) const noexcept {
            size_t h = std::hash<std::string>{}(key.path);
            h ^= static_cast<size_t>(key.modTime) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            return h;
        }
    };

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::weak_ptr<AudioBuffer>> m_cache;
    std::unordered_map<std::string, CacheMeta> m_meta;
    std::list<std::string> m_lru;

    struct LoadingState;
    std::unordered_map<InflightKey, std::shared_ptr<LoadingState>, InflightKeyHash> m_inflight;
    mutable std::mutex m_inflightMutex;

    struct LoadJob {
        std::string path;
        uint64_t modTime{0};
        std::function<bool(AudioBuffer&)> loader;
        std::shared_ptr<LoadingState> state;
    };
    std::deque<LoadJob> m_jobQueue;
    std::mutex m_jobMutex;
    std::condition_variable m_jobCV;
    std::vector<std::thread> m_workers;
    std::atomic<bool> m_running{true};

    size_t m_memoryBudget{0};
    std::atomic<size_t> m_memoryCurrent{0};

    std::atomic_uint64_t m_accessCounter{0};

    static std::atomic<size_t> s_configuredThreadPoolSize;
};

struct SamplePool::LoadingState {
    std::list<std::promise<std::shared_ptr<AudioBuffer>>> promises;
    std::mutex promisesMutex;
    bool completed{false};
    std::shared_ptr<AudioBuffer> result;
    std::shared_ptr<AudioBuffer> buffer;

    std::future<std::shared_ptr<AudioBuffer>> addPromise();
    void complete(std::shared_ptr<AudioBuffer> value);
};

} // namespace Audio
} // namespace Aestra
