// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerFader.h"

#include "../Core/NUIThemeSystem.h"
#include "../Graphics/NUIRenderer.h"

#include <algorithm>
#include <cstdio>
#include <cmath>

namespace NomadUI {

namespace {
    constexpr float TRACK_RADIUS = 3.0f;
    constexpr float HANDLE_RADIUS = 3.0f;
    constexpr float HANDLE_HEIGHT = 12.0f;
    constexpr float TOP_PAD = 8.0f;
    constexpr float BOTTOM_PAD = 18.0f; // room for text
    constexpr float SNAP_DB = 0.5f;
}

UIMixerFader::UIMixerFader()
{
    cacheThemeColors();
    updateCachedText();
}

void UIMixerFader::cacheThemeColors()
{
    auto& theme = NUIThemeManager::getInstance();
    m_trackBg = theme.getColor("backgroundSecondary");
    m_trackFg = theme.getColor("accentPrimary");
    m_handle = theme.getColor("textPrimary");
    m_handleHover = theme.getColor("accentPrimary");
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

void UIMixerFader::onRender(NUIRenderer& renderer)
{
    auto bounds = getBounds();

    // Track area
    const float trackTop = bounds.y + TOP_PAD;
    const float trackBottom = bounds.y + bounds.height - BOTTOM_PAD;
    const float trackHeight = std::max(1.0f, trackBottom - trackTop);

    const float trackWidth = std::max(6.0f, bounds.width * 0.35f);
    const float trackX = bounds.x + (bounds.width - trackWidth) * 0.5f;
    NUIRect trackRect{trackX, trackTop, trackWidth, trackHeight};

    // Background track
    renderer.fillRoundedRect(trackRect, TRACK_RADIUS, m_trackBg);

    // Filled portion (from bottom)
    const float norm = (m_valueDb - m_minDb) / std::max(1e-3f, (m_maxDb - m_minDb));
    const float filledH = std::clamp(norm, 0.0f, 1.0f) * trackHeight;
    if (filledH > 0.0f) {
        NUIRect fillRect{trackX, trackBottom - filledH, trackWidth, filledH};
        renderer.fillRoundedRect(fillRect, TRACK_RADIUS, m_trackFg.withAlpha(0.55f));
    }

    // Handle
    const float handleY = std::clamp(trackBottom - filledH - HANDLE_HEIGHT * 0.5f,
                                     trackTop - HANDLE_HEIGHT * 0.5f,
                                     trackBottom - HANDLE_HEIGHT * 0.5f);
    const float handleW = std::max(12.0f, bounds.width * 0.8f);
    const float handleX = bounds.x + (bounds.width - handleW) * 0.5f;
    NUIRect handleRect{handleX, handleY, handleW, HANDLE_HEIGHT};
    renderer.fillRoundedRect(handleRect, HANDLE_RADIUS, isHovered() ? m_handleHover : m_handle);

    // Value readout (bottom)
    const float fontSize = 10.0f;
    NUIRect textRect{bounds.x, trackBottom, bounds.width, bounds.y + bounds.height - trackBottom};
    renderer.drawTextCentered(m_cachedText, textRect, fontSize, m_textSecondary);
}

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
        m_dragStartPos = event.position;

        // Click-to-set
        const float trackTop = bounds.y + TOP_PAD;
        const float trackBottom = bounds.y + bounds.height - BOTTOM_PAD;
        const float trackHeight = std::max(1.0f, trackBottom - trackTop);
        const float norm = std::clamp(1.0f - (event.position.y - trackTop) / trackHeight, 0.0f, 1.0f);
        const float clickedDb = m_minDb + norm * (m_maxDb - m_minDb);
        setValueDb(clickedDb);

        m_dragStartDb = m_valueDb;
        return true;
    }

    if (event.released && event.button == NUIMouseButton::Left && m_dragging) {
        m_dragging = false;
        return true;
    }

    // Dragging (mouse move events set button = None)
    if (m_dragging && event.button == NUIMouseButton::None) {
        const float trackTop = bounds.y + TOP_PAD;
        const float trackBottom = bounds.y + bounds.height - BOTTOM_PAD;
        const float trackHeight = std::max(1.0f, trackBottom - trackTop);
        const float dbPerPixel = (m_maxDb - m_minDb) / trackHeight;

        float sensitivity = 1.0f;
        if (event.modifiers & NUIModifiers::Shift) {
            sensitivity *= 0.1f;
        }

        const float deltaPx = (m_dragStartPos.y - event.position.y);
        float nextDb = m_dragStartDb + deltaPx * dbPerPixel * sensitivity;

        if ((event.modifiers & NUIModifiers::Ctrl) || (event.modifiers & NUIModifiers::Alt)) {
            nextDb = std::round(nextDb / SNAP_DB) * SNAP_DB;
        }

        setValueDb(nextDb);
        return true;
    }

    return false;
}

} // namespace NomadUI
