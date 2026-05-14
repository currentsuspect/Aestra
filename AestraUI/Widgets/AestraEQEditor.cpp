// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraEQEditor.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "Plugin/AestraEQ.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace AestraUI {

namespace {
constexpr float kPi = 3.14159265358979323846f;

NUIColor accent() { return NUIColor(0.55f, 0.40f, 0.92f, 1.0f); }
NUIColor accentSoft() { return NUIColor(0.55f, 0.40f, 0.92f, 0.35f); }
NUIColor cardBg() { return NUIColor(0.085f, 0.080f, 0.115f, 0.95f); }
NUIColor graphBg() { return NUIColor(0.045f, 0.043f, 0.064f, 0.96f); }
NUIColor successCol() { return NUIColor(0.0f, 0.90f, 0.78f, 1.0f); }

NUIColor bandColor(size_t i) {
    static const NUIColor colors[] = {
        NUIColor(0.46f, 0.78f, 1.00f, 1.0f),  // HP — blue
        NUIColor(0.40f, 0.85f, 0.92f, 1.0f),  // LS — teal
        NUIColor(0.62f, 0.55f, 1.00f, 1.0f),  // B1 — violet
        NUIColor(0.85f, 0.55f, 1.00f, 1.0f),  // B2 — magenta
        NUIColor(0.98f, 0.70f, 0.42f, 1.0f),  // HS — amber
        NUIColor(0.95f, 0.50f, 0.55f, 1.0f),  // LP — coral
    };
    return colors[i % 6];
}

float bandFreqHz(size_t idx, float norm) {
    norm = std::clamp(norm, 0.0f, 1.0f);
    switch (idx) {
    case 0: return 20.0f   * std::pow(25.0f,  norm);  // HP   20-500
    case 1: return 40.0f   * std::pow(25.0f,  norm);  // LS   40-1k
    case 2: return 80.0f   * std::pow(100.0f, norm);  // B1   80-8k
    case 3: return 200.0f  * std::pow(80.0f,  norm);  // B2   200-16k
    case 4: return 2000.0f * std::pow(10.0f,  norm);  // HS   2k-20k
    case 5: return 1000.0f * std::pow(20.0f,  norm);  // LP   1k-20k
    default: return 1000.0f;
    }
}

float bandNormFromHz(size_t idx, float hz) {
    switch (idx) {
    case 0: return std::clamp(std::log10(hz / 20.0f)   / std::log10(25.0f),  0.0f, 1.0f);
    case 1: return std::clamp(std::log10(hz / 40.0f)   / std::log10(25.0f),  0.0f, 1.0f);
    case 2: return std::clamp(std::log10(hz / 80.0f)   / std::log10(100.0f), 0.0f, 1.0f);
    case 3: return std::clamp(std::log10(hz / 200.0f)  / std::log10(80.0f),  0.0f, 1.0f);
    case 4: return std::clamp(std::log10(hz / 2000.0f) / std::log10(10.0f),  0.0f, 1.0f);
    case 5: return std::clamp(std::log10(hz / 1000.0f) / std::log10(20.0f),  0.0f, 1.0f);
    default: return 0.5f;
    }
}

uint32_t slopeDbFromNorm(float norm) {
    static constexpr uint32_t db[] = {12, 24, 36, 48};
    const int idx = std::clamp(static_cast<int>(std::round(std::clamp(norm, 0.0f, 1.0f) * 3.0f)), 0, 3);
    return db[idx];
}

float quantizeSlopeNorm(float norm) {
    static constexpr float steps[] = {0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f};
    float best = steps[0];
    float bestD = std::abs(norm - steps[0]);
    for (int i = 1; i < 4; ++i) {
        float d = std::abs(norm - steps[i]);
        if (d < bestD) { bestD = d; best = steps[i]; }
    }
    return best;
}

std::vector<NUIPoint> smoothCurve(const std::vector<NUIPoint>& pts, int subdivisions) {
    if (pts.size() < 4 || subdivisions <= 1) return pts;
    std::vector<NUIPoint> result;
    result.reserve(pts.size() * static_cast<size_t>(subdivisions));
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const NUIPoint& p0 = pts[i == 0 ? i : i - 1];
        const NUIPoint& p1 = pts[i];
        const NUIPoint& p2 = pts[i + 1];
        const NUIPoint& p3 = pts[i + 2 < pts.size() ? i + 2 : i + 1];
        for (int s = 0; s < subdivisions; ++s) {
            const float t = static_cast<float>(s) / static_cast<float>(subdivisions);
            const float t2 = t * t;
            const float t3 = t2 * t;
            const float x = 0.5f * ((2.0f * p1.x) +
                                    (-p0.x + p2.x) * t +
                                    (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 +
                                    (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3);
            const float y = 0.5f * ((2.0f * p1.y) +
                                    (-p0.y + p2.y) * t +
                                    (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 +
                                    (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3);
            result.push_back({x, y});
        }
    }
    result.push_back(pts.back());
    return result;
}

float biquadDb(const Aestra::Audio::Plugins::FilterCoeffs& c, double omega) {
    const double c1 = std::cos(omega), s1 = std::sin(omega);
    const double c2 = std::cos(2.0 * omega), s2 = std::sin(2.0 * omega);
    const double nr = static_cast<double>(c.b0) + static_cast<double>(c.b1) * c1 + static_cast<double>(c.b2) * c2;
    const double ni = -static_cast<double>(c.b1) * s1 - static_cast<double>(c.b2) * s2;
    const double dr = static_cast<double>(c.a0) + static_cast<double>(c.a1) * c1 + static_cast<double>(c.a2) * c2;
    const double di = -static_cast<double>(c.a1) * s1 - static_cast<double>(c.a2) * s2;
    const double nm = std::hypot(nr, ni);
    const double dm = std::max(std::hypot(dr, di), 1.0e-12);
    return static_cast<float>(20.0 * std::log10(std::max(nm / dm, 1.0e-12)));
}
} // namespace

AestraEQEditor::AestraEQEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraEQEditor");
    setPanelTitle("Aestra EQ");
    setSize(kWinW, kWinH);
    setEnforceParentBounds(true);
    buildBands();
    m_spectrumWorker = std::thread([this]() { analyzerWorkerMain(); });
}

AestraEQEditor::~AestraEQEditor() {
    {
        std::lock_guard<std::mutex> lock(m_spectrumMutex);
        m_spectrumStop = true;
    }
    m_spectrumCv.notify_all();
    if (m_spectrumWorker.joinable()) m_spectrumWorker.join();
}

void AestraEQEditor::buildBands() {
    m_bands.clear();
    if (!m_instance) return;
    using EQ = Aestra::Audio::Plugins::AestraEQ;
    static constexpr uint32_t enableIds[] = {EQ::kParamHPFEnable, EQ::kParamLShEnable, EQ::kParamBell1Enable, EQ::kParamBell2Enable, EQ::kParamHShEnable, EQ::kParamLPFEnable};
    static constexpr uint32_t freqIds[]   = {EQ::kParamHPFFreq,   EQ::kParamLShFreq,   EQ::kParamBell1Freq,   EQ::kParamBell2Freq,   EQ::kParamHShFreq,   EQ::kParamLPFFreq};
    static constexpr uint32_t gainIds[]   = {0, EQ::kParamLShGain, EQ::kParamBell1Gain, EQ::kParamBell2Gain, EQ::kParamHShGain, 0};
    static constexpr uint32_t qIds[]      = {EQ::kParamHPFSlope, EQ::kParamLShQ, EQ::kParamBell1Q, EQ::kParamBell2Q, EQ::kParamHShQ, EQ::kParamLPFSlope};
    static const char* names[]            = {"HP", "LS", "B1", "B2", "HS", "LP"};
    static const char* typeNames[]        = {"HP \u00B7 Cut", "LS \u00B7 Shelf", "B1 \u00B7 Bell", "B2 \u00B7 Bell", "HS \u00B7 Shelf", "LP \u00B7 Cut"};

    for (size_t i = 0; i < kNumBands; ++i) {
        Band b;
        b.enableId = enableIds[i];
        b.freqId = freqIds[i];
        b.gainId = gainIds[i];
        b.qId = qIds[i];
        b.name = names[i];
        b.typeName = typeNames[i];
        b.usesGain = (gainIds[i] != 0);
        b.usesSlope = (i == 0 || i == 5);
        b.enabled = m_instance->getParameter(b.enableId) > 0.5f;
        b.freq = m_instance->getParameter(b.freqId);
        b.gain = b.usesGain ? m_instance->getParameter(b.gainId) : 0.5f;
        b.q = m_instance->getParameter(b.qId);
        m_bands.push_back(std::move(b));
    }
    layoutControls();
}

void AestraEQEditor::syncBandsFromPlugin() {
    if (!m_instance) return;
    for (auto& b : m_bands) {
        b.enabled = m_instance->getParameter(b.enableId) > 0.5f;
        b.freq = m_instance->getParameter(b.freqId);
        if (b.usesGain) b.gain = m_instance->getParameter(b.gainId);
        b.q = m_instance->getParameter(b.qId);
    }
}

void AestraEQEditor::layoutControls() {
    const auto b = getBounds();
    const float contentX = b.x + kPad;
    const float contentW = b.width - kPad * 2.0f;

    constexpr float kBypassW = 80.0f;
    constexpr float kBypassH = 24.0f;
    constexpr float kBypassRightPad = 44.0f;
    m_bypassRect = NUIRect(b.right() - kBypassRightPad - kBypassW,
                           b.y + AestraPanelWindow::TITLE_BAR_H + 4.0f,
                           kBypassW, kBypassH);

    const float graphY = b.y + AestraPanelWindow::TITLE_BAR_H + 36.0f;
    m_graphBounds = NUIRect(contentX, graphY, contentW, kCurveH);

    const float cardGap = 8.0f;
    const float rowY = m_graphBounds.bottom() + 14.0f;
    const float rowH = b.y + b.height - rowY - kPad;
    const float cardW = (contentW - cardGap * static_cast<float>(kNumBands - 1)) / static_cast<float>(kNumBands);

    for (size_t i = 0; i < m_bands.size(); ++i) {
        auto& bd = m_bands[i];
        const float x = contentX + static_cast<float>(i) * (cardW + cardGap);
        bd.cardBounds = NUIRect(x, rowY, cardW, rowH);

        const float knobSize = 30.0f;
        const float bodyTop = rowY + 32.0f;
        const float colCx = x + cardW * 0.5f;
        const float rowGap = bd.usesSlope ? 24.0f : 20.0f;
        bd.freqKnob = NUIRect(colCx - knobSize * 0.5f, bodyTop, knobSize, knobSize);
        bd.gainKnob = NUIRect(colCx - knobSize * 0.5f, bodyTop + knobSize + rowGap, knobSize, knobSize);
        if (bd.usesSlope) {
            bd.qKnob = NUIRect();
        } else {
            bd.qKnob = NUIRect(colCx - knobSize * 0.5f, bodyTop + (knobSize + rowGap) * 2.0f, knobSize, knobSize);
        }
    }
}

bool AestraEQEditor::isBypassed() const {
    using EQ = Aestra::Audio::Plugins::AestraEQ;
    return m_instance && m_instance->getParameter(EQ::kParamBypass) > 0.5f;
}

void AestraEQEditor::setBypassed(bool bypassed) {
    using EQ = Aestra::Audio::Plugins::AestraEQ;
    if (m_instance) m_instance->setParameter(EQ::kParamBypass, bypassed ? 1.0f : 0.0f);
    setDirty(true);
}

void AestraEQEditor::drawBypassPill(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const bool bypassed = isBypassed();
    constexpr float kFont = 10.0f;
    if (bypassed) {
        renderer.fillRoundedRect(m_bypassRect, 7.0f, NUIColor(0.92f, 0.28f, 0.22f).withAlpha(m_bypassHovered ? 0.94f : 0.78f));
        renderer.strokeRoundedRect(m_bypassRect, 7.0f, 1.0f, NUIColor(0.92f, 0.28f, 0.22f).withAlpha(0.50f));
        renderer.drawTextCentered("BYPASSED", m_bypassRect, kFont, theme.getColor("textPrimary"));
    } else {
        renderer.fillRoundedRect(m_bypassRect, 7.0f, theme.getColor("success").withAlpha(m_bypassHovered ? 0.30f : 0.18f));
        renderer.strokeRoundedRect(m_bypassRect, 7.0f, 1.0f, theme.getColor("success").withAlpha(0.40f));
        renderer.drawTextCentered("ACTIVE", m_bypassRect, kFont, theme.getColor("success"));
    }
}

void AestraEQEditor::drawKnob(NUIRenderer& renderer, const NUIRect& bounds, float value, bool active, const NUIColor& a) {
    const float cx = bounds.center().x;
    const float cy = bounds.center().y;
    const float r = bounds.width * 0.42f;
    const NUIColor col = active ? a : NUIColor(a.r, a.g, a.b, 0.30f);
    renderer.fillCircle({cx, cy}, r + 5.0f, col.withAlpha(active ? 0.08f : 0.04f));
    renderer.fillCircle({cx, cy}, r, NUIColor(0.045f, 0.043f, 0.060f, 0.96f));
    renderer.strokeCircle({cx, cy}, r, 1.0f, col.withAlpha(active ? 0.34f : 0.18f));

    const float sa = kPi * 0.75f;
    const float ea = sa + std::clamp(value, 0.0f, 1.0f) * kPi * 1.5f;
    constexpr int kSegments = 26;
    for (int i = 0; i < kSegments; ++i) {
        const float a1 = sa + (ea - sa) * static_cast<float>(i) / static_cast<float>(kSegments);
        const float a2 = sa + (ea - sa) * static_cast<float>(i + 1) / static_cast<float>(kSegments);
        renderer.drawLine({cx + std::cos(a1) * (r - 3.0f), cy + std::sin(a1) * (r - 3.0f)},
                          {cx + std::cos(a2) * (r - 3.0f), cy + std::sin(a2) * (r - 3.0f)},
                          2.4f, col.withAlpha(active ? 0.92f : 0.40f));
    }
    const float pa = sa + std::clamp(value, 0.0f, 1.0f) * kPi * 1.5f;
    renderer.fillCircle({cx + std::cos(pa) * (r - 6.0f), cy + std::sin(pa) * (r - 6.0f)}, 2.2f, col);
}

void AestraEQEditor::drawBandCard(NUIRenderer& renderer, size_t idx) {
    auto& theme = NUIThemeManager::getInstance();
    const auto& bd = m_bands[idx];
    const NUIColor band = bandColor(idx);
    const bool hot = static_cast<int>(idx) == m_hoveredBand || static_cast<int>(idx) == m_selectedBand;

    renderer.fillRoundedRect(bd.cardBounds, 10.0f, cardBg());
    renderer.strokeRoundedRect(bd.cardBounds, 10.0f, hot ? 1.4f : 1.0f,
                               bd.enabled ? band.withAlpha(hot ? 0.55f : 0.30f)
                                          : NUIColor(1, 1, 1, 0.08f));

    // Top color strip
    renderer.fillRoundedRect({bd.cardBounds.x, bd.cardBounds.y, bd.cardBounds.width, 3.0f}, 1.5f,
                             bd.enabled ? band.withAlpha(0.85f) : band.withAlpha(0.16f));

    // Enable LED + inline name·role label
    const float ledX = bd.cardBounds.x + 10.0f;
    const float ledY = bd.cardBounds.y + 16.0f;
    renderer.fillCircle({ledX, ledY}, 3.5f, bd.enabled ? band : NUIColor(0.30f, 0.30f, 0.34f, 0.80f));
    renderer.drawText(bd.typeName, {ledX + 9.0f, bd.cardBounds.y + 10.0f}, 10.0f,
                      bd.enabled ? theme.getColor("textPrimary").withAlpha(0.94f)
                                 : theme.getColor("textSecondary").withAlpha(0.46f));

    auto labelColor = bd.enabled ? theme.getColor("textSecondary").withAlpha(0.78f)
                                 : theme.getColor("textSecondary").withAlpha(0.36f);
    auto valueColor = bd.enabled ? band.withAlpha(0.96f) : band.withAlpha(0.42f);

    // Freq knob
    drawKnob(renderer, bd.freqKnob, bd.freq, bd.enabled, band);
    renderer.drawText("FREQ", {bd.freqKnob.x - 2.0f, bd.freqKnob.bottom() + 2.0f}, 8.0f, labelColor);
    renderer.drawText(formatFreq(idx, bd.freq), {bd.freqKnob.x - 6.0f, bd.freqKnob.bottom() + 12.0f}, 9.0f, valueColor);

    if (bd.usesSlope) {
        // Slope knob (4-step quantized)
        drawKnob(renderer, bd.gainKnob, quantizeSlopeNorm(bd.q), bd.enabled, band);
        renderer.drawText("SLOPE", {bd.gainKnob.x - 4.0f, bd.gainKnob.bottom() + 2.0f}, 8.0f, labelColor);
        renderer.drawText(formatSlope(bd.q), {bd.gainKnob.x - 6.0f, bd.gainKnob.bottom() + 12.0f}, 9.0f, valueColor);
    } else {
        // Gain knob
        drawKnob(renderer, bd.gainKnob, bd.gain, bd.enabled, band);
        renderer.drawText("GAIN", {bd.gainKnob.x - 2.0f, bd.gainKnob.bottom() + 2.0f}, 8.0f, labelColor);
        renderer.drawText(formatGain(bd.gain), {bd.gainKnob.x - 8.0f, bd.gainKnob.bottom() + 12.0f}, 9.0f, valueColor);

        // Q knob
        drawKnob(renderer, bd.qKnob, bd.q, bd.enabled, band);
        renderer.drawText("Q", {bd.qKnob.x + 8.0f, bd.qKnob.bottom() + 2.0f}, 8.0f, labelColor);
        renderer.drawText(formatQ(bd.q), {bd.qKnob.x - 2.0f, bd.qKnob.bottom() + 12.0f}, 9.0f, valueColor);
    }
}

NUIRect AestraEQEditor::graphInnerBounds(const NUIRect& outer) const {
    return {outer.x + 38.0f, outer.y + 18.0f, outer.width - 48.0f, outer.height - 36.0f};
}

NUIPoint AestraEQEditor::graphNodePosition(size_t bandIdx, const NUIRect& graphBounds) const {
    const auto inner = graphInnerBounds(graphBounds);
    const auto& bd = m_bands[bandIdx];
    const float hz = bandFreqHz(bandIdx, bd.freq);
    const float logMin = std::log10(20.0f), logMax = std::log10(20000.0f);
    const float xnorm = (std::log10(std::max(hz, 20.0f)) - logMin) / (logMax - logMin);
    const float x = inner.x + std::clamp(xnorm, 0.0f, 1.0f) * inner.width;
    const float y = bd.usesGain
        ? (inner.bottom() - bd.gain * inner.height)
        : (inner.y + inner.height * 0.5f);
    return {x, y};
}

void AestraEQEditor::drawSpectrumBackdrop(NUIRenderer& renderer, const NUIRect& bounds) {
    if (m_lastAnalyzerSerial == 0) return;
    const auto inner = graphInnerBounds(bounds);
    const size_t n = m_spectrumMagnitudes.size();
    const float div = n > 1 ? static_cast<float>(n - 1) : 1.0f;
    for (size_t i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / div;
        const float x = inner.x + t * inner.width;
        const float nextT = static_cast<float>(i + 1) / div;
        const float nextX = i + 1 < n ? inner.x + nextT * inner.width : inner.right();
        const float bw = std::max(1.0f, nextX - x);
        const float mag = std::clamp(m_spectrumMagnitudes[i], 0.0f, 1.0f);
        const float bh = mag * inner.height;
        renderer.fillRect({x, inner.bottom() - bh, bw, bh}, accent().withAlpha(0.04f + mag * 0.10f));
    }
}

void AestraEQEditor::drawResponseCurve(NUIRenderer& renderer, const NUIRect& bounds) {
    auto& theme = NUIThemeManager::getInstance();

    renderer.fillRoundedRect(bounds, 10.0f, graphBg());
    renderer.strokeRoundedRect(bounds, 10.0f, 1.0f, accentSoft());

    drawSpectrumBackdrop(renderer, bounds);
    const auto inner = graphInnerBounds(bounds);
    m_lastGraphInner = inner;

    constexpr float dbRange = 18.0f;
    for (int db = -18; db <= 18; db += 6) {
        const float y = inner.bottom() - (static_cast<float>(db) + dbRange) / (dbRange * 2.0f) * inner.height;
        const float alpha = (db == 0) ? 0.28f : 0.10f;
        renderer.drawLine({inner.x, y}, {inner.right(), y}, db == 0 ? 1.2f : 1.0f,
                          NUIColor(1.0f, 1.0f, 1.0f, alpha));
        const std::string lbl = (db > 0 ? "+" : "") + std::to_string(db);
        renderer.drawText(lbl, {bounds.x + 6.0f, y - 5.0f}, 8.0f,
                          theme.getColor("textSecondary").withAlpha(0.62f));
    }

    const float freqs[] = {30, 100, 300, 1000, 3000, 10000};
    const char* freqLabels[] = {"30", "100", "300", "1k", "3k", "10k"};
    const float logMin = std::log10(20.0f), logMax = std::log10(20000.0f);
    for (int i = 0; i < 6; ++i) {
        const float norm = (std::log10(freqs[i]) - logMin) / (logMax - logMin);
        const float x = inner.x + norm * inner.width;
        renderer.drawLine({x, inner.y}, {x, inner.bottom()}, 1.0f,
                          NUIColor(1.0f, 1.0f, 1.0f, 0.07f));
        renderer.drawText(freqLabels[i], {x - 8.0f, inner.bottom() + 6.0f}, 8.0f,
                          theme.getColor("textSecondary").withAlpha(0.66f));
    }

    auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
    const float sampleRate = static_cast<float>(std::max(1.0, eq ? eq->getAnalyzerSampleRate() : 48000.0));
    static constexpr Aestra::Audio::Plugins::FilterType kTypes[] = {
        Aestra::Audio::Plugins::FilterType::LowCut,
        Aestra::Audio::Plugins::FilterType::LowShelf,
        Aestra::Audio::Plugins::FilterType::Bell,
        Aestra::Audio::Plugins::FilterType::Bell,
        Aestra::Audio::Plugins::FilterType::HighShelf,
        Aestra::Audio::Plugins::FilterType::HighCut
    };

    constexpr int kNumPoints = 600;
    std::vector<float> response(kNumPoints, 0.0f);
    for (int p = 0; p < kNumPoints; ++p) {
        const float t = static_cast<float>(p) / static_cast<float>(kNumPoints - 1);
        const float hz = std::pow(10.0f, logMin + t * (logMax - logMin));
        const double omega = 2.0 * static_cast<double>(kPi) * static_cast<double>(hz) / static_cast<double>(sampleRate);
        for (size_t bi = 0; bi < m_bands.size(); ++bi) {
            const auto& bd = m_bands[bi];
            if (!bd.enabled) continue;
            float f0 = std::clamp(bandFreqHz(bi, bd.freq), 20.0f, sampleRate * 0.49f);
            float gainDb = bd.usesGain ? (-18.0f + bd.gain * 36.0f) : 0.0f;
            float Q = bd.usesSlope ? 0.70710678f : (0.1f + bd.q * 9.9f);
            Q = std::clamp(Q, 0.1f, 10.0f);
            const auto coeffs = Aestra::Audio::Plugins::designBiquad(kTypes[bi], f0, gainDb, Q, sampleRate);
            float r = biquadDb(coeffs, omega);
            if (bd.usesSlope) {
                uint32_t stages = slopeDbFromNorm(bd.q) / 12u;
                r *= static_cast<float>(stages);
                r = std::min(r, 0.0f);
            }
            response[p] += r;
        }
    }

    std::vector<NUIPoint> pts;
    pts.reserve(kNumPoints);
    for (int p = 0; p < kNumPoints; ++p) {
        const float t = static_cast<float>(p) / static_cast<float>(kNumPoints - 1);
        const float x = inner.x + t * inner.width;
        const float y = inner.bottom() - std::clamp((response[p] + dbRange) / (dbRange * 2.0f), 0.0f, 1.0f) * inner.height;
        pts.push_back({x, y});
    }
    auto smooth = smoothCurve(pts, 4);

    const NUIColor curveCol(0.78f, 0.62f, 1.0f, 0.95f);
    renderer.drawPolyline(smooth.data(), static_cast<int>(smooth.size()), 6.0f, curveCol.withAlpha(0.10f));
    renderer.drawPolyline(smooth.data(), static_cast<int>(smooth.size()), 3.0f, curveCol.withAlpha(0.30f));
    renderer.drawPolyline(smooth.data(), static_cast<int>(smooth.size()), 1.6f, curveCol);

    // Nodes
    for (size_t i = 0; i < m_bands.size(); ++i) {
        const auto& bd = m_bands[i];
        if (!bd.enabled) continue;
        const bool selected = static_cast<int>(i) == m_selectedBand;
        const bool hovered = static_cast<int>(i) == m_hoveredBand;
        const bool dragging = static_cast<int>(i) == m_draggingGraphBand;
        const NUIPoint node = graphNodePosition(i, bounds);
        const float radius = dragging ? 8.0f : (selected || hovered ? 7.0f : 5.5f);
        const NUIColor c = bandColor(i);

        renderer.fillCircle(node, radius + 4.0f, c.withAlpha(selected || dragging ? 0.18f : 0.10f));
        renderer.fillCircle(node, radius, NUIColor(0.045f, 0.043f, 0.060f, 0.96f));
        renderer.strokeCircle(node, radius, 1.6f, c);
        renderer.fillCircle(node, 2.0f, c);
        renderer.drawText(bd.name, {node.x + radius + 4.0f, node.y - 5.0f}, 8.5f,
                          c.withAlpha(selected || hovered || dragging ? 0.96f : 0.74f));
    }
}

void AestraEQEditor::drawContent(NUIRenderer& renderer, const NUIRect& /*contentRect*/) {
    updateSpectrumSnapshot();
    syncBandsFromPlugin();
    // Recompute layout each frame so cached absolute rects follow window drag.
    layoutControls();

    drawBypassPill(renderer);
    drawResponseCurve(renderer, m_graphBounds);
    for (size_t i = 0; i < m_bands.size(); ++i) {
        drawBandCard(renderer, i);
    }
}

// ---- Spectrum worker ----
void AestraEQEditor::updateSpectrumSnapshot() {
    std::array<float, 160> next{};
    bool ready = false;
    {
        std::lock_guard<std::mutex> lock(m_spectrumMutex);
        if (m_spectrumResultReady) {
            next = m_workerResultMagnitudes;
            m_lastAnalyzerSerial = m_workerResultSerial;
            m_pendingAnalyzerSerial = 0;
            m_spectrumResultReady = false;
            ready = true;
        }
    }
    if (ready) {
        for (size_t i = 0; i < m_spectrumMagnitudes.size(); ++i) {
            const float cur = m_spectrumMagnitudes[i];
            const float tgt = next[i];
            if (tgt >= cur) m_spectrumMagnitudes[i] = cur + (tgt - cur) * 0.55f;
            else m_spectrumMagnitudes[i] = cur * 0.92f + tgt * 0.08f;
        }
    }
    auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
    if (!eq) return;
    uint64_t serial = 0;
    if (!eq->getAnalyzerWindow(m_analyzerWindow, &serial) ||
        serial == m_lastAnalyzerSerial || m_pendingAnalyzerSerial != 0) return;
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
        std::array<float, Aestra::Audio::Plugins::AestraEQ::kAnalyzerWindowSize> win{};
        uint64_t serial = 0;
        {
            std::unique_lock<std::mutex> lock(m_spectrumMutex);
            m_spectrumCv.wait(lock, [this]() { return m_spectrumStop || m_spectrumWorkPending; });
            if (m_spectrumStop) return;
            win = m_workerAnalyzerWindow;
            serial = m_workerRequestedSerial;
            m_spectrumWorkPending = false;
        }
        auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
        const double sr = eq ? std::max(1.0, eq->getAnalyzerSampleRate()) : 48000.0;
        constexpr size_t kBins = 160;
        std::array<float, kBins> mags{};
        std::array<float, Aestra::Audio::Plugins::AestraEQ::kAnalyzerWindowSize> w{};
        const size_t N = win.size();
        const float div = N > 1 ? static_cast<float>(N - 1) : 1.0f;
        for (size_t n = 0; n < N; ++n) {
            const float h = 0.5f - 0.5f * std::cos(2.0f * kPi * static_cast<float>(n) / div);
            w[n] = win[n] * h;
        }
        const float logMin = std::log10(20.0f), logMax = std::log10(20000.0f);
        for (size_t bin = 0; bin < kBins; ++bin) {
            const float t = static_cast<float>(bin) / static_cast<float>(kBins - 1);
            const float hz = std::pow(10.0f, logMin + t * (logMax - logMin));
            const float omega = 2.0f * kPi * hz / static_cast<float>(sr);
            const float cs = std::cos(omega), sn = std::sin(omega);
            const float coeff = 2.0f * cs;
            float q1 = 0, q2 = 0;
            for (size_t n = 0; n < N; ++n) {
                const float q0 = coeff * q1 - q2 + w[n];
                q2 = q1; q1 = q0;
            }
            const float re = q1 - q2 * cs;
            const float im = q2 * sn;
            const float mag = std::sqrt(re * re + im * im) / static_cast<float>(N);
            const float db = 20.0f * std::log10(std::max(mag * 8.0f, 1.0e-5f));
            mags[bin] = std::clamp((db + 72.0f) / 72.0f, 0.0f, 1.0f);
        }
        {
            std::lock_guard<std::mutex> lock(m_spectrumMutex);
            m_workerResultMagnitudes = mags;
            m_workerResultSerial = serial;
            m_spectrumResultReady = true;
        }
    }
}

// ---- Formatters ----
std::string AestraEQEditor::formatFreq(size_t idx, float norm) const {
    const float hz = bandFreqHz(idx, norm);
    char buf[32];
    if (hz >= 1000.0f) std::snprintf(buf, sizeof(buf), "%.2fk", hz / 1000.0f);
    else std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(hz + 0.5f));
    return buf;
}
std::string AestraEQEditor::formatGain(float norm) const {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%+.1fdB", -18.0f + norm * 36.0f);
    return buf;
}
std::string AestraEQEditor::formatQ(float norm) const {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.2f", 0.1f + norm * 9.9f);
    return buf;
}
std::string AestraEQEditor::formatSlope(float norm) const {
    return std::to_string(slopeDbFromNorm(norm)) + "dB";
}

