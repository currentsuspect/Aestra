// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUIRendererGL.h"
#include <cstring>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_LCD_FILTER_H
// Profiler include for recording draw calls/triangles
#include "../../../NomadCore/include/NomadProfiler.h"

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #define NOCOMM
    #include <Windows.h>
#endif

// GLAD must be included after Windows headers to avoid macro conflicts
#include "../../External/glad/include/glad/glad.h"

// Suppress APIENTRY redefinition warning - both define the same value
#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable: 4005)
// Windows.h redefines APIENTRY but it's the same value, so we can ignore the warning
#pragma warning(pop)
#endif

#ifndef GL_VIEWPORT
#define GL_VIEWPORT 0x0BA2
#endif

namespace NomadUI {

// ============================================================================
// UTF-8 Decoding Helper
// ============================================================================

// Decode one UTF-8 codepoint from string, advancing the index
// Returns Unicode codepoint (U+XXXX), or 0 if invalid/end
static uint32_t decodeUTF8(const std::string& text, size_t& index) {
    if (index >= text.length()) return 0;
    
    unsigned char c = static_cast<unsigned char>(text[index++]);
    
    // 1-byte (ASCII): 0xxxxxxx
    if ((c & 0x80) == 0) {
        return c;
    }
    
    // 2-byte: 110xxxxx 10xxxxxx
    if ((c & 0xE0) == 0xC0) {
        if (index >= text.length()) return 0;
        uint32_t c1 = static_cast<unsigned char>(text[index++]);
        if ((c1 & 0xC0) != 0x80) return 0;
        return ((c & 0x1F) << 6) | (c1 & 0x3F);
    }
    
    // 3-byte: 1110xxxx 10xxxxxx 10xxxxxx  
    if ((c & 0xF0) == 0xE0) {
        if (index + 1 >= text.length()) return 0;
        uint32_t c1 = static_cast<unsigned char>(text[index++]);
        uint32_t c2 = static_cast<unsigned char>(text[index++]);
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return 0;
        return ((c & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
    }
    
    // 4-byte: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    if ((c & 0xF8) == 0xF0) {
        if (index + 2 >= text.length()) return 0;
        uint32_t c1 = static_cast<unsigned char>(text[index++]);
        uint32_t c2 = static_cast<unsigned char>(text[index++]);
        uint32_t c3 = static_cast<unsigned char>(text[index++]);
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) return 0;
        return ((c & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
    }
    
    return 0; // Invalid UTF-8
}

// ============================================================================
// Shader Sources (embedded)
// ============================================================================

static const char* vertexShaderSource = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
// Batching Attributes
layout(location = 3) in vec2 aRectSize;
layout(location = 4) in vec2 aQuadSize;
layout(location = 5) in float aRadius;
layout(location = 6) in float aBlur;
layout(location = 7) in float aStrokeWidth;
layout(location = 8) in float aPrimitiveType;

out vec2 vTexCoord;
out vec4 vColor;
out vec2 vRectSize;
out vec2 vQuadSize;
out float vRadius;
out float vBlur;
out float vStrokeWidth;
out float vPrimitiveType;

uniform mat4 uProjection;

void main() {
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
    vTexCoord = aTexCoord;
    vColor = aColor;
    vRectSize = aRectSize;
    vQuadSize = aQuadSize;
    vRadius = aRadius;
    vBlur = aBlur;
    vStrokeWidth = aStrokeWidth;
    vPrimitiveType = aPrimitiveType;
}
)";

static const char* fragmentShaderSource = R"(
#version 330 core
in vec2 vTexCoord;
in vec4 vColor;
in vec2 vRectSize;
in vec2 vQuadSize;
in float vRadius;
in float vBlur;
in float vStrokeWidth;
in float vPrimitiveType;

out vec4 FragColor;

// uniform int uPrimitiveType; // REMOVED: Now passed as vertex attribute
uniform sampler2D uTexture;
uniform bool uUseTexture;

uniform float uSmoothness;

float sdRoundedRect(vec2 p, vec2 size, float radius) {
    vec2 d = abs(p) - size + radius;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - radius;
}

void main() {
    vec4 color = vColor;
    int primitiveID = int(floor(vPrimitiveType + 0.5));
    
    // Apply texture if enabled (Only for Image=0, SDFText=2, BitmapText=4)
    if (uUseTexture && (primitiveID == 0 || primitiveID == 2 || primitiveID == 4)) {
        vec4 texColor = texture(uTexture, vTexCoord);
        if (primitiveID == 2) {
             // Standard SDF rendering
             float dist = texColor.r;
             
             // Screen-space derivative for sharp edges regardless of scale/zoom
             // ddist is large for small text (dUV/dPixel is high), small for large text.
             float ddist = fwidth(dist);
             
             // Softness factor: 0.7 gives a nice crisp edge without aliasing
             float edgeWidth = ddist * 0.7;
             
             // Adaptive Weighting:
             // Thicken small text (high ddist) to prevent it looking spindly.
             // ranges from 0.5 (large text) to ~0.42 (tiny text)
             // smoothstep(0.1, 0.5, ddist) maps ddist range 0.1-0.5 to 0.0-1.0
             float center = 0.5 - smoothstep(0.1, 0.5, ddist) * 0.08; 

             float alpha = smoothstep(center - edgeWidth, center + edgeWidth, dist);
             
             // Standard alpha blending
             color.a *= alpha;
        } else if (primitiveID == 4) {
               // Bitmap text rendering (coverage stored in alpha)
               float coverageAlpha = texColor.a;
               // We use standard (non-premultiplied) alpha blending: SRC_ALPHA, ONE_MINUS_SRC_ALPHA.
               // Only alpha should be modulated by coverage; multiplying RGB as well effectively squares
               // coverage in the blend equation and makes small text look overly faded.
               color.a *= coverageAlpha;
        } else {
             // Regular textured primitive
             color *= texColor;
        }
    }
    
    if (primitiveID == 1) {
        // Filled rounded rectangle / Shadow
        vec2 pos = (vTexCoord - 0.5) * vQuadSize;
        float dist = sdRoundedRect(pos, vRectSize * 0.5, vRadius);
        float alpha = 1.0 - smoothstep(-vBlur, vBlur, dist);
        color.a *= alpha;
    }
    
    if (primitiveID == 3) {
        // Stroked rounded rectangle using SDF
        vec2 pos = (vTexCoord - 0.5) * vQuadSize;
        float dist = sdRoundedRect(pos, vRectSize * 0.5, vRadius);
        // Create stroke by checking if distance is near the edge
        float halfStroke = vStrokeWidth * 0.5;
        float outerAlpha = 1.0 - smoothstep(-vBlur, vBlur, dist - halfStroke);
        float innerAlpha = 1.0 - smoothstep(-vBlur, vBlur, dist + halfStroke);
        float alpha = outerAlpha - innerAlpha;
        color.a *= alpha;
    }
    
    FragColor = color;
}

)";

// ============================================================================
// Constructor / Destructor
// ============================================================================

NUIRendererGL::NUIRendererGL() {
    renderCache_.setRenderer(this);
    sdfRenderer_ = std::make_unique<NUITextRendererSDF>();
}

NUIRendererGL::~NUIRendererGL() {
    shutdown();
}

// ============================================================================
// Lifecycle
// ============================================================================

bool NUIRendererGL::initialize(int width, int height) {
    width_ = width;
    height_ = height;
    
    if (!initializeGL()) {
        return false;
    }
    
    if (!loadShaders()) {
        return false;
    }
    
    createBuffers();
    updateProjectionMatrix();
    
    // Text rendering will be initialized with FreeType below
    
    // Initialize FreeType
    fontInitialized_ = false;
    if (FT_Init_FreeType(&ftLibrary_) != 0) {
        std::cerr << "ERROR: Could not init FreeType Library" << std::endl;
        return false;
    }

    // NOTE: Nomad UI currently tints text in the shader and composites into an sRGB-authored UI.
    // Proper LCD/subpixel text requires display subpixel order/orientation handling and a
    // subpixel-aware blending pipeline. Until that's implemented, default to grayscale coverage.
    fontUseLCD_ = false;
    
        // Try to load the best font for Nomad
        std::vector<std::string> fontPaths = {
            // Bundled UI fonts (drop the TTFs into these paths)
            // Bundled UI fonts (drop the TTFs into these paths)
            // Try distinct paths for dev/build environments
            "NomadAssets/fonts/Geist/Geist-Regular.ttf",
            "../NomadAssets/fonts/Geist/Geist-Regular.ttf", 
            "../../NomadAssets/fonts/Geist/Geist-Regular.ttf",
            "../../../NomadAssets/fonts/Geist/Geist-Regular.ttf",

            "NomadAssets/fonts/Manrope/Manrope-Regular.ttf",
            "../../../NomadAssets/fonts/Manrope/Manrope-Regular.ttf",

            // System fallbacks (Windows)
            "C:/Windows/Fonts/segoeui.ttf",      // VS Code look
            "C:/Windows/Fonts/segoeuisl.ttf",    // Segoe UI Semilight
            "C:/Windows/Fonts/calibri.ttf",      // Modern, clear
            "C:/Windows/Fonts/arial.ttf",        // Classic fallback
            "C:/Windows/Fonts/consola.ttf",      // Monospace fallback
            "C:/Windows/Fonts/tahoma.ttf",       // Good for small text
            "C:/Windows/Fonts/verdana.ttf"       // Designed for screen clarity
        };
        
        bool fontLoaded = false;
        for (const auto& fontPath : fontPaths) {
            if (loadFont(fontPath)) {
                fontLoaded = true;
                break;
            }
        }
        
        if (!fontLoaded) {
            std::cerr << "WARNING: Could not load any font, using fallback" << std::endl;
            useSDFText_ = false;
        } else if (sdfRenderer_) {
            useSDFText_ = sdfRenderer_->initialize(defaultFontPath_, 64.0f);
            if (!useSDFText_) {
                std::cerr << "MSDF init failed, falling back to bitmap text\n";
            } else {
                std::cout << "MSDF text renderer enabled (atlas @64px)\n";
                useSDFText_ = true;
            }
        }
    
    // Set initial state
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE); // Ensure we don't write to depth buffer in 2D mode
    glDisable(GL_CULL_FACE);
    // Leave framebuffer sRGB disabled; palette is authored in sRGB and shaders output in sRGB
    
    // Note: MSAA support will be added in Phase 2
    // For now, we rely on thin lines (1.0px) and proper blending for good quality
    // Phase 2 will implement MSAA via framebuffer or WGL extensions
    // See docs/MSAA_IMPLEMENTATION_GUIDE.md for details
    
    return true;
}

void NUIRendererGL::shutdown() {
    if (sdfRenderer_) {
        sdfRenderer_->shutdown();
    }
    
    // Cleanup FreeType
    if (fontInitialized_) {
        // Clean up atlas texture
        if (fontAtlasTextureId_ != 0) {
            glDeleteTextures(1, &fontAtlasTextureId_);
            fontAtlasTextureId_ = 0;
        }
        if (fontAtlasTextureIdMedium_ != 0) {
            glDeleteTextures(1, &fontAtlasTextureIdMedium_);
            fontAtlasTextureIdMedium_ = 0;
        }
        if (fontAtlasTextureIdSmall_ != 0) {
            glDeleteTextures(1, &fontAtlasTextureIdSmall_);
            fontAtlasTextureIdSmall_ = 0;
        }
        fontCache_.clear();
        fontCacheMedium_.clear();
        fontCacheSmall_.clear();
        
        FT_Done_Face(ftFace_);
        FT_Done_FreeType(ftLibrary_);
        fontInitialized_ = false;
        fontHasKerning_ = false;
        fontUseLCD_ = false;
        fontAscent_ = fontDescent_ = fontLineHeight_ = 0.0f;
        fontAscentMedium_ = fontDescentMedium_ = fontLineHeightMedium_ = 0.0f;
        fontAscentSmall_ = fontDescentSmall_ = fontLineHeightSmall_ = 0.0f;
    }
    
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    
    if (vbo_) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    
    if (ebo_) {
        glDeleteBuffers(1, &ebo_);
        ebo_ = 0;
    }
    
    if (primitiveShader_.id) {
        glDeleteProgram(primitiveShader_.id);
        primitiveShader_.id = 0;
    }
    
    // Text rendering cleanup (no font objects to clean up)
}

