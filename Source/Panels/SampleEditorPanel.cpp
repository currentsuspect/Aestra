// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "SampleEditorPanel.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "NUILabel.h"
#include "NUICoreWidgets.h"
#include "MiniAudioDecoder.h"
#include "../AestraCore/include/AestraLog.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

using namespace AestraUI;

namespace Aestra {
namespace Audio {

// =============================================================================
// ADSRDisplayComponent
// =============================================================================

ADSRDisplayComponent::ADSRDisplayComponent() = default;

void ADSRDisplayComponent::setADSR(float attack, float decay, float sustain, float release) {
    m_attack = attack;
    m_decay = decay;
    m_sustain = sustain;
    m_release = release;
    repaint();
}

void ADSRDisplayComponent::onRender(NUIRenderer& renderer) {
    if (!isVisible()) return;
    auto b = getBounds();
    auto& theme = NUIThemeManager::getInstance();

    // Background
    renderer.fillRect(NUIRect(b.x, b.y, b.width, b.height), theme.getColor("surfaceTertiary"));
    renderer.strokeRect(b, 1.0f, theme.getColor("borderSubtle").withAlpha(0.82f));

    // Calculate envelope shape points
    // Total time = attack + decay + sustain(hold) + release
    // We use a fixed "hold" time for visualization (0.3 of total width)
    const float holdFrac = 0.3f;
    const float totalParam = m_attack + m_decay + 0.5f + m_release; // 0.5f = arbitrary hold
    const float aFrac = m_attack / std::max(totalParam, 0.001f);
    const float dFrac = m_decay / std::max(totalParam, 0.001f);
    const float rFrac = m_release / std::max(totalParam, 0.001f);
    const float holdFracActual = holdFrac;

    float margin = 8.0f;
    float graphW = b.width - margin * 2;
    float graphH = b.height - margin * 2;
    float baseX = b.x + margin;
    float baseY = b.y + margin;

    // Envelope points (normalized 0-1 in X, 0-1 in Y)
    struct Pt { float x, y; };
    std::vector<Pt> pts;
    pts.push_back({0.0f, 0.0f});
    pts.push_back({aFrac, 1.0f});
    pts.push_back({aFrac + dFrac, m_sustain});
    pts.push_back({aFrac + dFrac + holdFracActual, m_sustain});
    pts.push_back({aFrac + dFrac + holdFracActual + rFrac, 0.0f});

    // Draw envelope
    NUIColor lineCol = theme.getColor("secondary").withAlpha(0.88f);

    // Draw envelope line
    for (size_t i = 1; i < pts.size(); ++i) {
        float x0 = baseX + pts[i - 1].x * graphW;
        float y0 = baseY + graphH - pts[i - 1].y * graphH;
        float x1 = baseX + pts[i].x * graphW;
        float y1 = baseY + graphH - pts[i].y * graphH;
        renderer.drawLine(NUIPoint(x0, y0), NUIPoint(x1, y1), 2.0f, lineCol);
    }

    // Draw control point dots
    for (const auto& p : pts) {
        float px = baseX + p.x * graphW;
        float py = baseY + graphH - p.y * graphH;
        renderer.fillCircle(NUIPoint(px, py), 4.0f, NUIColor::white().withAlpha(0.9f));
        renderer.strokeCircle(NUIPoint(px, py), 4.0f, 1.0f, lineCol);
    }

    // Labels
    std::string label = "A:" + std::to_string(static_cast<int>(m_attack * 1000)) + "ms "
                       + "D:" + std::to_string(static_cast<int>(m_decay * 1000)) + "ms "
                       + "S:" + std::to_string(static_cast<int>(m_sustain * 100)) + "% "
                       + "R:" + std::to_string(static_cast<int>(m_release * 1000)) + "ms";
    renderer.drawText(label, NUIPoint(baseX, baseY + graphH + 4), 10.0f, theme.getColor("textSecondary").withAlpha(0.82f));
}

// =============================================================================
// WaveformDisplayComponent
// =============================================================================

WaveformDisplayComponent::WaveformDisplayComponent() = default;

void WaveformDisplayComponent::setWaveformData(const std::vector<float>& data) {
    m_waveformData = data;
    repaint();
}

void WaveformDisplayComponent::setZoom(float zoom) {
    m_zoom = std::clamp(zoom, 1.0f, 50.0f);
    // Clamp scroll to valid range
    float maxScroll = std::max(0.0f, 1.0f - 1.0f / m_zoom);
    m_scrollOffset = std::clamp(m_scrollOffset, 0.0f, maxScroll);
    repaint();
}

void WaveformDisplayComponent::setLoopPoints(float start, float end) {
    m_loopStart = std::clamp(start, 0.0f, 1.0f);
    m_loopEnd = std::clamp(end, start, 1.0f);
    repaint();
}

void WaveformDisplayComponent::setScrollOffset(float offset) {
    float maxScroll = std::max(0.0f, 1.0f - 1.0f / m_zoom);
    m_scrollOffset = std::clamp(offset, 0.0f, maxScroll);
    repaint();
}

void WaveformDisplayComponent::onRender(NUIRenderer& renderer) {
    if (!isVisible()) return;
    auto b = getBounds();
    auto& theme = NUIThemeManager::getInstance();

    renderer.setClipRect(b);

    // Background
    renderer.fillRect(NUIRect(b.x, b.y, b.width, b.height), theme.getColor("backgroundSecondary"));
    renderer.strokeRect(b, 1.0f, theme.getColor("borderSubtle").withAlpha(0.82f));

    // Center line
    float centerY = b.y + b.height * 0.5f;
    renderer.drawLine(NUIPoint(b.x, centerY), NUIPoint(b.x + b.width, centerY),
                      0.5f, theme.getColor("border").withAlpha(0.2f));

    if (m_waveformData.empty()) {
        renderer.clearClipRect();
        renderer.drawText("No sample loaded", NUIPoint(b.x + b.width * 0.5f - 50, centerY - 7),
                          12.0f, theme.getColor("textSecondary").withAlpha(0.4f));
        return;
    }

    // Visible range based on zoom and scroll
    float viewStart = m_scrollOffset;
    float viewEnd = m_scrollOffset + 1.0f / m_zoom;
    viewEnd = std::min(viewEnd, 1.0f);

    size_t totalPairs = m_waveformData.size() / 2;
    float pixelsPerSample = b.width / (viewEnd - viewStart);

    // Draw waveform
    NUIColor waveCol = theme.getColor("secondary").withAlpha(0.82f);
    NUIColor waveColNeg = theme.getColor("secondary").withAlpha(0.52f);

    for (size_t i = 0; i < totalPairs; ++i) {
        float normX = static_cast<float>(i) / static_cast<float>(totalPairs);
        if (normX < viewStart || normX > viewEnd) continue;

        float screenX = b.x + (normX - viewStart) * pixelsPerSample;
        float maxVal = m_waveformData[i * 2];
        float minVal = m_waveformData[i * 2 + 1];

        float yMax = centerY - maxVal * b.height * 0.45f;
        float yMin = centerY - minVal * b.height * 0.45f;

        // Positive (louder)
        renderer.drawLine(NUIPoint(screenX, centerY), NUIPoint(screenX, yMax), 1.0f, waveCol);
        // Negative (dimmer)
        renderer.drawLine(NUIPoint(screenX, centerY), NUIPoint(screenX, yMin), 1.0f, waveColNeg);
    }

    // Draw loop region overlay
    if (m_loopEnd > m_loopStart) {
        float loopX0 = b.x + (m_loopStart - viewStart) * pixelsPerSample;
        float loopX1 = b.x + (m_loopEnd - viewStart) * pixelsPerSample;
        NUIColor loopCol = theme.getColor("warning").withAlpha(0.12f);
        renderer.fillRect(NUIRect(loopX0, b.y, loopX1 - loopX0, b.height), loopCol);

        // Loop start handle
        renderer.drawLine(NUIPoint(loopX0, b.y), NUIPoint(loopX0, b.y + b.height),
                          2.0f, theme.getColor("warning").withAlpha(0.72f));
        renderer.fillCircle(NUIPoint(loopX0, centerY), 5.0f, theme.getColor("warning").withAlpha(0.90f));

        // Loop end handle
        renderer.drawLine(NUIPoint(loopX1, b.y), NUIPoint(loopX1, b.y + b.height),
                          2.0f, theme.getColor("warning").withAlpha(0.72f));
        renderer.fillCircle(NUIPoint(loopX1, centerY), 5.0f, theme.getColor("warning").withAlpha(0.90f));
    }

    renderer.clearClipRect();
}

bool WaveformDisplayComponent::onMouseEvent(const NUIMouseEvent& event) {
    auto b = getBounds();
    const bool isDragging = m_draggingLoopStart || m_draggingLoopEnd || m_draggingViewport;
    if (event.released && event.button == NUIMouseButton::Left) {
        const bool consumed = isDragging;
        m_draggingLoopStart = false;
        m_draggingLoopEnd = false;
        m_draggingViewport = false;
        return consumed;
    }

    if ((event.position.x < b.x || event.position.x > b.x + b.width ||
         event.position.y < b.y || event.position.y > b.y + b.height) && !isDragging) {
        return false;
    }

    if (event.wheelDelta != 0.0f) {
        const float zoomDelta = event.wheelDelta > 0.0f ? 1.2f : 0.8f;
        const float oldZoom = m_zoom;
        const float newZoom = std::clamp(oldZoom * zoomDelta, 1.0f, 50.0f);
        const float mouseNorm = std::clamp((event.position.x - b.x) / std::max(1.0f, b.width), 0.0f, 1.0f);
        const float oldViewStart = m_scrollOffset;
        const float oldViewWidth = 1.0f / std::max(oldZoom, 1.0f);
        const float anchorPos = oldViewStart + mouseNorm * oldViewWidth;

        setZoom(newZoom);

        const float newViewWidth = 1.0f / std::max(newZoom, 1.0f);
        float newScroll = anchorPos - mouseNorm * newViewWidth;
        const float maxScroll = std::max(0.0f, 1.0f - newViewWidth);
        newScroll = std::clamp(newScroll, 0.0f, maxScroll);
        m_scrollOffset = newScroll;

        if (onZoomChanged) onZoomChanged(m_zoom);
        if (onScrollChanged) onScrollChanged(m_scrollOffset);
        repaint();
        return true;
    }

    float viewStart = m_scrollOffset;
    float viewEnd = m_scrollOffset + 1.0f / m_zoom;
    float pixelsPerSample = b.width / (viewEnd - viewStart);

    // Check loop handle drag
    constexpr float handleThreshold = 8.0f;
    float loopStartX = b.x + (m_loopStart - viewStart) * pixelsPerSample;
    float loopEndX = b.x + (m_loopEnd - viewStart) * pixelsPerSample;

    if (event.pressed && event.button == NUIMouseButton::Left) {
        // Check if clicking on loop handles
        if (std::abs(event.position.x - loopStartX) < handleThreshold) {
            m_draggingLoopStart = true;
            if (onLoopDragStarted) onLoopDragStarted(m_loopStart, m_loopEnd);
            return true;
        }
        if (std::abs(event.position.x - loopEndX) < handleThreshold) {
            m_draggingLoopEnd = true;
            if (onLoopDragStarted) onLoopDragStarted(m_loopStart, m_loopEnd);
            return true;
        }
        // Otherwise, start viewport drag
        m_draggingViewport = true;
        m_dragStartX = event.position.x;
        m_dragStartScroll = m_scrollOffset;
        return true;
    }

    if (!event.pressed) {
        if (m_draggingLoopStart) {
            float dx = event.position.x - loopStartX;
            float dNorm = dx / pixelsPerSample;
            float newStart = std::clamp(m_loopStart + dNorm, 0.0f, m_loopEnd - 0.01f);
            m_loopStart = newStart;
            if (onLoopDragged) onLoopDragged(newStart, m_loopEnd);
            repaint();
            return true;
        }
        if (m_draggingLoopEnd) {
            float dx = event.position.x - loopEndX;
            float dNorm = dx / pixelsPerSample;
            float newEnd = std::clamp(m_loopEnd + dNorm, m_loopStart + 0.01f, 1.0f);
            m_loopEnd = newEnd;
            if (onLoopDragged) onLoopDragged(m_loopStart, newEnd);
            repaint();
            return true;
        }
        if (m_draggingViewport) {
            float dx = event.position.x - m_dragStartX;
            float dNorm = -dx / pixelsPerSample;
            float newScroll = std::clamp(m_dragStartScroll + dNorm, 0.0f, 1.0f - 1.0f / m_zoom);
            m_scrollOffset = newScroll;
            if (onScrollChanged) onScrollChanged(newScroll);
            repaint();
            return true;
        }
    }

    return false;
}

// =============================================================================
// SampleEditorPanel
// =============================================================================

SampleEditorPanel::SampleEditorPanel(std::shared_ptr<TrackManager> trackManager)
    : WindowPanel("SAMPLE EDITOR")
    , m_trackManager(std::move(trackManager))
{
    buildUI();
}

void SampleEditorPanel::buildUI() {
    m_contentContainer = std::make_shared<NUIComponent>();
    auto& theme = NUIThemeManager::getInstance();

    // Waveform display
    m_waveformDisplay = std::make_shared<WaveformDisplayComponent>();
    m_waveformDisplay->onZoomChanged = [this](float zoom) {
        m_suppressControlCallbacks = true;
        m_zoomSlider->setValue(zoom);
        m_suppressControlCallbacks = false;
    };
    m_waveformDisplay->onLoopDragged = [this](float start, float end) {
        m_suppressControlCallbacks = true;
        m_loopStartSlider->setValue(start);
        m_loopEndSlider->setValue(end);
        m_suppressControlCallbacks = false;
        onLoopControlChanged();
        requestControlCommit();
    };

    // ADSR display
    m_adsrDisplay = std::make_shared<ADSRDisplayComponent>();
    m_adsrDisplay->setADSR(m_adsr.attack, m_adsr.decay, m_adsr.sustain, m_adsr.release);

    // ADSR Sliders
    auto makeSlider = [](const std::string& label, double min, double max, double initial) {
        auto slider = std::make_shared<NUISlider>(label);
        slider->setOrientation(NUISlider::Orientation::Horizontal);
        slider->setTextBoxVisible(false);
        slider->setSliderRadius(7.0f);
        slider->setSliderThickness(5.0f);
        slider->setRange(min, max);
        slider->setValue(initial);
        return slider;
    };
    auto makeLabel = [&theme](const std::string& text) {
        auto label = std::make_shared<NUILabel>(text);
        label->setFontSize(12.0f);
        label->setTextColor(theme.getColor("textSecondary").withAlpha(0.92f));
        return label;
    };

    m_attackSlider = makeSlider("Attack", 0.001, 2.0, m_adsr.attack);
    m_decaySlider = makeSlider("Decay", 0.001, 2.0, m_adsr.decay);
    m_sustainSlider = makeSlider("Sustain", 0.0, 1.0, m_adsr.sustain);
    m_releaseSlider = makeSlider("Release", 0.001, 5.0, m_adsr.release);

    // Zoom slider
    m_zoomSlider = makeSlider("Zoom", 1.0, 20.0, 1.0);

    // Loop controls
    m_loopStartSlider = makeSlider("Loop Start", 0.0, 1.0, m_loopPoints.start);
    m_loopEndSlider = makeSlider("Loop End", 0.0, 1.0, m_loopPoints.end);

    m_loopModeCombo = std::make_shared<NUIComboBox>();
    m_loopModeCombo->setItems({
        {"oneshot", "One-Shot"},
        {"loop", "Loop"},
        {"pingpong", "Ping-Pong"}
    });
    m_loopModeCombo->setSelectedId("oneshot");

    // Pitch/Tune controls
    m_pitchCoarseSlider = makeSlider("Coarse", -24.0, 24.0, 0.0);
    m_pitchFineSlider = makeSlider("Fine", -100.0, 100.0, 0.0);
    m_voiceCountSlider = makeSlider("Voices", 1.0, 32.0, 16.0);

    // Action buttons
    m_normalizeBtn = std::make_shared<NUIButton>("Normalize");
    m_reverseBtn = std::make_shared<NUIButton>("Reverse");
    m_zoomLabel = makeLabel("Waveform Zoom");
    m_trimLabel = makeLabel("Trim Start / End");
    m_modeLabel = makeLabel("Playback Mode / Voices");
    m_pitchLabel = makeLabel("Keyboard Pitch (coarse / fine)");
    m_adsrLabel = makeLabel("Envelope (A / D / S / R)");

    // Wire callbacks
    auto adsrChanged = [this]() { onADSRControlChanged(); };
    m_attackSlider->setOnValueChange([this, adsrChanged](double) { adsrChanged(); });
    m_decaySlider->setOnValueChange([this, adsrChanged](double) { adsrChanged(); });
    m_sustainSlider->setOnValueChange([this, adsrChanged](double) { adsrChanged(); });
    m_releaseSlider->setOnValueChange([this, adsrChanged](double) { adsrChanged(); });
    m_attackSlider->setOnDragEnd([this]() { requestControlCommit(); });
    m_decaySlider->setOnDragEnd([this]() { requestControlCommit(); });
    m_sustainSlider->setOnDragEnd([this]() { requestControlCommit(); });
    m_releaseSlider->setOnDragEnd([this]() { requestControlCommit(); });

    m_zoomSlider->setOnValueChange([this](double v) { onWaveformZoomChanged(static_cast<float>(v)); });

    auto loopChanged = [this]() { onLoopControlChanged(); };
    m_loopStartSlider->setOnValueChange([this, loopChanged](double) { loopChanged(); });
    m_loopEndSlider->setOnValueChange([this, loopChanged](double) { loopChanged(); });
    m_loopStartSlider->setOnDragEnd([this]() { requestControlCommit(); });
    m_loopEndSlider->setOnDragEnd([this]() { requestControlCommit(); });
    m_loopModeCombo->setOnSelectionChanged([this, loopChanged](const std::string&) { loopChanged(); requestControlCommit(); });

    auto pitchChanged = [this]() { onPitchControlChanged(); };
    m_pitchCoarseSlider->setOnValueChange([this, pitchChanged](double) { pitchChanged(); });
    m_pitchFineSlider->setOnValueChange([this, pitchChanged](double) { pitchChanged(); });
    m_pitchCoarseSlider->setOnDragEnd([this]() { requestControlCommit(); });
    m_pitchFineSlider->setOnDragEnd([this]() { requestControlCommit(); });
    m_voiceCountSlider->setOnValueChange([this](double) { onVoiceCountControlChanged(); });
    m_voiceCountSlider->setOnDragEnd([this]() { requestControlCommit(); });

    m_normalizeBtn->setOnClick([this]() {
        normalize();
        if (onNormalizeRequested) onNormalizeRequested();
        if (onSampleModified) onSampleModified();
        requestControlCommit();
    });
    m_reverseBtn->setOnClick([this]() {
        reverse();
        if (onReverseRequested) onReverseRequested();
        if (onSampleModified) onSampleModified();
        requestControlCommit();
    });

    m_contentContainer->addChild(m_waveformDisplay);
    m_contentContainer->addChild(m_zoomLabel);
    m_contentContainer->addChild(m_adsrDisplay);
    m_contentContainer->addChild(m_trimLabel);
    m_contentContainer->addChild(m_modeLabel);
    m_contentContainer->addChild(m_pitchLabel);
    m_contentContainer->addChild(m_adsrLabel);
    m_contentContainer->addChild(m_zoomSlider);
    m_contentContainer->addChild(m_attackSlider);
    m_contentContainer->addChild(m_decaySlider);
    m_contentContainer->addChild(m_sustainSlider);
    m_contentContainer->addChild(m_releaseSlider);
    m_contentContainer->addChild(m_loopStartSlider);
    m_contentContainer->addChild(m_loopEndSlider);
    m_contentContainer->addChild(m_loopModeCombo);
    m_contentContainer->addChild(m_pitchCoarseSlider);
    m_contentContainer->addChild(m_pitchFineSlider);
    m_contentContainer->addChild(m_voiceCountSlider);
    m_contentContainer->addChild(m_normalizeBtn);
    m_contentContainer->addChild(m_reverseBtn);

    setContent(m_contentContainer);
}

void SampleEditorPanel::requestControlCommit() {
    if (!m_suppressControlCallbacks && onControlCommitRequested) {
        onControlCommitRequested();
    }
}

void SampleEditorPanel::loadSample(const std::string& path) {
    if (!m_trackManager) return;

    Log::info("[SampleEditor] Loading sample: " + path);

    std::vector<float> decoded;
    uint32_t decodedRate = 0;
    uint32_t decodedChannels = 0;
    if (decodeAudioFile(path, decoded, decodedRate, decodedChannels) && decodedChannels > 0 && !decoded.empty()) {
        m_sampleRate = static_cast<double>(decodedRate);
        m_sampleLength = static_cast<uint32_t>(decoded.size() / decodedChannels);

        const size_t buckets = 1024;
        const size_t framesPerBucket = std::max<size_t>(1, static_cast<size_t>(m_sampleLength) / buckets);
        m_waveformData.clear();
        m_waveformData.reserve(buckets * 2);

        for (size_t bucket = 0; bucket < buckets; ++bucket) {
            const size_t startFrame = bucket * framesPerBucket;
            const size_t endFrame = std::min<size_t>(static_cast<size_t>(m_sampleLength), startFrame + framesPerBucket);
            if (startFrame >= endFrame) {
                m_waveformData.push_back(0.0f);
                m_waveformData.push_back(0.0f);
                continue;
            }

            float minV = 1.0f;
            float maxV = -1.0f;
            for (size_t frame = startFrame; frame < endFrame; ++frame) {
                const size_t idx = frame * decodedChannels;
                const float mono = (decodedChannels > 1) ? 0.5f * (decoded[idx] + decoded[idx + 1]) : decoded[idx];
                minV = std::min(minV, mono);
                maxV = std::max(maxV, mono);
            }
            m_waveformData.push_back(maxV);
            m_waveformData.push_back(minV);
        }
    } else {
        m_waveformData.resize(1000 * 2);
        for (size_t i = 0; i < 1000; ++i) {
            float t = static_cast<float>(i) / 1000.0f;
            m_waveformData[i * 2] = std::sin(t * 20.0f) * 0.5f;
            m_waveformData[i * 2 + 1] = -std::sin(t * 20.0f) * 0.5f;
        }
    }

    m_waveformDisplay->setWaveformData(m_waveformData);
    m_waveformDisplay->repaint();
    m_adsrDisplay->repaint();
}

void SampleEditorPanel::setADSR(const ADSRParams& params) {
    m_adsr = params;
    m_suppressControlCallbacks = true;
    m_attackSlider->setValue(params.attack);
    m_decaySlider->setValue(params.decay);
    m_sustainSlider->setValue(params.sustain);
    m_releaseSlider->setValue(params.release);
    m_suppressControlCallbacks = false;
    m_adsrDisplay->setADSR(params.attack, params.decay, params.sustain, params.release);
}

void SampleEditorPanel::setLoopPoints(const LoopPoints& lp) {
    m_loopPoints = lp;
    m_suppressControlCallbacks = true;
    m_loopStartSlider->setValue(lp.start);
    m_loopEndSlider->setValue(lp.end);
    std::string modeId;
    switch (lp.mode) {
        case LoopMode::OneShot: modeId = "oneshot"; break;
        case LoopMode::Loop: modeId = "loop"; break;
        case LoopMode::PingPong: modeId = "pingpong"; break;
    }
    m_loopModeCombo->setSelectedId(modeId);
    m_suppressControlCallbacks = false;
    m_waveformDisplay->setLoopPoints(lp.start, lp.end);
}

void SampleEditorPanel::setPitchTune(const PitchTune& pt) {
    m_pitchTune = pt;
    m_suppressControlCallbacks = true;
    m_pitchCoarseSlider->setValue(static_cast<double>(pt.coarse));
    m_pitchFineSlider->setValue(pt.fine);
    m_suppressControlCallbacks = false;
}

void SampleEditorPanel::setVoiceCount(int voices) {
    if (m_voiceCountSlider) {
        m_suppressControlCallbacks = true;
        m_voiceCountSlider->setValue(static_cast<double>(std::clamp(voices, 1, 32)));
        m_suppressControlCallbacks = false;
    }
}

int SampleEditorPanel::getVoiceCount() const {
    return m_voiceCountSlider ? static_cast<int>(std::round(m_voiceCountSlider->getValue())) : 16;
}

void SampleEditorPanel::onResize(int width, int height) {
    WindowPanel::onResize(width, height);
    if (!m_contentContainer) {
        return;
    }

    const auto cb = m_contentContainer->getBounds();
    const float pad = 8.0f;
    const float gutter = 6.0f;
    const float labelH = 14.0f;
    const float rowH = 22.0f;
    const float controlW = std::max(80.0f, (cb.width - (pad * 2.0f) - gutter * 3.0f) / 4.0f);
    const float halfW = std::max(80.0f, (cb.width - (pad * 2.0f) - gutter) * 0.5f);

    float y = cb.y + pad;
    m_waveformDisplay->setBounds(NUIRect(cb.x + pad, y, cb.width - pad * 2.0f, std::max(100.0f, cb.height * 0.30f)));
    y += m_waveformDisplay->getBounds().height + gutter;

    m_zoomLabel->setBounds(NUIRect(cb.x + pad, y, cb.width - pad * 2.0f, labelH));
    y += labelH;
    m_zoomSlider->setBounds(NUIRect(cb.x + pad, y, cb.width - pad * 2.0f, rowH));
    y += rowH + gutter;

    m_trimLabel->setBounds(NUIRect(cb.x + pad, y, cb.width - pad * 2.0f, labelH));
    y += labelH;
    m_loopStartSlider->setBounds(NUIRect(cb.x + pad, y, halfW, rowH));
    m_loopEndSlider->setBounds(NUIRect(cb.x + pad + halfW + gutter, y, halfW, rowH));
    y += rowH + gutter;

    m_modeLabel->setBounds(NUIRect(cb.x + pad, y, cb.width - pad * 2.0f, labelH));
    y += labelH;
    m_loopModeCombo->setBounds(NUIRect(cb.x + pad, y, halfW, rowH));
    m_voiceCountSlider->setBounds(NUIRect(cb.x + pad + halfW + gutter, y, halfW, rowH));
    y += rowH + gutter;

    m_pitchLabel->setBounds(NUIRect(cb.x + pad, y, cb.width - pad * 2.0f, labelH));
    y += labelH;
    m_pitchCoarseSlider->setBounds(NUIRect(cb.x + pad, y, halfW, rowH));
    m_pitchFineSlider->setBounds(NUIRect(cb.x + pad + halfW + gutter, y, halfW, rowH));
    y += rowH + gutter;

    m_adsrLabel->setBounds(NUIRect(cb.x + pad, y, cb.width - pad * 2.0f, labelH));
    y += labelH;
    m_attackSlider->setBounds(NUIRect(cb.x + pad, y, controlW, rowH));
    m_decaySlider->setBounds(NUIRect(cb.x + pad + (controlW + gutter), y, controlW, rowH));
    m_sustainSlider->setBounds(NUIRect(cb.x + pad + 2.0f * (controlW + gutter), y, controlW, rowH));
    m_releaseSlider->setBounds(NUIRect(cb.x + pad + 3.0f * (controlW + gutter), y, controlW, rowH));
    y += rowH + gutter;

    m_adsrDisplay->setBounds(NUIRect(cb.x + pad, y, cb.width - pad * 2.0f, std::max(56.0f, cb.bottom() - y - (rowH + gutter + pad))));
    y = std::max(y, cb.bottom() - rowH - pad);
    m_normalizeBtn->setBounds(NUIRect(cb.x + pad, y, halfW, rowH));
    m_reverseBtn->setBounds(NUIRect(cb.x + pad + halfW + gutter, y, halfW, rowH));
}

void SampleEditorPanel::normalize() {
    if (m_waveformData.empty()) return;

    // Find peak
    float peak = 0.0f;
    for (float v : m_waveformData) {
        peak = std::max(peak, std::abs(v));
    }
    if (peak < 0.001f) return; // Silence

    // Normalize to 0.95
    float gain = 0.95f / peak;
    for (auto& v : m_waveformData) {
        v *= gain;
    }
    m_waveformDisplay->setWaveformData(m_waveformData);
    Log::info("[SampleEditor] Normalized to peak " + std::to_string(peak));
}

void SampleEditorPanel::reverse() {
    if (m_waveformData.empty()) return;

    size_t totalPairs = m_waveformData.size() / 2;
    for (size_t i = 0; i < totalPairs / 2; ++i) {
        size_t j = totalPairs - 1 - i;
        // Swap min/max pairs
        std::swap(m_waveformData[i * 2], m_waveformData[j * 2]);
        std::swap(m_waveformData[i * 2 + 1], m_waveformData[j * 2 + 1]);
    }
    m_waveformDisplay->setWaveformData(m_waveformData);
    Log::info("[SampleEditor] Reversed sample");
}

void SampleEditorPanel::onWaveformZoomChanged(float zoom) {
    m_waveformZoom = zoom;
    m_waveformDisplay->setZoom(zoom);
}

void SampleEditorPanel::onADSRControlChanged() {
    if (m_suppressControlCallbacks) {
        return;
    }
    m_adsr.attack = static_cast<float>(m_attackSlider->getValue());
    m_adsr.decay = static_cast<float>(m_decaySlider->getValue());
    m_adsr.sustain = static_cast<float>(m_sustainSlider->getValue());
    m_adsr.release = static_cast<float>(m_releaseSlider->getValue());

    m_adsrDisplay->setADSR(m_adsr.attack, m_adsr.decay, m_adsr.sustain, m_adsr.release);

    if (onADSRChanged) onADSRChanged(m_adsr);
}

void SampleEditorPanel::onLoopControlChanged() {
    if (m_suppressControlCallbacks) {
        return;
    }
    m_loopPoints.start = static_cast<float>(m_loopStartSlider->getValue());
    m_loopPoints.end = static_cast<float>(m_loopEndSlider->getValue());

    std::string modeId = m_loopModeCombo->getSelectedId();
    if (modeId == "oneshot") m_loopPoints.mode = LoopMode::OneShot;
    else if (modeId == "loop") m_loopPoints.mode = LoopMode::Loop;
    else if (modeId == "pingpong") m_loopPoints.mode = LoopMode::PingPong;

    m_waveformDisplay->setLoopPoints(m_loopPoints.start, m_loopPoints.end);

    if (onLoopPointsChanged) onLoopPointsChanged(m_loopPoints);
}

void SampleEditorPanel::onPitchControlChanged() {
    if (m_suppressControlCallbacks) {
        return;
    }
    m_pitchTune.coarse = static_cast<int>(m_pitchCoarseSlider->getValue());
    m_pitchTune.fine = static_cast<float>(m_pitchFineSlider->getValue());

    if (onPitchTuneChanged) onPitchTuneChanged(m_pitchTune);
}

void SampleEditorPanel::onVoiceCountControlChanged() {
    if (m_suppressControlCallbacks) {
        return;
    }
    if (onVoiceCountChanged) {
        onVoiceCountChanged(static_cast<int>(std::round(m_voiceCountSlider->getValue())));
    }
}

} // namespace Audio
} // namespace Aestra
