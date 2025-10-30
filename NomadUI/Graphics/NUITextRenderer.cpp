// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUITextRenderer.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <glad/glad.h>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #define NOCOMM
    #include <Windows.h>
#endif

// GLAD must be included after Windows headers to avoid macro conflicts
#include <glad/glad.h>

// Suppress APIENTRY redefinition warning - both define the same value
#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable: 4005)
// Windows.h redefines APIENTRY but it's the same value, so we can ignore the warning
#pragma warning(pop)
#endif

namespace NomadUI {

// ============================================================================
// Shader Sources
// ============================================================================

const char* NUITextRenderer::vertexShaderSource_ = R"(
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

const char* NUITextRenderer::fragmentShaderSource_ = R"(
#version 330 core
in vec2 vTexCoord;
in vec4 vColor;

out vec4 FragColor;

uniform sampler2D uTexture;

void main() {
    // Try different sampling methods
    vec4 texColor = texture(uTexture, vTexCoord);
    
    // Method 1: Use red channel as alpha
    float alpha = texColor.r;
    
    // Method 2: Use luminance as alpha (fallback)
    if (alpha < 0.1) {
        alpha = (texColor.r + texColor.g + texColor.b) / 3.0;
    }
    
    // Method 3: Use all channels for better visibility
    alpha = max(alpha, texColor.g * 0.5);
    alpha = max(alpha, texColor.b * 0.3);
    
    FragColor = vec4(vColor.rgb, vColor.a * alpha);
}
)";

// ============================================================================
// Constructor / Destructor
// ============================================================================

NUITextRenderer::NUITextRenderer()
    : vao_(0)
    , vbo_(0)
    , ebo_(0)
    , shaderProgram_(0)
    , projectionLoc_(-1)
    , textureLoc_(-1)
    , batching_(false)
    , opacity_(1.0f)
{
}

