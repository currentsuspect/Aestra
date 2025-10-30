// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUITextRendererModern.h"
#include <iostream>
#include <cassert>

#ifdef NOMADUI_SDL2_AVAILABLE

namespace NomadUI {

// Shader sources
const char* NUITextRendererModern::vertexShaderSource_ = R"glsl(
#version 330 core
layout(location = 0) in vec2 aPos;     // screen-space position
layout(location = 1) in vec2 aTex;     // texture coord

out vec2 vTex;

uniform mat4 uOrtho;

void main(){
    gl_Position = uOrtho * vec4(aPos, 0.0, 1.0);
    vTex = aTex;
}
)glsl";

const char* NUITextRendererModern::fragmentShaderSource_ = R"glsl(
#version 330 core
in vec2 vTex;
out vec4 FragColor;

uniform sampler2D uTex;
uniform vec4 uColor; // RGBA text color (alpha used from texture)

void main(){
    vec4 sampled = texture(uTex, vTex);
    // atlas is pre-blended glyphs (alpha), so multiply by color
    FragColor = vec4(uColor.rgb, uColor.a * sampled.r);
}
)glsl";

// ============================================================================
// Constructor / Destructor
// ============================================================================

NUITextRendererModern::NUITextRendererModern() {
}

NUITextRendererModern::~NUITextRendererModern() {
    shutdown();
}

bool NUITextRendererModern::initialize() {
    // Initialize SDL2_ttf if not already done
    if (!TTF_WasInit()) {
        if (TTF_Init() == -1) {
            std::cerr << "TTF_Init failed: " << TTF_GetError() << std::endl;
            return false;
        }
    }
    
    // Create shader program
    shaderProgram_ = createProgram(vertexShaderSource_, fragmentShaderSource_);
    if (!shaderProgram_) {
        std::cerr << "Failed to create shader program" << std::endl;
        return false;
    }
    
    // Setup OpenGL buffers
    setupBuffers();
    
    // Set initial viewport
    setViewport(width_, height_);
    
    std::cout << "âœ“ Modern text renderer initialized" << std::endl;
    return true;
}

void NUITextRendererModern::shutdown() {
    if (atlas_.texture) {
        glDeleteTextures(1, &atlas_.texture);
        atlas_.texture = 0;
    }
    
    if (shaderProgram_) {
        glDeleteProgram(shaderProgram_);
        shaderProgram_ = 0;
    }
    
    if (vbo_) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
}

// ============================================================================
// Font Loading
// ============================================================================

bool NUITextRendererModern::loadFont(const std::string& fontPath, int fontSize) {
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), fontSize);
    if (!font) {
        std::cerr << "Failed to open font: " << TTF_GetError() << std::endl;
        return false;
    }
    
    bool success = buildFontAtlas(font, atlas_);
    TTF_CloseFont(font);
    
    if (success) {
        std::cout << "âœ“ Font loaded: " << fontPath << " (" << fontSize << "px)" << std::endl;
    }
    
    return success;
}

// ============================================================================
// Text Rendering
// ============================================================================

void NUITextRendererModern::drawText(const std::string& text, const NUIPoint& position, const NUIColor& color) {
    drawText(text, position.x, position.y, color.r, color.g, color.b, color.a);
}

