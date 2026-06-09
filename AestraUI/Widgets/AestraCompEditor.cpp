// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraCompEditor.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "Plugin/AestraComp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace AestraUI {

namespace {
constexpr float kPi = 3.14159265358979323846f;


NUIColor insetBg() { return NUIColor(0.018f, 0.020f, 0.024f, 0.960f); }
NUIColor curveBg() { return NUIColor(0.035f, 0.037f, 0.042f, 1.0f); }
NUIColor amber() { return NUIColor(0.88f, 0.63f, 0.14f, 1.0f); }
NUIColor purple() { return NUIColor(0.50f, 0.35f, 0.94f, 1.0f); }
NUIColor dimText() { return NUIColor(1.0f, 1.0f, 1.0f, 0.30f); }

float levelToNorm(float linear) {
    const float db = linear > 1.0e-8f ? 20.0f * std::log10(linear) : -60.0f;
    return std::clamp((db + 60.0f) / 66.0f, 0.0f, 1.0f);
}

float smoothMeter(float current, float target, float attack, float release) {
    const float coeff = target > current ? attack : release;
    return current + (target - current) * coeff;
}

float thresholdDbFromNorm(float v) { return -60.0f + std::clamp(v, 0.0f, 1.0f) * 60.0f; }
float ratioFromNorm(float v) { return 1.0f + std::clamp(v, 0.0f, 1.0f) * 19.0f; }
float kneeDbFromNorm(float v) { return std::clamp(v, 0.0f, 1.0f) * 24.0f; }

float compTransferDb(float inputDb, float thresholdDb, float ratio, float kneeDb) {
    if (kneeDb < 0.01f) {
        if (inputDb <= thresholdDb) return inputDb;
        return thresholdDb + (inputDb - thresholdDb) / ratio;
    }
    const float halfKnee = kneeDb * 0.5f;
    if (inputDb < thresholdDb - halfKnee) return inputDb;
    if (inputDb > thresholdDb + halfKnee)
        return thresholdDb + (inputDb - thresholdDb) / ratio;
    const float x = inputDb - thresholdDb + halfKnee;
    return inputDb + (1.0f / ratio - 1.0f) * (x * x) / (2.0f * kneeDb);
}
} // namespace

AestraCompEditor::AestraCompEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraCompEditor");
    setPanelTitle("Aestra Compressor");
    setSize(kWinW, kWinH);
    setEnforceParentBounds(true);

    // Precompute log-frequency LUT: pixel index -> fractional bin index
    constexpr float kMinFreq = 20.0f;
    constexpr float kMaxFreq = 20000.0f;
    constexpr float kLogMin = std::log(kMinFreq);
    constexpr float kLogRange = std::log(kMaxFreq) - kLogMin;
    const float binHz = 48000.0f / static_cast<float>(Aestra::Audio::Plugins::AestraComp::kFftSize);
    for (int i = 0; i < kLogFreqLutSize; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kLogFreqLutSize - 1);
        const float freq = kMinFreq * std::exp(t * kLogRange);
        m_logFreqLut[i] = freq / binHz;
    }

    buildControls();
}

void AestraCompEditor::setPlatformBridge(NUIPlatformBridge* bridge) {
    AestraPanelWindow::setPlatformBridge(bridge);
    for (auto& control : m_controls) {
        if (control.slider) control.slider->setPlatformBridge(bridge);
    }
}

void AestraCompEditor::buildControls() {
    m_controls.clear();
    if (!m_instance) return;

    using Comp = Aestra::Audio::Plugins::AestraComp;
    struct Meta { const char* label; uint32_t id; float defaultValue; bool primary; };

    const Meta controls[] = {
        {"Threshold",  Comp::kThreshold,   0.6667f, true},
        {"Ratio",      Comp::kRatio,       0.1579f, true},
        {"Attack",     Comp::kAttack,      0.0991f, true},
        {"Release",    Comp::kRelease,     0.1414f, true},
        {"Knee",       Comp::kKnee,        0.0f,    true},
        {"Makeup",     Comp::kMakeup,      0.0f,    false},
        {"Mix",        Comp::kMix,         1.0f,    false},
        {"Input",      Comp::kInputGain,   0.5f,    false},
        {"Output",     Comp::kOutputGain,  0.5f,    false},
        {"Det HPF",    Comp::kDetectorHPF, 0.0f,    false},
    };

    for (const auto& item : controls) {
        KnobControl control;
        control.label = item.label;
        control.paramId = item.id;
        control.isPrimary = item.primary;

        auto slider = std::make_shared<NUISlider>();
        slider->setStyle(NUISlider::Style::Rotary);
        slider->setRange(0.0, 1.0);
        slider->setValue(std::clamp(m_instance->getParameter(item.id), 0.0f, 1.0f));
        slider->setPlatformBridge(getPlatformBridge());
        slider->setOnValueChange([this, paramId = item.id](double value) {
            if (m_instance) {
                m_instance->setParameter(paramId, static_cast<float>(std::clamp(value, 0.0, 1.0)));
                repaint();
            }
        });

        control.slider = slider;
        addChild(slider);
        m_controls.push_back(control);
    }

    layoutControls();
}

