// ¶¸ 2025 Nomad Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"
#include "../Core/NUITypes.h"
#include <string>

namespace NomadUI {

/**
 * @brief Mixer strip header: color chip + track name + routing label.
 */
class UIMixerHeader : public NUIComponent {
public:
    UIMixerHeader();

    void onRender(NUIRenderer& renderer) override;

    void setTrackName(std::string name);
    void setRouteName(std::string route);
    void setTrackColor(uint32_t argb);
    void setSelected(bool selected);
    void setIsMaster(bool isMaster);

private:
    std::string m_name;
    std::string m_route;
    uint32_t m_trackColorArgb{0xFF808080};
    bool m_selected{false};
    bool m_isMaster{false};

    // Cached theme colors
    NUIColor m_text;
    NUIColor m_textSecondary;
    NUIColor m_selectedText;
    NUIColor m_selectedBg;

    void cacheThemeColors();
    static NUIColor colorFromARGB(uint32_t argb);
};

} // namespace NomadUI
