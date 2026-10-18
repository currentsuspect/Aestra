// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "SampleEditorPanel.h"
#include <array>
#include <cstdio>
#include "Widgets/FlatSlider.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "NUILabel.h"
#include "NUITextInput.h"
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
constexpr float kSampleEditorMinPanelW = 700.0f;
constexpr float kSampleEditorMinContentH = 440.0f;
constexpr float kSampleEditorMinPanelH = kSampleEditorMinContentH + 28.0f;
constexpr float kSampleEditorModeRowH = 36.0f;
constexpr float kSampleEditorPitchRowH = 92.0f;
constexpr float kSampleEditorADSRH = 140.0f;
constexpr float kSampleEditorButtonRowH = 36.0f;
constexpr float kSampleEditorWaveformMinH = 130.0f;
constexpr float kSampleEditorColumnsH = 196.0f; // the three columns under the waveform: Play, Pitch, Envelope
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

std::string signedLabel(int value, const char* unit) {
    return (value >= 0 ? "+" : "") + std::to_string(value) + unit;
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

        // One panel colour, then rules: a line above the three columns, one between each, and one under each title.
        // No cards inside the panel; the waveform and the envelope sit on the darker bed they draw themselves.
        renderer.fillRect(b, theme.getColor("backgroundSecondary"));
        const NUIColor rule = theme.getColor("borderSubtle");
        const float top = b.bottom() - kSampleEditorColumnsH;
        renderer.fillRect({b.x, top, b.width, 1.0f}, rule);
        const float pad = 16.0f;
        const float c1 = b.x + b.width * 0.297f, c2 = b.x + b.width * 0.648f;
        renderer.fillRect({c1, top, 1.0f, kSampleEditorColumnsH}, rule);
        renderer.fillRect({c2, top, 1.0f, kSampleEditorColumnsH}, rule);
        const float titleRule = top + 30.0f;
        renderer.fillRect({b.x + pad, titleRule, c1 - b.x - pad * 2.0f, 1.0f}, rule);
        renderer.fillRect({c1 + pad, titleRule, c2 - c1 - pad * 2.0f, 1.0f}, rule);
        renderer.fillRect({c2 + pad, titleRule, b.right() - c2 - pad * 2.0f, 1.0f}, rule);

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
    const float radius = std::min(theme.getRadius("s"), 2.0f);
    renderer.fillRoundedRect(NUIRect(b.x, b.y, b.width, b.height), radius, theme.getColor("recessedPanel"));

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
    renderer.fillRect(b, theme.getColor("backgroundPrimary")); // the dark bed, sharp: no radius, no outline

    // Time ruler along the top, when the length is known: five labels over the visible range.
    const float rulerH = m_durationSeconds > 0.0 ? 16.0f : 0.0f;
    if (rulerH > 0.0f) {
        const float vs = m_scrollOffset, ve = std::min(1.0f, m_scrollOffset + 1.0f / m_zoom);
        const NUIColor tick = theme.getColor("textSecondary").withAlpha(0.45f);
        for (int i = 0; i <= 4; ++i) {
            const float f = static_cast<float>(i) / 4.0f;
            const float x = b.x + f * b.width;
            const double sec = (vs + f * (ve - vs)) * m_durationSeconds;
            char text[24];
            if (sec >= 60.0) std::snprintf(text, sizeof text, "%d:%02d", static_cast<int>(sec) / 60, static_cast<int>(sec) % 60);
            else std::snprintf(text, sizeof text, sec < 10.0 ? "%.2f" : "%.1f", sec);
            renderer.fillRect({std::min(x, b.right() - 1.0f), b.y + rulerH - 4.0f, 1.0f, 4.0f}, tick);
            const float tw = renderer.measureText(text, 10.0f).width;
            const float tx = i == 0 ? b.x + 12.0f : (i == 4 ? b.right() - tw - 12.0f : x - tw * 0.5f);
            renderer.drawText(text, {tx, b.y + 1.0f}, 10.0f, theme.getColor("textSecondary").withAlpha(0.75f));
        }
    }

    // Center line
    const float areaTop = b.y + rulerH;
    const float areaH = b.height - rulerH;
    float centerY = areaTop + areaH * 0.5f;
    renderer.drawLine(NUIPoint(b.x, centerY), NUIPoint(b.x + b.width, centerY),
                      0.5f, m_color.a > 0.0f ? m_color.withAlpha(0.28f) : theme.getColor("textSecondary").withAlpha(0.28f));

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

    const size_t totalPairs = m_waveformData.size() / 2;
    const float pixelsPerSample = b.width / (viewEnd - viewStart);

    // One crisp column per pixel: the extreme min and max of every data pair that lands in it, drawn as a filled
    // bar, so the shape reads the same at any zoom (hairlines per data pair shimmered and left gaps). A silent
    // column still shows a 1 px line so the file never looks empty. The signal keeps the audio hue.
    const NUIColor signal = m_color.a > 0.0f ? m_color : NUIColor::fromHex(0xffd8d0c2); // channel colour, else bone
    const NUIColor waveCol = signal.withAlpha(0.92f);
    const float halfHeight = areaH * 0.46f;
    const int columns = static_cast<int>(std::ceil(b.width));
    for (int px = 0; px < columns; ++px) {
        const float n0 = viewStart + (static_cast<float>(px) / b.width) * (viewEnd - viewStart);
        const float n1 = viewStart + (static_cast<float>(px + 1) / b.width) * (viewEnd - viewStart);
        size_t i0 = static_cast<size_t>(std::max(0.0f, n0) * static_cast<float>(totalPairs));
        size_t i1 = static_cast<size_t>(std::max(0.0f, n1) * static_cast<float>(totalPairs));
        i0 = std::min(i0, totalPairs - 1);
        i1 = std::clamp(i1, i0 + 1, totalPairs);
        float hi = 0.0f, lo = 0.0f;
        for (size_t i = i0; i < i1; ++i) {
            hi = std::max(hi, m_waveformData[i * 2]);
            lo = std::min(lo, m_waveformData[i * 2 + 1]);
        }
        const float yTop = centerY - hi * halfHeight;
        const float yBottom = centerY - lo * halfHeight;
        renderer.fillRect(NUIRect(b.x + static_cast<float>(px), yTop, 1.0f, std::max(1.0f, yBottom - yTop)), waveCol);
    }

    // Draw loop region overlay
    if (m_loopEnd > m_loopStart) {
        float loopX0 = b.x + (m_loopStart - viewStart) * pixelsPerSample;
        float loopX1 = b.x + (m_loopEnd - viewStart) * pixelsPerSample;
        // The part outside the loop is dimmed; a loop that covers everything leaves the bed untouched.
        const NUIColor dim = theme.getColor("backgroundPrimary").withAlpha(0.62f);
        if (loopX0 > b.x) renderer.fillRect(NUIRect(b.x, areaTop, std::min(loopX0, b.right()) - b.x, areaH), dim);
        if (loopX1 < b.right()) renderer.fillRect(NUIRect(std::max(loopX1, b.x), areaTop, b.right() - std::max(loopX1, b.x), areaH), dim);

        // Loop start handle
        renderer.fillRect(NUIRect(loopX0, b.y, 2.0f, b.height), theme.getColor("secondary").withAlpha(0.80f));
        renderer.fillRect(NUIRect(loopX0 - 1.0f, b.y, 8.0f, 14.0f), theme.getColor("secondary").withAlpha(0.94f)); // a square tab

        // Loop end handle
        renderer.fillRect(NUIRect(loopX1 - 2.0f, b.y, 2.0f, b.height), theme.getColor("secondary").withAlpha(0.80f));
        renderer.fillRect(NUIRect(loopX1 - 7.0f, b.y, 8.0f, 14.0f), theme.getColor("secondary").withAlpha(0.94f));
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

    auto makeSlider = [](const std::string& label, double min, double max, double initial, bool bipolar = false) {
        auto slider = std::make_shared<FlatSlider>(bipolar);
        slider->setOrientation(NUISlider::Orientation::Horizontal);
        slider->setTextBoxVisible(false);
        slider->setSliderRadius(3.0f);
        slider->setSliderThickness(3.0f);
        slider->setRange(min, max);
        slider->setValue(initial);
        slider->setDefaultValue(initial);
        slider->setDoubleClickReturnValue(true, initial); // double-click returns to where it started
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
        button->setCornerRadius(2.0f);
        button->setGlowEnabled(false);
    };
    auto styleActionButton = [&theme](const std::shared_ptr<NUIButton>& button) {
        button->setStyle(NUIButton::Style::Text);
        button->setFontSize(11.0f);
        button->setCornerRadius(2.0f);
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
    m_pitchCoarseSlider = makeSlider("Coarse", -12.0, 12.0, 0.0, true);
    m_pitchFineSlider = makeSlider("Fine", -100.0, 100.0, 0.0, true);
    m_voiceCountSlider = makeSlider("Voices", 1.0, 8.0, 4.0);
    m_pitchRootSlider->setSnapValue(1.0);
    m_pitchRootSlider->setSliderThickness(3.0f); // one track weight on every slider
    m_pitchCoarseSlider->setSliderThickness(3.0f);
    m_pitchFineSlider->setSliderThickness(3.0f);
    m_voiceCountSlider->setSliderThickness(3.0f);

    // Action buttons
    m_normalizeBtn = std::make_shared<NUIButton>("Normalize");
    m_reverseBtn = std::make_shared<NUIButton>("Reverse");
    m_oneShotModeBtn = std::make_shared<NUIButton>("One-Shot");
    m_loopModeBtn = std::make_shared<NUIButton>("Loop");
    m_pingPongModeBtn = std::make_shared<NUIButton>("Ping-Pong");
    m_monoModeBtn = std::make_shared<NUIButton>("Mono");
    m_polyModeBtn = std::make_shared<NUIButton>("Poly");
    m_cutSelfModeBtn = std::make_shared<NUIButton>("Cut-Self");
    m_keepLengthBtn = std::make_shared<NUIButton>("Keep length");
    m_keepLengthBtn->setTooltip("Pitch notes without speeding up or slowing down the sample");
    styleActionButton(m_normalizeBtn);
    styleActionButton(m_reverseBtn);
    styleModeButton(m_oneShotModeBtn);
    styleModeButton(m_loopModeBtn);
    styleModeButton(m_pingPongModeBtn);
    styleModeButton(m_monoModeBtn);
    styleModeButton(m_polyModeBtn);
    styleModeButton(m_cutSelfModeBtn);
    styleModeButton(m_keepLengthBtn);
    m_waveformHintLabel = makeLabel("Scroll to zoom  |  drag the tabs to trim");
    m_waveformHintLabel->setTextColor(theme.getColor("textSecondary").withAlpha(0.56f));
    m_waveformHintLabel->setAlignment(NUILabel::Alignment::Left);
    m_modeLabel = makeLabel("PLAY");
    m_voiceCountLabel = makeLabel("Voices");
    m_voiceCountValueLabel = makeLabel("4");
    m_voiceCountValueLabel->setAlignment(NUILabel::Alignment::Right);
    m_pitchLabel = makeLabel("PITCH");
    m_pitchRootLabel = makeLabel("Root");
    m_pitchRootValueLabel = makeLabel(midiNoteName(60));
    m_pitchRootValueLabel->setAlignment(NUILabel::Alignment::Right);
    m_pitchCoarseLabel = makeLabel("Coarse");
    m_pitchFineLabel = makeLabel("Fine");
    m_pitchCoarseValueLabel = makeLabel("+0 st");
    m_pitchFineValueLabel = makeLabel("+0 ct");
    m_pitchCoarseValueLabel->setAlignment(NUILabel::Alignment::Right);
    m_pitchFineValueLabel->setAlignment(NUILabel::Alignment::Right);
    m_adsrLabel = makeLabel("ENVELOPE");
    for (int i = 0; i < 4; ++i) {
        static const char* captions[4] = {"A", "D", "S", "R"};
        m_adsrFieldCaptions[static_cast<size_t>(i)] = makeLabel(captions[i]);
        m_adsrFieldCaptions[static_cast<size_t>(i)]->setFontSize(9.5f);
        auto field = std::make_shared<NUITextInput>();
        field->setBorderRadius(0.0f);
        field->setBorderWidth(0.0f);
        field->setBackgroundVisible(true);
        field->setBackgroundColor(theme.getColor("backgroundPrimary"));
        field->setTextColor(theme.getColor("textPrimary"));
        field->setCaretColor(theme.getColor("textPrimary"));
        field->setSelectionColor(theme.getColor("accentPrimary").withAlpha(0.45f));
        field->setFocusedBorderColor(theme.getColor("accentPrimary"));
        field->setPadding(6.0f);
        field->setMaxLength(10);
        field->setOnReturnKey([this, i]() { commitADSRField(i); });
        field->setOnFocusLost([this, i]() { commitADSRField(i); });
        m_adsrFields[static_cast<size_t>(i)] = field;
    }

    // Wire callbacks
    m_oneShotModeBtn->setOnClick([this]() { setLoopMode(LoopMode::OneShot); });
    m_loopModeBtn->setOnClick([this]() { setLoopMode(LoopMode::Loop); });
    m_pingPongModeBtn->setOnClick([this]() { setLoopMode(LoopMode::PingPong); });
    m_monoModeBtn->setOnClick([this]() { setMonoModeInternal(true, true); });
    m_polyModeBtn->setOnClick([this]() { setMonoModeInternal(false, true); });
    m_cutSelfModeBtn->setOnClick([this]() { setCutSelfModeInternal(!m_cutSelfMode, true); });
    m_keepLengthBtn->setOnClick([this]() { setKeepLengthPitchInternal(!m_keepLengthPitch, true); });
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
    m_contentContainer->addChild(m_pitchCoarseValueLabel);
    m_contentContainer->addChild(m_pitchFineValueLabel);
    m_contentContainer->addChild(m_pitchCoarseLabel);
    m_contentContainer->addChild(m_pitchFineLabel);
    m_contentContainer->addChild(m_adsrLabel);
    for (int i = 0; i < 4; ++i) { m_contentContainer->addChild(m_adsrFieldCaptions[static_cast<size_t>(i)]); m_contentContainer->addChild(m_adsrFields[static_cast<size_t>(i)]); }
    refreshADSRFields();
    m_contentContainer->addChild(m_oneShotModeBtn);
    m_contentContainer->addChild(m_loopModeBtn);
    m_contentContainer->addChild(m_pingPongModeBtn);
    m_contentContainer->addChild(m_monoModeBtn);
    m_contentContainer->addChild(m_polyModeBtn);
    m_contentContainer->addChild(m_cutSelfModeBtn);
    m_contentContainer->addChild(m_keepLengthBtn);
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
    m_waveformDisplay->setDurationSeconds(m_sampleRate > 0.0 ? static_cast<double>(m_sampleLength) / m_sampleRate : 0.0);
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
    m_waveformDisplay->setDurationSeconds(m_sampleRate > 0.0 ? static_cast<double>(m_sampleLength) / m_sampleRate : 0.0);
    m_waveformDisplay->repaint();
    m_adsrDisplay->repaint();
}

void SampleEditorPanel::setWaveformColor(const AestraUI::NUIColor& color) {
    if (m_waveformDisplay) m_waveformDisplay->setColor(color);
}

namespace {
// "10 ms", "1.20 s", "100 %": the typed envelope fields show times in ms below a second, and read ms, s or % back.
std::string formatEnvelopeTime(float seconds) {
    char buf[24];
    if (seconds >= 1.0f) std::snprintf(buf, sizeof buf, "%.2f s", seconds);
    else std::snprintf(buf, sizeof buf, "%d ms", static_cast<int>(std::lround(seconds * 1000.0f)));
    return buf;
}
bool parseEnvelopeValue(const std::string& text, bool percent, float& out) {
    char* end = nullptr;
    const float number = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(number)) return false;
    std::string unit(end);
    unit.erase(std::remove_if(unit.begin(), unit.end(), [](unsigned char c) { return std::isspace(c); }), unit.end());
    std::transform(unit.begin(), unit.end(), unit.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (percent) { out = number / 100.0f; return true; }
    out = (unit == "s") ? number : number / 1000.0f; // a bare number or "ms" is milliseconds
    return true;
}
} // namespace

void SampleEditorPanel::refreshADSRFields() {
    const std::array<std::string, 4> texts{formatEnvelopeTime(m_adsr.attack), formatEnvelopeTime(m_adsr.decay),
                                           std::to_string(static_cast<int>(std::lround(m_adsr.sustain * 100.0f))) + " %",
                                           formatEnvelopeTime(m_adsr.release)};
    for (size_t i = 0; i < m_adsrFields.size(); ++i)
        if (m_adsrFields[i] && !m_adsrFields[i]->isFocused()) m_adsrFields[i]->setText(texts[i]);
}

void SampleEditorPanel::commitADSRField(int index) {
    if (index < 0 || index > 3 || !m_adsrFields[static_cast<size_t>(index)]) return;
    float value = 0.0f;
    if (parseEnvelopeValue(m_adsrFields[static_cast<size_t>(index)]->getText(), index == 2, value)) {
        ADSRParams next = m_adsr;
        switch (index) {
            case 0: next.attack = std::clamp(value, kADSRMinAttack, kADSRMaxAttack); break;
            case 1: next.decay = std::clamp(value, kADSRMinDecay, kADSRMaxDecay); break;
            case 2: next.sustain = std::clamp(value, 0.0f, 1.0f); break;
            default: next.release = std::clamp(value, kADSRMinRelease, kADSRMaxRelease); break;
        }
        m_adsr = next;
        m_adsrDisplay->setADSR(next.attack, next.decay, next.sustain, next.release);
        if (onADSRChanged) onADSRChanged(m_adsr);
        requestControlCommit();
    }
    // Whatever was typed, show the value that is in force (a bad entry snaps back).
    m_adsrFields[static_cast<size_t>(index)]->setFocused(false);
    refreshADSRFields();
}

void SampleEditorPanel::setADSR(const ADSRParams& params) {
    m_adsr = params;
    m_adsrDisplay->setADSR(params.attack, params.decay, params.sustain, params.release);
    refreshADSRFields();
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
    m_pitchCoarseValueLabel->setText(signedLabel(pt.coarse, " st"));
    m_pitchFineValueLabel->setText(signedLabel(static_cast<int>(std::lround(pt.fine)), " ct"));
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
    const float pad = 16.0f;
    const float layoutW = std::max(cb.width, kSampleEditorMinPanelW);
    const float layoutH = std::max(cb.height, kSampleEditorMinContentH);
    const float contentW = layoutW - pad * 2.0f;

    // The sound first: the waveform takes everything the three columns do not need.
    const float hintH = 28.0f;
    const float waveformH = std::max(kSampleEditorWaveformMinH, layoutH - kSampleEditorColumnsH - hintH - 12.0f);
    m_waveformDisplay->setBounds(NUIRect(cb.x + pad, cb.y + 12.0f, contentW, waveformH));
    const float hintY = cb.y + 12.0f + waveformH + 6.0f;
    m_waveformHintLabel->setBounds(NUIRect(cb.x + pad, hintY + 4.0f, contentW * 0.5f, 14.0f));
    // Normalize and Reverse are one-off actions: small flat keys at the right of the hint line.
    const float keyH = 22.0f;
    m_reverseBtn->setBounds(NUIRect(cb.x + layoutW - pad - 74.0f, hintY, 74.0f, keyH));
    m_normalizeBtn->setBounds(NUIRect(cb.x + layoutW - pad - 74.0f - 6.0f - 84.0f, hintY, 84.0f, keyH));

    // Three columns, 0.297 : 0.351 : 0.351 of the width, each under a titled rule.
    const float top = cb.y + layoutH - kSampleEditorColumnsH;
    const float c1 = cb.x + layoutW * 0.297f, c2 = cb.x + layoutW * 0.648f;
    const float labelH = 14.0f;
    const float rowH = 24.0f;
    const float rowTop = top + 42.0f;
    const float rowStep = 32.0f;

    // Play
    const float playX = cb.x + pad, playW = c1 - cb.x - pad * 2.0f;
    m_modeLabel->setBounds(NUIRect(playX, top + 10.0f, playW, labelH));
    const float segW = (playW - 2.0f) / 3.0f;
    const std::array<std::shared_ptr<NUIButton>, 3> modeRow1{m_polyModeBtn, m_monoModeBtn, m_cutSelfModeBtn};
    const std::array<std::shared_ptr<NUIButton>, 3> modeRow2{m_oneShotModeBtn, m_loopModeBtn, m_pingPongModeBtn};
    for (size_t i = 0; i < 3; ++i) {
        modeRow1[i]->setBounds(NUIRect(playX + static_cast<float>(i) * (segW + 1.0f), rowTop, segW, rowH));
        modeRow2[i]->setBounds(NUIRect(playX + static_cast<float>(i) * (segW + 1.0f), rowTop + 30.0f, segW, rowH));
    }
    const float voiceY = rowTop + 68.0f;
    const float voiceValueW = 24.0f, voiceLabelW = 46.0f;
    m_voiceCountLabel->setBounds(NUIRect(playX, voiceY + 4.0f, voiceLabelW, labelH));
    m_voiceCountSlider->setBounds(NUIRect(playX + voiceLabelW, voiceY, playW - voiceLabelW - voiceValueW - 8.0f, rowH));
    m_voiceCountValueLabel->setBounds(NUIRect(playX + playW - voiceValueW, voiceY + 4.0f, voiceValueW, labelH));

    // Pitch
    const float pitchX = c1 + pad, pitchW = c2 - c1 - pad * 2.0f;
    m_pitchLabel->setBounds(NUIRect(pitchX, top + 10.0f, pitchW * 0.4f, labelH));
    m_keepLengthBtn->setBounds(NUIRect(pitchX + pitchW - 84.0f, top + 5.0f, 84.0f, 20.0f));
    const float pitchLabelW = 52.0f, pitchValueW = 56.0f;
    const auto pitchRow = [&](float y, const auto& label, const auto& slider, const auto& value) {
        label->setBounds(NUIRect(pitchX, y + 4.0f, pitchLabelW, labelH));
        slider->setBounds(NUIRect(pitchX + pitchLabelW, y, pitchW - pitchLabelW - pitchValueW - 8.0f, rowH));
        value->setBounds(NUIRect(pitchX + pitchW - pitchValueW, y + 4.0f, pitchValueW, labelH));
    };
    pitchRow(rowTop, m_pitchRootLabel, m_pitchRootSlider, m_pitchRootValueLabel);
    pitchRow(rowTop + rowStep, m_pitchCoarseLabel, m_pitchCoarseSlider, m_pitchCoarseValueLabel);
    pitchRow(rowTop + rowStep * 2.0f, m_pitchFineLabel, m_pitchFineSlider, m_pitchFineValueLabel);

    // Envelope
    const float envX = c2 + pad, envW = cb.x + layoutW - c2 - pad * 2.0f;
    m_adsrLabel->setBounds(NUIRect(envX, top + 10.0f, envW, labelH));
    const float fieldsH = 24.0f, captionH = 12.0f;
    const float fieldsY = top + kSampleEditorColumnsH - 14.0f - fieldsH;
    m_adsrDisplay->setBounds(NUIRect(envX, top + 40.0f, envW, fieldsY - captionH - 4.0f - (top + 40.0f)));
    const float fieldW = (envW - 3.0f) / 4.0f;
    for (size_t i = 0; i < 4; ++i) {
        const float fx = envX + static_cast<float>(i) * (fieldW + 1.0f);
        m_adsrFieldCaptions[i]->setBounds(NUIRect(fx + 2.0f, fieldsY - captionH, fieldW, captionH));
        m_adsrFields[i]->setBounds(NUIRect(fx, fieldsY, fieldW, fieldsH));
    }
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
    refreshADSRFields();
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
    // Active interaction intent is the Aestra accent (DESIGN.md color
    // semantics); the previous secondary (electric blue) token violated it.
    const auto activeBg = theme.getColor("accentPrimary"); // the active choice is solid violet
    const auto inactiveBg = theme.getColor("surfaceTertiary"); // a flat fill, so an off choice still reads as a key
    const auto hoverBg = theme.getColor("accentPrimary").withAlpha(0.10f);
    const auto borderCol = theme.getColor("borderSubtle").withAlpha(0.40f);
    const auto activeText = NUIColor::white();
    const auto inactiveText = theme.getColor("textSecondary").withAlpha(0.72f);

    auto styleButton = [&](const std::shared_ptr<NUIButton>& button, LoopMode mode) {
        if (!button) {
            return;
        }
        const bool active = m_loopPoints.mode == mode;
        button->setBackgroundColor(active ? activeBg : inactiveBg);
        button->setHoverColor(active ? activeBg : hoverBg);
        button->setPressedColor(theme.getColor("accentPrimary").withAlpha(0.32f));
        button->setTextColor(active ? activeText : inactiveText);
        button->setCornerRadius(2.0f);
        button->setBorderEnabled(false);
        button->setBorderColor(active ? theme.getColor("accentPrimary").withAlpha(0.55f) : borderCol);
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

void SampleEditorPanel::setCutSelfModeInternal(bool cutSelf, bool notify) {
    if (m_cutSelfMode == cutSelf) {
        updateMonoPolyControls();
        return;
    }

    m_cutSelfMode = cutSelf;
    updateMonoPolyControls();

    if (notify && !m_suppressControlCallbacks) {
        if (onCutSelfModeChanged) onCutSelfModeChanged(m_cutSelfMode);
        requestControlCommit();
    }
}

void SampleEditorPanel::setCutSelfMode(bool cutSelf) {
    setCutSelfModeInternal(cutSelf, false);
}

void SampleEditorPanel::setKeepLengthPitchInternal(bool keepLength, bool notify) {
    if (m_keepLengthPitch != keepLength) {
        m_keepLengthPitch = keepLength;
        if (notify && !m_suppressControlCallbacks) {
            if (onKeepLengthPitchChanged) onKeepLengthPitchChanged(m_keepLengthPitch);
            requestControlCommit();
        }
    }
    updateMonoPolyControls();
}

void SampleEditorPanel::setKeepLengthPitch(bool keepLength) {
    setKeepLengthPitchInternal(keepLength, false);
}

void SampleEditorPanel::updateMonoPolyControls() {
    auto& theme = NUIThemeManager::getInstance();
    const auto activeBg = theme.getColor("accentPrimary"); // the active choice is solid violet
    const auto inactiveBg = theme.getColor("surfaceTertiary"); // a flat fill, so an off choice still reads as a key
    const auto hoverBg = theme.getColor("accentPrimary").withAlpha(0.10f);
    const auto activeText = NUIColor::white();
    const auto inactiveText = theme.getColor("textSecondary").withAlpha(0.72f);
    const auto inactiveBorder = theme.getColor("borderSubtle").withAlpha(0.40f);

    auto styleButton = [&](const std::shared_ptr<NUIButton>& button, bool active) {
        if (!button) return;
        button->setBackgroundColor(active ? activeBg : inactiveBg);
        button->setHoverColor(active ? activeBg : hoverBg);
        button->setPressedColor(theme.getColor("accentPrimary").withAlpha(0.32f));
        button->setTextColor(active ? activeText : inactiveText);
        button->setCornerRadius(2.0f);
        button->setBorderEnabled(false);
        button->setBorderColor(active ? theme.getColor("accentPrimary").withAlpha(0.55f) : inactiveBorder);
    };

    styleButton(m_monoModeBtn, m_monoMode);
    styleButton(m_polyModeBtn, !m_monoMode);
    styleButton(m_cutSelfModeBtn, m_cutSelfMode);
    styleButton(m_keepLengthBtn, m_keepLengthPitch);

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

void SampleEditorPanel::updatePitchValueLabels() {
    m_pitchCoarseValueLabel->setText(signedLabel(static_cast<int>(m_pitchCoarseSlider->getValue()), " st"));
    m_pitchFineValueLabel->setText(signedLabel(static_cast<int>(std::lround(m_pitchFineSlider->getValue())), " ct"));
}

void SampleEditorPanel::onPitchControlChanged() {
    if (m_suppressControlCallbacks) {
        return;
    }
    m_pitchTune.rootMidiNote = static_cast<int>(std::round(m_pitchRootSlider->getValue()));
    m_pitchTune.coarse = static_cast<int>(m_pitchCoarseSlider->getValue());
    m_pitchTune.fine = static_cast<float>(m_pitchFineSlider->getValue());
    m_pitchRootValueLabel->setText(midiNoteName(m_pitchTune.rootMidiNote));
    updatePitchValueLabels();

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