void AestraCompEditor::layoutControls() {
    auto b = getBounds();
    if (b.width <= 0.0f || b.height <= 0.0f) {
        setBounds(b.x, b.y, kWinW, kWinH);
        b = getBounds();
    }

    const float contentX = b.x + kPad;
    const float contentW = b.width - kPad * 2.0f;
    const float titleH = AestraPanelWindow::TITLE_BAR_H;

    // Transfer curve strip
    const float curveY = b.y + titleH + 10.0f;
    m_transferCurveRect = NUIRect(contentX, curveY, contentW, 150.0f);

    // Meters row (IN / OUT / GR)
    const float metersY = m_transferCurveRect.bottom() + 8.0f;
    m_metersRect = NUIRect(contentX, metersY, contentW, 30.0f);

    // Primary knobs row
    const float primDivY = m_metersRect.bottom() + 10.0f;
    const float primKnobY = primDivY + 28.0f;
    const float primCellW = (contentW - 8.0f * 4.0f) / 5.0f;
    for (int i = 0; i < 5 && i < static_cast<int>(m_controls.size()); ++i) {
        const float x = contentX + static_cast<float>(i) * (primCellW + 8.0f);
        m_controls[i].bounds = NUIRect(x, primKnobY, primCellW, 100.0f);
        const NUIRect knobRect(x + (primCellW - kKnobSizePrimary) * 0.5f, primKnobY + 16.0f,
                               kKnobSizePrimary, kKnobSizePrimary);
        if (m_controls[i].slider) m_controls[i].slider->setBounds(knobRect);
    }

    // Utility knobs row
    const float utilDivY = primKnobY + 114.0f;
    const float utilKnobY = utilDivY + 20.0f;
    const float utilCellW = (contentW - 8.0f * 4.0f) / 5.0f;
    for (int i = 5; i < 10 && i < static_cast<int>(m_controls.size()); ++i) {
        const float x = contentX + static_cast<float>(i - 5) * (utilCellW + 8.0f);
        m_controls[i].bounds = NUIRect(x, utilKnobY, utilCellW, 82.0f);
        const float knobOffsetY = (82.0f - kKnobSizeSecondary) * 0.5f;
        const NUIRect knobRect(x + (utilCellW - kKnobSizeSecondary) * 0.5f, utilKnobY + knobOffsetY,
                               kKnobSizeSecondary, kKnobSizeSecondary);
        if (m_controls[i].slider) m_controls[i].slider->setBounds(knobRect);
    }

    // Utility buttons row
    const float btnY = utilKnobY + 94.0f;
    const float btnH = 22.0f;
    const float btnGap = 6.0f;
    const float btnW = (contentW - btnGap * 4.0f) / 5.0f;
    m_bypassRect = NUIRect(contentX, btnY, btnW, btnH);
    m_autoRect = NUIRect(contentX + (btnW + btnGap), btnY, btnW, btnH);
    m_linkRect = NUIRect(contentX + (btnW + btnGap) * 2.0f, btnY, btnW, btnH);
    m_mixLockRect = NUIRect(contentX + (btnW + btnGap) * 3.0f, btnY, btnW, btnH);
    m_resetRect = NUIRect(contentX + (btnW + btnGap) * 4.0f, btnY, btnW, btnH);

    // Mode pills — stacked vertically, centered in modes column
    const float statsX = m_transferCurveRect.right() - kRightMargin - kModesW - kDividerW - kStatsW;
    const float modesX = statsX + kStatsW + kDividerW;
    const float pillsX = modesX + (kModesW - kPillW) * 0.5f;
    const float pillsY = m_transferCurveRect.y + (150.0f - (3.0f * kPillH + 2.0f * kPillGap)) * 0.5f + 6.0f;
    for (int i = 0; i < 3; ++i) {
        m_modePills[i] = NUIRect(pillsX, pillsY + static_cast<float>(i) * (kPillH + kPillGap), kPillW, kPillH);
    }
}

