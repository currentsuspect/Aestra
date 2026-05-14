// ¶¸ 2025 Aestra Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITypes.h"
#include "TrackColorPalette.h"
#include "NUIContextMenu.h"
#include <string>

namespace AestraUI {

/**
 * @brief Mixer strip header: color chip + track name + routing label.
 */
class UIMixerHeader : public NUIComponent {
public:
    UIMixerHeader();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setTrackName(std::string name);
    void setRouteName(std::string route);
    void setTrackColor(uint32_t argb);
    void setTrackColorIndex(int index);
    void setSelected(bool selected);
    void setIsMaster(bool isMaster);

    // Fired when user picks a color from the context menu
    std::function<void(int colorIndex)> onColorChanged;

private:
    std::string m_name;
    std::string m_route;
    int m_colorIndex{-1};
    uint32_t m_trackColorArgb{0xFF808080};
    bool m_selected{false};
    bool m_isMaster{false};

    std::shared_ptr<NUIContextMenu> m_colorMenu;

    // Cached theme colors
    NUIColor m_text;
    NUIColor m_textSecondary;
    NUIColor m_selectedText;
    NUIColor m_selectedBg;
    NUIColor m_selectedBorder;

    void cacheThemeColors();
    static NUIColor colorFromARGB(uint32_t argb);
};

} // namespace AestraUI