NUITextRenderer::~NUITextRenderer() {
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================

bool NUITextRenderer::initialize() {
    if (!loadShaders()) {
        std::cerr << "Failed to load text shaders" << std::endl;
        return false;
    }
    
    createBuffers();
    
    std::cout << "âœ“ Text renderer initialized" << std::endl;
    return true;
}

void NUITextRenderer::shutdown() {
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
    
    if (shaderProgram_) {
        glDeleteProgram(shaderProgram_);
        shaderProgram_ = 0;
    }
}

// ============================================================================
// Rendering
// ============================================================================

void NUITextRenderer::drawText(
    const std::string& text,
    std::shared_ptr<NUIFont> font,
    const NUIPoint& position,
    const NUIColor& color)
{
    if (!font || !font->isLoaded()) {
        return;
    }
    
    float x = position.x;
    float y = position.y;
    uint32_t prevChar = 0;
    
    for (char c : text) {
        if (c == '\n') {
            y += font->getLineHeight();
            x = position.x;
            prevChar = 0;
            continue;
        }
        
        uint32_t character = static_cast<uint32_t>(c);
        
        // Get glyph
        const NUIGlyph* glyph = font->getGlyph(character);
        if (!glyph) continue;
        
        // Apply kerning
        if (prevChar != 0) {
            x += font->getKerning(prevChar, character);
        }
        
        // Calculate glyph position
        float xpos = x + glyph->bearingX;
        // Convert from screen coordinates (Y=0 at top) to OpenGL coordinates (Y=0 at bottom)
        // We need to flip the Y coordinate since our projection matrix has Y=0 at top
        float ypos = y + font->getAscender() - glyph->bearingY;
        
        // Add glyph to batch
        if (glyph->textureID != 0) {  // Skip glyphs without texture (e.g., space)
            addGlyph(
                glyph->textureID,
                xpos, ypos,
                static_cast<float>(glyph->width),
                static_cast<float>(glyph->height),
                glyph->u0, glyph->v0,
                glyph->u1, glyph->v1,
                color
            );
        }
        
        // Advance cursor
        x += glyph->advance;
        prevChar = character;
    }
}

void NUITextRenderer::drawTextAligned(
    const std::string& text,
    std::shared_ptr<NUIFont> font,
    const NUIRect& rect,
    const NUIColor& color,
    Alignment hAlign,
    VerticalAlignment vAlign)
{
    if (!font) return;
    
    // Measure text
    NUISize textSize = measureText(text, font);
    
    // Calculate position based on alignment
    float x = rect.x;
    float y = rect.y;
    
    // Horizontal alignment
    switch (hAlign) {
        case Alignment::Left:
            x = rect.x;
            break;
        case Alignment::Center:
            x = rect.x + (rect.width - textSize.width) * 0.5f;
            break;
        case Alignment::Right:
            x = rect.x + rect.width - textSize.width;
            break;
    }
    
    // Vertical alignment
    switch (vAlign) {
        case VerticalAlignment::Top:
            y = rect.y;
            break;
        case VerticalAlignment::Middle:
            y = rect.y + (rect.height - textSize.height) * 0.5f;
            break;
        case VerticalAlignment::Bottom:
            y = rect.y + rect.height - textSize.height;
            break;
    }
    
    drawText(text, font, {x, y}, color);
}

void NUITextRenderer::drawTextMultiline(
    const std::string& text,
    std::shared_ptr<NUIFont> font,
    const NUIRect& rect,
    const NUIColor& color,
    float lineSpacing)
{
    if (!font) return;
    
    std::vector<std::string> lines = splitLines(text);
    float lineHeight = font->getLineHeight() * lineSpacing;
    float y = rect.y;
    
    for (const auto& line : lines) {
        if (y + lineHeight > rect.y + rect.height) break;
        
        drawText(line, font, {rect.x, y}, color);
        y += lineHeight;
    }
}

void NUITextRenderer::drawTextWithShadow(
    const std::string& text,
    std::shared_ptr<NUIFont> font,
    const NUIPoint& position,
    const NUIColor& color,
    const NUIColor& shadowColor,
    float shadowOffsetX,
    float shadowOffsetY)
{
    // Draw shadow first
    drawText(text, font, {position.x + shadowOffsetX, position.y + shadowOffsetY}, shadowColor);
    
    // Draw main text
    drawText(text, font, position, color);
}

// ============================================================================
// Measurement
// ============================================================================

NUISize NUITextRenderer::measureText(const std::string& text, std::shared_ptr<NUIFont> font) {
    if (!font || !font->isLoaded()) {
        return {0, 0};
    }
    
    float width = font->measureText(text);
    float height = static_cast<float>(font->getLineHeight());
    
    return {width, height};
}

NUISize NUITextRenderer::measureTextMultiline(
    const std::string& text,
    std::shared_ptr<NUIFont> font,
    float maxWidth,
    float lineSpacing)
{
    if (!font) return {0, 0};
    
    std::vector<std::string> lines = splitLines(text);
    float width = 0;
    float height = 0;
    
    for (const auto& line : lines) {
        float lineWidth = font->measureText(line);
        width = std::max(width, lineWidth);
    }
    
    height = lines.size() * font->getLineHeight() * lineSpacing;
    
    return {width, height};
}

// ============================================================================
// Batching
// ============================================================================

void NUITextRenderer::beginBatch(const float* projectionMatrix) {
    batching_ = true;
    batches_.clear();
    
    // Setup OpenGL state
    glUseProgram(shaderProgram_);
    glUniformMatrix4fv(projectionLoc_, 1, GL_FALSE, projectionMatrix);
    glUniform1i(textureLoc_, 0);
    
    // Ensure proper OpenGL state for text rendering
    glDisable(GL_DEPTH_TEST);  // Disable depth testing for text
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void NUITextRenderer::endBatch() {
    flush();
    batching_ = false;
}

void NUITextRenderer::flush() {
    if (batches_.empty()) {
        return;
    }
    
    glBindVertexArray(vao_);
    
    // Render each batch (grouped by texture)
    for (auto& [textureID, batch] : batches_) {
        if (batch.vertices.empty()) continue;
        
        // Upload vertex data
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, 
                     batch.vertices.size() * sizeof(TextVertex),
                     batch.vertices.data(),
                     GL_DYNAMIC_DRAW);
        
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     batch.indices.size() * sizeof(uint32_t),
                     batch.indices.data(),
                     GL_DYNAMIC_DRAW);
        
        // Bind texture
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureID);
        
        // Draw
        glDrawElements(GL_TRIANGLES, 
                      static_cast<GLsizei>(batch.indices.size()),
                      GL_UNSIGNED_INT,
                      0);
    }
    
    glBindVertexArray(0);
    batches_.clear();
}