void AestraCompEditor::onResize(int width, int height) {
    (void)width;
    (void)height;
    layoutControls();
    AestraPanelWindow::onResize(width, height);
}

void AestraCompEditor::syncControlsFromPlugin() {
    if (!m_instance) return;
    for (auto& control : m_controls) {
        if (control.slider)
            control.slider->setValue(std::clamp(m_instance->getParameter(control.paramId), 0.0f, 1.0f));
    }
}

void AestraCompEditor::onUpdate(double deltaTime) {
    AestraPanelWindow::onUpdate(deltaTime);
    m_meterTimer += deltaTime;
    if (m_meterTimer < 1.0 / 30.0) return;
    m_meterTimer = 0.0;

    syncControlsFromPlugin();
    if (auto comp = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraComp>(m_instance)) {
        const float gr = std::clamp(comp->getCurrentGainReductionDb(), 0.0f, 48.0f);
        const float in = std::clamp(comp->getInputLevel(), 0.0f, 16.0f);
        const float out = std::clamp(comp->getOutputLevel(), 0.0f, 16.0f);
        m_grDisplayDb = smoothMeter(m_grDisplayDb, gr, 0.58f, 0.18f);
        m_inputDisplay = smoothMeter(m_inputDisplay, in, 0.55f, 0.16f);
        m_outputDisplay = smoothMeter(m_outputDisplay, out, 0.55f, 0.16f);

        // Read FFT spectrum with exponential smoothing
        const auto& fft = comp->getFftSpectrum();
        constexpr float kSpectrumSmooth = 0.35f;
        for (uint32_t i = 0; i < kDisplayBins; ++i) {
            m_inputSpectrum[i] += (fft.inputBins[i] - m_inputSpectrum[i]) * kSpectrumSmooth;
            m_outputSpectrum[i] += (fft.outputBins[i] - m_outputSpectrum[i]) * kSpectrumSmooth;
        }
    }
    repaint();
}

void AestraCompEditor::drawSectionLabel(NUIRenderer& renderer, const char* label, float y, float leftX, float rightX) {
    auto& theme = NUIThemeManager::getInstance();
    renderer.drawText(label, {leftX, y}, 8.5f, dimText());
    const NUIPoint lineStart{leftX + 56.0f, y + 4.0f};
    const NUIPoint lineEnd{rightX, y + 4.0f};
    renderer.drawLine(lineStart, lineEnd, 1.0f, NUIColor(1, 1, 1, 0.06f));
}

