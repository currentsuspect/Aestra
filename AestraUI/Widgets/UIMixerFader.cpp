// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerFader.h"

#include "NUIThemeSystem.h"
#include "NUIRenderer.h"

#include <algorithm>
#include <cstdio>
#include <cmath>

namespace AestraUI {

namespace {
    constexpr float TRACK_RADIUS = 4.0f;
    constexpr float HANDLE_RADIUS = 4.0f;
    constexpr float HANDLE_HEIGHT = 16.0f;
    constexpr float TOP_PAD = 10.0f;
    constexpr float BOTTOM_PAD = 20.0f; // room for text
    constexpr float SNAP_DB = 0.5f;
    constexpr float DRAG_SLOP = 1.5f;
}

UIMixerFader::UIMixerFader()
{
    cacheThemeColors();
    updateCachedText();
}

void UIMixerFader::cacheThemeColors()
{
    auto& theme = NUIThemeManager::getInstance();
    // Track: Deep Glass Slot
    m_trackBg = AestraUI::NUIColor(0.05f, 0.05f, 0.08f, 0.6f); 
    // Fill: Gradient handled in render
    m_trackFg = theme.getColor("accentPrimary"); 
    m_handle = theme.getColor("backgroundSecondary"); // Handle Core
    m_handleHover = theme.getColor("textPrimary");    // Handle Active
    m_text = theme.getColor("textPrimary");
    m_textSecondary = theme.getColor("textSecondary");
}

float UIMixerFader::clampDb(float db) const
{
    return std::max(m_minDb, std::min(m_maxDb, db));
}

void UIMixerFader::setRangeDb(float minDb, float maxDb)
{
    m_minDb = minDb;
    m_maxDb = maxDb;
    setValueDb(m_valueDb);
}

void UIMixerFader::updateCachedText()
{
    if (std::abs(m_cachedDbValue - m_valueDb) < 0.01f) {
        return;
    }
    m_cachedDbValue = m_valueDb;

    // "-∞" below -89.5 dB
    if (m_valueDb <= (m_minDb + 0.5f)) {
        m_cachedText = "-\xE2\x88\x9E";
        return;
    }

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.1f", m_valueDb);
    m_cachedText = buf;
}

void UIMixerFader::setValueDb(float db)
{
    float clamped = clampDb(db);
    if (std::abs(clamped - m_valueDb) < 1e-4f) {
        return;
    }

    m_valueDb = clamped;
    updateCachedText();
    repaint();

    if (onValueChanged) {
        onValueChanged(m_valueDb);
    }
}

/**
 * @brief Renders the mixer fader UI: track, filled level, handle, drag tooltip, and value readout.
 *
 * Draws the vertical track background, the filled portion representing the current dB value,
 * the fader handle (with hover/active styling), an in-drag tooltip showing the numeric dB value,
 * and the bottom-aligned value readout.
 *
 * @param renderer Renderer used to draw the widget visuals.
 */
void UIMixerFader::onRender(NUIRenderer& renderer)
{
    auto bounds = getBounds();

    // Track area
    const float trackTop = bounds.y + TOP_PAD;
    const float trackBottom = bounds.y + bounds.height - BOTTOM_PAD;
    const float trackHeight = std::max(1.0f, trackBottom - trackTop);

    // Thinner track for "Tech" look
    const float trackWidth = 6.0f;
    const float trackX = bounds.x + (bounds.width - trackWidth) * 0.5f;
    NUIRect trackRect{trackX, trackTop, trackWidth, trackHeight};

    // 1. Track Background (Deep Slot)
    renderer.fillRoundedRect(trackRect, 2.0f, m_trackBg);
    // Inner shadow simulation (subtle borders)
    renderer.strokeRoundedRect(trackRect, 2.0f, 1.0f, AestraUI::NUIColor(0.0f, 0.0f, 0.0f, 0.8f));

    // 2. Filled Portion (Neon Gradient)
    const float norm = (m_valueDb - m_minDb) / std::max(1e-3f, (m_maxDb - m_minDb));
    const float filledH = std::clamp(norm, 0.0f, 1.0f) * trackHeight;
    
    if (filledH > 0.0f) {
        // Draw fill slightly wider than track for glow bleed
        float fillW = 6.0f; 
        
        NUIRect fillRect{trackX, trackBottom - filledH, fillW, filledH};
        
        // Gradient Fill: Top is Bright Neon, Bottom is Darker
        NUIColor colorTop = m_trackFg;
        NUIColor colorBottom = m_trackFg.withAlpha(0.3f);
        
        // We simulate gradient by drawing solid for now, but renderer supports gradient rects?
        // Using solid for core, glow for effect
        
        // Inner Glow (Blur)
        renderer.fillRoundedRect(
            NUIRect{fillRect.x - 2, fillRect.y, fillRect.width + 4, fillRect.height},
            4.0f,
            m_trackFg.withAlpha(0.25f)
        );
        
        // Core (Bright)
        renderer.fillRoundedRect(fillRect, 2.0f, m_trackFg.withAlpha(0.9f));
    }

    // 3. Fader Handle (Illuminated Ring)
    const float handleY = std::clamp(trackBottom - filledH - HANDLE_HEIGHT * 0.5f,
                                     trackTop - HANDLE_HEIGHT * 0.5f,
                                     trackBottom - HANDLE_HEIGHT * 0.5f);
    
    // Wider tech handle
    const float handleW = 28.0f;
    const float handleX = bounds.x + (bounds.width - handleW) * 0.5f;
    const float handleH = HANDLE_HEIGHT;
    
    NUIRect handleRect{handleX, handleY, handleW, handleH};
    float handleRad = 3.0f;

    // Handle Body (Dark Glass)
    renderer.fillRoundedRect(handleRect, handleRad, NUIColor(0.15f, 0.15f, 0.20f, 0.95f));
    
    // Handle Border (Neon when active/hovered)
    NUIColor handleBorder = isHovered() || m_dragging ? m_trackFg : NUIColor(1.0f, 1.0f, 1.0f, 0.3f);
    renderer.strokeRoundedRect(handleRect, handleRad, 1.0f, handleBorder);
    
    // Center "Light Slit"
    float slitW = 14.0f;
    float slitH = 2.5f;
    renderer.fillRoundedRect(
        NUIRect{handleX + (handleW - slitW)*0.5f, handleY + (handleH - slitH)*0.5f, slitW, slitH}, 
        1.0f, 
        isHovered() || m_dragging ? m_trackFg : NUIColor(1.0f, 1.0f, 1.0f, 0.6f)
    );

    // Drag Value Tooltip
    if (m_dragging) {
        const float tipW = 38.0f;
        const float tipH = 18.0f;
        float tipX = handleX + (handleW - tipW) * 0.5f;
        float tipY = handleY - tipH - 4.0f;
        
        // Flip to bottom if near top edge
        if (tipY < bounds.y) {
            tipY = handleY + handleH + 4.0f;
        }

        renderer.fillRoundedRect({tipX, tipY, tipW, tipH}, 3.0f, NUIColor(0.05f, 0.05f, 0.08f, 0.95f));
        renderer.strokeRoundedRect({tipX, tipY, tipW, tipH}, 3.0f, 1.0f, m_trackFg.withAlpha(0.5f));
        renderer.drawTextCentered(m_cachedText, {tipX, tipY, tipW, tipH}, 10.5f, m_text);
    }

    // Value readout (bottom)
    const float fontSize = 10.5f;
    NUIRect textRect{bounds.x, trackBottom + 2.0f, bounds.width, bounds.y + bounds.height - trackBottom};
    renderer.drawTextCentered(m_cachedText, textRect, fontSize, m_textSecondary);
}

/**
 * @brief Handle mouse input for the mixer fader, updating hover state, initiating/ending drags, and changing the fader value.
 *
 * Processes mouse presses, releases, moves, and double-clicks:
 * - Updates hover state based on cursor position.
 * - Double-clicking the left button resets the fader to its default dB value.
 * - Left-button press begins a drag; clicking outside the handle moves the fader to the clicked position.
 * - While dragging, pointer movement adjusts the fader value; holding Shift reduces sensitivity, and holding Ctrl or Alt snaps changes to increments defined by `SNAP_DB`.
 * - Drag latching: small initial movements within `DRAG_SLOP` do not change the value until movement exceeds the slop threshold.
 *
 * @param event The mouse event to handle.
 * @return bool `true` if the event was handled (consumed) by the fader, `false` otherwise.
 */
bool UIMixerFader::onMouseEvent(const NUIMouseEvent& event)
{
    if (!isVisible() || !isEnabled()) return false;

    auto bounds = getBounds();
    setHovered(bounds.contains(event.position));
    if (!bounds.contains(event.position) && !m_dragging) return false;

    // Double-click reset
    if (event.doubleClick && event.pressed && event.button == NUIMouseButton::Left) {
        setValueDb(m_defaultDb);
        return true;
    }

    if (event.pressed && event.button == NUIMouseButton::Left) {
        m_dragging = true;
        m_dragLatched = false;
        m_dragStartPos = event.position;
        const float trackTop = bounds.y + TOP_PAD;
        const float trackBottom = bounds.y + bounds.height - BOTTOM_PAD;
        const float trackHeight = std::max(1.0f, trackBottom - trackTop);
        const float currentNorm = (m_valueDb - m_minDb) / std::max(1e-3f, (m_maxDb - m_minDb));
        const float filledH = std::clamp(currentNorm, 0.0f, 1.0f) * trackHeight;
        const float handleY = std::clamp(trackBottom - filledH - HANDLE_HEIGHT * 0.5f,
                                         trackTop - HANDLE_HEIGHT * 0.5f,
                                         trackBottom - HANDLE_HEIGHT * 0.5f);
        const float handleW = 28.0f;
        const float handleH = HANDLE_HEIGHT;
        const float handleX = bounds.x + (bounds.width - handleW) * 0.5f;
        const NUIRect handleRect{handleX, handleY, handleW, handleH};

        if (!handleRect.contains(event.position)) {
            const float norm = std::clamp(1.0f - (event.position.y - trackTop) / trackHeight, 0.0f, 1.0f);
            const float clickedDb = m_minDb + norm * (m_maxDb - m_minDb);
            setValueDb(clickedDb);
        }

        m_dragStartDb = m_valueDb;
        return true;
    }

    if (event.released && event.button == NUIMouseButton::Left && m_dragging) {
        m_dragging = false;
        m_dragLatched = false;
        return true;
    }

    // Dragging (mouse move events set button = None)
    if (m_dragging && event.button == NUIMouseButton::None) {
        const float trackTop = bounds.y + TOP_PAD;
        const float trackBottom = bounds.y + bounds.height - BOTTOM_PAD;
        const float trackHeight = std::max(1.0f, trackBottom - trackTop);
        const float dbPerPixel = (m_maxDb - m_minDb) / trackHeight;
        const float deltaPx = (m_dragStartPos.y - event.position.y);
        const float absDelta = std::abs(deltaPx);

        float sensitivity = 1.0f;
        if (event.modifiers & NUIModifiers::Shift) {
            sensitivity *= 0.2f;
        }
        if (!m_dragLatched && absDelta < DRAG_SLOP) {
            return true;
        }
        if (!m_dragLatched) {
            m_dragLatched = true;
            m_dragStartDb = m_valueDb;
            m_dragStartPos = event.position;
            return true;
        }

        float nextDb = m_dragStartDb + deltaPx * dbPerPixel * sensitivity;

        if ((event.modifiers & NUIModifiers::Ctrl) || (event.modifiers & NUIModifiers::Alt)) {
            nextDb = std::round(nextDb / SNAP_DB) * SNAP_DB;
        }

        setValueDb(nextDb);
        return true;
    }

    return false;
}

} // namespace AestraUI
