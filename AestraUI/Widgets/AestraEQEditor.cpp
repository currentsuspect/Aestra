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
constexpr float kRadius = 12.0f;
constexpr float kPi = 3.14159265358979323846f;

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

bool usesDiscreteCutSlope(uint32_t type) {
    using FilterType = Aestra::Audio::Plugins::FilterType;
    const auto filterType = static_cast<FilterType>(type);
    return filterType == FilterType::LowCut || filterType == FilterType::HighCut;
}

float quantizeCutSlopeNorm(float norm) {
    static constexpr float kSlopeSteps[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    const float clamped = std::clamp(norm, 0.0f, 1.0f);
    int bestIndex = 0;
    float bestDistance = std::abs(clamped - kSlopeSteps[0]);
    for (int i = 1; i < 5; ++i) {
        const float distance = std::abs(clamped - kSlopeSteps[i]);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
    }
    return kSlopeSteps[bestIndex];
}

uint32_t cutSlopeDbPerOct(float norm) {
    static constexpr uint32_t kSlopeDb[] = {12u, 24u, 48u, 72u, 96u};
    const float quantized = quantizeCutSlopeNorm(norm);
    const int index = static_cast<int>(std::round(quantized * 4.0f));
    return kSlopeDb[std::clamp(index, 0, 4)];
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
}

AestraEQEditor::AestraEQEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraEQEditor");
    setSize(kWindowWidth, kWindowHeight);
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

    for (size_t i = 0; i < kNumBands; ++i) {
        BandControl bc;
        bc.paramBase = static_cast<uint32_t>(i * 5);
        bc.name = "B" + std::to_string(static_cast<int>(i + 1));
        bc.enabled = m_instance->getParameter(bc.paramBase) > 0.5f;
        bc.type = static_cast<uint32_t>(m_instance->getParameter(bc.paramBase + 1) * 7.0f + 0.5f);
        bc.freq = m_instance->getParameter(bc.paramBase + 2);
        bc.gain = m_instance->getParameter(bc.paramBase + 3);
        bc.q = m_instance->getParameter(bc.paramBase + 4);
        m_bands.push_back(std::move(bc));
    }
    layoutControls();
}

void AestraEQEditor::layoutControls() {
    auto bounds = getBounds();
    constexpr float bandGap = 12.0f;
    float bandW = (bounds.width - kPadding * 2.0f - bandGap * 7.0f) / 8.0f;
    float y = bounds.y + kTitleHeight + kCurveHeight + 14.0f;
    float h = bounds.bottom() - y - kPadding;

    for (size_t i = 0; i < m_bands.size(); ++i) {
        auto& b = m_bands[i];
        float x = bounds.x + kPadding + i * (bandW + bandGap);
        b.bounds = NUIRect(x, y, bandW, h);
        float sliderW = 8.0f;
        float knobSize = 14.0f;
        float sliderTop = y + 30.0f;
        float sliderHeight = std::max(40.0f, h - 58.0f);
        float cx = x + bandW * 0.5f;

        b.freqSlider = {};
        b.freqKnob = {};
        b.gainSlider = {};
        b.gainKnob = {};

        b.qSlider = NUIRect(cx - sliderW * 0.5f, sliderTop, sliderW, sliderHeight);
        b.qKnob = NUIRect(cx - knobSize * 0.5f,
                          sliderTop + (1.0f - b.q) * sliderHeight - knobSize * 0.5f,
                          knobSize, knobSize);
    }
}