void NUITextRendererModern::drawText(const std::string& text, float x, float y, float r, float g, float b, float a) {
    if (!atlas_.texture || text.empty()) return;
    
    // Build vertex data: for each glyph we create 6 vertices (two triangles)
    std::vector<Vertex> vertices;
    vertices.reserve(text.size() * 6);

    float penX = x, penY = y;
    for (char ch : text) {
        if (ch == '\n') { 
            penX = x; 
            penY += atlas_.fontSize; 
            continue; 
        }
        
        auto it = atlas_.glyphs.find(ch);
        if (it == atlas_.glyphs.end()) {
            penX += atlas_.fontSize * 0.5f;
            continue;
        }
        
        const Glyph& g = it->second;
        float gx = penX + g.bl;          // glyph bitmap left
        float gy = penY - g.bt;          // glyph top -> convert to top-left origin
        float gw = g.bw;
        float gh = g.bh;

        // Texture coords
        float u0 = g.tx, v0 = g.ty;
        float u1 = g.tx + g.tw, v1 = g.ty + g.th;

        // Two triangles (triangle list)
        // triangle 1
        vertices.push_back({gx,     gy,     u0, v0});
        vertices.push_back({gx+gw,  gy,     u1, v0});
        vertices.push_back({gx+gw,  gy+gh,  u1, v1});
        // triangle 2
        vertices.push_back({gx,     gy,     u0, v0});
        vertices.push_back({gx+gw,  gy+gh,  u1, v1});
        vertices.push_back({gx,     gy+gh,  u0, v1});

        penX += g.ax; // advance
    }

    if (vertices.empty()) return;

    // Upload to VBO (dynamic)
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_DYNAMIC_DRAW);

    // Bind atlas texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_.texture);

    // Use shader program
    glUseProgram(shaderProgram_);
    
    // Set uniforms
    GLint texLoc = glGetUniformLocation(shaderProgram_, "uTex");
    glUniform1i(texLoc, 0);
    
    GLint colLoc = glGetUniformLocation(shaderProgram_, "uColor");
    glUniform4f(colLoc, r, g, b, a);

    // Enable blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Draw
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));

    // Cleanup
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);
}

void NUITextRendererModern::setViewport(int width, int height) {
    width_ = width;
    height_ = height;
    updateProjectionMatrix();
}

NUISize NUITextRendererModern::measureText(const std::string& text) {
    if (atlas_.glyphs.empty()) return {0, 0};
    
    float width = 0;
    float maxWidth = 0;
    float height = static_cast<float>(atlas_.fontSize);
    float currentHeight = height;
    
    for (char ch : text) {
        if (ch == '\n') {
            maxWidth = std::max(maxWidth, width);
            width = 0;
            currentHeight += height;
            continue;
        }
        
        auto it = atlas_.glyphs.find(ch);
        if (it != atlas_.glyphs.end()) {
            width += it->second.ax;
        } else {
            width += atlas_.fontSize * 0.5f;
        }
    }
    
    maxWidth = std::max(maxWidth, width);
    return {maxWidth, currentHeight};
}

// ============================================================================
// Private Methods
// ============================================================================

GLuint NUITextRendererModern::compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        glGetShaderInfoLog(shader, 1024, nullptr, infoLog);
        std::cerr << "Shader compile error: " << infoLog << std::endl;
        return 0;
    }
    
    return shader;
}

GLuint NUITextRendererModern::createProgram(const char* vs, const char* fs) {
    GLuint vsId = compileShader(GL_VERTEX_SHADER, vs);
    GLuint fsId = compileShader(GL_FRAGMENT_SHADER, fs);
    
    if (!vsId || !fsId) {
        if (vsId) glDeleteShader(vsId);
        if (fsId) glDeleteShader(fsId);
        return 0;
    }
    
    GLuint program = glCreateProgram();
    glAttachShader(program, vsId);
    glAttachShader(program, fsId);
    glLinkProgram(program);
    
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        glGetProgramInfoLog(program, 1024, nullptr, infoLog);
        std::cerr << "Program link error: " << infoLog << std::endl;
        glDeleteProgram(program);
        program = 0;
    }
    
    glDeleteShader(vsId);
    glDeleteShader(fsId);
    
    return program;
}

