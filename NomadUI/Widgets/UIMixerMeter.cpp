// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerMeter.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include <algorithm>
#include <cmath>
#include <string> // Added for std::to_string
#include <cstdio> // Added for snprintf, though std::to_string is used
#include "../../NomadCore/include/NomadLog.h"

namespace NomadUI {

UIMixerMeter::UIMixerMeter()
{
    cacheThemeColors();
}

void UIMixerMeter::cacheThemeColors()
{
    auto& theme = NUIThemeManager::getInstance();

    // Meter colors from design spec (Nomad Heat Gradient)
    m_colorGreen = theme.getColor("meterSafe");     // Purple (Bottom)
    m_colorYellow = theme.getColor("meterWarn");    // Pink (Mid)
    m_colorRed = theme.getColor("meterCrit");       // Red (Top)
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

void UIMixerMeter::setCorrelation(float correlation)
{
    // Clamp to -1.0 to 1.0
    float c = std::max(-1.0f, std::min(1.0f, correlation));
    if (std::abs(m_correlation - c) > 0.005f) { // hysteresis
        m_correlation = c;
        if (m_showCorrelation) repaint();
    }
}

void UIMixerMeter::setShowCorrelation(bool show)
{
    if (m_showCorrelation != show) {
        m_showCorrelation = show;
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

void UIMixerMeter::setIntegratedLufs(float lufs)
{
    if (std::abs(m_integratedLufs - lufs) > 0.05f) {
        m_integratedLufs = lufs;
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
    // Background (flat fill - gradient caused edge artifacts)
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

    // Layout Correlation Meter
    constexpr float CORR_HEIGHT = 12.0f; // Taller for better readability
    constexpr float CORR_GAP = 4.0f;
    float meterHeight = bounds.height;
    
    if (m_showCorrelation) {
        meterHeight -= (CORR_HEIGHT + CORR_GAP);
    }

    // Calculate bar widths (L/R)
    // Reserve space for Text Readout at top (increased for better readability)
    constexpr float TEXT_HEIGHT = 24.0f;

    // Calculate bar widths (L/R)
    float totalWidth = bounds.width;
    // float barWidth = (totalWidth - METER_GAP) / 2.0f; // Unused variable warning fix

    // Left Meter
    NUIRect leftBounds = bounds;
    leftBounds.y += TEXT_HEIGHT;
    leftBounds.height = std::max(1.0f, meterHeight - TEXT_HEIGHT);
    leftBounds.width = (bounds.width - METER_GAP) * 0.5f;
    renderMeterBar(renderer, leftBounds, m_peakL, m_rmsL, m_peakOverlayL, m_peakHoldL, m_clipL);

    // Right Meter
    NUIRect rightBounds = leftBounds;
    rightBounds.x += leftBounds.width + METER_GAP;
    renderMeterBar(renderer, rightBounds, m_peakR, m_rmsR, m_peakOverlayR, m_peakHoldR, m_clipR);

    // Draw Correlation Meter
    if (m_showCorrelation) {
        NUIRect corrBounds = {
            bounds.x,
            bounds.y + bounds.height - CORR_HEIGHT,
            bounds.width,
            CORR_HEIGHT
        };
        
        // Background (distinct from main meter)
        // Use a visible gray track (0.25f)
        renderer.fillRect(corrBounds, NUIColor(0.25f, 0.25f, 0.25f, 1.0f));
        
        // Center line (white/bright) - 2px width
        float centerX = corrBounds.x + corrBounds.width * 0.5f;
        renderer.fillRect({centerX - 1.0f, corrBounds.y, 2.0f, corrBounds.height}, m_colorPeakHold);
        
        // Bar
        float val = m_correlation; // -1 to 1
        float barW = (corrBounds.width * 0.5f) * std::abs(val);
        float barX = (val >= 0.0f) ? centerX : (centerX - barW);
        
        NUIRect barRect = {
            barX,
            corrBounds.y + 1.0f,
            barW,
            CORR_HEIGHT - 2.0f
        };
        
        // Color: Red for negative (phase issue), Green/Blue for positive.
        // Use hardcoded Phase colors to avoid theme confusion (e.g. if 'safe' is purple)
        NUIColor phaseBad(0.9f, 0.15f, 0.15f); // Red
        NUIColor phaseGood(0.2f, 0.85f, 0.3f); // Green/Cyan-ish
        
        NUIColor barColor = (val < 0.0f) ? phaseBad : phaseGood;
        if (m_dimmed) barColor = barColor.withSaturation(0.0f).withAlpha(0.6f);
        
        renderer.fillRect(barRect, barColor);
    }
    
    // Draw Value Readout (Top)
    // Master: Show both LUFS (top line) and Peak dB (bottom line)
    // Tracks: Show Peak dB only
    
    // Get peak value (used for both master and regular tracks)
    float peak = std::max(m_peakL, m_peakR);
    if (m_peakHoldL > DB_MIN || m_peakHoldR > DB_MIN) {
        peak = std::max(m_peakHoldL, m_peakHoldR);
    }
    
    bool hasPeak = (peak > -90.0f);
    // Show LUFS if we have a valid reading (ignore start-up silence -144.0)
    bool hasLufs = (m_integratedLufs > -100.0f);
    
    if (hasLufs && hasPeak) {
        // Master channel: Show LUFS on top, dB below
        char lufsBuf[32];
        char dbBuf[32];
        std::snprintf(lufsBuf, sizeof(lufsBuf), "%.1f LUFS", m_integratedLufs);
        std::snprintf(dbBuf, sizeof(dbBuf), "%.1f dB", peak);
        
        // Each line gets half the text area (12px each with 24px total)
        float lineHeight = TEXT_HEIGHT * 0.5f;
        
        // LUFS on top - slightly larger, primary readout
        NUIRect lufsRect = {
            bounds.x,
            bounds.y + 1.0f,  // Small top padding
            bounds.width,
            lineHeight
        };
        renderer.drawTextCentered(lufsBuf, lufsRect, 10.0f, m_colorPeakHold);
        
        // dB below - secondary readout
        NUIRect dbRect = {
            bounds.x,
            bounds.y + lineHeight,
            bounds.width,
            lineHeight
        };
        renderer.drawTextCentered(dbBuf, dbRect, 9.0f, m_colorPeakHold.withAlpha(0.75f));
        
    } else if (hasPeak) {
        // Regular tracks: Show Peak dB only with "dB" suffix
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f dB", peak);
        
        // Center vertically in the text area
        NUIRect textRect = {
            bounds.x,
            bounds.y + (TEXT_HEIGHT - 12.0f) * 0.5f,  // Center a 12px line in 24px area
            bounds.width,
            12.0f
        };
        renderer.drawTextCentered(buf, textRect, 11.0f, m_colorPeakHold);
    }

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
