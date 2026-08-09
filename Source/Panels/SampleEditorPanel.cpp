// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "SampleEditorPanel.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "NUILabel.h"
#include "../AestraUI/Platform/NUIPlatformBridge.h"
#include "MiniAudioDecoder.h"
#include "../AestraCore/include/AestraLog.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

using namespace AestraUI;

namespace Aestra {
namespace Audio {

namespace {
constexpr float kADSRMinAttack = 0.001f;
constexpr float kADSRMaxAttack = 2.0f;
constexpr float kADSRMinDecay = 0.001f;
constexpr float kADSRMaxDecay = 2.0f;
constexpr float kADSRMinRelease = 0.001f;
constexpr float kADSRMaxRelease = 5.0f;
constexpr float kSampleEditorMinPanelW = 500.0f;
constexpr float kSampleEditorMinContentH = 480.0f;
constexpr float kSampleEditorMinPanelH = kSampleEditorMinContentH + 28.0f;
constexpr float kSampleEditorModeRowH = 36.0f;
constexpr float kSampleEditorPitchRowH = 92.0f;
constexpr float kSampleEditorADSRH = 140.0f;
constexpr float kSampleEditorButtonRowH = 36.0f;
constexpr float kSampleEditorWaveformMinH = 120.0f;
constexpr float kADSRHandleHitRadius = 11.0f; // Generous grab area — handles were hard to pick up
constexpr float kTwoPi = 6.28318530718f;

float remapClamped(float value, float inMin, float inMax, float outMin, float outMax) {
    const float t = std::clamp((value - inMin) / std::max(0.001f, inMax - inMin), 0.0f, 1.0f);
    return outMin + t * (outMax - outMin);
}

std::string midiNoteName(int midiNote) {
    static constexpr const char* kPitchNames[] = {"C", "C#", "D", "D#", "E", "F",
                                                  "F#", "G", "G#", "A", "A#", "B"};
    const int clamped = std::clamp(midiNote, 0, 127);
    return std::string(kPitchNames[clamped % 12]) + std::to_string(clamped / 12 - 2) +
           " (" + std::to_string(clamped) + ")";
}

// Perceptual (logarithmic) time mapping for the envelope handles. The linear
// mapping put ~13ms of attack on every pixel, so the tiniest drag jumped to
// hundreds of ms and made short one-shot samples inaudible ("ADSR = silence").
// Log spacing gives the musically useful 1-100ms range most of the travel.
float adsrTimeToNorm(float seconds, float minSeconds, float maxSeconds) {
    seconds = std::clamp(seconds, minSeconds, maxSeconds);
    return std::log(seconds / minSeconds) / std::log(maxSeconds / minSeconds);
}

float adsrNormToTime(float norm, float minSeconds, float maxSeconds) {
    norm = std::clamp(norm, 0.0f, 1.0f);
    return minSeconds * std::pow(maxSeconds / minSeconds, norm);
}

struct ADSRGeometry {
    float baseX{0.0f};
    float baseY{0.0f};
    float graphW{1.0f};
    float graphH{1.0f};
    NUIPoint start;
    NUIPoint attack;
    NUIPoint decay;
    NUIPoint sustain;
    NUIPoint releaseStart;
    NUIPoint end;
};

ADSRGeometry calculateADSRGeometry(const NUIRect& b, float attack, float decay, float sustain, float release) {
    const float margin = 10.0f;
    const float readoutH = 18.0f;
    const float readoutGap = 16.0f;
    ADSRGeometry g;
    g.baseX = b.x + margin;
    g.baseY = b.y + margin;
    g.graphW = std::max(1.0f, b.width - margin * 2.0f);
    g.graphH = std::max(1.0f, b.height - margin * 2.0f - readoutH - readoutGap);

    const float leftEdge = g.baseX + kADSRHandleHitRadius;
    const float rightEdge = g.baseX + g.graphW - kADSRHandleHitRadius;
    const float handleRange = std::max(1.0f, rightEdge - leftEdge);
    const float attackNorm = adsrTimeToNorm(attack, kADSRMinAttack, kADSRMaxAttack);
    const float decayNorm = adsrTimeToNorm(decay, kADSRMinDecay, kADSRMaxDecay);
    const float releaseNorm = adsrTimeToNorm(release, kADSRMinRelease, kADSRMaxRelease);
    const float attackX = leftEdge + handleRange * attackNorm * 0.35f;
    const float decayXAbs = std::clamp(attackX + handleRange * decayNorm * 0.32f, leftEdge, rightEdge);
    const float releaseXAbs = std::clamp(rightEdge - handleRange * releaseNorm * 0.42f, leftEdge, rightEdge);
    const float decayX = std::clamp((decayXAbs - g.baseX) / g.graphW, 0.0f, 1.0f);
    const float releaseX = std::clamp((releaseXAbs - g.baseX) / g.graphW, decayX, 1.0f);
    const float sustainMidX = decayX + (releaseX - decayX) * 0.5f;

    auto point = [&](float xNorm, float yNorm) {
        return NUIPoint(g.baseX + xNorm * g.graphW, g.baseY + g.graphH - yNorm * g.graphH);
    };

    g.start = point(0.0f, 0.0f);
    g.attack = NUIPoint(std::clamp(attackX, leftEdge, rightEdge), g.baseY);
    g.decay = point(decayX, sustain);
    g.sustain = point(sustainMidX, sustain);
    g.releaseStart = point(releaseX, sustain);
    g.end = point(1.0f, 0.0f);
    return g;
}

float distanceTo(const NUIPoint& a, const NUIPoint& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

class SampleEditorSurfaceComponent final : public NUIComponent {
public:
    void onRender(NUIRenderer& renderer) override {
        if (!isVisible()) return;
        auto b = getBounds();
        auto& theme = NUIThemeManager::getInstance();

        renderer.fillRect(b, theme.getColor("workspaceBackground"));

        const float pad = 8.0f;
        const float gutter = 6.0f;
        const float labelH = 14.0f;
        const float layoutW = std::max(b.width, kSampleEditorMinPanelW);
        const float layoutH = std::max(b.height, kSampleEditorMinContentH);
        const float contentW = layoutW - pad * 2.0f;
        const float fixedStackH = labelH + gutter + kSampleEditorModeRowH + gutter + kSampleEditorPitchRowH +
                                  gutter + labelH + kSampleEditorADSRH + gutter + kSampleEditorButtonRowH + pad * 2.0f;
        const float waveformH = std::max(kSampleEditorWaveformMinH, layoutH - fixedStackH);
        const float controlY = b.y + pad + waveformH + 3.0f + labelH + gutter - 4.0f;
        const float controlH = kSampleEditorModeRowH + gutter + kSampleEditorPitchRowH + 8.0f;
        const float adsrY = controlY + controlH + gutter + 2.0f;
        const float adsrH = labelH + kSampleEditorADSRH + 8.0f;

        const auto cardFill = theme.getColor("surfaceTertiary").withAlpha(0.42f);
        const auto cardStroke = theme.getColor("secondary").withAlpha(0.10f);
        renderer.fillRoundedRect(NUIRect(b.x + pad, controlY, contentW, controlH), 6.0f, cardFill);
        renderer.strokeRoundedRect(NUIRect(b.x + pad, controlY, contentW, controlH), 6.0f, 1.0f, cardStroke);
        renderer.fillRoundedRect(NUIRect(b.x + pad, adsrY, contentW, adsrH), 6.0f, cardFill.withAlpha(0.34f));
        renderer.strokeRoundedRect(NUIRect(b.x + pad, adsrY, contentW, adsrH), 6.0f, 1.0f, cardStroke);

        renderChildren(renderer);
        setDirty(false);
    }
};
} // namespace

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
    const float radius = theme.getRadius("s");
    renderer.fillRoundedRect(NUIRect(b.x, b.y, b.width, b.height), radius, theme.getColor("recessedPanel"));
    renderer.strokeRoundedRect(b, radius, 1.0f, theme.getColor("borderSubtle"));