void AestraCompEditor::drawTransferCurve(NUIRenderer& renderer, NUIColor accent) {
    auto& theme = NUIThemeManager::getInstance();
    const auto& b = m_transferCurveRect;

    const float thrNorm = m_instance ? m_instance->getParameter(Aestra::Audio::Plugins::AestraComp::kThreshold) : 0.67f;
    const float ratNorm = m_instance ? m_instance->getParameter(Aestra::Audio::Plugins::AestraComp::kRatio) : 0.16f;
    const float kneeNorm = m_instance ? m_instance->getParameter(Aestra::Audio::Plugins::AestraComp::kKnee) : 0.0f;
    const float thrDb = thresholdDbFromNorm(thrNorm);
    const float ratio = ratioFromNorm(ratNorm);
    const float kneeDb = kneeDbFromNorm(kneeNorm);

    // Stats sidebar — right column, premium typography
    const float statsX = b.right() - kRightMargin - kModesW - kDividerW - kStatsW;
    const float modesX = statsX + kStatsW + kDividerW;

    // Curve area bounds (left of stats column)
    const float curveLeft = b.x + 28.0f;
    const float curveTop = b.y + 18.0f;
    const float curveW = statsX - kDividerW - curveLeft - 6.0f;
    const float curveH = b.height - 44.0f;
    const float curveRight = curveLeft + curveW;
    const float curveBottom = curveTop + curveH;

    // Zone backgrounds — curve gets its own bg, stats + modes get a slightly darker treatment
    renderer.fillRoundedRect({curveLeft - 4.0f, b.y, curveW + 8.0f, b.height}, 6.0f, curveBg());
    renderer.fillRoundedRect({statsX - 4.0f, b.y, kStatsW + kModesW + kDividerW + kRightMargin + 8.0f, b.height},
                             6.0f, NUIColor(0.014f, 0.014f, 0.018f, 0.92f));

    renderer.strokeRoundedRect(b, 6.0f, 1.0f, NUIColor(1, 1, 1, 0.05f));
    // Subtle top highlight
    renderer.drawLine({b.x + 6.0f, b.y + 1.0f}, {b.x + b.width - 6.0f, b.y + 1.0f},
                      1.0f, NUIColor(1, 1, 1, 0.06f));

    // Spectrum analyzer — log frequency axis, -90 to 0 dB
    {
        constexpr float kMinDb = -90.0f;
        constexpr float kDbRange = 90.0f;
        const int numCols = std::min(static_cast<int>(curveW), kMaxCurvePixels);
        if (numCols > 0) {
            m_numSpectrumPts = numCols + 1;
            const float invNumCols = 1.0f / static_cast<float>(numCols);
            const float binMax = static_cast<float>(Aestra::Audio::Plugins::AestraComp::kFftBins - 2);

            for (int px = 0; px <= numCols; ++px) {
                const float t = static_cast<float>(px) * invNumCols;
                const int lutIdx = std::min(px, kLogFreqLutSize - 1);
                const float binF = m_logFreqLut[lutIdx];
                const int bin = std::clamp(static_cast<int>(binF), 0, static_cast<int>(binMax));
                const float frac = binF - static_cast<float>(bin);

                const float inDb = m_inputSpectrum[bin] * (1.0f - frac) + m_inputSpectrum[bin + 1] * frac;
                const float outDb = m_outputSpectrum[bin] * (1.0f - frac) + m_outputSpectrum[bin + 1] * frac;

                const float x = curveLeft + t * curveW;
                const float inY = curveBottom - std::clamp((inDb - kMinDb) / kDbRange, 0.0f, 1.0f) * curveH;
                const float outY = curveBottom - std::clamp((outDb - kMinDb) / kDbRange, 0.0f, 1.0f) * curveH;

                m_spectrumInputPts[px] = {x, inY};
                m_spectrumOutputPts[px] = {x, outY};
                m_spectrumBaseline[px] = {x, curveBottom};
            }

            renderer.fillWaveform(m_spectrumInputPts, m_spectrumBaseline, m_numSpectrumPts,
                                  NUIColor(0.35f, 0.45f, 1.0f, 0.08f));
            for (int i = 1; i < m_numSpectrumPts; ++i) {
                renderer.drawLine(m_spectrumInputPts[i - 1], m_spectrumInputPts[i], 1.0f,
                                  NUIColor(0.35f, 0.45f, 1.0f, 0.30f));
            }

            renderer.fillWaveform(m_spectrumOutputPts, m_spectrumBaseline, m_numSpectrumPts,
                                  accent.withAlpha(0.10f));
            for (int i = 1; i < m_numSpectrumPts; ++i) {
                renderer.drawLine(m_spectrumOutputPts[i - 1], m_spectrumOutputPts[i], 1.2f,
                                  accent.withAlpha(0.50f));
            }
        }
    }

    const float grDb = m_grDisplayDb;

    // Vertical divider: curve | stats
    renderer.drawLine({statsX - kDividerW, b.y + 8.0f}, {statsX - kDividerW, b.bottom() - 8.0f},
                      kDividerW, NUIColor(1, 1, 1, 0.14f));
    // Vertical divider: stats | modes
    renderer.drawLine({modesX - kDividerW, b.y + 8.0f}, {modesX - kDividerW, b.bottom() - 8.0f},
                      kDividerW, NUIColor(1, 1, 1, 0.14f));

    // GR section
    renderer.drawText("GR", {statsX + 6.0f, b.y + 12.0f}, 8.0f, NUIColor(1, 1, 1, 0.28f));
    char grBuf[16]{};
    std::snprintf(grBuf, sizeof(grBuf), "-%.1f dB", grDb);
    renderer.drawText(grBuf, {statsX + 6.0f, b.y + 24.0f}, 14.0f, amber().withAlpha(0.95f));

    // Thin separator
    renderer.drawLine({statsX + 8.0f, b.y + 46.0f}, {statsX + kStatsW - 4.0f, b.y + 46.0f},
                      0.5f, NUIColor(1, 1, 1, 0.05f));

    // RATIO section
    renderer.drawText("RATIO", {statsX + 6.0f, b.y + 54.0f}, 8.0f, NUIColor(1, 1, 1, 0.28f));
    char ratBuf[16]{};
    if (ratio >= 19.95f) std::snprintf(ratBuf, sizeof(ratBuf), "20:1");
    else std::snprintf(ratBuf, sizeof(ratBuf), "%d:1", static_cast<int>(std::round(ratio)));
    renderer.drawText(ratBuf, {statsX + 6.0f, b.y + 66.0f}, 14.0f, NUIColor(1, 1, 1, 0.88f));

    // Thin separator
    renderer.drawLine({statsX + 8.0f, b.y + 88.0f}, {statsX + kStatsW - 4.0f, b.y + 88.0f},
                      0.5f, NUIColor(1, 1, 1, 0.05f));

    // KNEE section
    renderer.drawText("KNEE", {statsX + 6.0f, b.y + 96.0f}, 8.0f, NUIColor(1, 1, 1, 0.28f));
    char kneeBuf[16]{};
    std::snprintf(kneeBuf, sizeof(kneeBuf), "%.0f dB", kneeDb);
    renderer.drawText(kneeBuf, {statsX + 6.0f, b.y + 108.0f}, 14.0f, NUIColor(1, 1, 1, 0.88f));

    // Axis labels
    renderer.drawText("-60", {b.x + 4.0f, curveBottom - 6.0f}, 7.0f, dimText());
    renderer.drawText("0 dB", {b.x + 6.0f, curveTop + 2.0f}, 7.0f, dimText());

    // 1:1 reference line
    renderer.drawLine({curveLeft, curveBottom}, {curveRight, curveTop}, 1.0f, NUIColor(1, 1, 1, 0.06f));

    // Threshold vertical guide
    const float thrNormClamped = std::clamp(thrNorm, 0.0f, 1.0f);
    const float thrX = curveLeft + curveW * thrNormClamped;
    renderer.drawLine({thrX, curveTop}, {thrX, curveBottom}, 1.0f, accent.withAlpha(0.15f));

    // Build transfer curve path
    constexpr int kCurveSteps = 128;
    for (int i = 0; i <= kCurveSteps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kCurveSteps);
        const float inputDb = -60.0f + t * 60.0f;
        const float outputDb = compTransferDb(inputDb, thrDb, ratio, kneeDb);
        const float x = curveLeft + t * curveW;
        const float yNorm = std::clamp((outputDb + 60.0f) / 60.0f, 0.0f, 1.0f);
        const float y = curveBottom - yNorm * curveH;
        m_curvePts[i] = {x, y};
    }

    for (int i = 1; i <= kCurveSteps; ++i) {
        renderer.drawLine(m_curvePts[i - 1], m_curvePts[i], 1.8f, accent.withAlpha(0.92f));
    }

    // Knee point
    const float kneeX = thrX;
    const float kneeOutDb = compTransferDb(thrDb, thrDb, ratio, kneeDb);
    const float kneeYNorm = std::clamp((kneeOutDb + 60.0f) / 60.0f, 0.0f, 1.0f);
    const float kneeY = curveBottom - kneeYNorm * curveH;
    renderer.fillCircle({kneeX, kneeY}, 4.0f, accent);
    char kneeLabelBuf[16]{};
    std::snprintf(kneeLabelBuf, sizeof(kneeLabelBuf), "-%ddB", static_cast<int>(std::round(-thrDb)));
    const float kneeLabelX = std::min(kneeX + 10.0f, curveRight - 44.0f);
    renderer.drawText(kneeLabelBuf, {kneeLabelX, kneeY - 14.0f}, 8.0f, accent.withAlpha(0.85f));

    // Mode pills
    const char* pillLabels[] = {"Clean", "Classic", "Optical"};
    const uint32_t currentMode = m_instance
        ? static_cast<uint32_t>(std::round(std::clamp(
              m_instance->getParameter(Aestra::Audio::Plugins::AestraComp::kCompMode) * 2.0f, 0.0f, 2.0f)))
        : 0;
    for (int i = 0; i < 3; ++i) {
        const bool sel = (static_cast<uint32_t>(i) == currentMode);
        const NUIRect& r = m_modePills[i];

        if (sel) {
            // Outer glow — wider, softer
            renderer.fillRoundedRect({r.x - 3.0f, r.y - 3.0f, r.width + 6.0f, r.height + 6.0f},
                                     8.0f, accent.withAlpha(0.15f));
            // Body
            renderer.fillRoundedRect(r, 5.0f, accent.withAlpha(0.80f));
            // Top specular
            renderer.drawLine({r.x + 6.0f, r.y + 1.0f}, {r.x + r.width - 6.0f, r.y + 1.0f},
                              1.0f, NUIColor(1, 1, 1, 0.22f));
            // Border
            renderer.strokeRoundedRect(r, 5.0f, 1.0f, accent);
            // Label
            renderer.drawTextCentered(pillLabels[i], r, 9.5f, NUIColor(1, 1, 1, 0.98f));
        } else {
            // Body — deep dark inset
            renderer.fillRoundedRect(r, 5.0f, NUIColor(0.018f, 0.018f, 0.024f, 0.90f));
            // Top inner shadow
            renderer.drawLine({r.x + 5.0f, r.y + 1.0f}, {r.x + r.width - 5.0f, r.y + 1.0f},
                              0.5f, NUIColor(0, 0, 0, 0.30f));
            // Border — visible but not loud
            renderer.strokeRoundedRect(r, 5.0f, 1.0f, NUIColor(1, 1, 1, 0.10f));
            // Label — readable but clearly inactive
            renderer.drawTextCentered(pillLabels[i], r, 9.0f, NUIColor(1, 1, 1, 0.42f));
        }
    }
}