// ============================================================================
// Private Helpers
// ============================================================================

bool NUITextRenderer::loadShaders() {
    uint32_t vertShader = compileShader(vertexShaderSource_, GL_VERTEX_SHADER);
    uint32_t fragShader = compileShader(fragmentShaderSource_, GL_FRAGMENT_SHADER);
    
    if (!vertShader || !fragShader) {
        return false;
    }
    
    shaderProgram_ = linkProgram(vertShader, fragShader);
    
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);
    
    if (!shaderProgram_) {
        return false;
    }
    
    // Get uniform locations
    projectionLoc_ = glGetUniformLocation(shaderProgram_, "uProjection");
    textureLoc_ = glGetUniformLocation(shaderProgram_, "uTexture");
    
    return true;
}

void NUITextRenderer::createBuffers() {
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    
    // Position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex),
                         (void*)offsetof(TextVertex, x));
    
    // TexCoord
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex),
                         (void*)offsetof(TextVertex, u));
    
    // Color
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(TextVertex),
                         (void*)offsetof(TextVertex, r));
    
    glBindVertexArray(0);
}

uint32_t NUITextRenderer::compileShader(const char* source, uint32_t type) {
    uint32_t shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    
    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::cerr << "Shader compilation failed: " << infoLog << std::endl;
        return 0;
    }
    
    return shader;
}

uint32_t NUITextRenderer::linkProgram(uint32_t vertexShader, uint32_t fragmentShader) {
    uint32_t program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    
    int success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        std::cerr << "Shader linking failed: " << infoLog << std::endl;
        return 0;
    }
    
    return program;
}

void NUITextRenderer::addGlyph(
    uint32_t textureID,
    float x, float y,
    float width, float height,
    float u0, float v0,
    float u1, float v1,
    const NUIColor& color)
{
    // Get or create batch for this texture
    TextBatch& batch = batches_[textureID];
    
    // Add vertices
    uint32_t base = static_cast<uint32_t>(batch.vertices.size());
    
    NUIColor finalColor = color;
    finalColor.a *= opacity_;
    
    batch.vertices.push_back({x, y, u0, v0, finalColor.r, finalColor.g, finalColor.b, finalColor.a});
    batch.vertices.push_back({x + width, y, u1, v0, finalColor.r, finalColor.g, finalColor.b, finalColor.a});
    batch.vertices.push_back({x + width, y + height, u1, v1, finalColor.r, finalColor.g, finalColor.b, finalColor.a});
    batch.vertices.push_back({x, y + height, u0, v1, finalColor.r, finalColor.g, finalColor.b, finalColor.a});
    
    // Add indices
    batch.indices.push_back(base + 0);
    batch.indices.push_back(base + 1);
    batch.indices.push_back(base + 2);
    batch.indices.push_back(base + 0);
    batch.indices.push_back(base + 2);
    batch.indices.push_back(base + 3);
}

std::vector<std::string> NUITextRenderer::splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    
    return lines;
}

std::vector<std::string> NUITextRenderer::wrapText(
    const std::string& text,
    std::shared_ptr<NUIFont> font,
    float maxWidth)
{
    // TODO: Implement word wrapping
    return splitLines(text);
}

} // namespace NomadUI

