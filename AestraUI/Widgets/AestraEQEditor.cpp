// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraEQEditor.h"
#include "Plugin/AestraEQ.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace AestraUI {

namespace {
constexpr float kCloseSize = 16.0f;
constexpr float kRadius = 16.0f;
constexpr float kPi = 3.14159265358979323846f;

const NUIColor kBlueprintBg(0.035f, 0.035f, 0.052f, 0.98f);
const NUIColor kBlueprintPanel(0.070f, 0.064f, 0.102f, 0.86f);
const NUIColor kBlueprintLine(0.58f, 0.42f, 0.94f, 0.42f);
const NUIColor kBlueprintDim(0.72f, 0.66f, 0.90f, 0.18f);
const NUIColor kBlueprintText(0.94f, 0.93f, 1.0f, 0.94f);
const NUIColor kBlueprintMuted(0.72f, 0.69f, 0.84f, 0.66f);
const NUIColor kGlassDeep(0.018f, 0.017f, 0.028f, 0.94f);
const NUIColor kGlassSurface(0.085f, 0.078f, 0.120f, 0.88f);
const NUIColor kAccentPurple(0.55f, 0.38f, 0.92f, 1.0f);
const NUIColor kAccentCyan(0.0f, 0.88f, 0.80f, 1.0f);

NUIColor bandColorForIndex(size_t index) {
    static const NUIColor colors[] = {
        NUIColor(0.55f, 0.72f, 1.0f, 1.0f),
        NUIColor(0.48f, 0.82f, 1.0f, 1.0f),
        NUIColor(0.62f, 0.74f, 1.0f, 1.0f),
        NUIColor(0.74f, 0.67f, 1.0f, 1.0f),
        NUIColor(0.96f, 0.72f, 0.52f, 1.0f),
        NUIColor(0.90f, 0.60f, 0.40f, 1.0f),
        NUIColor(0.88f, 0.56f, 0.76f, 1.0f),
        NUIColor(0.78f, 0.76f, 1.0f, 1.0f),
    };
    return colors[index % (sizeof(colors) / sizeof(colors[0]))];
}

const char* glyphTypeLabel(uint32_t type) {
    static const char* labels[] = {"Bell", "Cut", "Cut", "Shelf", "Shelf", "Notch", "Band", "Tilt"};
    return type < 8 ? labels[type] : "Bell";
}

float quantizeCutSlopeNorm(float norm) {
    static constexpr float kSlopeSteps[] = {0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f};
    const float clamped = std::clamp(norm, 0.0f, 1.0f);
    int bestIndex = 0;
    float bestDistance = std::abs(clamped - kSlopeSteps[0]);
    for (int i = 1; i < 4; ++i) {
        const float distance = std::abs(clamped - kSlopeSteps[i]);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
    }
    return kSlopeSteps[bestIndex];
}

uint32_t cutSlopeDbPerOct(float norm) {
    static constexpr uint32_t kSlopeDb[] = {12u, 24u, 36u, 48u};
    const float quantized = quantizeCutSlopeNorm(norm);
    const int index = static_cast<int>(std::round(quantized * 3.0f));
    return kSlopeDb[std::clamp(index, 0, 3)];
}

uint32_t cutSlopeStageCount(float norm) {
    return cutSlopeDbPerOct(norm) / 12u;
}

std::vector<NUIPoint> makeSmoothCurvePoints(const std::vector<NUIPoint>& points, int subdivisions) {
    if (points.size() < 4 || subdivisions <= 1) {
        return points;
    }

    std::vector<NUIPoint> result;
    result.reserve(points.size() * static_cast<size_t>(subdivisions));

    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const NUIPoint& p0 = points[(i == 0) ? i : (i - 1)];
        const NUIPoint& p1 = points[i];
        const NUIPoint& p2 = points[i + 1];
        const NUIPoint& p3 = points[(i + 2 < points.size()) ? (i + 2) : (i + 1)];

        for (int s = 0; s < subdivisions; ++s) {
            const float t = static_cast<float>(s) / static_cast<float>(subdivisions);
            const float t2 = t * t;
            const float t3 = t2 * t;

            const float x =
                0.5f * ((2.0f * p1.x) +
                        (-p0.x + p2.x) * t +
                        (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 +
                        (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3);
            const float y =
                0.5f * ((2.0f * p1.y) +
                        (-p0.y + p2.y) * t +
                        (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 +
                        (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3);

            result.push_back({x, y});
        }
    }

    result.push_back(points.back());
    return result;
}

NUIRect graphNodeSafeBounds(const NUIRect& graphBounds) {
    constexpr float kNodePadX = 10.0f;
    constexpr float kNodePadY = 12.0f;
    return {
        graphBounds.x + kNodePadX,
        graphBounds.y + kNodePadY,
        std::max(1.0f, graphBounds.width - kNodePadX * 2.0f),
        std::max(1.0f, graphBounds.height - kNodePadY * 2.0f),
    };
}

void drawBlueprintLabel(NUIRenderer& renderer, const std::string& text, const NUIRect& rect, float size = 9.0f) {
    renderer.drawText(text, {rect.x + 11.0f, rect.y + 8.0f}, size, kBlueprintMuted);
}

void drawBlueprintKnob(NUIRenderer& renderer,
                       const NUIPoint& center,
                       float radius,
                       float norm,
                       const std::string& label,
                       const std::string& value,
                       bool active,
                       const NUIColor& accent) {
    const float start = 0.73f * kPi;
    const float end = 2.27f * kPi;
    const float angle = start + std::clamp(norm, 0.0f, 1.0f) * (end - start);
    const NUIColor line = active ? accent.withAlpha(0.98f) : kBlueprintDim;

    renderer.fillCircle(center, radius + 8.0f, active ? accent.withAlpha(0.12f) : NUIColor(0.0f, 0.0f, 0.0f, 0.20f));
    renderer.fillCircle(center, radius + 4.0f, NUIColor(0.0f, 0.0f, 0.0f, 0.28f));
    renderer.strokeCircle(center, radius + 3.0f, 1.2f, active ? accent.withAlpha(0.30f) : kBlueprintDim);
    renderer.fillCircle(center, radius, NUIColor(0.018f, 0.017f, 0.030f, 0.96f));
    renderer.fillCircle({center.x - radius * 0.28f, center.y - radius * 0.30f}, radius * 0.42f,
                        NUIColor(0.17f, 0.15f, 0.23f, 0.56f));
    renderer.strokeCircle(center, radius, 1.4f, active ? accent.withAlpha(0.76f) : kBlueprintDim);
    const NUIPoint tip{center.x + std::cos(angle) * (radius - 3.0f), center.y + std::sin(angle) * (radius - 3.0f)};
    renderer.drawLine(center, tip, 2.0f, line);
    renderer.fillCircle(tip, 2.0f, line);
    renderer.drawText(label, {center.x - radius - 4.0f, center.y + radius + 8.0f}, 8.2f, kBlueprintMuted);
    renderer.drawText(value, {center.x - radius - 4.0f, center.y + radius + 19.0f}, 8.4f, active ? kBlueprintText : kBlueprintMuted.withAlpha(0.46f));
}
}

AestraEQEditor::AestraEQEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraEQEditor");
    setPanelTitle("Aestra EQ");
    setSize(kWindowWidth, kWindowHeight);
    setEnforceParentBounds(true);
    buildControls();
    m_spectrumWorker = std::thread([this]() { analyzerWorkerMain(); });
}

AestraEQEditor::~AestraEQEditor() {
    {
        std::lock_guard<std::mutex> lock(m_spectrumMutex);
        m_spectrumStop = true;
    }
    m_spectrumCv.notify_all();
    if (m_spectrumWorker.joinable()) {
        m_spectrumWorker.join();
    }
}

void AestraEQEditor::buildControls() {
    m_bands.clear();
    if (!m_instance) return;

    using EQ = Aestra::Audio::Plugins::AestraEQ;
    static constexpr uint32_t enableIds[] = {EQ::kParamHPFEnable, EQ::kParamLShEnable, EQ::kParamBell1Enable, EQ::kParamBell2Enable, EQ::kParamHShEnable, EQ::kParamLPFEnable};
    static constexpr uint32_t freqIds[]   = {EQ::kParamHPFFreq,   EQ::kParamLShFreq,   EQ::kParamBell1Freq,   EQ::kParamBell2Freq,   EQ::kParamHShFreq,   EQ::kParamLPFFreq};
    static constexpr uint32_t gainIds[]   = {0,                   EQ::kParamLShGain,   EQ::kParamBell1Gain,   EQ::kParamBell2Gain,   EQ::kParamHShGain,   0};
    static constexpr uint32_t qIds[]      = {EQ::kParamHPFSlope,  EQ::kParamLShQ,      EQ::kParamBell1Q,      EQ::kParamBell2Q,      EQ::kParamHShQ,      EQ::kParamLPFSlope};
    static constexpr uint32_t types[]     = {1, 3, 0, 0, 4, 2}; // LowCut, LowShelf, Bell, Bell, HighShelf, HighCut
    static const std::string names[]      = {"HP", "LS", "B1", "B2", "HS", "LP"};

    for (size_t i = 0; i < kNumBands; ++i) {
        BandControl bc;
        bc.enableId = enableIds[i];
        bc.freqId = freqIds[i];
        bc.gainId = gainIds[i];
        bc.qId = qIds[i];
        bc.name = names[i];
        bc.type = types[i];
        bc.usesGain = (gainIds[i] != 0);
        bc.usesSlope = (i == 0 || i == 5);
        bc.enabled = m_instance->getParameter(bc.enableId) > 0.5f;
        bc.freq = m_instance->getParameter(bc.freqId);
        bc.gain = bc.usesGain ? m_instance->getParameter(bc.gainId) : 0.5f;
        bc.q = m_instance->getParameter(bc.qId);
        m_bands.push_back(std::move(bc));
    }
    layoutControls();
}

void AestraEQEditor::layoutControls() {
    auto bounds = getBounds();
    constexpr float bandGap = 8.0f;
    float bandW = (bounds.width - kPadding * 2.0f - bandGap * 5.0f) / 6.0f;
    float y = bounds.y + AestraPanelWindow::TITLE_BAR_H + kCurveHeight + 76.0f;
    float h = 124.0f;

    for (size_t i = 0; i < m_bands.size(); ++i) {
        auto& b = m_bands[i];
        float x = bounds.x + kPadding + i * (bandW + bandGap);
        b.bounds = NUIRect(x, y, bandW, h);
        const float knobSize = 32.0f;
        const float hitPad = 10.0f;
        const float knobY = y + 60.0f;
        const float freqX = x + bandW * 0.24f;
        const float gainX = x + bandW * 0.52f;
        const float qX = x + bandW * 0.80f;

        b.freqSlider = NUIRect(freqX - knobSize * 0.5f - hitPad, y + 30.0f, knobSize + hitPad * 2.0f, h - 38.0f);
        b.gainSlider = NUIRect(gainX - knobSize * 0.5f - hitPad, y + 30.0f, knobSize + hitPad * 2.0f, h - 38.0f);
        b.qSlider = NUIRect(qX - knobSize * 0.5f - hitPad, y + 30.0f, knobSize + hitPad * 2.0f, h - 38.0f);
        b.freqKnob = NUIRect(freqX - knobSize * 0.5f, knobY - knobSize * 0.5f, knobSize, knobSize);
        b.gainKnob = NUIRect(gainX - knobSize * 0.5f, knobY - knobSize * 0.5f, knobSize, knobSize);
        b.qKnob = NUIRect(qX - knobSize * 0.5f, knobY - knobSize * 0.5f, knobSize, knobSize);
    }
}

void AestraEQEditor::drawBlueprintGrid(NUIRenderer& renderer, const NUIRect& bounds) {
    renderer.fillRoundedRect(bounds, kRadius, kBlueprintBg);
    renderer.fillRoundedRect({bounds.x + 1.0f, bounds.y + 1.0f, bounds.width - 2.0f, bounds.height * 0.45f},
                             kRadius, NUIColor(0.14f, 0.10f, 0.22f, 0.12f));
    renderer.fillRoundedRect({bounds.x + bounds.width * 0.55f, bounds.y + bounds.height * 0.20f,
                              bounds.width * 0.32f, bounds.height * 0.38f},
                             kRadius, kAccentPurple.withAlpha(0.035f));
    renderer.strokeRoundedRect(bounds, kRadius, 1.0f, kBlueprintLine.withAlpha(0.26f));
}

void AestraEQEditor::drawUtilityStrip(NUIRenderer& renderer, const NUIRect& bounds) {
    const float gap = 8.0f;
    const float blockW = (bounds.width - gap * 6.0f) / 7.0f;
    static const char* titles[] = {"BYPASS", "STEREO MODE", "ANALYZER", "SLOPE", "RESOLUTION", "DYNAMIC EQ", "PANEL"};
    static const char* values[] = {"ON", "L/R  M/S  L  R", "Pre+Post", "12  24  36  48", "High", "Per Band", "Compact"};

    for (int i = 0; i < 7; ++i) {
        const NUIRect block{bounds.x + static_cast<float>(i) * (blockW + gap), bounds.y, blockW, bounds.height};
        const bool primary = i == 0 || i == 2 || i == 5;
        renderer.fillRoundedRect(block, 8.0f, primary ? kAccentPurple.withAlpha(0.13f) : NUIColor(0.050f, 0.047f, 0.070f, 0.88f));
        renderer.strokeRoundedRect(block, 10.0f, 1.0f,
                                   primary ? kAccentPurple.withAlpha(0.34f) : kBlueprintDim.withAlpha(0.82f));
        renderer.drawText(titles[i], {block.x + 11.0f, block.y + 9.0f}, 8.5f, kBlueprintMuted);
        renderer.drawText(values[i], {block.x + 11.0f, block.y + 31.0f}, 9.5f, kBlueprintText);
        renderer.drawLine({block.x + 11.0f, block.bottom() - 13.0f}, {block.right() - 11.0f, block.bottom() - 13.0f},
                          1.0f, (primary ? kAccentPurple : kAccentCyan).withAlpha(0.22f));
    }
}

void AestraEQEditor::drawInputOutputPanel(NUIRenderer& renderer, const NUIRect& bounds, bool output) {
    renderer.fillRoundedRect(bounds, 10.0f, NUIColor(0.040f, 0.038f, 0.056f, 0.92f));
    renderer.strokeRoundedRect(bounds, 11.0f, 1.0f, kBlueprintDim.withAlpha(0.72f));

    const std::string title = output ? "OUTPUT" : "INPUT";
    renderer.drawText(title, {bounds.x + 34.0f, bounds.y + 17.0f}, 10.0f, kBlueprintText);
    renderer.drawText("0.0 dB", {bounds.x + 33.0f, bounds.y + 39.0f}, 11.0f, kBlueprintText.withAlpha(0.86f));

    const NUIPoint knobCenter{bounds.x + bounds.width * 0.50f, bounds.y + 86.0f};
    drawBlueprintKnob(renderer, knobCenter, 21.0f, 0.50f, "", "", true, output ? kAccentCyan : kAccentPurple);

    const float meterTop = bounds.y + 136.0f;
    const float meterH = bounds.height - 164.0f;
    const float meterW = 10.0f;
    const float leftX = bounds.x + bounds.width * 0.39f;
    const float rightX = bounds.x + bounds.width * 0.55f;
    for (int ch = 0; ch < 2; ++ch) {
        const float x = ch == 0 ? leftX : rightX;
        renderer.fillRoundedRect({x, meterTop, meterW, meterH}, 2.0f, NUIColor(0.010f, 0.010f, 0.016f, 0.82f));
        renderer.fillRoundedRect({x, meterTop + meterH * 0.26f, meterW, meterH * 0.74f}, 2.0f, kAccentCyan.withAlpha(0.80f));
        renderer.fillRoundedRect({x, meterTop + meterH * 0.62f, meterW, meterH * 0.38f}, 2.0f, kAccentPurple.withAlpha(0.42f));
    }

    renderer.drawText("L", {leftX + 1.0f, bounds.bottom() - 22.0f}, 8.0f, kBlueprintMuted);
    renderer.drawText("R", {rightX + 1.0f, bounds.bottom() - 22.0f}, 8.0f, kBlueprintMuted);
}

void AestraEQEditor::drawFilterGuardPanel(NUIRenderer& renderer, const NUIRect& bounds, bool highPass) {
    renderer.fillRoundedRect(bounds, 10.0f, NUIColor(0.040f, 0.038f, 0.056f, 0.92f));
    renderer.strokeRoundedRect(bounds, 11.0f, 1.0f, kBlueprintDim.withAlpha(0.72f));

    const NUIColor accent = highPass ? bandColorForIndex(0) : bandColorForIndex(7);
    renderer.fillCircle({bounds.x + 23.0f, bounds.y + 25.0f}, 3.0f, accent);
    renderer.drawText(highPass ? "HPF" : "LPF", {bounds.x + 44.0f, bounds.y + 20.0f}, 10.5f, kBlueprintText);

    const NUIRect slope{bounds.x + 24.0f, bounds.y + 58.0f, bounds.width - 48.0f, 24.0f};
    renderer.fillRoundedRect(slope, 6.0f, kGlassDeep);
    renderer.strokeRoundedRect(slope, 6.0f, 1.0f, kBlueprintDim.withAlpha(0.64f));
    renderer.drawText("24 dB/Oct", {slope.x + 17.0f, slope.y + 8.0f}, 8.8f, kBlueprintText.withAlpha(0.86f));
    renderer.drawText(highPass ? "20.0 Hz" : "20.0 kHz", {bounds.x + 42.0f, bounds.bottom() - 38.0f}, 12.0f, kBlueprintText);
    drawBlueprintKnob(renderer, {bounds.x + bounds.width * 0.50f, bounds.y + 141.0f}, 21.0f,
                      highPass ? 0.00f : 1.0f, "FREQ", "", true, accent);
}

void AestraEQEditor::drawResponseCurve(NUIRenderer& renderer, const NUIRect& bounds) {
    auto& theme = NUIThemeManager::getInstance();
    drawBlueprintGrid(renderer, bounds);
    drawSpectrumBackdrop(renderer, bounds);
    m_lastResponseBounds = responseGraphBounds(bounds);

    // Compute approximate response curve
    auto graphFreqHz = [](float norm) {
        float logMin = std::log10(20.0f), logMax = std::log10(20000.0f);
        return std::pow(10.0f, logMin + norm * (logMax - logMin));
    };
    auto bandFreqHz = [](size_t bandIdx, float norm) -> float {
        switch (bandIdx) {
        case 0: return 20.0f * std::pow(25.0f, std::clamp(norm, 0.0f, 1.0f));
        case 1: return 40.0f * std::pow(25.0f, std::clamp(norm, 0.0f, 1.0f));
        case 2: return 80.0f * std::pow(100.0f, std::clamp(norm, 0.0f, 1.0f));
        case 3: return 200.0f * std::pow(80.0f, std::clamp(norm, 0.0f, 1.0f));
        case 4: return 2000.0f * std::pow(10.0f, std::clamp(norm, 0.0f, 1.0f));
        case 5: return 1000.0f * std::pow(20.0f, std::clamp(norm, 0.0f, 1.0f));
        default: return 1000.0f;
        }
    };
    auto freqToGraphNorm = [](float hz) {
        float logMin = std::log10(20.0f), logMax = std::log10(20000.0f);
        return (std::log10(hz) - logMin) / (logMax - logMin);
    };
    auto gainToDb = [](float norm) { return -18.0f + norm * 36.0f; };
    auto qToLinear = [](float norm) { return 0.1f + norm * 9.9f; };
    auto slopeStages = [](float norm) -> uint32_t {
        return cutSlopeStageCount(norm);
    };
    auto biquadMagnitudeDb = [](const Aestra::Audio::Plugins::FilterCoeffs& coeffs, double omega) {
        const double cos1 = std::cos(omega);
        const double sin1 = std::sin(omega);
        const double cos2 = std::cos(2.0 * omega);
        const double sin2 = std::sin(2.0 * omega);

        const double numRe = static_cast<double>(coeffs.b0) + static_cast<double>(coeffs.b1) * cos1 + static_cast<double>(coeffs.b2) * cos2;
        const double numIm = -static_cast<double>(coeffs.b1) * sin1 - static_cast<double>(coeffs.b2) * sin2;
        const double denRe = static_cast<double>(coeffs.a0) + static_cast<double>(coeffs.a1) * cos1 + static_cast<double>(coeffs.a2) * cos2;
        const double denIm = -static_cast<double>(coeffs.a1) * sin1 - static_cast<double>(coeffs.a2) * sin2;

        const double numMag = std::hypot(numRe, numIm);
        const double denMag = std::hypot(denRe, denIm);
        const double safeDen = std::max(denMag, 1.0e-12);
        const double mag = std::max(numMag / safeDen, 1.0e-12);
        return static_cast<float>(20.0 * std::log10(mag));
    };

    auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
    const float sampleRate = static_cast<float>(std::max(1.0, eq ? eq->getAnalyzerSampleRate() : 48000.0));

    static constexpr Aestra::Audio::Plugins::FilterType kBandTypes[] = {
        Aestra::Audio::Plugins::FilterType::LowCut,
        Aestra::Audio::Plugins::FilterType::LowShelf,
        Aestra::Audio::Plugins::FilterType::Bell,
        Aestra::Audio::Plugins::FilterType::Bell,
        Aestra::Audio::Plugins::FilterType::HighShelf,
        Aestra::Audio::Plugins::FilterType::HighCut
    };

    const int numPoints = 1400;
    std::vector<float> responseDb(numPoints, 0.0f);
    for (int p = 0; p < numPoints; ++p) {
        float freqNorm = static_cast<float>(p) / (numPoints - 1);
        float freq = graphFreqHz(freqNorm);
        double omega = 2.0 * static_cast<double>(kPi) * static_cast<double>(freq) / static_cast<double>(sampleRate);
        for (size_t bi = 0; bi < m_bands.size(); ++bi) {
            const auto& band = m_bands[bi];
            if (!band.enabled) continue;
            float f0 = bandFreqHz(bi, band.freq);
            f0 = std::clamp(f0, 20.0f, sampleRate * 0.49f);
            float gainDb = band.usesGain ? gainToDb(band.gain) : 0.0f;
            float Q = band.usesSlope ? 0.70710678f : qToLinear(band.q);
            Q = std::clamp(Q, 0.1f, 10.0f);
            const auto type = kBandTypes[bi];
            const auto coeffs = Aestra::Audio::Plugins::designBiquad(type, f0, gainDb, Q, sampleRate);
            uint32_t stages = band.usesSlope ? slopeStages(band.q) : 1;
            float bandResponse = biquadMagnitudeDb(coeffs, omega) * static_cast<float>(stages);
            if (type == Aestra::Audio::Plugins::FilterType::LowCut || type == Aestra::Audio::Plugins::FilterType::HighCut) {
                bandResponse = std::min(bandResponse, 0.0f);
            }
            responseDb[p] += bandResponse;
        }
    }

    // Light neighbor smoothing keeps the rendered line visually fluid without
    // materially changing the underlying filter shape.
    std::vector<float> smoothedResponseDb(numPoints, 0.0f);
    for (int i = 0; i < numPoints; ++i) {
        const int i0 = std::max(0, i - 2);
        const int i1 = std::max(0, i - 1);
        const int i3 = std::min(numPoints - 1, i + 1);
        const int i4 = std::min(numPoints - 1, i + 2);
        smoothedResponseDb[i] =
            responseDb[i0] * 0.10f +
            responseDb[i1] * 0.20f +
            responseDb[i]  * 0.40f +
            responseDb[i3] * 0.20f +
            responseDb[i4] * 0.10f;
    }

    // Draw grid lines
    float dbRange = 18.0f;
    for (int db = -18; db <= 18; db += 6) {
        float y = bounds.bottom() - 20.0f - (static_cast<float>(db) + dbRange) / (dbRange * 2.0f) * (bounds.height - 40.0f);
        float alpha = (db == 0) ? 0.25f : 0.10f;
        renderer.drawLine({bounds.x + 50.0f, y}, {bounds.right() - 18.0f, y},
                          db == 0 ? 1.5f : 1.0f, kBlueprintLine.withAlpha(alpha + 0.10f));
        std::string label = (db >= 0 ? "+" : "") + std::to_string(db) + "dB";
        renderer.drawText(label, {bounds.x + 10.0f, y - 6.0f}, 9.0f, kBlueprintMuted);
    }

    // Frequency labels
    float freqs[] = {30, 100, 300, 1000, 3000, 10000};
    const char* freqLabels[] = {"30", "100", "300", "1k", "3k", "10k"};
    for (int i = 0; i < 6; ++i) {
        float norm = (std::log10(freqs[i]) - std::log10(20.0f)) / (std::log10(20000.0f) - std::log10(20.0f));
        float x = bounds.x + 40.0f + norm * (bounds.width - 50.0f);
        renderer.drawLine({x, bounds.y + 12.0f}, {x, bounds.bottom() - 22.0f},
                          1.0f, kBlueprintDim.withAlpha(0.25f));
        renderer.drawText(freqLabels[i], {x - 8.0f, bounds.y + 10.0f}, 9.0f, kBlueprintMuted);
    }

    // Draw curve
    float xStart = m_lastResponseBounds.x;
    float xEnd = m_lastResponseBounds.right();
    float curveTop = m_lastResponseBounds.y;
    float curveBottom = m_lastResponseBounds.bottom();

    std::vector<NUIPoint> curvePoints;
    curvePoints.reserve(numPoints);
    for (int p = 0; p < numPoints; ++p) {
        const float x = xStart + (static_cast<float>(p) / (numPoints - 1)) * (xEnd - xStart);
        const float y = curveBottom - std::clamp((smoothedResponseDb[p] + dbRange) / (dbRange * 2.0f), 0.0f, 1.0f) * (curveBottom - curveTop);
        curvePoints.push_back({x, y});
    }

    const std::vector<NUIPoint> smoothCurvePoints = makeSmoothCurvePoints(curvePoints, 6);
    const NUIColor curveColor = NUIColor(0.76f, 0.62f, 1.0f, 0.94f);
    std::vector<NUIPoint> offsetUp = smoothCurvePoints;
    std::vector<NUIPoint> offsetDown = smoothCurvePoints;
    for (size_t i = 0; i < smoothCurvePoints.size(); ++i) {
        offsetUp[i].y -= 0.75f;
        offsetDown[i].y += 0.75f;
    }

    renderer.drawPolyline(offsetUp.data(), static_cast<int>(offsetUp.size()), 6.0f, curveColor.withAlpha(0.06f));
    renderer.drawPolyline(offsetDown.data(), static_cast<int>(offsetDown.size()), 6.0f, kAccentCyan.withAlpha(0.035f));
    renderer.drawPolyline(smoothCurvePoints.data(), static_cast<int>(smoothCurvePoints.size()), 7.0f, curveColor.withAlpha(0.10f));
    renderer.drawPolyline(smoothCurvePoints.data(), static_cast<int>(smoothCurvePoints.size()), 3.5f, curveColor.withAlpha(0.32f));
    renderer.drawPolyline(smoothCurvePoints.data(), static_cast<int>(smoothCurvePoints.size()), 1.6f, kBlueprintText.withAlpha(0.90f));

    for (size_t i = 0; i < m_bands.size(); ++i) {
        const auto& band = m_bands[i];
        if (!band.enabled) {
            continue;
        }

        const bool selected = static_cast<int>(i) == m_selectedBand;
        const bool hovered = static_cast<int>(i) == m_hoveredBand;
        const bool dragging = static_cast<int>(i) == m_draggingGraphBand;
        const NUIPoint node = graphNodePosition(band, m_lastResponseBounds);
        const float radius = dragging ? 10.0f : (selected ? 8.75f : (hovered ? 7.3f : 6.5f));
        NUIColor nodeColor = bandColorForIndex(i).withAlpha(selected || hovered || dragging ? 0.99f : 0.80f);
        const NUIColor guideColor = nodeColor.withAlpha(selected || hovered || dragging ? 0.30f : 0.08f);

        renderer.drawLine({node.x, m_lastResponseBounds.bottom()}, {node.x, node.y},
                          selected || hovered || dragging ? 1.6f : 1.0f,
                          guideColor);

        if (selected || dragging) {
            renderer.fillCircle(node, radius + 7.0f, nodeColor.withAlpha(0.08f));
        }
        renderer.fillCircle(node, radius + 4.0f, nodeColor.withAlpha(selected || dragging ? 0.16f : 0.10f));
        renderer.fillCircle(node, radius, kGlassDeep);
        renderer.strokeCircle(node, radius, selected || dragging ? 1.8f : 1.4f, nodeColor);
        renderer.fillCircle(node, selected || dragging ? 2.5f : 2.0f, nodeColor);

        const float labelY = m_lastResponseBounds.y + 8.0f + static_cast<float>(i % 2) * 16.0f;
        const float pillWidth = 58.0f;
        const float pillX = std::clamp(node.x - pillWidth * 0.5f,
                                       m_lastResponseBounds.x + 2.0f,
                                       m_lastResponseBounds.right() - pillWidth - 2.0f);

        renderer.drawLine({node.x, labelY + 16.0f}, {node.x, node.y - radius - 4.0f},
                          1.0f, nodeColor.withAlpha(selected || hovered || dragging ? 0.18f : 0.10f));
        const NUIRect pillRect{pillX, labelY, pillWidth, 14.0f};
        renderer.fillRoundedRect(pillRect, 5.0f,
                                 selected || dragging ? kBlueprintPanel : NUIColor(0.040f, 0.038f, 0.060f, 0.88f));
        if (selected || dragging) {
            renderer.fillRoundedRect({pillRect.x + 1.0f, pillRect.y + 1.0f, pillRect.width - 2.0f, pillRect.height * 0.52f},
                                     5.0f, nodeColor.withAlpha(0.09f));
        }
        renderer.strokeRoundedRect(pillRect, 5.0f, 1.0f,
                                   nodeColor.withAlpha(selected || dragging ? 0.52f : 0.26f));
        renderer.drawText(band.name, {pillX + 5.0f, labelY + 3.0f}, 8.25f,
                          theme.getColor("textPrimary").withAlpha(selected || dragging ? 0.98f : 0.82f));
        renderer.drawText(bandFreqLabel(i, band.freq), {pillX + 20.0f, labelY + 3.0f}, 8.25f,
                          nodeColor.withAlpha(selected || dragging ? 0.96f : 0.78f));
    }

    if (m_selectedBand >= 0 && m_selectedBand < static_cast<int>(m_bands.size())) {
        const auto& band = m_bands[m_selectedBand];
        const NUIColor selectedColor = bandColorForIndex(static_cast<size_t>(m_selectedBand));
        const auto hudX = bounds.right() - 214.0f;
        const auto hudY = bounds.y + 10.0f;
        const NUIRect hudRect{hudX, hudY, 174.0f, 26.0f};
        renderer.fillRoundedRect(hudRect, 12.0f, NUIColor(0.08f, 0.09f, 0.11f, 0.94f));
        renderer.fillRoundedRect({hudRect.x + 1.0f, hudRect.y + 1.0f, hudRect.width - 2.0f, hudRect.height * 0.5f},
                                 11.0f, selectedColor.withAlpha(0.08f));
        renderer.strokeRoundedRect(hudRect, 12.0f, 1.0f, selectedColor.withAlpha(0.34f));
        renderer.fillRoundedRect({hudRect.x + 8.0f, hudRect.y + 6.0f, 34.0f, 14.0f}, 7.0f,
                                 selectedColor.withAlpha(0.16f));
        renderer.strokeRoundedRect({hudRect.x + 8.0f, hudRect.y + 6.0f, 34.0f, 14.0f}, 7.0f, 1.0f,
                                   selectedColor.withAlpha(0.34f));
        renderer.drawText(band.name, {hudRect.x + 15.0f, hudRect.y + 9.0f}, 8.75f,
                          theme.getColor("textPrimary").withAlpha(0.98f));

        const std::string meta = std::string(glyphTypeLabel(band.type)) + "  " + bandFreqLabel(static_cast<size_t>(m_selectedBand), band.freq);
        renderer.drawText(meta, {hudRect.x + 50.0f, hudRect.y + 8.0f}, 9.0f,
                          selectedColor.withAlpha(0.90f));

        const std::string detail = usesGainAxis(band)
            ? ("Gain " + gainLabel(band.gain) + " dB   Q " + qLabel(band.q, band.type))
            : ("Slope/Q " + qLabel(band.q, band.type));
        renderer.drawText(detail, {hudRect.x + 50.0f, hudRect.y + 15.5f}, 8.0f,
                          theme.getColor("textSecondary").withAlpha(0.90f));
    }
}

void AestraEQEditor::updateSpectrumSnapshot() {
    std::array<float, 160> nextMagnitudes{};
    bool hasReadyMagnitudes = false;
    {
        std::lock_guard<std::mutex> lock(m_spectrumMutex);
        if (m_spectrumResultReady) {
            nextMagnitudes = m_workerResultMagnitudes;
            m_lastAnalyzerSerial = m_workerResultSerial;
            m_pendingAnalyzerSerial = 0;
            m_spectrumResultReady = false;
            hasReadyMagnitudes = true;
        }
    }

    if (hasReadyMagnitudes) {
        std::array<float, 160> smoothedMagnitudes{};
        for (size_t i = 0; i < nextMagnitudes.size(); ++i) {
            const size_t i0 = (i == 0) ? i : (i - 1);
            const size_t i2 = std::min(i + 1, nextMagnitudes.size() - 1);
            smoothedMagnitudes[i] =
                nextMagnitudes[i0] * 0.22f +
                nextMagnitudes[i]  * 0.56f +
                nextMagnitudes[i2] * 0.22f;
        }

        for (size_t i = 0; i < m_spectrumMagnitudes.size(); ++i) {
            const float current = m_spectrumMagnitudes[i];
            const float target = smoothedMagnitudes[i];
            const float attack = 0.62f;
            const float release = 0.92f;
            if (target >= current) {
                m_spectrumMagnitudes[i] = current + (target - current) * attack;
            } else {
                m_spectrumMagnitudes[i] = current * release + target * (1.0f - release);
            }
        }
    }

    auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
    if (!eq) {
        return;
    }

    uint64_t serial = 0;
    if (!eq->getAnalyzerWindow(m_analyzerWindow, &serial) ||
        serial == m_lastAnalyzerSerial ||
        m_pendingAnalyzerSerial != 0) {
        return;
    }

    const double sampleRate = std::max(1.0, eq->getAnalyzerSampleRate());
    {
        std::lock_guard<std::mutex> lock(m_spectrumMutex);
        m_workerAnalyzerWindow = m_analyzerWindow;
        m_workerRequestedSerial = serial;
        m_spectrumWorkPending = true;
        m_pendingAnalyzerSerial = serial;
    }
    m_spectrumCv.notify_one();
}

void AestraEQEditor::analyzerWorkerMain() {
    while (true) {
        std::array<float, Aestra::Audio::Plugins::AestraEQ::kAnalyzerWindowSize> analyzerWindow{};
        uint64_t serial = 0;
        {
            std::unique_lock<std::mutex> lock(m_spectrumMutex);
            m_spectrumCv.wait(lock, [this]() { return m_spectrumStop || m_spectrumWorkPending; });
            if (m_spectrumStop) {
                return;
            }
            analyzerWindow = m_workerAnalyzerWindow;
            serial = m_workerRequestedSerial;
            m_spectrumWorkPending = false;
        }

        auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
        const double sampleRate = eq ? std::max(1.0, eq->getAnalyzerSampleRate()) : 1.0;
        constexpr size_t kSpectrumBins = 160;
        const size_t sampleCount = analyzerWindow.size();
        std::array<float, kSpectrumBins> nextMagnitudes{};
        std::array<float, Aestra::Audio::Plugins::AestraEQ::kAnalyzerWindowSize> windowedSamples{};

        for (size_t n = 0; n < sampleCount; ++n) {
            const float window = 0.5f - 0.5f * std::cos((2.0f * kPi * static_cast<float>(n)) /
                                                        static_cast<float>(sampleCount - 1));
            windowedSamples[n] = analyzerWindow[n] * window;
        }

        for (size_t bin = 0; bin < nextMagnitudes.size(); ++bin) {
            const float norm = static_cast<float>(bin) / static_cast<float>(nextMagnitudes.size() - 1);
            const float targetHz = std::pow(10.0f,
                                            std::log10(20.0f) +
                                                norm * (std::log10(20000.0f) - std::log10(20.0f)));
            const float omega = 2.0f * kPi * targetHz / static_cast<float>(sampleRate);
            const float cosine = std::cos(omega);
            const float sine = std::sin(omega);
            const float coeff = 2.0f * cosine;
            float q0 = 0.0f;
            float q1 = 0.0f;
            float q2 = 0.0f;

            for (size_t n = 0; n < sampleCount; ++n) {
                q0 = coeff * q1 - q2 + windowedSamples[n];
                q2 = q1;
                q1 = q0;
            }

            const float real = q1 - q2 * cosine;
            const float imag = q2 * sine;
            const float magnitude = std::sqrt(real * real + imag * imag) / static_cast<float>(sampleCount);
            const float db = 20.0f * std::log10(std::max(magnitude * 8.0f, 1.0e-5f));
            nextMagnitudes[bin] = std::clamp((db + 72.0f) / 72.0f, 0.0f, 1.0f);
        }

        {
            std::lock_guard<std::mutex> lock(m_spectrumMutex);
            m_workerResultMagnitudes = nextMagnitudes;
            m_workerResultSerial = serial;
            m_spectrumResultReady = true;
        }
    }
}

void AestraEQEditor::drawSpectrumBackdrop(NUIRenderer& renderer, const NUIRect& bounds) {
    if (m_lastAnalyzerSerial == 0) {
        return;
    }

    const float left = bounds.x + 50.0f;
    const float right = bounds.right() - 18.0f;
    const float top = bounds.y + 22.0f;
    const float bottom = bounds.bottom() - 20.0f;
    const float width = right - left;
    const float height = bottom - top;

    for (size_t i = 0; i < m_spectrumMagnitudes.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(m_spectrumMagnitudes.size() - 1);
        const float x = left + t * width;
        const float nextT = static_cast<float>(i + 1) / static_cast<float>(m_spectrumMagnitudes.size() - 1);
        const float nextX = (i + 1 < m_spectrumMagnitudes.size()) ? (left + nextT * width) : right;
        const float barWidth = std::max(1.0f, nextX - x);
        const float currentMag = std::clamp(m_spectrumMagnitudes[i], 0.0f, 1.0f);
        const float nextMag = (i + 1 < m_spectrumMagnitudes.size())
            ? std::clamp(m_spectrumMagnitudes[i + 1], 0.0f, 1.0f)
            : currentMag;
        const float mag = currentMag * 0.65f + nextMag * 0.35f;
        const float barHeight = mag * height;
        const float y = bottom - barHeight;

        const NUIColor glow(0.55f, 0.38f, 0.92f, 0.026f + mag * 0.082f);
        const NUIColor core(0.0f, 0.88f, 0.80f, 0.018f + mag * 0.050f);
        renderer.fillRect({x, y, barWidth, barHeight}, glow);
        renderer.fillRect({x, std::max(top, y + barHeight * 0.38f), barWidth, barHeight * 0.62f}, core);
    }
}

NUIRect AestraEQEditor::responseGraphBounds(const NUIRect& outerBounds) const {
    return {outerBounds.x + 50.0f, outerBounds.y + 22.0f,
            outerBounds.width - 68.0f, outerBounds.height - 42.0f};
}

bool AestraEQEditor::usesGainAxis(const BandControl& band) const {
    return band.usesGain;
}

NUIPoint AestraEQEditor::graphNodePosition(const BandControl& band, const NUIRect& graphBounds) const {
    const NUIRect safeBounds = graphNodeSafeBounds(graphBounds);
    const size_t bandIdx = static_cast<size_t>(&band - m_bands.data());

    auto bandFreqToGraphNorm = [](size_t idx, float norm) -> float {
        float hz = 1000.0f;
        switch (idx) {
        case 0: hz = 20.0f * std::pow(25.0f, std::clamp(norm, 0.0f, 1.0f)); break;
        case 1: hz = 40.0f * std::pow(25.0f, std::clamp(norm, 0.0f, 1.0f)); break;
        case 2: hz = 80.0f * std::pow(100.0f, std::clamp(norm, 0.0f, 1.0f)); break;
        case 3: hz = 200.0f * std::pow(80.0f, std::clamp(norm, 0.0f, 1.0f)); break;
        case 4: hz = 2000.0f * std::pow(10.0f, std::clamp(norm, 0.0f, 1.0f)); break;
        case 5: hz = 1000.0f * std::pow(20.0f, std::clamp(norm, 0.0f, 1.0f)); break;
        }
        float logMin = std::log10(20.0f), logMax = std::log10(20000.0f);
        return (std::log10(hz) - logMin) / (logMax - logMin);
    };

    const float x = safeBounds.x + bandFreqToGraphNorm(bandIdx, band.freq) * safeBounds.width;
    const float y = usesGainAxis(band)
        ? (safeBounds.bottom() - band.gain * safeBounds.height)
        : (safeBounds.y + safeBounds.height * 0.5f);
    return {x, y};
}

int AestraEQEditor::hitTestGraphNode(float x, float y) const {
    if (m_lastResponseBounds.isEmpty() || !m_lastResponseBounds.contains(x, y)) {
        return -1;
    }

    int bestIndex = -1;
    float bestDistance = 18.0f;
    for (size_t i = 0; i < m_bands.size(); ++i) {
        if (!m_bands[i].enabled) {
            continue;
        }
        const NUIPoint node = graphNodePosition(m_bands[i], m_lastResponseBounds);
        const float distance = node.distanceTo({x, y});
        if (distance <= bestDistance) {
            bestDistance = distance;
            bestIndex = static_cast<int>(i);
        }
    }
    return bestIndex;
}

void AestraEQEditor::updateBandFromGraphPosition(int bandIndex, const NUIPoint& position) {
    if (bandIndex < 0 || bandIndex >= static_cast<int>(m_bands.size()) || !m_instance || m_lastResponseBounds.isEmpty()) {
        return;
    }

    auto& band = m_bands[bandIndex];
    const NUIRect safeBounds = graphNodeSafeBounds(m_lastResponseBounds);
    const float graphNorm = std::clamp((position.x - safeBounds.x) / std::max(1.0f, safeBounds.width), 0.0f, 1.0f);
    const float verticalNorm = std::clamp(1.0f - ((position.y - safeBounds.y) / std::max(1.0f, safeBounds.height)), 0.0f, 1.0f);

    // Convert graph frequency norm (20-20kHz) to band-specific frequency norm
    auto graphNormToHz = [](float norm) -> float {
        float logMin = std::log10(20.0f), logMax = std::log10(20000.0f);
        return std::pow(10.0f, logMin + norm * (logMax - logMin));
    };
    auto hzToBandNorm = [](size_t idx, float hz) -> float {
        switch (idx) {
        case 0: return std::clamp(std::log10(hz / 20.0f) / std::log10(25.0f), 0.0f, 1.0f);
        case 1: return std::clamp(std::log10(hz / 40.0f) / std::log10(25.0f), 0.0f, 1.0f);
        case 2: return std::clamp(std::log10(hz / 80.0f) / std::log10(100.0f), 0.0f, 1.0f);
        case 3: return std::clamp(std::log10(hz / 200.0f) / std::log10(80.0f), 0.0f, 1.0f);
        case 4: return std::clamp(std::log10(hz / 2000.0f) / std::log10(10.0f), 0.0f, 1.0f);
        case 5: return std::clamp(std::log10(hz / 1000.0f) / std::log10(20.0f), 0.0f, 1.0f);
        default: return 0.5f;
        }
    };

    const float hz = graphNormToHz(graphNorm);
    const float freqNorm = hzToBandNorm(static_cast<size_t>(bandIndex), hz);

    band.freq = freqNorm;
    m_instance->setParameter(band.freqId, band.freq);
    if (band.usesGain) {
        band.gain = verticalNorm;
        m_instance->setParameter(band.gainId, band.gain);
    } else if (band.usesSlope) {
        const float quantized = quantizeCutSlopeNorm(verticalNorm);
        band.q = quantized;
        m_instance->setParameter(band.qId, band.q);
    } else {
        band.q = verticalNorm;
        m_instance->setParameter(band.qId, band.q);
    }
    layoutControls();
    setDirty(true);
}

void AestraEQEditor::drawBandPanel(NUIRenderer& renderer, const BandControl& band) {
    const size_t bandIndex = static_cast<size_t>(&band - m_bands.data());
    NUIColor accent = bandColorForIndex(bandIndex);
    const bool hot = band.hovered || band.dragging || static_cast<int>(bandIndex) == m_selectedBand;
    NUIColor cardColor = hot ? NUIColor(0.095f, 0.078f, 0.130f, 0.94f) : NUIColor(0.045f, 0.043f, 0.064f, 0.92f);

    renderer.fillRoundedRect(band.bounds, 11.0f, cardColor);
    renderer.strokeRoundedRect(band.bounds, 11.0f, hot ? 1.7f : 1.0f,
                               band.enabled ? (hot ? accent.withAlpha(0.54f) : kBlueprintDim.withAlpha(0.78f))
                                            : kBlueprintDim.withAlpha(0.30f));

    const NUIRect header{band.bounds.x, band.bounds.y, band.bounds.width, 25.0f};
    renderer.fillRoundedRect({header.x, header.y, header.width, 4.0f}, 2.0f,
                             band.enabled ? accent.withAlpha(hot ? 0.72f : 0.42f)
                                          : kBlueprintMuted.withAlpha(0.14f));
    renderer.drawLine({header.x + 10.0f, header.bottom()}, {header.right() - 10.0f, header.bottom()}, 1.0f,
                      accent.withAlpha(band.enabled ? 0.20f : 0.08f));
    renderer.fillCircle({band.bounds.x + 12.0f, band.bounds.y + 12.0f}, 3.0f,
                        band.enabled ? accent.withAlpha(0.95f) : kBlueprintMuted.withAlpha(0.20f));
    renderer.drawText(band.name, {band.bounds.x + 21.0f, band.bounds.y + 8.0f}, 10.0f,
                      band.enabled ? kBlueprintText : kBlueprintMuted.withAlpha(0.42f));
    renderer.drawText(typeLabel(band.type), {band.bounds.right() - 50.0f, band.bounds.y + 8.0f}, 8.5f,
                      band.enabled ? accent.withAlpha(0.94f) : kBlueprintMuted.withAlpha(0.42f));

    if (!band.enabled) {
        renderer.drawText("OFF", {band.bounds.x + 9.0f, band.bounds.y + 54.0f}, 16.0f, kBlueprintMuted.withAlpha(0.35f));
        return;
    }

    drawBlueprintKnob(renderer, band.freqKnob.center(), 15.0f, band.freq, "FREQ", bandFreqLabel(bandIndex, band.freq), true, accent);
    if (band.usesGain) {
        drawBlueprintKnob(renderer, band.gainKnob.center(), 15.0f, band.gain, "GAIN", gainLabel(band.gain) + " dB", true, accent);
    }
    if (band.usesSlope) {
        const uint32_t slopeVal = cutSlopeDbPerOct(band.q);
        drawBlueprintKnob(renderer, band.qKnob.center(), 15.0f, band.q, "SLOPE", std::to_string(slopeVal) + " dB", true, accent);
    } else {
        drawBlueprintKnob(renderer, band.qKnob.center(), 15.0f, band.q, "Q", qLabel(band.q, band.type), true, accent);
    }
}

void AestraEQEditor::drawDynamicSection(NUIRenderer& renderer, const NUIRect& bounds) {
    const float sideW = bounds.width * 0.18f;
    const float graphW = bounds.width * 0.20f;
    const float knobAreaW = bounds.width - sideW - graphW - 28.0f;
    const NUIRect left{bounds.x, bounds.y, sideW, bounds.height};
    const NUIRect knobs{left.right() + 8.0f, bounds.y, knobAreaW, bounds.height};
    const NUIRect graph{knobs.right() + 8.0f, bounds.y, graphW, bounds.height};
    const NUIRect meter{graph.right() + 8.0f, bounds.y, bounds.right() - graph.right() - 8.0f, bounds.height};

    renderer.fillRoundedRect(left, 10.0f, NUIColor(0.040f, 0.038f, 0.056f, 0.90f));
    renderer.strokeRoundedRect(left, 11.0f, 1.0f, kBlueprintDim.withAlpha(0.72f));
    drawBlueprintLabel(renderer, "DYNAMIC EQ", left);
    renderer.fillRoundedRect({left.x + 12.0f, left.y + 42.0f, 34.0f, 24.0f}, 7.0f, kAccentPurple.withAlpha(0.28f));
    renderer.drawText("ON", {left.x + 22.0f, left.y + 50.0f}, 8.0f, kBlueprintText);
    renderer.drawText("MODE", {left.x + 78.0f, left.y + 26.0f}, 8.0f, kBlueprintMuted);
    renderer.drawText("Bell   Wide", {left.x + 78.0f, left.y + 50.0f}, 9.5f, kBlueprintText);
    renderer.drawText("Sidechain", {left.x + 78.0f, left.y + 82.0f}, 8.0f, kBlueprintMuted);
    renderer.drawText("Internal", {left.x + 78.0f, left.y + 106.0f}, 9.5f, kBlueprintText);

    renderer.fillRoundedRect(knobs, 10.0f, NUIColor(0.040f, 0.038f, 0.056f, 0.90f));
    renderer.strokeRoundedRect(knobs, 11.0f, 1.0f, kBlueprintDim.withAlpha(0.72f));
    static const char* labels[] = {"THRESHOLD", "RANGE", "RATIO", "ATTACK", "RELEASE", "MAKEUP", "MIX"};
    static const char* values[] = {"-24.0 dB", "-6.0 dB", "3.0:1", "10 ms", "120 ms", "0.0 dB", "100%"};
    const float step = knobs.width / 7.0f;
    for (int i = 0; i < 7; ++i) {
        const float cx = knobs.x + step * (static_cast<float>(i) + 0.5f);
        drawBlueprintKnob(renderer, {cx, knobs.y + 62.0f}, 19.0f, 0.50f + static_cast<float>(i % 3 - 1) * 0.08f,
                          labels[i], values[i], true, i == 6 ? kAccentCyan : kAccentPurple);
    }

    renderer.fillRoundedRect(graph, 10.0f, NUIColor(0.040f, 0.038f, 0.056f, 0.90f));
    renderer.strokeRoundedRect(graph, 11.0f, 1.0f, kBlueprintDim.withAlpha(0.72f));
    for (int i = 1; i < 4; ++i) {
        const float y = graph.y + static_cast<float>(i) * graph.height / 4.0f;
        renderer.drawLine({graph.x + 14.0f, y}, {graph.right() - 14.0f, y}, 1.0f, kBlueprintDim.withAlpha(0.20f));
    }
    std::vector<NUIPoint> curve{{graph.x + 18.0f, graph.bottom() - 26.0f},
                                {graph.x + graph.width * 0.42f, graph.y + graph.height * 0.52f},
                                {graph.x + graph.width * 0.72f, graph.y + graph.height * 0.32f},
                                {graph.right() - 20.0f, graph.y + 30.0f}};
    renderer.drawPolyline(curve.data(), static_cast<int>(curve.size()), 2.0f, kAccentPurple.withAlpha(0.85f));

    renderer.fillRoundedRect(meter, 10.0f, NUIColor(0.040f, 0.038f, 0.056f, 0.90f));
    renderer.strokeRoundedRect(meter, 11.0f, 1.0f, kBlueprintDim.withAlpha(0.72f));
    renderer.drawText("GR", {meter.x + 18.0f, meter.y + 14.0f}, 10.0f, kBlueprintText);
    const NUIRect gr{meter.x + 25.0f, meter.y + 42.0f, 12.0f, meter.height - 60.0f};
    renderer.fillRoundedRect(gr, 2.0f, kGlassDeep);
    renderer.fillRoundedRect({gr.x, gr.y + gr.height * 0.48f, gr.width, gr.height * 0.52f}, 2.0f, kAccentPurple.withAlpha(0.82f));
}

void AestraEQEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    updateSpectrumSnapshot();
    auto bounds = getBounds();
    drawBlueprintGrid(renderer, bounds);

    const float graphTop = bounds.y + AestraPanelWindow::TITLE_BAR_H + 12.0f;
    const float sideW = 104.0f;
    const float filterW = 118.0f;
    const float gap = 8.0f;
    const NUIRect inputPanel{bounds.x + kPadding, graphTop, sideW, kCurveHeight};
    const NUIRect hpfPanel{inputPanel.right() + gap, graphTop, filterW, kCurveHeight};
    const NUIRect outputPanel{bounds.right() - kPadding - sideW, graphTop, sideW, kCurveHeight};
    const NUIRect lpfPanel{outputPanel.x - gap - filterW, graphTop, filterW, kCurveHeight};
    const NUIRect graphPanel{hpfPanel.right() + gap, graphTop,
                             lpfPanel.x - hpfPanel.right() - gap * 2.0f, kCurveHeight};

    drawInputOutputPanel(renderer, inputPanel, false);
    drawFilterGuardPanel(renderer, hpfPanel, true);
    drawResponseCurve(renderer, graphPanel);
    drawFilterGuardPanel(renderer, lpfPanel, false);
    drawInputOutputPanel(renderer, outputPanel, true);
    drawUtilityStrip(renderer, {bounds.x + kPadding, bounds.y + AestraPanelWindow::TITLE_BAR_H + kCurveHeight + 20.0f,
                                bounds.width - kPadding * 2.0f, 48.0f});

    if (!m_bands.empty()) {
        const auto& first = m_bands.front();
        const auto& last = m_bands.back();
        renderer.fillRoundedRect(
            {first.bounds.x - 4.0f, first.bounds.y - 8.0f,
             last.bounds.right() - first.bounds.x + 8.0f, first.bounds.height + 16.0f},
            14.0f, NUIColor(0.024f, 0.022f, 0.034f, 0.74f));
        renderer.strokeRoundedRect(
            {first.bounds.x - 4.0f, first.bounds.y - 8.0f,
             last.bounds.right() - first.bounds.x + 8.0f, first.bounds.height + 16.0f},
            14.0f, 1.0f, kBlueprintLine.withAlpha(0.28f));
    }

    for (const auto& band : m_bands) {
        drawBandPanel(renderer, band);
    }

    drawDynamicSection(renderer, {bounds.x + kPadding, bounds.bottom() - 126.0f,
                                  bounds.width - kPadding * 2.0f, 104.0f});

    renderer.drawText("AESTRA EQ", {bounds.x + 18.0f, bounds.bottom() - 16.0f}, 8.0f, kBlueprintMuted);
    renderer.drawText("Zero Latency", {bounds.x + bounds.width * 0.30f, bounds.bottom() - 16.0f}, 8.0f, kBlueprintMuted);
    renderer.drawText("Oversampling: 2x", {bounds.x + bounds.width * 0.46f, bounds.bottom() - 16.0f}, 8.0f, kBlueprintMuted);
    renderer.drawText("Max Bands: 6", {bounds.x + bounds.width * 0.62f, bounds.bottom() - 16.0f}, 8.0f, kBlueprintMuted);
}

int AestraEQEditor::hitTestBand(float x, float y) const {
    for (size_t i = 0; i < m_bands.size(); ++i) {
        if (m_bands[i].bounds.contains({x, y})) return static_cast<int>(i);
    }
    return -1;
}

AestraEQEditor::BandControl::DragTarget AestraEQEditor::hitTestSlider(float x, float y, const BandControl& band) const {
    if (band.freqKnob.contains({x, y}) || band.freqSlider.contains({x, y})) return BandControl::Freq;
    if (band.gainKnob.contains({x, y}) || band.gainSlider.contains({x, y})) return BandControl::Gain;
    if (band.qKnob.contains({x, y}) || band.qSlider.contains({x, y})) return BandControl::Q;
    return BandControl::None;
}

void AestraEQEditor::updateBandValue(int bandIndex, BandControl::DragTarget target, float normalizedValue) {
    if (bandIndex < 0 || bandIndex >= static_cast<int>(m_bands.size()) || !m_instance) return;
    auto& b = m_bands[bandIndex];
    normalizedValue = std::clamp(normalizedValue, 0.0f, 1.0f);

    switch (target) {
    case BandControl::Freq:
        b.freq = normalizedValue;
        m_instance->setParameter(b.freqId, b.freq);
        break;
    case BandControl::Gain:
        if (b.usesGain) {
            b.gain = normalizedValue;
            m_instance->setParameter(b.gainId, b.gain);
        }
        break;
    case BandControl::Q:
        if (b.usesSlope) {
            b.q = quantizeCutSlopeNorm(normalizedValue);
        } else {
            b.q = normalizedValue;
        }
        m_instance->setParameter(b.qId, b.q);
        break;
    default: break;
    }
    layoutControls();
    setDirty(true);
}

std::string AestraEQEditor::typeLabel(uint32_t type) const {
    static const char* names[] = {"Bell", "HiPass", "LoPass", "LoShelf", "HiShelf", "Notch", "BP", "Tilt"};
    return type < 8 ? names[type] : "Bell";
}
std::string AestraEQEditor::bandFreqLabel(size_t bandIdx, float norm) const {
    float hz = 1000.0f;
    switch (bandIdx) {
    case 0: hz = 20.0f * std::pow(25.0f, std::clamp(norm, 0.0f, 1.0f)); break;
    case 1: hz = 40.0f * std::pow(25.0f, std::clamp(norm, 0.0f, 1.0f)); break;
    case 2: hz = 80.0f * std::pow(100.0f, std::clamp(norm, 0.0f, 1.0f)); break;
    case 3: hz = 200.0f * std::pow(80.0f, std::clamp(norm, 0.0f, 1.0f)); break;
    case 4: hz = 2000.0f * std::pow(10.0f, std::clamp(norm, 0.0f, 1.0f)); break;
    case 5: hz = 1000.0f * std::pow(20.0f, std::clamp(norm, 0.0f, 1.0f)); break;
    }
    if (hz >= 1000) { std::ostringstream o; o << std::fixed << std::setprecision(1) << hz / 1000.0f << "k"; return o.str(); }
    return std::to_string(static_cast<int>(hz + 0.5f));
}
std::string AestraEQEditor::gainLabel(float norm) const {
    float db = -18.0f + norm * 36.0f;
    std::ostringstream o; o << std::fixed << std::setprecision(1) << db; return o.str();
}
std::string AestraEQEditor::qLabel(float norm, uint32_t type) const {
    const auto filterType = static_cast<Aestra::Audio::Plugins::FilterType>(type);
    if (filterType == Aestra::Audio::Plugins::FilterType::LowCut || filterType == Aestra::Audio::Plugins::FilterType::HighCut) {
        return std::to_string(cutSlopeDbPerOct(norm)) + "dB";
    }
    float q = 0.1f + norm * 9.9f;
    std::ostringstream o; o << std::fixed << std::setprecision(1) << q; return o.str();
}

bool AestraEQEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;

