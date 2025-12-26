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
    // Muted style: monochrome, slightly reduced alpha.
    m_colorGreenDim = m_colorGreen.withSaturation(0.0f).withAlpha(0.55f);
    m_colorYellowDim = m_colorYellow.withSaturation(0.0f).withAlpha(0.55f);
    m_colorRedDim = m_colorRed.withSaturation(0.0f).withAlpha(0.55f);
    // Match fader track background so meters feel integrated with the strip.
    m_colorBackground = theme.getColor("backgroundSecondary"); // #1e1e1f
    m_colorPeakHold = theme.getColor("textPrimary"); // #E5E5E8
    m_colorPeakOverlay = m_colorPeakHold.withAlpha(0.8f);
    m_colorPeakOverlayDim = m_colorPeakOverlay.withSaturation(0.0f).withAlpha(0.6f);
    m_colorClipOff = theme.getColor("borderSubtle").withAlpha(0.55f);
}

void UIMixerMeter::setLevels(float dbL, float dbR)
{
    m_peakL = std::max(DB_MIN, std::min(DB_MAX, dbL));
    m_peakR = std::max(DB_MIN, std::min(DB_MAX, dbR));
    repaint();
}

void UIMixerMeter::setRmsLevels(float dbL, float dbR)
{
    m_rmsL = std::max(DB_MIN, std::min(DB_MAX, dbL));
    m_rmsR = std::max(DB_MIN, std::min(DB_MAX, dbR));
    repaint();
}

