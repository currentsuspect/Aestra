// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "NUIPianoRollWidgets.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "PianoRollWidgetShared.h"
#include <algorithm>
#include <cmath>

namespace AestraUI {

// =============================================================================
// PianoRollKeyLane (split from NUIPianoRollWidgets.cpp)
// =============================================================================
PianoRollKeyLane::PianoRollKeyLane()
    : keyHeight_(24.0f), scrollY_(0.0f), hoveredKey_(-1), previewPitch_(-1), onPreviewNote_(nullptr), m_isPlayingCallback(nullptr)
{
}

void PianoRollKeyLane::onRender(NUIRenderer& renderer) {
    if (!isVisible()) return;
    auto b = getBounds();
    auto& themeManager = NUIThemeManager::getInstance();

    renderer.setClipRect(b);

    const auto laneBg = themeManager.getColor("backgroundSecondary").darkened(0.02f);
    const auto naturalKey = themeManager.getColor("surfaceRaised").withAlpha(0.72f);
    const auto naturalHover = themeManager.getColor("surfaceRaised").lightened(0.08f).withAlpha(0.92f);
    const auto accidentalKey = themeManager.getColor("backgroundPrimary").darkened(0.08f);
    const auto accidentalHover = themeManager.getColor("accentPrimary").darkened(0.35f).withAlpha(0.92f);
    const auto separator = themeManager.getColor("border").withAlpha(0.34f);
    const auto rootAccent = themeManager.getColor("accentPrimary").withAlpha(0.86f);
    const auto naturalText = themeManager.getColor("textPrimary").withAlpha(0.72f);
    // Pressed (SPEC 3 §5.1): the key takes the accent, sinks a little, and its tip glows
    // toward the grid row it plays. Blended by the key's press level, so release eases out.
    const auto accent = themeManager.getColor("accentPrimary");
    const auto naturalPressed = accent.withAlpha(0.78f);
    const auto accidentalPressed = accent.darkened(0.18f).withAlpha(0.96f);
    
    int startPitch = 127 - static_cast<int>((scrollY_) / keyHeight_);
    int endPitch = 127 - static_cast<int>((scrollY_ + b.height) / keyHeight_);
    
    // Expand range for safety margin (2 extra keys on each end)
    startPitch = std::clamp(startPitch + 2, 0, 127);
    endPitch = std::clamp(endPitch - 2, 0, 127);

    renderer.fillRect(b, laneBg);

    for (int p = startPitch; p >= endPitch; --p) {
        float worldY = (127 - p) * keyHeight_;
        float y = b.y + worldY - scrollY_;
        const bool accidental = isBlackKey(p);
        const bool hovered = p == hoveredKey_;
        const float press = pressLevel_[p];
        const float rightPad = 3.0f;
        const float accidentalW = std::round(b.width * 0.68f);
        // A pressed key sinks: its free end (toward the grid) travels in by up to 2 px.
        const float sink = 2.0f * press;
        NUIRect keyRect(b.x + 3.0f,
                        y + 1.0f,
                        (accidental ? accidentalW : b.width - 3.0f - rightPad) - sink,
                        std::max(4.0f, keyHeight_ - 2.0f));

        const auto restFill = hovered ? (accidental ? accidentalHover : naturalHover)
                                      : (accidental ? accidentalKey : naturalKey);
        const auto fill =
            press > 0.0f ? NUIColor::lerp(restFill, accidental ? accidentalPressed : naturalPressed, press) : restFill;
        renderer.fillRoundedRect(keyRect, accidental ? 2.0f : 3.0f, fill);
        renderer.strokeRoundedRect(keyRect, accidental ? 2.0f : 3.0f, 1.0f, separator);
        if (press > 0.0f) {
            // The lit tip: a bright edge where the key meets its grid row.
            renderer.fillRoundedRect(NUIRect(keyRect.right() - 3.0f, keyRect.y + 1.0f, 2.0f, keyRect.height - 2.0f),
                                     1.0f,
                                     accent.lightened(0.25f).withAlpha(0.95f * press));
        }

        if (!accidental) {
            renderer.drawLine(NUIPoint(keyRect.x, keyRect.bottom()),
                              NUIPoint(keyRect.right(), keyRect.bottom()),
                              1.0f,
                              separator.withAlpha(0.52f));
        }

        if (keyHeight_ >= 15.0f && p % 12 == 0) {
            const std::string label = getNoteLabel(p);
            const float fontSize = 10.0f;
            const auto textSize = renderer.measureText(label, fontSize);
            const float textX = keyRect.right() - textSize.width - 8.0f;
            const float textY = keyRect.y + (keyRect.height - textSize.height) * 0.5f;
            renderer.drawText(label,
                              NUIPoint(textX, textY),
                              fontSize,
                              naturalText.lightened(0.12f));
        }

        if (p % 12 == 0) {
            renderer.fillRoundedRect(NUIRect(b.x + 3.0f, y + 4.0f, 2.0f, std::max(2.0f, keyHeight_ - 8.0f)),
                                     1.0f,
                                     rootAccent);
        }
    }

    renderer.drawLine(NUIPoint(b.right() - 0.5f, b.y),
                      NUIPoint(b.right() - 0.5f, b.bottom()),
                      1.0f,
                      separator.withAlpha(0.9f));

    renderer.clearClipRect();
}

bool PianoRollKeyLane::onMouseEvent(const NUIMouseEvent& event) {
    if (!getBounds().contains(event.position)) {
        if (hoveredKey_ != -1) {
            hoveredKey_ = -1;
            if (onHoveredKeyChanged_) onHoveredKeyChanged_(-1);
            repaint();
        }
        // A press that started on a key can be released outside the lane —
        // still end the preview so the next press re-fires cleanly.
        if (event.released && event.button == NUIMouseButton::Left) {
            previewPitch_ = -1;
            releaseAllKeys();
        }
        return false;
    }

    auto b = getBounds();
    float localY = event.position.y - b.y + scrollY_;
    int pitch = 127 - static_cast<int>(localY / keyHeight_);
    pitch = std::clamp(pitch, 0, 127);

    if (hoveredKey_ != pitch) {
        hoveredKey_ = pitch;
        if (onHoveredKeyChanged_) onHoveredKeyChanged_(pitch);
        repaint();
    }

    // The key goes down on a press whether or not it sounds (playback suppresses the audition).
    if (event.pressed && event.button == NUIMouseButton::Left) {
        pressKey(pitch);
    }

    if (m_isPlayingCallback && m_isPlayingCallback()) {
        if (event.released && event.button == NUIMouseButton::Left) releaseAllKeys();
        return NUIComponent::onMouseEvent(event);
    }

    if (event.pressed && event.button == NUIMouseButton::Left) {
        previewPitch_ = pitch;
        if (onPreviewNote_) {
            onPreviewNote_(pitch, 100);
        }
    }
    else if (event.released && event.button == NUIMouseButton::Left && previewPitch_ != -1) {
        // The engine audition voice is one-shot (auto note-off, velocity
        // clamped to >=1) — sending a velocity-0 "note-off" would retrigger a
        // near-silent voice on top of it, so just clear the preview state.
        previewPitch_ = -1;
        releaseAllKeys();
    }

    return NUIComponent::onMouseEvent(event);
}

void PianoRollKeyLane::pressKey(int pitch) {
    if (pitch < 0 || pitch > 127) return;
    keyHeld_[pitch] = true;
    pressLevel_[pitch] = 1.0f; // down at once: the press is immediate, only the release eases
    repaint();
}

void PianoRollKeyLane::releaseKey(int pitch) {
    if (pitch < 0 || pitch > 127 || !keyHeld_[pitch]) return;
    keyHeld_[pitch] = false;
    pressAnimating_ = true;
}

void PianoRollKeyLane::releaseAllKeys() {
    for (int p = 0; p < 128; ++p) {
        if (keyHeld_[p]) {
            keyHeld_[p] = false;
            pressAnimating_ = true;
        }
    }
}

void PianoRollKeyLane::onUpdate(double deltaTime) {
    NUIComponent::onUpdate(deltaTime);
    if (!pressAnimating_) return;
    const float step = static_cast<float>(deltaTime) / kKeyReleaseSeconds;
    bool stillEasing = false;
    for (int p = 0; p < 128; ++p) {
        if (keyHeld_[p] || pressLevel_[p] <= 0.0f) continue;
        pressLevel_[p] = std::max(0.0f, pressLevel_[p] - step);
        stillEasing = stillEasing || pressLevel_[p] > 0.0f;
    }
    pressAnimating_ = stillEasing;
    repaint();
}

void PianoRollKeyLane::setKeyHeight(float height) {
    keyHeight_ = std::max(8.0f, height);
    repaint();
}

void PianoRollKeyLane::setScrollOffsetY(float offset) {
    scrollY_ = offset;
    repaint();
}

void PianoRollKeyLane::setOnPreviewNote(std::function<void(int pitch, int velocity)> cb) {
    onPreviewNote_ = std::move(cb);
}

} // namespace AestraUI
