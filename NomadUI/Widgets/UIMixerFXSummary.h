// ¶¸ 2025 Nomad Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"

#include <functional>
#include <string>

namespace NomadUI {

/**
 * @brief Compact FX summary button with count badge.
 */
class UIMixerFXSummary : public NUIComponent {
public:
    UIMixerFXSummary();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setFxCount(int count);
    int getFxCount() const { return m_fxCount; }

    std::function<void()> onClicked;
    std::function<void()> onInvalidateRequested;

private:
    int m_fxCount{0};
    std::string m_labelText;
    bool m_pressed{false};
    bool m_hovered{false};

    // Cached theme colors
    NUIColor m_bg;
    NUIColor m_border;
    NUIColor m_borderHover;
    NUIColor m_textPrimary;
    NUIColor m_textSecondary;
    NUIColor m_accent;

    void cacheThemeColors();
    void requestInvalidate();
};

} // namespace NomadUI
