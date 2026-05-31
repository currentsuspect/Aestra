// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraPanelWindow.h"
#include "NUIComponent.h"
#include "NUITypes.h"
#include "Plugin/AestraEQ.h"
#include "PluginHost.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace AestraUI {

class AestraEQEditor : public AestraPanelWindow {
public:
    explicit AestraEQEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);
    ~AestraEQEditor() override;

    void drawContent(NUIRenderer& renderer, const NUIRect& contentRect) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    bool onKeyEvent(const NUIKeyEvent& event) override;
    using AestraPanelWindow::onResize;
    void onResize() { layoutControls(); }

private:
    enum class Knob { None, Enable, Freq, Gain, Q, Type, Stereo };
    enum class BandMenuAction {
        Reset,
        InvertGain,
        ToggleDynamic,
        SplitLR,
        SplitMS,
        Duplicate,
        Delete,
        Copy,
        Paste,
        ClearAll
    };
    enum class NodeQuickAction { TypePrev = 0, TypeNext, Stereo, Solo, Duplicate, Delete, Count };

    struct Band {
        uint32_t slotIndex = 0;
        bool legacySlot = true;
        uint32_t enableId = 0;
        uint32_t freqId = 0;
        uint32_t gainId = 0;
        uint32_t qId = 0;
        uint32_t typeId = 0;
        uint32_t stereoId = 0;
        std::string name;
        std::string typeName;
        bool enabled = false;
        bool usesGain = true;
        bool usesSlope = false;
        float freq = 0.5f;
        float gain = 0.5f;
        float q = 0.5f;
        float typeNorm = 0.0f;
        float stereoNorm = 0.0f;
        bool dynamicEnabled = false;
        float targetGain = 0.5f;
        float dynamicAmount = 0.0f;
        float dynamicThreshold = 0.5f;
        float dynamicKnee = 0.10f;
        float dynamicAttack = 0.18f;
        float dynamicRelease = 0.36f;
        bool sidechainLinked = true;
        Aestra::Audio::Plugins::FilterType sidechainType = Aestra::Audio::Plugins::FilterType::BandPass;
        float sidechainFreq = 0.5f;
        float sidechainQ = 0.091f;
        NUIRect cardBounds;
        NUIRect freqKnob;
        NUIRect gainKnob;
        NUIRect qKnob;
        NUIRect typeButton;
        NUIRect stereoButton;
        NUIRect enableSwitch;
    };

    struct FloatingBandPanelLayout {
        bool valid = false;
        int bandIdx = -1;
        NUIRect panel;
        NUIRect typeRect;
        NUIRect stereoRect;
        NUIRect enableRect;
        NUIRect freqRect;
        NUIRect gainRect;
        NUIRect qRect;
        bool hasQRow = false;
    };

    void buildBands();
    void layoutControls();
    void syncBandsFromPlugin();
    void appendLegacyBand(uint32_t slotIndex);
    void appendDynamicBand(uint32_t slotIndex);

    void drawBypassPill(NUIRenderer& renderer);
    void drawOutputGainPill(NUIRenderer& renderer);
    void drawPolarityPill(NUIRenderer& renderer);
    void drawComparePills(NUIRenderer& renderer);
    void drawCurveScalePill(NUIRenderer& renderer);
    void drawAnalyzerMenuPill(NUIRenderer& renderer);
    void drawAnalyzerSettingsPanel(NUIRenderer& renderer);
    void drawAnalyzerSourcePill(NUIRenderer& renderer);
    void drawAnalyzerStereoPill(NUIRenderer& renderer);
    void drawAnalyzerTiltPill(NUIRenderer& renderer);
    void drawAnalyzerDecayPill(NUIRenderer& renderer);
    void drawAnalyzerFreezePill(NUIRenderer& renderer);
    void drawAnalyzerCollisionPill(NUIRenderer& renderer);
    void drawAnalyzerCollisionStrengthPill(NUIRenderer& renderer);
    void drawResponseCurve(NUIRenderer& renderer, const NUIRect& bounds);
    void drawSpectrumBackdrop(NUIRenderer& renderer, const NUIRect& bounds);
    void drawAnalyzerCollisionOverlay(NUIRenderer& renderer, const NUIRect& bounds);
    void drawBandResponseCurves(NUIRenderer& renderer, const NUIRect& bounds);
    void drawDynamicDetectorHandle(NUIRenderer& renderer, const NUIRect& bounds);
    void drawGraphCursorReadout(NUIRenderer& renderer, const NUIRect& bounds);
    void drawAnalyzerReadout(NUIRenderer& renderer, const NUIRect& bounds);
    void drawNodeHoverTooltip(NUIRenderer& renderer, const NUIRect& bounds);
    void drawSelectedNodeQuickActions(NUIRenderer& renderer, const NUIRect& bounds);
    void drawFloatingBandWindow(NUIRenderer& renderer, const NUIRect& bounds);
    void drawBandTypeMenu(NUIRenderer& renderer);
    void drawBandStereoMenu(NUIRenderer& renderer);
    void drawBandContextMenu(NUIRenderer& renderer);
    void drawNumericEditBox(NUIRenderer& renderer);
    void drawBandCard(NUIRenderer& renderer, size_t idx);
    void drawKnob(NUIRenderer& renderer, const NUIRect& bounds, float value, bool active, const NUIColor& accent);

    void updateSpectrumSnapshot();
    void analyzerWorkerMain();

    NUIRect graphInnerBounds(const NUIRect& outer) const;
    NUIPoint graphNodePosition(size_t bandIdx, const NUIRect& graphBounds) const;
    bool createBandAtGraphPoint(const NUIPoint& position);
    bool writeDynamicBandSnapshot(int bandIdx);
    int currentFloatingBandIndex() const;
    FloatingBandPanelLayout floatingBandPanelLayout(int bandIdx, const NUIRect& bounds) const;
    bool handleFloatingBandPanelClick(const NUIMouseEvent& event);
    bool handleSelectedNodeQuickActionClick(const NUIMouseEvent& event);
    int hitTestGraphNode(float x, float y) const;
    int hitTestDynamicDetectorHandle(float x, float y) const;
    void updateBandFromGraphPosition(int bandIdx, const NUIPoint& position, NUIModifiers modifiers);
    void updateDynamicDetectorFromGraphPosition(int bandIdx, const NUIPoint& position, NUIModifiers modifiers);
    int hitTestBandCard(float x, float y, Knob& outKnob) const;
    void setBandValue(int bandIdx, Knob target, float normalizedValue);
    void setBandType(int bandIdx, uint32_t typeIndex);
    void setBandStereoMode(int bandIdx, uint32_t modeIndex);
    void resetParameterToDefault(uint32_t parameterId);
    void resetBandToDefault(int bandIdx);
    bool deleteBand(int bandIdx);
    bool resetBandControlToDefault(int bandIdx, Knob target);
    bool resetGraphBandToDefault(int bandIdx);
    void selectAdjacentBand(int direction);
    int adjacentGraphBand(int direction) const;
    void openBandContextMenu(int bandIdx, const NUIPoint& position);
    void closeBandContextMenu();
    void applyBandContextAction(BandMenuAction action);
    bool canApplyBandContextAction(BandMenuAction action) const;
    void copyBandToClipboard(int bandIdx);
    bool pasteClipboardToBand(int bandIdx);
    int findDuplicateTargetBand(int bandIdx) const;
    bool duplicateBand(int bandIdx);
    bool clearAllBands();
    bool splitBandStereoPair(int bandIdx, float sourceStereoNorm, float targetStereoNorm);
    bool nudgeSelectedBand(const NUIKeyEvent& event);
    void beginNumericEdit(int bandIdx, Knob target);
    bool handleNumericEditKey(const NUIKeyEvent& event);
    bool commitNumericEdit();
    void cancelNumericEdit();
    std::string numericEditValueString(int bandIdx, Knob target) const;
    NUIRect numericEditBounds() const;
    void captureCompareSlot(uint32_t slot);
    void applyCompareSlot(uint32_t slot);
    void switchCompareSlot(uint32_t slot);
    void copyCompareSlotToOther();

    std::string formatFreq(size_t bandIdx, float norm) const;
    std::string formatGain(float norm) const;
    std::string formatQ(float norm) const;
    std::string formatSlope(float norm) const;
    bool isBypassed() const;
    void setBypassed(bool bypassed);
    float outputGain() const;
    void setOutputGain(float normalizedGain);
    bool polarityInverted() const;
    void setPolarityInverted(bool inverted);
    float curveDbRange() const;
    void cycleCurveDbRange();
    void cycleAnalyzerSource();
    Aestra::Audio::Plugins::AestraEQ::StereoMode analyzerStereoMode() const;
    void cycleAnalyzerStereoMode();
    float analyzerTiltDbPerOct() const;
    void cycleAnalyzerTilt();
    void cycleAnalyzerDecay();
    void cycleAnalyzerCollisionStrength();
    float analyzerCollisionStrength() const;
    void setAnalyzerFrozen(bool frozen);
    void setAnalyzerCollisionEnabled(bool enabled);

    const std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    std::vector<Band> m_bands;

    NUIRect m_graphBounds;
    NUIRect m_lastGraphInner;
    NUIRect m_bypassRect;
    NUIRect m_outputGainRect;
    NUIRect m_polarityRect;
    NUIRect m_compareARect;
    NUIRect m_compareBRect;
    NUIRect m_compareCopyRect;
    NUIRect m_curveScaleRect;
    NUIRect m_analyzerMenuRect;
    NUIRect m_analyzerPanelRect;
    NUIRect m_analyzerSourceRect;
    NUIRect m_analyzerStereoRect;
    NUIRect m_analyzerTiltRect;
    NUIRect m_analyzerDecayRect;
    NUIRect m_analyzerFreezeRect;
    NUIRect m_analyzerCollisionRect;
    NUIRect m_analyzerCollisionStrengthRect;
    NUIRect m_bandStripRect;
    NUIRect m_addBandRect;
    NUIRect m_bandInspectorRect;
    NUIRect m_selectedPrevRect;
    NUIRect m_selectedNextRect;
    NUIRect m_selectedDuplicateRect;
    NUIRect m_selectedDeleteRect;
    NUIRect m_selectedCollapseRect;
    NUIRect m_selectedSlotRailRect;
    NUIRect m_typeMenuRect;
    std::array<NUIRect, 8> m_typeOptionRects{};
    NUIRect m_stereoMenuRect;
    std::array<NUIRect, 5> m_stereoOptionRects{};
    NUIRect m_bandContextMenuRect;
    std::array<NUIRect, 10> m_bandContextOptionRects{};
    NUIRect m_nodeQuickActionRect;
    std::array<NUIRect, static_cast<size_t>(NodeQuickAction::Count)> m_nodeQuickActionRects{};

    int m_hoveredBand = -1;
    bool m_hoveredBandFromGraph = false;
    int m_selectedBand = -1;
    int m_typeMenuBand = -1;
    bool m_typeMenuFromNodeQuickAction = false;
    int m_hoveredTypeOption = -1;
    int m_stereoMenuBand = -1;
    bool m_stereoMenuFromNodeQuickAction = false;
    int m_hoveredStereoOption = -1;
    int m_bandContextMenuBand = -1;
    int m_hoveredBandContextOption = -1;
    int m_hoveredNodeQuickAction = -1;
    std::chrono::steady_clock::time_point m_nodeQuickActionHoverStarted{};
    int m_draggingGraphBand = -1;
    int m_draggingDetectorBand = -1;
    int m_draggingCardBand = -1;
    Knob m_draggingKnob = Knob::None;
    NUIRect m_draggingLaneRect;
    NUIPoint m_graphDragStartPosition{};
    float m_graphDragStartFreq = 0.5f;
    float m_graphDragStartGain = 0.5f;
    float m_detectorDragStartFreq = 0.5f;
    float m_detectorDragStartQ = 0.5f;
    float m_dragStartY = 0.0f;
    float m_dragStartValue = 0.0f;
    bool m_bypassHovered = false;
    bool m_outputGainHovered = false;
    bool m_polarityHovered = false;
    bool m_compareAHovered = false;
    bool m_compareBHovered = false;
    bool m_compareCopyHovered = false;
    bool m_selectedPrevHovered = false;
    bool m_selectedNextHovered = false;
    bool m_selectedDuplicateHovered = false;
    bool m_selectedDeleteHovered = false;
    bool m_selectedCollapseHovered = false;
    bool m_selectedStereoHovered = false;
    int m_hoveredFloatingBand = -1;
    int m_hoveredSelectedSlot = -1;
    bool m_bandInspectorCollapsed = false;
    bool m_curveScaleHovered = false;
    bool m_analyzerMenuHovered = false;
    bool m_analyzerPanelOpen = false;
    bool m_analyzerSourceHovered = false;
    bool m_analyzerStereoHovered = false;
    bool m_analyzerTiltHovered = false;
    bool m_analyzerDecayHovered = false;
    bool m_analyzerFreezeHovered = false;
    bool m_analyzerCollisionHovered = false;
    bool m_analyzerCollisionStrengthHovered = false;
    bool m_analyzerFrozen = false;
    bool m_analyzerCollisionEnabled = false;
    bool m_draggingOutputGain = false;
    bool m_numericEditActive = false;
    int m_numericEditBand = -1;
    Knob m_numericEditTarget = Knob::None;
    std::string m_numericEditText;
    bool m_graphCursorVisible = false;
    NUIPoint m_graphCursorPoint{};
    uint32_t m_curveDbRangeIndex = 1;
    uint32_t m_analyzerSourceIndex = 1;
    uint32_t m_analyzerStereoIndex = 0;
    uint32_t m_analyzerDecayIndex = 1;
    std::atomic<uint32_t> m_analyzerCollisionStrengthIndex{1};
    std::atomic<uint32_t> m_analyzerTiltIndex{2};
    uint32_t m_compareActiveSlot = 0;
    std::array<std::array<float, Aestra::Audio::Plugins::AestraEQ::kParamCount>, 2> m_compareSlots{};
    struct BandClipboard {
        bool valid = false;
        bool enabled = false;
        bool hasGain = false;
        bool usesSlope = false;
        bool hasType = false;
        bool hasStereo = false;
        float freq = 0.5f;
        float freqHz = 1000.0f;
        float gain = 0.5f;
        float q = 0.5f;
        float type = 0.0f;
        float stereo = 0.0f;
        bool dynamicEnabled = false;
        float targetGain = 0.5f;
        float dynamicAmount = 0.0f;
        float dynamicThreshold = 0.5f;
        float dynamicKnee = 0.10f;
        float dynamicAttack = 0.18f;
        float dynamicRelease = 0.36f;
        bool sidechainLinked = true;
        Aestra::Audio::Plugins::FilterType sidechainType = Aestra::Audio::Plugins::FilterType::BandPass;
        float sidechainFreq = 0.5f;
        float sidechainQ = 0.091f;
    };
    BandClipboard m_bandClipboard;

    std::array<float, 160> m_spectrumMagnitudes{};
    std::array<float, 160> m_spectrumPeakMagnitudes{};
    std::array<float, 160> m_collisionMagnitudes{};
    std::string m_cachedAnalyzerReadoutFreq;
    std::string m_cachedAnalyzerReadoutDb;
    std::chrono::steady_clock::time_point m_lastAnalyzerReadoutUpdate{};
    std::array<float, Aestra::Audio::Plugins::AestraEQ::kAnalyzerWindowSize> m_analyzerWindow{};
    std::array<float, 160> m_workerResultMagnitudes{};
    std::array<float, 160> m_workerResultCollisionMagnitudes{};
    std::array<float, Aestra::Audio::Plugins::AestraEQ::kAnalyzerWindowSize> m_workerAnalyzerWindow{};
    std::array<float, Aestra::Audio::Plugins::AestraEQ::kAnalyzerWindowSize> m_workerAnalyzerCollisionWindow{};
    std::mutex m_spectrumMutex;
    std::condition_variable m_spectrumCv;
    std::thread m_spectrumWorker;
    bool m_spectrumStop = false;
    bool m_spectrumWorkPending = false;
    bool m_spectrumResultReady = false;
    bool m_spectrumCollisionWorkPending = false;
    bool m_spectrumCollisionResultReady = false;
    uint64_t m_lastAnalyzerSerial = 0;
    uint64_t m_pendingAnalyzerSerial = 0;
    uint64_t m_workerRequestedSerial = 0;
    uint64_t m_workerResultSerial = 0;

    static constexpr float kWinW = 820.0f;
    static constexpr float kWinH = 500.0f;
    static constexpr float kPad = 18.0f;
    static constexpr float kCurveH = 220.0f;
    static constexpr size_t kNumBands = Aestra::Audio::Plugins::AestraEQ::kLegacyBandCount;
};

} // namespace AestraUI