// ---- Interaction ----
int AestraEQEditor::hitTestGraphNode(float x, float y) const {
    if (!m_graphBounds.contains({x, y})) return -1;
    int best = -1;
    float bestD = 16.0f;
    for (size_t i = 0; i < m_bands.size(); ++i) {
        if (!m_bands[i].enabled) continue;
        const NUIPoint n = graphNodePosition(i, m_graphBounds);
        const float d = std::hypot(n.x - x, n.y - y);
        if (d <= bestD) { bestD = d; best = static_cast<int>(i); }
    }
    return best;
}

void AestraEQEditor::updateBandFromGraphPosition(int idx, const NUIPoint& p) {
    if (idx < 0 || idx >= static_cast<int>(m_bands.size()) || !m_instance) return;
    const auto inner = graphInnerBounds(m_graphBounds);
    const float xn = std::clamp((p.x - inner.x) / std::max(1.0f, inner.width), 0.0f, 1.0f);
    const float yn = std::clamp(1.0f - (p.y - inner.y) / std::max(1.0f, inner.height), 0.0f, 1.0f);
    const float logMin = std::log10(20.0f), logMax = std::log10(20000.0f);
    const float hz = std::pow(10.0f, logMin + xn * (logMax - logMin));
    auto& bd = m_bands[idx];
    bd.freq = bandNormFromHz(idx, hz);
    m_instance->setParameter(bd.freqId, bd.freq);
    if (bd.usesGain) {
        bd.gain = yn;
        m_instance->setParameter(bd.gainId, bd.gain);
    }
    setDirty(true);
}