void AestraCompEditor::drawMeters(NUIRenderer& renderer, NUIColor accent) {
    auto& theme = NUIThemeManager::getInstance();
    const auto& b = m_metersRect;
    const float thirdW = (b.width - 16.0f) / 3.0f;

    auto drawMeter = [&](float x, const char* label, float norm, NUIColor color) {
        const NUIRect meterBounds(x, b.y, thirdW, b.height);
        renderer.fillRoundedRect(meterBounds, 5.0f, NUIColor(0.008f, 0.008f, 0.012f, 1.0f));
        renderer.strokeRoundedRect(meterBounds, 5.0f, 1.0f, NUIColor(1, 1, 1, 0.055f));

        // Fill
        const float fillNorm = levelToNorm(norm);
        if (fillNorm > 0.001f) {
            renderer.fillRoundedRect({meterBounds.x, meterBounds.y, meterBounds.width * fillNorm, meterBounds.height},
                                     5.0f, color.withAlpha(0.88f));
        }

        // Text overlaid on the meter
        const float textY = b.y + (b.height - 8.0f) * 0.5f;
        renderer.drawText(label, {x + 8.0f, textY}, 8.0f, NUIColor(1, 1, 1, 0.50f));

        char valBuf[16]{};
        const float db = norm > 1.0e-8f ? 20.0f * std::log10(norm) : -60.0f;
        std::snprintf(valBuf, sizeof(valBuf), "%.1f dB", db);
        const NUISize valSize = renderer.measureText(valBuf, 8.0f);
        renderer.drawText(valBuf, {x + thirdW - valSize.width - 14.0f, textY}, 8.0f,
                          NUIColor(1, 1, 1, 0.90f));
    };

    drawMeter(b.x, "IN", m_inputDisplay, purple());
    drawMeter(b.x + thirdW + 8.0f, "OUT", m_outputDisplay, purple());
    drawMeter(b.x + (thirdW + 8.0f) * 2.0f, "GR", m_grDisplayDb > 0.05f ? std::pow(10.0f, -m_grDisplayDb / 20.0f) : 1.0f, amber());
}

