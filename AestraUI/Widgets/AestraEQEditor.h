// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraPanelWindow.h"
#include "NUIComponent.h"
#include "NUITypes.h"
#include "Plugin/AestraEQ.h"
#include "PluginHost.h"

#include <array>
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
    using AestraPanelWindow::onResize;
    void onResize() { layoutControls(); }

private:
    enum class Knob { None, Freq, Gain, Q };

    struct Band {
        uint32_t enableId = 0;
        uint32_t freqId = 0;
        uint32_t gainId = 0;
        uint32_t qId = 0;
        std::string name;
        std::string typeName;
        bool enabled = false;
        bool usesGain = true;
        bool usesSlope = false;
        float freq = 0.5f;
        float gain = 0.5f;
        float q = 0.5f;
        NUIRect cardBounds;
        NUIRect freqKnob;
        NUIRect gainKnob;
        NUIRect qKnob;
    };

    void buildBands();
    void layoutControls();
    void syncBandsFromPlugin();

    void drawBypassPill(NUIRenderer& renderer);
    void drawResponseCurve(NUIRenderer& renderer, const NUIRect& bounds);
    void drawSpectrumBackdrop(NUIRenderer& renderer, const NUIRect& bounds);
    void drawBandCard(NUIRenderer& renderer, size_t idx);
    void drawKnob(NUIRenderer& renderer, const NUIRect& bounds, float value, bool active, const NUIColor& accent);

    void updateSpectrumSnapshot();
    void analyzerWorkerMain();

    NUIRect graphInnerBounds(const NUIRect& outer) const;
    NUIPoint graphNodePosition(size_t bandIdx, const NUIRect& graphBounds) const;
    int hitTestGraphNode(float x, float y) const;
    void updateBandFromGraphPosition(int bandIdx, const NUIPoint& position);
    int hitTestBandCard(float x, float y, Knob& outKnob) const;
    void setBandValue(int bandIdx, Knob target, float normalizedValue);

    std::string formatFreq(size_t bandIdx, float norm) const;
    std::string formatGain(float norm) const;
    std::string formatQ(float norm) const;
    std::string formatSlope(float norm) const;
    bool isBypassed() const;
    void setBypassed(bool bypassed);

    const std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    std::vector<Band> m_bands;

    NUIRect m_graphBounds;
    NUIRect m_lastGraphInner;
    NUIRect m_bypassRect;

    int m_hoveredBand = -1;
    int m_selectedBand = -1;
    int m_draggingGraphBand = -1;
    int m_draggingCardBand = -1;
    Knob m_draggingKnob = Knob::None;
    float m_dragStartY = 0.0f;
    float m_dragStartValue = 0.0f;
    bool m_bypassHovered = false;

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

    static constexpr float kWinW = 820.0f;
    static constexpr float kWinH = 500.0f;
    static constexpr float kPad = 18.0f;
    static constexpr float kCurveH = 220.0f;
    static constexpr size_t kNumBands = 6;
};

} // namespace AestraUI