void NUIRendererGL::resize(int width, int height) {
    width_ = width;
    height_ = height;
    updateProjectionMatrix();
    
    // Invalidate all cached FBOs when the surface size changes
    // to avoid sampling from stale textures after minimize/restore.
    renderCache_.clearAll();
    
    // MSDF text renderer viewport will be updated externally
    
    glViewport(0, 0, width, height);
}

// ============================================================================
// Frame Management
// ============================================================================

void NUIRendererGL::beginFrame() {
    // Enforce 2D rendering state at the start of every frame
    // This protects against state pollution from other renderers (e.g. plugins, 3D views)
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    vertices_.clear();
    indices_.clear();
    frameCounter_++;
    drawCallCount_ = 0;  // Reset draw call counter each frame
    // Reset primitive state so the first non-rounded draw in a frame does not inherit rounded settings
    currentPrimitiveType_ = 0;
    currentRadius_ = 0.0f;
    currentBlur_ = 0.0f;
    currentSize_ = {0.0f, 0.0f};
    currentQuadSize_ = {0.0f, 0.0f};
    renderCache_.setCurrentFrame(frameCounter_);
    
    // OPTIMIZED: Don't mark all dirty - let widgets mark their own dirty regions
    // This enables true incremental rendering where only changed areas are redrawn
    // dirtyRegionManager_.markAllDirty(NUISize(static_cast<float>(width_), static_cast<float>(height_)));
    
    // Clear batch manager
    batchManager_.clearAll();
}

void NUIRendererGL::endFrame() {
    flush();
    
    // Clear dirty regions for next frame
    dirtyRegionManager_.clear();
    
    // Cleanup old caches every 60 frames
    if (frameCounter_ % 60 == 0) {
        renderCache_.cleanup(frameCounter_, 300);
    }
}

void NUIRendererGL::clear(const NUIColor& color) {
    glClearColor(color.r, color.g, color.b, color.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

// ============================================================================
// State Management
// ============================================================================

void NUIRendererGL::pushTransform(float tx, float ty, float rotation, float scale) {
    Transform t;
    t.tx = tx;
    t.ty = ty;
    t.rotation = rotation;
    t.scale = scale;
    transformStack_.push_back(t);
}

void NUIRendererGL::popTransform() {
    if (!transformStack_.empty()) {
        transformStack_.pop_back();
    }
}

void NUIRendererGL::setClipRect(const NUIRect& rect) {
    flush(); // Must flush before changing scissor state!
    glEnable(GL_SCISSOR_TEST);
    scissorEnabled_ = true;
    
    // Transform the rect to global screen coordinates (pixels)
    // Points: Top-Left and Bottom-Right
    float x1 = rect.x;
    float y1 = rect.y;
    float x2 = rect.right();
    float y2 = rect.bottom();
    
    // Apply transform stack manually
    if (!transformStack_.empty()) {
        struct Transform { float tx, ty, rot, scale; };
        for (const auto& t : transformStack_) {
            // Apply scale
            x1 *= t.scale;
            y1 *= t.scale;
            x2 *= t.scale;
            y2 *= t.scale;
            
            // Apply translation
            x1 += t.tx;
            y1 += t.ty;
            x2 += t.tx;
            y2 += t.ty;
        }
    }
    
    // Normalize if scale was negative (unlikely but safe)
    if (x1 > x2) std::swap(x1, x2);
    if (y1 > y2) std::swap(y1, y2);
    
    // Correct rounding to prevent shrinking (floor min, ceil max)
    int glX = static_cast<int>(std::floor(x1));
    int glRight = static_cast<int>(std::ceil(x2));
    int glWidth = std::max(0, glRight - glX);
    
    // Convert to GL coords (bottom-up)
    // Range in UI (y-down): [y1, y2]
    // Range in GL (y-up):   [height_ - y2, height_ - y1]
    
    float bottomGL = static_cast<float>(height_) - y2;
    float topGL = static_cast<float>(height_) - y1;
    
    int glY = static_cast<int>(std::floor(bottomGL));
    int glTop = static_cast<int>(std::ceil(topGL));
    int glHeight = std::max(0, glTop - glY);

    glScissor(glX, glY, glWidth, glHeight);
    
    // Track renderer-side scissor state so other systems can query it
    scissorEnabled_ = true;
}


void NUIRendererGL::clearClipRect() {
    flush(); // Must flush before changing scissor state to prevent spilling/ruined layout
    glDisable(GL_SCISSOR_TEST);
    scissorEnabled_ = false;
}

void NUIRendererGL::setOpacity(float opacity) {
    globalOpacity_ = opacity;
}

// ============================================================================
// Primitive Drawing
// ============================================================================

void NUIRendererGL::fillRect(const NUIRect& rect, const NUIColor& color) {
    ensureBasicPrimitive();
    addQuad(rect, color);
}

void NUIRendererGL::fillRoundedRect(const NUIRect& rect, float radius, const NUIColor& color) {
    // Batching enabled! We pass size and radius as vertex attributes.
    // Dimensions for both Rect and Quad are the same for standard rounded rects.
    float w = rect.width;
    float h = rect.height;
    
    // Blur is 1.0f for standard anti-aliasing
    // Snap to integers for sharp filling without subpixel shifts
    NomadUI::NUIRect snapped = rect;
    snapped.x = std::floor(rect.x);
    snapped.y = std::floor(rect.y);
    snapped.width = std::round(rect.width);
    snapped.height = std::round(rect.height);

    // Standard behavior: Clamp radius to fit
    float safeRadius = std::min(radius, std::min(snapped.width * 0.5f, snapped.height * 0.5f));
    addQuad(snapped, color, snapped.width, snapped.height, snapped.width, snapped.height, safeRadius, 1.0f, 0.0f, 1.0f);
}

void NUIRendererGL::strokeRect(const NUIRect& rect, float thickness, const NUIColor& color) {
    // Draw 4 lines
    // Inset the Right and Bottom lines by 1.0f so they stay INSIDE the clip rect (which is integer aligned)
    // This prevents the "aggressive cutoff" where the stroke width causes it to cross the clip boundary.
    drawLine(NUIPoint({rect.x, rect.y}), NUIPoint({rect.right() - 1.0f, rect.y}), thickness, color);
    drawLine(NUIPoint({rect.right() - 1.0f, rect.y}), NUIPoint({rect.right() - 1.0f, rect.bottom() - 1.0f}), thickness, color);
    drawLine(NUIPoint({rect.right() - 1.0f, rect.bottom() - 1.0f}), NUIPoint({rect.x, rect.bottom() - 1.0f}), thickness, color);
    drawLine(NUIPoint({rect.x, rect.bottom() - 1.0f}), NUIPoint({rect.x, rect.y}), thickness, color);
}

void NUIRendererGL::strokeRoundedRect(const NUIRect& rect, float radius, float thickness, const NUIColor& color) {
    float w = rect.width;
    float h = rect.height;
    
    // Blur 1.0f for AA
    // Snap to pixel grid + 0.5f, BUT reduce width/height by 1.0f to keep stroke INSIDE integers
    NomadUI::NUIRect snapped = rect;
    snapped.x = std::floor(rect.x) + 0.5f;
    snapped.y = std::floor(rect.y) + 0.5f;
    snapped.width = std::round(rect.width) - 1.0f;
    snapped.height = std::round(rect.height) - 1.0f;

    // Standard behavior: Clamp radius to fit
    float safeRadius = std::min(radius, std::min(snapped.width * 0.5f, snapped.height * 0.5f));
    addQuad(snapped, color, snapped.width, snapped.height, snapped.width, snapped.height, safeRadius, 1.0f, thickness, 3.0f);
}

void NUIRendererGL::fillCircle(const NUIPoint& center, float radius, const NUIColor& color) {
    ensureBasicPrimitive();
    // Approximate circle with triangle fan
    const int segments = 32;
    const float angleStep = 2.0f * 3.14159f / segments;
    
    for (int i = 0; i < segments; ++i) {
        float angle1 = i * angleStep;
        float angle2 = (i + 1) * angleStep;
        
        uint32_t base = static_cast<uint32_t>(vertices_.size());
        
        addVertex(center.x, center.y, 0.5f, 0.5f, color);
        addVertex(center.x + std::cos(angle1) * radius, center.y + std::sin(angle1) * radius, 0, 0, color);
        addVertex(center.x + std::cos(angle2) * radius, center.y + std::sin(angle2) * radius, 1, 1, color);
        
        // Add indices for this triangle
        indices_.push_back(base + 0);
        indices_.push_back(base + 1);
        indices_.push_back(base + 2);
    }
}

void NUIRendererGL::strokeCircle(const NUIPoint& center, float radius, float thickness, const NUIColor& color) {
    ensureBasicPrimitive();
    // Approximate with line segments
    const int segments = 32;
    const float angleStep = 2.0f * 3.14159f / segments;
    
    for (int i = 0; i < segments; ++i) {
        float angle1 = i * angleStep;
        float angle2 = (i + 1) * angleStep;
        
        NUIPoint p1(center.x + std::cos(angle1) * radius, center.y + std::sin(angle1) * radius);
        NUIPoint p2(center.x + std::cos(angle2) * radius, center.y + std::sin(angle2) * radius);
        
        drawLine(p1, p2, thickness, color);
    }
}

void NUIRendererGL::drawLine(const NUIPoint& start, const NUIPoint& end, float thickness, const NUIColor& color) {
    ensureBasicPrimitive();

    // Ensure we are not using a texture (batch breaking)
    if (currentTextureId_ != 0) {
        flush();
        currentTextureId_ = 0;
    }

    // Simple line as thin quad
    float dx = end.x - start.x;
    float dy = end.y - start.y;
    float len = std::sqrt(dx * dx + dy * dy);
    
    if (len < 0.001f) return;

    // Sub-pixel Snapping for crisp 1px lines (Horizontal/Vertical only)
    float x1 = start.x;
    float y1 = start.y;
    float x2 = end.x;
    float y2 = end.y;

    const float kSnapThreshold = 0.1f;
    if (thickness < 1.5f) { // Only snap thin lines
        if (std::abs(dx) < kSnapThreshold) { // Vertical
            // Snap X to pixel center (N.5)
            float snappedX = std::floor(x1) + 0.5f;
            x1 = snappedX;
            x2 = snappedX;
            // Snap Y to nearest integer
            y1 = std::round(y1);
            y2 = std::round(y2);
        } else if (std::abs(dy) < kSnapThreshold) { // Horizontal
            // Snap Y to pixel center (N.5)
            float snappedY = std::floor(y1) + 0.5f;
            y1 = snappedY;
            y2 = snappedY;
            // Snap X to nearest integer
            x1 = std::round(x1);
            x2 = std::round(x2);
        }
    }
    
    dx = x2 - x1;
    dy = y2 - y1; // Recompute deltas
    len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;

    float nx = -dy / len * thickness * 0.5f;
    float ny = dx / len * thickness * 0.5f;
    
    // Use Type 5 (Colored Geometry) for perfectly solid, non-aliased rendering when snapped
    // SDF (Type 1) is too soft for 1px lines (alpha falloff causes "dotted" look)
    // We pass 0 for UVs/Sizes as regular geometry doesn't need them
    
    addVertex(x1 + nx, y1 + ny, 0.0f, 0.0f, color, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5.0f);
    addVertex(x1 - nx, y1 - ny, 0.0f, 0.0f, color, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5.0f); 
    addVertex(x2 - nx, y2 - ny, 0.0f, 0.0f, color, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5.0f);
    addVertex(x2 + nx, y2 + ny, 0.0f, 0.0f, color, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5.0f);
    
    uint32_t base = static_cast<uint32_t>(vertices_.size()) - 4;
    indices_.push_back(base + 0);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base + 0);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);
}

void NUIRendererGL::drawPolyline(const NUIPoint* points, int count, float thickness, const NUIColor& color) {
    ensureBasicPrimitive();
    for (int i = 0; i < count - 1; ++i) {
        drawLine(points[i], points[i + 1], thickness, color);
    }
}

