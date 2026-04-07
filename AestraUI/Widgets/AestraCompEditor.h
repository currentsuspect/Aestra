// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once
#include "NUIComponent.h"
#include "NUITypes.h"
#include "PluginHost.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace AestraUI {

class AestraCompEditor : public NUIComponent {
public:
    explicit AestraCompEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);
    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void setOnClose(std::function<void()> cb) { m_onClose = std::move(cb); }

private:
    struct Knob {
        std::string label;
        std::string subtitle;
        uint32_t paramId = 0;
        float value = 0.5f;
        NUIRect bounds;
        NUIRect knobRect;
        bool dragging = false;
        bool hovered = false;
        float dragStartY = 0;
        float dragStartValue = 0;
    };

    void buildControls();
    void layoutControls();
    void drawTitleBar(NUIRenderer& renderer);
    void drawMeter(NUIRenderer& renderer, const NUIRect& bounds);
    void drawKnob(NUIRenderer& renderer, const Knob& k);
    int hitTestKnob(float x, float y) const;
    bool hitTestCloseButton(float x, float y) const;
    bool hitTestTitleBar(float x, float y) const;
    void updateKnobValue(int idx, float normalizedValue);

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    std::vector<Knob> m_knobs;
    std::function<void()> m_onClose;
    int m_hoveredKnob = -1;
    bool m_isDraggingWindow = false;
    NUIPoint m_dragStartPos, m_windowStartPos;

    static constexpr float kWinW = 480, kWinH = 300, kTitleH = 42, kPad = 14, kRadius = 12;
    static constexpr float kKnobSize = 56, kKnobGap = 16;
};

} // namespace AestraUI
