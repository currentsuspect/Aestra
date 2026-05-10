// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "SamplePool.h"

#include "AestraLog.h"
#include "PathUtils.h"

#include <algorithm>
#include <chrono>
#include <filesystem>

namespace fs = std::filesystem;

namespace Aestra {
namespace Audio {

namespace {
constexpr uint64_t INVALID_MODTIME = 0;

inline void subtractAtomic(std::atomic<size_t>& value, size_t amount) {
    size_t prev = value.fetch_sub(amount, std::memory_order_relaxed);
    (void)prev; // suppress unused warning in release builds
    assert(prev >= amount && "SamplePool: memory tracking underflow");
}
} // namespace

// ------------------------------------------------------------------
// LoadingState
// ------------------------------------------------------------------
std::future<std::shared_ptr<AudioBuffer>> SamplePool::LoadingState::addPromise() {
    std::lock_guard<std::mutex> lock(promisesMutex);
    if (completed) {
        std::promise<std::shared_ptr<AudioBuffer>> p;
        p.set_value(result);
        return p.get_future();
    }
    auto& p = promises.emplace_back();
    return p.get_future();
}

void SamplePool::LoadingState::complete(std::shared_ptr<AudioBuffer> value) {
    std::lock_guard<std::mutex> lock(promisesMutex);
    if (completed) return; // idempotent — defensive against duplicate completion
    completed = true;
    result = value;
    for (auto& p : promises) {
        p.set_value(value);
    }
    promises.clear();
}

// ------------------------------------------------------------------
// Thread-pool configuration
// ------------------------------------------------------------------
std::atomic<size_t> SamplePool::s_configuredThreadPoolSize{0};

void SamplePool::setThreadPoolSize(size_t numThreads) {
    s_configuredThreadPoolSize.store(numThreads, std::memory_order_release);
}

size_t SamplePool::getThreadPoolSize() {
    return s_configuredThreadPoolSize.load(std::memory_order_acquire);
}

size_t SamplePool::computeThreadPoolSize() const {
    size_t configured = s_configuredThreadPoolSize.load(std::memory_order_acquire);
    if (configured > 0) {
        return configured;
    }
    unsigned numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) {
        numThreads = 4;
    }
    if (numThreads > 8) {
        numThreads = 8;
    }
    if (numThreads < 2) {
        numThreads = 2;
    }
    return numThreads;
}

// ------------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------------
SamplePool::SamplePool() {
    size_t numThreads = computeThreadPoolSize();
    for (size_t i = 0; i < numThreads; ++i) {
        m_workers.emplace_back(&SamplePool::workerThread, this);
    }
}

SamplePool::~SamplePool() {
    m_running.store(false, std::memory_order_release);
    m_jobCV.notify_all();
    for (auto& t : m_workers) {
        if (t.joinable()) {
            t.join();
        }
    }

    // Drain any jobs that never got picked up by a worker.
    std::deque<LoadJob> remaining;
    {
        std::lock_guard<std::mutex> lock(m_jobMutex);
        remaining.swap(m_jobQueue);
    }
    for (auto& job : remaining) {
        std::lock_guard<std::mutex> lock(m_inflightMutex);
        m_inflight.erase({job.path, job.modTime});
        job.state->complete(nullptr);
    }

    // Defensive: complete any inflight entries that may still be pending
    // (should not happen in normal shutdown, but prevents caller hangs).
    {
        std::lock_guard<std::mutex> lock(m_inflightMutex);
        for (auto& kv : m_inflight) {
            kv.second->complete(nullptr);
        }
        m_inflight.clear();
    }
}

SamplePool& SamplePool::getInstance() {
    static SamplePool instance;
    return instance;
}

// ------------------------------------------------------------------
// Key generation
// ------------------------------------------------------------------
SampleKey SamplePool::makeKey(const std::string& path) {
    SampleKey key;
    try {
        fs::path p = fs::absolute(makeUnicodePath(path));
        key.filePath = pathToUtf8(p);
        auto mod = fs::last_write_time(p);
        key.modTime = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(mod.time_since_epoch()).count());
    } catch (const std::exception& e) {
        key.filePath = path;
        key.modTime = INVALID_MODTIME;
        Log::warning(std::string("SamplePool: failed to stat file '") + path + "': " + e.what());
    }
    return key;
}

