// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUITextRendererSDF.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif
#include "../External/glad/include/glad/glad.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "../External/stb_truetype.h"

#include <vector>
#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>

namespace NomadUI {

namespace {
const char* kVertexSrc = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;

out vec2 vUV;
out vec4 vColor;

uniform mat4 uProjection;

void main() {
    vUV = aUV;
    vColor = aColor;
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
}
)";

const char* kFragmentSrc = R"(
#version 330 core
in vec2 vUV;
in vec4 vColor;
out vec4 FragColor;

uniform sampler2D uTexture;
uniform float uSmoothness;

void main() {
    float dist = texture(uTexture, vUV).r;
    // Derivative-based smoothing to stay crisp at any scale
    float width = max(fwidth(dist) * uSmoothness, 1e-4);
    float alpha = smoothstep(0.5 - width, 0.5 + width, dist);
    FragColor = vec4(vColor.rgb, vColor.a * alpha);
}
)";
} // namespace

NUITextRendererSDF::NUITextRendererSDF() = default;

NUITextRendererSDF::~NUITextRendererSDF() {
    shutdown();
}

bool NUITextRendererSDF::initialize(const std::string& fontPath, float fontSize) {
    if (initialized_) return true;
    baseFontSize_ = fontSize;
    
    // Adaptive resolution based on font size for optimal crispness
    if (fontSize <= 12.0f) {
        atlasFontSize_ = fontSize * 4.0f;  // 4x for small fonts
    } else if (fontSize <= 24.0f) {
        atlasFontSize_ = fontSize * 3.0f;  // 3x for medium fonts
    } else {
        atlasFontSize_ = fontSize * 2.5f;  // 2.5x for large fonts
    }
    
    std::cout << "MSDF atlas: " << fontSize << "px -> " << atlasFontSize_ << "px (" 
              << (atlasFontSize_/fontSize) << "x scale)" << std::endl;

    if (!createShader()) {
        std::cerr << "MSDF shader compile failed" << std::endl;
        return false;
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(4 * sizeof(float)));
    glBindVertexArray(0);

    if (!loadFontAtlas(fontPath, atlasFontSize_)) {
        std::cerr << "Failed to build MSDF atlas" << std::endl;
        return false;
    }

    initialized_ = true;
    std::cout << "✓ MSDF text renderer ready" << std::endl;
    return true;
}

void NUITextRendererSDF::shutdown() {
    if (atlasTexture_) {
        glDeleteTextures(1, &atlasTexture_);
        atlasTexture_ = 0;
    }
    if (vbo_) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (ebo_) {
        glDeleteBuffers(1, &ebo_);
        ebo_ = 0;
    }
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    if (shaderProgram_) {
        glDeleteProgram(shaderProgram_);
        shaderProgram_ = 0;
    }
    glyphs_.clear();
    initialized_ = false;
}

