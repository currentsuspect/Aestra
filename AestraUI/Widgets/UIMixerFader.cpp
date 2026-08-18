// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerFader.h"

#include "NUIThemeSystem.h"
#include "NUIRenderer.h"
#include "NUITextInput.h"
#include "../Platform/NUIPlatformBridge.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <exception>
#include <locale>
#include <sstream>
#include <string>

namespace AestraUI {

namespace {
    constexpr float TRACK_RADIUS = 4.0f;
    constexpr float HANDLE_RADIUS = 4.0f;
    constexpr float HANDLE_HEIGHT = 22.0f;  // taller cap: a real grab target
    constexpr float HANDLE_WIDTH = 30.0f;
    constexpr float TRACK_WIDTH = 6.0f;     // was 4 — thin enough to look like a meter
    constexpr float TOP_PAD = 18.0f;    // room for fixed dB readout at top
    constexpr float BOTTOM_PAD = 6.0f;   // minimal padding below track
    constexpr float SNAP_DB = 0.5f;
    constexpr float DRAG_SLOP = 1.5f;

    // Restrained scale: unity plus a few orientation marks. Unity (0 dB) is the
    // only one that gets emphasis and a label.
    constexpr float SCALE_TICKS[] = {6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f};

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
    m_trackBg = theme.getColor("meterBackground").withAlpha(0.72f);
    // Rail fill is deliberately NEUTRAL. Colouring it with the channel accent
    // made the fader read as a second level meter — the fader shows where the
    // control is set, the meter beside it shows what the signal is doing.
    m_railFill = theme.getColor("textPrimary").withAlpha(0.55f);
    m_accent = theme.getColor("accentPrimary");
    m_handle = theme.getColor("backgroundSecondary"); // Handle Core
    m_handleHover = theme.getColor("textPrimary");    // Handle Active
    m_text = theme.getColor("textPrimary");
    m_textSecondary = theme.getColor("textSecondary");
    m_border = theme.getColor("borderStrong");
    m_tick = theme.getColor("textSecondary").withAlpha(0.34f);
    m_tickUnity = theme.getColor("textPrimary").withAlpha(0.72f);
    m_tooltipBg = theme.getColor("elevatedPanel").withAlpha(0.98f);
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
    std::snprintf(buf, sizeof(buf), "%.1f dB", m_valueDb);
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

float UIMixerFader::handleWidth() const
{
    // The drawn handle and the drag hit-box must be the same rectangle; keeping
    // one formula is what guarantees that.
    return std::min(HANDLE_WIDTH, getBounds().width - 4.0f);
}

NUIRect UIMixerFader::readoutRect() const
{
    const auto bounds = getBounds();
    return NUIRect{bounds.x, bounds.y + 2.0f, bounds.width, TOP_PAD - 4.0f};
}

float UIMixerFader::dbToY(float db, float trackTop, float trackHeight) const
{
    const float norm = (db - m_minDb) / std::max(1e-3f, (m_maxDb - m_minDb));
    return trackTop + (1.0f - std::clamp(norm, 0.0f, 1.0f)) * trackHeight;
}

void UIMixerFader::renderScale(NUIRenderer& renderer, float trackX, float trackWidth,
                               float trackTop, float trackHeight, bool showLabels)
{
    // Ticks flank the rail on both sides so the cap never hides the scale.
    constexpr float TICK_LEN = 4.0f;
    constexpr float TICK_GAP = 3.0f;
    const float leftEnd = trackX - TICK_GAP;
    const float rightStart = trackX + trackWidth + TICK_GAP;

    // The scale is structural, not a second readout. Unity is useful orientation
    // at a glance; the complete dB ladder is revealed for the master strip or
    // while the musician is directly manipulating this fader.
    char labelBuf[8];
    for (float db : SCALE_TICKS) {
        if (db > m_maxDb || db < m_minDb) continue;

        const bool unity = (std::abs(db) < 0.01f);
        const float y = dbToY(db, trackTop, trackHeight);
        const float len = unity ? TICK_LEN + 2.0f : TICK_LEN;
        const float thickness = unity ? 2.0f : 1.0f;
        const NUIColor color =
            unity ? m_tickUnity : (showLabels ? m_tick : m_tick.withAlpha(m_tick.a * 0.58f));

        renderer.fillRect(NUIRect{leftEnd - len, y - thickness * 0.5f, len, thickness}, color);
        renderer.fillRect(NUIRect{rightStart, y - thickness * 0.5f, len, thickness}, color);

        if (!showLabels && !unity) {
            continue;
        }

        const char* label = "0";
        if (!unity) {
            std::snprintf(labelBuf, sizeof(labelBuf), "%+d", static_cast<int>(db));
            label = labelBuf;
        }
        const float labelSize = renderer.measureText(label, 7.5f).width;
        // Right-align labels against the tick so they remain a contextual
        // measurement aid instead of forming a permanent text column.
        renderer.drawText(label,
                          {leftEnd - TICK_LEN - 5.0f - labelSize, y - 3.5f},
                          unity ? 8.0f : 7.5f,
                          unity ? m_tickUnity : m_tick.withAlpha(showLabels ? m_tick.a : m_tick.a * 0.72f));
    }
}

void UIMixerFader::onRender(NUIRenderer& renderer)
{
    // Safe point to free a committed/cancelled editor: we are no longer inside
    // any NUITextInput callback.
    m_retiredInput.reset();

    auto bounds = getBounds();

    // Track area
    const float trackTop = bounds.y + TOP_PAD;
    const float trackBottom = bounds.y + bounds.height - BOTTOM_PAD;
    const float trackHeight = std::max(1.0f, trackBottom - trackTop);

    const float trackWidth = TRACK_WIDTH;
    const float trackX = bounds.x + (bounds.width - trackWidth) * 0.5f;
    NUIRect trackRect{trackX, trackTop, trackWidth, trackHeight};

    // 1. Scale first, so the rail and cap sit on top of it.
    const bool showScaleLabels = m_showScaleLabels || isHovered() || m_dragging;
    renderScale(renderer, trackX, trackWidth, trackTop, trackHeight, showScaleLabels);

    // 2. Track Background (deep recessed slot)
    renderer.fillRoundedRect(trackRect, 3.0f, m_trackBg);

    // 3. Filled Portion — neutral. This is control state, not signal.
    const float norm = (m_valueDb - m_minDb) / std::max(1e-3f, (m_maxDb - m_minDb));
    const float filledH = std::clamp(norm, 0.0f, 1.0f) * trackHeight;

    if (filledH > 0.0f) {
        NUIRect fillRect{trackX, trackBottom - filledH, trackWidth, filledH};
        renderer.fillRoundedRect(fillRect, 3.0f, m_railFill);
    }

    // 4. Fader Handle (distinct grab block)
    const float handleY = std::clamp(trackBottom - filledH - HANDLE_HEIGHT * 0.5f,
                                     trackTop - HANDLE_HEIGHT * 0.5f,
                                     trackBottom - HANDLE_HEIGHT * 0.5f);

    const float handleW = handleWidth();
    const float handleX = bounds.x + (bounds.width - handleW) * 0.5f;
    const float handleH = HANDLE_HEIGHT;

    NUIRect handleRect{handleX, handleY, handleW, handleH};
    float handleRad = 4.0f;

    // Handle Body (dark surface) — flat, no drop shadow; the border + grip
    // give it enough definition.
    renderer.fillRoundedRect(handleRect, handleRad, m_handle.withAlpha(0.98f));

    // Handle Border — the one place the per-channel accent appears on the fader,
    // and only while the control is actually engaged.
    bool handleActive = isHovered() || m_dragging;
    NUIColor handleBorder = handleActive ? m_accent : m_border;
    renderer.strokeRoundedRect(handleRect, handleRad, 1.0f, handleBorder);

    // Grip: three short lines read as "grab me" better than a single slit.
    const float gripW = std::min(18.0f, handleW - 10.0f);
    const float gripX = handleX + (handleW - gripW) * 0.5f;
    const float gripCy = handleY + handleH * 0.5f;
    const NUIColor gripColor = handleActive ? m_accent : m_textSecondary.withAlpha(0.85f);
    for (int i = -1; i <= 1; ++i) {
        renderer.fillRoundedRect(
            NUIRect{gripX, gripCy + static_cast<float>(i) * 4.0f - 1.0f, gripW, 2.0f},
            1.0f,
            i == 0 ? gripColor : gripColor.withAlpha(gripColor.a * 0.45f));
    }

    // 5. dB readout at top. It remains available for exact entry, but recedes
    //    on ordinary channel strips so the control shape and the adjacent meter
    //    retain visual priority. The master strip keeps a stronger readout.
    if (m_editing && m_textInput) {
        renderChildren(renderer);
    } else {
        const bool valueActive = isHovered() || m_dragging || m_showScaleLabels;
        const float fontSize = m_showScaleLabels ? 11.0f : 10.0f;
        const NUIRect textRect = readoutRect();
        if (isHovered()) {
            renderer.fillRoundedRect(textRect, 3.0f, m_trackBg.withAlpha(0.55f));
        }
        renderer.drawTextCentered(m_cachedText,
                                  textRect,
                                  fontSize,
                                  valueActive ? m_text : m_textSecondary.withAlpha(0.72f));
    }

    // 6. Drag Value Tooltip (only while dragging)
    if (m_dragging) {
        const float tipW = 38.0f;
        const float tipH = 18.0f;
        float tipX = handleX + (handleW - tipW) * 0.5f;
        float tipY = handleY - tipH - 4.0f;

        // Flip to bottom if near top edge
        if (tipY < bounds.y) {
            tipY = handleY + handleH + 4.0f;
        }

        renderer.fillRoundedRect({tipX, tipY, tipW, tipH}, 3.0f, m_tooltipBg);
        renderer.strokeRoundedRect({tipX, tipY, tipW, tipH}, 3.0f, 1.0f, m_accent.withAlpha(0.5f));
        renderer.drawTextCentered(m_cachedText, {tipX, tipY, tipW, tipH}, 10.5f, m_text);
    }
}

void UIMixerFader::setPlatformBridge(NUIPlatformBridge* bridge)
{
    m_platformBridge = bridge;
}

void UIMixerFader::beginEdit()
{
    if (m_editing && m_textInput) {
        return;
    }

    m_editing = true;

    // Seed with the plain number so the user can just type over it. "−∞" is not
    // round-trippable, so an inaudible fader starts from an empty field.
    std::string seed;
    if (m_valueDb > FADER_FLOOR_THRESHOLD) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.1f", m_valueDb);
        seed = buf;
    }

