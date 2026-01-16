// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include <functional>
#include <cstdint>

namespace AestraUI {

/**
 * @brief A popup color picker with preset colors for Arsenal units.
 * 
 * Displays a grid of 16 color swatches inspired by modern DAW palettes.
 * Right-click on a unit's color strip to show this picker.
 */
class UnitColorPicker : public NUIComponent {
public:
    UnitColorPicker();
    ~UnitColorPicker() override = default;
    
    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    
    // Show at specific position
    void showAt(NUIPoint position);
    void hide();
    bool isShowing() const { return m_isVisible; }
    
    // Set the callback for when a color is selected
    void setOnColorSelected(std::function<void(uint32_t)> callback) { m_onColorSelected = callback; }
    
    // Get preferred size
    static constexpr float WIDTH = 140.0f;
    static constexpr float HEIGHT = 140.0f;

private:
    bool m_isVisible = false;
    int m_hoveredIndex = -1;
    
    std::function<void(uint32_t)> m_onColorSelected;
    
    // 16 preset colors (ARGB format)
    static constexpr size_t PRESET_COUNT = 16;
    static constexpr uint32_t PRESET_COLORS[PRESET_COUNT] = {
        0xFF00A8E8,  // Cyan/Teal (default)
        0xFFBB86FC,  // Purple (Aestra accent)
        0xFFFF4081,  // Magenta/Pink
        0xFFFF5E5E,  // Red
        0xFFFF8A65,  // Coral/Orange
        0xFFFFB74D,  // Orange
        0xFFFFD54F,  // Yellow
        0xFFDCE775,  // Lime Yellow
        0xFF9CCC65,  // Light Green
        0xFF66BB6A,  // Green
        0xFF26A69A,  // Teal Green
        0xFF4DD0E1,  // Light Cyan
        0xFF42A5F5,  // Blue
        0xFF5C6BC0,  // Indigo
        0xFFAB47BC,  // Purple
        0xFF8D6E63,  // Brown
    };
    
    static constexpr int COLUMNS = 4;
    static constexpr int ROWS = 4;
    static constexpr float SWATCH_SIZE = 28.0f;
    static constexpr float SWATCH_SPACING = 6.0f;
    static constexpr float PADDING = 8.0f;
};

} // namespace AestraUI