    const auto g = calculateADSRGeometry(b, m_attack, m_decay, m_sustain, m_release);
    const std::vector<NUIPoint> pts{g.start, g.attack, g.decay, g.releaseStart, g.end};

    // Draw envelope
    NUIColor lineCol = theme.getColor("secondary").withAlpha(0.88f);
    NUIColor gridCol = theme.getColor("borderSubtle").withAlpha(0.16f);

    for (int i = 1; i < 4; ++i) {
        const float gx = g.baseX + (g.graphW * static_cast<float>(i) / 4.0f);
        renderer.drawLine(NUIPoint(gx, g.baseY), NUIPoint(gx, g.baseY + g.graphH), 1.0f, gridCol);
    }
    for (int i = 1; i < 3; ++i) {
        const float gy = g.baseY + (g.graphH * static_cast<float>(i) / 3.0f);
        renderer.drawLine(NUIPoint(g.baseX, gy), NUIPoint(g.baseX + g.graphW, gy), 1.0f, gridCol);
    }

    // Draw envelope line
    for (size_t i = 1; i < pts.size(); ++i) {
        renderer.drawLine(pts[i - 1], pts[i], 2.0f, lineCol);
    }

    renderer.fillCircle(g.start, 3.0f, NUIColor::white().withAlpha(0.35f));
    renderer.fillCircle(g.end, 3.0f, NUIColor::white().withAlpha(0.35f));

