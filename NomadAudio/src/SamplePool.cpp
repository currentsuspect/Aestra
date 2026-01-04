// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "SamplePool.h"
#include "NomadLog.h"
#include "PathUtils.h"

#include <algorithm>
#include <chrono>
#include <filesystem>

namespace fs = std::filesystem;

namespace Nomad {
namespace Audio {

SamplePool& SamplePool::getInstance() {
    static SamplePool instance;
    return instance;
}

// static: No instance state needed
SampleKey SamplePool::makeKey(const std::string& path) {
    SampleKey key;
    try {
        // Use makeUnicodePath to handle non-ASCII characters properly
        fs::path p = fs::absolute(makeUnicodePath(path));
        // Convert to UTF-8 for consistent cache key regardless of platform
        key.filePath = pathToUtf8(p);
        auto mod = fs::last_write_time(p);
        key.modTime = static_cast<uint64_t>(mod.time_since_epoch().count());
    } catch (const std::exception& e) {
        key.filePath = path; // Fallback to raw path
        key.modTime = 0;
        Log::warning(std::string("SamplePool: failed to stat file '") + path + "': " + e.what());
    }
    return key;
}

// Fast key generation: path-only, no filesystem stat (for instant cache lookups)
SampleKey SamplePool::makeKeyFast(const std::string& path) {
    SampleKey key;
    try {
        fs::path p = fs::absolute(makeUnicodePath(path));
        key.filePath = pathToUtf8(p);
        key.modTime = 0;  // Skip file stat - much faster
    } catch (...) {
        key.filePath = path;
        key.modTime = 0;
    }
    return key;
}

std::shared_ptr<AudioBuffer> SamplePool::tryGetCached(const std::string& path) {
    // Fast path: path-only lookup without filesystem stat
    // This is much faster than acquire() for checking cache hits
    SampleKey fastKey = makeKeyFast(path);
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Try exact path match first (ignoring mod time)
    for (const auto& [key, weakBuf] : m_samples) {
        // Compare paths only, ignore modification time for fast lookup
        if (key.filePath == fastKey.filePath) {
            if (auto existing = weakBuf.lock()) {
                if (existing->ready.load(std::memory_order_acquire)) {
                    existing->lastAccessTick.store(++m_accessCounter);
                    return existing;  // Cache hit
                }
            }
        }
    }
    return nullptr;  // Cache miss
}

size_t SamplePool::calculateBufferBytes(const AudioBuffer& buffer) {
    return buffer.data.size() * sizeof(float);
}

void SamplePool::updateMemoryUsageLocked() {
    size_t total = 0;
    for (const auto& [_, weakBuf] : m_samples) {
        if (auto buf = weakBuf.lock()) {
            total += calculateBufferBytes(*buf);
        }
    }
    m_memoryCurrent.store(total);
}

std::shared_ptr<AudioBuffer> SamplePool::acquire(
    const std::string& path,
    const std::function<bool(AudioBuffer&)>& loader) {
    
    SampleKey key = makeKey(path);

    // Fast path: try to find an existing buffer
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (auto it = m_samples.find(key); it != m_samples.end()) {
            if (auto existing = it->second.lock()) {
                existing->lastAccessTick.store(++m_accessCounter);
                return existing; // Cache hit
            }
            // Expired entry: clean it up
            m_samples.erase(it);
        }
    }

    // Miss: load the sample outside the lock
    if (!loader) {
        Log::warning("SamplePool: no loader provided for missing sample: " + path);
        return nullptr;
    }

    auto buffer = std::make_shared<AudioBuffer>();
    buffer->sourcePath = path;
    
    // Exception-safe loading
    try {
        if (!loader(*buffer)) {
            Log::warning("SamplePool: loader failed for " + path);
            return nullptr;
        }
    } catch (const std::exception& e) {
        Log::warning(std::string("SamplePool: loader exception for ") + path + ": " + e.what());
        return nullptr;
    }

    buffer->numFrames = (buffer->channels > 0) ? buffer->data.size() / buffer->channels : 0;
    buffer->ready.store(true);
    buffer->lastAccessTick.store(++m_accessCounter);

    // Insert into cache (re-check to prevent race)
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Race check: another thread may have loaded this while we were unlocked
    if (auto it = m_samples.find(key); it != m_samples.end()) {
        if (auto existing = it->second.lock()) {
            return existing; // Another thread won: use their buffer
        }
        m_samples.erase(it); // Expired: remove stale entry
    }
    
    m_samples.emplace(key, buffer);
    updateMemoryUsageLocked();
    garbageCollectLocked(); // Already locked: use private variant

    return buffer;
}

void SamplePool::garbageCollect() {
    std::lock_guard<std::mutex> lock(m_mutex);
    garbageCollectLocked();
}

void SamplePool::garbageCollectLocked() {
    // Remove expired entries first
    for (auto it = m_samples.begin(); it != m_samples.end(); ) {
        if (it->second.expired()) {
            it = m_samples.erase(it);
        } else {
            ++it;
        }
    }

    if (m_memoryBudget == 0) {
        return; // Unlimited budget, skip calculation entirely
    }

    // Build live list AND calculate total size in one pass
    struct EntryInfo {
        SampleKey key;
        uint64_t lastTick;
        size_t sizeBytes;
    };
    std::vector<EntryInfo> live;
    live.reserve(m_samples.size());
    
    size_t totalBytes = 0;
    for (auto& [key, weakBuf] : m_samples) {
        if (auto buf = weakBuf.lock()) {
            size_t sz = calculateBufferBytes(*buf);
            live.push_back({key, buf->lastAccessTick.load(), sz});
            totalBytes += sz;
        }
    }

    m_memoryCurrent.store(totalBytes);

    if (m_memoryCurrent.load() <= m_memoryBudget) {
        return;
    }

    // Sort and evict (using pre-calculated sizes)
    std::sort(live.begin(), live.end(), [](const EntryInfo& a, const EntryInfo& b) {
        return a.lastTick < b.lastTick;
    });

    for (const auto& info : live) {
        if (m_memoryCurrent.load() <= m_memoryBudget) break;
        if (m_samples.erase(info.key) > 0) {
            m_memoryCurrent.fetch_sub(info.sizeBytes);
        }
    }
}

void SamplePool::setMemoryBudget(size_t bytes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_memoryBudget = bytes;
    garbageCollectLocked();
}

} // namespace Audio
} // namespace Nomad