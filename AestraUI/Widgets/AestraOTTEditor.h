// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraPanelWindow.h"
#include "NUITypes.h"
#include "PluginHost.h"

#include <memory>
#include <string>

namespace AestraUI {

class AestraOTTEditor : public AestraPanelWindow {
public:
    explicit AestraOTTEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);
    void drawContent(NUIRenderer& renderer, const NUIRect& contentRect) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onResize(int width, int height) override;
    using AestraPanelWindow::onResize;
    void onResize() { layoutControls(); }
    void setPlatformBridge(NUIPlatformBridge* bridge) override;

private:
    void layoutControls();
    void drawKnob(NUIRenderer& renderer, const NUIRect& rect, uint32_t paramId, const char* label, bool large,
                  bool bipolar);
    void drawBypassPill(NUIRenderer& renderer);

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;

    NUIRect m_depthRect;
    NUIRect m_timeRect;
    NUIRect m_inRect;
    NUIRect m_outRect;
    NUIRect m_lowRect;
    NUIRect m_midRect;
    NUIRect m_highRect;
    NUIRect m_xloRect;
    NUIRect m_xhiRect;
    NUIRect m_bypassRect;

    // Vertical-drag knob state: which param is being dragged, and the value
    // at drag start so movement is relative, not absolute.
    int m_draggingParam = -1;
    float m_dragStartY = 0.0f;
    float m_dragStartValue = 0.0f;
    bool m_bypassHovered = false;

    static constexpr float kWinW = 560.0f;
    static constexpr float kWinH = 340.0f;
};

} // namespace AestraUI