int AestraEQEditor::hitTestBandCard(float x, float y, Knob& outKnob) const {
    for (size_t i = 0; i < m_bands.size(); ++i) {
        const auto& bd = m_bands[i];
        if (!bd.cardBounds.contains({x, y})) continue;
        if (bd.freqKnob.contains({x, y})) { outKnob = Knob::Freq; return static_cast<int>(i); }
        if (bd.gainKnob.contains({x, y})) { outKnob = Knob::Gain; return static_cast<int>(i); }
        if (!bd.usesSlope && bd.qKnob.contains({x, y})) { outKnob = Knob::Q; return static_cast<int>(i); }
        outKnob = Knob::None;
        return static_cast<int>(i);
    }
    outKnob = Knob::None;
    return -1;
}

void AestraEQEditor::setBandValue(int idx, Knob target, float v) {
    if (idx < 0 || idx >= static_cast<int>(m_bands.size()) || !m_instance) return;
    auto& bd = m_bands[idx];
    v = std::clamp(v, 0.0f, 1.0f);
    switch (target) {
    case Knob::Freq:
        bd.freq = v; m_instance->setParameter(bd.freqId, v); break;
    case Knob::Gain:
        if (bd.usesGain) {
            bd.gain = v; m_instance->setParameter(bd.gainId, v);
        } else if (bd.usesSlope) {
            bd.q = quantizeSlopeNorm(v);
            m_instance->setParameter(bd.qId, bd.q);
        }
        break;
    case Knob::Q:
        if (!bd.usesSlope) {
            bd.q = v; m_instance->setParameter(bd.qId, v);
        }
        break;
    default: break;
    }
    setDirty(true);
}

