// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUITextRendererGDI.h"
#include <iostream>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <Windows.h>
#endif

namespace NomadUI {

NUITextRendererGDI::NUITextRendererGDI()
    : initialized_(false)
    , font_(nullptr)
    , oldFont_(nullptr)
{
}

NUITextRendererGDI::~NUITextRendererGDI() {
    shutdown();
}

bool NUITextRendererGDI::initialize() {
    if (initialized_) return true;
    
    // Create a default font
    font_ = static_cast<void*>(CreateFontA(
        16,                    // Height
        0,                     // Width
        0,                     // Escapement
        0,                     // Orientation
        FW_NORMAL,             // Weight
        FALSE,                 // Italic
        FALSE,                 // Underline
        FALSE,                 // StrikeOut
        DEFAULT_CHARSET,       // CharSet
        OUT_DEFAULT_PRECIS,    // OutPrecision
        CLIP_DEFAULT_PRECIS,   // ClipPrecision
        DEFAULT_QUALITY,       // Quality
        DEFAULT_PITCH | FF_SWISS, // PitchAndFamily
        DEFAULT_PITCH | FF_SWISS, // PitchAndFamily
        "Arial"                // FaceName
    ));
    
    if (!font_) {
        std::cerr << "Failed to create GDI font" << std::endl;
        return false;
    }
    
    initialized_ = true;
    std::cout << "âœ“ GDI Text renderer initialized" << std::endl;
    return true;
}

void NUITextRendererGDI::shutdown() {
    if (font_) {
        DeleteObject(static_cast<HFONT>(font_));
        font_ = nullptr;
    }
    
    // Clean up font cache
    for (auto& [size, cachedFont] : fontCache_) {
        DeleteObject(static_cast<HFONT>(cachedFont));
    }
    fontCache_.clear();
    
    initialized_ = false;
}

void NUITextRendererGDI::drawText(
    const std::string& text,
    const NUIPoint& position,
    float fontSize,
    const NUIColor& color,
    void* hdc)
{
    if (!initialized_ || !hdc) return;
    
    HDC deviceContext = static_cast<HDC>(hdc);
    int fontSizeInt = static_cast<int>(fontSize);
    
    // Get or create cached font
    HFONT sizedFont = nullptr;
    auto it = fontCache_.find(fontSizeInt);
    if (it != fontCache_.end()) {
        sizedFont = static_cast<HFONT>(it->second);
    } else {
        // Create new font and cache it
        sizedFont = CreateFontA(
            fontSizeInt,            // Height
            0,                      // Width
            0,                      // Escapement
            0,                      // Orientation
            FW_NORMAL,              // Weight
            FALSE,                  // Italic
            FALSE,                  // Underline
            FALSE,                  // StrikeOut
            DEFAULT_CHARSET,        // CharSet
            OUT_DEFAULT_PRECIS,     // OutPrecision
            CLIP_DEFAULT_PRECIS,    // ClipPrecision
            CLEARTYPE_QUALITY,      // Quality - use ClearType for better rendering
            DEFAULT_PITCH | FF_SWISS, // PitchAndFamily
            "Arial"                 // FaceName
        );
        
        if (sizedFont) {
            fontCache_[fontSizeInt] = static_cast<void*>(sizedFont);
        }
    }
    
    if (!sizedFont) return;
    
    // Select font
    HFONT previousFont = static_cast<HFONT>(SelectObject(deviceContext, sizedFont));
    
    // Set text color
    COLORREF textColor = RGB(
        static_cast<int>(color.r * 255),
        static_cast<int>(color.g * 255),
        static_cast<int>(color.b * 255)
    );
    SetTextColor(deviceContext, textColor);
    
    // Set background mode to transparent
    SetBkMode(deviceContext, TRANSPARENT);
    
    // Set text alignment for better positioning
    SetTextAlign(deviceContext, TA_LEFT | TA_TOP);
    
    // Draw text
    TextOutA(deviceContext, 
             static_cast<int>(position.x), 
             static_cast<int>(position.y), 
             text.c_str(), 
             static_cast<int>(text.length()));
    
    // Restore old font
    SelectObject(deviceContext, previousFont);
}

NUISize NUITextRendererGDI::measureText(const std::string& text, float fontSize) {
    if (!initialized_) return {0, 0};
    
    // This is a simplified measurement - in a real implementation,
    // you'd need a device context to measure text properly
    return {
        static_cast<float>(text.length() * fontSize * 0.6f), // Approximate width
        fontSize // Height
    };
}

} // namespace NomadUI