void AestraEQEditor::drawTitleBar(NUIRenderer& renderer) {
    auto bounds = getBounds();
    auto& theme = NUIThemeManager::getInstance();
    NUIRect titleBar(bounds.x, bounds.y, bounds.width, kTitleHeight);

    renderer.fillRoundedRect(titleBar, kRadius, NUIColor(0.08f, 0.07f, 0.10f, 0.98f));
    renderer.drawText("Aestra EQ", {titleBar.x + kPadding, titleBar.y + 14.0f}, 13.0f,
                      theme.getColor("textPrimary"));
    renderer.drawText("8-band parametric", {titleBar.x + 110.0f, titleBar.y + 15.0f}, 11.0f,
                      theme.getColor("accentPrimary").withAlpha(0.85f));

    float closeX = titleBar.right() - kCloseSize - 10.0f;
    float closeY = titleBar.y + (kTitleHeight - kCloseSize) * 0.5f;
    renderer.drawLine({closeX + 4.0f, closeY + 4.0f}, {closeX + 12.0f, closeY + 12.0f}, 1.5f,
                      theme.getColor("textSecondary"));
    renderer.drawLine({closeX + 12.0f, closeY + 4.0f}, {closeX + 4.0f, closeY + 12.0f}, 1.5f,
                      theme.getColor("textSecondary"));
    renderer.drawLine({titleBar.x, titleBar.bottom()}, {titleBar.right(), titleBar.bottom()},
                      1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.06f));
}

