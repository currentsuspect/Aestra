// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUIRenderCache.h"
#include "NUIRendererGL.h"
#include <iostream>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <Windows.h>
#endif

// GLAD must be included after Windows headers
#include "../../External/glad/include/glad/glad.h"
#include <cstring>

#ifndef GL_DRAW_BUFFER
#define GL_DRAW_BUFFER 0x0C01
#endif
#ifndef GL_SCISSOR_BOX
#define GL_SCISSOR_BOX 0x0C10
#endif
#ifndef GL_READ_BUFFER
#define GL_READ_BUFFER 0x0C02
#endif

#ifndef GL_VIEWPORT
#define GL_VIEWPORT 0x0BA2
#endif

#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable: 4005)
#pragma warning(pop)
#endif

namespace NomadUI {
    // Small GL debug helper (marker-only; glGetError may be trimmed in our GL loader)
    static void glCheckLog(const char* where, bool enabled) {
        if (!enabled) return;
        std::cerr << "[NUIRenderCache] GL checkpoint: " << where << std::endl;
    }

    // ========================================================================================
    // NUIRenderCache
    // ========================================================================================

    NUIRenderCache::NUIRenderCache()
        : m_enabled(true)
        , m_currentFrame(0)
        , m_previousFBO(0)
        , m_previousViewport{0, 0, 0, 0}
        , m_restoreViewport(false)
        , m_previousScissorEnabled(false)
        , m_activeCache(nullptr)
        , m_renderInProgress(false)
        , m_renderer(nullptr)
    {
    }

    NUIRenderCache::~NUIRenderCache() {
        clearAll();
    }

    CachedRenderData* NUIRenderCache::getOrCreateCache(uint64_t widgetId, const NUISize& size) {
        if (!m_enabled) return nullptr;

        auto it = m_caches.find(widgetId);
        if (it != m_caches.end()) {
            it->second->lastUsedFrame = m_currentFrame;

            if (it->second->size.width != size.width || it->second->size.height != size.height) {
                destroyFramebuffer(it->second.get());
                createFramebuffer(it->second.get(), size);
            }

            return it->second.get();
        }

        // Soft cap memory usage to avoid runaway FBO allocations
        size_t currentBytes = getMemoryUsage();
        size_t newBytes = static_cast<size_t>(size.width * size.height * 4);
        if (currentBytes + newBytes > m_maxMemoryBytes) {
            std::cerr << "[NUIRenderCache] Skipping cache creation (over budget): "
                      << currentBytes + newBytes << " bytes" << std::endl;
            return nullptr;
        }

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
        if (size.width <= 0 || size.height <= 0) return;

        cache->size = size;
        cache->valid = false;
        cache->ownsTexture = true;
        cache->rendererTextureId = 0;
        cache->textureId = 0;

        // Generate FBO
        glGenFramebuffers(1, &cache->framebufferId);
        glBindFramebuffer(GL_FRAMEBUFFER, cache->framebufferId);
    glCheckLog("glBindFramebuffer(create)", m_debug);

        // Try to allocate a renderer-managed texture first so batching recognizes it
        if (m_renderer) {
            cache->rendererTextureId = m_renderer->createTexture(nullptr, size.width, size.height);
            if (cache->rendererTextureId != 0) {
                cache->textureId = m_renderer->getGLTextureId(cache->rendererTextureId);
                if (cache->textureId != 0) {
                    cache->ownsTexture = false;
                } else {
                    m_renderer->deleteTexture(cache->rendererTextureId);
                    cache->rendererTextureId = 0;
                }
            }
        }

        if (cache->textureId == 0) {
            glGenTextures(1, &cache->textureId);
            cache->ownsTexture = true;
        }

        glBindTexture(GL_TEXTURE_2D, cache->textureId);

        // Ensure texture storage and parameters are correct regardless of ownership.
        // Many drivers default MIN_FILTER to a mipmapped mode, which causes sampling to
        // return black when no mipmaps exist. Always force non-mipmapped linear filtering
        // and clamp-to-edge for FBO-backed textures used as UI caches.
        // Allocate (or re-allocate) storage to the required size.
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size.width, size.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Attach texture to FBO
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, cache->textureId, 0);
    glCheckLog("glFramebufferTexture2D", m_debug);

