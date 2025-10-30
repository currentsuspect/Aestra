// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUIFont.h"
#include <iostream>
#include <cstring>

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

// Static members
FT_Library NUIFont::ftLibrary_ = nullptr;
int NUIFont::refCount_ = 0;

// ============================================================================
// Constructor / Destructor
// ============================================================================

NUIFont::NUIFont()
    : face_(nullptr)
    , fontSize_(14)
    , ascender_(0)
    , descender_(0)
    , lineHeight_(0)
{
    initializeFreeType();
}

NUIFont::~NUIFont() {
    clearCache();
    
    if (face_) {
        FT_Done_Face(face_);
        face_ = nullptr;
    }
    
    shutdownFreeType();
}

// ============================================================================
// FreeType Initialization
// ============================================================================

bool NUIFont::initializeFreeType() {
    if (refCount_ == 0) {
        FT_Error error = FT_Init_FreeType(&ftLibrary_);
        if (error) {
            std::cerr << "Failed to initialize FreeType library" << std::endl;
            return false;
        }
    }
    refCount_++;
    return true;
}

void NUIFont::shutdownFreeType() {
    refCount_--;
    if (refCount_ == 0 && ftLibrary_) {
        FT_Done_FreeType(ftLibrary_);
        ftLibrary_ = nullptr;
    }
}

// ============================================================================
// Loading
// ============================================================================

bool NUIFont::loadFromFile(const std::string& filepath, int fontSize) {
    if (!ftLibrary_) {
        std::cerr << "FreeType not initialized" << std::endl;
        return false;
    }
    
    // Load font face
    FT_Error error = FT_New_Face(ftLibrary_, filepath.c_str(), 0, &face_);
    if (error) {
        std::cerr << "Failed to load font: " << filepath << std::endl;
        return false;
    }
    
    filepath_ = filepath;
    setSize(fontSize);
    
    std::cout << "âœ“ Font loaded: " << filepath << " (" << fontSize << "px)" << std::endl;
    return true;
}

bool NUIFont::loadFromMemory(const uint8_t* data, size_t size, int fontSize) {
    if (!ftLibrary_) {
        std::cerr << "FreeType not initialized" << std::endl;
        return false;
    }
    
    // Load font from memory
    FT_Error error = FT_New_Memory_Face(ftLibrary_, data, static_cast<FT_Long>(size), 0, &face_);
    if (error) {
        std::cerr << "Failed to load font from memory" << std::endl;
        return false;
    }
    
    filepath_ = "[memory]";
    setSize(fontSize);
    
    std::cout << "âœ“ Font loaded from memory (" << fontSize << "px)" << std::endl;
    return true;
}

void NUIFont::setSize(int fontSize) {
    if (!face_) return;
    
    fontSize_ = fontSize;
    
    // Set pixel size (width 0 means "dynamically calculate based on height")
    FT_Set_Pixel_Sizes(face_, 0, fontSize);
    
    // Update metrics
    ascender_ = static_cast<int>(face_->size->metrics.ascender >> 6);
    descender_ = static_cast<int>(face_->size->metrics.descender >> 6);
    lineHeight_ = static_cast<int>(face_->size->metrics.height >> 6);
    
    // Clear glyph cache since size changed
    clearCache();
}

// ============================================================================
// Glyph Access
// ============================================================================

const NUIGlyph* NUIFont::getGlyph(uint32_t character) {
    // Check if already cached
    auto it = glyphs_.find(character);
    if (it != glyphs_.end()) {
        return &it->second;
    }
    
    // Rasterize new glyph
    NUIGlyph glyph;
    if (!rasterizeGlyph(character, glyph)) {
        return nullptr;
    }
    
    // Cache and return
    glyphs_[character] = glyph;
    return &glyphs_[character];
}

int NUIFont::getKerning(uint32_t left, uint32_t right) {
    if (!face_ || !FT_HAS_KERNING(face_)) {
        return 0;
    }
    
    FT_UInt leftIndex = FT_Get_Char_Index(face_, left);
    FT_UInt rightIndex = FT_Get_Char_Index(face_, right);
    
    FT_Vector kerning;
    FT_Get_Kerning(face_, leftIndex, rightIndex, FT_KERNING_DEFAULT, &kerning);
    
    return static_cast<int>(kerning.x >> 6);
}

// ============================================================================
// Metrics
// ============================================================================

