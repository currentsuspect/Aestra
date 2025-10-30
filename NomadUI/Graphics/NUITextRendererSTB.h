// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUITypes.h"
#include <string>
#include <unordered_map>

namespace NomadUI {

/**
 * High-quality text renderer using stb_truetype
 * Renders smooth, anti-aliased text using GPU textures
 */
class NUITextRendererSTB {
public:
    struct GlyphInfo {
        uint32_t textureId;
        float width, height;
        float bearingX, bearingY;
        float advance;
    };

    NUITextRendererSTB();
    ~NUITextRendererSTB();

    bool initialize(const std::string& fontPath, float fontSize);
    void shutdown();

    void renderText(
        const std::string& text,
        float x, float y,
        const NUIColor& color,
        uint32_t shaderProgram,
        const float* projectionMatrix
    );

    NUISize measureText(const std::string& text);

private:
    bool loadFont(const std::string& fontPath);
    void bakeGlyphs(float fontSize);

    bool initialized_;
    unsigned char* fontBuffer_;
    std::unordered_map<char, GlyphInfo> glyphs_;
    uint32_t vao_, vbo_;
    float fontSize_;
};

} // namespace NomadUI
