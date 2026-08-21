// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUIDoubleClick.h"
#include "NUITypes.h"
#include <functional>
#include <memory>
#include <string>

namespace AestraUI {

// Forward declaration
class NUIPlatformBridge;
class NUITextInput;

/**
 * @brief Vertical dB fader widget for the modern mixer UI.
 *
 * - Range: typically -90 dB to +6 dB
 * - Shift drag: fine mode (0.1x sensitivity)
 * - Ctrl/Alt drag: snap mode (0.5 dB increments)
 * - Double-click: reset to 0 dB
 * - Click the dB readout: type an exact value
 *
 * The rail reads as *control state* only: it is deliberately neutral so the
 * adjacent meter stays the sole carrier of live signal colour. The per-channel
 * accent is used sparingly (active handle edge), never as the rail fill.
 */
class UIMixerFader : public NUIComponent {
public:
    UIMixerFader();
    ~UIMixerFader() override; // cancels an active cursor capture (see .cpp)

    void onRender(NUIRenderer& renderer) override;
    void onThemeChanged(const NUIThemeProperties& theme) override { cacheThemeColors(); NUIComponent::onThemeChanged(theme); }
    bool onMouseEvent(const NUIMouseEvent& event) override;
    bool onKeyEvent(const NUIKeyEvent& event) override;
    void onMouseEnter() override;
    void onMouseLeave() override;

    void setRangeDb(float minDb, float maxDb);
    void setDefaultDb(float db) { m_defaultDb = db; }

    void setValueDb(float db);
    float getValueDb() const { return m_valueDb; }

    /// Per-channel accent. Tints the active handle edge only — never the rail
    /// fill, which would make the fader read as a level meter.
    void setAccentColor(const NUIColor& color) { m_accent = color; repaint(); }

    /// Wider scale/readout treatment for the master strip.
    void setShowScaleLabels(bool show) { m_showScaleLabels = show; repaint(); }

    /// At or below this, a gain readout shows "−∞" rather than a number.
    /// Public because any readout of the same value has to agree with the
    /// fader — two copies of the threshold drift the moment one is tuned.
    static constexpr float FADER_FLOOR_THRESHOLD = -60.0f;

    bool isDragging() const { return m_dragging; }
    bool isEditing() const { return m_editing; }

    /**
     * @brief Commit an open inline edit when a press lands outside this fader.
     *
     * A press on another strip is never routed to this component, so the editor
     * cannot dismiss itself and would stay open indefinitely. The panel calls
     * this on every press so the mixer has one dismissal path.
     */
    void dismissEditAt(const NUIPoint& position);

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

    // Inline numeric entry (same transient-NUITextInput pattern as UnitNameLabel).
    bool m_editing{false};
    std::shared_ptr<NUITextInput> m_textInput;

    /// Editor kept alive until the next frame.
    ///
    /// commitEdit() runs from NUITextInput::onFocusLost(), which touches its own
    /// members again after the callback returns. Dropping the last reference
    /// inside that callback would free the object mid-call, so the retired
    /// editor parks here and is released in onRender(), off that stack.
    std::shared_ptr<NUITextInput> m_retiredInput;

    // The platform never populates NUIMouseEvent::doubleClick.
    NUIDoubleClickTracker m_clickTracker;

    bool m_showScaleLabels{false};

    // Cursor-warp state (infinite drag)
    NUIPlatformBridge* m_platformBridge = nullptr;
    float m_lastDragY;            // Last cursor Y position for frame-to-frame delta

    // Cached value string (updated only on value change)
    float m_cachedDbValue{1000.0f};
    std::string m_cachedText;

    // Cached theme colors
    NUIColor m_trackBg;
    NUIColor m_railFill;      // neutral "how far up is this set" fill
    NUIColor m_accent;        // per-channel identity, used sparingly
    NUIColor m_handle;
    NUIColor m_handleHover;
    NUIColor m_text;
    NUIColor m_textSecondary;
    NUIColor m_border;
    NUIColor m_tick;
    NUIColor m_tickUnity;
    NUIColor m_tooltipBg;

    void cacheThemeColors();
    void updateCachedText();
    float clampDb(float db) const;

    /// Drawn width of the handle, and therefore its drag hit-box.
    float handleWidth() const;

    /// Readout band at the top of the fader — click target for inline entry.
    NUIRect readoutRect() const;
    float dbToY(float db, float trackTop, float trackHeight) const;
    void renderScale(NUIRenderer& renderer, float trackX, float trackWidth,
                     float trackTop, float trackHeight, bool showLabels);

    void beginEdit();
    void commitEdit();
    void cancelEdit();
};

} // namespace AestraUI
