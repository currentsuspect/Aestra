// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraPanelWindow.h"
#include "NUITypes.h"
#include "PluginHost.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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
    struct KnobControl {
        uint32_t parameterId = 0;
        std::string label;
        std::string subtitle;
        float defaultValue = 0.0f;
        NUIRect bounds;
        NUIRect knobBounds;
    };

    void buildControls();
    void layoutControls();
    void drawPitchPanel(NUIRenderer& renderer);
    void drawPitchWheel(NUIRenderer& renderer);
    void drawTexturePanel(NUIRenderer& renderer);
    void drawKnob(NUIRenderer& renderer, const KnobControl& control, int index);
    void drawMixBar(NUIRenderer& renderer);
    void drawBypassPill(NUIRenderer& renderer);
    void setParameter(uint32_t parameterId, float value);
    int hitTestKnob(NUIPoint position) const;
    std::string pitchValueString() const;

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    std::vector<KnobControl> m_knobs;
    NUIRect m_pitchPanelBounds;
    NUIRect m_texturePanelBounds;
    NUIRect m_pitchWheelBounds;
    NUIRect m_mixBounds;
    NUIRect m_bypassBounds;
    std::array<NUIRect, 7> m_intervalBounds{};

    int m_hoveredKnob = -1;
    int m_draggingKnob = -1;
    int m_hoveredInterval = -1;
    float m_dragStartY = 0.0f;
    float m_dragStartValue = 0.0f;
    bool m_pitchHovered = false;
    bool m_draggingPitch = false;
    bool m_draggingMix = false;
    bool m_bypassHovered = false;

    static constexpr float kWinW = 720.0f;
    static constexpr float kWinH = 440.0f;
    static constexpr float kPad = 14.0f;
    static constexpr float kDragRangePixels = 180.0f;
};

} // namespace AestraUI
