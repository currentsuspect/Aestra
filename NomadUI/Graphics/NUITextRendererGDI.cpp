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

// Accurate measurement using GDI
NUISize NUITextRendererGDI::measureText(const std::string& text, float fontSize) {
    if (!initialized_) return {0, 0};
    
    // Create a temporary DC if we don't have one (we don't persist it in this class)
    HDC hdc = CreateCompatibleDC(NULL);
    if (!hdc) {
        // Fallback if DC creation fails
        return {
             static_cast<float>(text.length() * fontSize * 0.6f),
             fontSize
        };
    }

    int fontSizeInt = static_cast<int>(fontSize);
    
    // Get detailed font from cache or create one
    HFONT hFont = nullptr;
    auto it = fontCache_.find(fontSizeInt);
    bool createdFont = false;
    
    if (it != fontCache_.end()) {
        hFont = static_cast<HFONT>(it->second);
    } else {
        // Must create one to match drawText logic
        hFont = CreateFontA(
            fontSizeInt, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Arial"
        );
        createdFont = true;
    }
    
    HGDIOBJ oldFont = SelectObject(hdc, hFont);
    
    SIZE size;
    GetTextExtentPoint32A(hdc, text.c_str(), static_cast<int>(text.length()), &size);
    
    SelectObject(hdc, oldFont);
    if (createdFont && hFont) DeleteObject(hFont);
    DeleteDC(hdc);
    
    // Add a tiny bit of padding to ensure no clipping at edges due to anti-aliasing
    return {
        static_cast<float>(size.cx) + 2.0f, 
        static_cast<float>(size.cy)
    };
}

} // namespace NomadUI
