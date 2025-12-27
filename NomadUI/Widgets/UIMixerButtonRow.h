// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"
#include "../Core/NUITypes.h"

#include <functional>

namespace NomadUI {

/**
 * @brief Compact M/S/R button row for mixer strips.
 *
 * This is a lightweight, allocation-free widget (no child components) designed
 * to be cached as part of a strip static layer.
 */
class UIMixerButtonRow : public NUIComponent {
public:
    UIMixerButtonRow();

    void onRender(NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onMouseLeave() override;

    void setMuted(bool muted);
    void setSoloed(bool soloed);
    void setArmed(bool armed);

    bool isMuted() const { return m_muted; }
    bool isSoloed() const { return m_soloed; }
    bool isArmed() const { return m_armed; }

    // Callbacks fire after internal state changes.
    std::function<void(bool)> onMuteToggled;
    std::function<void(bool, NUIModifiers)> onSoloToggled;
    std::function<void(bool)> onArmToggled;

    // Used by cached parents to invalidate their static layer on hover/press changes.
    std::function<void()> onInvalidateRequested;

private:
    static constexpr int kButtonCount = 3;

    bool m_muted{false};
    bool m_soloed{false};
    bool m_armed{false};

    int m_hovered{-1};
    int m_pressed{-1};

    NUIRect m_buttonBounds[kButtonCount]{};

    // Cached theme colors
    NUIColor m_bg;
    NUIColor m_border;
    NUIColor m_text;
    NUIColor m_textOnBright;
    NUIColor m_textOnRed;
    NUIColor m_hoverBorder;

    NUIColor m_muteOn;
    NUIColor m_soloOn;
    NUIColor m_armOn;

    void cacheThemeColors();
    void layoutButtons();
    int hitTest(const NUIPoint& p) const;
    void requestInvalidate();
};

} // namespace NomadUI

