// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#ifndef NUIRENDERCACHE_H
#define NUIRENDERCACHE_H

#include "../../Core/NUITypes.h"
#include <unordered_map>
#include <memory>

namespace NomadUI {

    // Cached render data for a widget
    struct CachedRenderData {
        uint32_t framebufferId;
        uint32_t textureId;
        NUISize size;
        bool valid;
        uint64_t lastUsedFrame;

        CachedRenderData()
            : framebufferId(0)
            , textureId(0)
            , size(0, 0)
            , valid(false)
            , lastUsedFrame(0)
        {
        }
    };

    // Manages cached rendering for static widgets
    class NUIRenderCache {
    public:
        NUIRenderCache();
        ~NUIRenderCache();

        // Create/get a cache entry for a widget
        CachedRenderData* getOrCreateCache(uint64_t widgetId, const NUISize& size);

        // Invalidate a cache entry
        void invalidate(uint64_t widgetId);

        // Clear all caches
        void clearAll();

        // Remove unused caches (LRU cleanup)
        void cleanup(uint64_t currentFrame, uint64_t maxAge = 300);

        // Enable/disable caching
        void setEnabled(bool enabled) { m_enabled = enabled; }
        bool isEnabled() const { return m_enabled; }

        // Get stats
        size_t getCacheCount() const { return m_caches.size(); }
        size_t getMemoryUsage() const;

        // Begin rendering to a cache
        void beginCacheRender(CachedRenderData* cache);

        // End rendering to a cache
        void endCacheRender();

        // Render a cached widget
        void renderCached(const CachedRenderData* cache, const NUIRect& destRect);

    private:
        void createFramebuffer(CachedRenderData* cache, const NUISize& size);
        void destroyFramebuffer(CachedRenderData* cache);

        std::unordered_map<uint64_t, std::unique_ptr<CachedRenderData>> m_caches;
        bool m_enabled;
        uint64_t m_currentFrame;

        // Previous FBO for restoration
        uint32_t m_previousFBO;
    };

    // Helper class to determine if a widget should be cached
    class NUICachePolicy {
    public:
        // Check if a widget should be cached based on its properties
        static bool shouldCache(bool isStatic, const NUISize& size, float updateFrequency);

        // Get cache priority (higher = more important to cache)
        static int getCachePriority(const NUISize& size, float renderCost);

    private:
        static constexpr float MIN_SIZE_TO_CACHE = 100.0f; // Min width or height
        static constexpr float MAX_UPDATE_FREQ = 0.1f;     // Max updates per frame to consider static
    };

} // namespace NomadUI

#endif // NUIRENDERCACHE_H