    auto drawHandle = [&](Handle handle, const NUIPoint& p) {
        const bool active = handle == m_hoveredHandle || handle == m_draggingHandle;
        const float pulse = active ? (0.5f - 0.5f * std::cos(m_hoverPulseTime * kTwoPi / 0.4f)) : 0.0f;
        const float radius = active ? (4.0f + 1.5f * pulse) : 4.0f;
        renderer.fillCircle(p, radius, NUIColor::white().withAlpha(active ? 0.98f : 0.9f));
        renderer.strokeCircle(p, radius, 1.5f, lineCol.withAlpha(active ? 1.0f : 0.82f));

        if (!active) {
            return;
        }

        const auto arrowCol = lineCol.withAlpha(0.78f + 0.22f * pulse);
        const float gap = radius + 4.0f;
        const float arrow = 4.0f;
        if (handle == Handle::Sustain) {
            const float topY = p.y - gap - arrow;
            const float bottomY = p.y + gap + arrow;
            renderer.drawLine({p.x, topY + arrow}, {p.x, topY}, 1.4f, arrowCol);
            renderer.drawLine({p.x, topY}, {p.x - 2.5f, topY + 3.0f}, 1.4f, arrowCol);
            renderer.drawLine({p.x, topY}, {p.x + 2.5f, topY + 3.0f}, 1.4f, arrowCol);
            renderer.drawLine({p.x, bottomY - arrow}, {p.x, bottomY}, 1.4f, arrowCol);
            renderer.drawLine({p.x, bottomY}, {p.x - 2.5f, bottomY - 3.0f}, 1.4f, arrowCol);
            renderer.drawLine({p.x, bottomY}, {p.x + 2.5f, bottomY - 3.0f}, 1.4f, arrowCol);
        } else {
            const float leftX = p.x - gap - arrow;
            const float rightX = p.x + gap + arrow;
            renderer.drawLine({leftX + arrow, p.y}, {leftX, p.y}, 1.4f, arrowCol);
            renderer.drawLine({leftX, p.y}, {leftX + 3.0f, p.y - 2.5f}, 1.4f, arrowCol);
            renderer.drawLine({leftX, p.y}, {leftX + 3.0f, p.y + 2.5f}, 1.4f, arrowCol);
            renderer.drawLine({rightX - arrow, p.y}, {rightX, p.y}, 1.4f, arrowCol);
            renderer.drawLine({rightX, p.y}, {rightX - 3.0f, p.y - 2.5f}, 1.4f, arrowCol);
            renderer.drawLine({rightX, p.y}, {rightX - 3.0f, p.y + 2.5f}, 1.4f, arrowCol);
        }
    };
    drawHandle(Handle::Attack, g.attack);
    drawHandle(Handle::Decay, g.decay);
    drawHandle(Handle::Sustain, g.sustain);
    drawHandle(Handle::Release, g.releaseStart);