void AestraEQEditor::drawResponseCurve(NUIRenderer& renderer, const NUIRect& bounds) {
    auto& theme = NUIThemeManager::getInstance();
    renderer.fillRoundedRect(bounds, kRadius, NUIColor(0.06f, 0.06f, 0.07f, 0.95f));
    renderer.strokeRoundedRect(bounds, kRadius, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.08f));
    drawSpectrumBackdrop(renderer, bounds);
    m_lastResponseBounds = responseGraphBounds(bounds);

    // Compute approximate response curve
    auto freqToHz = [](float norm) {
        float logMin = std::log10(20.0f), logMax = std::log10(20000.0f);
        return std::pow(10.0f, logMin + norm * (logMax - logMin));
    };
    auto gainToDb = [](float norm) { return -18.0f + norm * 36.0f; };
    auto qToLinear = [](float norm) { return 0.1f + norm * 9.9f; };
    auto filterStageCount = [](uint32_t type, float norm) {
        using FilterType = Aestra::Audio::Plugins::FilterType;
        const auto filterType = static_cast<FilterType>(type);
        if (!usesDiscreteCutSlope(type)) {
            return 1u;
        }
        switch (cutSlopeDbPerOct(norm)) {
        case 12u: return 1u;
        case 24u: return 2u;
        case 48u: return 4u;
        case 72u: return 6u;
        case 96u: return 8u;
        default: return 1u;
        }
    };
    auto effectiveQ = [&](uint32_t type, float norm) {
        using FilterType = Aestra::Audio::Plugins::FilterType;
        const auto filterType = static_cast<FilterType>(type);
        if (usesDiscreteCutSlope(type)) {
            return 0.70710678f;
        }
        return qToLinear(norm);
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

    // Compute total response at a higher point density so the graph reads as a
    // continuous curve rather than a chain of obvious line segments.
    const int numPoints = 1400;
    std::vector<float> responseDb(numPoints, 0.0f);
    for (int p = 0; p < numPoints; ++p) {
        float freqNorm = static_cast<float>(p) / (numPoints - 1);
        float freq = freqToHz(freqNorm);
        double omega = 2.0 * static_cast<double>(kPi) * static_cast<double>(freq) / static_cast<double>(sampleRate);
        for (const auto& band : m_bands) {
            if (!band.enabled) continue;
            float f0 = freqToHz(band.freq);
            float gainDb = gainToDb(band.gain);
            float Q = effectiveQ(band.type, band.q);
            const auto type = static_cast<Aestra::Audio::Plugins::FilterType>(band.type);
            const auto coeffs = Aestra::Audio::Plugins::designBiquad(type, f0, gainDb, Q, sampleRate);
            float bandResponse = biquadMagnitudeDb(coeffs, omega) * static_cast<float>(filterStageCount(band.type, band.q));
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
        renderer.drawLine({bounds.x + 40.0f, y}, {bounds.right() - 10.0f, y},
                          1.0f, theme.getColor("accentPrimary").withAlpha(alpha));
        std::string label = (db >= 0 ? "+" : "") + std::to_string(db) + "dB";
        renderer.drawText(label, {bounds.x + 4.0f, y - 6.0f}, 9.0f,
                          theme.getColor("textSecondary").withAlpha(0.84f));
    }

    // Frequency labels
    float freqs[] = {30, 100, 300, 1000, 3000, 10000};
    const char* freqLabels[] = {"30", "100", "300", "1k", "3k", "10k"};
    for (int i = 0; i < 6; ++i) {
        float norm = (std::log10(freqs[i]) - std::log10(20.0f)) / (std::log10(20000.0f) - std::log10(20.0f));
        float x = bounds.x + 40.0f + norm * (bounds.width - 50.0f);
        renderer.drawLine({x, bounds.y + 10.0f}, {x, bounds.bottom() - 10.0f},
                          1.0f, theme.getColor("textSecondary").withAlpha(0.10f));
        renderer.drawText(freqLabels[i], {x - 8.0f, bounds.bottom() - 10.0f}, 9.0f,
                          theme.getColor("textSecondary").withAlpha(0.84f));
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
    const NUIColor curveColor = theme.getColor("accentPrimary").withAlpha(0.85f);
    std::vector<NUIPoint> offsetUp = smoothCurvePoints;
    std::vector<NUIPoint> offsetDown = smoothCurvePoints;
    for (size_t i = 0; i < smoothCurvePoints.size(); ++i) {
        offsetUp[i].y -= 0.75f;
        offsetDown[i].y += 0.75f;
    }

    renderer.drawPolyline(offsetUp.data(), static_cast<int>(offsetUp.size()), 4.0f, curveColor.withAlpha(0.07f));
    renderer.drawPolyline(offsetDown.data(), static_cast<int>(offsetDown.size()), 4.0f, curveColor.withAlpha(0.07f));
    renderer.drawPolyline(smoothCurvePoints.data(), static_cast<int>(smoothCurvePoints.size()), 6.0f, curveColor.withAlpha(0.10f));
    renderer.drawPolyline(smoothCurvePoints.data(), static_cast<int>(smoothCurvePoints.size()), 3.0f, curveColor.withAlpha(0.34f));
    renderer.drawPolyline(smoothCurvePoints.data(), static_cast<int>(smoothCurvePoints.size()), 1.4f, curveColor.withAlpha(0.96f));

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
        renderer.fillCircle(node, radius, NUIColor(0.08f, 0.08f, 0.10f, 0.95f));
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
        renderer.fillRoundedRect(pillRect, 7.0f,
                                 selected || dragging
                                     ? NUIColor(0.11f, 0.11f, 0.14f, 0.96f)
                                     : NUIColor(0.09f, 0.09f, 0.11f, 0.90f));
        if (selected || dragging) {
            renderer.fillRoundedRect({pillRect.x + 1.0f, pillRect.y + 1.0f, pillRect.width - 2.0f, pillRect.height * 0.52f},
                                     6.0f, nodeColor.withAlpha(0.09f));
        }
        renderer.strokeRoundedRect(pillRect, 7.0f, 1.0f,
                                   nodeColor.withAlpha(selected || dragging ? 0.52f : 0.26f));
        renderer.drawText(band.name, {pillX + 5.0f, labelY + 3.0f}, 8.25f,
                          theme.getColor("textPrimary").withAlpha(selected || dragging ? 0.98f : 0.82f));
        renderer.drawText(freqLabel(band.freq), {pillX + 20.0f, labelY + 3.0f}, 8.25f,
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

        const std::string meta = std::string(glyphTypeLabel(band.type)) + "  " + freqLabel(band.freq);
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

    const float left = bounds.x + 40.0f;
    const float right = bounds.right() - 10.0f;
    const float top = bounds.y + 10.0f;
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

        const NUIColor glow(0.64f, 0.36f, 1.0f, 0.05f + mag * 0.09f);
        const NUIColor core(0.74f, 0.52f, 1.0f, 0.05f + mag * 0.08f);
        renderer.fillRect({x, y, barWidth, barHeight}, glow);
        renderer.fillRect({x, std::max(top, y + barHeight * 0.38f), barWidth, barHeight * 0.62f}, core);
    }
}

NUIRect AestraEQEditor::responseGraphBounds(const NUIRect& outerBounds) const {
    return {outerBounds.x + 40.0f, outerBounds.y + 10.0f,
            outerBounds.width - 50.0f, outerBounds.height - 30.0f};
}

bool AestraEQEditor::usesGainAxis(const BandControl& band) const {
    switch (static_cast<Aestra::Audio::Plugins::FilterType>(band.type)) {
    case Aestra::Audio::Plugins::FilterType::Bell:
    case Aestra::Audio::Plugins::FilterType::LowShelf:
    case Aestra::Audio::Plugins::FilterType::HighShelf:
    case Aestra::Audio::Plugins::FilterType::Tilt:
        return true;
    case Aestra::Audio::Plugins::FilterType::LowCut:
    case Aestra::Audio::Plugins::FilterType::HighCut:
    case Aestra::Audio::Plugins::FilterType::Notch:
    case Aestra::Audio::Plugins::FilterType::BandPass:
        return false;
    }
    return true;
}

NUIPoint AestraEQEditor::graphNodePosition(const BandControl& band, const NUIRect& graphBounds) const {
    const NUIRect safeBounds = graphNodeSafeBounds(graphBounds);
    const float x = safeBounds.x + band.freq * safeBounds.width;
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
    const float freqNorm = std::clamp((position.x - safeBounds.x) / std::max(1.0f, safeBounds.width), 0.0f, 1.0f);
    const float verticalNorm = std::clamp(1.0f - ((position.y - safeBounds.y) / std::max(1.0f, safeBounds.height)), 0.0f, 1.0f);

    band.freq = freqNorm;
    m_instance->setParameter(band.paramBase + 2, band.freq);
    if (usesGainAxis(band)) {
        band.gain = verticalNorm;
        m_instance->setParameter(band.paramBase + 3, band.gain);
    } else {
        band.q = usesDiscreteCutSlope(band.type) ? quantizeCutSlopeNorm(verticalNorm) : verticalNorm;
        m_instance->setParameter(band.paramBase + 4, band.q);
    }
    layoutControls();
    setDirty(true);
}

void AestraEQEditor::drawBandPanel(NUIRenderer& renderer, const BandControl& band) {
    auto& theme = NUIThemeManager::getInstance();
    const size_t bandIndex = static_cast<size_t>(&band - m_bands.data());
    NUIColor accent = bandColorForIndex(bandIndex);
    NUIColor cardColor = band.hovered || band.dragging ? NUIColor(0.14f, 0.12f, 0.16f, 0.98f)
                                                        : NUIColor(0.10f, 0.09f, 0.12f, 0.96f);

    renderer.fillRoundedRect(band.bounds, 8.0f, cardColor);
    if (band.enabled) {
        renderer.strokeRoundedRect(band.bounds, 8.0f, 1.0f,
                                   band.hovered || band.dragging ? accent.withAlpha(0.60f)
                                                                 : accent.withAlpha(0.25f));
    } else {
        renderer.strokeRoundedRect(band.bounds, 8.0f, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.04f));
    }

    renderer.drawText(band.name, {band.bounds.x + 6.0f, band.bounds.y + 4.0f}, 9.0f,
                      band.enabled ? theme.getColor("textPrimary") : theme.getColor("textSecondary").withAlpha(0.4f));

    if (band.enabled) {
        renderer.drawText(typeLabel(band.type), {band.bounds.x + 6.0f, band.bounds.y + 14.0f}, 8.5f,
                          accent.withAlpha(0.9f));
        renderer.drawText("Q", {band.bounds.center().x - 3.0f, band.bounds.y + 14.0f}, 8.5f,
                          theme.getColor("textSecondary").withAlpha(0.86f));
    }

    if (!band.enabled) return;

    renderer.fillRoundedRect({band.bounds.center().x - 1.0f, band.qSlider.y, 2.0f, band.qSlider.height}, 1.0f,
                             NUIColor(1.0f, 1.0f, 1.0f, 0.06f));
    renderer.fillRoundedRect(band.qSlider, 4.0f, NUIColor(0.02f, 0.02f, 0.03f, 0.7f));
    float qFill = band.q * band.qSlider.height;
    if (qFill > 0) {
        renderer.fillRoundedRect({band.qSlider.x, band.qSlider.y + band.qSlider.height - qFill,
                                  band.qSlider.width, qFill},
                                 3.0f, NUIColor(0.8f, 0.5f, 0.3f, 0.8f));
    }
    renderer.fillRoundedRect(band.qKnob, 7.0f, band.dragTarget == BandControl::Q ? NUIColor(1.0f, 1.0f, 1.0f, 1.0f) : NUIColor(0.9f, 0.7f, 0.5f, 0.95f));

    renderer.drawText(qLabel(band.q, band.type), {band.bounds.x + 6.0f, band.bounds.bottom() - 12.0f}, 8.0f,
                      theme.getColor("textSecondary").withAlpha(0.78f));
}

void AestraEQEditor::onRender(NUIRenderer& renderer) {
    updateSpectrumSnapshot();
    auto bounds = getBounds();
    renderer.fillRoundedRect(bounds, kRadius, NUIColor(0.07f, 0.07f, 0.08f, 0.97f));
    renderer.strokeRoundedRect(bounds, kRadius, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.10f));

    drawTitleBar(renderer);
    drawResponseCurve(renderer, {bounds.x + kPadding, bounds.y + kTitleHeight + 4.0f,
                                  bounds.width - kPadding * 2.0f, kCurveHeight});

    if (!m_bands.empty()) {
        const auto& first = m_bands.front();
        const auto& last = m_bands.back();
        renderer.fillRoundedRect(
            {first.bounds.x - 4.0f, first.bounds.y - 8.0f,
             last.bounds.right() - first.bounds.x + 8.0f, first.bounds.height + 16.0f},
            11.0f, NUIColor(0.10f, 0.11f, 0.16f, 0.72f));
    }

    for (const auto& band : m_bands) {
        drawBandPanel(renderer, band);
    }
}

