// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "NUIPianoRollWidgets.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "PianoRollLook.h"
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

    // Design Spec, Piano keys (locked 13 Nov 2026): D, "In the bed", by default. The keyboard takes the theme: naturals are
    // the raised surface, accidentals are bars of the bed with a lit top edge, and the root's keys carry an amber edge.
    // "Name every note" switches to E, hardware rows: one row per note, naturals a step lighter than sharps, a lit cap
    // on each C, and every row named.
    const auto bed = themeManager.getColor("backgroundPrimary");
    const auto raised = themeManager.getColor("surfaceRaised");
    const auto tertiary = themeManager.getColor("surfaceTertiary");
    const auto ink = themeManager.getColor("textPrimary");
    const auto inkSoft = themeManager.getColor("textSecondary");
    const auto inkMuted = themeManager.getColor("textMuted");
    const auto rootEdge = themeManager.getColor("warning");
    const bool keyed = scaleType_ != ScaleType::Chromatic;
    // Pressed (SPEC 3 §5.1): the key sinks a little and its tip glows toward the grid row it plays.
    const auto accent = themeManager.getColor("accentPrimary");

    int startPitch = 127 - static_cast<int>((scrollY_) / keyHeight_);
    int endPitch = 127 - static_cast<int>((scrollY_ + b.height) / keyHeight_);
    startPitch = std::clamp(startPitch + 2, 0, 127); // a safety margin of two keys at each end
    endPitch = std::clamp(endPitch - 2, 0, 127);

    renderer.fillRect(b, nameEveryNote_ ? bed : raised); // D: the naturals continue behind the accidentals

    for (int p = startPitch; p >= endPitch; --p) {
        const float y = b.y + (127 - p) * keyHeight_ - scrollY_;
        const bool accidental = isBlackKey(p);
        const bool hovered = p == hoveredKey_;
        const bool isRoot = keyed && ((p % 12) + 12) % 12 == ((rootKey_ % 12) + 12) % 12;
        const float press = pressLevel_[p];
        const float accidentalW = std::round(b.width * (44.0f / 68.0f));
        const float sink = 2.0f * press; // a pressed key's free end travels in by up to 2 px
        const float fontSize = 10.0f;
        NUIRect keyRect(b.x, y, (nameEveryNote_ || !accidental ? b.width : accidentalW) - sink, keyHeight_);

        NUIColor rest = nameEveryNote_ ? (accidental ? bed : tertiary) : (accidental ? bed : raised);
        if (hovered) rest = rest.lightened(0.06f);
        const NUIColor sunk = accidental ? themeManager.getColor("meterBackground") : tertiary;
        renderer.fillRect(keyRect, press > 0.0f ? NUIColor::lerp(rest, sunk, press) : rest);
        if (nameEveryNote_ && p % 12 == 0 && press <= 0.0f) { // the lit cap on each C
            const NUIRect cap(keyRect.x + 2.0f, keyRect.y + 2.0f, keyRect.width - 4.0f, keyRect.height - 4.0f);
            renderer.fillRoundedRect(cap, 3.0f, raised);
            renderer.fillRect(NUIRect(cap.x + 1.0f, cap.y, cap.width - 2.0f, 1.0f), NUIColor::white().withAlpha(0.12f));
        }
        if (!nameEveryNote_ && accidental && press <= 0.0f) // the accidental's lit top edge
            renderer.fillRect(NUIRect(keyRect.x + 1.0f, keyRect.y + 1.0f, keyRect.width - 3.0f, 1.0f), ink.withAlpha(0.10f));
        if (const float play = playLevel_[p]; play > 0.0f) renderer.fillRect(keyRect, accent.withAlpha(0.34f * play));
        if (press > 0.0f)
            renderer.fillRect(NUIRect(keyRect.right() - 2.0f, keyRect.y, 2.0f, keyRect.height), accent.lightened(0.25f).withAlpha(0.95f * press));
        if (!nameEveryNote_ && isRoot) renderer.fillRect(NUIRect(keyRect.right() - 3.0f, keyRect.y, 3.0f, keyRect.height), rootEdge);
        // The edge between keys: under every row in E; in D only where two naturals meet (E-F, B-C).
        if (nameEveryNote_ || (!accidental && p > 0 && !isBlackKey(p - 1)))
            renderer.drawLine(NUIPoint(b.x, keyRect.bottom() - 0.5f), NUIPoint(b.right(), keyRect.bottom() - 0.5f), 1.0f, bed);

        if (keyHeight_ < 12.0f) continue;
        if (const std::string degree = PianoRollLook::scaleDegreeLabel(p, rootKey_, scaleType_); !degree.empty()) {
            const auto textSize = renderer.measureText(degree, fontSize);
            renderer.drawText(degree, NUIPoint(keyRect.x + 5.0f, keyRect.y + (keyRect.height - textSize.height) * 0.5f),
                              fontSize, degree == "1" ? rootEdge : (accidental ? inkSoft : inkMuted));
        }
        if (p % 12 == 0 || nameEveryNote_) {
            const std::string label = getNoteLabel(p);
            const bool c = p % 12 == 0;
            const auto textSize = renderer.measureText(label, fontSize);
            renderer.drawText(label, NUIPoint(keyRect.right() - 5.0f - textSize.width - (!nameEveryNote_ && isRoot ? 3.0f : 0.0f),
                                              keyRect.y + (keyRect.height - textSize.height) * 0.5f),
                              fontSize, c ? (nameEveryNote_ ? ink : inkSoft) : inkMuted);
        }
    }

    renderer.drawLine(NUIPoint(b.right() - 0.5f, b.y),
                      NUIPoint(b.right() - 0.5f, b.bottom()),
                      1.0f,
                      bed);

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

