// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUITypes.h"
#include <string>
#include <memory>
#include <unordered_map>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
#endif

namespace NomadUI {

class NUITextRendererGDI {
public:
    NUITextRendererGDI();
    ~NUITextRendererGDI();
    
    bool initialize();
    void shutdown();
    
    void drawText(
        const std::string& text,
        const NUIPoint& position,
        float fontSize,
        const NUIColor& color,
        void* hdc
    );
    
    NUISize measureText(const std::string& text, float fontSize);

private:
    bool initialized_;
    HFONT font_;
    HFONT oldFont_;
    std::unordered_map<int, HFONT> fontCache_; // Cache fonts by size
};

} // namespace NomadUI
