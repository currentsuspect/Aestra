// © 2025 Nomad Studios. All Rights Reserved. Licensed for personal & educational use only.
#include "../External/glad/include/glad/glad.h"  // MUST be first
#define STB_TRUETYPE_IMPLEMENTATION
#include "../External/stb_truetype.h"
#include "NUITextRendererSTB.h"
#include <fstream>
#include <iostream>
#include <vector>

namespace NomadUI {

NUITextRendererSTB::NUITextRendererSTB()
    : initialized_(false)
    , fontBuffer_(nullptr)
    , vao_(0)
    , vbo_(0)
    , fontSize_(16.0f)
{
}

NUITextRendererSTB::~NUITextRendererSTB() {
    shutdown();
}

bool NUITextRendererSTB::initialize(const std::string& fontPath, float fontSize) {
    fontSize_ = fontSize;

    if (!loadFont(fontPath)) {
        return false;
    }

    bakeGlyphs(fontSize);

    // Create VAO and VBO
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    initialized_ = true;
    std::cout << "âœ“ STB Text Renderer initialized with font: " << fontPath << std::endl;
    return true;
}

void NUITextRendererSTB::shutdown() {
    for (auto& pair : glyphs_) {
        glDeleteTextures(1, &pair.second.textureId);
    }
    glyphs_.clear();

    if (vbo_) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }

    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }

    if (fontBuffer_) {
        delete[] fontBuffer_;
        fontBuffer_ = nullptr;
    }

    initialized_ = false;
}

bool NUITextRendererSTB::loadFont(const std::string& fontPath) {
    std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Failed to open font file: " << fontPath << std::endl;
        return false;
    }

    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    fontBuffer_ = new unsigned char[fileSize];
    file.read(reinterpret_cast<char*>(fontBuffer_), fileSize);
    file.close();

    return true;
}

void NUITextRendererSTB::bakeGlyphs(float fontSize) {
    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, fontBuffer_, 0)) {
        std::cerr << "Failed to initialize STB font" << std::endl;
        return;
    }

    float scale = stbtt_ScaleForPixelHeight(&font, fontSize);

    // Bake ASCII characters
    for (int c = 32; c < 127; c++) {
        int width, height, xoff, yoff;
        unsigned char* bitmap = stbtt_GetCodepointBitmap(&font, 0, scale, c, &width, &height, &xoff, &yoff);

        if (!bitmap) continue;

        // Create OpenGL texture
        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, bitmap);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        int advance, lsb;
        stbtt_GetCodepointHMetrics(&font, c, &advance, &lsb);

        GlyphInfo glyph;
        glyph.textureId = texture;
        glyph.width = width;
        glyph.height = height;
        glyph.bearingX = xoff;
        glyph.bearingY = yoff;
        glyph.advance = advance * scale;

        glyphs_[c] = glyph;

        stbtt_FreeBitmap(bitmap, nullptr);
    }
}

void NUITextRendererSTB::renderText(
    const std::string& text,
    float x, float y,
    const NUIColor& color,
    uint32_t shaderProgram,
    const float* projectionMatrix)
{
    if (!initialized_) return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(shaderProgram);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "uProjection"), 1, GL_FALSE, projectionMatrix);
    glUniform4f(glGetUniformLocation(shaderProgram, "uColor"), color.r, color.g, color.b, color.a);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao_);

    for (char c : text) {
        if (glyphs_.find(c) == glyphs_.end()) continue;

        const GlyphInfo& glyph = glyphs_[c];

        float xpos = x + glyph.bearingX;
        float ypos = y + glyph.bearingY;
        float w = glyph.width;
        float h = glyph.height;

        float vertices[6][4] = {
            { xpos,     ypos + h,   0.0f, 1.0f },
            { xpos,     ypos,       0.0f, 0.0f },
            { xpos + w, ypos,       1.0f, 0.0f },

            { xpos,     ypos + h,   0.0f, 1.0f },
            { xpos + w, ypos,       1.0f, 0.0f },
            { xpos + w, ypos + h,   1.0f, 1.0f }
        };

        glBindTexture(GL_TEXTURE_2D, glyph.textureId);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        x += glyph.advance;
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

NUISize NUITextRendererSTB::measureText(const std::string& text) {
    float width = 0.0f;
    float height = fontSize_;

    for (char c : text) {
        if (glyphs_.find(c) != glyphs_.end()) {
            width += glyphs_[c].advance;
        }
    }

    return { width, height };
}

} // namespace NomadUI
