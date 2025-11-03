// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUITypes.h"
#include "NUIFont.h"
#include <memory>
#include <vector>

namespace NomadUI {

/**
 * Text renderer - handles drawing text using fonts.
 * 
 * Features:
 * - Renders text using cached glyph textures
 * - Supports alignment (left, center, right)
 * - Multi-line text with line wrapping
 * - Color and opacity control
 * - Optimized batching
 */
class NUITextRenderer {
public:
    enum class Alignment {
        Left,
        Center,
        Right
    };
    
    enum class VerticalAlignment {
        Top,
        Middle,
        Bottom
    };
    
    NUITextRenderer();
    ~NUITextRenderer();
    
    // ========================================================================
    // Initialization
    // ========================================================================
    
    /**
     * Initialize the text renderer with OpenGL context.
     */
    bool initialize();
    
    /**
     * Shutdown and cleanup resources.
     */
    void shutdown();
    
    // ========================================================================
    // Rendering
    // ========================================================================
    
    /**
     * Draw text at a position.
     * @param text Text string to draw
     * @param font Font to use
     * @param position Top-left position
     * @param color Text color
     */
    void drawText(
        const std::string& text,
        std::shared_ptr<NUIFont> font,
        const NUIPoint& position,
        const NUIColor& color
    );
    
    /**
     * Draw text centered in a rectangle.
     * @param text Text string to draw
     * @param font Font to use
     * @param rect Bounding rectangle
     * @param color Text color
     * @param hAlign Horizontal alignment
     * @param vAlign Vertical alignment
     */
    void drawTextAligned(
        const std::string& text,
        std::shared_ptr<NUIFont> font,
        const NUIRect& rect,
        const NUIColor& color,
        Alignment hAlign = Alignment::Center,
        VerticalAlignment vAlign = VerticalAlignment::Middle
    );
    
    /**
     * Draw multi-line text with word wrapping.
     * @param text Text string to draw (can contain \n for line breaks)
     * @param font Font to use
     * @param rect Bounding rectangle
     * @param color Text color
     * @param lineSpacing Extra spacing between lines (multiplier, 1.0 = normal)
     */
    void drawTextMultiline(
        const std::string& text,
        std::shared_ptr<NUIFont> font,
        const NUIRect& rect,
        const NUIColor& color,
        float lineSpacing = 1.2f
    );
    
    /**
     * Draw text with shadow/outline effect.
     */
    void drawTextWithShadow(
        const std::string& text,
        std::shared_ptr<NUIFont> font,
        const NUIPoint& position,
        const NUIColor& color,
        const NUIColor& shadowColor,
        float shadowOffsetX,
        float shadowOffsetY
    );
    
    // ========================================================================
    // Measurement
    // ========================================================================
    
    /**
     * Measure text size.
     * @param text Text to measure
     * @param font Font to use
     * @return Size of the rendered text
     */
    NUISize measureText(const std::string& text, std::shared_ptr<NUIFont> font);
    
    /**
     * Measure multi-line text.
     */
    NUISize measureTextMultiline(
        const std::string& text,
        std::shared_ptr<NUIFont> font,
        float maxWidth,
        float lineSpacing = 1.2f
    );
    
    // ========================================================================
    // Batching
    // ========================================================================
    
    /**
     * Begin text batch (for rendering multiple text strings efficiently).
     */
    void beginBatch(const float* projectionMatrix);
    
    /**
     * End text batch and flush to GPU.
     */
    void endBatch();
    
    /**
     * Flush pending text to GPU.
     */
    void flush();
    
    // ========================================================================
    // State
    // ========================================================================
    
    void setOpacity(float opacity) { opacity_ = opacity; }
    float getOpacity() const { return opacity_; }
    
private:
    struct TextVertex {
        float x, y;       // Position
        float u, v;       // Texture coords
        float r, g, b, a; // Color
    };
    
    struct TextBatch {
        uint32_t textureID;
        std::vector<TextVertex> vertices;
        std::vector<uint32_t> indices;
    };
    
    // OpenGL resources
    uint32_t vao_;
    uint32_t vbo_;
    uint32_t ebo_;
    uint32_t shaderProgram_;
    int projectionLoc_;
    int textureLoc_;
    
    // Batching
    std::map<uint32_t, TextBatch> batches_;  // Keyed by texture ID
    bool batching_;
    float opacity_;
    
    // Shader sources
    static const char* vertexShaderSource_;
    static const char* fragmentShaderSource_;
    
    // Helper methods
    bool loadShaders();
    void createBuffers();
    uint32_t compileShader(const char* source, uint32_t type);
    uint32_t linkProgram(uint32_t vertexShader, uint32_t fragmentShader);
    
    void addGlyph(
        uint32_t textureID,
        float x, float y,
        float width, float height,
        float u0, float v0,
        float u1, float v1,
        const NUIColor& color
    );
    
    std::vector<std::string> splitLines(const std::string& text);
    std::vector<std::string> wrapText(
        const std::string& text,
        std::shared_ptr<NUIFont> font,
        float maxWidth
    );
};

} // namespace NomadUI