SampleKey SamplePool::makeKeyFast(const std::string& path) {
    SampleKey key;
    try {
        fs::path p = fs::absolute(makeUnicodePath(path));
        key.filePath = pathToUtf8(p);
        key.modTime = 0;
    } catch (const std::exception&) {
        key.filePath = path;
        key.modTime = 0;
    }
    return key;
}

// ------------------------------------------------------------------
// Helpers
// ------------------------------------------------------------------
void SamplePool::touchPath(const std::string& path) {
    auto key = makeKey(path);

    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_cache.find(key.filePath);
    if (it == m_cache.end()) {
        return;
    }

    auto buf = it->second.lock();
    if (!buf) {
        auto metaIt = m_meta.find(key.filePath);
        if (metaIt != m_meta.end()) {
            subtractAtomic(m_memoryCurrent, metaIt->second.bufferBytes);
            m_lru.erase(metaIt->second.lruIt);
            m_meta.erase(metaIt);
        }
        m_cache.erase(it);
        return;
    }

    auto metaIt = m_meta.find(key.filePath);
    if (metaIt == m_meta.end()) {
        // Inconsistent state; do nothing — the periodic sweep will fix it.
        return;
    }

    // Mismatched modTime means our stat is stale (another thread loaded a
    // newer version) or the file changed after we stat'd. Either way, do NOT
    // evict — the cached entry reflects the latest known file state.
    if (metaIt->second.lastKnownModTime != key.modTime) {
        return;
    }

    auto tick = m_accessCounter.fetch_add(1, std::memory_order_relaxed) + 1;
    buf->lastAccessTick.store(tick, std::memory_order_relaxed);
    metaIt->second.lastAccessTick = tick;
    m_lru.splice(m_lru.end(), m_lru, metaIt->second.lruIt);
}

void SamplePool::invalidatePath(const std::string& path) {
    auto key = makeKey(path);

    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_cache.find(key.filePath);
    if (it != m_cache.end()) {
        auto metaIt = m_meta.find(key.filePath);
        if (metaIt != m_meta.end()) {
            subtractAtomic(m_memoryCurrent, metaIt->second.bufferBytes);
            m_lru.erase(metaIt->second.lruIt);
            m_meta.erase(metaIt);
        }
        m_cache.erase(it);
    }
}

std::shared_ptr<AudioBuffer> SamplePool::tryGetCached(const std::string& path) {
    auto key = makeKey(path);

    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_cache.find(key.filePath);
    if (it == m_cache.end()) {
        return nullptr;
    }

    auto buf = it->second.lock();
    if (!buf) {
        auto metaIt = m_meta.find(key.filePath);
        if (metaIt != m_meta.end()) {
            subtractAtomic(m_memoryCurrent, metaIt->second.bufferBytes);
            m_lru.erase(metaIt->second.lruIt);
            m_meta.erase(metaIt);
        }
        m_cache.erase(it);
        return nullptr;
    }

    auto metaIt = m_meta.find(key.filePath);
    if (metaIt == m_meta.end()) {
        return nullptr;
    }

    // Mismatched modTime: our stat is stale or the file changed after we
    // stat'd. The cached entry may be newer than our key, so don't evict.
    if (metaIt->second.lastKnownModTime != key.modTime) {
        return nullptr;
    }

    auto tick = m_accessCounter.fetch_add(1, std::memory_order_relaxed) + 1;
    buf->lastAccessTick.store(tick, std::memory_order_relaxed);
    metaIt->second.lastAccessTick = tick;
    m_lru.splice(m_lru.end(), m_lru, metaIt->second.lruIt);
    return buf;
}

size_t SamplePool::calculateBufferBytes(const AudioBuffer& buffer) {
    return buffer.data.size() * sizeof(float);
}

void SamplePool::updateMemoryUsageLocked() {
    size_t total = 0;
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        if (auto buf = it->second.lock()) {
            total += calculateBufferBytes(*buf);
            ++it;
        } else {
            auto metaIt = m_meta.find(it->first);
            if (metaIt != m_meta.end()) {
                m_lru.erase(metaIt->second.lruIt);
                m_meta.erase(metaIt);
            }
            it = m_cache.erase(it);
        }
    }
    m_memoryCurrent.store(total, std::memory_order_relaxed);
}

