// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "MixerDensity.h"
#include "NUIComponent.h"
#include "NUITypes.h"

#include <functional>
#include <string>

namespace AestraUI {

/**
 * @brief The mixer's toolbar (V81-M1, FD-28 points 1 and 4): what is on screen, the density segment
 *        (Wide / Normal / Narrow), the Group-by-output toggle and Reset peaks.
 */
class UIMixerToolbar : public NUIComponent {
public:
    UIMixerToolbar() = default;

    void onRender(NUIRenderer& renderer) override;
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

    /// Hit areas, exposed so a test can click what a user would.
    NUIRect densityRect(MixerDensity d) const;
    NUIRect groupRect() const;
    NUIRect resetRect() const;

private:
    MixerDensity m_density{MixerDensity::Normal};
    bool m_group{true};
    std::string m_summary;
    int m_hover{-1}; // 0..2 density, 3 group, 4 reset
};

} // namespace AestraUI