void AestraCompEditor::drawControl(NUIRenderer& renderer, const KnobControl& control, NUIColor accent) {
    auto& theme = NUIThemeManager::getInstance();
    const float value = control.slider ? static_cast<float>(control.slider->getValue()) : 0.0f;
    const bool prim = control.isPrimary;
    const NUIColor knobAccent = prim ? accent : accent.withAlpha(0.72f);

    // Knob cell background + border
    const NUIColor cellBg(0.035f, 0.035f, 0.040f, 0.96f);
    const NUIColor cellBorder = prim ? NUIColor(0.18f, 0.13f, 0.28f, 1.0f) : NUIColor(0.118f, 0.118f, 0.133f, 1.0f);
    renderer.fillRoundedRect(control.bounds, 8.0f, cellBg);
    renderer.strokeRoundedRect(control.bounds, 8.0f, 1.0f, cellBorder);
    // Subtle top highlight on primary cells
    if (prim) {
        renderer.drawLine({control.bounds.x + 8.0f, control.bounds.y + 1.0f},
                          {control.bounds.x + control.bounds.width - 8.0f, control.bounds.y + 1.0f},
                          0.5f, NUIColor(1, 1, 1, 0.06f));
    }

    const NUIRect knobRect = control.slider ? control.slider->getBounds() : NUIRect();
    const float cx = knobRect.center().x;
    const float cy = knobRect.center().y;
    const float r = std::min(knobRect.width, knobRect.height) * 0.35f;

    // Glow + body
    renderer.fillCircle({cx, cy}, r + 6.0f, knobAccent.withAlpha(0.06f));
    renderer.fillCircle({cx, cy}, r, insetBg());
    renderer.strokeCircle({cx, cy}, r + 2.5f, 1.5f, NUIColor(1, 1, 1, 0.07f));

    // Arc
    const float start = kPi * 0.75f;
    const float sweep = kPi * 1.5f * value;
    std::array<NUIPoint, 32> arc{};
    for (size_t i = 0; i < arc.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(arc.size() - 1);
        const float a = start + sweep * t;
        arc[i] = {cx + std::cos(a) * (r + 4.0f), cy + std::sin(a) * (r + 4.0f)};
    }
    renderer.drawPolyline(arc.data(), static_cast<int>(arc.size()), prim ? 2.8f : 2.2f, knobAccent.withAlpha(0.90f));

    // Pointer
    const float pa = start + sweep;
    const float pointerLen = r - (prim ? 4.0f : 3.0f);
    renderer.drawLine({cx, cy}, {cx + std::cos(pa) * pointerLen, cy + std::sin(pa) * pointerLen},
                      prim ? 1.8f : 1.5f, theme.getColor("textPrimary").withAlpha(0.82f));

    // Label (top of cell) — spec: rgba(255,255,255,0.5), 9px
    renderer.drawTextCentered(control.label, {control.bounds.x, control.bounds.y + 2.0f, control.bounds.width, 12.0f},
                              9.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.50f));

    // Value (bottom of cell) — primary: muted lavender #9b8fcc, utility: spec rgba(255,255,255,0.85)
    const float valueY = prim ? control.bounds.bottom() - 18.0f
                              : knobRect.bottom() + 4.0f;
    const NUIColor valueColor = prim
        ? NUIColor(0.608f, 0.561f, 0.800f, 1.0f)   // #9b8fcc
        : NUIColor(1.0f, 1.0f, 1.0f, 0.85f);        // rgba(255,255,255,0.85)
    renderer.drawTextCentered(valueText(control.paramId),
                              {control.bounds.x, valueY, control.bounds.width, 14.0f},
                              11.0f, valueColor);
}

