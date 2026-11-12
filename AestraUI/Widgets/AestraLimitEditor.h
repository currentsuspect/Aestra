// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraPanelWindow.h"
#include "EmberControls.h"
#include "NUITypes.h"
#include "PluginHost.h"

#include <array>
#include <memory>
#include <string>

namespace AestraUI {

class AestraLimitEditor : public AestraPanelWindow {
public:
    explicit AestraLimitEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);

    void drawContent(NUIRenderer& renderer, const NUIRect& contentRect) override;
    void onUpdate(double deltaTime) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    using AestraPanelWindow::onResize;
    void onResize() { layoutControls(); }
    void onResize(int width, int height) override;
    void setPlatformBridge(NUIPlatformBridge* bridge) override;

private:
    void layoutControls();
    void drawHistory(NUIRenderer& renderer, bool bypassed);
    void drawMeters(NUIRenderer& renderer, bool bypassed);

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    EditorKit::EmberKnobBoard m_board;
    EditorKit::EmberSegments m_releaseMode;

    NUIRect m_historyRect;
    NUIRect m_meterRects[2]; // In, Out
    NUIPoint m_origin;

    // The last ~4 s of gain reduction (positive dB), one bar per sample, newest last.
    static constexpr int kHistoryBars = 90;
    std::array<float, kHistoryBars> m_history{};
    float m_grDisplayDb = 0.0f;
    float m_inputDisplay = 0.0f;
    float m_outputDisplay = 0.0f;
    double m_meterTimer = 0.0;
    double m_historyTimer = 0.0;

    static constexpr float kWinW = 520.0f;
    static constexpr float kContentH = 334.0f;
    static constexpr float kWinH = kContentH + 2.0f * AestraPanelWindow::TITLE_BAR_H; // two-row header
};

} // namespace AestraUI