void NUIRendererGL::fillWaveform(const NUIPoint* topPoints, const NUIPoint* bottomPoints, int count, const NUIColor& color) {
    // Flush previous batch if untyped or different type to ensure clean state
    ensureBasicPrimitive();
    
    if (count < 2) return;
    
    ensureBasicPrimitive();

    // Ensure we are not using a texture (batch breaking)
    if (currentTextureId_ != 0) {
        flush();
        currentTextureId_ = 0;
    }
    
    // Build triangle strip: top[0], bottom[0], top[1], bottom[1], ...
    // This creates a filled shape between the top and bottom edges
    uint32_t baseIndex = static_cast<uint32_t>(vertices_.size());
    
    // Add all vertices - addVertex handles transform internally
    // Use Type 5 (Colored Geometry) for solid crisp rendering
    // Arguments: x, y, u, v, color, rw, rh, qw, qh, radius, blur, strokeWidth, type
    for (int i = 0; i < count; ++i) {
        addVertex(topPoints[i].x, topPoints[i].y, 0, 0, color, 0, 0, 0, 0, 0, 0, 0, 5.0f);
        addVertex(bottomPoints[i].x, bottomPoints[i].y, 0, 1, color, 0, 0, 0, 0, 0, 0, 0, 5.0f);
    }
    
    // Build triangle strip indices
    // For each quad between columns: 4 vertices -> 2 triangles
    for (int i = 0; i < count - 1; ++i) {
        uint32_t topLeft = baseIndex + i * 2;
        uint32_t bottomLeft = baseIndex + i * 2 + 1;
        uint32_t topRight = baseIndex + (i + 1) * 2;
        uint32_t bottomRight = baseIndex + (i + 1) * 2 + 1;
        
        // Triangle 1: topLeft, bottomLeft, topRight
        indices_.push_back(topLeft);
        indices_.push_back(bottomLeft);
        indices_.push_back(topRight);
        
        // Triangle 2: topRight, bottomLeft, bottomRight
        indices_.push_back(topRight);
        indices_.push_back(bottomLeft);
        indices_.push_back(bottomRight);
    }
}

void NUIRendererGL::fillWaveformGradient(const NUIPoint* topPoints, const NUIPoint* bottomPoints, int count, 
                                          const NUIColor& colorTop, const NUIColor& colorBottom) {
    // Flush previous batch if untyped or different type
    ensureBasicPrimitive();

    if (count < 2) return;

    // Ensure we are not using a texture (batch breaking)
    if (currentTextureId_ != 0) {
        flush();
        currentTextureId_ = 0;
    }
    
    // Build triangle strip with gradient colors (bright at top, darker at bottom)
    uint32_t baseIndex = static_cast<uint32_t>(vertices_.size());
    
    // Add vertices with different colors for top and bottom edges
    // Fixed: Use Type 5 (Colored Geometry) to prevent texture sampling
    // Arguments: x, y, u, v, color, rw, rh, qw, qh, radius, blur, strokeWidth, type
    for (int i = 0; i < count; ++i) {
        addVertex(topPoints[i].x, topPoints[i].y, 0, 0, colorTop, 0, 0, 0, 0, 0, 0, 0, 5.0f);
        addVertex(bottomPoints[i].x, bottomPoints[i].y, 0, 1, colorBottom, 0, 0, 0, 0, 0, 0, 0, 5.0f);
    }
    
    // Build triangle strip indices
    for (int i = 0; i < count - 1; ++i) {
        uint32_t topLeft = baseIndex + i * 2;
        uint32_t bottomLeft = baseIndex + i * 2 + 1;
        uint32_t topRight = baseIndex + (i + 1) * 2;
        uint32_t bottomRight = baseIndex + (i + 1) * 2 + 1;
        
        // Triangle 1: topLeft, bottomLeft, topRight
        indices_.push_back(topLeft);
        indices_.push_back(bottomLeft);
        indices_.push_back(topRight);
        
        // Triangle 2: topRight, bottomLeft, bottomRight
        indices_.push_back(topRight);
        indices_.push_back(bottomLeft);
        indices_.push_back(bottomRight);
    }
}

// ============================================================================
// Gradient Drawing
// ============================================================================

void NUIRendererGL::fillRectGradient(const NUIRect& rect, const NUIColor& colorStart, const NUIColor& colorEnd, bool vertical) {
    ensureBasicPrimitive();
    if (vertical) {
        addVertex(rect.x, rect.y, 0, 0, colorStart);
        addVertex(rect.right(), rect.y, 1, 0, colorStart);
        addVertex(rect.right(), rect.bottom(), 1, 1, colorEnd);
        addVertex(rect.x, rect.bottom(), 0, 1, colorEnd);
    } else {
        addVertex(rect.x, rect.y, 0, 0, colorStart);
        addVertex(rect.right(), rect.y, 1, 0, colorEnd);
        addVertex(rect.right(), rect.bottom(), 1, 1, colorEnd);
        addVertex(rect.x, rect.bottom(), 0, 1, colorStart);
    }
    
    uint32_t base = static_cast<uint32_t>(vertices_.size()) - 4;
    indices_.push_back(base + 0);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base + 0);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);
}

void NUIRendererGL::fillCircleGradient(const NUIPoint& center, float radius, const NUIColor& colorInner, const NUIColor& colorOuter) {
    ensureBasicPrimitive();
    // Simple radial gradient
    const int segments = 32;
    const float angleStep = 2.0f * 3.14159f / segments;
    
    for (int i = 0; i < segments; ++i) {
        float angle1 = i * angleStep;
        float angle2 = (i + 1) * angleStep;
        
        addVertex(center.x, center.y, 0.5f, 0.5f, colorInner);
        addVertex(center.x + std::cos(angle1) * radius, center.y + std::sin(angle1) * radius, 0, 0, colorOuter);
        addVertex(center.x + std::cos(angle2) * radius, center.y + std::sin(angle2) * radius, 1, 1, colorOuter);
    }
}

// ============================================================================
// Effects (Simplified for now)
// ============================================================================

void NUIRendererGL::drawGlow(const NUIRect& rect, float radius, float intensity, const NUIColor& color) {
    // Simple glow as expanded semi-transparent rect
    NUIRect glowRect = rect;
    glowRect.x -= radius;
    glowRect.y -= radius;
    glowRect.width += radius * 2;
    glowRect.height += radius * 2;
    
    NUIColor glowColor = color;
    glowColor.a *= intensity * 0.3f;
    
    fillRect(glowRect, glowColor);
}

void NUIRendererGL::drawShadow(const NUIRect& rect, float offsetX, float offsetY, float blur, const NUIColor& color) {
    NOMAD_ZONE("Renderer_DrawShadow");
    
    // Shadow quad is larger than the rect to contain the blur
    float spread = blur * 2.0f;
    NUIRect shadowQuad = rect;
    shadowQuad.x += offsetX - spread;
    shadowQuad.y += offsetY - spread;
    shadowQuad.width += spread * 2.0f;
    shadowQuad.height += spread * 2.0f;
    
    // Pass RectSize (logic size) and QuadSize (drawing size) separately
    // Radius fixed at 8.0f for standard shadows for now, matching previous logic
    // Type 1 = Filled Rect (Shadows use same SDF logic with blur)
    addQuad(shadowQuad, color, rect.width, rect.height, shadowQuad.width, shadowQuad.height, 8.0f, blur, 0.0f, 1.0f);
}

// ============================================================================
// Text Rendering
// ============================================================================

void NUIRendererGL::drawText(const std::string& text, const NUIPoint& position, float fontSize, const NUIColor& color) {
    // Use FreeType atlas for ALL text - consistent rendering
    
    if (fontInitialized_) {
        // Use centralized atlas selection
        AtlasInfo atlas = selectAtlas(fontSize);
        
        // Scale factor from baked atlas to requested fontSize
        float scale = fontSize / static_cast<float>(atlas.atlasSize);
        float scaledAscent = atlas.ascent * scale;

        renderTextWithFont(text, NUIPoint(position.x, position.y + scaledAscent), fontSize, color);
        return;
    }

    // Fallback only if FreeType not initialized - simple rectangles
    float charWidth = fontSize * 0.5f;
    float charHeight = fontSize * 0.8f;
    
    for (size_t i = 0; i < text.length(); ++i) {
        char c = text[i];
        if (c >= 32 && c <= 126) {
            float x = position.x + i * charWidth;
            float y = position.y;
            drawCleanCharacter(c, x, y, charWidth, charHeight, color);
        }
    }
}


// ============================================================================
// High-Quality Text Rendering Helpers
// ============================================================================

float NUIRendererGL::getDPIScale() {
#ifdef _WIN32
    HDC hdc = GetDC(NULL);
    if (hdc) {
        int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(NULL, hdc);
        return dpiX / 96.0f; // 96 is standard DPI
    }
#endif
    return 1.0f; // Default scale
}

NUIRendererGL::AtlasInfo NUIRendererGL::selectAtlas(float fontSize) const {
    AtlasInfo info;
    
    const bool useSmallAtlas = (fontSize <= 13.0f);
    const bool useMediumAtlas = (!useSmallAtlas && fontSize <= 17.0f);
    
    if (useSmallAtlas && fontAtlasTextureIdSmall_ != 0 && atlasFontSizeSmall_ > 0) {
        info.textureId = fontAtlasTextureIdSmall_;
        info.atlasSize = atlasFontSizeSmall_;
        info.ascent = fontAscentSmall_;
        info.descent = fontDescentSmall_;
        info.lineHeight = fontLineHeightSmall_;
        info.cache = &fontCacheSmall_;
    } else if (useMediumAtlas && fontAtlasTextureIdMedium_ != 0 && atlasFontSizeMedium_ > 0) {
        info.textureId = fontAtlasTextureIdMedium_;
        info.atlasSize = atlasFontSizeMedium_;
        info.ascent = fontAscentMedium_;
        info.descent = fontDescentMedium_;
        info.lineHeight = fontLineHeightMedium_;
        info.cache = &fontCacheMedium_;
    } else {
        info.textureId = fontAtlasTextureId_;
        info.atlasSize = atlasFontSize_;
        info.ascent = fontAscent_;
        info.descent = fontDescent_;
        info.lineHeight = fontLineHeight_;
        info.cache = &fontCache_;
    }
    
    return info;
}