bool AestraEQEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;
    if (AestraPanelWindow::onMouseEvent(event)) return true;

    const auto b = getBounds();
    const bool contains = b.contains(event.position);
    const bool dragging = m_draggingGraphBand >= 0 || m_draggingCardBand >= 0;
    if (!contains && !isDraggingWindow() && !dragging) return false;

    // Bypass click
    if (event.pressed && event.button == NUIMouseButton::Left && m_bypassRect.contains(event.position)) {
        setBypassed(!isBypassed());
        return true;
    }

    // Wheel on graph nodes adjusts Q/slope
    if (event.wheelDelta != 0.0f) {
        int idx = hitTestGraphNode(event.position.x, event.position.y);
        if (idx >= 0) {
            auto& bd = m_bands[idx];
            m_selectedBand = idx;
            if (bd.usesSlope) {
                const float cur = std::round(quantizeSlopeNorm(bd.q) * 3.0f);
                const float nx = std::clamp(cur + (event.wheelDelta > 0 ? 1.0f : -1.0f), 0.0f, 3.0f);
                bd.q = nx / 3.0f;
            } else {
                bd.q = std::clamp(bd.q + (event.wheelDelta > 0 ? 0.04f : -0.04f), 0.0f, 1.0f);
            }
            if (m_instance) m_instance->setParameter(bd.qId, bd.q);
            setDirty(true);
            return true;
        }
    }

    // Press
    if (event.pressed && event.button == NUIMouseButton::Left) {
        int idx = hitTestGraphNode(event.position.x, event.position.y);
        if (idx >= 0) {
            m_selectedBand = idx;
            m_draggingGraphBand = idx;
            updateBandFromGraphPosition(idx, event.position);
            return true;
        }
        Knob knob = Knob::None;
        int cardIdx = hitTestBandCard(event.position.x, event.position.y, knob);
        if (cardIdx >= 0) {
            m_selectedBand = cardIdx;
            if (knob == Knob::None) {
                auto& bd = m_bands[cardIdx];
                const bool nowEnabled = !bd.enabled;
                bd.enabled = nowEnabled;
                if (m_instance) m_instance->setParameter(bd.enableId, nowEnabled ? 1.0f : 0.0f);
                setDirty(true);
                return true;
            }
            m_draggingCardBand = cardIdx;
            m_draggingKnob = knob;
            m_dragStartY = event.position.y;
            const auto& bd = m_bands[cardIdx];
            m_dragStartValue = (knob == Knob::Freq) ? bd.freq
                              : (knob == Knob::Gain) ? (bd.usesSlope ? bd.q : bd.gain)
                              : bd.q;
            return true;
        }
    }

    // Drag
    if (m_draggingGraphBand >= 0) {
        updateBandFromGraphPosition(m_draggingGraphBand, event.position);
        if (!event.pressed && event.button == NUIMouseButton::Left) m_draggingGraphBand = -1;
        return true;
    }
    if (m_draggingCardBand >= 0) {
        const float v = m_dragStartValue + (m_dragStartY - event.position.y) * 0.008f;
        setBandValue(m_draggingCardBand, m_draggingKnob, v);
        if (!event.pressed && event.button == NUIMouseButton::Left) {
            m_draggingCardBand = -1;
            m_draggingKnob = Knob::None;
        }
        return true;
    }

    // Hover
    if (!event.pressed && !event.released) {
        Knob k = Knob::None;
        int idx = hitTestGraphNode(event.position.x, event.position.y);
        if (idx < 0 && contains) idx = hitTestBandCard(event.position.x, event.position.y, k);
        if (idx != m_hoveredBand) {
            m_hoveredBand = idx;
            setDirty(true);
        }
        const bool hover = m_bypassRect.contains(event.position);
        if (hover != m_bypassHovered) {
            m_bypassHovered = hover;
            setDirty(true);
        }
    }

    return contains;
}

} // namespace AestraUI