void AestraCompEditor::drawUtilityButtons(NUIRenderer& renderer, NUIColor accent) {
    auto& theme = NUIThemeManager::getInstance();
    struct BtnInfo { NUIRect bounds; bool on; bool hov; const char* label; };
    BtnInfo btns[] = {
        {m_bypassRect, isBypassed(), m_bypassHovered, "BYP"},
        {m_autoRect, m_autoEnabled, m_autoHovered, "AUTO"},
        {m_linkRect, m_linkEnabled, m_linkHovered, "LINK"},
        {m_mixLockRect, m_mixLocked, m_mixLockHovered, "MIX LOCK"},
        {m_resetRect, false, m_resetHovered, "RESET"},
    };

    for (auto& btn : btns) {
        const bool byp = btn.label[0] == 'B' && btn.label[1] == 'Y';
        const NUIColor activeAccent = byp ? NUIColor(0.92f, 0.28f, 0.22f, 1.0f) : accent;

        if (btn.on) {
            renderer.fillRoundedRect({btn.bounds.x - 1.5f, btn.bounds.y - 1.5f,
                                      btn.bounds.width + 3.0f, btn.bounds.height + 3.0f},
                                     6.5f, activeAccent.withAlpha(0.10f));
            renderer.fillRoundedRect(btn.bounds, 5.0f, activeAccent.withAlpha(0.55f));
            renderer.drawLine({btn.bounds.x + 5.0f, btn.bounds.y + 1.0f},
                              {btn.bounds.x + btn.bounds.width - 5.0f, btn.bounds.y + 1.0f},
                              1.0f, NUIColor(1, 1, 1, 0.15f));
            renderer.strokeRoundedRect(btn.bounds, 5.0f, 1.0f, activeAccent.withAlpha(0.80f));
            renderer.drawTextCentered(btn.label, btn.bounds, 9.0f, NUIColor(1, 1, 1, 0.95f));
        } else {
            const NUIColor bg = btn.hov ? NUIColor(0.04f, 0.04f, 0.048f, 0.90f)
                                        : NUIColor(0.022f, 0.022f, 0.028f, 0.90f);
            renderer.fillRoundedRect(btn.bounds, 5.0f, bg);
            renderer.drawLine({btn.bounds.x + 5.0f, btn.bounds.y + 1.0f},
                              {btn.bounds.x + btn.bounds.width - 5.0f, btn.bounds.y + 1.0f},
                              0.5f, NUIColor(0, 0, 0, 0.25f));
            const NUIColor border = btn.hov ? NUIColor(1, 1, 1, 0.16f) : NUIColor(1, 1, 1, 0.08f);
            renderer.strokeRoundedRect(btn.bounds, 5.0f, 1.0f, border);
            const NUIColor text = btn.hov ? NUIColor(1, 1, 1, 0.72f) : NUIColor(1, 1, 1, 0.40f);
            renderer.drawTextCentered(btn.label, btn.bounds, 9.0f, text);
        }
    }
}