    if (AestraPanelWindow::onMouseEvent(event)) return true;

    auto bounds = getBounds();
    bool isDraggingBand = std::any_of(m_bands.begin(), m_bands.end(),
                                      [](const BandControl& band) { return band.dragging; });
    bool contains = bounds.contains(event.position);

    if (m_bandTypeMenu && m_bandTypeMenu->isVisible()) {
        if (m_bandTypeMenu->onMouseEvent(event)) {
            return true;
        }
        if (event.pressed && event.button == NUIMouseButton::Left && !m_bandTypeMenu->getBounds().contains(event.position)) {
            m_bandTypeMenu->hide();
            if (auto parent = m_bandTypeMenu->getParent()) {
                parent->removeChild(m_bandTypeMenu);
            }
            m_bandTypeMenu.reset();
        }
    }

    if (!contains && !isDraggingWindow() && !isDraggingBand) return false;

    if (event.wheelDelta != 0.0f) {
        int graphBandIdx = hitTestGraphNode(event.position.x, event.position.y);
        if (graphBandIdx >= 0) {
            auto& band = m_bands[graphBandIdx];
            m_selectedBand = graphBandIdx;
            if (band.usesSlope) {
                const float currentIndex = std::round(quantizeCutSlopeNorm(band.q) * 3.0f);
                const float nextIndex = std::clamp(currentIndex + (event.wheelDelta > 0.0f ? 1.0f : -1.0f), 0.0f, 3.0f);
                band.q = nextIndex / 3.0f;
            } else {
                const float step = 0.03f;
                band.q = std::clamp(band.q + (event.wheelDelta > 0.0f ? step : -step), 0.0f, 1.0f);
            }
            if (m_instance) {
                m_instance->setParameter(band.qId, band.q);
            }
            layoutControls();
            setDirty(true);
            return true;
        }
    }

