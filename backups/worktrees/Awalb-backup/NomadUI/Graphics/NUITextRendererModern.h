// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUITypes.h"
#include <string>
#include <vector>
#include <map>
#include <memory>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
#endif

// OpenGL includes
#include "../External/glad/include/glad/glad.h"

#ifdef NOMADUI_SDL2_AVAILABLE
// SDL2 includes
#include <SDL.h>
#include <SDL_ttf.h>

namespace NomadUI {

/**
 * Modern OpenGL text renderer using SDL2_ttf and font atlases.
 * 
 * Features:
 * - Font atlas caching for performance
 * - VAO/VBO batching
 * - Modern OpenGL 3.3+ shaders
 * - Crisp text rendering
 * - Multiple font sizes support
 */
class NUITextRendererModern {
public:
    NUITextRendererModern();
    ~NUITextRendererModern();
    
    bool initialize();
    void shutdown();
    
    // Load font and build atlas
    bool loadFont(const std::string& fontPath, int fontSize);
    
    // Draw text at position with color
    void drawText(const std::string& text, const NUIPoint& position, const NUIColor& color);
    void drawText(const std::string& text, float x, float y, float r, float g, float b, float a);
    
    // Set viewport for orthographic projection
    void setViewport(int width, int height);
    
    // Measure text dimensions
    NUISize measureText(const std::string& text);

private:
    struct Glyph {
        float ax; // advance
        float bw; // bitmap width
        float bh; // bitmap height
        float bl; // bitmap left
        float bt; // bitmap top
        float tx; // x offset in atlas (normalized)
        float ty; // y offset in atlas (normalized)
        float tw; // glyph width in atlas (normalized)
        float th; // glyph height in atlas (normalized)
    };

    struct FontAtlas {
        GLuint texture = 0;
        int atlasWidth = 0;
        int atlasHeight = 0;
        int fontSize = 0;
        std::map<char, Glyph> glyphs;
    };

    struct Vertex {
        float x, y, u, v;
    };

    // OpenGL objects
    GLuint shaderProgram_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    
    // Viewport
    int width_ = 800;
    int height_ = 600;
    
    // Font atlas
    FontAtlas atlas_;
    
    // Shader sources
    static const char* vertexShaderSource_;
    static const char* fragmentShaderSource_;
    
    // Helper methods
    GLuint compileShader(GLenum type, const char* source);
    GLuint createProgram(const char* vs, const char* fs);
    bool buildFontAtlas(TTF_Font* font, FontAtlas& outAtlas, int first = 32, int last = 126);
    void setupBuffers();
    void updateProjectionMatrix();
};

} // namespace NomadUI

#else // NOMADUI_SDL2_AVAILABLE

// Stub class when SDL2 is not available
namespace NomadUI {

class NUITextRendererModern {
public:
    NUITextRendererModern() {}
    ~NUITextRendererModern() {}
    
    bool initialize() { return false; }
    void shutdown() {}
    bool loadFont(const std::string& fontPath, int fontSize) { return false; }
    void drawText(const std::string& text, const NUIPoint& position, const NUIColor& color) {}
    void drawText(const std::string& text, float x, float y, float r, float g, float b, float a) {}
    void setViewport(int width, int height) {}
    NUISize measureText(const std::string& text) { return {0, 0}; }
};

} // namespace NomadUI

#endif // NOMADUI_SDL2_AVAILABLE
