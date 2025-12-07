// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUITypes.h"
#include <string>
#include <memory>

// Forward declarations for optimization systems
namespace NomadUI {
    class NUIRenderBatch;
    class NUIDirtyRegionManager;
    class NUIRenderCache;
}

namespace NomadUI {

/**
 * Abstract renderer interface for the Nomad UI framework.
 * 
 * This provides a platform-agnostic API for rendering UI elements.
 * Implementations exist for OpenGL and Vulkan.
 */
class NUIRenderer {
public:
    virtual ~NUIRenderer() = default;
    
    // ========================================================================
    // Lifecycle
    // ========================================================================
    
    /**
     * Initialize the renderer with the given viewport size.
     */
    virtual bool initialize(int width, int height) = 0;
    
    /**
     * Shutdown and cleanup resources.
     */
    virtual void shutdown() = 0;
    
    /**
     * Resize the viewport.
     */
    virtual void resize(int width, int height) = 0;
    
    // ========================================================================
    // Frame Management
    // ========================================================================
    
    /**
     * Begin a new frame.
     */
    virtual void beginFrame() = 0;
    
    /**
     * End the current frame and present to screen.
     */
    virtual void endFrame() = 0;
    
    /**
     * Clear the framebuffer with the given color.
     */
    virtual void clear(const NUIColor& color) = 0;
    
    // ========================================================================
    // State Management
    // ========================================================================
    
    /**
     * Push a transform matrix onto the stack.
     */
    virtual void pushTransform(float tx, float ty, float rotation = 0.0f, float scale = 1.0f) = 0;
    
    /**
     * Pop the transform matrix from the stack.
     */
    virtual void popTransform() = 0;
    
    /**
     * Set the current clip rectangle (scissor test).
     */
    virtual void setClipRect(const NUIRect& rect) = 0;
    
    /**
     * Clear the clip rectangle.
     */
    virtual void clearClipRect() = 0;
    
    /**
     * Set global opacity for subsequent draw calls.
     */
    virtual void setOpacity(float opacity) = 0;
    
    // ========================================================================
    // Primitive Drawing
    // ========================================================================
    
    /**
     * Draw a filled rectangle.
     */
    virtual void fillRect(const NUIRect& rect, const NUIColor& color) = 0;
    
    /**
     * Draw a filled rounded rectangle.
     */
    virtual void fillRoundedRect(const NUIRect& rect, float radius, const NUIColor& color) = 0;
    
    /**
     * Draw a rectangle outline.
     */
    virtual void strokeRect(const NUIRect& rect, float thickness, const NUIColor& color) = 0;
    
    /**
     * Draw a rounded rectangle outline.
     */
    virtual void strokeRoundedRect(const NUIRect& rect, float radius, float thickness, const NUIColor& color) = 0;
    
    /**
     * Draw a filled circle.
     */
    virtual void fillCircle(const NUIPoint& center, float radius, const NUIColor& color) = 0;
    
    /**
     * Draw a circle outline.
     */
    virtual void strokeCircle(const NUIPoint& center, float radius, float thickness, const NUIColor& color) = 0;
    
    /**
     * Draw a line.
     */
    virtual void drawLine(const NUIPoint& start, const NUIPoint& end, float thickness, const NUIColor& color) = 0;
    
    /**
     * Draw a polyline (connected line segments).
     */
    virtual void drawPolyline(const NUIPoint* points, int count, float thickness, const NUIColor& color) = 0;
    
    /**
     * Draw a waveform as an optimized filled shape (triangle strip).
     * Much faster than per-pixel lines for audio visualization.
     * @param topPoints Array of points defining the top edge of the waveform
     * @param bottomPoints Array of points defining the bottom edge (same count as topPoints)
     * @param count Number of points in each array
     * @param color Fill color for the waveform
     */
    virtual void fillWaveform(const NUIPoint* topPoints, const NUIPoint* bottomPoints, int count, const NUIColor& color) = 0;
    
    // ========================================================================
    // Gradient Drawing
    // ========================================================================
    
    /**
     * Draw a linear gradient rectangle.
     */
    virtual void fillRectGradient(const NUIRect& rect, const NUIColor& colorStart, const NUIColor& colorEnd, bool vertical = true) = 0;
    
    /**
     * Draw a radial gradient circle.
     */
    virtual void fillCircleGradient(const NUIPoint& center, float radius, const NUIColor& colorInner, const NUIColor& colorOuter) = 0;
    
    // ========================================================================
    // Effects
    // ========================================================================
    
    /**
     * Draw a glow effect around a rectangle.
     */
    virtual void drawGlow(const NUIRect& rect, float radius, float intensity, const NUIColor& color) = 0;
    
    /**
     * Draw a shadow.
     */
    virtual void drawShadow(const NUIRect& rect, float offsetX, float offsetY, float blur, const NUIColor& color) = 0;
    
    // ========================================================================
    // Text Rendering
    // ========================================================================
    
    /**
     * Draw text at the given position.
     */
    virtual void drawText(const std::string& text, const NUIPoint& position, float fontSize, const NUIColor& color) = 0;
    