    if (event.pressed && event.button == NUIMouseButton::Right) {
        int graphBandIdx = hitTestGraphNode(event.position.x, event.position.y);
        int bandIdx = (graphBandIdx >= 0) ? graphBandIdx : hitTestBand(event.position.x, event.position.y);
        if (bandIdx >= 0) {
            m_selectedBand = bandIdx;
            auto menu = std::make_shared<NUIContextMenu>();
            static const char* labels[] = {"Bell", "Low Cut", "High Cut", "Low Shelf", "High Shelf", "Notch", "Band Pass", "Tilt"};
            for (uint32_t type = 0; type < 8; ++type) {
                auto item = std::make_shared<NUIContextMenuItem>(labels[type], NUIContextMenuItem::Type::Radio);
                item->setRadioGroup("eq_band_type");
                item->setChecked(m_bands[bandIdx].type == type);
                item->setOnClick([this, bandIdx, type]() {
                    if (bandIdx < 0 || bandIdx >= static_cast<int>(m_bands.size())) {
                        return;
                    }
                    // V1: type changes are display-only (DSP uses fixed band types)
                    m_bands[bandIdx].type = type;
                    if (m_bandTypeMenu) {
                        m_bandTypeMenu->hide();
                    }
                    setDirty(true);
                });
                menu->addItem(item);
            }
            if (auto parent = getParent()) {
                if (m_bandTypeMenu && m_bandTypeMenu->getParent()) {
                    m_bandTypeMenu->getParent()->removeChild(m_bandTypeMenu);
                }
                m_bandTypeMenu = menu;
                parent->addChild(std::static_pointer_cast<NUIComponent>(menu));
                menu->showAt(event.position);
                return true;
            }
        }
    }

