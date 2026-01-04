// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UnitColorPicker.h"
#include "../Core/NUIThemeSystem.h"
#include "../Graphics/NUIRenderer.h"

namespace NomadUI {

UnitColorPicker::UnitColorPicker() {
    // Set initial bounds
    setBounds(NUIRect(0, 0, WIDTH, HEIGHT));
}

void UnitColorPicker::showAt(NUIPoint position) {
    setBounds(NUIRect(position.x, position.y, WIDTH, HEIGHT));
    m_isVisible = true;
    m_hoveredIndex = -1;
    repaint();
}

void UnitColorPicker::hide() {
    m_isVisible = false;
    repaint();
}

void UnitColorPicker::onRender(NUIRenderer& renderer) {
    if (!m_isVisible) return;
    
    auto bounds = getBounds();
    auto& theme = NUIThemeManager::getInstance();
    
    // Background with shadow
    NUIRect shadowRect(bounds.x + 2, bounds.y + 2, bounds.width, bounds.height);
    renderer.fillRoundedRect(shadowRect, 8.0f, NUIColor(0, 0, 0, 0.4f));
    
    // Main panel background
    renderer.fillRoundedRect(bounds, 8.0f, theme.getColor("surfaceTertiary"));
    renderer.strokeRoundedRect(bounds, 8.0f, 1.0f, theme.getColor("borderSubtle"));
    
    // Draw color swatches
    for (size_t i = 0; i < PRESET_COUNT; ++i) {
        int col = static_cast<int>(i % COLUMNS);
        int row = static_cast<int>(i / COLUMNS);
        
        float x = bounds.x + PADDING + col * (SWATCH_SIZE + SWATCH_SPACING);
        float y = bounds.y + PADDING + row * (SWATCH_SIZE + SWATCH_SPACING);
        
        NUIRect swatchRect(x, y, SWATCH_SIZE, SWATCH_SIZE);
        
        // Extract color components
        uint32_t color = PRESET_COLORS[i];
        float r = ((color >> 16) & 0xFF) / 255.0f;
        float g = ((color >> 8) & 0xFF) / 255.0f;
        float b = (color & 0xFF) / 255.0f;
        NUIColor swatchColor(r, g, b, 1.0f);
        
        // Draw swatch
        renderer.fillRoundedRect(swatchRect, 4.0f, swatchColor);
        
        // Hover effect
        if (static_cast<int>(i) == m_hoveredIndex) {
            renderer.strokeRoundedRect(swatchRect, 4.0f, 2.0f, NUIColor(1, 1, 1, 0.8f));
            
            // Outer glow
            NUIRect glowRect(x - 2, y - 2, SWATCH_SIZE + 4, SWATCH_SIZE + 4);
            renderer.strokeRoundedRect(glowRect, 5.0f, 1.5f, swatchColor.withAlpha(0.5f));
        } else {
            renderer.strokeRoundedRect(swatchRect, 4.0f, 1.0f, NUIColor(0, 0, 0, 0.3f));
        }
    }
}

bool UnitColorPicker::onMouseEvent(const NUIMouseEvent& event) {
    if (!m_isVisible) return false;
    
    auto bounds = getBounds();
    
    // Check if click is outside bounds (to close)
    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (!bounds.contains(event.position)) {
            hide();
            return true;
        }
    }
    
    // Calculate which swatch is being hovered/clicked
    int oldHovered = m_hoveredIndex;
    m_hoveredIndex = -1;
    
    float localX = event.position.x - bounds.x - PADDING;
    float localY = event.position.y - bounds.y - PADDING;
    
    if (localX >= 0 && localY >= 0) {
        int col = static_cast<int>(localX / (SWATCH_SIZE + SWATCH_SPACING));
        int row = static_cast<int>(localY / (SWATCH_SIZE + SWATCH_SPACING));
        
        if (col >= 0 && col < COLUMNS && row >= 0 && row < ROWS) {
            // Check if actually within the swatch (not in spacing)
            float withinSwatchX = localX - col * (SWATCH_SIZE + SWATCH_SPACING);
            float withinSwatchY = localY - row * (SWATCH_SIZE + SWATCH_SPACING);
            
            if (withinSwatchX <= SWATCH_SIZE && withinSwatchY <= SWATCH_SIZE) {
                m_hoveredIndex = row * COLUMNS + col;
            }
        }
    }
    
    if (oldHovered != m_hoveredIndex) {
        repaint();
    }
    
    // Handle click on swatch
    if (event.pressed && event.button == NUIMouseButton::Left && m_hoveredIndex >= 0) {
        uint32_t selectedColor = PRESET_COLORS[m_hoveredIndex];
        if (m_onColorSelected) {
            m_onColorSelected(selectedColor);
        }
        hide();
        return true;
    }
    
    return bounds.contains(event.position);
}

} // namespace NomadUI