// ------------------------------------------------------------------
// Async acquire
// ------------------------------------------------------------------
std::shared_ptr<std::future<std::shared_ptr<AudioBuffer>>>
SamplePool::acquireAsync(const std::string& path, const std::function<bool(AudioBuffer&)>& loader) {
    auto key = makeKey(path);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_cache.find(key.filePath);
        if (it != m_cache.end()) {
            if (auto existing = it->second.lock()) {
                auto metaIt = m_meta.find(key.filePath);
                if (metaIt != m_meta.end() && metaIt->second.lastKnownModTime == key.modTime) {
                    auto tick = m_accessCounter.fetch_add(1, std::memory_order_relaxed) + 1;
                    existing->lastAccessTick.store(tick, std::memory_order_relaxed);
                    metaIt->second.lastAccessTick = tick;
                    m_lru.splice(m_lru.end(), m_lru, metaIt->second.lruIt);
                    std::promise<std::shared_ptr<AudioBuffer>> p;
                    p.set_value(existing);
                    return std::make_shared<std::future<std::shared_ptr<AudioBuffer>>>(
                        p.get_future());
                }
                // Mismatched modTime: our stat is stale. Don't evict — the
                // cached entry may be newer. Proceed to load.
            } else {
                // Expired
                auto metaIt = m_meta.find(key.filePath);
                if (metaIt != m_meta.end()) {
                    subtractAtomic(m_memoryCurrent, metaIt->second.bufferBytes);
                    m_lru.erase(metaIt->second.lruIt);
                    m_meta.erase(metaIt);
                }
                m_cache.erase(it);
            }
        }
    }

    std::shared_ptr<LoadingState> state;
    bool shouldEnqueue = false;

    {
        std::lock_guard<std::mutex> lock(m_inflightMutex);
        InflightKey inflightKey{key.filePath, key.modTime};
        auto it = m_inflight.find(inflightKey);
        if (it != m_inflight.end()) {
            state = it->second;
            shouldEnqueue = false;
        } else {
            state = std::make_shared<LoadingState>();
            state->buffer = std::make_shared<AudioBuffer>();
            state->buffer->sourcePath = path;
            m_inflight.emplace(inflightKey, state);
            shouldEnqueue = true;
        }
    }

    if (shouldEnqueue) {
        {
            std::lock_guard<std::mutex> lock(m_jobMutex);
            m_jobQueue.push_back({path, key.modTime, loader, state});
        }
        m_jobCV.notify_one();
    }

    auto future = std::make_shared<std::future<std::shared_ptr<AudioBuffer>>>(
        state->addPromise());
    return future;
}

