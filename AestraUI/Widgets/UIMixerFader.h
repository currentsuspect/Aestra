// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITypes.h"
#include <functional>
#include <string>

namespace AestraUI {

// Forward declaration
class NUIPlatformBridge;

/**
 * @brief Vertical dB fader widget for the modern mixer UI.
 *
 * - Range: typically -90 dB to +6 dB
 * - Shift drag: fine mode (0.1x sensitivity)
 * - Ctrl/Alt drag: snap mode (0.5 dB increments)
 * - Double-click: reset to 0 dB
 */
class UIMixerFader : public NUIComponent {
public:
    UIMixerFader();
    ~UIMixerFader() override; // cancels an active cursor capture (see .cpp)

    void onRender(NUIRenderer& renderer) override;
    void onThemeChanged(const NUIThemeProperties& theme) override { cacheThemeColors(); NUIComponent::onThemeChanged(theme); }
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setRangeDb(float minDb, float maxDb);
    void setDefaultDb(float db) { m_defaultDb = db; }

    void setValueDb(float db);
    float getValueDb() const { return m_valueDb; }

    // Per-channel accent: fill/handle-active colour follows the track colour.
    void setAccentColor(const NUIColor& color) { m_trackFg = color; repaint(); }

    bool isDragging() const { return m_dragging; }

    std::function<void(float db)> onValueChanged;

    // Platform bridge for cursor warp (infinite drag)
    void setPlatformBridge(NUIPlatformBridge* bridge);

private:
    float m_minDb{-90.0f};
    float m_maxDb{6.0f};
    float m_defaultDb{0.0f};
    float m_valueDb{0.0f};

    bool m_dragging{false};
    bool m_dragLatched{false};
    NUIPoint m_dragStartPos{};
    float m_dragStartDb{0.0f};

    // Cursor-warp state (infinite drag)
    NUIPlatformBridge* m_platformBridge = nullptr;
    float m_lastDragY;            // Last cursor Y position for frame-to-frame delta

    // Cached value string (updated only on value change)
    float m_cachedDbValue{1000.0f};
    std::string m_cachedText;

    // Cached theme colors
    NUIColor m_trackBg;
    NUIColor m_trackFg;
    NUIColor m_handle;
    NUIColor m_handleHover;
    NUIColor m_text;
    NUIColor m_textSecondary;
    NUIColor m_border;
    NUIColor m_tooltipBg;

    static constexpr float FADER_FLOOR_THRESHOLD = -60.0f;

    void cacheThemeColors();
    void updateCachedText();
    float clampDb(float db) const;
};

} // namespace AestraUI
