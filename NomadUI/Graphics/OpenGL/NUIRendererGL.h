// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../NUIRenderer.h"
#include "../NUIFont.h"
#include "../NUITextRendererSDF.h"
#include "../NUITextRenderer.h"
#include "../NUITextRendererGDI.h"
#include "../NUITextRendererModern.h"
#include "NUIRenderBatch.h"
#include "NUIDirtyRegion.h"
#include "NUIRenderCache.h"
#include <vector>
#include <tuple>
#include <unordered_map>
#include <memory>

// Forward declarations
struct FT_Bitmap_;
typedef struct FT_Bitmap_ FT_Bitmap;

namespace NomadUI {

/**
 * OpenGL 3.3+ renderer implementation for Nomad UI.
 * 
 * Features:
 * - Shader-based rendering
 * - Batched draw calls
 * - SDF text rendering
 * - Post-processing effects
 */
class NUIRendererGL : public NUIRenderer {
public:
    NUIRendererGL();
    ~NUIRendererGL() override;
    
    // ========================================================================
    // Lifecycle
    // ========================================================================
    
    bool initialize(int width, int height) override;
    void shutdown() override;
    void resize(int width, int height) override;
    
    // ========================================================================
    // Frame Management
    // ========================================================================
    
    void beginFrame() override;
    void endFrame() override;
    void clear(const NUIColor& color) override;
    
    // ========================================================================
    // State Management
    // ========================================================================
    
    void pushTransform(float tx, float ty, float rotation = 0.0f, float scale = 1.0f) override;
    void popTransform() override;
    void setClipRect(const NUIRect& rect) override;
    void clearClipRect() override;
    void setOpacity(float opacity) override;
    
    // ========================================================================
    // Primitive Drawing
    // ========================================================================
    
    void fillRect(const NUIRect& rect, const NUIColor& color) override;
    void fillRoundedRect(const NUIRect& rect, float radius, const NUIColor& color) override;
    void strokeRect(const NUIRect& rect, float thickness, const NUIColor& color) override;
    void strokeRoundedRect(const NUIRect& rect, float radius, float thickness, const NUIColor& color) override;
    void fillCircle(const NUIPoint& center, float radius, const NUIColor& color) override;
    void strokeCircle(const NUIPoint& center, float radius, float thickness, const NUIColor& color) override;
    void drawLine(const NUIPoint& start, const NUIPoint& end, float thickness, const NUIColor& color) override;
    void drawPolyline(const NUIPoint* points, int count, float thickness, const NUIColor& color) override;
    void fillWaveform(const NUIPoint* topPoints, const NUIPoint* bottomPoints, int count, const NUIColor& color) override;
    
    // ========================================================================
    // Gradient Drawing
    // ========================================================================
    
    void fillRectGradient(const NUIRect& rect, const NUIColor& colorStart, const NUIColor& colorEnd, bool vertical = true) override;
    void fillCircleGradient(const NUIPoint& center, float radius, const NUIColor& colorInner, const NUIColor& colorOuter) override;
    
    // ========================================================================
    // Effects
    // ========================================================================
    
    void drawGlow(const NUIRect& rect, float radius, float intensity, const NUIColor& color) override;
    void drawShadow(const NUIRect& rect, float offsetX, float offsetY, float blur, const NUIColor& color) override;
    
    // ========================================================================
    // Text Rendering
    // ========================================================================
    
    void drawText(const std::string& text, const NUIPoint& position, float fontSize, const NUIColor& color) override;
    void drawTextCentered(const std::string& text, const NUIRect& rect, float fontSize, const NUIColor& color) override;
    NUISize measureText(const std::string& text, float fontSize) override;
    
    // ========================================================================
    // Texture/Image Drawing
    // ========================================================================
    
    void drawTexture(uint32_t textureId, const NUIRect& destRect, const NUIRect& sourceRect) override;
    // Helper to draw a texture flipped vertically (used when sampling FBOs rendered in GL origin space)
    void drawTextureFlippedV(uint32_t textureId, const NUIRect& destRect, const NUIRect& sourceRect);
    void drawTexture(const NUIRect& bounds, const unsigned char* rgba, 
                    int width, int height) override;
    uint32_t loadTexture(const std::string& filepath) override;
    uint32_t createTexture(const uint8_t* data, int width, int height) override;
    void deleteTexture(uint32_t textureId) override;
    uint32_t getGLTextureId(uint32_t textureId) const;
    
    // Render-to-texture helpers (FBO)
    uint32_t renderToTextureBegin(int width, int height);
    uint32_t renderToTextureEnd();

    // Temporary offscreen rendering (adjusts projection to target size)
    void beginOffscreen(int width, int height);
    void endOffscreen();
    
    // ========================================================================
    // Batching
    // ========================================================================
    
    void beginBatch() override;
    void endBatch() override;
    void flush() override;
    
    // ========================================================================
    // Performance Optimizations
    // ========================================================================
    
    void setBatchingEnabled(bool enabled) override;
    void setDirtyRegionTrackingEnabled(bool enabled) override;
    void setCachingEnabled(bool enabled) override;
    void getOptimizationStats(size_t& batchedQuads, size_t& dirtyRegions, 
                             size_t& cachedWidgets, size_t& cacheMemoryBytes) override;
    NUIDirtyRegionManager* getDirtyRegionManager() override { return &dirtyRegionManager_; }
    NUIRenderCache* getRenderCache() override { return &renderCache_; }
    