void NUIRendererGL::drawCleanCharacter(char c, float x, float y, float width, float height, const NUIColor& color) {
    // Clean character rendering using filled rectangles
    // This creates much more readable text
    
    float charWidth = width * 0.8f;
    float charHeight = height;
    float thickness = charWidth * 0.15f; // Thickness of character elements
    
    // Center the character
    float charX = x + (width - charWidth) * 0.5f;
    float charY = y + (height - charHeight) * 0.5f;
    
    // Draw character using clean filled rectangles
    switch (c) {
        case 'A':
        case 'a':
            // A shape: triangle with crossbar
            fillRect(NUIRect(charX + charWidth*0.4f, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY + charHeight*0.6f, charWidth, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.2f, charY + charHeight*0.3f, thickness, charHeight*0.4f), color);
            fillRect(NUIRect(charX + charWidth*0.6f, charY + charHeight*0.3f, thickness, charHeight*0.4f), color);
            break;
        case 'B':
        case 'b':
            // B shape: vertical line with two rectangles
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth*0.7f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, charWidth*0.7f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth*0.7f, thickness), color);
            break;
        case 'C':
        case 'c':
            // C shape: curved rectangle
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth*0.7f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth*0.7f, thickness), color);
            break;
        case 'D':
        case 'd':
            // D shape: vertical line with curved right side
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth*0.6f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth*0.6f, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.6f, charY + charHeight*0.2f, thickness, charHeight*0.6f), color);
            break;
        case 'E':
        case 'e':
            // E shape: vertical line with three horizontals
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, charWidth*0.6f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth*0.8f, thickness), color);
            break;
        case 'F':
        case 'f':
            // F shape: vertical line with two horizontals
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, charWidth*0.6f, thickness), color);
            break;
        case 'G':
        case 'g':
            // G shape: C with additional line
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth*0.7f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth*0.7f, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.5f, charY + charHeight*0.5f, charWidth*0.3f, thickness), color);
            break;
        case 'H':
        case 'h':
            // H shape: two verticals with horizontal
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, charWidth, thickness), color);
            break;
        case 'I':
        case 'i':
            // I shape: vertical line with top and bottom
            fillRect(NUIRect(charX + charWidth*0.4f, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth, thickness), color);
            break;
        case 'J':
        case 'j':
            // J shape: vertical with curve
            fillRect(NUIRect(charX + charWidth*0.4f, charY, thickness, charHeight*0.7f), color);
            fillRect(NUIRect(charX, charY, charWidth*0.6f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight*0.7f, charWidth*0.4f, thickness), color);
            break;
        case 'K':
        case 'k':
            // K shape: vertical with diagonal
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, charWidth*0.6f, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.6f, charY, thickness, charHeight*0.3f), color);
            fillRect(NUIRect(charX + charWidth*0.6f, charY + charHeight*0.7f, thickness, charHeight*0.3f), color);
            break;
        case 'L':
        case 'l':
            // L shape: vertical with bottom horizontal
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth*0.8f, thickness), color);
            break;
        case 'M':
        case 'm':
            // M shape: two verticals with diagonal
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX + charWidth*0.2f, charY + charHeight*0.3f, thickness, charHeight*0.4f), color);
            fillRect(NUIRect(charX + charWidth*0.6f, charY + charHeight*0.3f, thickness, charHeight*0.4f), color);
            break;
        case 'N':
        case 'n':
            // N shape: two verticals with diagonal
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX + charWidth*0.2f, charY + charHeight*0.3f, thickness, charHeight*0.4f), color);
            break;
        case 'O':
        case 'o':
            // O shape: rounded rectangle
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth, thickness), color);
            break;
        case 'P':
        case 'p':
            // P shape: vertical with top and middle
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth*0.7f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, charWidth*0.7f, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.7f, charY, thickness, charHeight*0.5f), color);
            break;
        case 'Q':
        case 'q':
            // O with tail
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.6f, charY + charHeight*0.6f, thickness, charHeight*0.4f), color);
            break;
        case 'R':
        case 'r':
            // P with diagonal
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth*0.7f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, charWidth*0.7f, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.7f, charY, thickness, charHeight*0.5f), color);
            fillRect(NUIRect(charX + charWidth*0.5f, charY + charHeight*0.5f, thickness, charHeight*0.5f), color);
            break;
        case 'S':
        case 's':
            // S shape: three horizontals with verticals
            fillRect(NUIRect(charX, charY, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX, charY, thickness, charHeight*0.5f), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY + charHeight*0.5f, thickness, charHeight*0.5f), color);
            break;
        case 'T':
        case 't':
            // T shape: horizontal with vertical
            fillRect(NUIRect(charX, charY, charWidth, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.4f, charY, thickness, charHeight), color);
            break;
        case 'U':
        case 'u':
            // U shape: two verticals with bottom
            fillRect(NUIRect(charX, charY, thickness, charHeight*0.8f), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY, thickness, charHeight*0.8f), color);
            fillRect(NUIRect(charX, charY + charHeight*0.8f, charWidth, thickness), color);
            break;
        case 'V':
        case 'v':
            // V shape: two diagonals
            fillRect(NUIRect(charX + charWidth*0.2f, charY, thickness, charHeight*0.6f), color);
            fillRect(NUIRect(charX + charWidth*0.6f, charY, thickness, charHeight*0.6f), color);
            fillRect(NUIRect(charX + charWidth*0.3f, charY + charHeight*0.6f, thickness, charHeight*0.4f), color);
            fillRect(NUIRect(charX + charWidth*0.5f, charY + charHeight*0.6f, thickness, charHeight*0.4f), color);
            break;
        case 'W':
        case 'w':
            // W shape: four verticals
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX + charWidth*0.3f, charY, thickness, charHeight*0.6f), color);
            fillRect(NUIRect(charX + charWidth*0.6f, charY, thickness, charHeight*0.6f), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY, thickness, charHeight), color);
            break;
        case 'X':
        case 'x':
            // X shape: two diagonals
            fillRect(NUIRect(charX + charWidth*0.2f, charY, thickness, charHeight*0.4f), color);
            fillRect(NUIRect(charX + charWidth*0.6f, charY, thickness, charHeight*0.4f), color);
            fillRect(NUIRect(charX + charWidth*0.2f, charY + charHeight*0.6f, thickness, charHeight*0.4f), color);
            fillRect(NUIRect(charX + charWidth*0.6f, charY + charHeight*0.6f, thickness, charHeight*0.4f), color);
            break;
        case 'Y':
        case 'y':
            // Y shape: V with vertical
            fillRect(NUIRect(charX + charWidth*0.2f, charY, thickness, charHeight*0.4f), color);
            fillRect(NUIRect(charX + charWidth*0.6f, charY, thickness, charHeight*0.4f), color);
            fillRect(NUIRect(charX + charWidth*0.4f, charY + charHeight*0.4f, thickness, charHeight*0.6f), color);
            break;
        case 'Z':
        case 'z':
            // Z shape: three horizontals
            fillRect(NUIRect(charX, charY, charWidth, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.3f, charY + charHeight*0.5f, charWidth*0.4f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth, thickness), color);
            break;
        case '0':
            // 0 shape: rounded rectangle
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth, thickness), color);
            break;
        case '1':
            // 1 shape: vertical with top
            fillRect(NUIRect(charX + charWidth*0.4f, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX + charWidth*0.2f, charY, charWidth*0.4f, thickness), color);
            break;
        case '2':
            // 2 shape: top, middle, bottom
            fillRect(NUIRect(charX, charY, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.6f, charY + charHeight*0.5f, charWidth*0.2f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, thickness, charHeight*0.5f), color);
            break;
        case '3':
            // 3 shape: three horizontals with vertical
            fillRect(NUIRect(charX, charY, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY, thickness, charHeight), color);
            break;
        case '4':
            // 4 shape: vertical with horizontal
            fillRect(NUIRect(charX, charY, thickness, charHeight*0.6f), color);
            fillRect(NUIRect(charX, charY + charHeight*0.4f, charWidth*0.6f, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.6f, charY, thickness, charHeight), color);
            break;
        case '5':
            // 5 shape: top, middle, bottom
            fillRect(NUIRect(charX, charY, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX, charY, thickness, charHeight*0.5f), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY + charHeight*0.5f, thickness, charHeight*0.5f), color);
            break;
        case '6':
            // 6 shape: vertical with three horizontals
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY + charHeight*0.5f, thickness, charHeight*0.5f), color);
            break;
        case '7':
            // 7 shape: top with vertical
            fillRect(NUIRect(charX, charY, charWidth, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY, thickness, charHeight), color);
            break;
        case '8':
            // 8 shape: vertical with three horizontals
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, charWidth, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth, thickness), color);
            break;
        case '9':
            // 9 shape: vertical with three horizontals
            fillRect(NUIRect(charX, charY, thickness, charHeight*0.5f), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, charWidth, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth, thickness), color);
            break;
        case '.':
            // Period: small square
            fillRect(NUIRect(charX + charWidth*0.4f, charY + charHeight*0.8f, thickness, thickness), color);
            break;
        case ',':
            // Comma: small square with tail
            fillRect(NUIRect(charX + charWidth*0.4f, charY + charHeight*0.8f, thickness, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.3f, charY + charHeight*0.9f, thickness*0.5f, thickness*0.5f), color);
            break;
        case ':':
            // Colon: two small squares
            fillRect(NUIRect(charX + charWidth*0.4f, charY + charHeight*0.3f, thickness, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.4f, charY + charHeight*0.7f, thickness, thickness), color);
            break;
        case ';':
            // Semicolon: colon with tail
            fillRect(NUIRect(charX + charWidth*0.4f, charY + charHeight*0.3f, thickness, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.4f, charY + charHeight*0.7f, thickness, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.3f, charY + charHeight*0.8f, thickness*0.5f, thickness*0.5f), color);
            break;
        case '!':
            // Exclamation: vertical with dot
            fillRect(NUIRect(charX + charWidth*0.4f, charY, thickness, charHeight*0.7f), color);
            fillRect(NUIRect(charX + charWidth*0.4f, charY + charHeight*0.8f, thickness, thickness), color);
            break;
        case '?':
            // Question mark: curve with dot
            fillRect(NUIRect(charX + charWidth*0.6f, charY, charWidth*0.2f, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.4f, charY + charHeight*0.2f, thickness, charHeight*0.3f), color);
            fillRect(NUIRect(charX + charWidth*0.4f, charY + charHeight*0.5f, charWidth*0.2f, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.4f, charY + charHeight*0.8f, thickness, thickness), color);
            break;
        case ' ':
            // Space: nothing
            break;
        default:
            // Unknown character: simple rectangle
            fillRect(NUIRect(charX + charWidth*0.2f, charY + charHeight*0.2f, charWidth*0.6f, charHeight*0.6f), color);
            break;
    }
}

