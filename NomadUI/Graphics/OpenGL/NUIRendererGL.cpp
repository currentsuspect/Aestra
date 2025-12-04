// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUIRendererGL.h"
#include <cstring>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <ft2build.h>
#include FT_FREETYPE_H
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
// Shader Sources (embedded)
// ============================================================================

static const char* vertexShaderSource = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;

out vec2 vTexCoord;
out vec4 vColor;

uniform mat4 uProjection;

void main() {
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
    vTexCoord = aTexCoord;
    vColor = aColor;
}
)";

static const char* fragmentShaderSource = R"(
#version 330 core
in vec2 vTexCoord;
in vec4 vColor;

out vec4 FragColor;

uniform int uPrimitiveType;
uniform float uRadius;
uniform float uBlur;
uniform float uRectWidth;
uniform float uRectHeight;
uniform float uQuadWidth;
uniform float uQuadHeight;
uniform sampler2D uTexture;
uniform bool uUseTexture;

float sdRoundedRect(vec2 p, vec2 size, float radius) {
    vec2 d = abs(p) - size + radius;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - radius;
}

void main() {
    vec4 color = vColor;
    
    // Apply texture if enabled
    if (uUseTexture) {
        vec4 texColor = texture(uTexture, vTexCoord);
        color *= texColor;
    }
    
    if (uPrimitiveType == 1) {
        // Rounded rectangle / Shadow
        // Map vTexCoord (0..1) to centered coordinates based on Quad Size
        vec2 pos = (vTexCoord - 0.5) * vec2(uQuadWidth, uQuadHeight);
        
        // Calculate SDF to the Rect (Shape)
        float dist = sdRoundedRect(pos, vec2(uRectWidth, uRectHeight) * 0.5, uRadius);
        
        // Soft edge / Shadow blur
        // Inside is negative, Outside is positive
        // We want 1.0 inside, 0.0 outside
        // smoothstep(-blur, blur, dist) goes from 0 to 1 as we go from inside to outside
        float alpha = 1.0 - smoothstep(-uBlur, uBlur, dist);
        
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
    
        // Try to load the best font for Nomad
        std::vector<std::string> fontPaths = {
            "C:/Windows/Fonts/segoeui.ttf",      // Segoe UI - Windows native, excellent for UI
            "C:/Windows/Fonts/calibri.ttf",      // Calibri - Modern, very clear
            "C:/Windows/Fonts/arial.ttf",        // Arial - Classic fallback
            "C:/Windows/Fonts/consola.ttf",      // Consolas - Monospace fallback
            "C:/Windows/Fonts/tahoma.ttf",       // Tahoma - Good for small text
            "C:/Windows/Fonts/verdana.ttf"       // Verdana - Designed for screen clarity
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
    glDisable(GL_CULL_FACE);
    
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
        // Clean up character textures
        for (auto& pair : fontCache_) {
            glDeleteTextures(1, &pair.second.textureId);
        }
        fontCache_.clear();
        
        FT_Done_Face(ftFace_);
        FT_Done_FreeType(ftLibrary_);
        fontInitialized_ = false;
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
    glEnable(GL_SCISSOR_TEST);
    glScissor(
        static_cast<int>(rect.x),
        static_cast<int>(height_ - rect.y - rect.height),
        static_cast<int>(rect.width),
        static_cast<int>(rect.height)
    );
    // Track renderer-side scissor state so other systems can query it
    scissorEnabled_ = true;
}

void NUIRendererGL::clearClipRect() {
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
    // Check if we need to flush (state change or size change)
    // Note: uQuadSize and uRectSize are uniforms, so we must flush if they change.
    // For simple rounded rects, QuadSize == RectSize.
    if (currentPrimitiveType_ != 1 || 
        currentRadius_ != radius || 
        currentBlur_ != 1.0f || 
        currentSize_.width != rect.width || 
        currentSize_.height != rect.height) {
        flush();
    }
    
    currentPrimitiveType_ = 1;
    currentRadius_ = radius;
    currentBlur_ = 1.0f; // 1px blur for anti-aliasing
    currentSize_ = {rect.width, rect.height};
    currentQuadSize_ = {rect.width, rect.height};
    
    addQuad(rect, color);
}

void NUIRendererGL::strokeRect(const NUIRect& rect, float thickness, const NUIColor& color) {
    // Draw 4 lines
    drawLine(NUIPoint({rect.x, rect.y}), NUIPoint({rect.right(), rect.y}), thickness, color);
    drawLine(NUIPoint({rect.right(), rect.y}), NUIPoint({rect.right(), rect.bottom()}), thickness, color);
    drawLine(NUIPoint({rect.right(), rect.bottom()}), NUIPoint({rect.x, rect.bottom()}), thickness, color);
    drawLine(NUIPoint({rect.x, rect.bottom()}), NUIPoint({rect.x, rect.y}), thickness, color);
}

void NUIRendererGL::strokeRoundedRect(const NUIRect& rect, float radius, float thickness, const NUIColor& color) {
    // For now, use simple stroke
    strokeRect(rect, thickness, color);
}

void NUIRendererGL::fillCircle(const NUIPoint& center, float radius, const NUIColor& color) {
    ensureBasicPrimitive();
    // Approximate circle with triangle fan
    const int segments = 32;
    const float angleStep = 2.0f * 3.14159f / segments;
    
    for (int i = 0; i < segments; ++i) {
        float angle1 = i * angleStep;
        float angle2 = (i + 1) * angleStep;
        
        addVertex(center.x, center.y, 0.5f, 0.5f, color);
        addVertex(center.x + std::cos(angle1) * radius, center.y + std::sin(angle1) * radius, 0, 0, color);
        addVertex(center.x + std::cos(angle2) * radius, center.y + std::sin(angle2) * radius, 1, 1, color);
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
    // Simple line as thin quad
    float dx = end.x - start.x;
    float dy = end.y - start.y;
    float len = std::sqrt(dx * dx + dy * dy);
    
    if (len < 0.001f) return;
    
    float nx = -dy / len * thickness * 0.5f;
    float ny = dx / len * thickness * 0.5f;
    
    addVertex(start.x + nx, start.y + ny, 0, 0, color);
    addVertex(start.x - nx, start.y - ny, 0, 1, color);
    addVertex(end.x - nx, end.y - ny, 1, 1, color);
    addVertex(end.x + nx, end.y + ny, 1, 0, color);
    
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
    if (count < 2) return;
    
    ensureBasicPrimitive();
    
    // Build triangle strip: top[0], bottom[0], top[1], bottom[1], ...
    // This creates a filled shape between the top and bottom edges
    uint32_t baseIndex = static_cast<uint32_t>(vertices_.size());
    
    // Add all vertices - addVertex handles transform internally
    for (int i = 0; i < count; ++i) {
        addVertex(topPoints[i].x, topPoints[i].y, 0, 0, color);
        addVertex(bottomPoints[i].x, bottomPoints[i].y, 0, 1, color);
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
    
    // Flush if state changes
    if (currentPrimitiveType_ != 1 || 
        currentRadius_ != 8.0f || // Default radius for shadows
        currentBlur_ != blur ||
        currentSize_.width != rect.width || 
        currentSize_.height != rect.height ||
        currentQuadSize_.width != shadowQuad.width ||
        currentQuadSize_.height != shadowQuad.height) {
        flush();
    }
    
    currentPrimitiveType_ = 1;
    currentRadius_ = 8.0f;
    currentBlur_ = blur;
    currentSize_ = {rect.width, rect.height};
    currentQuadSize_ = {shadowQuad.width, shadowQuad.height};
    
    // Use the passed color (alpha should be handled by caller or config)
    addQuad(shadowQuad, color);
}

// ============================================================================
// Text Rendering
// ============================================================================

void NUIRendererGL::drawText(const std::string& text, const NUIPoint& position, float fontSize, const NUIColor& color) {
    if (!useSDFText_ && !triedSDFInit_ && sdfRenderer_ && !defaultFontPath_.empty()) {
        useSDFText_ = sdfRenderer_->initialize(defaultFontPath_, 64.0f);
        triedSDFInit_ = true;
    }

    if (useSDFText_ && sdfRenderer_ && sdfRenderer_->isInitialized()) {
        // Apply transform stack to position (critical for FBO cache rendering)
        float transformedX = position.x;
        float transformedY = position.y;
        applyTransform(transformedX, transformedY);
        
        // Convert Top-Left (UI) to Baseline (SDF)
        float dpiScale = getDPIScale();
        float ascent = sdfRenderer_->getAscent(fontSize);
        float alignedY = std::floor(transformedY + ascent + 0.5f);
        float alignedX = std::floor(transformedX + 0.5f);
        
        // Flush pending batch before SDF renderer takes over
        flush();
        
        sdfRenderer_->drawText(text, NUIPoint(alignedX, alignedY), fontSize * dpiScale, color, projectionMatrix_);
        
        // Invalidate renderer state after SDF returns
        currentPrimitiveType_ = -1;
        currentTextureId_ = 0;
        glBindVertexArray(vao_);
        
        return;
    }
    
    // Use real font rendering if available, otherwise fallback to blocky text
    if (fontInitialized_) {
        float dpiScale = getDPIScale();
        FT_Set_Pixel_Sizes(ftFace_, 0, static_cast<FT_UInt>(fontSize * dpiScale));
        float ascent = (ftFace_->size->metrics.ascender >> 6) / dpiScale;
        
        renderTextWithFont(text, NUIPoint(position.x, position.y + ascent), fontSize, color);
    } else {
        // Fallback to blocky text rendering
        float charWidth = fontSize * 0.5f;
        float charHeight = fontSize * 0.8f;
        
        glUseProgram(primitiveShader_.id);
        glBindVertexArray(vao_);
        
        for (size_t i = 0; i < text.length(); ++i) {
            char c = text[i];
            if (c >= 32 && c <= 126) {
                float x = position.x + i * charWidth;
                float y = position.y;
                drawCleanCharacter(c, x, y, charWidth, charHeight, color);
            }
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

// REMOVED: renderCharacterImproved (replaced by atlas rendering)

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
    
    // Calculate horizontal centering
    float x = rect.x + (rect.width - textSize.width) * 0.5f;
    
    // Calculate vertical centering (Top-Left)
    // We standardize on Top-Left coordinates for drawText
    float y = std::floor(rect.y + (rect.height - textSize.height) * 0.5f);
    
    drawText(text, NUIPoint(x, y), fontSize, color);
}

NUISize NUIRendererGL::measureText(const std::string& text, float fontSize) {
    if (!useSDFText_ && !triedSDFInit_ && sdfRenderer_ && !defaultFontPath_.empty()) {
        useSDFText_ = sdfRenderer_->initialize(defaultFontPath_, 64.0f);
        triedSDFInit_ = true;
    }

    if (useSDFText_ && sdfRenderer_ && sdfRenderer_->isInitialized()) {
        float dpiScale = getDPIScale();
        return sdfRenderer_->measureText(text, fontSize * dpiScale);
    }

    if (!fontInitialized_ || text.empty()) {
        return {text.length() * fontSize * 0.6f, fontSize};
    }
    
    try {
        // Use DPI scaling for accurate measurements
        float dpiScale = getDPIScale();
        float actualFontSize = fontSize * dpiScale;
        
        // Set font size for FreeType
        FT_Set_Pixel_Sizes(ftFace_, 0, static_cast<FT_UInt>(actualFontSize));
        
        float totalWidth = 0.0f;
        float maxHeight = 0.0f;
        
        // Measure each character
        for (char c : text) {
            if (FT_Load_Char(ftFace_, c, FT_LOAD_RENDER)) {
                continue; // Skip failed characters
            }
            
            // Get character metrics
            float charWidth = (ftFace_->glyph->advance.x >> 6) / dpiScale;
            float charHeight = (ftFace_->glyph->bitmap_top + ftFace_->glyph->bitmap.rows) / dpiScale;
            
            totalWidth += charWidth;
            maxHeight = std::max(maxHeight, charHeight);
        }
        
        return {totalWidth, maxHeight};
        
    } catch (...) {
        // Fallback to simple estimation
        return {text.length() * fontSize * 0.6f, fontSize};
    }
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
    
    // Set font size (96 pixels for high quality)
    FT_Set_Pixel_Sizes(ftFace_, 0, 96);
    
    // Create Texture Atlas
    // ---------------------------------------------------------
    glGenTextures(1, &fontAtlasTextureId_);
    glBindTexture(GL_TEXTURE_2D, fontAtlasTextureId_);
    
    // Allocate empty texture storage
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RED,
        fontAtlasWidth_,
        fontAtlasHeight_,
        0,
        GL_RED,
        GL_UNSIGNED_BYTE,
        nullptr
    );
    
    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    // Swizzle R to Alpha (so text color works correctly)
    GLint swizzleMask[] = {GL_ONE, GL_ONE, GL_ONE, GL_RED};
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);
    
    // Reset atlas cursor
    fontAtlasX_ = 0;
    fontAtlasY_ = 0;
    fontAtlasRowHeight_ = 0;
    
    // Load characters into atlas
    int loadedChars = 0;
    // Load ASCII + some Latin-1 supplement
    for (unsigned char c = 32; c < 128; c++) {
        if (FT_Load_Char(ftFace_, c, FT_LOAD_RENDER)) {
            std::cerr << "ERROR: Failed to load glyph for character: " << c << std::endl;
            continue;
        }
        
        FT_Bitmap* bitmap = &ftFace_->glyph->bitmap;
        int width = bitmap->width;
        int height = bitmap->rows;
        
        // Add padding to prevent bleeding
        int padding = 2;
        
        // Check if we need to move to next row
        if (fontAtlasX_ + width + padding >= fontAtlasWidth_) {
            fontAtlasX_ = 0;
            fontAtlasY_ += fontAtlasRowHeight_ + padding;
            fontAtlasRowHeight_ = 0;
        }
        
        // Check if atlas is full
        if (fontAtlasY_ + height + padding >= fontAtlasHeight_) {
            std::cerr << "ERROR: Font atlas full!" << std::endl;
            break;
        }
        
        // Upload glyph to atlas
        if (width > 0 && height > 0) {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(
                GL_TEXTURE_2D,
                0,
                fontAtlasX_,
                fontAtlasY_,
                width,
                height,
                GL_RED,
                GL_UNSIGNED_BYTE,
                bitmap->buffer
            );
        }
        
        // Store character data
        FontData charData;
        charData.textureId = fontAtlasTextureId_;
        charData.width = width;
        charData.height = height;
        charData.bearingX = ftFace_->glyph->bitmap_left;
        charData.bearingY = ftFace_->glyph->bitmap_top;
        charData.advance = ftFace_->glyph->advance.x;
        
        // Calculate UVs
        float invW = 1.0f / fontAtlasWidth_;
        float invH = 1.0f / fontAtlasHeight_;
        
        charData.u0 = fontAtlasX_ * invW;
        charData.v0 = fontAtlasY_ * invH;
        charData.u1 = (fontAtlasX_ + width) * invW;
        charData.v1 = (fontAtlasY_ + height) * invH;
        
        fontCache_[c] = charData;
        
        // Advance cursor
        fontAtlasX_ += width + padding;
        fontAtlasRowHeight_ = std::max(fontAtlasRowHeight_, height);
        
        loadedChars++;
    }
    
    fontInitialized_ = true;
    std::cout << "âœ“ Font loaded successfully: " << fontPath << " (" << loadedChars << " characters in atlas)" << std::endl;
    return true;
}

void NUIRendererGL::renderTextWithFont(const std::string& text, const NUIPoint& position, float fontSize, const NUIColor& color) {
    NOMAD_ZONE("Text_Render");
    if (!fontInitialized_) {
        drawText(text, position, fontSize, color);
        return;
    }
    
    // Ensure we are using the font atlas texture
    if (currentTextureId_ != fontAtlasTextureId_) {
        flush();
        currentTextureId_ = fontAtlasTextureId_;
    }
    
    float dpiScale = getDPIScale();
    float scale = (fontSize * dpiScale) / 96.0f; // 96 is our atlas font size
    
    float x = position.x;
    float y = position.y;
    
    for (char c : text) {
        if (fontCache_.find(c) == fontCache_.end()) continue;
        
        const FontData& ch = fontCache_[c];
        
        float xpos = x + ch.bearingX * scale;
        float ypos = y - ch.bearingY * scale; // Correct baseline alignment
        
        float w = ch.width * scale;
        float h = ch.height * scale;
        
        // Draw textured quad for character
        // Note: We use the atlas UVs
        // Fix: Map Top-Left Screen (y) to Top-Texture (v0)
        // Vertices are: BL, BR, TR, TL (CCW winding)
        
        // 0: Bottom-Left (x, y+h) -> v1 (Bottom)
        addVertex(xpos,     ypos + h, ch.u0, ch.v1, color); 
        // 1: Bottom-Right (x+w, y+h) -> v1 (Bottom)
        addVertex(xpos + w, ypos + h, ch.u1, ch.v1, color); 
        // 2: Top-Right (x+w, y) -> v0 (Top)
        addVertex(xpos + w, ypos,     ch.u1, ch.v0, color); 
        // 3: Top-Left (x, y) -> v0 (Top)
        addVertex(xpos,     ypos,     ch.u0, ch.v0, color);
        
        // Add indices for quad
        uint32_t base = static_cast<uint32_t>(vertices_.size()) - 4;
        indices_.push_back(base + 0);
        indices_.push_back(base + 1);
        indices_.push_back(base + 2);
        indices_.push_back(base + 0);
        indices_.push_back(base + 2);
        indices_.push_back(base + 3);
        
        // Advance cursor
        x += (ch.advance >> 6) * scale;
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
    updateProjectionMatrix();
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
        glUniform1f(primitiveShader_.radiusLoc, currentRadius_);
        glUniform1f(primitiveShader_.blurLoc, currentBlur_);
        glUniform1f(primitiveShader_.rectWidthLoc, currentSize_.width);
        glUniform1f(primitiveShader_.rectHeightLoc, currentSize_.height);
        glUniform1f(primitiveShader_.quadWidthLoc, currentQuadSize_.width);
        glUniform1f(primitiveShader_.quadHeightLoc, currentQuadSize_.height);
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
    primitiveShader_.radiusLoc = glGetUniformLocation(primitiveShader_.id, "uRadius");
    primitiveShader_.blurLoc = glGetUniformLocation(primitiveShader_.id, "uBlur");
    primitiveShader_.rectWidthLoc = glGetUniformLocation(primitiveShader_.id, "uRectWidth");
    primitiveShader_.rectHeightLoc = glGetUniformLocation(primitiveShader_.id, "uRectHeight");
    primitiveShader_.quadWidthLoc = glGetUniformLocation(primitiveShader_.id, "uQuadWidth");
    primitiveShader_.quadHeightLoc = glGetUniformLocation(primitiveShader_.id, "uQuadHeight");
    primitiveShader_.textureLoc = glGetUniformLocation(primitiveShader_.id, "uTexture");
    primitiveShader_.useTextureLoc = glGetUniformLocation(primitiveShader_.id, "uUseTexture");
    
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

void NUIRendererGL::addVertex(float x, float y, float u, float v, const NUIColor& color) {
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
    
    vertices_.push_back(vertex);
}

void NUIRendererGL::addQuad(const NUIRect& rect, const NUIColor& color) {
    // Check if we need to switch to non-textured mode (batch breaking)
    if (currentTextureId_ != 0) {
        flush();
        currentTextureId_ = 0;
    }

    addVertex(rect.x, rect.y, 0.0f, 0.0f, color);
    addVertex(rect.right(), rect.y, 1.0f, 0.0f, color);
    addVertex(rect.right(), rect.bottom(), 1.0f, 1.0f, color);
    addVertex(rect.x, rect.bottom(), 0.0f, 1.0f, color);
    
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