    // Labels
    std::string label = "A:" + std::to_string(static_cast<int>(m_attack * 1000)) + "ms "
                       + "D:" + std::to_string(static_cast<int>(m_decay * 1000)) + "ms "
                       + "S:" + std::to_string(static_cast<int>(m_sustain * 100)) + "% "
                       + "R:" + std::to_string(static_cast<int>(m_release * 1000)) + "ms";
    renderer.drawText(label, NUIPoint(g.baseX, g.baseY + g.graphH + 6), 10.0f, theme.getColor("textSecondary").withAlpha(0.82f));
}

void ADSRDisplayComponent::onUpdate(double deltaTime) {
    NUIComponent::onUpdate(deltaTime);
    if (m_hoveredHandle != Handle::None || m_draggingHandle != Handle::None) {
        m_hoverPulseTime = std::fmod(m_hoverPulseTime + static_cast<float>(deltaTime), 0.4f);
        repaint();
    }
}

ADSRDisplayComponent::Handle ADSRDisplayComponent::getHandleAtPoint(const NUIPoint& point) const {
    const auto b = getBounds();
    const auto g = calculateADSRGeometry(b, m_attack, m_decay, m_sustain, m_release);
    Handle closest = Handle::None;
    float closestDist = kADSRHandleHitRadius;
    const std::pair<Handle, NUIPoint> handles[] = {
        {Handle::Attack, g.attack},
        {Handle::Decay, g.decay},
        {Handle::Sustain, g.sustain},
        {Handle::Release, g.releaseStart}
    };
    for (const auto& h : handles) {
        const float d = distanceTo(point, h.second);
        if (d <= closestDist) {
            closest = h.first;
            closestDist = d;
        }
    }
    return closest;
}

AestraUI::NUICursorStyle ADSRDisplayComponent::getCursorStyleForPoint(const NUIPoint& point) const {
    const Handle handle = m_draggingHandle != Handle::None ? m_draggingHandle : getHandleAtPoint(point);
    switch (handle) {
        case Handle::Attack:
        case Handle::Decay:
        case Handle::Release:
            return NUICursorStyle::ResizeEW;
        case Handle::Sustain:
            return NUICursorStyle::ResizeNS;
        case Handle::None:
            return NUICursorStyle::Arrow;
    }
    return NUICursorStyle::Arrow;
}

bool ADSRDisplayComponent::onMouseEvent(const NUIMouseEvent& event) {
    const auto b = getBounds();
    const bool inside = event.position.x >= b.x && event.position.x <= b.x + b.width &&
                        event.position.y >= b.y && event.position.y <= b.y + b.height;
    if (!inside && m_draggingHandle == Handle::None) {
        if (m_hoveredHandle != Handle::None) {
            m_hoveredHandle = Handle::None;
            repaint();
        }
        return false;
    }

    const auto g = calculateADSRGeometry(b, m_attack, m_decay, m_sustain, m_release);

    if (event.pressed && event.button == NUIMouseButton::Left) {
        m_draggingHandle = getHandleAtPoint(event.position);
        if (m_draggingHandle != Handle::None) {
            m_hoveredHandle = m_draggingHandle;
            m_hoverPulseTime = 0.0f;
            m_dragStartMouse = event.position;
            m_dragStartAttack = m_attack;
            m_dragStartDecay = m_decay;
            m_dragStartSustain = m_sustain;
            m_dragStartRelease = m_release;
            repaint();
            return true;
        }
        return false;
    }

    if (event.released && event.button == NUIMouseButton::Left) {
        const bool consumed = m_draggingHandle != Handle::None;
        m_draggingHandle = Handle::None;
        if (consumed && onADSRCommitRequested) {
            onADSRCommitRequested();
        }
        repaint();
        return consumed;
    }

    if (m_draggingHandle != Handle::None) {
        const float leftEdge = g.baseX + kADSRHandleHitRadius;
        const float rightEdge = g.baseX + g.graphW - kADSRHandleHitRadius;
        const float handleRange = std::max(1.0f, rightEdge - leftEdge);
        const float dx = event.position.x - m_dragStartMouse.x;
        const float dy = event.position.y - m_dragStartMouse.y;
        const auto startG = calculateADSRGeometry(b, m_dragStartAttack, m_dragStartDecay, m_dragStartSustain, m_dragStartRelease);
        switch (m_draggingHandle) {
            case Handle::Attack:
            {
                const float x = std::clamp(startG.attack.x + dx, leftEdge, rightEdge);
                const float norm = (x - leftEdge) / (handleRange * 0.35f);
                m_attack = adsrNormToTime(norm, kADSRMinAttack, kADSRMaxAttack);
                break;
            }
            case Handle::Decay:
            {
                const float x = std::clamp(startG.decay.x + dx, leftEdge, rightEdge);
                const float norm = (x - startG.attack.x) / (handleRange * 0.32f);
                m_decay = adsrNormToTime(norm, kADSRMinDecay, kADSRMaxDecay);
                break;
            }
            case Handle::Sustain:
                m_sustain = std::clamp(m_dragStartSustain - (dy / std::max(1.0f, g.graphH)), 0.0f, 1.0f);
                break;
            case Handle::Release:
            {
                const float x = std::clamp(startG.releaseStart.x + dx, leftEdge, rightEdge);
                const float norm = (rightEdge - x) / (handleRange * 0.42f);
                m_release = adsrNormToTime(norm, kADSRMinRelease, kADSRMaxRelease);
                break;
            }
            case Handle::None:
                break;
        }
        if (onADSRChanged) {
            onADSRChanged(m_attack, m_decay, m_sustain, m_release);
        }
        repaint();
        return true;
    }

    const Handle newHover = getHandleAtPoint(event.position);
    if (newHover != m_hoveredHandle) {
        m_hoveredHandle = newHover;
        m_hoverPulseTime = 0.0f;
        repaint();
    }
    return newHover != Handle::None;
}

void ADSRDisplayComponent::onMouseLeave() {
    m_hoveredHandle = Handle::None;
    m_hoverPulseTime = 0.0f;
    repaint();
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
    const float radius = theme.getRadius("s");
    renderer.fillRoundedRect(NUIRect(b.x, b.y, b.width, b.height), radius, theme.getColor("recessedPanel"));
    renderer.strokeRoundedRect(b, radius, 1.0f, theme.getColor("borderSubtle"));

    // Center line
    float centerY = b.y + b.height * 0.5f;
    renderer.drawLine(NUIPoint(b.x, centerY), NUIPoint(b.x + b.width, centerY),
                      0.5f, theme.getColor("secondary").withAlpha(0.18f));

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
    NUIColor waveCol = theme.getColor("secondary").withAlpha(0.86f);
    NUIColor waveColNeg = theme.getColor("secondary").withAlpha(0.58f);

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
        NUIColor loopCol = theme.getColor("secondary").withAlpha(0.10f);
        renderer.fillRect(NUIRect(loopX0, b.y, loopX1 - loopX0, b.height), loopCol);

        // Loop start handle
        renderer.drawLine(NUIPoint(loopX0, b.y), NUIPoint(loopX0, b.y + b.height),
                          2.0f, theme.getColor("secondary").withAlpha(0.80f));
        renderer.fillCircle(NUIPoint(loopX0, centerY), 5.0f, theme.getColor("secondary").withAlpha(0.94f));

        // Loop end handle
        renderer.drawLine(NUIPoint(loopX1, b.y), NUIPoint(loopX1, b.y + b.height),
                          2.0f, theme.getColor("secondary").withAlpha(0.80f));
        renderer.fillCircle(NUIPoint(loopX1, centerY), 5.0f, theme.getColor("secondary").withAlpha(0.94f));
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
    setMinimumPanelSize(kSampleEditorMinPanelW, kSampleEditorMinPanelH);
    buildUI();
}

void SampleEditorPanel::buildUI() {
    m_contentContainer = std::make_shared<SampleEditorSurfaceComponent>();
    auto& theme = NUIThemeManager::getInstance();

    // Waveform display
    m_waveformDisplay = std::make_shared<WaveformDisplayComponent>();
    m_waveformDisplay->onZoomChanged = [this](float zoom) {
        m_waveformZoom = zoom;
    };
    m_waveformDisplay->onLoopDragged = [this](float start, float end) {
        m_loopPoints.start = start;
        m_loopPoints.end = end;
        onLoopControlChanged();
        requestControlCommit();
    };

    // ADSR display
    m_adsrDisplay = std::make_shared<ADSRDisplayComponent>();
    m_adsrDisplay->setADSR(m_adsr.attack, m_adsr.decay, m_adsr.sustain, m_adsr.release);
    m_adsrDisplay->onADSRChanged = [this](float attack, float decay, float sustain, float release) {
        onADSRDisplayChanged({attack, decay, sustain, release});
    };
    m_adsrDisplay->onADSRCommitRequested = [this]() {
        requestControlCommit();
    };

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
        label->setFontSize(11.0f);
        label->setTextColor(theme.getColor("textSecondary").withAlpha(0.78f));
        return label;
    };
    auto styleModeButton = [&theme](const std::shared_ptr<NUIButton>& button) {
        button->setStyle(NUIButton::Style::Text);
        button->setFontSize(11.0f);
        button->setCornerRadius(5.0f);
        button->setGlowEnabled(false);
    };
    auto styleActionButton = [&theme](const std::shared_ptr<NUIButton>& button) {
        button->setStyle(NUIButton::Style::Text);
        button->setFontSize(11.0f);
        button->setCornerRadius(5.0f);
        button->setGlowEnabled(false);
        button->setBackgroundColor(theme.getColor("surfaceRaised").withAlpha(0.72f));
        button->setHoverColor(theme.getColor("secondary").withAlpha(0.18f));
        button->setPressedColor(theme.getColor("secondary").withAlpha(0.28f));
        button->setTextColor(theme.getColor("textPrimary").withAlpha(0.82f));
        button->setBorderEnabled(true);
        button->setBorderWidth(1.0f);
        button->setBorderColor(theme.getColor("borderSubtle").withAlpha(0.72f));
    };

