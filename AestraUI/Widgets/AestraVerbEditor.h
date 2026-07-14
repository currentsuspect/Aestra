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
        float defaultValue = 0.0f;
        NUIRect bounds;
        bool verticalLayout = false;
    };
    struct CategoryPill {
        std::string label;
        int category = 0;
        NUIRect bounds;
        bool hovered = false;
        bool enabled = true;
    };
    struct ModeDropdownItem {
        std::string label;
        int mode = 0;
        NUIRect bounds;
        bool hovered = false;
    };
    struct PresetButton {
        std::string label;
        std::string tooltip;
        int mode = 0;
        float size = 0.5f;
        float decay = 0.5f;
        float damping = 0.5f;
        float diffusion = 0.7f;
        float modRate = 0.4f;
        float modDepth = 0.07f;
        float width = 0.68f;
        float mix = 0.36f;
        float attack = 0.0f;
        float shape = 0.5f;
        int predelaySync = 0;
        int modCharacter = 0;
        std::string artworkPath;
        uint32_t artworkTexture = 0;
        bool artworkLoadAttempted = false;
        NUIRect bounds;
        bool hovered = false;
    };
    struct ABState {
        float params[18] = {};
        bool valid = false;
    };
    void buildControls();
    void layoutControls();
    void drawKnob(NUIRenderer& renderer, const KnobControl& k, NUIColor accent);
    void drawCategoryPills(NUIRenderer& renderer, NUIColor accent);
    void drawModeDropdown(NUIRenderer& renderer, NUIColor accent);
    void drawMixSlider(NUIRenderer& renderer, NUIColor accent);
    void drawBypassPill(NUIRenderer& renderer, NUIColor accent);
    void drawFreezePill(NUIRenderer& renderer, NUIColor accent);
    void drawSectionLabels(NUIRenderer& renderer);
    void drawPresetStrip(NUIRenderer& renderer, NUIColor accent);
    void drawPresetNav(NUIRenderer& renderer, NUIColor accent);
    void drawABButtons(NUIRenderer& renderer, NUIColor accent);
    void drawMixLock(NUIRenderer& renderer, NUIColor accent);
    void drawParamRow(NUIRenderer& renderer, NUIColor accent);
    void enforceBoundsInParent(bool recenterWhenPossible);
    int hitTestCategory(float x, float y) const;
    int hitTestDropdown(float x, float y) const;
    int hitTestPreset(float x, float y) const;
    bool hitTestMix(float x, float y) const;
    bool hitTestBypass(float x, float y) const;
    bool hitTestFreeze(float x, float y) const;
    bool hitTestPresetNav(float x, float y, int& direction) const;
    bool hitTestAB(float x, float y, bool& isA) const;
    bool hitTestMixLock(float x, float y) const;
    bool presetIsInSelectedCategory(const PresetButton& preset) const;
    int presetCountForSelectedCategory() const;
    int visiblePresetRows() const;
    int maxPresetScroll() const;
    void syncCategoryFromMode();
    void updateParameter(uint32_t paramId, float v);
    void applyPreset(const PresetButton& preset);
    void loadUserPresets();
    void saveUserPreset(const std::string& name);
    void deleteUserPreset(const std::string& name);
    std::string getUserPresetDir() const;
    float getParamValue(uint32_t paramId) const;
    std::string formatParameterValue(uint32_t paramId) const;
    std::string parameterTooltip(uint32_t paramId) const;
    std::string presetTooltip(const PresetButton& preset) const;
    std::string modeTooltip(int mode) const;
    static float paramDefault(uint32_t paramId);
    static constexpr int categoryForMode(int mode);
    static constexpr int modesInCategory(int category, int* outModes);

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    std::vector<KnobControl> m_knobs;
    std::array<CategoryPill, 4> m_categoryPills;
    std::vector<ModeDropdownItem> m_dropdownItems;
    std::vector<PresetButton> m_presets;
    std::vector<PresetButton> m_userPresets;
    int m_presetScroll = 0;
    int m_selectedCategory = 0;
    bool m_dropdownOpen = false;
    NUIRect m_dropdownButtonBounds;
    NUIRect m_dropdownListBounds;
    NUIRect m_mixBounds;
    NUIRect m_mixTrack;
    NUIRect m_bypassBounds;
    NUIRect m_freezeBounds;
    NUIRect m_navPrevBounds;
    NUIRect m_navNextBounds;
    NUIRect m_saveBounds;
    NUIRect m_abBoundsA;
    NUIRect m_abBoundsB;
    NUIRect m_mixLockBounds;
    NUIRect m_paramRowBounds[3];
    int m_hoveredCategory = -1;
    int m_hoveredDropdownItem = -1;
    int m_hoveredPreset = -1;
    int m_focusedCategory = -1;
    int m_focusedPreset = -1;
    int m_pressedCategory = -1;
    int m_pressedPreset = -1;
    bool m_draggingMix = false;
    bool m_mixHovered = false;
    bool m_mixFocused = false;
    bool m_bypassHovered = false;
    bool m_freezeHovered = false;
    bool m_navPrevHovered = false;
    bool m_navNextHovered = false;
    bool m_saveHovered = false;
    bool m_abHoveredA = false;
    bool m_abHoveredB = false;
    bool m_mixLockHovered = false;
    bool m_mixLocked = false;
    bool m_layouting = false;
    bool m_userPositioned = false;
    bool m_haveParentSnapshot = false;
    NUIRect m_lastParentBounds;
    ABState m_abA;
    ABState m_abB;
    int m_activeAB = 0; // 0 = none, 1 = A, 2 = B
    static constexpr float kWinW = 880, kWinH = 620, kPad = 18, kRadius = 10;
    static constexpr float kKnobSize = 76, kKnobGap = 16;
    static constexpr int kParamCount = 18;
    static constexpr int kMaxVisiblePresets = 12;
    static constexpr int kCategoryCount = 4;
};

} // namespace AestraUI
