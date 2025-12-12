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
#include "../../NomadCore/include/NomadLog.h"

namespace NomadUI {

// Shader strings removed - using unified renderer shader


NUITextRendererSDF::NUITextRendererSDF() = default;

NUITextRendererSDF::~NUITextRendererSDF() {
    shutdown();
}

bool NUITextRendererSDF::initialize(const std::string& fontPath, float fontSize) {
    if (initialized_) return true;
    baseFontSize_ = fontSize;
    
    // High resolution atlas for ULTRA crisp text at all sizes
    // Generate glyphs at 4x requested size for maximum quality
    atlasFontSize_ = fontSize * 4.0f;
    
    // Clamp to reasonable range (64px minimum for quality, 320px max for memory)
    atlasFontSize_ = std::max(64.0f, std::min(atlasFontSize_, 320.0f));
    
    NOMAD_LOG_STREAM_INFO << "SDF atlas: " << fontSize << "px -> " << atlasFontSize_ << "px (" 
                          << (atlasFontSize_/fontSize) << "x scale)";

    if (!loadFontAtlas(fontPath, atlasFontSize_)) {
        std::cerr << "Failed to build SDF atlas" << std::endl;
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
    // Legacy cleanup removed

    glyphs_.clear();
    initialized_ = false;
}

bool NUITextRendererSDF::generateMesh(const std::string& text,
                                      const NUIPoint& position,
                                      float fontSize,
                                      const NUIColor& color,
                                      std::vector<float>& outVertices,
                                      std::vector<unsigned int>& outIndices,
                                      uint32_t vertexOffset) {
    if (!initialized_) return false;

    // Incoming position.y is the baseline (matches FreeType path)
    float scale = fontSize / atlasFontSize_;
    // Snap positions to pixel grid to keep edges crisp
    float penX = std::floor(position.x + 0.5f);
    float baseline = std::floor(position.y + 0.5f);

    bool addedAny = false;

    // Pre-calculate indices to avoid resizing repeatedly? 
    // Vector implementation usually handles amortized growth well.

    for (char c : text) {
        if (c == ' ') {
            auto spaceIt = glyphs_.find(' ');
            if (spaceIt != glyphs_.end()) {
                penX += spaceIt->second.advance * scale;
            } else {
                // Fallback: ~1/3 em is typical space width
                penX += fontSize * 0.33f * scale;
            }
            continue;
        }
        
        auto it = glyphs_.find(c);
        if (it == glyphs_.end()) {
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

        // Add 4 vertices
        // Layout: x, y, u, v, r, g, b, a
        float quad[4][8] = {
            {x0, y0, g.u0, g.v0, color.r, color.g, color.b, color.a},
            {x0, y1, g.u0, g.v1, color.r, color.g, color.b, color.a},
            {x1, y1, g.u1, g.v1, color.r, color.g, color.b, color.a},
            {x1, y0, g.u1, g.v0, color.r, color.g, color.b, color.a}
        };

        outVertices.insert(outVertices.end(), &quad[0][0], &quad[0][0] + 32);

        // Add 6 indices
        uint32_t base = vertexOffset;
        outIndices.push_back(base + 0);
        outIndices.push_back(base + 1);
        outIndices.push_back(base + 2);
        outIndices.push_back(base + 0);
        outIndices.push_back(base + 2);
        outIndices.push_back(base + 3);

        vertexOffset += 4;
        penX += g.advance * scale;
        addedAny = true;
    }

    return addedAny;
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
                // Fallback: ~1/3 em is typical space width
                width += fontSize * 0.33f * scale;
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

// createShader removed


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
    int padding = 8;  // Adequate padding for SDF spread without waste
    
    // SDF parameters tuned for CRISP text rendering
    // onedge_value: 180 = ~0.7 normalized, makes edge detection more robust
    // pixel_dist_scale: higher values = more precision in the distance field
    //   For crisp text, we want a well-defined gradient around edges
    float onedge_value = 180.0f;     // Slightly above 0.5 for robust edges
    float pixel_dist_scale = 255.0f / 4.0f;  // ~64 - good balance of precision
    
    // For very large fonts, we can use even tighter values
    if (fontSize > 128.0f) {
        pixel_dist_scale = 255.0f / 5.0f;  // ~51 - tighter for huge text
    }
    
    std::cout << "SDF params: onedge=" << onedge_value << ", dist_scale=" << pixel_dist_scale 
              << ", padding=" << padding << std::endl;

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
    // Space character has no visible bitmap (stbtt_GetCodepointSDF returns nullptr)
    // but we MUST add it with correct advance width from font metrics
    auto spaceIt = glyphs_.find(' ');
    if (spaceIt == glyphs_.end()) {
        // Get actual space advance from font metrics (this is the key fix!)
        int spaceAdvance, spaceLsb;
        stbtt_GetCodepointHMetrics(&font, ' ', &spaceAdvance, &spaceLsb);
        
        GlyphData spaceGlyph{};
        spaceGlyph.width = 0.0f;
        spaceGlyph.height = 0.0f;
        spaceGlyph.bearingX = 0.0f;
        spaceGlyph.bearingY = 0.0f;
        spaceGlyph.advance = spaceAdvance * scale;  // Use ACTUAL font metrics
        spaceGlyph.u0 = spaceGlyph.u1 = spaceGlyph.v0 = spaceGlyph.v1 = 0.0f;
        glyphs_[' '] = spaceGlyph;
        
        std::cout << "Space character created with advance: " << spaceGlyph.advance 
                  << "px (font metrics)" << std::endl;
    }

    glGenTextures(1, &atlasTexture_);
    glBindTexture(GL_TEXTURE_2D, atlasTexture_);
    // Ensure byte-aligned rows for arbitrary glyph widths
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, atlasW, atlasH, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.data());
    // Mipmaps can soften small text; keep them optional for UI at 1:1 scale
    const bool useMipmaps = false;
    if (useMipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
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
