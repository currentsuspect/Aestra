// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "MixerDensity.h"
#include "../Core/NUIButtonMotion.h"
#include "NUIComponent.h"
#include "NUITypes.h"

#include <functional>
#include <string>

namespace AestraUI {

/**
 * @brief The mixer's toolbar (V81-M1, FD-28 points 1 and 4): what is on screen, the density segment
 *        (Wide / Normal / Narrow), the Group-by-output toggle, Reset peaks and "+ Channel".
 */
class UIMixerToolbar : public NUIComponent {
public:
    UIMixerToolbar() = default;

    void onRender(NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onMouseLeave() override;

    void setDensity(MixerDensity density);
    void setGroupByOutput(bool on);
    void setSummary(int channelCount, bool grouped);

    MixerDensity density() const { return m_density; }
    bool groupByOutput() const { return m_group; }
    const std::string& summary() const { return m_summary; }

    std::function<void(MixerDensity)> onDensityChanged;
    std::function<void(bool)> onGroupToggled;
    std::function<void()> onResetPeaks;
    /// "+ Channel": the mixer starts with one strip and grows when the user asks.
    std::function<void()> onAddChannel;

    /// Hit areas, exposed so a test can click what a user would.
    NUIRect densityRect(MixerDensity d) const;
    NUIRect groupRect() const;
    NUIRect resetRect() const;
    NUIRect addRect() const;

private:
    bool compact() const;
    float segW() const;
    float toggleW() const;
    float resetW() const;
    float addW() const;

    MixerDensity m_density{MixerDensity::Normal};
    bool m_group{true};
    std::string m_summary;
    ButtonMotion m_resetMotion{ButtonGroup::Action}; ///< the two pills share the Action group's motion
    ButtonMotion m_addMotion{ButtonGroup::Action};
    int m_pressed{-1}; // the pill held down (4 reset, 5 add), for its press dip
    int m_hover{-1}; // 0..2 density, 3 group, 4 reset, 5 add channel
};

} // namespace AestraUI
