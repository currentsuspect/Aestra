// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once
#include "AestraPanelWindow.h"
#include "NUISlider.h"
#include "NUITypes.h"
#include "PluginHost.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace AestraUI {

class AestraVerbEditor : public AestraPanelWindow {
public:
    explicit AestraVerbEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);
    void drawContent(NUIRenderer& renderer, const NUIRect& contentRect) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onUpdate(double deltaTime) override;
    void onResize(int width, int height) override;
    using AestraPanelWindow::onResize;
    void onResize() { layoutControls(); }
    void onDragEnd() override { m_userPositioned = true; }
    void setPlatformBridge(NUIPlatformBridge* bridge) override;

private:
    struct KnobControl {
        std::shared_ptr<NUISlider> slider;
        std::string label;
        uint32_t paramId = 0;
        NUIRect bounds;
    };
    struct ModeButton {
        std::string label;
        int mode = 0;
        NUIRect bounds;
        bool hovered = false;
    };
    struct PresetButton {
        std::string label;
        int mode = 0;
        float size = 0.5f;
        float decay = 0.5f;
        float damping = 0.5f;
        float diffusion = 0.7f;
        float modRate = 0.4f;
        float modDepth = 0.14f;
        float width = 0.68f;
        float mix = 0.36f;
        std::string artworkPath;
        uint32_t artworkTexture = 0;
        bool artworkLoadAttempted = false;
        NUIRect bounds;
        bool hovered = false;
    };
    void buildControls();
    void layoutControls();
    void drawKnob(NUIRenderer& renderer, const KnobControl& k, NUIColor accent);
    void drawModeSelector(NUIRenderer& renderer, NUIColor accent);
    void drawMixSlider(NUIRenderer& renderer, NUIColor accent);
    void drawSectionLabels(NUIRenderer& renderer);
    void drawPresetStrip(NUIRenderer& renderer, NUIColor accent);
    void drawAnalysisPanels(NUIRenderer& renderer, NUIColor accent);
    void enforceBoundsInParent(bool recenterWhenPossible);
    int hitTestMode(float x, float y) const;
    int hitTestPreset(float x, float y) const;
    bool hitTestMix(float x, float y) const;
    void updateParameter(uint32_t paramId, float v);
    void applyPreset(const PresetButton& preset);
    float getParamValue(uint32_t paramId) const;
    std::string formatParameterValue(uint32_t paramId) const;

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    std::vector<KnobControl> m_knobs;
    std::array<ModeButton, 3> m_modes;
    std::array<PresetButton, 4> m_presets;
    NUIRect m_mixBounds;
    NUIRect m_mixTrack;
    int m_hoveredMode = -1;
    int m_hoveredPreset = -1;
    int m_focusedMode = -1;
    int m_focusedPreset = -1;
    int m_pressedMode = -1;
    int m_pressedPreset = -1;
    bool m_draggingMix = false;
    bool m_mixHovered = false;
    bool m_mixFocused = false;
    bool m_layouting = false;
    bool m_userPositioned = false;
    bool m_haveParentSnapshot = false;
    float m_visualPhase = 0.0f;
    float m_visualDirtyAccum = 0.0f;
    float m_modeIndicatorPosition = 0.0f;
    NUIRect m_lastParentBounds;
    static constexpr float kWinW = 760, kWinH = 560, kPad = 18, kRadius = 10;
    static constexpr float kKnobSize = 76, kKnobGap = 16;
};

} // namespace AestraUI
