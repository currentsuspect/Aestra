// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerMeter.h"
#include "../Graphics/NUIDepth.h"
#include "MeterLeds.h"
#include "MeterZones.h"
#include "MixerScale.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include <algorithm>
#include <cmath>
#include <string> // Added for std::to_string
#include <cstdio> // Added for snprintf, though std::to_string is used
#include "../../AestraCore/include/AestraLog.h"

namespace AestraUI {

UIMixerMeter::UIMixerMeter()
{
    cacheThemeColors();
}

void UIMixerMeter::cacheThemeColors()
{
    auto& theme = NUIThemeManager::getInstance();

    // Meter colors are semantic: audio activity, warning, clipping.
    m_colorGreen = theme.getColor("meterSafe");
    m_colorYellow = theme.getColor("meterWarn");
    m_colorRed = theme.getColor("meterCrit");
    // Muted style: monochrome, slightly reduced alpha.
    m_colorGreenDim = m_colorGreen.withSaturation(0.0f).withAlpha(0.55f);
    m_colorYellowDim = m_colorYellow.withSaturation(0.0f).withAlpha(0.55f);
    m_colorRedDim = m_colorRed.withSaturation(0.0f).withAlpha(0.55f);
    // The meter must read as a meter even at silence, otherwise a resting strip
    // shows only a fader and the "what is sounding" channel looks like padding.
    // At 0.3 alpha the track was invisible against the strip background.
    m_colorBackground = theme.getColor("meterBackground").withAlpha(0.72f);
    m_colorRailEdge = theme.getColor("textPrimary").withAlpha(0.12f);
    m_colorPeakHold = theme.getColor("textPrimary"); // #E5E5E8
    m_colorPeakOverlay = m_colorPeakHold.withAlpha(0.8f);
    m_colorPeakOverlayDim = m_colorPeakOverlay.withSaturation(0.0f).withAlpha(0.6f);
    m_colorClipOff = theme.getColor("borderSubtle").withAlpha(0.55f);
}

// Repaint only when a level moves by a visible amount. These setters are
// called every frame from the strip; repainting unconditionally kept the
// mixer (and the whole app) awake even while dead silent, defeating idle
// elision. The stored value only advances once the drift crosses the
// threshold, so slow decays still animate — in ~0.1 dB steps — rather than
// freezing. kMeterRepaintEpsilonDb is sub-perceptual on the -80..+6 dB scale.
static constexpr float kMeterRepaintEpsilonDb = 0.1f;

void UIMixerMeter::setLevels(float dbL, float dbR)
{
    const float nl = std::max(DB_MIN, std::min(DB_MAX, dbL));
    const float nr = std::max(DB_MIN, std::min(DB_MAX, dbR));
    if (std::abs(nl - m_peakL) > kMeterRepaintEpsilonDb || std::abs(nr - m_peakR) > kMeterRepaintEpsilonDb) {
        m_peakL = nl;
        m_peakR = nr;
        repaint();
    }
}

void UIMixerMeter::setRmsLevels(float dbL, float dbR)
{
    const float nl = std::max(DB_MIN, std::min(DB_MAX, dbL));
    const float nr = std::max(DB_MIN, std::min(DB_MAX, dbR));
    if (std::abs(nl - m_rmsL) > kMeterRepaintEpsilonDb || std::abs(nr - m_rmsR) > kMeterRepaintEpsilonDb) {
        m_rmsL = nl;
        m_rmsR = nr;
        repaint();
    }
}

void UIMixerMeter::setPeakOverlay(float peakDbL, float peakDbR)
{
    const float nl = std::max(DB_MIN, std::min(DB_MAX, peakDbL));
    const float nr = std::max(DB_MIN, std::min(DB_MAX, peakDbR));
    if (std::abs(nl - m_peakOverlayL) > kMeterRepaintEpsilonDb || std::abs(nr - m_peakOverlayR) > kMeterRepaintEpsilonDb) {
        m_peakOverlayL = nl;
        m_peakOverlayR = nr;
        repaint();
    }
}

void UIMixerMeter::setPeakHold(float holdL, float holdR)
{
    const float nl = std::max(DB_MIN, std::min(DB_MAX, holdL));
    const float nr = std::max(DB_MIN, std::min(DB_MAX, holdR));
    if (std::abs(nl - m_peakHoldL) > kMeterRepaintEpsilonDb || std::abs(nr - m_peakHoldR) > kMeterRepaintEpsilonDb) {
        m_peakHoldL = nl;
        m_peakHoldR = nr;
        repaint();
    }
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

void UIMixerMeter::setMasterMode(bool master)
{
    if (m_masterMode != master) {
        m_masterMode = master;
        repaint();
    }
}

float UIMixerMeter::getDisplayPeakDb() const
{
    if (m_peakHoldL > DB_MIN || m_peakHoldR > DB_MIN) {
        return std::max(m_peakHoldL, m_peakHoldR);
    }
    return std::max(m_peakL, m_peakR);
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
    // The mixer's shared scale (MixerScale.h): +6 at the top, -80 at the floor, the same curve the fader cap
    // and the scale at the panel's edge use, so level and gain read against each other. A silent meter is empty
    // (the scale itself puts -80 at 0.02, which would leave a sliver of green at rest).
    return db <= DB_MIN ? 0.0f : MixerScale::norm(db);
}

NUIColor UIMixerMeter::getColorForLevel(float db) const
{
    // Zones are fractions of the lane's height (MeterZones.h), read off the same scale the bar is drawn on.
    switch (MeterZones::meterZone(dbToNormalized(db))) {
    case MeterZones::Zone::Red: return m_colorRed;
    case MeterZones::Zone::Amber: return m_colorYellow;
    case MeterZones::Zone::Green: break;
    }
    return m_colorGreen;
}

void UIMixerMeter::renderMeterBar(NUIRenderer& renderer, const NUIRect& bounds,
                                   float peakDb, float rmsDb, float peakOverlayDb, float peakHoldDb, bool clip)
{
    // Stepped LEDs (Design Spec, Mixer, board p-mixer2 locked): 3px lit, 1px of well between, stacked up from the
    // lane's bottom; an unlit LED is not drawn, the well shows. The well itself is drawn once for both columns
    // (onRender). The lane is exactly the fader's travel, so level reads against gain.
    const NUIColor& yellow = m_dimmed ? m_colorYellowDim : m_colorYellow;
    const NUIColor& red = m_dimmed ? m_colorRedDim : m_colorRed;
    const NUIColor green = m_dimmed ? m_colorGreenDim : m_colorGreen;
    (void)clip;
    (void)peakOverlayDb; // Unused in this mode (bars are already showing fast peak)

    const NUIRect meterArea = bounds;
    const int count = MeterLeds::ledCount(meterArea.height);
    const int rmsLit = MeterLeds::litCount(dbToNormalized(rmsDb), count);
    const int peakLit = MeterLeds::litCount(dbToNormalized(peakDb), count);

    for (int i = 0; i < std::max(rmsLit, peakLit); ++i) {
        const NUIColor* c = &green;
        switch (MeterZones::meterZone(MeterLeds::ledFraction(i, count))) { // zone by height fraction (MeterZones.h)
        case MeterZones::Zone::Red: c = &red; break;
        case MeterZones::Zone::Amber: c = &yellow; break;
        case MeterZones::Zone::Green: break;
        }
        // The RMS body is solid; the peak's excess above it stays the 35% ghost the continuous bar had.
        NUIColor color = i < rmsLit ? *c : c->withAlpha(0.35f);
        if (m_dimmed) color = color.withSaturation(0.0f).withAlpha(i < rmsLit ? 0.5f : 0.35f);
        renderer.fillRect(NUIRect{meterArea.x, meterArea.y + MeterLeds::ledTop(i, meterArea.height), meterArea.width,
                                  MeterLeds::kLedH},
                          color);
    }

    // Peak Hold indicator (white line that sticks)
    if (peakHoldDb > DB_MIN) {
        float peakNorm = dbToNormalized(peakHoldDb);
        float peakY = meterArea.y + meterArea.height * (1.0f - peakNorm);
        
        // Clamp to meter area
        peakY = std::max(meterArea.y, std::min(peakY, meterArea.y + meterArea.height - PEAK_HOLD_HEIGHT));
        
        // Draw hold line (white 0.85 alpha, 1px thick)
        const NUIColor holdColor = m_dimmed
            ? m_colorPeakOverlayDim
            : m_colorPeakHold.withAlpha(0.85f);
        renderer.fillRect(NUIRect{meterArea.x, peakY, meterArea.width, PEAK_HOLD_HEIGHT}, holdColor);
    }
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

    // Reserve space for Text Readout at top (increased for better readability).
    // In master mode the strip owns a wider, explicitly labelled readout block,
    // so the meter reclaims that space and only labels its two bars L / R.
    constexpr float TEXT_HEIGHT = READOUT_BAND;
    constexpr float LR_LABEL_HEIGHT = 11.0f;
    // Track strips keep TEXT_HEIGHT reserved even when the compact readout is
    // hidden (idle): reclaiming it on hover would grow the bars by 24 px and
    // then shrink them again — a geometry jump. Stable meter geometry is the
    // deliberate tradeoff; the empty band reads as quiet headroom, not layout.
    const float topReserve = barsTopOffset() - LAMP_BAND;
    const float bottomReserve = m_masterMode ? LR_LABEL_HEIGHT : 0.0f;

    // The LED pair (MeterLeds.h): two 6px columns, gap 2, in an 18px well padded 2. The strip still reserves the
    // old 24px column (slice 3 re-lays it), so the well is centred in whatever width it is given.
    const float wellW = std::min(bounds.width, MeterLeds::kWellW);
    const float colW = (wellW - MeterLeds::kPad * 2.0f - MeterLeds::kColumnGap) * 0.5f;
    const float wellX = std::round(bounds.x + (bounds.width - wellW) * 0.5f);

    NUIRect leftBounds = bounds;
    leftBounds.x = wellX + MeterLeds::kPad;
    leftBounds.y += topReserve + LAMP_BAND;
    leftBounds.height = std::max(1.0f, meterHeight - topReserve - LAMP_BAND - bottomReserve);
    leftBounds.width = colW;
    NUIRect rightBounds = leftBounds;
    rightBounds.x += colW + MeterLeds::kColumnGap;

    // The well pads the lane by 2px at the top and (track strips) the bottom, so the lane keeps the fader's travel.
    // The master's L / R labels sit right under the lane, so its well stops at the lane.
    {
        const float padBottom = m_masterMode ? 0.0f : MeterLeds::kPad;
        const NUIRect well{wellX, leftBounds.y - MeterLeds::kPad, wellW,
                           leftBounds.height + MeterLeds::kPad + padBottom};
        Depth::bed(renderer, well, MeterLeds::kWellRadius, m_colorBackground, Depth::Cut::Slot);
    }
    renderMeterBar(renderer, leftBounds, m_peakL, m_rmsL, m_peakOverlayL, m_peakHoldL, m_clipL);
    renderMeterBar(renderer, rightBounds, m_peakR, m_rmsR, m_peakOverlayR, m_peakHoldR, m_clipR);

    // Design Spec, Mixer (locked): the clip lamp, an 8px sunk dot above the meters. Any sample at or over 0 dBFS
    // lights it in meterCrit and it stays lit until clicked, so a clip you missed is still there when you look.
    {
        const bool clipped = m_clipL || m_clipR;
        const NUIRect lamp{std::round(bounds.x + (bounds.width - CLIP_LAMP) * 0.5f),
                           bounds.y + topReserve + (LAMP_BAND - CLIP_LAMP) * 0.5f, CLIP_LAMP, CLIP_LAMP};
        Depth::bed(renderer, lamp, CLIP_LAMP * 0.5f, clipped ? (m_dimmed ? m_colorRedDim : m_colorRed) : m_colorBackground,
                   Depth::Cut::Slot);
    }

    // Which bar is which — otherwise the master's two bars are unidentified.
    if (m_masterMode) {
        const NUIColor labelColor = m_colorPeakHold.withAlpha(0.55f);
        const NUIRect lLabel{leftBounds.x, leftBounds.y + leftBounds.height,
                             leftBounds.width, LR_LABEL_HEIGHT};
        const NUIRect rLabel{rightBounds.x, rightBounds.y + rightBounds.height,
                             rightBounds.width, LR_LABEL_HEIGHT};
        renderer.drawTextCentered("L", lLabel, 9.0f, labelColor);
        renderer.drawTextCentered("R", rLabel, 9.0f, labelColor);
    }

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
        renderer.fillRect(corrBounds, NUIThemeManager::getInstance().getColor("controlBackground"));
        
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
        
        // Correlation is also semantic status:
        // positive/in-phase -> audio color, slight negative -> warning, deep negative -> error.
        auto& theme = NUIThemeManager::getInstance();
        const NUIColor phaseGood = theme.getColor("meterSafe");
        const NUIColor phaseWarn = theme.getColor("warning");
        const NUIColor phaseBad = theme.getColor("error");
        NUIColor barColor = (val < -0.25f) ? phaseBad : (val < 0.0f ? phaseWarn : phaseGood);
        if (m_dimmed) barColor = barColor.withSaturation(0.0f).withAlpha(0.6f);
        
        renderer.fillRect(barRect, barColor);
    }
    
    // Draw Value Readout (Top) — track strips only.
    // The master's numbers are rendered by the strip instead: stacking "x.x dB"
    // over "LUFS -13.5" inside a 36 px meter clipped the text, and losing the
    // minus sign turns -13.5 LUFS into a radically different claim.
    if (m_masterMode) {
        renderChildren(renderer);
        return;
    }

    const bool showCompactReadout = isHovered() || m_clipL || m_clipR;
    if (!showCompactReadout) {
        renderChildren(renderer);
        return;
    }

    // Get peak value
    float peak = std::max(m_peakL, m_peakR);
    if (m_peakHoldL > DB_MIN || m_peakHoldR > DB_MIN) {
        peak = std::max(m_peakHoldL, m_peakHoldR);
    }
    
    bool hasPeak = (peak > -90.0f);
    // Show LUFS if we have a valid reading (ignore start-up silence -144.0)
    bool hasLufs = (m_integratedLufs > -100.0f);
    
    if (hasLufs && hasPeak) {
        // Master channel: Peak dB primary, LUFS secondary below
        if (std::abs(m_integratedLufs - m_cachedLufs) > 0.05f) {
            m_cachedLufs = m_integratedLufs;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f", m_integratedLufs);
            m_cachedLufsStr = buf;
        }
        if (std::abs(peak - m_cachedDbPeak) > 0.05f) {
            m_cachedDbPeak = peak;
            if (peak <= DB_MIN) {
                m_cachedDbStr = "\xE2\x88\x92\xE2\x88\x9E";
            } else {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.1f dB", peak);
                m_cachedDbStr = buf;
            }
        }
        
        // Each line gets half the text area (12px each with 24px total)
        float lineHeight = TEXT_HEIGHT * 0.5f;
        
        // Peak dB on top - primary readout, same weight as track meters
        NUIRect dbRect = {
            bounds.x,
            bounds.y + 1.0f,  // Small top padding
            bounds.width,
            lineHeight
        };
        renderer.drawTextCentered(m_cachedDbStr, dbRect, 11.0f, m_colorPeakHold);
        
        // LUFS below - secondary readout, smaller, dimmed, with label prefix
        NUIRect lufsRect = {
            bounds.x,
            bounds.y + lineHeight,
            bounds.width,
            lineHeight
        };
        std::string lufsLabel = "LUFS " + m_cachedLufsStr;
        renderer.drawTextCentered(lufsLabel, lufsRect, 7.0f, NUIThemeManager::getInstance().getCurrentTheme().textPrimary.withAlpha(0.45f));
        
    } else if (hasPeak) {
        // Regular tracks: peak dB or −∞ at the silence floor.
        // The "PK" prefix is what separates this from the fader's gain readout
        // sitting a few pixels to the right — position alone left a bare "−∞"
        // next to "0.0 dB" as a small decoding task on every strip.
        if (std::abs(peak - m_cachedDbPeak) > 0.05f) {
            m_cachedDbPeak = peak;
            if (peak <= DB_MIN) {
                m_cachedDbStr = "\xE2\x88\x92\xE2\x88\x9E";
            } else {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.1f", peak);
                m_cachedDbStr = buf;
            }
        }
        
        // Center vertically in the text area
        NUIRect textRect = {
            bounds.x,
            bounds.y + (TEXT_HEIGHT - 12.0f) * 0.5f,  // Center a 12px line in 24px area
            bounds.width,
            12.0f
        };
        renderer.drawTextCentered(m_cachedDbStr, textRect, 11.0f, m_colorPeakHold);
    }

    // Render children
    renderChildren(renderer);
}

bool UIMixerMeter::onMouseEvent(const NUIMouseEvent& event)
{
    const auto bounds = getBounds();
    if (event.button == NUIMouseButton::None && !event.cursorCaptured) {
        setHovered(bounds.contains(event.position));
    }

    // Handle click on clip indicator to clear clip latch
    if (event.pressed &&
        (event.button == NUIMouseButton::Left || event.button == NUIMouseButton::Right)) {
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

void UIMixerMeter::onMouseLeave()
{
    setHovered(false);
    NUIComponent::onMouseLeave();
}

} // namespace AestraUI