// ------------------------------------------------------------------
// Synchronous acquire
// ------------------------------------------------------------------
std::shared_ptr<AudioBuffer> SamplePool::acquire(
    const std::string& path,
    const std::function<bool(AudioBuffer&)>& loader) {

    auto key = makeKey(path);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_cache.find(key.filePath);
        if (it != m_cache.end()) {
            if (auto existing = it->second.lock()) {
                auto metaIt = m_meta.find(key.filePath);
                if (metaIt != m_meta.end() && metaIt->second.lastKnownModTime == key.modTime) {
                    auto tick = m_accessCounter.fetch_add(1, std::memory_order_relaxed) + 1;
                    existing->lastAccessTick.store(tick, std::memory_order_relaxed);
                    metaIt->second.lastAccessTick = tick;
                    m_lru.splice(m_lru.end(), m_lru, metaIt->second.lruIt);
                    return existing;
                }
                // Mismatched modTime: our stat is stale. Don't evict — the
                // cached entry may be newer. Proceed to load.
            } else {
                // Expired
                auto metaIt = m_meta.find(key.filePath);
                if (metaIt != m_meta.end()) {
                    subtractAtomic(m_memoryCurrent, metaIt->second.bufferBytes);
                    m_lru.erase(metaIt->second.lruIt);
                    m_meta.erase(metaIt);
                }
                m_cache.erase(it);
            }
        }
    }

    std::shared_ptr<LoadingState> state;
    bool shouldLoad = false;

    {
        std::lock_guard<std::mutex> lock(m_inflightMutex);
        InflightKey inflightKey{key.filePath, key.modTime};
        auto it = m_inflight.find(inflightKey);
        if (it != m_inflight.end()) {
            state = it->second;
            shouldLoad = false;
        } else {
            state = std::make_shared<LoadingState>();
            state->buffer = std::make_shared<AudioBuffer>();
            state->buffer->sourcePath = path;
            m_inflight.emplace(inflightKey, state);
            shouldLoad = true;
        }
    }

    if (!shouldLoad) {
        return state->addPromise().get();
    }

    if (!loader) {
        std::lock_guard<std::mutex> lock(m_inflightMutex);
        m_inflight.erase({key.filePath, key.modTime});
        state->complete(nullptr);
        Log::warning("SamplePool: no loader provided for missing sample: " + path);
        return nullptr;
    }

    auto tick = m_accessCounter.fetch_add(1, std::memory_order_relaxed) + 1;

    try {
        if (!loader(*state->buffer)) {
            std::lock_guard<std::mutex> lock(m_inflightMutex);
            m_inflight.erase({key.filePath, key.modTime});
            state->complete(nullptr);
            Log::warning("SamplePool: loader failed for " + path);
            return nullptr;
        }
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(m_inflightMutex);
        m_inflight.erase({key.filePath, key.modTime});
        state->complete(nullptr);
        Log::warning(std::string("SamplePool: loader exception for ") + path + ": " + e.what());
        return nullptr;
    }

    state->buffer->numFrames = (state->buffer->channels > 0)
        ? state->buffer->data.size() / state->buffer->channels : 0;
    state->buffer->lastAccessTick.store(tick, std::memory_order_relaxed);
    state->buffer->ready.store(true, std::memory_order_release);

    std::shared_ptr<AudioBuffer> resultBuffer = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Another thread may have loaded it while we were loading.
        // Re-use a single lookup: check for a valid match, otherwise clean up
        // any stale/expired entry before inserting the new one.
        auto it = m_cache.find(key.filePath);
        if (it != m_cache.end()) {
            if (auto existing = it->second.lock()) {
                auto metaIt = m_meta.find(key.filePath);
                if (metaIt != m_meta.end() && metaIt->second.lastKnownModTime == key.modTime) {
                    resultBuffer = existing;
                }
            }
            if (!resultBuffer) {
                auto metaIt = m_meta.find(key.filePath);
                if (metaIt != m_meta.end()) {
                    subtractAtomic(m_memoryCurrent, metaIt->second.bufferBytes);
                    m_lru.erase(metaIt->second.lruIt);
                    m_meta.erase(metaIt);
                }
                m_cache.erase(it);
            }
        }

        if (!resultBuffer) {
            size_t bufBytes = calculateBufferBytes(*state->buffer);
            auto lruIt = m_lru.insert(m_lru.end(), key.filePath);
            m_meta[key.filePath] = {key.modTime, tick, bufBytes, lruIt};
            m_cache[key.filePath] = state->buffer;
            m_memoryCurrent.fetch_add(bufBytes, std::memory_order_relaxed);
            garbageCollectLocked();
            resultBuffer = state->buffer;
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_inflightMutex);
        m_inflight.erase({key.filePath, key.modTime});
    }

    state->complete(resultBuffer);
    return resultBuffer;
}