int AestraEQEditor::hitTestBand(float x, float y) const {
    for (size_t i = 0; i < m_bands.size(); ++i) {
        if (m_bands[i].bounds.contains({x, y})) return static_cast<int>(i);
    }
    return -1;
}

bool AestraEQEditor::hitTestCloseButton(float x, float y) const {
    auto bounds = getBounds();
    NUIRect closeRect(bounds.right() - kCloseSize - 10.0f,
                      bounds.y + (kTitleHeight - kCloseSize) * 0.5f, kCloseSize, kCloseSize);
    return closeRect.contains({x, y});
}

bool AestraEQEditor::hitTestTitleBar(float x, float y) const {
    auto bounds = getBounds();
    NUIRect titleBar(bounds.x, bounds.y, bounds.width - 32.0f, kTitleHeight);
    return titleBar.contains({x, y});
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

    uint32_t pid = b.paramBase;
    switch (target) {
    case BandControl::Freq:
        b.freq = normalizedValue;
        m_instance->setParameter(pid + 2, b.freq);
        break;
    case BandControl::Gain:
        b.gain = normalizedValue;
        m_instance->setParameter(pid + 3, b.gain);
        break;
    case BandControl::Q:
        b.q = usesDiscreteCutSlope(b.type) ? quantizeCutSlopeNorm(normalizedValue) : normalizedValue;
        m_instance->setParameter(pid + 4, b.q);
        break;
    default: break;
    }
    layoutControls();
    setDirty(true);
}

