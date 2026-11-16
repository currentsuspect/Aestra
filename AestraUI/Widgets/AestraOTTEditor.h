// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraPanelWindow.h"
#include "EmberControls.h"
#include "NUITypes.h"
#include "PluginHost.h"

#include <memory>
#include <string>

namespace AestraUI {

class AestraOTTEditor : public AestraPanelWindow {
public:
    explicit AestraOTTEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);
    void drawContent(NUIRenderer& renderer, const NUIRect& contentRect) override;
    void onUpdate(double deltaTime) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onResize(int width, int height) override;
    using AestraPanelWindow::onResize;
    void onResize() { layoutControls(); }
    void setPlatformBridge(NUIPlatformBridge* bridge) override;

private:
    void layoutControls();
    void drawBands(NUIRenderer& renderer, bool bypassed);
    float crossoverX(int which) const; ///< 0 = low, 1 = high; window coordinates

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    EditorKit::EmberKnobBoard m_board;

    NUIRect m_well;
    NUIRect m_plot;           ///< the log-frequency / dB area inside the well
    int m_dragCrossover = -1; ///< 0 or 1 while a crossover handle is dragged
    float m_bandGainDb[3]{};
    double m_meterTimer = 0.0;

    // 24 px wider than the mock: its nine knobs and two group gaps need 640 px of row, so a 616 px row ran past
    // the window edge. 12 px margins both sides.
    static constexpr float kWinW = 664.0f;
    static constexpr float kContentH = 294.0f;
    static constexpr float kWinH = kContentH + 2.0f * AestraPanelWindow::TITLE_BAR_H; // two-row header
};

} // namespace AestraUI
