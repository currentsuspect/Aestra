// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraPanelWindow.h"
#include "NUISlider.h"
#include "NUITypes.h"
#include "PluginHost.h"
#include <memory>
#include <string>

namespace AestraUI {

class AestraDriftEditor : public AestraPanelWindow {
public:
    explicit AestraDriftEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);
    void drawContent(NUIRenderer& renderer, const NUIRect& contentRect) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onResize(int width, int height) override;
    using AestraPanelWindow::onResize;
    void onResize() { layoutControls(); }
    void setPlatformBridge(NUIPlatformBridge* bridge) override;

private:
    void layoutControls();
    void drawPitchWheel(NUIRenderer& renderer, float cx, float cy);
    void drawBypassPill(NUIRenderer& renderer);
    void drawMixSlider(NUIRenderer& renderer);
    std::string pitchValueString() const;
    float semitonesFromAngle(float angle) const;
    float angleFromPosition(NUIPoint center, NUIPoint pos) const;

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    NUIRect m_bypassRect;
    NUIRect m_wheelRect;
    NUIRect m_mixRect;
    bool m_bypassHovered = false;
    bool m_draggingPitch = false;
    bool m_draggingMix = false;
    float m_wheelRadius = 0.0f;

    static constexpr float kWinW = 480.0f;
    static constexpr float kWinH = 290.0f;
    static constexpr float kPad = 20.0f;
    static constexpr float kRadius = 15.0f;
    static constexpr float kMixKnobSize = 50.0f;
};

} // namespace AestraUI
