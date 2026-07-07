// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerFader.h"

#include "NUIThemeSystem.h"
#include "NUIRenderer.h"
#include "../Platform/NUIPlatformBridge.h"

#include <algorithm>
#include <cstdio>
#include <cmath>

namespace AestraUI {

namespace {
    constexpr float TRACK_RADIUS = 4.0f;
    constexpr float HANDLE_RADIUS = 4.0f;
    constexpr float HANDLE_HEIGHT = 18.0f;
    constexpr float TOP_PAD = 18.0f;    // room for fixed dB readout at top
    constexpr float BOTTOM_PAD = 6.0f;   // minimal padding below track
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
    m_trackBg = AestraUI::NUIColor(0.05f, 0.05f, 0.05f, 0.6f);
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

    // "−∞" at or below silence floor
    if (m_valueDb <= FADER_FLOOR_THRESHOLD) {
        m_cachedText = "\xE2\x88\x92\xE2\x88\x9E";
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

void UIMixerFader::onRender(NUIRenderer& renderer)
{
    auto bounds = getBounds();

    // Track area
    const float trackTop = bounds.y + TOP_PAD;
    const float trackBottom = bounds.y + bounds.height - BOTTOM_PAD;
    const float trackHeight = std::max(1.0f, trackBottom - trackTop);

    // Narrow rail (4 px) centered in strip
    const float trackWidth = 4.0f;
    const float trackX = bounds.x + (bounds.width - trackWidth) * 0.5f;
    NUIRect trackRect{trackX, trackTop, trackWidth, trackHeight};

    // 1. Track Background (deep recessed slot)
    renderer.fillRoundedRect(trackRect, 2.0f, m_trackBg);
    renderer.strokeRoundedRect(trackRect, 2.0f, 1.0f, NUIColor(0.0f, 0.0f, 0.0f, 0.6f));

    // 2. Filled Portion (subtle, no wide glow)
    const float norm = (m_valueDb - m_minDb) / std::max(1e-3f, (m_maxDb - m_minDb));
    const float filledH = std::clamp(norm, 0.0f, 1.0f) * trackHeight;

    if (filledH > 0.0f) {
        NUIRect fillRect{trackX, trackBottom - filledH, trackWidth, filledH};
        // Core fill (slightly wider than track for crisp edge)
        renderer.fillRoundedRect(fillRect, 2.0f, m_trackFg.withAlpha(0.85f));
        // Very subtle inner glow (2px bleed)
        renderer.fillRoundedRect(
            NUIRect{fillRect.x - 1, fillRect.y, fillRect.width + 2, fillRect.height},
            3.0f,
            m_trackFg.withAlpha(0.15f)
        );
    }

    // 3. Fader Handle (distinct grab block)
    const float handleY = std::clamp(trackBottom - filledH - HANDLE_HEIGHT * 0.5f,
                                     trackTop - HANDLE_HEIGHT * 0.5f,
                                     trackBottom - HANDLE_HEIGHT * 0.5f);

    // Handle: wider than track, clear visual weight
    const float handleW = 32.0f;
    const float handleX = bounds.x + (bounds.width - handleW) * 0.5f;
    const float handleH = HANDLE_HEIGHT;

    NUIRect handleRect{handleX, handleY, handleW, handleH};
    float handleRad = 4.0f;

    // Handle Body (dark surface) — flat, no drop shadow; the border + slit
    // give it enough definition.
    renderer.fillRoundedRect(handleRect, handleRad, NUIColor(0.18f, 0.18f, 0.18f, 0.98f));

    // Handle Border (accent when active/hovered, subtle white otherwise)
    bool handleActive = isHovered() || m_dragging;
    NUIColor handleBorder = handleActive ? m_trackFg : NUIColor(1.0f, 1.0f, 1.0f, 0.25f);
    renderer.strokeRoundedRect(handleRect, handleRad, 1.0f, handleBorder);

    // Center accent line (the "light slit")
    float slitW = 16.0f;
    float slitH = 3.0f;
    renderer.fillRoundedRect(
        NUIRect{handleX + (handleW - slitW)*0.5f, handleY + (handleH - slitH)*0.5f, slitW, slitH},
        1.5f,
        handleActive ? m_trackFg : NUIColor(1.0f, 1.0f, 1.0f, 0.5f)
    );

    // 4. Fixed dB readout at top of fader area (not attached to handle)
    const float fontSize = 10.5f;
    NUIRect textRect{bounds.x, bounds.y + 2.0f, bounds.width, TOP_PAD - 4.0f};
    renderer.drawTextCentered(m_cachedText, textRect, fontSize, m_textSecondary);

    // 5. Drag Value Tooltip (only while dragging)
    if (m_dragging) {
        const float tipW = 38.0f;
        const float tipH = 18.0f;
        float tipX = handleX + (handleW - tipW) * 0.5f;
        float tipY = handleY - tipH - 4.0f;

        // Flip to bottom if near top edge
        if (tipY < bounds.y) {
            tipY = handleY + handleH + 4.0f;
        }

        renderer.fillRoundedRect({tipX, tipY, tipW, tipH}, 3.0f, NUIColor(0.05f, 0.05f, 0.05f, 0.95f));
        renderer.strokeRoundedRect({tipX, tipY, tipW, tipH}, 3.0f, 1.0f, m_trackFg.withAlpha(0.5f));
        renderer.drawTextCentered(m_cachedText, {tipX, tipY, tipW, tipH}, 10.5f, m_text);
    }
}

void UIMixerFader::setPlatformBridge(NUIPlatformBridge* bridge)
{
    m_platformBridge = bridge;
}

bool UIMixerFader::onMouseEvent(const NUIMouseEvent& event)
{
    if (!isVisible() || !isEnabled()) return false;

    auto bounds = getBounds();
    if (!event.cursorCaptured) {
        setHovered(bounds.contains(event.position));
    }
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
        const float handleW = 32.0f;
        const float handleH = HANDLE_HEIGHT;
        const float handleX = bounds.x + (bounds.width - handleW) * 0.5f;
        const NUIRect handleRect{handleX, handleY, handleW, handleH};

        if (!handleRect.contains(event.position)) {
            const float norm = std::clamp(1.0f - (event.position.y - trackTop) / trackHeight, 0.0f, 1.0f);
            const float clickedDb = m_minDb + norm * (m_maxDb - m_minDb);
            setValueDb(clickedDb);
        }

        m_dragStartDb = m_valueDb;

        // Cursor-warp setup for infinite drag
        if (m_platformBridge) {
            // Initialize drag origin and tracking
            m_warpOrigin = event.position;
            m_lastDragY = event.position.y;
            m_platformBridge->setCursorStyle(NUICursorStyle::Hidden);
        }

        return true;
    }

    if (event.released && event.button == NUIMouseButton::Left && m_dragging) {
        m_dragging = false;
        m_dragLatched = false;

        if (m_platformBridge) {
            // Warp cursor to the fader handle position (matches current value),
            // not to the click origin — user expects cursor at the slider thumb.
            auto bounds = getBounds();
            const float trackTop = bounds.y + TOP_PAD;
            const float trackBottom = bounds.y + bounds.height - BOTTOM_PAD;
            const float trackHeight = std::max(1.0f, trackBottom - trackTop);
            const float norm = (m_valueDb - m_minDb) / std::max(1e-3f, (m_maxDb - m_minDb));
            const float filledH = std::clamp(norm, 0.0f, 1.0f) * trackHeight;
            const float handleY = std::clamp(trackBottom - filledH - HANDLE_HEIGHT * 0.5f,
                                             trackTop - HANDLE_HEIGHT * 0.5f,
                                             trackBottom - HANDLE_HEIGHT * 0.5f);
            const float handleW = 32.0f;
            const float handleX = bounds.x + (bounds.width - handleW) * 0.5f;

            m_platformBridge->setCursorPosition(
                static_cast<int>(handleX + handleW * 0.5f),
                static_cast<int>(handleY + HANDLE_HEIGHT * 0.5f));
            m_platformBridge->setCursorStyle(NUICursorStyle::Arrow);
        }

        return true;
    }

    // Dragging (mouse move events set button = None)
    if (m_dragging && event.button == NUIMouseButton::None) {
        // Cursor-warp mode: use frame-to-frame delta
        if (m_platformBridge) {
            // Compute frame-to-frame delta
            float dy = event.position.y - m_lastDragY;
            m_lastDragY = event.position.y;

            // Invert Y for fader: up = positive
            const float deltaPx = -dy;
            const float trackTop = bounds.y + TOP_PAD;
            const float trackBottom = bounds.y + bounds.height - BOTTOM_PAD;
            const float trackHeight = std::max(1.0f, trackBottom - trackTop);
            const float dbPerPixel = (m_maxDb - m_minDb) / trackHeight;

            // Reduced sensitivity for cursor-warp mode
            float sensitivity = (event.modifiers & NUIModifiers::Shift) ? 0.1f : 0.5f;
            float nextDb = m_valueDb + deltaPx * dbPerPixel * sensitivity;

            if ((event.modifiers & NUIModifiers::Ctrl) || (event.modifiers & NUIModifiers::Alt)) {
                nextDb = std::round(nextDb / SNAP_DB) * SNAP_DB;
            }

            setValueDb(nextDb);
            return true;
        }

        // Non-warp mode: use drag start position with latching
        const float deltaPx = m_dragStartPos.y - event.position.y;
        const float trackTop = bounds.y + TOP_PAD;
        const float trackBottom = bounds.y + bounds.height - BOTTOM_PAD;
        const float trackHeight = std::max(1.0f, trackBottom - trackTop);
        const float dbPerPixel = (m_maxDb - m_minDb) / trackHeight;
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