        // For core-profile correctness, explicitly select the color attachment as the
        // active draw/read buffer for this FBO.
    // Prefer simple single-buffer variant for broader header compatibility
    GLenum drawBuf = GL_COLOR_ATTACHMENT0;
    glDrawBuffer(drawBuf);
#if defined(GL_READ_BUFFER)
        glReadBuffer(GL_COLOR_ATTACHMENT0);
#endif

        // Check FBO completeness
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "FBO creation failed! Status: " << status << std::endl;
            // Clean up on failure
            if (cache->rendererTextureId != 0 && m_renderer) {
                m_renderer->deleteTexture(cache->rendererTextureId);
                cache->rendererTextureId = 0;
            } else if (cache->textureId != 0) {
                glDeleteTextures(1, &cache->textureId);
            }
            glDeleteFramebuffers(1, &cache->framebufferId);
            cache->textureId = 0;
            cache->framebufferId = 0;
            cache->ownsTexture = true;
            cache->valid = false;
        }

        // Unbind
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void NUIRenderCache::destroyFramebuffer(CachedRenderData* cache) {
        if (!cache) return;
        
        if (cache->rendererTextureId != 0 && m_renderer) {
            m_renderer->deleteTexture(cache->rendererTextureId);
            cache->rendererTextureId = 0;
            cache->textureId = 0;
        } else if (cache->textureId != 0) {
            glDeleteTextures(1, &cache->textureId);
            cache->textureId = 0;
        }
        
        if (cache->framebufferId != 0) {
            glDeleteFramebuffers(1, &cache->framebufferId);
            cache->framebufferId = 0;
        }
        
        cache->valid = false;
        cache->ownsTexture = true;
    }

    void NUIRenderCache::beginCacheRender(CachedRenderData* cache) {
        if (!cache || cache->framebufferId == 0) {
            m_activeCache = nullptr;
            m_renderInProgress = false;
            return;
        }

        // Save current FBO
        GLint currentFBO;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFBO);
        m_previousFBO = static_cast<uint32_t>(currentFBO);

        // Capture current viewport to restore later
        GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
        m_previousViewport[0] = viewport[0];
        m_previousViewport[1] = viewport[1];
        m_previousViewport[2] = viewport[2];
        m_previousViewport[3] = viewport[3];
        m_restoreViewport = true;

        // Capture and restore clear color/draw/read buffers to avoid side effects
        glGetIntegerv(GL_DRAW_BUFFER, reinterpret_cast<GLint*>(&m_previousDrawBuffer));
#if defined(GL_COLOR_CLEAR_VALUE)
        glGetFloatv(GL_COLOR_CLEAR_VALUE, m_previousClearColor);
        m_restoreClearColor = true;
#else
        m_restoreClearColor = false;
#endif

        // Track active cache so we can mark content valid when finished
        m_activeCache = cache;
        cache->valid = false;
        m_renderInProgress = true;

        // Bind our FBO
        glBindFramebuffer(GL_FRAMEBUFFER, cache->framebufferId);
        
        // Set viewport to match FBO size
        glViewport(0, 0, cache->size.width, cache->size.height);

    // Ensure subsequent draws hit our color buffer and start from a clean slate
    GLenum drawBuf = GL_COLOR_ATTACHMENT0;
    glDrawBuffer(drawBuf);
#if defined(GL_READ_BUFFER)
    glReadBuffer(GL_COLOR_ATTACHMENT0);
