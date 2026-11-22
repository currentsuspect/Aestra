// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerFader.h"
#include "../Graphics/NUIDepth.h"
#include "CapLook.h"
#include "MixerLane.h"
#include "MixerScale.h"
#include "SlotGroove.h"

#include "NUIThemeSystem.h"
#include "NUIRenderer.h"
#include "NUITextInput.h"
#include "../Platform/NUIPlatformBridge.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <iterator>
#include <exception>
#include <locale>
#include <sstream>
#include <string>

namespace AestraUI {

namespace {
    // Design Spec, Mixer (locked, board p-mixer2): one 24 by 34 dished cap for channel and master. The compact
    // overlay look (Narrow strips, which have no room for the lane) keeps the old 20 by 26 cap until a Trial says otherwise.
    constexpr float kCapH = CapLook::kH;
    constexpr float kOverlayHandleW = 20.0f;
    constexpr float kOverlayHandleH = 26.0f;
    constexpr float TRACK_WIDTH = SlotGroove::kSlotW; // one slot for master and channel (board p-mixer2, variant F)
    constexpr float TOP_PAD = UIMixerFader::kReadoutBand; // room for the master's dB readout at the top
    constexpr float SNAP_DB = 0.5f;
    constexpr float DRAG_SLOP = 1.5f;

    // The console scale's marks (MixerScale). Unity (0 dB) is the only one that gets emphasis.
    constexpr const auto& SCALE_TICKS = MixerScale::kTicks;