    // Pitch/Tune controls
    m_pitchRootSlider = makeSlider("Root", 0.0, 127.0, 60.0);
    m_pitchCoarseSlider = makeSlider("Coarse", -24.0, 24.0, 0.0);
    m_pitchFineSlider = makeSlider("Fine", -100.0, 100.0, 0.0);
    m_voiceCountSlider = makeSlider("Voices", 1.0, 8.0, 4.0);
    m_pitchRootSlider->setSnapValue(1.0);
    m_pitchRootSlider->setSliderThickness(4.0f);
    m_pitchCoarseSlider->setSliderThickness(8.0f);
    m_pitchFineSlider->setSliderThickness(4.0f);
    m_voiceCountSlider->setSliderThickness(4.0f);

    // Action buttons
    m_normalizeBtn = std::make_shared<NUIButton>("Normalize");
    m_reverseBtn = std::make_shared<NUIButton>("Reverse");
    m_oneShotModeBtn = std::make_shared<NUIButton>("One-Shot");
    m_loopModeBtn = std::make_shared<NUIButton>("Loop");
    m_pingPongModeBtn = std::make_shared<NUIButton>("Ping-Pong");
    m_monoModeBtn = std::make_shared<NUIButton>("Mono");
    m_polyModeBtn = std::make_shared<NUIButton>("Poly");
    styleActionButton(m_normalizeBtn);
    styleActionButton(m_reverseBtn);
    styleModeButton(m_oneShotModeBtn);
    styleModeButton(m_loopModeBtn);
    styleModeButton(m_pingPongModeBtn);
    styleModeButton(m_monoModeBtn);
    styleModeButton(m_polyModeBtn);
    m_waveformHintLabel = makeLabel("Scroll to zoom  |  Drag handles to trim");
    m_waveformHintLabel->setTextColor(theme.getColor("textSecondary").withAlpha(0.56f));
    m_waveformHintLabel->setAlignment(NUILabel::Alignment::Right);
    m_modeLabel = makeLabel("PLAYBACK / VOICES");
    m_voiceCountLabel = makeLabel("Voices");
    m_voiceCountValueLabel = makeLabel("4");
    m_voiceCountValueLabel->setAlignment(NUILabel::Alignment::Right);
    m_pitchLabel = makeLabel("KEYBOARD PITCH / ROOT");
    m_pitchRootLabel = makeLabel("Root");
    m_pitchRootValueLabel = makeLabel(midiNoteName(60));
    m_pitchRootValueLabel->setAlignment(NUILabel::Alignment::Right);
    m_pitchCoarseLabel = makeLabel("Coarse");
    m_pitchFineLabel = makeLabel("Fine");
    m_adsrLabel = makeLabel("ENVELOPE");

    // Wire callbacks
    m_oneShotModeBtn->setOnClick([this]() { setLoopMode(LoopMode::OneShot); });
    m_loopModeBtn->setOnClick([this]() { setLoopMode(LoopMode::Loop); });
    m_pingPongModeBtn->setOnClick([this]() { setLoopMode(LoopMode::PingPong); });
    m_monoModeBtn->setOnClick([this]() { setMonoModeInternal(true, true); });
    m_polyModeBtn->setOnClick([this]() { setMonoModeInternal(false, true); });
    updateModeButtons();
    updateMonoPolyControls();

