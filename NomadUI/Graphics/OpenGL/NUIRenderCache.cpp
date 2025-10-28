#include "NUIRenderCache.h"
#include <iostream>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <Windows.h>
#endif

// GLAD must be included after Windows headers
#include "../../External/glad/include/glad/glad.h"

#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable: 4005)
#pragma warning(pop)
#endif

namespace NomadUI {

    // ========================================================================================
    // NUIRenderCache
    // ========================================================================================

    NUIRenderCache::NUIRenderCache()
        : m_enabled(true)
        , m_currentFrame(0)
        , m_previousFBO(0)
    {
    }

    NUIRenderCache::~NUIRenderCache() {
        clearAll();
    }

    CachedRenderData* NUIRenderCache::getOrCreateCache(uint64_t widgetId, const NUISize& size) {
        if (!m_enabled) return nullptr;

        auto it = m_caches.find(widgetId);
        if (it != m_caches.end()) {
            // Update last used frame
            it->second->lastUsedFrame = m_currentFrame;

            // Check if size changed
            if (it->second->size.width != size.width || it->second->size.height != size.height) {
                // Recreate framebuffer with new size
                destroyFramebuffer(it->second.get());
                createFramebuffer(it->second.get(), size);
            }

            return it->second.get();
        }

        // Create new cache entry
        auto cache = std::make_unique<CachedRenderData>();
        cache->lastUsedFrame = m_currentFrame;
        createFramebuffer(cache.get(), size);

        CachedRenderData* ptr = cache.get();
        m_caches[widgetId] = std::move(cache);
        return ptr;
    }

    void NUIRenderCache::invalidate(uint64_t widgetId) {
        auto it = m_caches.find(widgetId);
        if (it != m_caches.end()) {
            it->second->valid = false;
        }
    }

    void NUIRenderCache::clearAll() {
        for (auto& pair : m_caches) {
            destroyFramebuffer(pair.second.get());
        }
        m_caches.clear();
    }

    void NUIRenderCache::cleanup(uint64_t currentFrame, uint64_t maxAge) {
        m_currentFrame = currentFrame;

        // Remove caches that haven't been used recently
        auto it = m_caches.begin();
        while (it != m_caches.end()) {
            if ((currentFrame - it->second->lastUsedFrame) > maxAge) {
                destroyFramebuffer(it->second.get());
                it = m_caches.erase(it);
            } else {
                ++it;
            }
        }
    }

    size_t NUIRenderCache::getMemoryUsage() const {
        size_t totalBytes = 0;
        for (const auto& pair : m_caches) {
            const auto& cache = pair.second;
            // Estimate: 4 bytes per pixel (RGBA)
            totalBytes += static_cast<size_t>(cache->size.width * cache->size.height * 4);
        }
        return totalBytes;
    }

    void NUIRenderCache::createFramebuffer(CachedRenderData* cache, const NUISize& size) {
        if (!cache) return;

        cache->size = size;
        cache->valid = false; // TODO: Implement FBO creation after fixing GL 3.0+ support
        
        // For now, caching is disabled until we can properly load GL 3.0+ functions
    }

    void NUIRenderCache::destroyFramebuffer(CachedRenderData* cache) {
        if (!cache) return;
        
        // TODO: Implement FBO destruction
        cache->valid = false;
    }

    void NUIRenderCache::beginCacheRender(CachedRenderData* cache) {
        // TODO: Implement cache rendering
    }

    void NUIRenderCache::endCacheRender() {
        // TODO: Restore previous FBO
    }

    void NUIRenderCache::renderCached(const CachedRenderData* cache, const NUIRect& destRect) {
        // TODO: Render cached texture
    }

    // ========================================================================================
    // NUICachePolicy
    // ========================================================================================

    bool NUICachePolicy::shouldCache(bool isStatic, const NUISize& size, float updateFrequency) {
        // Don't cache if not static
        if (!isStatic) return false;

        // Don't cache if updates too frequently
        if (updateFrequency > MAX_UPDATE_FREQ) return false;

        // Don't cache if too small
        if (size.width < MIN_SIZE_TO_CACHE && size.height < MIN_SIZE_TO_CACHE) {
            return false;
        }

        return true;
    }

    int NUICachePolicy::getCachePriority(const NUISize& size, float renderCost) {
        // Higher priority for larger widgets with higher render cost
        float area = size.width * size.height;
        return static_cast<int>(area * renderCost);
    }

} // namespace NomadUI
