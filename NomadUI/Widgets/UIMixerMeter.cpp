// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerMeter.h"

#include "../Graphics/NUIRenderer.h"
#include "../Core/NUIThemeSystem.h"
#include <algorithm>
#include <cmath>

namespace NomadUI {

UIMixerMeter::UIMixerMeter()
{
    cacheThemeColors();
}

void UIMixerMeter::cacheThemeColors()
{
    auto& theme = NUIThemeManager::getInstance();

    // Meter colors from design spec
    m_colorGreen = theme.getColor("success");       // #5BD896
    m_colorYellow = theme.getColor("warning");      // #FFD86B
    m_colorRed = theme.getColor("error");           // #FF5E5E
    m_colorBackground = theme.getColor("backgroundPrimary"); // #181819
    m_colorPeakHold = theme.getColor("textPrimary"); // #E5E5E8
    m_colorClipOff = theme.getColor("backgroundSecondary"); // #1e1e1f
}

void UIMixerMeter::setLevels(float dbL, float dbR)
{
    // Clamp to valid range
    m_peakL = std::max(DB_MIN, std::min(DB_MAX, dbL));
    m_peakR = std::max(DB_MIN, std::min(DB_MAX, dbR));
    repaint();
}

void UIMixerMeter::setPeakHold(float holdL, float holdR)
{
    m_peakHoldL = std::max(DB_MIN, std::min(DB_MAX, holdL));
    m_peakHoldR = std::max(DB_MIN, std::min(DB_MAX, holdR));
    repaint();
}

void UIMixerMeter::setClipLatch(bool clipL, bool clipR)
{
    if (m_clipL != clipL || m_clipR != clipR) {
        m_clipL = clipL;
        m_clipR = clipR;
        repaint();
    }
}

float UIMixerMeter::dbToNormalized(float db) const
{
    // Map -60 to 0 dB to 0.0 to 1.0 with segmented linear curve
    if (db <= DB_MIN) return 0.0f;
    if (db >= DB_MAX) return 1.0f;

    // Segmented linear for predictable behavior:
    // -60 to -20 dB maps to 0.0 to 0.5
    // -20 to 0 dB maps to 0.5 to 1.0
    if (db < -20.0f) {
        return (db - DB_MIN) / ((-20.0f) - DB_MIN) * 0.5f;
    } else {
        return 0.5f + (db - (-20.0f)) / (DB_MAX - (-20.0f)) * 0.5f;
    }
}

NUIColor UIMixerMeter::getColorForLevel(float db) const
{
    if (db >= DB_RED_THRESHOLD) {
        return m_colorRed;
    } else if (db >= DB_YELLOW_THRESHOLD) {
        return m_colorYellow;
    }
    return m_colorGreen;
}

void UIMixerMeter::renderMeterBar(NUIRenderer& renderer, const NUIRect& bounds,
                                   float levelDb, float peakHoldDb, bool clip)
{
    // Background
    renderer.fillRect(bounds, m_colorBackground);

    // Calculate meter fill height
    float normalizedLevel = dbToNormalized(levelDb);
    float meterAreaHeight = bounds.height - CLIP_HEIGHT;
    float fillHeight = normalizedLevel * meterAreaHeight;

    // Meter bar area (below clip indicator)
    NUIRect meterArea = {
        bounds.x,
        bounds.y + CLIP_HEIGHT,
        bounds.width,
        meterAreaHeight
    };

    // Render meter fill from bottom up with color zones
    if (fillHeight > 0.0f) {
        // Calculate Y positions for color zone thresholds
        float yellowNorm = dbToNormalized(DB_YELLOW_THRESHOLD);
        float redNorm = dbToNormalized(DB_RED_THRESHOLD);

        float yellowY = meterArea.y + meterArea.height * (1.0f - yellowNorm);
        float redY = meterArea.y + meterArea.height * (1.0f - redNorm);
        float fillTopY = meterArea.y + meterArea.height - fillHeight;
        float bottomY = meterArea.y + meterArea.height;

        // Green zone (bottom)
        if (normalizedLevel > 0.0f) {
            float greenTop = std::max(fillTopY, yellowY);
            if (greenTop < bottomY) {
                NUIRect greenRect = {
                    meterArea.x,
                    greenTop,
                    meterArea.width,
                    bottomY - greenTop
                };
                renderer.fillRect(greenRect, m_colorGreen);
            }
        }

        // Yellow zone (middle)
        if (normalizedLevel > yellowNorm) {
            float yellowTop = std::max(fillTopY, redY);
            float yellowBottom = std::min(yellowY, bottomY);
            if (yellowTop < yellowBottom) {
                NUIRect yellowRect = {
                    meterArea.x,
                    yellowTop,
                    meterArea.width,
                    yellowBottom - yellowTop
                };
                renderer.fillRect(yellowRect, m_colorYellow);
            }
        }

        // Red zone (top)
        if (normalizedLevel > redNorm) {
            float redTop = fillTopY;
            float redBottom = std::min(redY, bottomY);
            if (redTop < redBottom) {
                NUIRect redRect = {
                    meterArea.x,
                    redTop,
                    meterArea.width,
                    redBottom - redTop
                };
                renderer.fillRect(redRect, m_colorRed);
            }
        }
    }

    // Peak hold indicator (thin horizontal line)
    if (peakHoldDb > DB_MIN) {
        float peakNorm = dbToNormalized(peakHoldDb);
        float peakY = meterArea.y + meterArea.height * (1.0f - peakNorm);

        // Clamp to meter area
        peakY = std::max(meterArea.y, std::min(peakY, meterArea.y + meterArea.height - PEAK_HOLD_HEIGHT));

        NUIRect peakRect = {
            meterArea.x,
            peakY,
            meterArea.width,
            PEAK_HOLD_HEIGHT
        };
        renderer.fillRect(peakRect, m_colorPeakHold);
    }

    // Clip indicator at top
    NUIRect clipRect = {
        bounds.x,
        bounds.y,
        bounds.width,
        CLIP_HEIGHT
    };
    renderer.fillRect(clipRect, clip ? m_colorRed : m_colorClipOff);
}

void UIMixerMeter::onRender(NUIRenderer& renderer)
{
    auto bounds = getBounds();

    // Calculate bar widths
    float totalWidth = bounds.width;
    float barWidth = (totalWidth - METER_GAP) / 2.0f;

    // Left bar
    NUIRect leftBounds = {
        bounds.x,
        bounds.y,
        barWidth,
        bounds.height
    };
    renderMeterBar(renderer, leftBounds, m_peakL, m_peakHoldL, m_clipL);

    // Right bar
    NUIRect rightBounds = {
        bounds.x + barWidth + METER_GAP,
        bounds.y,
        barWidth,
        bounds.height
    };
    renderMeterBar(renderer, rightBounds, m_peakR, m_peakHoldR, m_clipR);

    // Render children
    renderChildren(renderer);
}

bool UIMixerMeter::onMouseEvent(const NUIMouseEvent& event)
{
    // Handle click on clip indicator to clear clip latch
    if (event.pressed && event.button == NUIMouseButton::Left) {
        auto bounds = getBounds();

        // Check if click is in clip indicator area (top CLIP_HEIGHT pixels)
        if (event.position.y >= bounds.y && event.position.y < bounds.y + CLIP_HEIGHT) {
            if (event.position.x >= bounds.x && event.position.x < bounds.x + bounds.width) {
                // Clear clip latch
                if ((m_clipL || m_clipR) && onClipCleared) {
                    onClipCleared();
                }
                return true;
            }
        }
    }

    return false;
}

} // namespace NomadUI
