// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"
#include "../Core/NUITypes.h"
#include <functional>
#include <string>

namespace NomadUI {

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

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setRangeDb(float minDb, float maxDb);
    void setDefaultDb(float db) { m_defaultDb = db; }

    void setValueDb(float db);
    float getValueDb() const { return m_valueDb; }

    bool isDragging() const { return m_dragging; }

    std::function<void(float db)> onValueChanged;

private:
    float m_minDb{-90.0f};
    float m_maxDb{6.0f};
    float m_defaultDb{0.0f};
    float m_valueDb{0.0f};

    bool m_dragging{false};
    NUIPoint m_dragStartPos{};
    float m_dragStartDb{0.0f};

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

    void cacheThemeColors();
    void updateCachedText();
    float clampDb(float db) const;
};

} // namespace NomadUI