    if (event.pressed && event.button == NUIMouseButton::Left) {
        int graphBandIdx = hitTestGraphNode(event.position.x, event.position.y);
        if (graphBandIdx >= 0) {
            m_selectedBand = graphBandIdx;
            m_draggingGraphBand = graphBandIdx;
            updateBandFromGraphPosition(graphBandIdx, event.position);
            return true;
        }

        int bandIdx = hitTestBand(event.position.x, event.position.y);
        if (bandIdx >= 0) {
            auto& band = m_bands[bandIdx];
            m_selectedBand = bandIdx;
            auto target = hitTestSlider(event.position.x, event.position.y, band);
            if (target != BandControl::None) {
                band.dragging = true;
                band.dragTarget = target;
                band.dragStartX = event.position.y;
                band.dragStartValue = (target == BandControl::Freq) ? band.freq : (target == BandControl::Gain ? band.gain : band.q);
                return true;
            }
            // Toggle enabled on click outside sliders
            if (!band.dragging) {
                m_instance->setParameter(band.enableId, band.enabled ? 0.0f : 1.0f);
                band.enabled = !band.enabled;
                layoutControls();
                setDirty(true);
                return true;
            }
        }
    }

    if (m_draggingGraphBand >= 0) {
        updateBandFromGraphPosition(m_draggingGraphBand, event.position);
        if (!event.pressed && event.button == NUIMouseButton::Left) {
            m_draggingGraphBand = -1;
        }
        return true;
    }

    for (size_t i = 0; i < m_bands.size(); ++i) {
        auto& band = m_bands[i];
        if (!band.dragging) {
            continue;
        }

        const float val = band.dragStartValue + (band.dragStartX - event.position.y) * 0.0065f;
        updateBandValue(static_cast<int>(i), band.dragTarget, val);

        if (!event.pressed && event.button == NUIMouseButton::Left) {
            band.dragging = false;
            band.dragTarget = BandControl::None;
        }
        return true;
    }

    if (!event.pressed && event.button == NUIMouseButton::Left) {
        for (auto& band : m_bands) {
            if (band.dragging) {
                band.dragging = false;
                band.dragTarget = BandControl::None;
                return true;
            }
        }
    }

    if (!event.pressed && !event.released) {
        int hovered = contains ? hitTestGraphNode(event.position.x, event.position.y) : -1;
        if (hovered < 0 && contains) {
            hovered = hitTestBand(event.position.x, event.position.y);
        }
        if (hovered != m_hoveredBand) {
            m_hoveredBand = hovered;
            for (size_t i = 0; i < m_bands.size(); ++i) m_bands[i].hovered = (static_cast<int>(i) == hovered);
            setDirty(true);
        }
    }

    return contains;
}

} // namespace AestraUI
