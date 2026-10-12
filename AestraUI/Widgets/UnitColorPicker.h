// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "TrackColorPalette.h"
#include <functional>
#include <cstdint>

namespace AestraUI {

/**
 * @brief A popup color picker with preset colors for Arsenal units.
 * 
 * Displays the eight Ember palette hues as swatches.
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
    
    // The picker offers the Ember identity hues (TRACK_PALETTE), the same ones the mixer and timeline use,
    // so a unit's colour and its channel's colour are one colour.
    static constexpr size_t PRESET_COUNT = static_cast<size_t>(PALETTE_SIZE);

    static constexpr int COLUMNS = 4;
    static constexpr int ROWS = 2;
    static constexpr float SWATCH_SIZE = 28.0f;
    static constexpr float SWATCH_SPACING = 6.0f;
    static constexpr float PADDING = 8.0f;
};

} // namespace AestraUI