std::string AestraCompEditor::valueText(uint32_t paramId) const {
    if (!m_instance) return {};
    return m_instance->getParameterDisplay(paramId);
}

void AestraCompEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    auto b = getBounds();
    NUIColor accent = purple();

    drawTransferCurve(renderer, accent);
    drawMeters(renderer, accent);

    // Draw knobs first (their backgrounds go behind labels)
    for (int i = 0; i < 5 && i < static_cast<int>(m_controls.size()); ++i)
        drawControl(renderer, m_controls[i], accent);
    for (int i = 5; i < 10 && i < static_cast<int>(m_controls.size()); ++i)
        drawControl(renderer, m_controls[i], accent);

    // Section labels on top of knob backgrounds
    const float contentX = b.x + kPad;
    const float contentW = b.width - kPad * 2.0f;
    const float primDivY = m_metersRect.bottom() + 10.0f;
    const float utilDivY = m_controls[5].bounds.y - 22.0f;
    drawSectionLabel(renderer, "PRIMARY", primDivY, contentX, contentX + contentW);
    drawSectionLabel(renderer, "UTILITY", utilDivY, contentX, contentX + contentW);

    drawUtilityButtons(renderer, accent);
}

void AestraCompEditor::setBypassed(bool bypassed) {
    if (!m_instance) return;
    m_instance->setParameter(Aestra::Audio::Plugins::AestraComp::kBypass, bypassed ? 1.0f : 0.0f);
    repaint();
}

bool AestraCompEditor::isBypassed() const {
    return m_instance && m_instance->getParameter(Aestra::Audio::Plugins::AestraComp::kBypass) > 0.5f;
}

bool AestraCompEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;

    if (AestraPanelWindow::onMouseEvent(event)) return true;

    auto b = getBounds();
    const bool contains = b.contains(event.position);
    if (!contains && !isDraggingWindow()) return false;

    // Click handlers
    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (m_bypassRect.contains(event.position)) { setBypassed(!isBypassed()); return true; }
        if (m_autoRect.contains(event.position)) { m_autoEnabled = !m_autoEnabled; setDirty(true); return true; }
        if (m_linkRect.contains(event.position)) { m_linkEnabled = !m_linkEnabled; setDirty(true); return true; }
        if (m_mixLockRect.contains(event.position)) { m_mixLocked = !m_mixLocked; setDirty(true); return true; }
        if (m_resetRect.contains(event.position)) {
            for (auto& c : m_controls) {
                if (c.slider) {
                    const float def = m_instance->getParameters()[c.paramId].defaultValue;
                    m_instance->setParameter(c.paramId, def);
                    c.slider->setValue(def);
                }
            }
            setDirty(true);
            return true;
        }
        for (int i = 0; i < 3; ++i) {
            if (m_modePills[i].contains(event.position)) {
                if (m_instance) {
                    m_instance->setParameter(Aestra::Audio::Plugins::AestraComp::kCompMode,
                                              static_cast<float>(i) / 2.0f);
                    setDirty(true);
                }
                return true;
            }
        }
        if (m_transferCurveRect.contains(event.position)) return true;
    }

    // Double-click reset
    if (event.pressed && event.button == NUIMouseButton::Left && event.doubleClick) {
        for (auto& c : m_controls) {
            if (c.slider && c.slider->getBounds().contains({event.position.x, event.position.y})) {
                const float def = m_instance->getParameters()[c.paramId].defaultValue;
                m_instance->setParameter(c.paramId, def);
                c.slider->setValue(def);
                setDirty(true);
                return true;
            }
        }
    }

    // Hover tracking
    if (!event.pressed && !event.released) {
        const bool bh = contains && m_bypassRect.contains(event.position);
        const bool ah = contains && m_autoRect.contains(event.position);
        const bool lh = contains && m_linkRect.contains(event.position);
        const bool mlh = contains && m_mixLockRect.contains(event.position);
        const bool rh = contains && m_resetRect.contains(event.position);
        if (bh != m_bypassHovered || ah != m_autoHovered || lh != m_linkHovered || mlh != m_mixLockHovered || rh != m_resetHovered) {
            m_bypassHovered = bh;
            m_autoHovered = ah;
            m_linkHovered = lh;
            m_mixLockHovered = mlh;
            m_resetHovered = rh;
            repaint();
        }
    }

    return NUIComponent::onMouseEvent(event);
}

} // namespace AestraUI
