// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUITypes.h"
#include <string>
#include <unordered_map>
#include <memory>

namespace NomadUI {

/**
 * GPU-accelerated SDF (Signed Distance Field) text renderer
 * Provides crisp, scale-independent text rendering using distance field textures
 */
class NUITextRendererSDF {
public:
    struct GlyphData {
        float u0, v0, u1, v1;  // UV coordinates in atlas
        float width, height;    // Glyph dimensions
        float bearingX, bearingY; // Glyph bearing
        float advance;          // Horizontal advance
    };

    NUITextRendererSDF();
    ~NUITextRendererSDF();

    bool initialize();
    void shutdown();

    void drawText(
        const std::string& text,
        const NUIPoint& position,
        float fontSize,
        const NUIColor& color
    );

    NUISize measureText(const std::string& text, float fontSize);

    uint32_t getAtlasTexture() const { return atlasTexture_; }

private:
    bool loadFontAtlas(const std::string& fontPath);
    bool createShader();

    bool initialized_;
    uint32_t atlasTexture_;
    uint32_t shaderProgram_;
    uint32_t vao_, vbo_;
    
    std::unordered_map<char, GlyphData> glyphMap_;
    float atlasWidth_, atlasHeight_;
};

} // namespace NomadUI