    /**
     * Draw text centered within a rectangle.
     */
    virtual void drawTextCentered(const std::string& text, const NUIRect& rect, float fontSize, const NUIColor& color) = 0;
    
    /**
     * Measure text dimensions.
     */
    virtual NUISize measureText(const std::string& text, float fontSize) = 0;
    
    /**
     * Calculate baseline-aligned Y position for vertically centered text.
     * This accounts for the MSDF renderer's baseline coordinate system.
     * 
     * @param rect The rectangle to center text within
     * @param fontSize The font size
     * @return Baseline Y coordinate for centered text
     */
    float calculateTextBaselineY(const NUIRect& rect, float fontSize) const {
        // Center vertically then offset to baseline (compensate for font ascent)
        return rect.y + (rect.height - fontSize) * 0.5f + fontSize * 0.8f;
    }
    
    /**
     * Calculate baseline-aligned Y position for vertically centered text using measured size.
     * This accounts for the MSDF renderer's top-left coordinate system.
     * 
     * @param rect The rectangle to center text within
     * @param fontSize The font size being used
     * @return Top-left Y coordinate for centered text
     */
    float calculateTextY(const NUIRect& rect, float fontSize) const {
        // Center vertically using font size (top-left Y positioning)
        return rect.y + (rect.height - fontSize) * 0.5f;
    }
    
    // ========================================================================
    // Texture/Image Drawing
    // ========================================================================
    
    /**
     * Draw a texture/image.
     */
    virtual void drawTexture(uint32_t textureId, const NUIRect& destRect, const NUIRect& sourceRect) = 0;
    
    /**
     * Draw a texture from raw RGBA pixel data.
     * 
     * This method creates a temporary OpenGL texture from the provided RGBA data,
     * renders it to the specified bounds, and cleans up the texture immediately.
     * It's designed for one-time rendering of rasterized content (e.g., SVG icons).
     * 
     * @param bounds The target rectangle where the texture should be rendered
     * @param rgba Pointer to RGBA pixel data (4 bytes per pixel: R, G, B, A)
     * @param width Width of the source image in pixels
     * @param height Height of the source image in pixels
     * 
     * @note The texture is created and destroyed within this call. For frequently
     *       rendered textures, consider using createTexture() + drawTexture() instead.
     * @note The current transform and opacity state will be applied to the rendering.
     */
    virtual void drawTexture(const NUIRect& bounds, const unsigned char* rgba, 
                            int width, int height) = 0;
    
    /**
     * Load a texture from file.
     */
    virtual uint32_t loadTexture(const std::string& filepath) = 0;
    
    /**
     * Create a texture from raw RGBA data.
     */
    virtual uint32_t createTexture(const uint8_t* data, int width, int height) = 0;
    
    /**
     * Delete a texture.
     */
    virtual void deleteTexture(uint32_t textureId) = 0;
    
    /**
     * Optional render-to-texture begin.
     * Implementations may return a texture id that will contain the rendered content
     * after renderToTextureEnd() is called. Default implementation returns 0 (not supported).
     */
    virtual uint32_t renderToTextureBegin(int width, int height) { (void)width; (void)height; return 0; }

    /**
     * Optional render-to-texture end.
     * Returns the texture id (if any) that contains the rendered content.
     */
    virtual uint32_t renderToTextureEnd() { return 0; }
    
    // ========================================================================
    // Batching
    // ========================================================================
    
    /**
     * Begin batching draw calls (for performance).
     */
    virtual void beginBatch() = 0;
    
    /**
     * End batching and flush all queued draw calls.
     */
    virtual void endBatch() = 0;
    
    /**
     * Flush all pending draw calls immediately.
     */
    virtual void flush() = 0;
    
    // ========================================================================
    // Performance Optimizations
    // ========================================================================
    
    /**
     * Enable/disable render batching.
     */
    virtual void setBatchingEnabled(bool enabled) = 0;
    
    /**
     * Enable/disable dirty region tracking.
     */
    virtual void setDirtyRegionTrackingEnabled(bool enabled) = 0;
    
    /**
     * Enable/disable render caching.
     */
    virtual void setCachingEnabled(bool enabled) = 0;
    
    /**
     * Get optimization stats.
     */
    virtual void getOptimizationStats(size_t& batchedQuads, size_t& dirtyRegions, 
                                     size_t& cachedWidgets, size_t& cacheMemoryBytes) = 0;
    
    /**
     * Access to dirty region manager.
     */
    virtual NUIDirtyRegionManager* getDirtyRegionManager() = 0;
    
    /**
     * Access to render cache.
     */
    virtual NUIRenderCache* getRenderCache() = 0;
    
    // ========================================================================
    // Info
    // ========================================================================
    
    virtual int getWidth() const = 0;
    virtual int getHeight() const = 0;
    virtual const char* getBackendName() const = 0;
};

/**
 * Factory function to create a renderer for the current platform.
 */
std::unique_ptr<NUIRenderer> createRenderer();

} // namespace NomadUI