void NUIRendererGL::drawCharacter(char c, float x, float y, float width, float height, const NUIColor& color) {
    // Improved character rendering with better proportions and smoother lines
    // This creates a more refined bitmap font representation
    
    float charWidth = width * 0.7f;  // Slightly narrower for better spacing
    float charHeight = height * 0.8f; // Slightly shorter for better proportions
    float lineWidth = charWidth * 0.08f; // Thinner lines for cleaner look
    
    // Adjust height based on character type
    if (c >= 'a' && c <= 'z') charHeight *= 0.85f; // lowercase
    else if (c >= 'A' && c <= 'Z') charHeight *= 0.95f; // uppercase
    else if (c >= '0' && c <= '9') charHeight *= 0.9f; // numbers
    else if (c == ' ') return; // space - don't draw anything
    else if (c == '.' || c == ',' || c == ';' || c == ':') charHeight *= 0.5f; // punctuation
    
    // Center the character both horizontally and vertically
    float charX = x + (width - charWidth) * 0.5f;
    float charY = y + (height - charHeight) * 0.5f;
    
    // Draw character patterns based on ASCII value
    switch (c) {
        case 'A':
        case 'a':
            // A shape: /\ and - (improved proportions)
            drawLine(NUIPoint(charX + charWidth*0.15f, charY + charHeight), NUIPoint(charX + charWidth*0.5f, charY + charHeight*0.1f), lineWidth, color);
            drawLine(NUIPoint(charX + charWidth*0.5f, charY + charHeight*0.1f), NUIPoint(charX + charWidth*0.85f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(charX + charWidth*0.25f, charY + charHeight*0.6f), NUIPoint(charX + charWidth*0.75f, charY + charHeight*0.6f), lineWidth, color);
            break;
        case 'B':
        case 'b':
            // B shape: | and curves
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.7f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.7f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.7f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.7f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.25f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.7f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.75f), lineWidth, color);
            break;
        case 'C':
        case 'c':
            // C shape: curve
            drawLine(NUIPoint(x + charWidth*0.7f, charY), NUIPoint(x + charWidth*0.1f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.7f, charY + charHeight), lineWidth, color);
            break;
        case 'D':
        case 'd':
            // D shape: | and curve
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.6f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.6f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.6f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.6f, charY + charHeight), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.5f), lineWidth, color);
            break;
        case 'E':
        case 'e':
            // E shape: | and horizontal lines (improved proportions)
            drawLine(NUIPoint(charX + charWidth*0.15f, charY), NUIPoint(charX + charWidth*0.15f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(charX + charWidth*0.15f, charY), NUIPoint(charX + charWidth*0.85f, charY), lineWidth, color);
            drawLine(NUIPoint(charX + charWidth*0.15f, charY + charHeight*0.5f), NUIPoint(charX + charWidth*0.7f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(charX + charWidth*0.15f, charY + charHeight), NUIPoint(charX + charWidth*0.85f, charY + charHeight), lineWidth, color);
            break;
        case 'F':
        case 'f':
            // F shape: | and horizontal lines
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.8f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.6f, charY + charHeight*0.5f), lineWidth, color);
            break;
        case 'G':
        case 'g':
            // G shape: C with line
            drawLine(NUIPoint(x + charWidth*0.7f, charY), NUIPoint(x + charWidth*0.1f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.7f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.7f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.5f), lineWidth, color);
            break;
        case 'H':
        case 'h':
            // H shape: | | and -
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.5f), lineWidth, color);
            break;
        case 'I':
        case 'i':
            // I shape: | with top and bottom
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.9f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.5f, charY), NUIPoint(x + charWidth*0.5f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            break;
        case 'J':
        case 'j':
            // J shape: | with curve
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.9f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.5f, charY), NUIPoint(x + charWidth*0.5f, charY + charHeight*0.8f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.5f, charY + charHeight*0.8f), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            break;
        case 'K':
        case 'k':
            // K shape: | and diagonal
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.9f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            break;
        case 'L':
        case 'l':
            // L shape: | and -
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.8f, charY + charHeight), lineWidth, color);
            break;
        case 'M':
        case 'm':
            // M shape: | \ / |
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.5f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.5f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.9f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            break;
        case 'N':
        case 'n':
            // N shape: | \ |
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            break;
        case 'O':
        case 'o':
            // O shape: circle/oval
            drawLine(NUIPoint(x + charWidth*0.3f, charY), NUIPoint(x + charWidth*0.7f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.2f), NUIPoint(x + charWidth*0.1f, charY + charHeight*0.8f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY + charHeight*0.2f), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.8f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.3f, charY + charHeight), NUIPoint(x + charWidth*0.7f, charY + charHeight), lineWidth, color);
            break;
        case 'P':
        case 'p':
            // P shape: | and P
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.7f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.7f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.7f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.25f), lineWidth, color);
            break;
        case 'Q':
        case 'q':
            // Q shape: O with tail
            drawLine(NUIPoint(x + charWidth*0.3f, charY), NUIPoint(x + charWidth*0.7f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.2f), NUIPoint(x + charWidth*0.1f, charY + charHeight*0.8f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY + charHeight*0.2f), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.8f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.3f, charY + charHeight), NUIPoint(x + charWidth*0.7f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.7f, charY + charHeight*0.8f), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            break;
        case 'R':
        case 'r':
            // R shape: P with diagonal
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.7f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.7f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.7f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.25f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.7f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            break;
        case 'S':
        case 's':
            // S shape: S curve (improved proportions)
            drawLine(NUIPoint(charX + charWidth*0.85f, charY + charHeight*0.1f), NUIPoint(charX + charWidth*0.15f, charY + charHeight*0.1f), lineWidth, color);
            drawLine(NUIPoint(charX + charWidth*0.15f, charY + charHeight*0.1f), NUIPoint(charX + charWidth*0.15f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(charX + charWidth*0.15f, charY + charHeight*0.5f), NUIPoint(charX + charWidth*0.85f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(charX + charWidth*0.85f, charY + charHeight*0.5f), NUIPoint(charX + charWidth*0.85f, charY + charHeight*0.9f), lineWidth, color);
            drawLine(NUIPoint(charX + charWidth*0.85f, charY + charHeight*0.9f), NUIPoint(charX + charWidth*0.15f, charY + charHeight*0.9f), lineWidth, color);
            break;
        case 'T':
        case 't':
            // T shape: - and | (improved proportions)
            drawLine(NUIPoint(charX + charWidth*0.1f, charY + charHeight*0.1f), NUIPoint(charX + charWidth*0.9f, charY + charHeight*0.1f), lineWidth, color);
            drawLine(NUIPoint(charX + charWidth*0.5f, charY + charHeight*0.1f), NUIPoint(charX + charWidth*0.5f, charY + charHeight), lineWidth, color);
            break;
        case 'U':
        case 'u':
            // U shape: | | and -
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight*0.8f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.8f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.8f), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.8f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            break;
        case 'V':
        case 'v':
            // V shape: \ /
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.5f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.5f, charY + charHeight), NUIPoint(x + charWidth*0.9f, charY), lineWidth, color);
            break;
        case 'W':
        case 'w':
            // W shape: | \ / |
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.5f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.5f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            break;
        case 'X':
        case 'x':
            // X shape: \ /
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            break;
        case 'Y':
        case 'y':
            // Y shape: \ / and |
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.5f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.5f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.9f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.5f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.5f, charY + charHeight), lineWidth, color);
            break;
        case 'Z':
        case 'z':
            // Z shape: - \ -
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.9f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            break;
        case '0':
            // 0 shape: oval
            drawLine(NUIPoint(x + charWidth*0.3f, charY), NUIPoint(x + charWidth*0.7f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.2f), NUIPoint(x + charWidth*0.1f, charY + charHeight*0.8f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY + charHeight*0.2f), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.8f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.3f, charY + charHeight), NUIPoint(x + charWidth*0.7f, charY + charHeight), lineWidth, color);
            break;
        case '1':
            // 1 shape: |
            drawLine(NUIPoint(x + charWidth*0.5f, charY), NUIPoint(x + charWidth*0.5f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.3f, charY), NUIPoint(x + charWidth*0.5f, charY), lineWidth, color);
            break;
        case '2':
            // 2 shape: - \ -
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.8f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.8f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.8f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.8f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.8f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.8f, charY + charHeight), lineWidth, color);
            break;
        case '3':
            // 3 shape: - | -
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.8f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.8f, charY), NUIPoint(x + charWidth*0.8f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.8f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.8f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.8f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.8f, charY + charHeight), lineWidth, color);
            break;
        case '4':
            // 4 shape: | \ |
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.8f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.8f, charY), NUIPoint(x + charWidth*0.8f, charY + charHeight), lineWidth, color);
            break;
        case '5':
            // 5 shape: | - \ -
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.8f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.8f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.8f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.8f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.8f, charY + charHeight), lineWidth, color);
            break;
        case '6':
            // 6 shape: | - \ -
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.8f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.8f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.8f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.8f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.8f, charY + charHeight), lineWidth, color);
            break;
        case '7':
            // 7 shape: - |
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.9f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            break;
        case '8':
            // 8 shape: | - | - |
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.9f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            break;
        case '9':
            // 9 shape: | - | - |
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.9f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            break;
        case '.':
            // Period: small dot
            fillRect(NUIRect(x + charWidth*0.4f, charY + charHeight*0.8f, charWidth*0.2f, charHeight*0.2f), color);
            break;
        case ',':
            // Comma: small dot with tail
            fillRect(NUIRect(x + charWidth*0.4f, charY + charHeight*0.8f, charWidth*0.2f, charHeight*0.2f), color);
            drawLine(NUIPoint(x + charWidth*0.4f, charY + charHeight*0.8f), NUIPoint(x + charWidth*0.3f, charY + charHeight), lineWidth, color);
            break;
        case ':':
            // Colon: two dots
            fillRect(NUIRect(x + charWidth*0.4f, charY + charHeight*0.3f, charWidth*0.2f, charHeight*0.2f), color);
            fillRect(NUIRect(x + charWidth*0.4f, charY + charHeight*0.7f, charWidth*0.2f, charHeight*0.2f), color);
            break;
        case ';':
            // Semicolon: dot with tail and comma
            fillRect(NUIRect(x + charWidth*0.4f, charY + charHeight*0.3f, charWidth*0.2f, charHeight*0.2f), color);
            fillRect(NUIRect(x + charWidth*0.4f, charY + charHeight*0.7f, charWidth*0.2f, charHeight*0.2f), color);
            drawLine(NUIPoint(x + charWidth*0.4f, charY + charHeight*0.7f), NUIPoint(x + charWidth*0.3f, charY + charHeight), lineWidth, color);
            break;
        case '!':
            // Exclamation: | and dot
            drawLine(NUIPoint(x + charWidth*0.5f, charY), NUIPoint(x + charWidth*0.5f, charY + charHeight*0.7f), lineWidth, color);
            fillRect(NUIRect(x + charWidth*0.4f, charY + charHeight*0.8f, charWidth*0.2f, charHeight*0.2f), color);
            break;
        case '?':
            // Question mark: ? shape
            drawLine(NUIPoint(x + charWidth*0.7f, charY), NUIPoint(x + charWidth*0.1f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight*0.3f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.3f), NUIPoint(x + charWidth*0.5f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.5f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.5f, charY + charHeight*0.7f), lineWidth, color);
            fillRect(NUIRect(x + charWidth*0.4f, charY + charHeight*0.8f, charWidth*0.2f, charHeight*0.2f), color);
            break;
        case ' ':
            // Space: nothing
            break;
        default:
            // Unknown character: draw a box
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.9f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            break;
    }
}

void NUIRendererGL::drawTextCentered(const std::string& text, const NUIRect& rect, float fontSize, const NUIColor& color) {
    // Measure actual text dimensions
    NUISize textSize = measureText(text, fontSize);
    
    // Measure actual text dimensions
    // NUISize textSize = measureText(text, fontSize); // Already declared above?
    
    // Check line 1454 in error message.
    // If I'm replacing lines 1453-1495, and I include the declaration, it should be fine unless it was already there outside the block?
    // The previous error said 1454: declaration, 1460: redefinition.
    // My previous edit REMOVED the redefinition.
    // Now I replaced the whole block.
    // Let's just look at the code to be sure.
    // I'll assume I need to remove one.

    
    // Calculate horizontal centering
    float x = std::round(rect.x + (rect.width - textSize.width) * 0.5f);
    
    // Calculate vertical centering using real font metrics (Top-Left Y + Ascent).
    float y = std::round(calculateTextY(rect, fontSize));
    
    drawText(text, NUIPoint(x, y), fontSize, color);
}

NUIRenderer::FontMetrics NUIRendererGL::getFontMetrics(float fontSize) const {
    NUIRenderer::FontMetrics metrics;
    if (fontInitialized_) {
        const bool useSmallAtlas = (fontSize <= 13.0f);
        const bool useMediumAtlas = (!useSmallAtlas && fontSize <= 17.0f);
        const int atlasSize = useSmallAtlas ? atlasFontSizeSmall_ : (useMediumAtlas ? atlasFontSizeMedium_ : atlasFontSize_);
        const float ascent = useSmallAtlas ? fontAscentSmall_ : (useMediumAtlas ? fontAscentMedium_ : fontAscent_);
        const float descent = useSmallAtlas ? fontDescentSmall_ : (useMediumAtlas ? fontDescentMedium_ : fontDescent_);
        const float lineHeight = useSmallAtlas ? fontLineHeightSmall_ : (useMediumAtlas ? fontLineHeightMedium_ : fontLineHeight_);
        if (atlasSize > 0) {
            const float scale = fontSize / static_cast<float>(atlasSize);
            metrics.ascent = ascent * scale;
            metrics.descent = descent * scale;
            metrics.lineHeight = lineHeight * scale;
            return metrics;
        }
        return metrics;
    }

    // Fallback approximation (matches the base NUIRenderer default).
    metrics.ascent = fontSize * 0.8f;
    metrics.descent = fontSize * 0.2f;
    metrics.lineHeight = metrics.ascent + metrics.descent;
    return metrics;
}

NUISize NUIRendererGL::measureText(const std::string& text, float fontSize) {
    // IMPORTANT: drawText() renders via the FreeType atlas path; prefer matching metrics here
    // so layout (centering/truncation) matches what actually hits the screen.
    
    // Handle empty text quickly
    if (text.empty()) {
        if (fontInitialized_) {
            AtlasInfo atlas = selectAtlas(fontSize);
            if (atlas.atlasSize > 0) {
                float scale = fontSize / static_cast<float>(atlas.atlasSize);
                return {0.0f, atlas.lineHeight * scale};
            }
        }
        return {0.0f, fontSize};
    }

    // Check measurement cache first (major performance optimization for repeated strings)
    TextMeasurementKey cacheKey{text, fontSize};
    auto cacheIt = textMeasurementCache_.find(cacheKey);
    if (cacheIt != textMeasurementCache_.end()) {
        return cacheIt->second;
    }

    NUISize result;

    if (fontInitialized_) {
        try {
            AtlasInfo atlas = selectAtlas(fontSize);
            if (atlas.atlasSize <= 0 || atlas.cache == nullptr) {
                result = {text.length() * fontSize * 0.6f, fontSize};
            } else {
                float totalWidth = 0.0f;
                float scale = fontSize / static_cast<float>(atlas.atlasSize);
                FT_UInt previousGlyph = 0;

                // UTF-8 decode loop
                size_t index = 0;
                while (index < text.length()) {
                    uint32_t codepoint = decodeUTF8(text, index);
                    if (codepoint == 0) break;
                    
                    auto it = atlas.cache->find(codepoint);
                    if (it == atlas.cache->end()) continue;

                    const FontData& glyph = it->second;

                    if (fontHasKerning_ && previousGlyph != 0 && glyph.glyphIndex != 0) {
                        FT_Vector kerning = {0, 0};
                        if (FT_Get_Kerning(ftFace_, previousGlyph, glyph.glyphIndex, FT_KERNING_DEFAULT, &kerning) == 0) {
                            totalWidth += (kerning.x / 64.0f) * scale;
                        }
                    }

                    totalWidth += (glyph.advance / 64.0f) * scale;
                    previousGlyph = glyph.glyphIndex;
                }

                result = {totalWidth, atlas.lineHeight * scale};
            }
        } catch (...) {
            // Fallback to simple estimation
            result = {text.length() * fontSize * 0.6f, fontSize};
        }
    } else {
        // Fallback: SDF measurement (if atlas text isn't available).
        if (!useSDFText_ && !triedSDFInit_ && sdfRenderer_ && !defaultFontPath_.empty()) {
            useSDFText_ = sdfRenderer_->initialize(defaultFontPath_, 64.0f);
            triedSDFInit_ = true;
        }
        if (useSDFText_ && sdfRenderer_ && sdfRenderer_->isInitialized()) {
            result = sdfRenderer_->measureText(text, fontSize);
        } else {
            result = {text.length() * fontSize * 0.6f, fontSize};
        }
    }
    
    // Store in cache (with simple eviction if needed)
    if (textMeasurementCache_.size() >= kTextMeasurementCacheMaxSize) {
        // Simple eviction: clear half the cache when full
        auto it = textMeasurementCache_.begin();
        for (size_t i = 0; i < kTextMeasurementCacheMaxSize / 2 && it != textMeasurementCache_.end(); ++i) {
            it = textMeasurementCache_.erase(it);
        }
    }
    textMeasurementCache_[cacheKey] = result;
    
    return result;
}