std::string AestraEQEditor::typeLabel(uint32_t type) const {
    static const char* names[] = {"Bell", "LoCut", "HiCut", "LoShelf", "HiShelf", "Notch", "BP", "Tilt"};
    return type < 8 ? names[type] : "Bell";
}
std::string AestraEQEditor::freqLabel(float norm) const {
    float hz = std::pow(10.0f, std::log10(20.0f) + norm * (std::log10(20000.0f) - std::log10(20.0f)));
    if (hz >= 1000) { std::ostringstream o; o << std::fixed << std::setprecision(1) << hz / 1000.0f << "k"; return o.str(); }
    return std::to_string(static_cast<int>(hz));
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

    if (event.pressed && event.button == NUIMouseButton::Left && !contains && !m_isDraggingWindow && !isDraggingBand) {
        if (m_onClose) m_onClose();
        return false;
    }
    if (!contains && !m_isDraggingWindow && !isDraggingBand) return false;

    if (event.wheelDelta != 0.0f) {
        int graphBandIdx = hitTestGraphNode(event.position.x, event.position.y);
        if (graphBandIdx >= 0) {
            auto& band = m_bands[graphBandIdx];
            m_selectedBand = graphBandIdx;
            if (usesDiscreteCutSlope(band.type)) {
                const float currentIndex = std::round(std::clamp(band.q, 0.0f, 1.0f) * 4.0f);
                const float nextIndex = std::clamp(currentIndex + (event.wheelDelta > 0.0f ? 1.0f : -1.0f), 0.0f, 4.0f);
                band.q = nextIndex / 4.0f;
            } else {
                const float step = 0.03f;
                band.q = std::clamp(band.q + (event.wheelDelta > 0.0f ? step : -step), 0.0f, 1.0f);
            }
            if (m_instance) {
                m_instance->setParameter(band.paramBase + 4, band.q);
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
                    if (!m_instance || bandIdx < 0 || bandIdx >= static_cast<int>(m_bands.size())) {
                        return;
                    }
                    m_bands[bandIdx].type = type;
                    m_instance->setParameter(m_bands[bandIdx].paramBase + 1, static_cast<float>(type) / 7.0f);
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
        if (hitTestCloseButton(event.position.x, event.position.y)) {
            if (m_onClose) m_onClose();
            return true;
        }
        if (hitTestTitleBar(event.position.x, event.position.y)) {
            m_isDraggingWindow = true;
            m_dragStartPos = event.position;
            m_windowStartPos = {bounds.x, bounds.y};
            return true;
        }

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
                const NUIRect& sliderRect = (target == BandControl::Freq) ? band.freqSlider : (target == BandControl::Gain ? band.gainSlider : band.qSlider);
                float val = 1.0f - (event.position.y - sliderRect.y) / std::max(1.0f, sliderRect.height);
                updateBandValue(bandIdx, target, val);
                return true;
            }
            // Toggle enabled on click outside sliders
            if (!band.dragging) {
                m_instance->setParameter(band.paramBase, band.enabled ? 0.0f : 1.0f);
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

    if (m_isDraggingWindow && !event.pressed && event.button == NUIMouseButton::Left) {
        m_isDraggingWindow = false;
        return true;
    }
    if (m_isDraggingWindow) {
        float dx = event.position.x - m_dragStartPos.x;
        float dy = event.position.y - m_dragStartPos.y;
        setBounds(m_windowStartPos.x + dx, m_windowStartPos.y + dy, bounds.width, bounds.height);
        layoutControls();
        return true;
    }

    for (size_t i = 0; i < m_bands.size(); ++i) {
        auto& band = m_bands[i];
        if (!band.dragging) {
            continue;
        }

        const NUIRect& sliderRect = (band.dragTarget == BandControl::Freq)
            ? band.freqSlider
            : (band.dragTarget == BandControl::Gain ? band.gainSlider : band.qSlider);
        float val = 1.0f - (event.position.y - sliderRect.y) / std::max(1.0f, sliderRect.height);
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
                const NUIRect& sliderRect = (band.dragTarget == BandControl::Freq) ? band.freqSlider : (band.dragTarget == BandControl::Gain ? band.gainSlider : band.qSlider);
                float val = 1.0f - (event.position.y - sliderRect.y) / std::max(1.0f, sliderRect.height);
                updateBandValue(static_cast<int>(&band - m_bands.data()), band.dragTarget, val);
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