void NUITextRendererSDF::drawText(const std::string& text,
                                  const NUIPoint& position,
                                  float fontSize,
                                  const NUIColor& color,
                                  const float* projection) {
    if (!initialized_) return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(shaderProgram_);
    GLint smoothLoc = glGetUniformLocation(shaderProgram_, "uSmoothness");
    if (smoothLoc >= 0) {
        // Adaptive smoothing based on font size for crisp text
        float adaptiveSmoothness;
        if (fontSize <= 12.0f) {
            adaptiveSmoothness = 0.4f;  // Less smoothing for small fonts
        } else if (fontSize <= 24.0f) {
            adaptiveSmoothness = 0.6f;  // Moderate smoothing for medium fonts
        } else {
            adaptiveSmoothness = 0.8f;  // More smoothing for large fonts
        }
        glUniform1f(smoothLoc, adaptiveSmoothness);
    }
    GLint projLoc = glGetUniformLocation(shaderProgram_, "uProjection");
    if (projLoc >= 0) {
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, projection);
    }
    GLint texLoc = glGetUniformLocation(shaderProgram_, "uTexture");
    if (texLoc >= 0) {
        glUniform1i(texLoc, 0);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlasTexture_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);

    // Incoming position.y is the baseline (matches FreeType path)
    float scale = fontSize / atlasFontSize_;
    // Snap positions to pixel grid to keep edges crisp
    float penX = std::floor(position.x + 0.5f);
    float baseline = std::floor(position.y + 0.5f);

    std::vector<float> verts;
    std::vector<unsigned int> indices;
    verts.reserve(text.size() * 4 * 8);
    indices.reserve(text.size() * 6);

    for (char c : text) {
        if (c == ' ') {
            // Special handling for space character - always advance the pen
            auto spaceIt = glyphs_.find(' ');
            if (spaceIt != glyphs_.end()) {
                penX += spaceIt->second.advance * scale;
            } else {
                // Fallback: use quarter font-size spacing for missing space glyph
                penX += fontSize * 0.25f * scale;
            }
            continue;
        }
        
        auto it = glyphs_.find(c);
        if (it == glyphs_.end()) {
            // For missing non-space characters, still advance by some amount
            penX += fontSize * 0.5f * scale;
            continue;
        }
        
        const GlyphData& g = it->second;

        float xpos = penX + g.bearingX * scale;
        float ypos = baseline + g.bearingY * scale;
        float w = g.width * scale;
        float h = g.height * scale;

        float x0 = xpos;
        float y0 = ypos;
        float x1 = xpos + w;
        float y1 = ypos + h;

        // order: top-left, bottom-left, bottom-right, top-right
        float quad[4][8] = {
            {x0, y0, g.u0, g.v0, color.r, color.g, color.b, color.a},
            {x0, y1, g.u0, g.v1, color.r, color.g, color.b, color.a},
            {x1, y1, g.u1, g.v1, color.r, color.g, color.b, color.a},
            {x1, y0, g.u1, g.v0, color.r, color.g, color.b, color.a}
        };
        verts.insert(verts.end(), &quad[0][0], &quad[0][0] + 4 * 8);
        unsigned int startIdx = static_cast<unsigned int>(verts.size() / 8) - 4;
        indices.push_back(startIdx + 0);
        indices.push_back(startIdx + 1);
        indices.push_back(startIdx + 2);
        indices.push_back(startIdx + 0);
        indices.push_back(startIdx + 2);
        indices.push_back(startIdx + 3);

        penX += g.advance * scale;
    }

    if (!verts.empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_DYNAMIC_DRAW);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, 0);
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

NUISize NUITextRendererSDF::measureText(const std::string& text, float fontSize) const {
    if (!initialized_) return {0, 0};
    float scale = fontSize / atlasFontSize_;
    float width = 0.0f;
    float height = (ascent_ - descent_) * scale;
    for (char c : text) {
        if (c == ' ') {
            // Special handling for space character - always advance the pen
            auto spaceIt = glyphs_.find(' ');
            if (spaceIt != glyphs_.end()) {
                width += spaceIt->second.advance * scale;
            } else {
                // Fallback: use quarter font-size spacing for missing space glyph
                width += fontSize * 0.25f * scale;
            }
            continue;
        }
        
        auto it = glyphs_.find(c);
        if (it == glyphs_.end()) {
            // For missing non-space characters, still advance by some amount
            width += fontSize * 0.5f * scale;
            continue;
        }
        
        width += it->second.advance * scale;
        height = std::max(height, it->second.height * scale);
    }
    return {width, height};
}

float NUITextRendererSDF::getAscent(float fontSize) const {
    if (!initialized_) return 0.0f;
    float scale = fontSize / atlasFontSize_;
    return ascent_ * scale;
}