// ============================================================================
// Real Font Rendering with FreeType
// ============================================================================

bool NUIRendererGL::loadFont(const std::string& fontPath) {
    if (FT_New_Face(ftLibrary_, fontPath.c_str(), 0, &ftFace_)) {
        std::cerr << "ERROR: Failed to load font: " << fontPath << std::endl;
        return false;
    }
    defaultFontPath_ = fontPath;

    fontHasKerning_ = FT_HAS_KERNING(ftFace_) != 0;

    auto buildAtlas = [&](int atlasFontSize,
                          uint32_t& atlasTextureId,
                          std::unordered_map<uint32_t, FontData>& cache, // Changed to uint32_t
                          float& outAscent,
                          float& outDescent,
                          float& outLineHeight,
                          int& atlasX,
                          int& atlasY,
                          int& atlasRowHeight) -> bool {
        if (FT_Set_Pixel_Sizes(ftFace_, 0, atlasFontSize) != 0) {
            std::cerr << "ERROR: Failed to set atlas pixel size (" << atlasFontSize << ") for font: " << fontPath << std::endl;
            return false;
        }

        // Use precise float metrics
        float rawAscent = static_cast<float>(ftFace_->size->metrics.ascender) / 64.0f;
        float rawDescent = static_cast<float>(-(ftFace_->size->metrics.descender)) / 64.0f;
        float rawLineHeight = rawAscent + rawDescent;

        // FIX: Rebalance Metrics + Expand Spacing
        // 1. Rebalance Ascent so Cap Height is centered (keeps text vertically aligned).
        // 2. Expand Line Height by ~15% to remove "Invisible Border" feel.
        
        float verticalPadding = atlasFontSize * 0.15f; 
        float capHalfHeight = atlasFontSize * 0.35f;
        
        // Center of Expanded Box = (RawLineHeight + Padding) / 2
        // We want Cap Center to match this.
        float expandedLineHeight = rawLineHeight + verticalPadding;
        
        // FIX: Ensure ascent is never smaller than raw font ascent to prevent top cutoff
        outAscent = std::max((expandedLineHeight * 0.5f) + capHalfHeight, rawAscent);
        outDescent = expandedLineHeight - outAscent;
        outLineHeight = expandedLineHeight;

        if (atlasTextureId != 0) {
            glDeleteTextures(1, &atlasTextureId);
            atlasTextureId = 0;
        }
        glGenTextures(1, &atlasTextureId);
        glBindTexture(GL_TEXTURE_2D, atlasTextureId);

        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA,
            fontAtlasWidth_,
            fontAtlasHeight_,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            nullptr
        );

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        atlasX = 0;
        atlasY = 0;
        atlasRowHeight = 0;
        cache.clear();

        int loadedChars = 0;
        const int loadFlagsBase = FT_LOAD_RENDER | FT_LOAD_NO_BITMAP;
        const int loadFlagsLCD = loadFlagsBase | FT_LOAD_TARGET_LCD | FT_LOAD_FORCE_AUTOHINT;
        const int loadFlagsGray = loadFlagsBase | FT_LOAD_TARGET_LIGHT;
        const int padding = 3;

        // Build character set: ASCII + Unicode symbols
        std::vector<uint32_t> charSet;
        for (uint32_t c = 32; c <= 126; ++c) charSet.push_back(c); // ASCII printable
        
        // Add Unicode transport symbols for UI
        charSet.push_back(0x25B6); // ▶ Play
        charSet.push_back(0x25C0); // ◀ Reverse
        charSet.push_back(0x25A0); // ■ Stop
        charSet.push_back(0x23F8); // ⏸ Pause
        charSet.push_back(0x23EF); // ⏯ Play/Pause
        charSet.push_back(0x23F9); // ⏹ Stop
        charSet.push_back(0x23FA); // ⏺ Record
        
        for (uint32_t codepoint : charSet) {
            FT_UInt glyphIndex = FT_Get_Char_Index(ftFace_, codepoint);
            if (glyphIndex == 0) {
                // Log only for symbols we explicitly requested (ignore control chars if any)
                if (codepoint > 127) {
                    std::cerr << "WARNING: Font missing glyph for U+" << std::hex << codepoint << std::dec << std::endl;
                }
                continue; // skip if not in font
            }

            int loadFlags = fontUseLCD_ ? loadFlagsLCD : loadFlagsGray;
            if (FT_Load_Glyph(ftFace_, glyphIndex, loadFlags)) {
                continue;
            }

            FT_Bitmap* bitmap = &ftFace_->glyph->bitmap;
            const bool glyphIsLCD = fontUseLCD_ && (bitmap->pixel_mode == FT_PIXEL_MODE_LCD);
            int width = glyphIsLCD ? (bitmap->width / 3) : bitmap->width;
            int height = bitmap->rows;

            // Check if we need to move to next row
            if (atlasX + width + padding >= fontAtlasWidth_) {
                atlasX = 0;
                atlasY += atlasRowHeight + padding;
                atlasRowHeight = 0;
            }

            // Check if atlas is full
            if (atlasY + height + padding >= fontAtlasHeight_) {
                std::cerr << "ERROR: Font atlas full (" << atlasFontSize << "px)!" << std::endl;
                break;
            }

            if (width > 0 && height > 0) {
                std::vector<unsigned char> rgba(static_cast<size_t>(width * height * 4), 0);
                const int rowStride = std::abs(bitmap->pitch);
                const bool flipRows = bitmap->pitch < 0;
                const unsigned char* base = flipRows
                    ? bitmap->buffer + static_cast<long>(rowStride) * (height - 1)
                    : bitmap->buffer;
                for (int y = 0; y < height; ++y) {
                    const unsigned char* srcRow = flipRows
                        ? base - static_cast<long>(y * rowStride)
                        : base + static_cast<long>(y * rowStride);
                    for (int x = 0; x < width; ++x) {
                        unsigned char r = 0, g = 0, b = 0, a = 0;
                        if (glyphIsLCD) {
                            r = srcRow[x * 3 + 0];
                            g = srcRow[x * 3 + 1];
                            b = srcRow[x * 3 + 2];
                            a = std::max({r, g, b});
                        } else {
                            r = g = b = srcRow[x];
                            a = srcRow[x];
                        }
                        size_t dst = static_cast<size_t>(y * width + x) * 4;
                        rgba[dst + 0] = r;
                        rgba[dst + 1] = g;
                        rgba[dst + 2] = b;
                        rgba[dst + 3] = a;
                    }
                }

                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexSubImage2D(
                    GL_TEXTURE_2D,
                    0,
                    atlasX,
                    atlasY,
                    width,
                    height,
                    GL_RGBA,
                    GL_UNSIGNED_BYTE,
                    rgba.data()
                );
            }

            FontData charData;
            charData.textureId = atlasTextureId;
            charData.glyphIndex = glyphIndex;
            charData.width = width;
            charData.height = height;
            charData.bearingX = ftFace_->glyph->bitmap_left;
            charData.bearingY = ftFace_->glyph->bitmap_top;
            charData.advance = ftFace_->glyph->advance.x;

            float invW = 1.0f / fontAtlasWidth_;
            float invH = 1.0f / fontAtlasHeight_;
            charData.u0 = atlasX * invW;
            charData.v0 = atlasY * invH;
            charData.u1 = (atlasX + width) * invW;
            charData.v1 = (atlasY + height) * invH;

            cache[codepoint] = charData; // Cache by Unicode codepoint

            atlasX += width + padding;
            atlasRowHeight = std::max(atlasRowHeight, height);
            loadedChars++;
        }

        std::cout << "[Text] Font atlas built: " << fontPath
                  << " (" << atlasFontSize << "px, " << loadedChars << " glyphs, "
                  << (fontUseLCD_ ? "LCD subpixel" : "grayscale") << ")" << std::endl;
        return loadedChars > 0;
    };

    // Large atlas (48px)
    {
        const int ATLAS_FONT_SIZE = 48;
        atlasFontSize_ = ATLAS_FONT_SIZE;
        if (!buildAtlas(ATLAS_FONT_SIZE,
                        fontAtlasTextureId_,
                        fontCache_,
                        fontAscent_,
                        fontDescent_,
                        fontLineHeight_,
                        fontAtlasX_,
                        fontAtlasY_,
                        fontAtlasRowHeight_)) {
            return false;
        }
    }

    // Medium atlas (32px) for common UI copy
    {
        const int ATLAS_FONT_SIZE_MEDIUM = 32;
        atlasFontSizeMedium_ = ATLAS_FONT_SIZE_MEDIUM;
        (void)buildAtlas(ATLAS_FONT_SIZE_MEDIUM,
                         fontAtlasTextureIdMedium_,
                         fontCacheMedium_,
                         fontAscentMedium_,
                         fontDescentMedium_,
                         fontLineHeightMedium_,
                         fontAtlasXMedium_,
                         fontAtlasYMedium_,
                         fontAtlasRowHeightMedium_);
    }

    // Small atlas (24px) for tiny labels
    {
        const int ATLAS_FONT_SIZE_SMALL = 24;
        atlasFontSizeSmall_ = ATLAS_FONT_SIZE_SMALL;
        (void)buildAtlas(ATLAS_FONT_SIZE_SMALL,
                         fontAtlasTextureIdSmall_,
                         fontCacheSmall_,
                         fontAscentSmall_,
                         fontDescentSmall_,
                         fontLineHeightSmall_,
                         fontAtlasXSmall_,
                         fontAtlasYSmall_,
                         fontAtlasRowHeightSmall_);
    }

    fontInitialized_ = true;
    std::cout << "[Text] Font loaded: " << fontPath << " (dual atlases enabled)" << std::endl;
    return true;
}

