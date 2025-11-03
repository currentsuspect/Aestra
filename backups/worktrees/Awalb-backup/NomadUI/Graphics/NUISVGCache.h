// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUITypes.h"
#include <vector>
#include <unordered_map>
#include <chrono>

namespace NomadUI {

// Forward declaration
class NUISVGDocument;

/**
 * Cache for rasterized SVG images to avoid redundant rasterization.
 * 
 * This cache stores RGBA buffers for SVGs that have been rasterized at specific
 * dimensions and tint colors. It uses an LRU-style eviction policy with time-based
 * cleanup to prevent unbounded memory growth.
 */
class NUISVGCache {
public:
    /**
     * Key for cache lookup based on document, dimensions, and tint color.
     */
    struct CacheKey {
        const NUISVGDocument* doc;
        int width;
        int height;
        NUIColor tint;
        
        bool operator==(const CacheKey& other) const {
            return doc == other.doc && 
                   width == other.width && 
                   height == other.height && 
                   tint.r == other.tint.r &&
                   tint.g == other.tint.g &&
                   tint.b == other.tint.b &&
                   tint.a == other.tint.a;
        }
    };
    
    /**
     * Hash function for CacheKey.
     */
    struct CacheKeyHash {
        size_t operator()(const CacheKey& k) const {
            // Combine hashes of all key components
            size_t h1 = std::hash<const void*>()(k.doc);
            size_t h2 = std::hash<int>()(k.width);
            size_t h3 = std::hash<int>()(k.height);
            size_t h4 = std::hash<float>()(k.tint.r);
            size_t h5 = std::hash<float>()(k.tint.g);
            size_t h6 = std::hash<float>()(k.tint.b);
            size_t h7 = std::hash<float>()(k.tint.a);
            
            // Simple hash combination
            return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3) ^ (h5 << 4) ^ (h6 << 5) ^ (h7 << 6);
        }
    };
    
    /**
     * Cached entry containing rasterized RGBA data and metadata.
     */
    struct CacheEntry {
        std::vector<unsigned char> rgba;
        int width;
        int height;
        std::chrono::steady_clock::time_point lastUsed;
    };
    
    NUISVGCache() = default;
    ~NUISVGCache() = default;
    
    // Disable copying
    NUISVGCache(const NUISVGCache&) = delete;
    NUISVGCache& operator=(const NUISVGCache&) = delete;
    
    /**
     * Get cached rasterization or nullptr if not cached.
     * Updates lastUsed timestamp on cache hit.
     */
    const CacheEntry* get(const CacheKey& key);
    
    /**
     * Store rasterization in cache.
     * If cache is full, removes oldest entry first.
     */
    void put(const CacheKey& key, std::vector<unsigned char>&& rgba, int w, int h);
    
    /**
     * Remove entries older than specified age.
     */
    void cleanup(std::chrono::seconds maxAge = std::chrono::seconds(60));
    
    /**
     * Clear all cached entries.
     */
    void clear();
    
    /**
     * Get current cache size.
     */
    size_t size() const { return cache_.size(); }
    
    /**
     * Set maximum number of cached entries.
     */
    void setMaxEntries(size_t max) { maxEntries_ = max; }
    
    /**
     * Get maximum number of cached entries.
     */
    size_t getMaxEntries() const { return maxEntries_; }
    
private:
    std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> cache_;
    size_t maxEntries_ = 100;
};

} // namespace NomadUI