void PianoRollKeyLane::setPlayingPitches(const std::array<bool, 128>& playing) {
    bool changed = false;
    for (int p = 0; p < 128; ++p) {
        if (playing[p] == playing_[p]) continue;
        playing_[p] = playing[p];
        changed = true;
        if (playing[p]) {
            playLevel_[p] = 1.0f; // on at once; only the release eases
        } else {
            playAnimating_ = true;
        }
    }
    if (changed) repaint();
}

void PianoRollKeyLane::onUpdate(double deltaTime) {
    NUIComponent::onUpdate(deltaTime);
    if (!pressAnimating_ && !playAnimating_) return;
    if (pressAnimating_) {
        const float step = static_cast<float>(deltaTime) / kKeyReleaseSeconds;
        bool stillEasing = false;
        for (int p = 0; p < 128; ++p) {
            if (keyHeld_[p] || pressLevel_[p] <= 0.0f) continue;
            pressLevel_[p] = std::max(0.0f, pressLevel_[p] - step);
            stillEasing = stillEasing || pressLevel_[p] > 0.0f;
        }
        pressAnimating_ = stillEasing;
    }
    if (playAnimating_) {
        const float step = static_cast<float>(deltaTime) / kPlayReleaseSeconds;
        bool stillEasing = false;
        for (int p = 0; p < 128; ++p) {
            if (playing_[p] || playLevel_[p] <= 0.0f) continue;
            playLevel_[p] = std::max(0.0f, playLevel_[p] - step);
            stillEasing = stillEasing || playLevel_[p] > 0.0f;
        }
        playAnimating_ = stillEasing;
    }
    repaint();
}

void PianoRollKeyLane::setKeyHeight(float height) {
    const float next = std::max(8.0f, height);
    if (keyHeight_ == next) return; // called every frame from the view's sync
    keyHeight_ = next;
    repaint();
}

void PianoRollKeyLane::setScrollOffsetY(float offset) {
    if (scrollY_ == offset) return; // called every frame from the view's sync
    scrollY_ = offset;
    repaint();
}

void PianoRollKeyLane::setOnPreviewNote(std::function<void(int pitch, int velocity)> cb) {
    onPreviewNote_ = std::move(cb);
}

} // namespace AestraUI