bool NUITextRendererSDF::createShader() {
    auto compile = [](const char* src, GLenum type) -> GLuint {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        GLint status;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
        if (!status) {
            char log[512];
            glGetShaderInfoLog(shader, 512, nullptr, log);
            std::cerr << "MSDF shader compile failed: " << log << std::endl;
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    };

    GLuint vs = compile(kVertexSrc, GL_VERTEX_SHADER);
    GLuint fs = compile(kFragmentSrc, GL_FRAGMENT_SHADER);
    if (!vs || !fs) return false;

    shaderProgram_ = glCreateProgram();
    glAttachShader(shaderProgram_, vs);
    glAttachShader(shaderProgram_, fs);
    glLinkProgram(shaderProgram_);

    GLint status;
    glGetProgramiv(shaderProgram_, GL_LINK_STATUS, &status);
    if (!status) {
        char log[512];
        glGetProgramInfoLog(shaderProgram_, 512, nullptr, log);
        std::cerr << "MSDF shader link failed: " << log << std::endl;
        glDeleteProgram(shaderProgram_);
        shaderProgram_ = 0;
        glDeleteShader(vs);
        glDeleteShader(fs);
        return false;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return true;
}

bool NUITextRendererSDF::loadFontAtlas(const std::string& fontPath, float fontSize) {
    std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Unable to open font for MSDF: " << fontPath << std::endl;
        return false;
    }
    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> buffer(fileSize);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    file.close();

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, buffer.data(), 0)) {
        std::cerr << "stbtt_InitFont failed for " << fontPath << std::endl;
        return false;
    }

    float scale = stbtt_ScaleForPixelHeight(&font, fontSize);
    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
    // Store scaled metrics in atlas pixel space so we can downscale accurately later
    ascent_ = static_cast<float>(ascent) * scale;
    descent_ = static_cast<float>(descent) * scale; // descent is negative
    int padding = 8;
    
    // Adaptive SDF parameters optimized for crisp text rendering
    float onedge_value, pixel_dist_scale;
    if (fontSize <= 12.0f) {
        onedge_value = 96.0f;      // Lower value = sharper edges for small fonts
        pixel_dist_scale = 150.0f; // Higher scale = better gradient for small fonts
    } else if (fontSize <= 24.0f) {
        onedge_value = 128.0f;     // Balanced for medium fonts
        pixel_dist_scale = 120.0f;
    } else {
        onedge_value = 160.0f;     // Smoother for large fonts
        pixel_dist_scale = 100.0f;
    }
    
    std::cout << "SDF params: onedge=" << onedge_value << ", dist_scale=" << pixel_dist_scale << std::endl;

    int atlasW = static_cast<int>(atlasWidth_);
    int atlasH = static_cast<int>(atlasHeight_);
    std::vector<unsigned char> atlas(atlasW * atlasH, 0);

    int penX = 0;
    int penY = 0;
    int rowHeight = 0;

    glyphs_.clear();

    for (int c = 32; c < 127; ++c) {
        int w, h, xoff, yoff;
        unsigned char* sdf = stbtt_GetCodepointSDF(&font, scale, c, padding, onedge_value, pixel_dist_scale, &w, &h, &xoff, &yoff);
        if (!sdf) continue;

        if (penX + w >= atlasW) {
            penX = 0;
            penY += rowHeight + 1;
            rowHeight = 0;
        }
        if (penY + h >= atlasH) {
            stbtt_FreeSDF(sdf, nullptr);
            break;
        }

        for (int y = 0; y < h; ++y) {
            std::memcpy(&atlas[(penY + y) * atlasW + penX], sdf + y * w, w);
        }

        GlyphData g;
        g.u0 = static_cast<float>(penX) / atlasW;
        g.v0 = static_cast<float>(penY) / atlasH;
        g.u1 = static_cast<float>(penX + w) / atlasW;
        g.v1 = static_cast<float>(penY + h) / atlasH;
        g.width = static_cast<float>(w);
        g.height = static_cast<float>(h);
        g.bearingX = static_cast<float>(xoff);
        g.bearingY = static_cast<float>(yoff);
        int advance, lsb;
        stbtt_GetCodepointHMetrics(&font, c, &advance, &lsb);
        g.advance = advance * scale;
        glyphs_[static_cast<char>(c)] = g;

        penX += w + 1;
        rowHeight = std::max(rowHeight, h);

        stbtt_FreeSDF(sdf, nullptr);
    }

    // Ensure space character exists in atlas
    auto spaceIt = glyphs_.find(' ');
    if (spaceIt == glyphs_.end()) {
        std::cerr << "WARNING: Space character not found in font atlas! Creating fallback." << std::endl;
        // Create a manual space glyph with default spacing
        GlyphData spaceGlyph{};
        spaceGlyph.width = 0.0f;
        spaceGlyph.height = 0.0f;
        spaceGlyph.bearingX = 0.0f;
        spaceGlyph.bearingY = 0.0f;
        spaceGlyph.advance = static_cast<float>(fontSize * 0.25f); // Quarter font-size spacing
        spaceGlyph.u0 = spaceGlyph.u1 = spaceGlyph.v0 = spaceGlyph.v1 = 0.0f;
        glyphs_[' '] = spaceGlyph;
    }

    glGenTextures(1, &atlasTexture_);
    glBindTexture(GL_TEXTURE_2D, atlasTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, atlasW, atlasH, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GLint swizzle[] = {GL_RED, GL_RED, GL_RED, GL_RED};
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);

    atlasWidth_ = static_cast<float>(atlasW);
    atlasHeight_ = static_cast<float>(atlasH);
    return true;
}

} // namespace NomadUI
