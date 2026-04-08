// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraEQEditor.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace AestraUI {

namespace {
constexpr float kCloseSize = 16.0f;
constexpr float kRadius = 12.0f;
}

AestraEQEditor::AestraEQEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraEQEditor");
    setSize(kWindowWidth, kWindowHeight);
    buildControls();
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

        // Vertical sliders
        float sliderW = 6.0f;
        float knobSize = 12.0f;

        // Freq slider (left)
        b.freqSlider = NUIRect(x + 6.0f, y + 20.0f, sliderW, h - 40.0f);
        b.freqKnob = NUIRect(x + 6.0f - (knobSize - sliderW) * 0.5f,
                             y + 20.0f + (1.0f - b.freq) * (h - 40.0f) - knobSize * 0.5f,
                             knobSize, knobSize);

        // Gain slider (center)
        float cx = x + bandW * 0.5f;
        b.gainSlider = NUIRect(cx - sliderW * 0.5f, y + 20.0f, sliderW, h - 40.0f);
        b.gainKnob = NUIRect(cx - knobSize * 0.5f,
                             y + 20.0f + (1.0f - b.gain) * (h - 40.0f) - knobSize * 0.5f,
                             knobSize, knobSize);

        // Q slider (right)
        b.qSlider = NUIRect(x + bandW - 6.0f - sliderW, y + 20.0f, sliderW, h - 40.0f);
        b.qKnob = NUIRect(x + bandW - 6.0f - sliderW - (knobSize - sliderW) * 0.5f,
                          y + 20.0f + (1.0f - b.q) * (h - 40.0f) - knobSize * 0.5f,
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
    renderer.drawText("8-band parametric", {titleBar.x + 110.0f, titleBar.y + 15.0f}, 10.0f,
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

    // Compute approximate response curve
    constexpr float pi = 3.14159265358979323846f;
    auto freqToHz = [](float norm) {
        float logMin = std::log10(20.0f), logMax = std::log10(20000.0f);
        return std::pow(10.0f, logMin + norm * (logMax - logMin));
    };
    auto gainToDb = [](float norm) { return -18.0f + norm * 36.0f; };
    auto qToLinear = [](float norm) { return 0.1f + norm * 9.9f; };

    // Compute total response at 500 frequency points
    const int numPoints = 500;
    std::vector<float> responseDb(numPoints, 0.0f);
    for (int p = 0; p < numPoints; ++p) {
        float freqNorm = static_cast<float>(p) / (numPoints - 1);
        float freq = freqToHz(freqNorm);
        for (const auto& band : m_bands) {
            if (!band.enabled) continue;
            float f0 = freqToHz(band.freq);
            float gainDb = gainToDb(band.gain);
            float Q = qToLinear(band.q);
            float w0 = 2.0f * pi * f0 / 48000.0f;
            float w = 2.0f * pi * freq / 48000.0f;
            float A = std::pow(10.0f, gainDb / 40.0f);
            float alpha = std::sin(w0) / (2.0f * Q);

            // Approximate magnitude response of biquad
            float numRe = 1.0f, numIm = 0.0f, denRe = 1.0f, denIm = 0.0f;
            // Simplified: just compute gain at this frequency for the band type
            float bandType = static_cast<float>(band.type);
            float bw = f0 / Q;
            float dist = std::abs(freq - f0) / std::max(bw * 0.5f, 1.0f);
            float magDb = gainDb / (1.0f + dist * dist);
            responseDb[p] += magDb;
        }
    }

    // Draw grid lines
    float dbRange = 18.0f;
    for (int db = -18; db <= 18; db += 6) {
        float y = bounds.bottom() - 20.0f - (static_cast<float>(db) + dbRange) / (dbRange * 2.0f) * (bounds.height - 40.0f);
        float alpha = (db == 0) ? 0.25f : 0.10f;
        renderer.drawLine({bounds.x + 40.0f, y}, {bounds.right() - 10.0f, y},
                          1.0f, theme.getColor("accentPrimary").withAlpha(alpha));
        std::string label = (db >= 0 ? "+" : "") + std::to_string(db) + "dB";
        renderer.drawText(label, {bounds.x + 4.0f, y - 6.0f}, 8.0f,
                          theme.getColor("textSecondary").withAlpha(0.7f));
    }

    // Frequency labels
    float freqs[] = {30, 100, 300, 1000, 3000, 10000};
    const char* freqLabels[] = {"30", "100", "300", "1k", "3k", "10k"};
    for (int i = 0; i < 6; ++i) {
        float norm = (std::log10(freqs[i]) - std::log10(20.0f)) / (std::log10(20000.0f) - std::log10(20.0f));
        float x = bounds.x + 40.0f + norm * (bounds.width - 50.0f);
        renderer.drawLine({x, bounds.y + 10.0f}, {x, bounds.bottom() - 10.0f},
                          1.0f, theme.getColor("textSecondary").withAlpha(0.10f));
        renderer.drawText(freqLabels[i], {x - 8.0f, bounds.bottom() - 10.0f}, 8.0f,
                          theme.getColor("textSecondary").withAlpha(0.7f));
    }

    // Draw curve
    float xStart = bounds.x + 40.0f;
    float xEnd = bounds.right() - 10.0f;
    float curveTop = bounds.y + 10.0f;
    float curveBottom = bounds.bottom() - 20.0f;

    for (int p = 0; p < numPoints - 1; ++p) {
        float x1 = xStart + (static_cast<float>(p) / (numPoints - 1)) * (xEnd - xStart);
        float x2 = xStart + (static_cast<float>(p + 1) / (numPoints - 1)) * (xEnd - xStart);
        float y1 = curveBottom - std::clamp((responseDb[p] + dbRange) / (dbRange * 2.0f), 0.0f, 1.0f) * (curveBottom - curveTop);
        float y2 = curveBottom - std::clamp((responseDb[p + 1] + dbRange) / (dbRange * 2.0f), 0.0f, 1.0f) * (curveBottom - curveTop);

        NUIColor curveColor = theme.getColor("accentPrimary").withAlpha(0.85f);
        // Glow
        renderer.drawLine({x1, y1}, {x2, y2}, 5.0f, curveColor.withAlpha(0.15f));
        renderer.drawLine({x1, y1}, {x2, y2}, 2.0f, curveColor);
    }
}

void AestraEQEditor::drawBandPanel(NUIRenderer& renderer, const BandControl& band) {
    auto& theme = NUIThemeManager::getInstance();
    NUIColor accent = theme.getColor("accentPrimary");
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

    // Band name
    renderer.drawText(band.name, {band.bounds.x + 4.0f, band.bounds.y + 4.0f}, 9.0f,
                      band.enabled ? theme.getColor("textPrimary") : theme.getColor("textSecondary").withAlpha(0.4f));

    // Type label
    if (band.enabled) {
        renderer.drawText(typeLabel(band.type), {band.bounds.x + 4.0f, band.bounds.y + 14.0f}, 7.5f,
                          accent.withAlpha(0.8f));
    }

    if (!band.enabled) return;

    // Freq slider
    renderer.fillRoundedRect(band.freqSlider, 3.0f, NUIColor(0.02f, 0.02f, 0.03f, 0.7f));
    float freqFill = band.freq * band.freqSlider.height;
    if (freqFill > 0) {
        renderer.fillRoundedRect({band.freqSlider.x, band.freqSlider.y + band.freqSlider.height - freqFill,
                                  band.freqSlider.width, freqFill},
                                 3.0f, NUIColor(0.3f, 0.5f, 0.8f, 0.8f));
    }
    renderer.fillRoundedRect(band.freqKnob, 6.0f, band.dragTarget == BandControl::Freq ? NUIColor(1.0f, 1.0f, 1.0f, 1.0f) : NUIColor(0.7f, 0.8f, 1.0f, 0.9f));

    // Gain slider
    renderer.fillRoundedRect(band.gainSlider, 3.0f, NUIColor(0.02f, 0.02f, 0.03f, 0.7f));
    float gainFill = band.gain * band.gainSlider.height;
    if (gainFill > 0) {
        renderer.fillRoundedRect({band.gainSlider.x, band.gainSlider.y + band.gainSlider.height - gainFill,
                                  band.gainSlider.width, gainFill},
                                 3.0f, accent.withAlpha(0.8f));
    }
    renderer.fillRoundedRect(band.gainKnob, 6.0f, band.dragTarget == BandControl::Gain ? NUIColor(1.0f, 1.0f, 1.0f, 1.0f) : accent.withAlpha(0.9f));

    // Q slider
    renderer.fillRoundedRect(band.qSlider, 3.0f, NUIColor(0.02f, 0.02f, 0.03f, 0.7f));
    float qFill = band.q * band.qSlider.height;
    if (qFill > 0) {
        renderer.fillRoundedRect({band.qSlider.x, band.qSlider.y + band.qSlider.height - qFill,
                                  band.qSlider.width, qFill},
                                 3.0f, NUIColor(0.8f, 0.5f, 0.3f, 0.8f));
    }
    renderer.fillRoundedRect(band.qKnob, 6.0f, band.dragTarget == BandControl::Q ? NUIColor(1.0f, 1.0f, 1.0f, 1.0f) : NUIColor(0.9f, 0.7f, 0.5f, 0.9f));

    // Value labels
    renderer.drawText(freqLabel(band.freq), {band.bounds.x + 2.0f, band.bounds.bottom() - 14.0f}, 7.0f,
                      theme.getColor("textSecondary").withAlpha(0.6f));
    renderer.drawText(gainLabel(band.gain), {band.bounds.x + 2.0f, band.bounds.bottom() - 6.0f}, 7.0f,
                      theme.getColor("textSecondary").withAlpha(0.6f));
    renderer.drawText(qLabel(band.q), {band.bounds.right() - 16.0f, band.bounds.bottom() - 14.0f}, 7.0f,
                      theme.getColor("textSecondary").withAlpha(0.6f));
}

void AestraEQEditor::onRender(NUIRenderer& renderer) {
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
        b.q = normalizedValue;
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
std::string AestraEQEditor::qLabel(float norm) const {
    float q = 0.1f + norm * 9.9f;
    std::ostringstream o; o << std::fixed << std::setprecision(1) << q; return o.str();
}

bool AestraEQEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;
    auto bounds = getBounds();
    bool isDraggingBand = std::any_of(m_bands.begin(), m_bands.end(),
                                      [](const BandControl& band) { return band.dragging; });
    bool contains = bounds.contains(event.position);

    if (event.pressed && event.button == NUIMouseButton::Left && !contains && !m_isDraggingWindow && !isDraggingBand) {
        if (m_onClose) m_onClose();
        return false;
    }
    if (!contains && !m_isDraggingWindow && !isDraggingBand) return false;

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

        int bandIdx = hitTestBand(event.position.x, event.position.y);
        if (bandIdx >= 0) {
            auto& band = m_bands[bandIdx];
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
        int hovered = contains ? hitTestBand(event.position.x, event.position.y) : -1;
        if (hovered != m_hoveredBand) {
            m_hoveredBand = hovered;
            for (size_t i = 0; i < m_bands.size(); ++i) m_bands[i].hovered = (static_cast<int>(i) == hovered);
            setDirty(true);
        }
    }

    return contains;
}

} // namespace AestraUI