// ------------------------------------------------------------------
// Worker thread
// ------------------------------------------------------------------
void SamplePool::workerThread() {
    while (m_running.load(std::memory_order_acquire)) {
        LoadJob job;

        {
            std::unique_lock<std::mutex> lock(m_jobMutex);
            m_jobCV.wait(lock, [this]() {
                return !m_jobQueue.empty() || !m_running.load(std::memory_order_acquire);
            });

            if (!m_running.load(std::memory_order_acquire)) {
                break;
            }

            if (m_jobQueue.empty()) {
                continue;
            }

            job = m_jobQueue.front();
            m_jobQueue.pop_front();
        }

        SampleKey key;
        try {
            key = makeKey(job.path);
        } catch (const std::exception& e) {
            Log::warning(std::string("SamplePool: makeKey failed in worker for ") + job.path + ": " + e.what());
            std::lock_guard<std::mutex> lock(m_inflightMutex);
            m_inflight.erase({job.path, job.modTime});
            job.state->complete(nullptr);
            continue;
        }

        auto tick = m_accessCounter.fetch_add(1, std::memory_order_relaxed) + 1;
        std::shared_ptr<AudioBuffer> resultBuffer = nullptr;
        bool loadOk = false;

        try {
            if (job.loader && job.loader(*job.state->buffer)) {
                job.state->buffer->numFrames = (job.state->buffer->channels > 0)
                    ? job.state->buffer->data.size() / job.state->buffer->channels : 0;
                job.state->buffer->lastAccessTick.store(tick, std::memory_order_relaxed);
                job.state->buffer->ready.store(true, std::memory_order_release);
                loadOk = true;
            }
        } catch (const std::exception& e) {
            Log::warning(std::string("SamplePool: async load exception for ") + job.path + ": " + e.what());
        }

        if (loadOk) {
            std::lock_guard<std::mutex> lock(m_mutex);

            // Check if another thread already inserted a valid entry.
            // Re-use a single lookup: check for a match, otherwise clean up
            // any stale/expired entry before inserting the new one.
            auto it = m_cache.find(key.filePath);
            if (it != m_cache.end()) {
                if (auto existing = it->second.lock()) {
                    auto metaIt = m_meta.find(key.filePath);
                    if (metaIt != m_meta.end() && metaIt->second.lastKnownModTime == key.modTime) {
                        resultBuffer = existing;
                    }
                }
                if (!resultBuffer) {
                    auto metaIt = m_meta.find(key.filePath);
                    if (metaIt != m_meta.end()) {
                        subtractAtomic(m_memoryCurrent, metaIt->second.bufferBytes);
                        m_lru.erase(metaIt->second.lruIt);
                        m_meta.erase(metaIt);
                    }
                    m_cache.erase(it);
                }
            }

            if (!resultBuffer) {
                size_t bufBytes = calculateBufferBytes(*job.state->buffer);
                auto lruIt = m_lru.insert(m_lru.end(), key.filePath);
                m_meta[key.filePath] = {key.modTime, tick, bufBytes, lruIt};
                m_cache[key.filePath] = job.state->buffer;
                m_memoryCurrent.fetch_add(bufBytes, std::memory_order_relaxed);
                garbageCollectLocked();
                resultBuffer = job.state->buffer;
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_inflightMutex);
            m_inflight.erase({job.path, job.modTime});
        }

        if (loadOk && resultBuffer) {
            job.state->complete(resultBuffer);
        } else {
            job.state->complete(nullptr);
        }
    }
}

// ------------------------------------------------------------------
// GC / budget
// ------------------------------------------------------------------
void SamplePool::garbageCollect() {
    std::lock_guard<std::mutex> lock(m_mutex);
    updateMemoryUsageLocked();
    garbageCollectLocked();
}

void SamplePool::garbageCollectLocked() {
    // Phase 1: Remove expired entries
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        if (it->second.expired()) {
            auto metaIt = m_meta.find(it->first);
            if (metaIt != m_meta.end()) {
                subtractAtomic(m_memoryCurrent, metaIt->second.bufferBytes);
                m_lru.erase(metaIt->second.lruIt);
                m_meta.erase(metaIt);
            }
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }

    if (m_memoryBudget == 0) {
        return;
    }

    // Phase 2: Evict LRU entries while over budget
    while (m_memoryCurrent.load(std::memory_order_relaxed) > m_memoryBudget && !m_lru.empty()) {
        auto lruPath = m_lru.front();
        auto metaIt = m_meta.find(lruPath);
        if (metaIt == m_meta.end()) {
            m_lru.pop_front();
            continue;
        }

        auto cacheIt = m_cache.find(lruPath);
        if (cacheIt != m_cache.end()) {
            subtractAtomic(m_memoryCurrent, metaIt->second.bufferBytes);
            m_cache.erase(cacheIt);
        }

        m_lru.pop_front();
        m_meta.erase(metaIt);
    }
}

void SamplePool::setMemoryBudget(size_t bytes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_memoryBudget = bytes;
    garbageCollectLocked();
}

} // namespace Audio
} // namespace Aestra