    auto& theme = NUIThemeManager::getInstance();
    m_textInput = std::make_shared<NUITextInput>(seed);

    // NUITextInput always draws its text and caret at the theme's "m" size, and
    // sizes the caret to that font's full line height. The 14 px readout band is
    // shorter than that, so the caret overflowed the field. Give the editor the
    // height the font actually needs; it grows down over the top of the track,
    // which is fine for a transient editor.
    const float fontM = theme.getFontSize("m");
    const float editH = std::max(readoutRect().height, std::ceil(fontM * 1.9f) + 4.0f);
    const NUIRect readout = readoutRect();
    m_textInput->setBounds(NUIRect{readout.x, getBounds().y, readout.width, editH});
    // Default 8 px padding on each side leaves almost nothing in a ~66 px strip.
    m_textInput->setPadding(4.0f);
    m_textInput->setInputType(NUITextInput::InputType::Number);
    m_textInput->setJustification(NUITextInput::Justification::Center);
    m_textInput->setTextColor(theme.getColor("textPrimary"));
    m_textInput->setBackgroundColor(theme.getColor("inputBgDefault"));
    m_textInput->setBorderColor(theme.getColor("inputBorderFocus"));
    m_textInput->setBorderWidth(1.0f);
    m_textInput->setBorderRadius(3.0f);
    m_textInput->setFocusedBorderColor(m_accent);

