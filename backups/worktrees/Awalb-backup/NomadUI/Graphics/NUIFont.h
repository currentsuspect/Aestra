// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUITypes.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <string>
#include <map>
#include <memory>

namespace NomadUI {

/**
 * Represents a single glyph in the font atlas.
 */
struct NUIGlyph {
    uint32_t textureID;      // OpenGL texture ID for this glyph
    int width;               // Glyph width in pixels
    int height;              // Glyph height in pixels
    int bearingX;            // Offset from baseline to left of glyph
    int bearingY;            // Offset from baseline to top of glyph
    int advance;             // Horizontal advance to next glyph
    
    // Atlas coordinates (for texture atlas approach)
    float u0, v0;            // Top-left UV
    float u1, v1;            // Bottom-right UV
};

/**
 * Font class that loads and manages TrueType/OpenType fonts using FreeType.
 * 
 * Features:
 * - Font loading from file or memory
 * - Glyph rasterization and caching
 * - Multiple font sizes support
 * - Texture atlas generation
 * - Kerning support
 */
class NUIFont {
public:
    NUIFont();
    ~NUIFont();
    
    // Prevent copying (FreeType face is not copyable)
    NUIFont(const NUIFont&) = delete;
    NUIFont& operator=(const NUIFont&) = delete;
    
    // ========================================================================
    // Loading
    // ========================================================================
    
    /**
     * Load a font from file.
     * @param filepath Path to .ttf or .otf font file
     * @param fontSize Default font size in pixels
     * @return True if loaded successfully
     */
    bool loadFromFile(const std::string& filepath, int fontSize);
    
    /**
     * Load a font from memory buffer.
     * @param data Font data buffer
     * @param size Size of font data
     * @param fontSize Default font size in pixels
     * @return True if loaded successfully
     */
    bool loadFromMemory(const uint8_t* data, size_t size, int fontSize);
    
    /**
     * Set the current font size.
     * This will regenerate glyphs if needed.
     */
    void setSize(int fontSize);
    
    /**
     * Get the current font size.
     */
    int getSize() const { return fontSize_; }
    
    // ========================================================================
    // Glyph Access
    // ========================================================================
    
    /**
     * Get glyph data for a character.
     * Will rasterize and cache if not already cached.
     * @param character Unicode character code
     * @return Glyph data, or nullptr if failed
     */
    const NUIGlyph* getGlyph(uint32_t character);
    
    /**
     * Get kerning offset between two characters.
     * @param left Left character code
     * @param right Right character code
     * @return Horizontal kerning adjustment
     */
    int getKerning(uint32_t left, uint32_t right);
    
    // ========================================================================
    // Metrics
    // ========================================================================
    
    /**
     * Get font ascender (distance from baseline to top).
     */
    int getAscender() const { return ascender_; }
    
    /**
     * Get font descender (distance from baseline to bottom, negative value).
     */
    int getDescender() const { return descender_; }
    
    /**
     * Get line height (recommended vertical distance between lines).
     */
    int getLineHeight() const { return lineHeight_; }
    
    /**
     * Measure the width of a text string.
     */
    float measureText(const std::string& text);
    
    // ========================================================================
    // Atlas Management
    // ========================================================================
    
    /**
     * Pre-cache common ASCII characters (32-126).
     * Call this after loading for better performance.
     */
    void cacheASCII();
    
    /**
     * Clear glyph cache and free textures.
     */
    void clearCache();
    
    /**
     * Get number of cached glyphs.
     */
    size_t getCachedGlyphCount() const { return glyphs_.size(); }
    
    // ========================================================================
    // State
    // ========================================================================
    
    bool isLoaded() const { return face_ != nullptr; }
    std::string getFilepath() const { return filepath_; }
    
private:
    // FreeType library (static, shared across all fonts)
    static FT_Library ftLibrary_;
    static int refCount_;
    
    // Font data
    FT_Face face_;
    std::string filepath_;
    int fontSize_;
    
    // Metrics
    int ascender_;
    int descender_;
    int lineHeight_;
    
    // Glyph cache
    std::map<uint32_t, NUIGlyph> glyphs_;
    
    // Helper methods
    bool initializeFreeType();
    void shutdownFreeType();
    bool rasterizeGlyph(uint32_t character, NUIGlyph& glyph);
    uint32_t createGlyphTexture(FT_Bitmap& bitmap);
};

/**
 * Font manager - loads and caches fonts.
 */
class NUIFontManager {
public:
    static NUIFontManager& getInstance();
    
    /**
     * Load or get cached font.
     * @param filepath Path to font file
     * @param fontSize Font size in pixels
     * @return Shared pointer to font, or nullptr if failed
     */
    std::shared_ptr<NUIFont> getFont(const std::string& filepath, int fontSize);
    
    /**
     * Get default system font.
     * On Windows: Segoe UI
     * On macOS: San Francisco
     * On Linux: DejaVu Sans
     */
    std::shared_ptr<NUIFont> getDefaultFont(int fontSize = 14);
    
    /**
     * Clear font cache.
     */
    void clearCache();
    
private:
    NUIFontManager() = default;
    
    struct FontKey {
        std::string filepath;
        int fontSize;
        
        bool operator<(const FontKey& other) const {
            if (filepath != other.filepath) return filepath < other.filepath;
            return fontSize < other.fontSize;
        }
    };
    
    std::map<FontKey, std::shared_ptr<NUIFont>> fonts_;
};

} // namespace NomadUI

