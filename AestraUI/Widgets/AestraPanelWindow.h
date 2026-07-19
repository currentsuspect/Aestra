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
 * @brief Unified panel chrome for all child windows (plugin editors, Settings, etc.).
 *
 * Renders a 32 px title bar with:
 *   - background: --color-background-primary
 *   - bottom border: 0.5 px solid --color-border-tertiary
 *   - title: 12 px, weight 500, --color-text-secondary, uppercase, left-aligned x=12
 *   - optional close button (right-aligned x=width-28)
 *   - optional badge pill (right of title)
 *
 * Derived classes override drawContent() to render inside the content area below the bar.
 */
class AestraPanelWindow : public NUIComponent {
public:
    AestraPanelWindow();

    void onRender(NUIRenderer& renderer) override;
    void onThemeChanged(const NUIThemeProperties& theme) override { cacheThemeColors(); NUIComponent::onThemeChanged(theme); }
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setPanelTitle(const std::string& title);
    void setBadgeText(const std::string& text);   // empty = no badge
    void setOnClose(std::function<void()> callback) { m_onClose = std::move(callback); }
    void setCloseOnOutsideClick(bool close) { m_closeOnOutsideClick = close; }
    bool isDraggingWindow() const { return m_isDraggingWindow; }

    // Derived classes render here; rect is the global content area (below title bar)
    virtual void drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {}

    // Called when a title-bar drag ends (before m_isDraggingWindow is cleared)
    virtual void onDragEnd() {}

    // Helpers
    static constexpr float TITLE_BAR_H = 32.0f;
    static constexpr float kRadius = 14.0f;
    NUIRect getContentRect() const;
    bool hitTestCloseButton(float x, float y) const;
    bool hitTestTitleBar(float x, float y) const;

    // Consume any mouse event that lands inside the panel bounds. A floating
    // editor is opaque to the mouse over its own area: a click on empty panel
    // space (hitting no control) must NOT fall through to widgets behind it,
    // which would otherwise both act on the click-through and dismiss the
    // editor. Subclasses call this as their onMouseEvent fall-through, after
    // all interactive controls have had a chance to consume the event.
    bool consumeInsideBounds(const NUIMouseEvent& event) const { return getBounds().contains(event.position); }

    void setEnforceParentBounds(bool enforce) { m_enforceParentBounds = enforce; }
    void enforceBoundsInParent(float safeMargin = 14.0f);

    // Platform bridge access for cursor capture (rotary knobs, etc.)
    virtual void setPlatformBridge(NUIPlatformBridge* bridge);
    NUIPlatformBridge* getPlatformBridge() const { return m_platformBridge; }

    void onUpdate(double deltaTime) override;

    ~AestraPanelWindow() override; // cancels an active knob capture (see .cpp)

protected:
    // Shared infinite-drag capture for editor knobs. Continuous parameter
    // knobs call beginKnobCapture() on press (hides + confines + routes here)
    // and endKnobCapture() on release (restores the cursor at the knob
    // center). During the drag, read the vertical delta from event.delta.y
    // (service-owned; do NOT read absolute cursor position). No-op when no
    // platform bridge is attached.
    void beginKnobCapture(const NUIPoint& knobCenter, const NUIPoint& grabPos);
    void endKnobCapture();
    // Center the active capture will restore to.
    NUIPoint m_captureKnobCenter;

    // Normalized vertical step for a knob drag during capture (up = positive).
    // rangePx = pixels of travel for the full 0..1 range. Scaled down while
    // Shift is held — opt-in fine/precision drag; the default (unmodified)
    // feel is unchanged. Add to the current value each drag frame.
    float knobDragStep(const NUIMouseEvent& event, float rangePx) const;

    // Window dragging from title bar
    bool m_isDraggingWindow = false;
    NUIPoint m_dragStartPos;
    NUIPoint m_windowStartPos;

    bool m_enforceParentBounds = false;
    bool m_userPositioned = false;
    bool m_haveParentSnapshot = false;
    NUIRect m_lastParentBounds;

    // Platform bridge for cursor capture
    NUIPlatformBridge* m_platformBridge = nullptr;

private:
    void cacheThemeColors();
    void drawTitleBar(NUIRenderer& renderer);

    std::string m_title;
    std::string m_badgeText;
    std::function<void()> m_onClose;
    bool m_closeOnOutsideClick = true;
    bool m_closeHovered = false;
    bool m_closePressed = false;

    NUIColor m_bg;
    NUIColor m_border;
    NUIColor m_textSecondary;
    NUIColor m_textTertiary;
    NUIColor m_closeHover;
    NUIColor m_closeActive;
};

} // namespace AestraUI
