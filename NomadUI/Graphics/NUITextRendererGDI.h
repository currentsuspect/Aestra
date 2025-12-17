// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUITypes.h"
#include <string>
#include <memory>
#include <unordered_map>

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
    void* font_; // HFONT (opaque)
    void* oldFont_; // HFONT (opaque)
    std::unordered_map<int, void*> fontCache_; // Cache fonts by size (HFONT opaque)
};

} // namespace NomadUI