    // The slot of the locked fader: meterBackground in 3px plain / 1px lighter bands, an inset shadow over it.
    // fillRect per line, no blur: the shadow is the renderer's SDF inner shadow, as Depth::bed uses.
    void drawGrooveSlot(NUIRenderer& renderer, const NUIRect& slot)
    {
        const auto& theme = NUIThemeManager::getInstance().getCurrentTheme();
        const NUIColor base = theme.meterBackground;
        const NUIColor line{SlotGroove::liftChannel(base.r), SlotGroove::liftChannel(base.g),
                            SlotGroove::liftChannel(base.b), base.a};
        renderer.fillRoundedRect(slot, SlotGroove::kSlotRadius, base);
        const int lines = SlotGroove::lineCount(slot.height);
        for (int i = 0; i < lines; ++i)
            renderer.fillRect({slot.x, slot.y + SlotGroove::lineTop(i), slot.width, 1.0f}, line);
        renderer.drawInnerShadow(slot, SlotGroove::kSlotRadius, SlotGroove::kShadowOffset, SlotGroove::kShadowBlur,
                                 theme.neuLo);
    }

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
    // Scale labels and the idle readout are small TEXT on a raised surface: the textSecondary tier.
    // They used to reuse the tick line's 0.34 alpha (1.8:1 in both themes). textMuted is calibrated
    // to the primary ground and measured 3.8:1 on this strip, so the secondary tier it is.
    m_textMuted = theme.getColor("textSecondary");
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
    return std::min(CapLook::kW, getBounds().width);
}

NUIRect UIMixerFader::readoutRect() const
{
    const auto bounds = getBounds();
    return NUIRect{bounds.x, bounds.y + 2.0f, bounds.width, TOP_PAD - 4.0f};
}

float UIMixerFader::normFromDb(float db) const
{
    // Every fader, the master's included, travels the one console scale the meters use (Design Spec, Mixer).
    return MixerScale::norm(db);
}

float UIMixerFader::dbFromNorm(float norm) const
{
    norm = std::clamp(norm, 0.0f, 1.0f);
    if (norm <= 0.004f)
        return m_minDb;
    const float db = MixerScale::db(norm);
    return std::isfinite(db) ? db : m_minDb; // the scale's bottom reads minus infinity: the fader's own floor

}

float UIMixerFader::overlayHandleHeight() { return kOverlayHandleH; }

float UIMixerFader::capHeight() const { return m_overlay ? kOverlayHandleH : kCapH; }
// The cap hangs half its height over each end of the track. The master also keeps its gain readout above the travel.
float UIMixerFader::topPad() const { return capHeight() * 0.5f + (!m_overlay && m_showScaleLabels ? TOP_PAD : 0.0f); }
float UIMixerFader::bottomPad() const { return capHeight() * 0.5f; }

NUIRect UIMixerFader::handleRectAt(float handleY) const
{
    const auto bounds = getBounds();
    if (m_overlay) {
        const float w = std::min(kOverlayHandleW, bounds.width);
        return NUIRect{bounds.x + (bounds.width - w) * 0.5f, handleY, w, kOverlayHandleH};
    }
    // The cap rides the slot, which sits at the lane's track centre, not the widget's.
    const float handleW = handleWidth();
    return NUIRect{bounds.x + MixerLane::slotCentre() - handleW * 0.5f, handleY, handleW, kCapH};
}

float UIMixerFader::dbToY(float db, float trackTop, float trackHeight) const
{
    return trackTop + (1.0f - normFromDb(db)) * trackHeight;
}

void UIMixerFader::renderScale(NUIRenderer& renderer, float trackX, float trackWidth,
                               float trackTop, float trackHeight, bool showLabels)
{
    // The printed console scale (Design Spec, Mixer, board p-mixer2): numbers in the 16px column on the left, a 1px
    // gridMajor tick 8px long on each side of the slot, 1px in from the track's edge. 0 dB is textSecondary and bold
    // (the renderer has no bold weight: it is drawn twice, half a pixel apart). Both sides print, so the cap never
    // hides the scale.
    (void)trackWidth;
    (void)showLabels;
    auto& theme = NUIThemeManager::getInstance().getCurrentTheme();
    const float faderLeft = getBounds().x;
    constexpr float kLabelSize = 9.0f;
    for (float db : SCALE_TICKS) {
        if (db > m_maxDb || db < m_minDb) continue;
        const bool unity = (std::abs(db) < 0.01f);
        const float y = std::round(dbToY(db, trackTop, trackHeight));
        const NUIColor tick = unity ? theme.textSecondary : theme.gridMajor;
        renderer.fillRect(NUIRect{faderLeft + MixerLane::leftTickX(), y, MixerLane::kTickLen, 1.0f}, tick);
        renderer.fillRect(NUIRect{faderLeft + MixerLane::rightTickX(), y, MixerLane::kTickLen, 1.0f}, tick);

        char buf[8];
        if (unity) std::snprintf(buf, sizeof(buf), "0");
        else std::snprintf(buf, sizeof(buf), "%s%d", db > 0.0f ? "+" : "-", static_cast<int>(std::fabs(db)));
        const NUIColor ink = unity ? theme.textSecondary : theme.textMuted;
        const NUIPoint at{faderLeft, y - kLabelSize * 0.5f - 1.0f};
        renderer.drawText(buf, at, kLabelSize, ink);
        if (unity) renderer.drawText(buf, {at.x + 0.5f, at.y}, kLabelSize, ink);
    }
    (void)trackX;
}

void UIMixerFader::onRender(NUIRenderer& renderer)
{
    // Safe point to free a committed/cancelled editor: we are no longer inside
    // any NUITextInput callback.
    m_retiredInput.reset();

    if (m_overlay) {
        renderOverlay(renderer);
        return;
    }

    auto bounds = getBounds();

    // Track area
    const float trackTop = bounds.y + topPad();
    const float trackBottom = bounds.y + bounds.height - bottomPad();
    const float trackHeight = std::max(1.0f, trackBottom - trackTop);

    const float trackWidth = TRACK_WIDTH;
    const float trackX = bounds.x + MixerLane::slotCentre() - trackWidth * 0.5f;
    NUIRect trackRect{trackX, trackTop, trackWidth, trackHeight};

    // 1. Scale first, so the rail and cap sit on top of it.
    const bool showScaleLabels = m_showScaleLabels || isHovered() || m_dragging;
    renderScale(renderer, trackX, trackWidth, trackTop, trackHeight, showScaleLabels);

    // 2. The slot, cut into the strip (Design Spec: a sunk bed).
    drawGrooveSlot(renderer, trackRect);

    // 3. Filled Portion — neutral. This is control state, not signal.
    const float filledH = normFromDb(m_valueDb) * trackHeight;

    // 4. Fader Handle (distinct grab block)
    const float handleY = std::clamp(trackBottom - filledH - kCapH * 0.5f,
                                     trackTop - kCapH * 0.5f,
                                     trackBottom - kCapH * 0.5f);

    const float handleW = handleWidth();
    const float handleX = bounds.x + (bounds.width - handleW) * 0.5f;
    const float handleH = kCapH;

    NUIRect handleRect{handleX, handleY, handleW, handleH};
    // The cap is an object (Design Spec, Mixer): one dished 24 by 34 cap, the master's index line in primary.
    Depth::consoleCap(renderer, handleRect, m_showScaleLabels, m_dragging);

    // 5. dB readout at top. It remains available for exact entry, but recedes
    //    on ordinary channel strips so the control shape and the adjacent meter
    //    retain visual priority. The master strip keeps a stronger readout.
    if (!m_showScaleLabels) {
        // A channel's gain reads in its footer; only the master keeps the readout above the travel.
    } else if (m_editing && m_textInput) {
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
                                  valueActive ? m_text : m_textMuted);
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

void UIMixerFader::renderOverlay(NUIRenderer& renderer)
{
    const auto bounds = getBounds();
    const float trackTop = bounds.y + topPad();
    const float trackBottom = bounds.y + bounds.height - bottomPad();
    const float trackHeight = std::max(1.0f, trackBottom - trackTop);
    const float y = trackBottom - normFromDb(m_valueDb) * trackHeight;

    // The slot the cap rides in, centred in the fader's column (the handle's own centre). Narrow strips only: the
    // other densities draw the full lane in onRender.
    {
        const NUIRect handleCol = handleRectAt(trackTop);
        const float slotX = std::round(handleCol.x + (handleCol.width - SlotGroove::kSlotW) * 0.5f);
        drawGrooveSlot(renderer, NUIRect{slotX, trackTop, SlotGroove::kSlotW, trackHeight});
    }

    // The cap: one line across the meters, so gain reads against the level behind it.
    const bool active = isHovered() || m_dragging;
    const NUIColor cap = active ? m_text : m_text.withAlpha(0.92f);
    const NUIRect line{bounds.x - m_lineExtentLeft, y - 1.0f, m_lineExtentLeft + bounds.width * 0.5f, 3.0f};
    renderer.fillRect(line, cap);

    // The handle on the meters' right edge is the cap you grab: lit, ridged, lifting while held.
    const NUIRect handle = handleRectAt(y - kOverlayHandleH * 0.5f);
    Depth::faderCap(renderer, handle, false, m_dragging);

    if (m_dragging) {
        const float tipW = 38.0f;
        const float tipH = 18.0f;
        const NUIRect tip{handle.x - tipW - 6.0f, y - tipH * 0.5f, tipW, tipH};
        renderer.fillRoundedRect(tip, 3.0f, m_tooltipBg);
        renderer.strokeRoundedRect(tip, 3.0f, 1.0f, m_accent.withAlpha(0.5f));
        renderer.drawTextCentered(m_cachedText, tip, 10.5f, m_text);
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
        if (m_showScaleLabels && readoutRect().contains(event.position)) {
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
        const float trackTop = bounds.y + topPad();
        const float trackBottom = bounds.y + bounds.height - bottomPad();
        const float trackHeight = std::max(1.0f, trackBottom - trackTop);
        const float filledH = normFromDb(m_valueDb) * trackHeight;
        const float handleY = std::clamp(trackBottom - filledH - capHeight() * 0.5f,
                                         trackTop - capHeight() * 0.5f,
                                         trackBottom - capHeight() * 0.5f);
        const NUIRect handleRect = handleRectAt(handleY);

        if (!handleRect.contains(event.position)) {
            const float norm = std::clamp(1.0f - (event.position.y - trackTop) / trackHeight, 0.0f, 1.0f);
            setValueDb(dbFromNorm(norm));
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
            const float trackTop = bounds.y + topPad();
            const float trackBottom = bounds.y + bounds.height - bottomPad();
            const float trackHeight = std::max(1.0f, trackBottom - trackTop);
            const float filledH = normFromDb(m_valueDb) * trackHeight;
            const float hh = capHeight();
            const float handleY = std::clamp(trackBottom - filledH - hh * 0.5f, trackTop - hh * 0.5f, trackBottom - hh * 0.5f);
            const NUIRect handleR = handleRectAt(handleY);
            const float handleW = handleR.width;
            const float handleX = handleR.x;

            // End capture: service warps to the handle, unhides, releases
            // confinement — in that order.
            m_platformBridge->endCursorCapture(
                static_cast<int>(handleX + handleW * 0.5f),
                static_cast<int>(handleY + hh * 0.5f));
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
            const float trackTop = bounds.y + topPad();
            const float trackBottom = bounds.y + bounds.height - bottomPad();
            const float trackHeight = std::max(1.0f, trackBottom - trackTop);

            // Reduced sensitivity for cursor-warp mode
            float sensitivity = (event.modifiers & NUIModifiers::Shift) ? 0.1f : 0.5f;
            float nextDb = dbFromNorm(normFromDb(m_valueDb) + deltaPx / trackHeight * sensitivity);

            if ((event.modifiers & NUIModifiers::Ctrl) || (event.modifiers & NUIModifiers::Alt)) {
                nextDb = std::round(nextDb / SNAP_DB) * SNAP_DB;
            }

            setValueDb(nextDb);
            return true;
        }

        // Non-warp mode: use drag start position with latching
        const float deltaPx = m_dragStartPos.y - event.position.y;
        const float trackTop = bounds.y + topPad();
        const float trackBottom = bounds.y + bounds.height - bottomPad();
        const float trackHeight = std::max(1.0f, trackBottom - trackTop);
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

        float nextDb = dbFromNorm(normFromDb(m_dragStartDb) + deltaPx / trackHeight * sensitivity);

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
    NUIComponent::onMouseEnter();
    // Hand/grab affordance: the fader is draggable (mixer hand-tool parity).
    // Never override a Hidden cursor mid-capture (captured fader drags keep
    // the pointer hidden via the cursor service).
    if (m_platformBridge && m_platformBridge->getCursorStyle() != NUICursorStyle::Hidden) {
        m_platformBridge->setCursorStyle(NUICursorStyle::Grab);
    }
}

void UIMixerFader::onMouseLeave()
{
    if (m_platformBridge && !m_dragging) m_platformBridge->setCursorStyle(NUICursorStyle::Arrow);
    NUIComponent::onMouseLeave();
}

} // namespace AestraUI