float NUIFont::measureText(const std::string& text) {
    float width = 0.0f;
    uint32_t prevChar = 0;
    
    for (char c : text) {
        uint32_t character = static_cast<uint32_t>(c);
        
        // Get glyph
        const NUIGlyph* glyph = getGlyph(character);
        if (!glyph) continue;
        
        // Add kerning
        if (prevChar != 0) {
            width += getKerning(prevChar, character);
        }
        
        // Add advance
        width += glyph->advance;
        prevChar = character;
    }
    
    return width;
}

// ============================================================================
// Atlas Management
// ============================================================================

void NUIFont::cacheASCII() {
    std::cout << "Caching ASCII glyphs (32-126)..." << std::endl;
    
    int cached = 0;
    for (uint32_t c = 32; c <= 126; ++c) {
        if (getGlyph(c)) {
            cached++;
        }
    }
    
    std::cout << "âœ“ Cached " << cached << " ASCII glyphs" << std::endl;
}

void NUIFont::clearCache() {
    // Delete OpenGL textures
    for (auto& pair : glyphs_) {
        if (pair.second.textureID != 0) {
            glDeleteTextures(1, &pair.second.textureID);
        }
    }
    
    glyphs_.clear();
}

// ============================================================================
// Private Helpers
// ============================================================================

bool NUIFont::rasterizeGlyph(uint32_t character, NUIGlyph& glyph) {
    if (!face_) return false;
    
    // Load glyph
    FT_Error error = FT_Load_Char(face_, character, FT_LOAD_RENDER);
    if (error) {
        std::cerr << "Failed to load glyph: " << character << std::endl;
        return false;
    }
    
    FT_GlyphSlot slot = face_->glyph;
    FT_Bitmap& bitmap = slot->bitmap;
    
    // Create OpenGL texture
    glyph.textureID = createGlyphTexture(bitmap);
    
    // Store glyph metrics
    glyph.width = bitmap.width;
    glyph.height = bitmap.rows;
    glyph.bearingX = slot->bitmap_left;
    glyph.bearingY = slot->bitmap_top;
    glyph.advance = static_cast<int>(slot->advance.x >> 6);
    
    // For individual textures (not atlas), UV coords are 0-1
    glyph.u0 = 0.0f;
    glyph.v0 = 0.0f;
    glyph.u1 = 1.0f;
    glyph.v1 = 1.0f;
    
    return true;
}

uint32_t NUIFont::createGlyphTexture(FT_Bitmap& bitmap) {
    if (bitmap.width == 0 || bitmap.rows == 0) {
        return 0;  // Empty glyph (e.g., space)
    }
    
    // Generate OpenGL texture
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    
    // Upload bitmap data
    // FreeType uses 1-channel grayscale, store in red channel
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RED,
        bitmap.width,
        bitmap.rows,
        0,
        GL_RED,
        GL_UNSIGNED_BYTE,
        bitmap.buffer
    );
    
    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    glBindTexture(GL_TEXTURE_2D, 0);
    
    return texture;
}

// ============================================================================
// Font Manager
// ============================================================================

NUIFontManager& NUIFontManager::getInstance() {
    static NUIFontManager instance;
    return instance;
}

std::shared_ptr<NUIFont> NUIFontManager::getFont(const std::string& filepath, int fontSize) {
    FontKey key{filepath, fontSize};
    
    // Check cache
    auto it = fonts_.find(key);
    if (it != fonts_.end()) {
        return it->second;
    }
    
    // Load new font
    auto font = std::make_shared<NUIFont>();
    if (!font->loadFromFile(filepath, fontSize)) {
        return nullptr;
    }
    
    // Cache ASCII characters for better performance
    font->cacheASCII();
    
    // Store in cache
    fonts_[key] = font;
    return font;
}

std::shared_ptr<NUIFont> NUIFontManager::getDefaultFont(int fontSize) {
    std::string fontPath;
    
#ifdef _WIN32
    // Windows: Segoe UI
    fontPath = "C:\\Windows\\Fonts\\segoeui.ttf";
#elif __APPLE__
    // macOS: San Francisco (fallback to Helvetica)
    fontPath = "/System/Library/Fonts/SFNS.ttf";
    // TODO: Check if exists, fallback to Helvetica
#else
    // Linux: DejaVu Sans
    fontPath = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
#endif
    
    return getFont(fontPath, fontSize);
}

void NUIFontManager::clearCache() {
    fonts_.clear();
}

} // namespace NomadUI