void NUIRendererGL::renderTextWithFont(const std::string& text, const NUIPoint& position, float fontSize, const NUIColor& color) {
    NOMAD_ZONE("Text_Render");
    if (!fontInitialized_) {
        return; // Can't render without font
    }

    // Use centralized atlas selection
    AtlasInfo atlas = selectAtlas(fontSize);
    
    // Ensure we are using the font atlas texture
    if (currentTextureId_ != atlas.textureId) {
        flush();
        currentTextureId_ = atlas.textureId;
    }
    
    // Pre-allocate vertex buffer space (4 vertices per glyph, 6 indices per glyph)
    // This avoids repeated vector resizing for large text blocks
    size_t estimatedGlyphs = text.length();
    vertices_.reserve(vertices_.size() + estimatedGlyphs * 4);
    indices_.reserve(indices_.size() + estimatedGlyphs * 6);
    
    // fontSize is in logical pixels - no DPI scaling needed here
    // Scale factor from atlas size to requested font size
    float scale = fontSize / static_cast<float>(atlas.atlasSize);

    // position.y is the BASELINE position (already adjusted by caller)
    // Pixel-align starting position for crisp text
    float x = std::round(position.x);
    float baseline = std::round(position.y);
    FT_UInt previousGlyph = 0;
    // Calculate total bounds for debug

    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    float startX = x;
    
    // UTF-8 decode loop
    size_t index = 0;
    while (index < text.length()) {
        size_t prevIndex = index;
        uint32_t codepoint = decodeUTF8(text, index);
        
        if (codepoint == 0) {
            // Prevent blank text: skip one byte and retry or continue
            // decodeUTF8 might have advanced index partially.
            // Ensure we advance at least by 1 to avoid infinite loop (decodeUTF8 handles this but let's be safe)
            if (index == prevIndex) index++;
            continue; 
        }

        auto it = atlas.cache->find(codepoint);
        if (it == atlas.cache->end()) {
             continue;
        }
        
        
        const FontData& ch = it->second;
        
        // Apply kerning for better spacing
        if (fontHasKerning_ && previousGlyph != 0 && ch.glyphIndex != 0) {
            FT_Vector kerning = {0, 0};
            if (FT_Get_Kerning(ftFace_, previousGlyph, ch.glyphIndex, FT_KERNING_DEFAULT, &kerning) == 0) {
                x += (kerning.x >> 6) * scale;
            }
        }
        previousGlyph = ch.glyphIndex;
        
        // Scale glyph metrics from the atlas to the target size
        float scaledBearingX = ch.bearingX * scale;
        float scaledBearingY = ch.bearingY * scale;
        float w = ch.width * scale;
        float h = ch.height * scale;
        
        // Glyph positioning relative to baseline
        // Allow sub-pixel positioning for smooth baseline alignment
        float xpos = x + scaledBearingX;
        float ypos = baseline - scaledBearingY; // Top of glyph

        minX = std::min(minX, xpos);
        minY = std::min(minY, ypos);
        maxX = std::max(maxX, xpos + w);
        maxY = std::max(maxY, ypos + h);

        // Draw textured quad for character
        // Type 4 = Bitmap Text
        addVertex(xpos,     ypos + h, ch.u0, ch.v1, color, 0,0,0,0, 0,0,0, 4.0f); 
        addVertex(xpos + w, ypos + h, ch.u1, ch.v1, color, 0,0,0,0, 0,0,0, 4.0f); 
        addVertex(xpos + w, ypos,     ch.u1, ch.v0, color, 0,0,0,0, 0,0,0, 4.0f); 
        addVertex(xpos,     ypos,     ch.u0, ch.v0, color, 0,0,0,0, 0,0,0, 4.0f);
        
        // Add indices for quad
        uint32_t base = static_cast<uint32_t>(vertices_.size()) - 4;
        indices_.push_back(base + 0);
        indices_.push_back(base + 1);
        indices_.push_back(base + 2);
        indices_.push_back(base + 0);
        indices_.push_back(base + 2);
        indices_.push_back(base + 3);
        
        // Advance cursor with sub-pixel precision to match measurement
        x += (ch.advance / 64.0f) * scale;
    }

    if (debugTextBounds_) {
        // Draw debug overlay (requires breaking batch)
        // Store current text state to restore? No, just flush.
        flush();
        
        // 1. Draw Text Bounds (Green)
        strokeRect(NUIRect(minX, minY, maxX - minX, maxY - minY), 1.0f, NUIColor(0, 1, 0, 1));
        
        // 2. Draw Baseline (Blue)
        drawLine(NUIPoint(minX, baseline), NUIPoint(maxX, baseline), 1.0f, NUIColor(0, 0, 1, 1));
        
        // 3. Draw Clip Rect (Red)
        if (scissorEnabled_) {
            GLint scissor[4];
            glGetIntegerv(GL_SCISSOR_BOX, scissor);
            // Convert GL bottom-up scissor back to UI coordinates top-down
            // Window height needed... using height_ member
            float sx = (float)scissor[0];
            float sy = (float)(height_ - scissor[1] - scissor[3]);
            float sw = (float)scissor[2];
            float sh = (float)scissor[3];
            strokeRect(NUIRect(sx, sy, sw, sh), 1.0f, NUIColor(1, 0, 0, 1));
        }
        
        // Restore text state for potential next text calls
        // Actually, the caller will set state if needed, but we should make sure we don't leave it in primitive mode if caller expects text batching.
        // It's safer to just let the next draw setup its state.
        
        // However, we need to ensure we don't accidentally leave a bound texture if we used strokeRect which clears it.
    }

}

// ============================================================================
// Texture/Image Drawing (Placeholder)
// ============================================================================

void NUIRendererGL::drawTexture(uint32_t textureId, const NUIRect& destRect, const NUIRect& sourceRect) {
    if (textureId == 0) return;
    auto it = textures_.find(textureId);
    if (it == textures_.end()) return;
    const TextureData& td = it->second;

    NOMAD_ZONE("Texture_Draw");

    ensureBasicPrimitive();

    // Check if we need to switch textures (batch breaking)
    if (currentTextureId_ != td.glId) {
        flush(); // Draw pending batch with old texture
        currentTextureId_ = td.glId; // Switch to new texture
    }

    // Compute texture coordinates (sourceRect is in pixels)
    float tx0 = 0.0f, ty0 = 0.0f, tx1 = 1.0f, ty1 = 1.0f;
    if (td.width > 0 && td.height > 0) {
        float srcX0 = sourceRect.x;
        float srcY0 = sourceRect.y;
        float srcX1 = sourceRect.x + sourceRect.width;
        float srcY1 = sourceRect.y + sourceRect.height;

        const float invWidth = 1.0f / static_cast<float>(td.width);
        const float invHeight = 1.0f / static_cast<float>(td.height);

        tx0 = srcX0 * invWidth;
        tx1 = srcX1 * invWidth;
        ty0 = srcY0 * invHeight;
        ty1 = srcY1 * invHeight;

        if (sourceRect.width < 0.0f) std::swap(tx0, tx1);
        if (sourceRect.height < 0.0f) std::swap(ty0, ty1);
    }

    NUIColor white(1.0f, 1.0f, 1.0f, 1.0f);
    addVertex(destRect.x, destRect.y, tx0, ty0, white);
    addVertex(destRect.right(), destRect.y, tx1, ty0, white);
    addVertex(destRect.right(), destRect.bottom(), tx1, ty1, white);
    addVertex(destRect.x, destRect.bottom(), tx0, ty1, white);

    uint32_t base = static_cast<uint32_t>(vertices_.size()) - 4;
    indices_.push_back(base + 0);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base + 0);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);
}

void NUIRendererGL::drawTexture(const NUIRect& bounds, const unsigned char* rgba, 
                                int width, int height) {
    // Validate input parameters
    if (!rgba || width <= 0 || height <= 0) {
        std::cerr << "OpenGL: Invalid texture data (rgba=" << (void*)rgba 
                  << ", width=" << width << ", height=" << height << ")" << std::endl;
        return;
    }

    NOMAD_ZONE("Texture_Upload");

    // Flush any pending batched geometry before texture rendering
    flush();

    // Create a temporary texture, upload pixels, draw and delete (legacy one-shot path)
    GLuint texture;
    glGenTextures(1, &texture);
    if (texture == 0) {
        std::cerr << "OpenGL: Failed to generate texture" << std::endl;
        return;
    }

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Draw textured quad using the uploaded texture
    NUIColor white(1.0f, 1.0f, 1.0f, 1.0f);
    addVertex(bounds.x, bounds.y, 0.0f, 0.0f, white);
    addVertex(bounds.right(), bounds.y, 1.0f, 0.0f, white);
    addVertex(bounds.right(), bounds.bottom(), 1.0f, 1.0f, white);
    addVertex(bounds.x, bounds.bottom(), 0.0f, 1.0f, white);

    uint32_t base = static_cast<uint32_t>(vertices_.size()) - 4;
    indices_.push_back(base + 0);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base + 0);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices_.size() * sizeof(Vertex), vertices_.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_.size() * sizeof(uint32_t), indices_.data(), GL_DYNAMIC_DRAW);

    glUseProgram(primitiveShader_.id);
    glUniformMatrix4fv(primitiveShader_.projectionLoc, 1, GL_FALSE, projectionMatrix_);
    glUniform1i(primitiveShader_.primitiveTypeLoc, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(primitiveShader_.textureLoc, 0);
    glUniform1i(primitiveShader_.useTextureLoc, 1);

    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices_.size()), GL_UNSIGNED_INT, 0);
    drawCallCount_++;
    if (Nomad::Profiler::getInstance().isEnabled()) {
        Nomad::Profiler::getInstance().recordDrawCall();
        Nomad::Profiler::getInstance().recordTriangles(static_cast<uint32_t>(indices_.size() / 3));
    }

    vertices_.clear();
    indices_.clear();

    glDeleteTextures(1, &texture);
}

void NUIRendererGL::drawTextureFlippedV(uint32_t textureId, const NUIRect& destRect, const NUIRect& sourceRect) {
    if (textureId == 0) return;
    auto it = textures_.find(textureId);
    if (it == textures_.end()) return;
    const TextureData& td = it->second;

    NOMAD_ZONE("Texture_Draw_FlippedV");

    // Flush batch to ensure correct ordering
    flush();

    // Compute normalized texture coordinates, with V flipped
    float tx0 = 0.0f, ty0 = 1.0f, tx1 = 1.0f, ty1 = 0.0f; // note V flipped
    if (td.width > 0 && td.height > 0) {
        float srcX0 = sourceRect.x;
        float srcY0 = sourceRect.y;
        float srcX1 = sourceRect.x + sourceRect.width;
        float srcY1 = sourceRect.y + sourceRect.height;

        const float invWidth = 1.0f / static_cast<float>(td.width);
        const float invHeight = 1.0f / static_cast<float>(td.height);

        tx0 = srcX0 * invWidth;
        tx1 = srcX1 * invWidth;
        // Flip V: swap mapping of top/bottom
        ty0 = srcY1 * invHeight; // top
        ty1 = srcY0 * invHeight; // bottom
    }

    NUIColor white(1.0f, 1.0f, 1.0f, 1.0f);
    addVertex(destRect.x, destRect.y, tx0, ty0, white);
    addVertex(destRect.right(), destRect.y, tx1, ty0, white);
    addVertex(destRect.right(), destRect.bottom(), tx1, ty1, white);
    addVertex(destRect.x, destRect.bottom(), tx0, ty1, white);

    uint32_t base = static_cast<uint32_t>(vertices_.size()) - 4;
    indices_.push_back(base + 0);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base + 0);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices_.size() * sizeof(Vertex), vertices_.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_.size() * sizeof(uint32_t), indices_.data(), GL_DYNAMIC_DRAW);

    glUseProgram(primitiveShader_.id);
    glUniformMatrix4fv(primitiveShader_.projectionLoc, 1, GL_FALSE, projectionMatrix_);
    glUniform1i(primitiveShader_.primitiveTypeLoc, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, td.glId);
    glUniform1i(primitiveShader_.textureLoc, 0);
    glUniform1i(primitiveShader_.useTextureLoc, 1);

    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices_.size()), GL_UNSIGNED_INT, 0);
    drawCallCount_++;
    if (Nomad::Profiler::getInstance().isEnabled()) {
        Nomad::Profiler::getInstance().recordDrawCall();
        Nomad::Profiler::getInstance().recordTriangles(static_cast<uint32_t>(indices_.size() / 3));
    }

    vertices_.clear();
    indices_.clear();
}

// ============================================================================
// Texture/Image Drawing (Stub implementations)
// ============================================================================

uint32_t NUIRendererGL::loadTexture(const std::string& filepath) {
    // Simple stub - file loading not implemented here
    (void)filepath;
    return 0;
}

uint32_t NUIRendererGL::createTexture(const uint8_t* data, int width, int height) {
    if (width <= 0 || height <= 0) return 0;

    NOMAD_ZONE("Texture_Create");

    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (tex == 0) return 0;

    glBindTexture(GL_TEXTURE_2D, tex);
    // Allocate storage (data can be null for empty texture)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    uint32_t id = nextTextureId_++;
    textures_[id] = TextureData{ static_cast<uint32_t>(tex), width, height };
    return id;
}

void NUIRendererGL::deleteTexture(uint32_t textureId) {
    if (textureId == 0) return;
    auto it = textures_.find(textureId);
    if (it == textures_.end()) return;
    uint32_t glId = it->second.glId;
    if (glId != 0) glDeleteTextures(1, &glId);
    textures_.erase(it);
}

uint32_t NUIRendererGL::renderToTextureBegin(int width, int height) {
    if (width <= 0 || height <= 0) return 0;
    
    // Flush current batch to screen before switching target
    flush();

    // Create FBO if needed
    if (fbo_ == 0) {
        glGenFramebuffers(1, &fbo_);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    // Create texture
    uint32_t texId = createTexture(nullptr, width, height);
    if (texId == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return 0;
    }
    
    // Attach texture to FBO
    uint32_t glTexId = getGLTextureId(texId);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, glTexId, 0);

    // Check status
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "FBO incomplete" << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return 0;
    }

    // Save state
    glGetIntegerv(GL_VIEWPORT, fboPrevViewport_);
    widthBackup_ = width_;
    heightBackup_ = height_;

    // Set up FBO state
    glViewport(0, 0, width, height);
    
    // Update projection for FBO (Ortho 0..width, 0..height)
    width_ = width;
    height_ = height;
    updateProjectionMatrix();

    // Clear FBO (transparent black)
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    renderingToTexture_ = true;
    lastRenderTextureId_ = texId;
    fboWidth_ = width;
    fboHeight_ = height;

    return texId;
}

