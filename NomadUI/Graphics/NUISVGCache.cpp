// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUISVGCache.h"

namespace NomadUI {

const NUISVGCache::CacheEntry* NUISVGCache::get(const CacheKey& key) {
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        // Update lastUsed timestamp on cache hit
        it->second.lastUsed = std::chrono::steady_clock::now();
        return &it->second;
    }
    return nullptr;
}

void NUISVGCache::put(const CacheKey& key, std::vector<unsigned char>&& rgba, int w, int h) {
    // If cache is full, remove oldest entry
    if (cache_.size() >= maxEntries_) {
        auto oldestIt = cache_.begin();
        auto oldestTime = oldestIt->second.lastUsed;
        
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->second.lastUsed < oldestTime) {
                oldestTime = it->second.lastUsed;
                oldestIt = it;
            }
        }
        
        cache_.erase(oldestIt);
    }
    
    // Store new entry with current timestamp
    CacheEntry entry;
    entry.rgba = std::move(rgba);
    entry.width = w;
    entry.height = h;
    entry.lastUsed = std::chrono::steady_clock::now();
    
    cache_[key] = std::move(entry);
}

void NUISVGCache::cleanup(std::chrono::seconds maxAge) {
    auto now = std::chrono::steady_clock::now();
    
    for (auto it = cache_.begin(); it != cache_.end(); ) {
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.lastUsed);
        if (age > maxAge) {
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

void NUISVGCache::clear() {
    cache_.clear();
}

} // namespace NomadUI