    // Performance stats
    uint32_t getDrawCallCount() const { return drawCallCount_; }
    void resetDrawCallCount() { drawCallCount_ = 0; }
    
    // ========================================================================
    // Info
    // ========================================================================
    
    int getWidth() const override { return width_; }
    int getHeight() const override { return height_; }
    const char* getBackendName() const override { return "OpenGL 3.3+"; }

    // Query renderer state
    bool isScissorEnabled() const { return scissorEnabled_; }
    
private:
    // Vertex structure for batching
    struct Vertex {
        float x, y;
        float u, v;
        float r, g, b, a;
    };
    
    // Shader program
    struct ShaderProgram {
        uint32_t id = 0;
        int32_t projectionLoc = -1;
        int32_t transformLoc = -1;
        int32_t opacityLoc = -1;
        int32_t primitiveTypeLoc = -1;
        int32_t radiusLoc = -1;
        int32_t rectWidthLoc = -1;
        int32_t rectHeightLoc = -1;
        int32_t quadWidthLoc = -1;
        int32_t quadHeightLoc = -1;
        int32_t textureLoc = -1;
        int32_t useTextureLoc = -1;
        int32_t blurLoc = -1;
    };
    
    // Transform stack
    struct Transform {
        float tx = 0.0f;
        float ty = 0.0f;
        float rotation = 0.0f;
        float scale = 1.0f;
    };
    
    // Initialize OpenGL resources
    bool initializeGL();
    bool loadShaders();
    void createBuffers();
    void initializeTextRendering();
    bool loadFont(const std::string& fontPath);
    void renderTextWithFont(const std::string& text, const NUIPoint& position, float fontSize, const NUIColor& color);
    void drawCharacterPixels(char c, float x, float y, float width, float height, const NUIColor& color, float scale);
    void drawCleanCharacter(char c, float x, float y, float width, float height, const NUIColor& color);
    void drawCharacter(char c, float x, float y, float width, float height, const NUIColor& color);
    
    // High-quality text rendering helpers
    float getDPIScale();

    // REMOVED: renderCharacterImproved (replaced by atlas rendering)
    
    // Shader helpers
    uint32_t compileShader(const char* source, uint32_t type);
    uint32_t linkProgram(uint32_t vertexShader, uint32_t fragmentShader);
    
    // Drawing helpers
    void ensureBasicPrimitive();
    void addVertex(float x, float y, float u, float v, const NUIColor& color);
    void addQuad(const NUIRect& rect, const NUIColor& color);
    void applyTransform(float& x, float& y);
    void updateProjectionMatrix();
    
    // State
    int width_ = 0;
    int height_ = 0;
    float globalOpacity_ = 1.0f;
    bool batching_ = false;
    uint32_t drawCallCount_ = 0;  // Draw call tracking
    bool scissorEnabled_ = false;
    
    // OpenGL objects
    uint32_t vao_ = 0;
    uint32_t vbo_ = 0;
    uint32_t ebo_ = 0;
    
    // Shaders
    ShaderProgram primitiveShader_;
    
    // Vertex batch
    std::vector<Vertex> vertices_;
    std::vector<uint32_t> indices_;
    
    // Transform stack
    std::vector<Transform> transformStack_;
    
    // Textures
    struct TextureData { uint32_t glId = 0; int width = 0; int height = 0; };
    std::unordered_map<uint32_t, TextureData> textures_;
    uint32_t nextTextureId_ = 1;

    // Render-to-texture (FBO) support
    uint32_t fbo_ = 0;
    int fboPrevViewport_[4] = {0,0,0,0};
    int fboWidth_ = 0;
    int fboHeight_ = 0;
    bool renderingToTexture_ = false;

    uint32_t lastRenderTextureId_ = 0;
    
    // Batching state
    uint32_t currentTextureId_ = 0; // 0 = no texture (flat color)
    int currentPrimitiveType_ = 0;
    float currentRadius_ = 0.0f;
    float currentBlur_ = 0.0f;
    NUISize currentSize_ = {0.0f, 0.0f};
    NUISize currentQuadSize_ = {0.0f, 0.0f};
    
    // Optimization systems
    NUIBatchManager batchManager_;
    NUIDirtyRegionManager dirtyRegionManager_;
    NUIRenderCache renderCache_;
    uint64_t frameCounter_ = 0;
    
    // Text rendering support
    std::string defaultFontPath_;
    std::unique_ptr<NUITextRendererSDF> sdfRenderer_;
    bool useSDFText_{false};
    bool triedSDFInit_{false};
    
    // FreeType text rendering
    struct FontData {
        uint32_t textureId; // Now refers to the atlas texture
        int width, height;
        int bearingX, bearingY;
        int advance;
        // Atlas UV coordinates
        float u0, v0, u1, v1;
    };
    std::unordered_map<char, FontData> fontCache_;
    bool fontInitialized_;
    uint32_t fontAtlasTextureId_ = 0; // The single texture atlas
    int fontAtlasWidth_ = 2048;
    int fontAtlasHeight_ = 2048;
    int fontAtlasX_ = 0;
    int fontAtlasY_ = 0;
    int fontAtlasRowHeight_ = 0;

    FT_Library ftLibrary_;
    FT_Face ftFace_;
    
    // Projection matrix (orthographic)
    float projectionMatrix_[16];

    // Offscreen state backup
    float projectionBackup_[16];
    int widthBackup_ = 0;
    int heightBackup_ = 0;
};

} // namespace NomadUI