    auto pitchChanged = [this]() { onPitchControlChanged(); };
    m_pitchRootSlider->setOnValueChange([this, pitchChanged](double) { pitchChanged(); });
    m_pitchCoarseSlider->setOnValueChange([this, pitchChanged](double) { pitchChanged(); });
    m_pitchFineSlider->setOnValueChange([this, pitchChanged](double) { pitchChanged(); });
    m_pitchRootSlider->setOnDragEnd([this]() { requestControlCommit(); });
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
    m_contentContainer->addChild(m_waveformHintLabel);
    m_contentContainer->addChild(m_adsrDisplay);
    m_contentContainer->addChild(m_modeLabel);
    m_contentContainer->addChild(m_voiceCountLabel);
    m_contentContainer->addChild(m_voiceCountValueLabel);
    m_contentContainer->addChild(m_pitchLabel);
    m_contentContainer->addChild(m_pitchRootLabel);
    m_contentContainer->addChild(m_pitchRootValueLabel);
    m_contentContainer->addChild(m_pitchCoarseLabel);
    m_contentContainer->addChild(m_pitchFineLabel);
    m_contentContainer->addChild(m_adsrLabel);
    m_contentContainer->addChild(m_oneShotModeBtn);
    m_contentContainer->addChild(m_loopModeBtn);
    m_contentContainer->addChild(m_pingPongModeBtn);
    m_contentContainer->addChild(m_monoModeBtn);
    m_contentContainer->addChild(m_polyModeBtn);
    m_contentContainer->addChild(m_pitchRootSlider);
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

void SampleEditorPanel::loadPreparedSample(const std::string& path, double sampleRate, uint32_t sampleLength,
                                           std::vector<float> waveformData) {
    if (!m_trackManager) {
        return;
    }

    Log::info("[SampleEditor] Loading prepared sample: " + path);
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    m_sampleLength = sampleLength;
    m_waveformData = std::move(waveformData);
    if (m_waveformData.empty()) {
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
    m_adsrDisplay->setADSR(params.attack, params.decay, params.sustain, params.release);
}

void SampleEditorPanel::setLoopPoints(const LoopPoints& lp) {
    m_loopPoints = lp;
    m_waveformDisplay->setLoopPoints(lp.start, lp.end);
    updateModeButtons();
}

void SampleEditorPanel::setPitchTune(const PitchTune& pt) {
    m_pitchTune = pt;
    m_suppressControlCallbacks = true;
    m_pitchRootSlider->setValue(static_cast<double>(pt.rootMidiNote));
    m_pitchCoarseSlider->setValue(static_cast<double>(pt.coarse));
    m_pitchFineSlider->setValue(pt.fine);
    m_suppressControlCallbacks = false;
    m_pitchRootValueLabel->setText(midiNoteName(pt.rootMidiNote));
}

void SampleEditorPanel::setVoiceCount(int voices) {
    if (m_voiceCountSlider) {
        m_suppressControlCallbacks = true;
        m_voiceCountSlider->setValue(static_cast<double>(std::clamp(voices, 1, 8)));
        m_suppressControlCallbacks = false;
    }
    updateMonoPolyControls();
}

int SampleEditorPanel::getVoiceCount() const {
    return m_voiceCountSlider ? static_cast<int>(std::round(m_voiceCountSlider->getValue())) : 4;
}

void SampleEditorPanel::setMonoMode(bool mono) {
    setMonoModeInternal(mono, false);
}

void SampleEditorPanel::onResize(int width, int height) {
    auto panelBounds = getBounds();
    if (panelBounds.width < kSampleEditorMinPanelW || panelBounds.height < kSampleEditorMinPanelH) {
        setBounds(NUIRect(
            panelBounds.x,
            panelBounds.y,
            std::max(panelBounds.width, kSampleEditorMinPanelW),
            std::max(panelBounds.height, kSampleEditorMinPanelH)
        ));
        return;
    }

    WindowPanel::onResize(width, height);
    if (!m_contentContainer) {
        return;
    }

    const auto cb = m_contentContainer->getBounds();
    const float pad = 8.0f;
    const float gutter = 6.0f;
    const float labelH = 14.0f;
    const float rowH = 22.0f;
    const float layoutW = std::max(cb.width, kSampleEditorMinPanelW);
    const float layoutH = std::max(cb.height, kSampleEditorMinContentH);
    const float halfW = std::max(80.0f, (layoutW - (pad * 2.0f) - gutter) * 0.5f);
    const float contentW = layoutW - pad * 2.0f;
    const float fixedStackH = labelH + gutter + kSampleEditorModeRowH + gutter + kSampleEditorPitchRowH +
                              gutter + labelH + kSampleEditorADSRH + gutter + kSampleEditorButtonRowH + pad * 2.0f;
    const float waveformH = std::max(kSampleEditorWaveformMinH, layoutH - fixedStackH);

    float y = cb.y + pad;
    m_waveformDisplay->setBounds(NUIRect(cb.x + pad, y, contentW, waveformH));
    y += waveformH + 3.0f;
    m_waveformHintLabel->setBounds(NUIRect(cb.x + pad, y, contentW, labelH));
    y += labelH + gutter;

    const float modeRowY = y;
    m_modeLabel->setBounds(NUIRect(cb.x + pad, modeRowY, contentW, labelH));
    y = modeRowY + labelH;
    const float monoBtnW = 52.0f;
    const float monoGroupW = monoBtnW * 2.0f - 1.0f;
    const float voiceValueW = 24.0f;
    const float voiceSliderW = std::min(132.0f, std::max(84.0f, contentW * 0.20f));
    const float voiceLabelW = 42.0f;
    const float voiceGroupW = voiceLabelW + 4.0f + voiceSliderW + 4.0f + voiceValueW;
    const float modeAvailableW = std::max(168.0f, contentW - monoGroupW - voiceGroupW - gutter * 3.0f);
    const float modeBtnW = std::max(56.0f, std::min(82.0f, modeAvailableW / 3.0f));
    float x = cb.x + pad;
    m_monoModeBtn->setBounds(NUIRect(x, y, monoBtnW, rowH));
    x += monoBtnW - 1.0f;
    m_polyModeBtn->setBounds(NUIRect(x, y, monoBtnW, rowH));
    x += monoBtnW + gutter;
    m_oneShotModeBtn->setBounds(NUIRect(x, y, modeBtnW, rowH));
    x += modeBtnW - 1.0f;
    m_loopModeBtn->setBounds(NUIRect(x, y, modeBtnW, rowH));
    x += modeBtnW - 1.0f;
    m_pingPongModeBtn->setBounds(NUIRect(x, y, modeBtnW, rowH));
    const float voiceX = cb.x + layoutW - pad - voiceGroupW;
    m_voiceCountLabel->setBounds(NUIRect(voiceX, y + 4.0f, voiceLabelW, labelH));
    m_voiceCountSlider->setBounds(NUIRect(voiceX + voiceLabelW + 4.0f, y, voiceSliderW, rowH));
    m_voiceCountValueLabel->setBounds(NUIRect(voiceX + voiceLabelW + 4.0f + voiceSliderW + 4.0f, y + 4.0f, voiceValueW, labelH));
    y = modeRowY + kSampleEditorModeRowH + gutter;

    const float pitchRowY = y;
    m_pitchLabel->setBounds(NUIRect(cb.x + pad, pitchRowY, contentW, labelH));
    y = pitchRowY + labelH;
    const float pitchLabelW = 56.0f;
    const float pitchValueW = 64.0f;
    m_pitchRootLabel->setBounds(NUIRect(cb.x + pad, y + 4.0f, pitchLabelW, labelH));
    m_pitchRootSlider->setBounds(
        NUIRect(cb.x + pad + pitchLabelW + gutter, y,
                contentW - pitchLabelW - pitchValueW - gutter * 2.0f, rowH));
    m_pitchRootValueLabel->setBounds(
        NUIRect(cb.x + layoutW - pad - pitchValueW, y + 4.0f, pitchValueW, labelH));
    y += rowH + gutter;
    m_pitchCoarseLabel->setBounds(NUIRect(cb.x + pad, y + 4.0f, pitchLabelW, labelH));
    m_pitchCoarseSlider->setBounds(NUIRect(cb.x + pad + pitchLabelW + gutter, y, contentW - pitchLabelW - gutter, rowH));
    y += rowH + gutter;
    m_pitchFineLabel->setBounds(NUIRect(cb.x + pad, y + 4.0f, pitchLabelW, labelH));
    m_pitchFineSlider->setBounds(NUIRect(cb.x + pad + pitchLabelW + gutter, y, contentW - pitchLabelW - gutter, rowH));
    y = pitchRowY + kSampleEditorPitchRowH + gutter;

    m_adsrLabel->setBounds(NUIRect(cb.x + pad, y, contentW, labelH));
    y += labelH;
    m_adsrDisplay->setBounds(NUIRect(cb.x + pad, y, contentW, kSampleEditorADSRH));
    y += kSampleEditorADSRH + gutter;

    const float buttonH = 32.0f;
    const float buttonY = y + (kSampleEditorButtonRowH - buttonH) * 0.5f;
    m_normalizeBtn->setBounds(NUIRect(cb.x + pad, buttonY, halfW, buttonH));
    m_reverseBtn->setBounds(NUIRect(cb.x + pad + halfW + gutter, buttonY, halfW, buttonH));
}

AestraUI::NUICursorStyle SampleEditorPanel::getResizeCursorStyleForPoint(const AestraUI::NUIPoint& point) const {
    if (m_adsrDisplay && m_adsrDisplay->isVisible()) {
        const auto adsrStyle = m_adsrDisplay->getCursorStyleForPoint(point);
        if (adsrStyle != AestraUI::NUICursorStyle::Arrow) {
            return adsrStyle;
        }
    }
    return WindowPanel::getResizeCursorStyleForPoint(point);
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

void SampleEditorPanel::onADSRDisplayChanged(const ADSRParams& params) {
    if (m_suppressControlCallbacks) {
        return;
    }
    m_adsr = params;
    if (onADSRChanged) onADSRChanged(m_adsr);
}

void SampleEditorPanel::onLoopControlChanged() {
    if (m_suppressControlCallbacks) {
        return;
    }
    m_waveformDisplay->setLoopPoints(m_loopPoints.start, m_loopPoints.end);

    if (onLoopPointsChanged) onLoopPointsChanged(m_loopPoints);
}

void SampleEditorPanel::setLoopMode(LoopMode mode) {
    if (m_suppressControlCallbacks) {
        return;
    }
    m_loopPoints.mode = mode;
    updateModeButtons();
    onLoopControlChanged();
    requestControlCommit();
}

void SampleEditorPanel::updateModeButtons() {
    auto& theme = NUIThemeManager::getInstance();
    const auto activeBg = theme.getColor("secondary").withAlpha(0.78f);
    const auto inactiveBg = NUIColor::transparent();
    const auto hoverBg = theme.getColor("secondary").withAlpha(0.18f);
    const auto borderCol = theme.getColor("secondary").withAlpha(0.40f);
    const auto activeText = theme.getColor("textPrimary").withAlpha(0.96f);
    const auto inactiveText = theme.getColor("textSecondary").withAlpha(0.72f);

    auto styleButton = [&](const std::shared_ptr<NUIButton>& button, LoopMode mode) {
        if (!button) {
            return;
        }
        const bool active = m_loopPoints.mode == mode;
        button->setBackgroundColor(active ? activeBg : inactiveBg);
        button->setHoverColor(active ? activeBg : hoverBg);
        button->setPressedColor(theme.getColor("secondary").withAlpha(0.9f));
        button->setTextColor(active ? activeText : inactiveText);
        button->setCornerRadius(4.0f);
        button->setBorderEnabled(true);
        button->setBorderWidth(1.0f);
        button->setBorderColor(active ? theme.getColor("secondary").withAlpha(0.72f) : borderCol);
    };

    styleButton(m_oneShotModeBtn, LoopMode::OneShot);
    styleButton(m_loopModeBtn, LoopMode::Loop);
    styleButton(m_pingPongModeBtn, LoopMode::PingPong);
}

void SampleEditorPanel::setMonoModeInternal(bool mono, bool notify) {
    if (m_monoMode == mono) {
        updateMonoPolyControls();
        return;
    }

    m_monoMode = mono;
    updateMonoPolyControls();

    if (notify && !m_suppressControlCallbacks) {
        if (onMonoModeChanged) onMonoModeChanged(m_monoMode);
        requestControlCommit();
    }
}

void SampleEditorPanel::updateMonoPolyControls() {
    auto& theme = NUIThemeManager::getInstance();
    const auto activeBg = theme.getColor("secondary").withAlpha(0.78f);
    const auto inactiveBg = NUIColor::transparent();
    const auto hoverBg = theme.getColor("secondary").withAlpha(0.18f);
    const auto activeText = theme.getColor("textPrimary").withAlpha(0.96f);
    const auto inactiveText = theme.getColor("textSecondary").withAlpha(0.72f);
    const auto inactiveBorder = theme.getColor("secondary").withAlpha(0.40f);

    auto styleButton = [&](const std::shared_ptr<NUIButton>& button, bool active) {
        if (!button) return;
        button->setBackgroundColor(active ? activeBg : inactiveBg);
        button->setHoverColor(active ? activeBg : hoverBg);
        button->setPressedColor(theme.getColor("secondary").withAlpha(0.9f));
        button->setTextColor(active ? activeText : inactiveText);
        button->setCornerRadius(4.0f);
        button->setBorderEnabled(true);
        button->setBorderWidth(1.0f);
        button->setBorderColor(active ? theme.getColor("secondary").withAlpha(0.72f) : inactiveBorder);
    };

    styleButton(m_monoModeBtn, m_monoMode);
    styleButton(m_polyModeBtn, !m_monoMode);

    const int voices = getVoiceCount();
    if (m_voiceCountValueLabel) {
        m_voiceCountValueLabel->setText(std::to_string(voices));
        m_voiceCountValueLabel->setTextColor(theme.getColor("textSecondary").withAlpha(m_monoMode ? 0.34f : 0.86f));
    }
    if (m_voiceCountLabel) {
        m_voiceCountLabel->setTextColor(theme.getColor("textSecondary").withAlpha(m_monoMode ? 0.34f : 0.78f));
    }
    if (m_voiceCountSlider) {
        m_voiceCountSlider->setEnabled(!m_monoMode);
        if (m_monoMode) {
            m_voiceCountSlider->setTrackColor(theme.getColor("surfaceRaised").withAlpha(0.32f));
            m_voiceCountSlider->setFillColor(theme.getColor("textSecondary").withAlpha(0.16f));
            m_voiceCountSlider->setThumbColor(theme.getColor("textSecondary").withAlpha(0.28f));
            m_voiceCountSlider->setThumbHoverColor(theme.getColor("textSecondary").withAlpha(0.28f));
        } else {
            m_voiceCountSlider->setTrackColor(theme.getColor("surfaceRaised").withAlpha(0.86f));
            m_voiceCountSlider->setFillColor(theme.getColor("secondary").withAlpha(0.92f));
            m_voiceCountSlider->setThumbColor(theme.getColor("textPrimary").withAlpha(0.94f));
            m_voiceCountSlider->setThumbHoverColor(theme.getColor("textPrimary"));
        }
    }
}

void SampleEditorPanel::onPitchControlChanged() {
    if (m_suppressControlCallbacks) {
        return;
    }
    m_pitchTune.rootMidiNote = static_cast<int>(std::round(m_pitchRootSlider->getValue()));
    m_pitchTune.coarse = static_cast<int>(m_pitchCoarseSlider->getValue());
    m_pitchTune.fine = static_cast<float>(m_pitchFineSlider->getValue());
    m_pitchRootValueLabel->setText(midiNoteName(m_pitchTune.rootMidiNote));

    if (onPitchTuneChanged) onPitchTuneChanged(m_pitchTune);
}

void SampleEditorPanel::onVoiceCountControlChanged() {
    if (m_suppressControlCallbacks) {
        return;
    }
    updateMonoPolyControls();
    if (m_monoMode) {
        return;
    }
    if (onVoiceCountChanged) {
        onVoiceCountChanged(static_cast<int>(std::round(m_voiceCountSlider->getValue())));
    }
}

} // namespace Audio
} // namespace Aestra
