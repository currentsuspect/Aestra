// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUIContextMenu.h"
#include "NUITypes.h"
#include "Plugin/AestraEQ.h"
#include "PluginHost.h"

#include <array>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace AestraUI {

class AestraEQEditor : public NUIComponent {
public:
    explicit AestraEQEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);
    ~AestraEQEditor() override;

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onResize() { layoutControls(); }

    void setOnClose(std::function<void()> callback) { m_onClose = std::move(callback); }

private:
    struct BandControl {
        uint32_t enableId = 0;
        uint32_t freqId = 0;
        uint32_t gainId = 0;
        uint32_t qId = 0;
        std::string name;
        bool enabled = true;
        float freq = 0.5f;
        float gain = 0.5f;
        float q = 0.5f;
        uint32_t type = 0;
        bool usesGain = true;
        bool usesSlope = false;
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
    void drawBlueprintGrid(NUIRenderer& renderer, const NUIRect& bounds);
    void drawUtilityStrip(NUIRenderer& renderer, const NUIRect& bounds);
    void drawInputOutputPanel(NUIRenderer& renderer, const NUIRect& bounds, bool output);
    void drawFilterGuardPanel(NUIRenderer& renderer, const NUIRect& bounds, bool highPass);
    void drawDynamicSection(NUIRenderer& renderer, const NUIRect& bounds);
    void updateSpectrumSnapshot();
    void drawSpectrumBackdrop(NUIRenderer& renderer, const NUIRect& bounds);
    NUIRect responseGraphBounds(const NUIRect& outerBounds) const;
    bool usesGainAxis(const BandControl& band) const;
    NUIPoint graphNodePosition(const BandControl& band, const NUIRect& graphBounds) const;
    int hitTestGraphNode(float x, float y) const;
    void updateBandFromGraphPosition(int bandIndex, const NUIPoint& position);
    void drawBandPanel(NUIRenderer& renderer, const BandControl& band);
    void updateBandValue(int bandIndex, BandControl::DragTarget target, float normalizedValue);
    int hitTestBand(float x, float y) const;
    BandControl::DragTarget hitTestSlider(float x, float y, const BandControl& band) const;
    bool hitTestCloseButton(float x, float y) const;
    bool hitTestTitleBar(float x, float y) const;
    std::string typeLabel(uint32_t type) const;
    std::string bandFreqLabel(size_t bandIdx, float norm) const;
    std::string gainLabel(float norm) const;
    std::string qLabel(float norm, uint32_t type) const;
    void analyzerWorkerMain();

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    std::vector<BandControl> m_bands;
    std::function<void()> m_onClose;
    int m_hoveredBand = -1;
    int m_selectedBand = -1;
    int m_draggingGraphBand = -1;
    bool m_isDraggingWindow = false;
    NUIPoint m_dragStartPos;
    NUIPoint m_windowStartPos;
    NUIRect m_lastResponseBounds;
    std::array<float, 160> m_spectrumMagnitudes{};
    std::array<float, Aestra::Audio::Plugins::AestraEQ::kAnalyzerWindowSize> m_analyzerWindow{};
    std::array<float, 160> m_workerResultMagnitudes{};
    std::array<float, Aestra::Audio::Plugins::AestraEQ::kAnalyzerWindowSize> m_workerAnalyzerWindow{};
    std::mutex m_spectrumMutex;
    std::condition_variable m_spectrumCv;
    std::thread m_spectrumWorker;
    bool m_spectrumStop = false;
    bool m_spectrumWorkPending = false;
    bool m_spectrumResultReady = false;
    uint64_t m_lastAnalyzerSerial = 0;
    uint64_t m_pendingAnalyzerSerial = 0;
    uint64_t m_workerRequestedSerial = 0;
    uint64_t m_workerResultSerial = 0;
    std::shared_ptr<NUIContextMenu> m_bandTypeMenu;

    static constexpr float kWindowWidth = 1180.0f;
    static constexpr float kWindowHeight = 660.0f;
    static constexpr float kTitleHeight = 58.0f;
    static constexpr float kCurveHeight = 238.0f;
    static constexpr float kPadding = 12.0f;
    static constexpr size_t kNumBands = 6;
};

} // namespace AestraUI