bool NUITextRendererModern::buildFontAtlas(TTF_Font* font, FontAtlas& outAtlas, int first, int last) {
    if (!font) return false;
    outAtlas.fontSize = TTF_FontHeight(font);

    // First, measure glyph bitmaps and compute atlas size (simple packing: row pack)
    int padding = 2; // padding between glyphs
    int atlasW = 1024; // fixed width (could be tuned)
    int x = padding, y = padding;
    int currentRowHeight = 0;
    std::vector<std::tuple<char, SDL_Surface*, int, int>> placements; // ch, surf, x, y

    // Render each glyph and place in atlas
    for (int c = first; c <= last; ++c) {
        char ch = static_cast<char>(c);
        // Render glyph to surface as 32-bit RGBA with alpha
        SDL_Surface* glyphSurface = TTF_RenderGlyph_Blended(font, c, SDL_Color{255, 255, 255, 255});
        if (!glyphSurface) {
            std::cerr << "Failed glyph " << c << ": " << TTF_GetError() << std::endl;
            continue;
        }
        
        // Check if glyph fits in current row
        if (x + glyphSurface->w + padding > atlasW) {
            x = padding;
            y += currentRowHeight + padding;
            currentRowHeight = 0;
        }
        
        placements.push_back({ch, glyphSurface, x, y});
        x += glyphSurface->w + padding;
        currentRowHeight = std::max(currentRowHeight, glyphSurface->h);
    }
    
    int atlasH = y + currentRowHeight + padding;
    
    // Create target surface (RGBA)
    SDL_Surface* atlasSurf = SDL_CreateRGBSurfaceWithFormat(0, atlasW, atlasH, 32, SDL_PIXELFORMAT_RGBA32);
    if (!atlasSurf) {
        std::cerr << "Failed create atlas surface" << std::endl;
        for (auto& tp : placements) {
            SDL_FreeSurface(std::get<1>(tp));
        }
        return false;
    }
    
    // Fill with transparent
    SDL_FillRect(atlasSurf, nullptr, SDL_MapRGBA(atlasSurf->format, 0, 0, 0, 0));

    // Blit each glyph into atlas and store glyph metrics
    float invW = 1.0f / float(atlasW), invH = 1.0f / float(atlasH);
    for (auto& tp : placements) {
        char ch;
        SDL_Surface* surf;
        int px, py;
        std::tie(ch, surf, px, py) = tp;
        
        SDL_Rect dst{px, py, surf->w, surf->h};
        SDL_BlitSurface(surf, nullptr, atlasSurf, &dst);

        // Retrieve font metrics for glyph
        int minx, maxx, miny, maxy, advance;
        if (TTF_GlyphMetrics(font, static_cast<Uint16>(ch), &minx, &maxx, &miny, &maxy, &advance) != 0) {
            std::cerr << "TTF_GlyphMetrics failed: " << TTF_GetError() << std::endl;
        }
        
        Glyph g;
        g.ax = static_cast<float>(advance);
        g.bw = static_cast<float>(surf->w);
        g.bh = static_cast<float>(surf->h);
        g.bl = static_cast<float>(minx);
        g.bt = static_cast<float>(maxy); // top bearing
        g.tx = px * invW;
        g.ty = py * invH;
        g.tw = surf->w * invW;
        g.th = surf->h * invH;
        outAtlas.glyphs[ch] = g;
    }

    // Upload atlas to GL
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlasW, atlasH, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlasSurf->pixels);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    outAtlas.texture = tex;
    outAtlas.atlasWidth = atlasW;
    outAtlas.atlasHeight = atlasH;

    // Cleanup
    for (auto& tp : placements) {
        SDL_FreeSurface(std::get<1>(tp));
    }
    SDL_FreeSurface(atlasSurf);

    return true;
}

void NUITextRendererModern::setupBuffers() {
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    
    // Attributes: layout 0 = vec2 aPos; layout 1 = vec2 aTex;
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(sizeof(float) * 2));
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void NUITextRendererModern::updateProjectionMatrix() {
    if (!shaderProgram_) return;
    
    // Orthographic projection matrix: left=0, right=w, top=0, bottom=h
    float l = 0.0f, r = static_cast<float>(width_), t = 0.0f, b = static_cast<float>(height_);
    // Note: we want y-down coordinates like typical UI; our ortho maps (0,0) top-left
    float ortho[16] = {
        2.0f/(r-l), 0, 0, 0,
        0, 2.0f/(t-b), 0, 0,
        0, 0, -1.0f, 0,
        -(r+l)/(r-l), -(t+b)/(t-b), 0, 1.0f
    };
    
    glUseProgram(shaderProgram_);
    GLint loc = glGetUniformLocation(shaderProgram_, "uOrtho");
    glUniformMatrix4fv(loc, 1, GL_FALSE, ortho);
    glUseProgram(0);
}

} // namespace NomadUI

#endif // NOMADUI_SDL2_AVAILABLE