#endif
    // Capture and preserve caller scissor state, then disable to avoid accidental clipping.
    // Use glGetBooleanv rather than glIsEnabled for compatibility with some GL loaders.
    // Ask the renderer (if available) for scissor state; avoids calling GL query
    // functions directly which may not be exposed by the project's GL loader.
    if (m_renderer) {
        m_previousScissorEnabled = m_renderer->isScissorEnabled();
    } else {
        m_previousScissorEnabled = false;
    }
    GLint scissorBox[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_SCISSOR_BOX, scissorBox);
    std::memcpy(m_previousScissorBox, scissorBox, sizeof(m_previousScissorBox));
    m_restoreScissorBox = true;
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
        glCheckLog("beginCacheRender", m_debug);

        // Ensure renderer uses an orthographic projection matching the FBO size
        if (m_renderer) {
            m_renderer->beginOffscreen(cache->size.width, cache->size.height);
        }
    }

    void NUIRenderCache::endCacheRender() {
        if (!m_renderInProgress) {
            return;
        }

        // Restore previous FBO
        glBindFramebuffer(GL_FRAMEBUFFER, m_previousFBO);

        if (m_restoreViewport) {
            glViewport(
                static_cast<GLint>(m_previousViewport[0]),
                static_cast<GLint>(m_previousViewport[1]),
                static_cast<GLint>(m_previousViewport[2]),
                static_cast<GLint>(m_previousViewport[3])
            );
            m_restoreViewport = false;
        }

        // Restore draw buffer
        glDrawBuffer(static_cast<GLenum>(m_previousDrawBuffer));
        if (m_restoreClearColor) {
            glClearColor(m_previousClearColor[0], m_previousClearColor[1],
                         m_previousClearColor[2], m_previousClearColor[3]);
            m_restoreClearColor = false;
        }

        if (m_activeCache) {
            // Mark cached content as valid now that rendering is complete
            m_activeCache->valid = true;
            m_activeCache = nullptr;
        }
        
        m_renderInProgress = false;

        // Viewport restored to previous state above
        // Restore renderer projection
        if (m_renderer) {
            m_renderer->endOffscreen();
        }
        // Restore caller scissor test state saved at beginCacheRender
        if (m_restoreScissorBox) {
            glScissor(m_previousScissorBox[0], m_previousScissorBox[1],
                      m_previousScissorBox[2], m_previousScissorBox[3]);
            m_restoreScissorBox = false;
        }
        if (m_previousScissorEnabled) {
            glEnable(GL_SCISSOR_TEST);
        } else {
            glDisable(GL_SCISSOR_TEST);
        }
        glCheckLog("endCacheRender", m_debug);
    }

    void NUIRenderCache::renderCached(const CachedRenderData* cache, const NUIRect& destRect) {
        if (!cache || !cache->valid) return;

        // Prefer renderer-managed texture path to ensure compatibility with shader pipeline
        if (m_renderer && cache->rendererTextureId != 0) {
            // Flip vertically when sampling to match UI's top-left origin.
            NUIRect src(0.0f, 0.0f, static_cast<float>(cache->size.width), static_cast<float>(cache->size.height));
            m_renderer->drawTextureFlippedV(cache->rendererTextureId, destRect, src);
            return;
        }

        // If no renderer texture is available, skip rendering rather than using deprecated immediate mode.
    }

    void NUIRenderCache::renderCachedOrUpdate(CachedRenderData* cache, const NUIRect& destRect,
                                              const std::function<void()>& renderCallback) {
        if (!cache) return;
        if (!cache->valid) {
            if (m_renderer && renderCallback) {
                beginCacheRender(cache);
                renderCallback();
                endCacheRender();
            }
        }
        renderCached(cache, destRect);
    }

    // ========================================================================================
    // NUICachePolicy
    // ========================================================================================

    bool NUICachePolicy::shouldCache(bool isStatic, const NUISize& size, float updateFrequency) {
        // Don't cache if not static
        if (!isStatic) return false;

        // Don't cache if updates too frequently
        if (updateFrequency > MAX_UPDATE_FREQ) return false;

        // Don't cache small widgets (either dimension under threshold)
        if (size.width < MIN_SIZE_TO_CACHE || size.height < MIN_SIZE_TO_CACHE) {
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