void UIMixerMeter::setPeakOverlay(float peakDbL, float peakDbR)
{
    m_peakOverlayL = std::max(DB_MIN, std::min(DB_MAX, peakDbL));
    m_peakOverlayR = std::max(DB_MIN, std::min(DB_MAX, peakDbR));
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

void UIMixerMeter::setDimmed(bool dimmed)
{
    if (m_dimmed != dimmed) {
        m_dimmed = dimmed;
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
                                   float peakDb, float rmsDb, float peakOverlayDb, float peakHoldDb, bool clip)
{
    // Background ("Grey" - matching mixer panel interior)
    // User requested "inactive/nothing playing mode should be the grey"
    renderer.fillRect(bounds, m_colorBackground);

    const NUIColor& yellow = m_dimmed ? m_colorYellowDim : m_colorYellow;
    const NUIColor& red = m_dimmed ? m_colorRedDim : m_colorRed;
    const NUIColor& peakOverlay = m_dimmed ? m_colorPeakOverlayDim : m_colorPeakOverlay;

    // Calculate meter fill height
    float meterAreaHeight = bounds.height - CLIP_HEIGHT;

    // Meter bar area (below clip indicator)
    NUIRect meterArea = {
        bounds.x,
        bounds.y + CLIP_HEIGHT,
        bounds.width,
        meterAreaHeight
    };

    // RMS Bar (Thick, Solid, Main Body)
    // User: "RMS thick bar"
    float normalizedRms = dbToNormalized(rmsDb);
    float rmsFillHeight = normalizedRms * meterAreaHeight;

    if (rmsFillHeight > 1.0f) {
        float rmsFillTopY = meterArea.y + meterArea.height - rmsFillHeight;
        NUIRect rmsFillRect = {
            meterArea.x,
            rmsFillTopY,
            meterArea.width,
            rmsFillHeight
        };

        // Standard Gradient Colors for RMS
        NUIColor bottomColor = m_colorGreen;
        NUIColor topColor = m_colorGreen;

        if (normalizedRms > 0.8f) { // High levels -> Red tip
             topColor = m_colorRed;
        } else if (normalizedRms > 0.5f) { // Mid levels -> Yellow tip
             topColor = m_colorYellow;
        } else {
             topColor = m_colorGreen.lightened(0.2f);
        }

        if (m_dimmed) {
            bottomColor = bottomColor.withSaturation(0.0f).withAlpha(0.5f);
            topColor = topColor.withSaturation(0.0f).withAlpha(0.5f);
        }

        renderer.fillRectGradient(rmsFillRect, topColor, bottomColor, true);
    }

    // Peak Bar (Full Width, Transparent/Ghost Overlay)
    // User: "peak the transparent bar"
    // Renders ON TOP of RMS but transparent so you see the "headroom" between RMS and Peak.
    float normalizedPeak = dbToNormalized(peakDb);
    float peakFillHeight = normalizedPeak * meterAreaHeight;
    
    // Only render the part of the peak that EXTENDS above RMS to avoid double-blending color
    // or render fully transparent if that's the desired look.
    // Ableton approach: Peak is a lighter box surrounding RMS or extending above it.
    
    if (peakFillHeight > rmsFillHeight) {
        float peakTopY = meterArea.y + meterArea.height - peakFillHeight;
        float peakHeight = peakFillHeight - rmsFillHeight; 
        
        // Draw the "excess" peak range as a transparent ghost
        NUIRect peakRect = {
            meterArea.x,
            peakTopY,
            meterArea.width,
            peakHeight
        };
        
        NUIColor peakColor = getColorForLevel(peakDb).withAlpha(0.35f); // 35% opacity
        if (m_dimmed) peakColor = peakColor.withSaturation(0.0f);
        
        renderer.fillRect(peakRect, peakColor);
    }

    // Peak Hold indicator (white line that sticks)
    if (peakHoldDb > DB_MIN) {
        float peakNorm = dbToNormalized(peakHoldDb);
        float peakY = meterArea.y + meterArea.height * (1.0f - peakNorm);
        
        // Clamp to meter area
        peakY = std::max(meterArea.y, std::min(peakY, meterArea.y + meterArea.height - PEAK_OVERLAY_HEIGHT));
        
        // Draw hold line (using the brighter 'textPrimary' color defined in cacheThemeColors)
        const NUIColor& holdColor = m_dimmed ? m_colorPeakOverlayDim : m_colorPeakHold;
        renderer.fillRect(NUIRect{meterArea.x, peakY, meterArea.width, PEAK_OVERLAY_HEIGHT}, holdColor);
    }

    (void)peakOverlayDb; // Unused in this mode (bars are already showing fast peak)

    // Clip indicator at top
    NUIRect clipRect = {
        bounds.x,
        bounds.y,
        bounds.width,
        CLIP_HEIGHT
    };
    renderer.fillRect(clipRect, clip ? red : m_colorClipOff);
}

void UIMixerMeter::onRender(NUIRenderer& renderer)
{
    auto bounds = getBounds();

    // Calculate bar widths
    float totalWidth = bounds.width;
    float barWidth = (totalWidth - METER_GAP) / 2.0f;

    // Left Meter
    NUIRect leftBounds = bounds;
    leftBounds.width = (bounds.width - METER_GAP) * 0.5f;
    renderMeterBar(renderer, leftBounds, m_peakL, m_rmsL, m_peakOverlayL, m_peakHoldL, m_clipL);

    // Right Meter
    NUIRect rightBounds = leftBounds;
    rightBounds.x += leftBounds.width + METER_GAP;
    renderMeterBar(renderer, rightBounds, m_peakR, m_rmsR, m_peakOverlayR, m_peakHoldR, m_clipR);

    // Render children
    renderChildren(renderer);
}

bool UIMixerMeter::onMouseEvent(const NUIMouseEvent& event)
{
    // Handle click on clip indicator to clear clip latch
    if (event.pressed && event.button == NUIMouseButton::Left) {
        auto bounds = getBounds();

        // Check if click is within the meter bounds (anywhere)
        // Improved UX: Allow clicking anywhere on the meter strip to clear clip latch, 
        // not just the tiny indicator at the top.
        if (bounds.contains(event.position)) {
            // Clear clip latch
            if ((m_clipL || m_clipR) && onClipCleared) {
                onClipCleared();
            }
            return true;
        }
    }

    return false;
}

} // namespace NomadUI
