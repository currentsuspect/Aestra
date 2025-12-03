// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUITypes.h"
#include <string>
#include <unordered_map>

namespace NomadUI {

/**
 * GPU-accelerated MSDF/SDF renderer built with stb_truetype-generated distance fields.
 * Currently generates an SDF atlas on startup for ASCII range.
 */
class NUITextRendererSDF {
public:
    struct GlyphData {
        float u0, v0, u1, v1;
        float width, height;
        float bearingX, bearingY;
        float advance;
    };

    NUITextRendererSDF();
    ~NUITextRendererSDF();

    bool initialize(const std::string& fontPath, float fontSize);
    void shutdown();

    void drawText(const std::string& text,
                  const NUIPoint& position,
                  float fontSize,
                  const NUIColor& color,
                  const float* projection);

    NUISize measureText(const std::string& text, float fontSize) const;
    float getAscent(float fontSize) const;

    bool isInitialized() const { return initialized_; }

private:
    bool createShader();
    bool loadFontAtlas(const std::string& fontPath, float fontSize);

    bool initialized_{false};
    float baseFontSize_{16.0f};
    float atlasFontSize_{16.0f};
    float ascent_{0.0f};
    float descent_{0.0f};

    uint32_t atlasTexture_{0};
    uint32_t shaderProgram_{0};
    uint32_t vao_{0};
    uint32_t vbo_{0};
    uint32_t ebo_{0};

    float atlasWidth_{2048.0f};   // Increased for better character packing
    float atlasHeight_{2048.0f};  // Increased for better character packing

    std::unordered_map<char, GlyphData> glyphs_;
};

} // namespace NomadUI