uint32_t NUIRendererGL::renderToTextureEnd() {
    if (!renderingToTexture_) return 0;

    // Flush batch to FBO
    flush();

    // Restore state
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(fboPrevViewport_[0], fboPrevViewport_[1], fboPrevViewport_[2], fboPrevViewport_[3]);
    
    width_ = widthBackup_;
    height_ = heightBackup_;
    updateProjectionMatrix();

    renderingToTexture_ = false;
    return lastRenderTextureId_;
}



uint32_t NUIRendererGL::getGLTextureId(uint32_t textureId) const {
    auto it = textures_.find(textureId);
    if (it == textures_.end()) {
        return 0;
    }
    return it->second.glId;
}

void NUIRendererGL::beginOffscreen(int width, int height) {
    // Backup current size and projection
    widthBackup_ = width_;
    heightBackup_ = height_;
    std::memcpy(projectionBackup_, projectionMatrix_, sizeof(projectionMatrix_));

    // Switch to offscreen size and update projection
    width_ = width;
    height_ = height;
    updateProjectionMatrix();
    // Set viewport to match offscreen target so draw calls map correctly
    glViewport(0, 0, width, height);
}

void NUIRendererGL::endOffscreen() {
    // Restore original projection and size
    width_ = widthBackup_;
    height_ = heightBackup_;
    // Restore backup matrix explicitly (avoid precision drift)
    std::memcpy(projectionMatrix_, projectionBackup_, sizeof(projectionBackup_));
    // Restore viewport to the original backbuffer size
    glViewport(0, 0, width_, height_);
}

// ============================================================================
// Batching
// ============================================================================

void NUIRendererGL::beginBatch() {
    batching_ = true;
}

void NUIRendererGL::endBatch() {
    batching_ = false;
    flush();
}

void NUIRendererGL::flush() {
    NOMAD_ZONE("Renderer_Flush");
    if (vertices_.empty()) {
        return;
    }
    
    // Upload vertex data
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices_.size() * sizeof(Vertex), vertices_.data(), GL_DYNAMIC_DRAW);
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_.size() * sizeof(uint32_t), indices_.data(), GL_DYNAMIC_DRAW);
    
    // Use shader
    glUseProgram(primitiveShader_.id);
    glUniformMatrix4fv(primitiveShader_.projectionLoc, 1, GL_FALSE, projectionMatrix_);
    // Note: opacity is already in vertex colors
    glUniform1i(primitiveShader_.primitiveTypeLoc, currentPrimitiveType_);
    // Default to no texturing; enable below if a texture is bound
    glUniform1i(primitiveShader_.useTextureLoc, 0);
    
    if (currentPrimitiveType_ == 1) {
        // Uniforms moved to attributes for batching
    } else if (currentPrimitiveType_ == 2) {
        // Text
        glUniform1f(primitiveShader_.smoothnessLoc, currentSmoothness_);
    } else if (currentPrimitiveType_ == 3) {
        // Stroked rounded rect (Uniforms moved to attributes)
    } else {
        glUniform1i(primitiveShader_.useTextureLoc, 0);
    }
    
    // Bind current texture if needed
    if (currentTextureId_ != 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, currentTextureId_);
        glUniform1i(primitiveShader_.textureLoc, 0);
        glUniform1i(primitiveShader_.useTextureLoc, 1);
    }
    
    // Draw
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices_.size()), GL_UNSIGNED_INT, 0);
    drawCallCount_++;  // Track draw call
    // Record with global profiler
    {
        if (Nomad::Profiler::getInstance().isEnabled()) {
            Nomad::Profiler::getInstance().recordDrawCall();
            Nomad::Profiler::getInstance().recordTriangles(static_cast<uint32_t>(indices_.size() / 3));
        }
    }

    // Clear for next batch
    vertices_.clear();
    indices_.clear();
    
    // Reset texture state after flush? No, keep it until it changes or frame ends.
    // Actually, if we just flushed, we might want to reset if the next call is a non-textured primitive.
    // But for now, we rely on the caller setting currentTextureId_ = 0 for non-textured.
    // Let's enforce that: if we flushed, we don't necessarily change the state, 
    // but addVertex/addQuad should probably set currentTextureId_ = 0 if they don't use textures.
    // For now, let's just clear the buffers.
}

void NUIRendererGL::ensureBasicPrimitive() {
    // Switch back to flat primitives when the previous draw used the rounded-rect path
    if (currentPrimitiveType_ != 0) {
        flush();
        currentPrimitiveType_ = 0;
        currentRadius_ = 0.0f;
        currentBlur_ = 0.0f;
        currentSize_ = {0.0f, 0.0f};
        currentQuadSize_ = {0.0f, 0.0f};
    }
}

// ============================================================================
// Private Helpers
// ============================================================================

bool NUIRendererGL::initializeGL() {
    // Load OpenGL functions with GLAD
    if (!gladLoadGL()) {
        // Failed to load OpenGL functions
        return false;
    }
    
    // Enable blending for transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // OpenGL context should already be created by platform layer
    return true;
}

bool NUIRendererGL::loadShaders() {
    uint32_t vertShader = compileShader(vertexShaderSource, GL_VERTEX_SHADER);
    uint32_t fragShader = compileShader(fragmentShaderSource, GL_FRAGMENT_SHADER);
    
    if (!vertShader || !fragShader) {
        return false;
    }
    
    primitiveShader_.id = linkProgram(vertShader, fragShader);
    
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);
    
    if (!primitiveShader_.id) {
        return false;
    }
    
    // Get uniform locations
    primitiveShader_.projectionLoc = glGetUniformLocation(primitiveShader_.id, "uProjection");
    // Note: opacity is baked into vertex colors, no uniform needed
    primitiveShader_.primitiveTypeLoc = glGetUniformLocation(primitiveShader_.id, "uPrimitiveType");
    primitiveShader_.textureLoc = glGetUniformLocation(primitiveShader_.id, "uTexture");
    primitiveShader_.useTextureLoc = glGetUniformLocation(primitiveShader_.id, "uUseTexture");
    primitiveShader_.smoothnessLoc = glGetUniformLocation(primitiveShader_.id, "uSmoothness");

    
    return true;
}

void NUIRendererGL::createBuffers() {
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    
    glBindVertexArray(vao_);
    
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    
    // Position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, x));
    
    // TexCoord
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
    
    // Color
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, r));

    // Rect Size
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, rw));

    // Quad Size
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, qw));

    // Radius
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, radius));

    // Blur
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, blur));

    // Stroke Width
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, strokeWidth));

    // Primitive Type
    glEnableVertexAttribArray(8);
    glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, primitiveType));
    
    glBindVertexArray(0);
}

uint32_t NUIRendererGL::compileShader(const char* source, uint32_t type) {
    uint32_t shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    
    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        // Error logging
        return 0;
    }
    
    return shader;
}

uint32_t NUIRendererGL::linkProgram(uint32_t vertexShader, uint32_t fragmentShader) {
    uint32_t program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    
    int success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        // Error logging
        return 0;
    }
    
    return program;
}

void NUIRendererGL::addVertex(float x, float y, float u, float v, const NUIColor& color,
                             float rw, float rh, float qw, float qh, 
                             float radius, float blur, float strokeWidth, float type) {
    applyTransform(x, y);
    
    Vertex vertex;
    vertex.x = x;
    vertex.y = y;
    vertex.u = u;
    vertex.v = v;
    vertex.r = color.r;
    vertex.g = color.g;
    vertex.b = color.b;
    vertex.a = color.a * globalOpacity_;
    vertex.rw = rw;
    vertex.rh = rh;
    vertex.qw = qw;
    vertex.qh = qh;
    vertex.radius = radius;
    vertex.blur = blur;
    vertex.strokeWidth = strokeWidth;
    vertex.primitiveType = type;
    
    vertices_.push_back(vertex);
}

void NUIRendererGL::addQuad(const NUIRect& rect, const NUIColor& color,
                             float rw, float rh, float qw, float qh, 
                             float radius, float blur, float strokeWidth, float type) {
    // Optimization: If Primitive Type is Rect (1) or Stroke (3), we IGNORE texture state!
    // So we don't need to flush if currentTextureId_ != 0.
    // Only flush if we are Type 0, 2, 4 AND texture mismatches.
    bool textureMatters = (type == 0.0f || type == 2.0f || type == 4.0f);
    
    if (textureMatters && currentTextureId_ != 0) {
        flush();
        currentTextureId_ = 0;
    }

    addVertex(rect.x, rect.y, 0.0f, 0.0f, color, rw, rh, qw, qh, radius, blur, strokeWidth, type);
    addVertex(rect.right(), rect.y, 1.0f, 0.0f, color, rw, rh, qw, qh, radius, blur, strokeWidth, type);
    addVertex(rect.right(), rect.bottom(), 1.0f, 1.0f, color, rw, rh, qw, qh, radius, blur, strokeWidth, type);
    addVertex(rect.x, rect.bottom(), 0.0f, 1.0f, color, rw, rh, qw, qh, radius, blur, strokeWidth, type);
    
    uint32_t base = static_cast<uint32_t>(vertices_.size()) - 4;
    indices_.push_back(base + 0);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base + 0);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);
}

void NUIRendererGL::applyTransform(float& x, float& y) {
    if (transformStack_.empty()) {
        return;
    }
    
    for (const auto& t : transformStack_) {
        x += t.tx;
        y += t.ty;
        x *= t.scale;
        y *= t.scale;
        // Apply rotation
    }
}

void NUIRendererGL::updateProjectionMatrix() {
    // Orthographic projection matrix
    float left = 0.0f;
    float right = static_cast<float>(width_);
    float bottom = static_cast<float>(height_);
    float top = 0.0f;
    float nearPlane = -1.0f;
    float farPlane = 1.0f;
    
    std::memset(projectionMatrix_, 0, sizeof(projectionMatrix_));
    
    projectionMatrix_[0] = 2.0f / (right - left);
    projectionMatrix_[5] = 2.0f / (top - bottom);
    projectionMatrix_[10] = -2.0f / (farPlane - nearPlane);
    projectionMatrix_[12] = -(right + left) / (right - left);
    projectionMatrix_[13] = -(top + bottom) / (top - bottom);
    projectionMatrix_[14] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    projectionMatrix_[15] = 1.0f;
}

// ============================================================================
// Performance Optimizations
// ============================================================================

void NUIRendererGL::invalidateCache(uint64_t widgetId) {
    renderCache_.invalidate(widgetId);
}

bool NUIRendererGL::renderCachedOrUpdate(uint64_t widgetId, const NUIRect& destRect,
                                         const std::function<void()>& renderCallback) {
    // If caching is disabled, just render directly
    if (!renderCache_.isEnabled()) {
        renderCallback();
        return false;
    }

    // Get or create cache entry for this widget
    // Use destination rect size
    NUISize size(destRect.width, destRect.height);
    CachedRenderData* cache = renderCache_.getOrCreateCache(widgetId, size);
    
    if (!cache) {
        renderCallback();
        return false;
    }

    // Delegate to render cache manager to render from cache or update it
    renderCache_.renderCachedOrUpdate(cache, destRect, renderCallback);
    return true;
}

void NUIRendererGL::setBatchingEnabled(bool enabled) {
    batchManager_.setEnabled(enabled);
}

void NUIRendererGL::setDirtyRegionTrackingEnabled(bool enabled) {
    dirtyRegionManager_.setEnabled(enabled);
}

void NUIRendererGL::setCachingEnabled(bool enabled) {
    renderCache_.setEnabled(enabled);
}

void NUIRendererGL::getOptimizationStats(size_t& batchedQuads, size_t& dirtyRegions, 
                                        size_t& cachedWidgets, size_t& cacheMemoryBytes) {
    batchedQuads = batchManager_.getTotalQuads();
    dirtyRegions = dirtyRegionManager_.getDirtyRegionCount();
    cachedWidgets = renderCache_.getCacheCount();
    cacheMemoryBytes = renderCache_.getMemoryUsage();
}

} // namespace NomadUI
