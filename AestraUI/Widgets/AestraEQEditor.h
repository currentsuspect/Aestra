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

class AestraEQEditor : public NUIComponent {
public:
    explicit AestraEQEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);
    ~AestraEQEditor() override = default;

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setOnClose(std::function<void()> callback) { m_onClose = std::move(callback); }

private:
    struct BandControl {
        uint32_t paramBase = 0;
        std::string name;
        bool enabled = true;
        float freq = 0.5f;
        float gain = 0.5f;
        float q = 0.5f;
        uint32_t type = 0;
        NUIRect bounds;
        NUIRect freqSlider;
        NUIRect gainSlider;
        NUIRect qSlider;
        NUIRect freqKnob;
        NUIRect gainKnob;
        NUIRect qKnob;
        bool dragging = false;
        enum DragTarget { None, Freq, Gain, Q } dragTarget = None;
        float dragStartX = 0;
        float dragStartValue = 0;
        bool hovered = false;
    };

    void buildControls();
    void layoutControls();
    void drawTitleBar(NUIRenderer& renderer);
    void drawResponseCurve(NUIRenderer& renderer, const NUIRect& bounds);
    void drawBandPanel(NUIRenderer& renderer, const BandControl& band);
    void updateBandValue(int bandIndex, BandControl::DragTarget target, float normalizedValue);
    int hitTestBand(float x, float y) const;
    BandControl::DragTarget hitTestSlider(float x, float y, const BandControl& band) const;
    bool hitTestCloseButton(float x, float y) const;
    bool hitTestTitleBar(float x, float y) const;
    std::string typeLabel(uint32_t type) const;
    std::string freqLabel(float norm) const;
    std::string gainLabel(float norm) const;
    std::string qLabel(float norm) const;

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    std::vector<BandControl> m_bands;
    std::function<void()> m_onClose;
    int m_hoveredBand = -1;
    bool m_isDraggingWindow = false;
    NUIPoint m_dragStartPos;
    NUIPoint m_windowStartPos;

    static constexpr float kWindowWidth = 720.0f;
    static constexpr float kWindowHeight = 400.0f;
    static constexpr float kTitleHeight = 42.0f;
    static constexpr float kCurveHeight = 160.0f;
    static constexpr float kPadding = 14.0f;
    static constexpr size_t kNumBands = 8;
};

} // namespace AestraUI
