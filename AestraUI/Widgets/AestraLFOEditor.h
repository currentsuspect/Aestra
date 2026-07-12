// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraPanelWindow.h"
#include "NUITypes.h"
#include "PluginHost.h"

#include <array>
#include <memory>
#include <string>

namespace AestraUI {

class AestraLFOEditor : public AestraPanelWindow {
public:
    explicit AestraLFOEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);
    void drawContent(NUIRenderer& renderer, const NUIRect& contentRect) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onResize(int width, int height) override;
    using AestraPanelWindow::onResize;
    void onResize() { layoutControls(); }
    void setPlatformBridge(NUIPlatformBridge* bridge) override;

private:
    void layoutControls();
    void drawKnob(NUIRenderer& renderer, const NUIRect& rect, uint32_t paramId, const char* label, bool large);
    void drawRateKnob(NUIRenderer& renderer);
    void drawTargetSelector(NUIRenderer& renderer);
    void drawWaveSelector(NUIRenderer& renderer);
    void drawSyncPill(NUIRenderer& renderer);
    void drawBypassPill(NUIRenderer& renderer);

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;

    std::array<NUIRect, 3> m_targetRects{};
    std::array<NUIRect, 6> m_waveRects{};
    NUIRect m_syncRect;
    NUIRect m_rateRect;
    NUIRect m_depthRect;
    NUIRect m_phaseRect;
    NUIRect m_smoothRect;
    NUIRect m_bypassRect;

    // Vertical-drag knob state: which param is being dragged, and the value
    // at drag start so movement is relative, not absolute.
    int m_draggingParam = -1;
    float m_dragStartY = 0.0f;
    float m_dragStartValue = 0.0f;
    bool m_bypassHovered = false;

    static constexpr float kWinW = 560.0f;
    static constexpr float kWinH = 320.0f;
};

} // namespace AestraUI
