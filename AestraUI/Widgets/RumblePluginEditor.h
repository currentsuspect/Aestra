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

class RumblePluginEditor : public NUIComponent {
public:
    explicit RumblePluginEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);
    ~RumblePluginEditor() override = default;

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onResize(int width, int height) override;

    void setOnClose(std::function<void()> callback) { m_onClose = std::move(callback); }

private:
    struct MacroControl {
        uint32_t parameterId = 0;
        std::string title;
        std::string subtitle;
        float normalizedValue = 0.0f;
        NUIRect cardBounds;
        NUIRect trackBounds;
        NUIRect thumbBounds;
        bool isDragging = false;
    };

    void buildControls();
    void layoutControls();
    void drawTitleBar(NUIRenderer& renderer, const NUIRect& bounds);
    void drawHero(NUIRenderer& renderer, const NUIRect& bounds);
    void drawControl(NUIRenderer& renderer, const MacroControl& control, bool hovered);
    int hitTestControl(float x, float y) const;
    bool hitTestCloseButton(float x, float y) const;
    bool hitTestTitleBar(float x, float y) const;
    void updateControlValue(int controlIndex, float normalizedValue);

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    std::vector<MacroControl> m_controls;
    std::function<void()> m_onClose;

    int m_hoveredControl = -1;
    bool m_isDraggingWindow = false;
    NUIPoint m_dragStartPos;
    NUIPoint m_windowStartPos;

    static constexpr float kWindowWidth = 520.0f;
    static constexpr float kWindowHeight = 340.0f;
    static constexpr float kTitleHeight = 42.0f;
    static constexpr float kHeroHeight = 78.0f;
    static constexpr float kPadding = 14.0f;
};

} // namespace AestraUI