    m_textInput->setOnReturnKey([this]() { commitEdit(); });
    m_textInput->setOnEscapeKey([this]() { cancelEdit(); });
    m_textInput->setOnFocusLost([this]() { commitEdit(); });

    addChild(m_textInput);
    // Printable characters are routed to the globally focused component, so the
    // field must take focus or it will never see any input.
    m_textInput->setFocused(true);
    m_textInput->setCaretPosition(static_cast<int>(seed.length()));
    m_textInput->selectAll();
    repaint();
}

void UIMixerFader::commitEdit()
{
    if (!m_editing) {
        return;
    }

    const std::string entered = m_textInput ? m_textInput->getText() : std::string{};
    m_editing = false;
    if (m_textInput) {
        removeChild(m_textInput);
        // Park, don't free: this can run from the editor's own onFocusLost.
        m_retiredInput = std::move(m_textInput);
        m_textInput.reset();
    }

    // An empty field means "silence"; anything unparseable leaves the value alone.
    if (entered.find_first_not_of(" \t") == std::string::npos) {
        setValueDb(m_minDb);
    } else {
        // Parsed in the C locale, not the process locale. A DAW loads arbitrary
        // third-party plugin binaries, and a plugin that calls
        // setlocale(LC_ALL, "") on a comma-decimal system would otherwise make
        // std::stof read "-6.5" as -6.
        std::istringstream stream(entered);
        stream.imbue(std::locale::classic());
        float parsed = 0.0f;
        stream >> parsed;

        // Reject "nan"/"inf": clampDb happens to swallow those today only
        // because of its comparison order, and that is not a guarantee worth
        // depending on. Garbage input leaves the current value alone.
        if (!stream.fail() && std::isfinite(parsed)) {
            setValueDb(parsed);
        }
    }

    repaint();
}

void UIMixerFader::cancelEdit()
{
    if (!m_editing) {
        return;
    }

    m_editing = false;
    if (m_textInput) {
        removeChild(m_textInput);
        // Park, don't free: this can run from the editor's own onFocusLost.
        m_retiredInput = std::move(m_textInput);
        m_textInput.reset();
    }
    repaint();
}

void UIMixerFader::dismissEditAt(const NUIPoint& position)
{
    if (!m_editing) {
        return;
    }
    if (getBounds().contains(position)) {
        return;
    }
    commitEdit();
}

bool UIMixerFader::onKeyEvent(const NUIKeyEvent& event)
{
    if (m_editing && m_textInput) {
        return m_textInput->onKeyEvent(event);
    }
    return false;
}

UIMixerFader::~UIMixerFader() {
    // Torn down mid-drag: cancel the capture so the bridge never routes to a
    // dangling owner and the cursor is never stranded hidden.
    if (m_platformBridge && m_platformBridge->isCursorCaptureOwner(this)) {
        m_platformBridge->cancelCursorCapture();
    }

    // Torn down mid-edit: the editor's callbacks capture `this`, and it can
    // outlive the fader — removeChild() defers while an event is dispatching,
    // parking a strong reference elsewhere. Nothing fires focus loss during
    // teardown today, so this is not a live crash; dropping the callbacks makes
    // that independent of a future change to component destruction order.
    if (m_textInput) {
        m_textInput->setOnReturnKey(nullptr);
        m_textInput->setOnEscapeKey(nullptr);
        m_textInput->setOnFocusLost(nullptr);
    }
}

bool UIMixerFader::onMouseEvent(const NUIMouseEvent& event)
{
    if (!isVisible() || !isEnabled()) return false;

    auto bounds = getBounds();

    // While the value is being typed, the field owns the pointer.
    if (m_editing && m_textInput) {
        if (m_textInput->onMouseEvent(event)) {
            return true;
        }
        if (event.pressed && !bounds.contains(event.position)) {
            commitEdit();
        }
        return false;
    }

    if (!event.cursorCaptured) {
        setHovered(bounds.contains(event.position));
    }
    if (!bounds.contains(event.position) && !m_dragging) return false;

    if (event.pressed && event.button == NUIMouseButton::Left) {
        // The platform never populates event.doubleClick, so presses are paired
        // up by the shared tracker — without this the reset below is dead code.
        const bool isDoubleClick = m_clickTracker.registerPress() || event.doubleClick;

        // The readout is an entry field, not part of the track: clicking it used
        // to slam the fader to whatever value that Y mapped to.
        if (readoutRect().contains(event.position)) {
            beginEdit();
            return true;
        }

        if (isDoubleClick) {
            setValueDb(m_defaultDb);
            return true;
        }
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
        const float handleW = handleWidth();
        const float handleH = HANDLE_HEIGHT;
        const float handleX = bounds.x + (bounds.width - handleW) * 0.5f;
        const NUIRect handleRect{handleX, handleY, handleW, handleH};

        if (!handleRect.contains(event.position)) {
            const float norm = std::clamp(1.0f - (event.position.y - trackTop) / trackHeight, 0.0f, 1.0f);
            const float clickedDb = m_minDb + norm * (m_maxDb - m_minDb);
            setValueDb(clickedDb);
        }

        m_dragStartDb = m_valueDb;

        // Cursor capture for infinite drag: service hides + confines and will
        // restore at the fader handle on release.
        if (m_platformBridge) {
            m_lastDragY = event.position.y;
            m_platformBridge->beginCursorCapture(
                this, NUICursorRestorePolicy::ThumbPosition,
                static_cast<int>(event.position.x), static_cast<int>(event.position.y));
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
            const float handleW = handleWidth();
            const float handleX = bounds.x + (bounds.width - handleW) * 0.5f;

            // End capture: service warps to the handle, unhides, releases
            // confinement — in that order.
            m_platformBridge->endCursorCapture(
                static_cast<int>(handleX + handleW * 0.5f),
                static_cast<int>(handleY + HANDLE_HEIGHT * 0.5f));
        }

        return true;
    }

    // Dragging (mouse move events set button = None)
    if (m_dragging && event.button == NUIMouseButton::None) {
        // Cursor-warp mode: use frame-to-frame delta
        if (m_platformBridge) {
            // Service-owned delta (recentered; no absolute-coord read).
            float dy = event.delta.y;

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

void UIMixerFader::onMouseEnter()
{
    // Hand/grab affordance: the fader is draggable (mixer hand-tool parity).
    if (m_platformBridge) m_platformBridge->setCursorStyle(NUICursorStyle::Grab);
    NUIComponent::onMouseEnter();
}

void UIMixerFader::onMouseLeave()
{
    if (m_platformBridge && !m_dragging) m_platformBridge->setCursorStyle(NUICursorStyle::Arrow);
    NUIComponent::onMouseLeave();
}

} // namespace AestraUI
