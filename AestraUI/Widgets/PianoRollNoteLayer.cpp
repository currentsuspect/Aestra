// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "NUIPianoRollWidgets.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "PianoRollWidgetShared.h"
#include <algorithm>
#include <cmath>
#include "../Helpers/PianoRollInteraction.h"
#include "../Platform/NUIPlatformBridge.h"
#include <chrono>
#include <random>

namespace AestraUI {

namespace {
constexpr float SELECTION_STRETCH_HANDLE_WIDTH = 9.0f;
constexpr float SELECTION_STRETCH_HANDLE_HEIGHT = 16.0f;
} // namespace

// =============================================================================
// PianoRollNoteLayer (split from NUIPianoRollWidgets.cpp)
// =============================================================================
PianoRollNoteLayer::PianoRollNoteLayer()
    : pixelsPerBeat_(80.0f), keyHeight_(24.0f), scrollX_(0.0f), scrollY_(0.0f)
{
}

double PianoRollNoteLayer::snapToGrid(double beat) {
    if (fineDrag_) return beat; // Alt held mid-drag: free positioning
    if (snap_ == SnapGrid::None) return beat;
    double grid = MusicTheory::getSnapDuration(snap_);
    if (grid <= 0.00001) return beat;
    return std::round(beat / grid) * grid;
}

int PianoRollNoteLayer::snapPitchToScale(int pitch) {
    if (!snapToScale_ || scaleType_ == ScaleType::Chromatic) return pitch;
    if (MusicTheory::isNoteInScale(pitch, rootKey_, scaleType_)) return pitch;
    int bestPitch = pitch;
    int bestDist = 128;
    for (int candidate = pitch - 6; candidate <= pitch + 6; ++candidate) {
        if (candidate < 0 || candidate > 127) continue;
        if (MusicTheory::isNoteInScale(candidate, rootKey_, scaleType_)) {
            int dist = std::abs(candidate - pitch);
            if (dist < bestDist) {
                bestDist = dist;
                bestPitch = candidate;
            }
        }
    }
    return bestPitch;
}

void PianoRollNoteLayer::auditionPitch(int pitch) {
    // Never talk over the transport — playback owns the audio focus. Clear the
    // sounding pitch while suppressed so the very next idle placement re-fires
    // (otherwise the same-pitch guard below would swallow it after playback stops).
    if (isPlayingCallback_ && isPlayingCallback_()) {
        auditionPitch_ = -1;
        return;
    }
    pitch = std::clamp(pitch, 0, 127);
    // The audition path is a one-shot voice that auto-releases (~125 ms), so a
    // fresh press must always fire. auditionStop() resets the guard on release;
    // the guard's only job is to avoid machine-gunning one pitch while a drag
    // jitters within the same row.
    if (pitch == auditionPitch_) return;
    if (onPreviewNote_) {
        onPreviewNote_(pitch, static_cast<int>(std::lround(lastNoteVelocity_ * 127.0f)));
    }
    auditionPitch_ = pitch;
}

void PianoRollNoteLayer::auditionStop() {
    // The one-shot voice releases itself; just clear the guard so the next
    // placement or pitch-drag can audition again, even on the same pitch.
    auditionPitch_ = -1;
}

std::vector<int> PianoRollNoteLayer::buildTriad(int rootPitch) const {
    rootPitch = std::clamp(rootPitch, 0, 127);
    // No scale context → a plain major triad is the sensible default.
    if (scaleType_ == ScaleType::Chromatic) {
        std::vector<int> chord = {rootPitch};
        if (rootPitch + 4 <= 127) chord.push_back(rootPitch + 4);
        if (rootPitch + 7 <= 127) chord.push_back(rootPitch + 7);
        return chord;
    }

    // Diatonic triad = root + the 2nd and 4th scale tones above it (the third and
    // fifth), so the chord quality follows the degree the root sits on.
    std::vector<int> chord = {rootPitch};
    const int wantDegrees[] = {2, 4};
    int found = 0;
    int scaleStepsUp = 0;
    for (int p = rootPitch + 1; p <= 127 && found < 2; ++p) {
        if (!MusicTheory::isNoteInScale(p, rootKey_, scaleType_)) continue;
        ++scaleStepsUp;
        if (scaleStepsUp == wantDegrees[found]) {
            chord.push_back(p);
            ++found;
        }
    }
    return chord;
}

bool PianoRollNoteLayer::paintBrushAt(float localX, float localY) {
    const double snappedBeat = snapToGrid(std::max(0.0, static_cast<double>(localX) / pixelsPerBeat_));
    const int rootPitch = snapPitchToScale(std::clamp(127 - static_cast<int>(localY / keyHeight_), 0, 127));
    const std::vector<int> strokePitches = chordMode_ ? buildTriad(rootPitch) : std::vector<int>{rootPitch};
    bool changed = false;

    for (const int pitch : strokePitches) {
        const bool occupied = std::any_of(notes_.begin(), notes_.end(), [pitch, snappedBeat](const MidiNote& note) {
            return !note.isDeleted && note.pitch == pitch && std::abs(note.startBeat - snappedBeat) < 0.001;
        });
        if (occupied) continue;

        MidiNote note;
        note.pitch = pitch;
        note.startBeat = snappedBeat;
        note.durationBeats = lastNoteDuration_;
        note.velocity = lastNoteVelocity_;
        note.unitId = defaultUnitId_;
        note.selected = true;
        note.animationScale = 1.0f;
        notes_.push_back(note);
        auditionPitch(pitch);
        changed = true;
    }
    return changed;
}

void PianoRollNoteLayer::connectSelectedNotes() {
    std::vector<int> selected;
    for (size_t i = 0; i < notes_.size(); ++i) {
        if (notes_[i].selected && !notes_[i].isDeleted) selected.push_back(static_cast<int>(i));
    }
    if (selected.empty()) return;

    // Grid used for the "nothing follows" fallback.
    double snapDur = MusicTheory::getSnapDuration(snap_);
    if (snap_ == SnapGrid::None || snapDur <= 0.0001) snapDur = 1.0; // fall back to one beat

    // Candidate starts to connect against (own start is excluded by the helper's
    // strict "> start" test, so passing all of them is fine).
    std::vector<double> starts;
    starts.reserve(notes_.size());
    for (const auto& n : notes_) {
        if (!n.isDeleted) starts.push_back(n.startBeat);
    }

    auto oldNotes = notes_;
    bool changed = false;
    for (int idx : selected) {
        const double start = notes_[idx].startBeat;
        const double end = start + notes_[idx].durationBeats;
        const double newEnd = computeConnectedNoteEnd(start, end, starts, snapDur);
        if (newEnd > end + 0.0001) {
            notes_[idx].durationBeats = newEnd - start;
            changed = true;
        }
    }

    if (changed) {
        pushUndo("Connect", oldNotes, notes_);
        commitNotes();
        repaint();
    }
}

void PianoRollNoteLayer::quantizeSelectedNotes() {
    double snapDur = MusicTheory::getSnapDuration(snap_);
    if (snap_ == SnapGrid::None || snapDur <= 0.0001) snapDur = 0.25; // sensible 1/16 default

    auto oldNotes = notes_;
    bool changed = false;
    for (auto& n : notes_) {
        if (!n.selected || n.isDeleted) continue;
        const double q = quantizeBeatToGrid(n.startBeat, snapDur);
        if (std::abs(q - n.startBeat) > 0.0001) {
            n.startBeat = q;
            changed = true;
        }
    }
    if (changed) {
        pushUndo("Quantize", oldNotes, notes_);
        commitNotes();
        repaint();
    }
}

void PianoRollNoteLayer::glueSelectedNotes() {
    // Collect selected note indices grouped by pitch.
    std::vector<int> selected;
    for (size_t i = 0; i < notes_.size(); ++i) {
        if (notes_[i].selected && !notes_[i].isDeleted) selected.push_back(static_cast<int>(i));
    }
    if (selected.size() < 2) return;

    auto oldNotes = notes_;
    std::vector<MidiNote> merged;      // rebuilt selected notes
    std::vector<bool> consumed(notes_.size(), false);

    // For each pitch, sweep left→right and coalesce runs that overlap or touch.
    std::sort(selected.begin(), selected.end(), [&](int a, int b) {
        if (notes_[a].pitch != notes_[b].pitch) return notes_[a].pitch < notes_[b].pitch;
        return notes_[a].startBeat < notes_[b].startBeat;
    });

    bool changed = false;
    size_t k = 0;
    while (k < selected.size()) {
        MidiNote run = notes_[selected[k]];
        double runEnd = run.startBeat + run.durationBeats;
        size_t j = k + 1;
        while (j < selected.size() && notes_[selected[j]].pitch == run.pitch &&
               notes_[selected[j]].startBeat <= runEnd + 0.0001) {
            runEnd = std::max(runEnd, notes_[selected[j]].startBeat + notes_[selected[j]].durationBeats);
            changed = true; // at least two notes folded together
            ++j;
        }
        run.durationBeats = runEnd - run.startBeat;
        run.selected = true;
        merged.push_back(run);
        for (size_t m = k; m < j; ++m) consumed[selected[m]] = true;
        k = j;
    }

    if (!changed) return;

    // Keep everything that wasn't glued, then append the merged notes.
    std::vector<MidiNote> rebuilt;
    rebuilt.reserve(notes_.size());
    for (size_t i = 0; i < notes_.size(); ++i) {
        if (!consumed[i]) rebuilt.push_back(notes_[i]);
    }
    for (auto& m : merged) rebuilt.push_back(m);
    notes_ = std::move(rebuilt);

    pushUndo("Glue", oldNotes, notes_);
    commitNotes();
    repaint();
}

void PianoRollNoteLayer::strumSelectedNotes(double spreadBeats) {
    std::vector<int> selected;
    for (size_t i = 0; i < notes_.size(); ++i) {
        if (notes_[i].selected && !notes_[i].isDeleted) selected.push_back(static_cast<int>(i));
    }
    if (selected.size() < 2) return;

    // Anchor at the earliest selected start so the strum begins where the chord
    // sits, then cascade low pitch → high pitch.
    double baseStart = std::numeric_limits<double>::max();
    for (int i : selected) baseStart = std::min(baseStart, notes_[i].startBeat);
    std::sort(selected.begin(), selected.end(),
              [&](int a, int b) { return notes_[a].pitch < notes_[b].pitch; });

    auto oldNotes = notes_;
    for (size_t k = 0; k < selected.size(); ++k) {
        notes_[selected[k]].startBeat = std::max(0.0, baseStart + static_cast<double>(k) * spreadBeats);
    }
    pushUndo("Strum", oldNotes, notes_);
    commitNotes();
    repaint();
}

void PianoRollNoteLayer::humanizeSelectedVelocities() {
    // A gentle ±10 MIDI steps of jitter — enough to break the machine-gun
    // feel of identical velocities without mangling drawn dynamics.
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> jitter(-0.08f, 0.08f);

    auto oldNotes = notes_;
    bool changed = false;
    for (auto& n : notes_) {
        if (n.selected && !n.isDeleted) {
            n.velocity = std::clamp(n.velocity + jitter(rng), 0.05f, 1.0f);
            changed = true;
        }
    }
    if (!changed) return;
    pushUndo("Humanize", oldNotes, notes_);
    commitNotes();
    repaint();
}

// --- Note Properties popup -------------------------------------------------
// Fixed layout shared by the renderer and the hit tests below.
namespace {
constexpr float kPropW = 208.0f;
constexpr float kPropPad = 10.0f;
constexpr float kPropTitleH = 24.0f;
constexpr float kPropRowH = 22.0f;
constexpr int kPropRowCount = 5; // Pitch, Velocity, Pan, Start, Length
constexpr float kPropFooterH = 28.0f;
constexpr float kPropH =
    kPropPad + kPropTitleH + kPropRowCount * kPropRowH + 8.0f + kPropFooterH + kPropPad;
} // namespace

void PianoRollNoteLayer::openNoteProperties(int noteIndex) {
    if (noteIndex < 0 || noteIndex >= static_cast<int>(notes_.size()) ||
        notes_[noteIndex].isDeleted) {
        return;
    }
    propNoteIndex_ = noteIndex;
    propUndoSnapshot_ = notes_;
    propOriginalNote_ = notes_[noteIndex];
    propDragField_ = -1;

    // Sit beside the note, flipped/clamped to stay inside the layer.
    const auto b = getBounds();
    const auto& n = notes_[noteIndex];
    const float nx = b.x + static_cast<float>(n.startBeat * pixelsPerBeat_) - scrollX_;
    const float ny = b.y + (127 - n.pitch) * keyHeight_ - scrollY_;
    float px = nx + 26.0f;
    float py = ny - kPropH - 8.0f;
    if (py < b.y + 4.0f) py = ny + keyHeight_ + 8.0f;
    px = std::clamp(px, b.x + 4.0f, std::max(b.x + 4.0f, b.right() - kPropW - 4.0f));
    py = std::clamp(py, b.y + 4.0f, std::max(b.y + 4.0f, b.bottom() - kPropH - 4.0f));
    propPanelRect_ = NUIRect(px, py, kPropW, kPropH);
    repaint();
}

void PianoRollNoteLayer::closeNoteProperties(bool accept) {
    if (propNoteIndex_ < 0) return;
    if (!accept) {
        notes_ = propUndoSnapshot_;
    } else if (propNoteIndex_ < static_cast<int>(notes_.size())) {
        const auto& n = notes_[propNoteIndex_];
        const auto& o = propOriginalNote_;
        const bool changed = n.pitch != o.pitch ||
                             std::abs(n.startBeat - o.startBeat) > 1e-9 ||
                             std::abs(n.durationBeats - o.durationBeats) > 1e-9 ||
                             std::abs(n.velocity - o.velocity) > 1e-6f ||
                             std::abs(n.pan - o.pan) > 1e-6f;
        if (changed) {
            pushUndo("Note Properties", propUndoSnapshot_, notes_);
        }
    }
    propNoteIndex_ = -1;
    propDragField_ = -1;
    propUndoSnapshot_.clear();
    commitNotes();
    repaint();
}

void PianoRollNoteLayer::applyNotePropertyDelta(int field, float dy, bool coarseStep) {
    if (propNoteIndex_ < 0 || propNoteIndex_ >= static_cast<int>(notes_.size())) return;
    auto& n = notes_[propNoteIndex_];
    const MidiNote& s = propDragStartNote_;

    double timeStep = MusicTheory::getSnapDuration(snap_);
    if (timeStep <= 0.0) timeStep = 0.25; // SnapGrid::None still steps musically
    if (!coarseStep) timeStep = 0.01;     // Alt: fine time adjustment

    switch (field) {
        case 0: { // Pitch — one semitone per few pixels, auditioned as it moves
            const int np = std::clamp(s.pitch + static_cast<int>(std::lround(dy / 6.0f)), 0, 127);
            if (np != n.pitch) {
                n.pitch = np;
                auditionPitch(np);
            }
            break;
        }
        case 1: // Velocity
            n.velocity = std::clamp(s.velocity + dy / 160.0f, 0.0f, 1.0f);
            break;
        case 2: // Pan
            n.pan = std::clamp(s.pan + dy / 90.0f, -1.0f, 1.0f);
            break;
        case 3: { // Start time
            const double steps = std::trunc(dy / 8.0f);
            n.startBeat = std::max(0.0, s.startBeat + steps * timeStep);
            break;
        }
        case 4: { // Length
            const double steps = std::trunc(dy / 8.0f);
            n.durationBeats = std::max(0.125, s.durationBeats + steps * timeStep);
            break;
        }
        default:
            break;
    }
    repaint();
}

bool PianoRollNoteLayer::handleNotePropertiesMouse(const NUIMouseEvent& event) {
    if (propNoteIndex_ < 0) return false;
    if (propNoteIndex_ >= static_cast<int>(notes_.size())) {
        propNoteIndex_ = -1;
        return false;
    }

    const NUIRect& r = propPanelRect_;
    const float rowsTop = r.y + kPropPad + kPropTitleH;
    const auto fieldAt = [&](const NUIPoint& p) -> int {
        if (p.x < r.x + 4.0f || p.x > r.right() - 4.0f) return -1;
        const int row = static_cast<int>(std::floor((p.y - rowsTop) / kPropRowH));
        return (row >= 0 && row < kPropRowCount) ? row : -1;
    };

    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (!r.contains(event.position)) {
            closeNoteProperties(true); // click-away accepts, like Accept
            return true;
        }
        const float btnY = r.bottom() - kPropPad - 22.0f;
        const NUIRect resetRect(r.x + kPropPad, btnY, 88.0f, 22.0f);
        const NUIRect acceptRect(r.right() - kPropPad - 88.0f, btnY, 88.0f, 22.0f);
        if (resetRect.contains(event.position)) {
            notes_[propNoteIndex_] = propOriginalNote_;
            repaint();
            return true;
        }
        if (acceptRect.contains(event.position)) {
            closeNoteProperties(true);
            return true;
        }
        const int field = fieldAt(event.position);
        if (field != -1) {
            propDragField_ = field;
            propDragStartPos_ = event.position;
            propDragStartNote_ = notes_[propNoteIndex_];
        }
        return true;
    }

    if (event.released) {
        // No commitNotes() while the popup is open: it sorts notes_ by start
        // beat, which would silently retarget propNoteIndex_ after a Start
        // edit. closeNoteProperties() commits once, on Accept/cancel.
        propDragField_ = -1;
        return true;
    }

    if (propDragField_ != -1) {
        // Row drag: up increases. Alt refines the time fields.
        const float dy = propDragStartPos_.y - event.position.y;
        applyNotePropertyDelta(propDragField_, dy, !(event.modifiers & NUIModifiers::Alt));
        return true;
    }

    if (event.wheelDelta != 0.0f) {
        const int field = fieldAt(event.position);
        if (field != -1) {
            // One step per notch, expressed as the drag distance of one step.
            static constexpr float kStepPixels[kPropRowCount] = {6.0f, 4.0f, 4.0f, 8.0f, 8.0f};
            propDragStartNote_ = notes_[propNoteIndex_];
            applyNotePropertyDelta(field,
                                   (event.wheelDelta > 0.0f ? 1.0f : -1.0f) * kStepPixels[field],
                                   !(event.modifiers & NUIModifiers::Alt));
        }
        return true;
    }

    return true; // modal: swallow hover/motion so nothing edits underneath
}

void PianoRollNoteLayer::renderNoteProperties(NUIRenderer& renderer) {
    if (propNoteIndex_ < 0 || propNoteIndex_ >= static_cast<int>(notes_.size())) return;
    const auto& n = notes_[propNoteIndex_];
    auto& theme = NUIThemeManager::getInstance();
    const NUIRect& r = propPanelRect_;

    renderer.drawShadow(r, 0.0f, 5.0f, 18.0f, NUIColor(0.0f, 0.0f, 0.0f, 0.5f));
    renderer.fillRoundedRect(r, 8.0f, theme.getColor("backgroundSecondary").withAlpha(0.98f));
    renderer.strokeRoundedRect(r, 8.0f, 1.0f, theme.getColor("border").withAlpha(0.85f));

    const auto accent = theme.getColor("accentPrimary");
    const auto labelColor = theme.getColor("textSecondary").withAlpha(0.85f);
    const auto valueColor = theme.getColor("textPrimary").withAlpha(0.96f);

    const std::string title = "Note Properties - " + MusicTheory::getPitchName(n.pitch);
    renderer.drawText(title, NUIPoint(r.x + kPropPad + 2.0f, r.y + kPropPad + 2.0f), 10.5f,
                      accent.withAlpha(0.95f));

    // Row values
    const int velMidi = static_cast<int>(std::lround(std::clamp(n.velocity, 0.0f, 1.0f) * 127.0f));
    const int panPct = static_cast<int>(std::lround(std::abs(n.pan) * 100.0f));
    const std::string panText =
        panPct == 0 ? "C" : ((n.pan < 0.0f ? "L " : "R ") + std::to_string(panPct));
    char startBuf[32];
    {
        const int bpb = std::max(1, beatsPerBar_);
        const int bar = static_cast<int>(n.startBeat / bpb) + 1;
        const double rem = n.startBeat - static_cast<double>((bar - 1) * bpb);
        const int beat = static_cast<int>(rem) + 1;
        const int pct = static_cast<int>(std::lround((rem - (beat - 1)) * 100.0));
        std::snprintf(startBuf, sizeof(startBuf), "%d:%d +%02d", bar, beat, pct);
    }
    char lenBuf[24];
    std::snprintf(lenBuf, sizeof(lenBuf), "%.2f beats", n.durationBeats);

    struct Row {
        const char* label;
        std::string value;
    };
    const Row rows[kPropRowCount] = {
        {"Pitch", MusicTheory::getPitchName(n.pitch)},
        {"Velocity", std::to_string(velMidi)},
        {"Pan", panText},
        {"Start", startBuf},
        {"Length", lenBuf},
    };

    const float rowsTop = r.y + kPropPad + kPropTitleH;
    for (int i = 0; i < kPropRowCount; ++i) {
        const NUIRect rowRect(r.x + 4.0f, rowsTop + i * kPropRowH, r.width - 8.0f, kPropRowH);
        if (i == propDragField_) {
            renderer.fillRoundedRect(rowRect, 4.0f, accent.withAlpha(0.16f));
        }
        const float textY = rowRect.y + 5.0f;
        renderer.drawText(rows[i].label, NUIPoint(r.x + kPropPad + 2.0f, textY), 10.0f, labelColor);
        const auto valDim = renderer.measureText(rows[i].value, 10.0f);
        renderer.drawText(rows[i].value,
                          NUIPoint(r.right() - kPropPad - 2.0f - valDim.width, textY), 10.0f,
                          i == propDragField_ ? accent.withAlpha(0.98f) : valueColor);
    }

    // Footer buttons
    const float btnY = r.bottom() - kPropPad - 22.0f;
    const NUIRect resetRect(r.x + kPropPad, btnY, 88.0f, 22.0f);
    const NUIRect acceptRect(r.right() - kPropPad - 88.0f, btnY, 88.0f, 22.0f);
    renderer.fillRoundedRect(resetRect, 5.0f, theme.getColor("surfaceRaised").withAlpha(0.9f));
    renderer.strokeRoundedRect(resetRect, 5.0f, 1.0f, theme.getColor("border").withAlpha(0.7f));
    renderer.fillRoundedRect(acceptRect, 5.0f, accent.withAlpha(0.28f));
    renderer.strokeRoundedRect(acceptRect, 5.0f, 1.0f, accent.withAlpha(0.8f));
    const auto resetDim = renderer.measureText("Reset", 10.0f);
    renderer.drawText("Reset",
                      NUIPoint(resetRect.x + (resetRect.width - resetDim.width) * 0.5f,
                               resetRect.y + 5.0f),
                      10.0f, labelColor);
    const auto acceptDim = renderer.measureText("Accept", 10.0f);
    renderer.drawText("Accept",
                      NUIPoint(acceptRect.x + (acceptRect.width - acceptDim.width) * 0.5f,
                               acceptRect.y + 5.0f),
                      10.0f, valueColor);
}

void PianoRollNoteLayer::onRender(NUIRenderer& renderer) {
    if (!isVisible()) return;
    auto b = getBounds();
    auto& themeManager = NUIThemeManager::getInstance();
    
    // CLIP TO BOUNDS
    renderer.setClipRect(b);
    
    const auto noteColor = themeManager.getColor("accentPrimary").lightened(0.04f);
    const auto noteColorSelected = themeManager.getColor("accentSecondary").lightened(0.12f);
    
    // 1. GHOST NOTES (Read-only backgrounds)
    for (const auto& ghost : ghostPatterns_) {
        NUIColor gCol = ghost.color.withAlpha(ghost.fillAlpha);
        NUIColor gBorder = ghost.color.withAlpha(ghost.strokeAlpha);
        
        for (const auto& n : ghost.notes) {
            float x = snapRectX(beatToScreenX(n.startBeat, pixelsPerBeat_, scrollX_, b.x));
            float y = b.y + (127 - n.pitch) * keyHeight_ - scrollY_;
            float w = std::max(1.0f, std::round(static_cast<float>(n.durationBeats * pixelsPerBeat_)));
            float h = keyHeight_;
            
            if (x + w < b.x || x > b.x + b.width || y + h < b.y || y > b.y + b.height) continue;
            
            NUIRect r(x, y + 2, std::max(4.0f, w), h - 4);
            renderer.fillRoundedRect(r, 2.0f, gCol);
            renderer.strokeRoundedRect(r, 2.0f, 1.0f, gBorder);
        }
    }
    
    const double visibleEndBeat = (scrollX_ + b.width) / pixelsPerBeat_;

    for (size_t noteIndex = 0; noteIndex < notes_.size(); ++noteIndex) {
        const auto& n = notes_[noteIndex];
        if (n.startBeat > visibleEndBeat) break;
        
        float x = snapRectX(beatToScreenX(n.startBeat, pixelsPerBeat_, scrollX_, b.x));
        float y = b.y + (127 - n.pitch) * keyHeight_ - scrollY_;
        float w = std::max(1.0f, std::round(static_cast<float>(n.durationBeats * pixelsPerBeat_)));
        float h = keyHeight_;
        
        if (x + w < b.x || y + h < b.y || y > b.y + b.height) continue;
        
        // Skip deleted notes
        if (n.isDeleted) continue;
        
        NUIRect r(x + 1.0f, y + 2.0f, std::max(6.0f, w - 2.0f), std::max(5.0f, h - 4.0f));

        const float normalizedVelocity = std::clamp(n.velocity, 0.0f, 1.0f);
        const bool isHovered = static_cast<int>(noteIndex) == hoveredNoteIndex_;
        // A note "sounds" while the playhead sits within its span during
        // playback — it briefly lifts so the ear and eye stay in sync.
        const bool isSounding = isPlaying_ && playheadBeat_ >= n.startBeat &&
                                playheadBeat_ < n.startBeat + n.durationBeats;
        const NUIColor baseColor = n.selected ? noteColorSelected : noteColor;
        NUIColor coreColor = baseColor.withAlpha(0.68f + normalizedVelocity * 0.28f);
        NUIColor edgeColor = n.selected ? NUIColor::white().withAlpha(0.78f)
                                        : (isHovered ? NUIColor::white().withAlpha(0.38f)
                                                     : baseColor.lightened(0.16f).withAlpha(0.74f));
        if (isSounding) {
            coreColor = baseColor.lightened(0.30f).withAlpha(1.0f);
            edgeColor = NUIColor::white().withAlpha(0.88f);
        }

        if (isSounding) {
            renderer.drawShadow(r, 0.0f, 0.0f, 9.0f, baseColor.withAlpha(0.55f));
        }
        renderer.drawShadow(NUIRect(r.x, r.y + 1.0f, r.width, r.height),
                            0.0f,
                            2.0f,
                            5.0f,
                            NUIColor(0, 0, 0, n.selected ? 0.22f : 0.14f));
        renderer.fillRoundedRect(r, 3.0f, coreColor);
        renderer.strokeRoundedRect(r, 3.0f, n.selected ? 1.5f : (isHovered ? 1.25f : 1.0f), edgeColor);
        renderer.fillRoundedRect(NUIRect(r.x + 1.0f, r.y + 2.0f, 2.5f, std::max(2.0f, r.height - 4.0f)),
                                 1.0f,
                                 NUIColor::white().withAlpha(n.selected ? 0.68f : 0.42f));

        if (isHovered && !hoverOnRightEdge_ && !hoverOnLeftEdge_) {
            renderer.fillRoundedRect(NUIRect(r.x + 4.0f, r.y + 1.0f, std::max(2.0f, r.width - 8.0f), 1.0f),
                                     0.5f,
                                     NUIColor::white().withAlpha(0.24f));
        }

        // Selected notes retain subtle resize handles; hovered edges intensify them.
        if (n.selected || (isHovered && (hoverOnRightEdge_ || hoverOnLeftEdge_))) {
            const NUIColor affordanceColor = NUIColor::white().withAlpha(0.72f);
            if (n.selected || hoverOnRightEdge_) {
                renderer.fillRoundedRect(NUIRect(r.right() - 3.0f, r.y + 3.0f, 2.0f, r.height - 6.0f), 1.0f, affordanceColor);
            }
            if (n.selected || hoverOnLeftEdge_) {
                renderer.fillRoundedRect(NUIRect(r.x + 1.0f, r.y + 3.0f, 2.0f, r.height - 6.0f), 1.0f, affordanceColor);
            }
        }

        // [FEATURE] Render pitch name label inside the note block
        // Minimum width threshold: hide label if note is too narrow to avoid overflow
        constexpr float kMinWidthForLabel = 22.0f;
        if (w >= kMinWidthForLabel) {
            std::string pitchLabel = MusicTheory::getPitchName(n.pitch);
            constexpr float kFontSize = 10.0f;
            auto measured = renderer.measureText(pitchLabel, kFontSize);
            // Left-align with padding, vertically centered
            float labelX = r.x + 7.0f;
            float labelY = r.y + (r.height - measured.height) * 0.5f;
            // Clip label to note bounds (renderer should handle this, but extra safety)
            if (labelX + measured.width < r.x + r.width) {
                NUIColor labelColor = NUIColor::white().withAlpha(0.88f);
                renderer.drawText(pitchLabel, NUIPoint(labelX, labelY), kFontSize, labelColor);
            }
        }
    }

    // A multi-note selection exposes one time-transform frame. Dragging the
    // right handle scales both note spacing and note lengths around the
    // earliest selected start, preserving the phrase's internal rhythm.
    const NUIRect selectionBounds = selectionTimeBoundsRect();
    if (selectionBounds.width > 0.0f && selectionBounds.height > 0.0f) {
        const auto accent = themeManager.getColor("accentSecondary");
        renderer.strokeRoundedRect(selectionBounds, 3.0f, 1.0f, accent.withAlpha(0.68f));
        const NUIRect handle = selectionStretchHandleRect();
        renderer.fillRoundedRect(handle, 3.0f,
                                 accent.lightened(m_hoverOnSelectionStretch ? 0.24f : 0.10f)
                                     .withAlpha(m_hoverOnSelectionStretch ? 1.0f : 0.88f));
        renderer.strokeRoundedRect(handle, 3.0f, 1.0f, NUIColor::white().withAlpha(0.82f));
    }

    // Draw-mode preview: a translucent phantom of the note a click would place,
    // tracking the snapped cursor cell so placement reads before you commit it.
    // Only while the pencil hovers empty space — never over an existing note.
    if (tool_ == GlobalTool::Pencil && state_ == State::None && hoveredNoteIndex_ == -1 &&
        hoveredPitch_ >= 0 && hoverBeat_ >= 0.0) {
        // In chord mode the phantom shows the whole diatonic triad a click stamps.
        const std::vector<int> previewPitches =
            chordMode_ ? buildTriad(snapPitchToScale(hoveredPitch_))
                       : std::vector<int>{snapPitchToScale(hoveredPitch_)};
        const float px = snapRectX(beatToScreenX(hoverBeat_, pixelsPerBeat_, scrollX_, b.x));
        const float pw = std::max(6.0f, std::round(static_cast<float>(lastNoteDuration_ * pixelsPerBeat_)) - 2.0f);
        const float ph = std::max(5.0f, keyHeight_ - 4.0f);
        for (int previewPitch : previewPitches) {
            const float py = b.y + (127 - previewPitch) * keyHeight_ - scrollY_;
            if (px + pw >= b.x && px <= b.right() && py + ph >= b.y && py <= b.bottom()) {
                const NUIRect pr(px + 1.0f, py + 2.0f, pw, ph);
                renderer.fillRoundedRect(pr, 3.0f, noteColor.withAlpha(0.20f));
                renderer.strokeRoundedRect(pr, 3.0f, 1.0f, noteColor.lightened(0.16f).withAlpha(0.48f));
            }
        }
    }

    // Floating edit HUD while placing/moving/resizing a note: read what you're
    // doing — pitch + bar:beat when moving, length while resizing, pitch + length
    // while drawing. Tracks the grabbed note and flips below if it clips the top.
    const bool isEditingNote = (state_ == State::Painting || state_ == State::Moving ||
                                state_ == State::Resizing || state_ == State::ResizingLeft);
    if (isEditingNote && dragAnchorIndex_ >= 0 && dragAnchorIndex_ < static_cast<int>(notes_.size()) &&
        !notes_[dragAnchorIndex_].isDeleted) {
        const auto& an = notes_[dragAnchorIndex_];

        const auto formatLength = [](double beats) {
            char buf[24];
            if (std::abs(beats - std::round(beats)) < 0.01)
                std::snprintf(buf, sizeof(buf), "%d beat%s", static_cast<int>(std::round(beats)),
                              std::abs(beats - 1.0) < 0.01 ? "" : "s");
            else
                std::snprintf(buf, sizeof(buf), "%.2f beats", beats);
            return std::string(buf);
        };
        const auto formatBarBeat = [this](double startBeat) {
            const int bpb = std::max(1, beatsPerBar_);
            const double barIndex = std::floor(startBeat / bpb);
            const double beatInBar = startBeat - barIndex * bpb;
            char buf[24];
            std::snprintf(buf, sizeof(buf), "%d:%.2f", static_cast<int>(barIndex) + 1, beatInBar + 1.0);
            return std::string(buf);
        };

        std::string label;
        if (state_ == State::Resizing || state_ == State::ResizingLeft)
            label = formatLength(an.durationBeats);
        else if (state_ == State::Moving)
            label = MusicTheory::getPitchName(an.pitch) + "  " + formatBarBeat(an.startBeat);
        else // Painting: pitch + the length being dragged out
            label = MusicTheory::getPitchName(an.pitch) + "  " + formatLength(an.durationBeats);

        const float ax = snapRectX(beatToScreenX(an.startBeat, pixelsPerBeat_, scrollX_, b.x));
        const float ay = b.y + (127 - an.pitch) * keyHeight_ - scrollY_;
        constexpr float kFontSize = 10.0f;
        const auto measured = renderer.measureText(label, kFontSize);
        const float bubbleW = measured.width + 12.0f;
        const float bubbleH = 17.0f;
        float bubbleY = ay - bubbleH - 3.0f;
        if (bubbleY < b.y) bubbleY = ay + keyHeight_ + 3.0f;
        const float bubbleX = std::clamp(ax, b.x + 2.0f, b.right() - bubbleW - 2.0f);
        const NUIRect bubble(bubbleX, bubbleY, bubbleW, bubbleH);
        renderer.fillRoundedRect(bubble, 4.0f, NUIColor::black().withAlpha(0.92f));
        renderer.strokeRoundedRect(bubble, 4.0f, 1.0f, themeManager.getColor("accentPrimary").withAlpha(0.80f));
        renderer.drawText(label,
                          NUIPoint(bubble.x + 6.0f, bubble.y + (bubble.height - measured.height) * 0.5f),
                          kFontSize,
                          NUIColor::white().withAlpha(0.95f));
    }

    // Velocity value bubble after an Alt+wheel nudge, while the cursor stays on
    // the note it edited (guarded so a -1/-1 match can't show a phantom bubble).
    if (velocityBubbleIndex_ >= 0 && velocityBubbleIndex_ == hoveredNoteIndex_ &&
        velocityBubbleIndex_ < static_cast<int>(notes_.size()) && !notes_[velocityBubbleIndex_].isDeleted) {
        const auto& vn = notes_[velocityBubbleIndex_];
        const float vx = snapRectX(beatToScreenX(vn.startBeat, pixelsPerBeat_, scrollX_, b.x));
        const float vy = b.y + (127 - vn.pitch) * keyHeight_ - scrollY_;
        const std::string label = "V " + std::to_string(static_cast<int>(std::lround(vn.velocity * 127.0f)));
        constexpr float kFontSize = 10.0f;
        const auto measured = renderer.measureText(label, kFontSize);
        const float bubbleW = measured.width + 12.0f;
        const float bubbleH = 17.0f;
        float bubbleY = vy - bubbleH - 3.0f;
        if (bubbleY < b.y) bubbleY = vy + keyHeight_ + 3.0f;
        const float bubbleX = std::clamp(vx, b.x + 2.0f, b.right() - bubbleW - 2.0f);
        const NUIRect bubble(bubbleX, bubbleY, bubbleW, bubbleH);
        renderer.fillRoundedRect(bubble, 4.0f, NUIColor::black().withAlpha(0.92f));
        renderer.strokeRoundedRect(bubble, 4.0f, 1.0f, themeManager.getColor("accentPrimary").withAlpha(0.80f));
        renderer.drawText(label,
                          NUIPoint(bubble.x + 6.0f, bubble.y + (bubble.height - measured.height) * 0.5f),
                          kFontSize,
                          NUIColor::white().withAlpha(0.95f));
    }

    // Rubber-band selection rectangle (already normalized during drag)
    if (state_ == State::SelectingBox && selectionRect_.width > 0 && selectionRect_.height > 0) {
        renderer.fillRoundedRect(selectionRect_, 2.0f, NUIThemeManager::getInstance().getColor("accentPrimary").withAlpha(0.15f));
        renderer.strokeRoundedRect(selectionRect_, 2.0f, 1.0f, NUIThemeManager::getInstance().getColor("accentPrimary").withAlpha(0.55f));
    }

    renderNoteProperties(renderer);

    renderer.clearClipRect();

    // Cleanup Deleted Notes
    bool cleanNeeded = false;
    for (const auto& n : notes_) { 
        if (n.isDeleted && n.animationScale <= 0.001f) {
            cleanNeeded = true; 
            break; 
        }
    }
    
    if (cleanNeeded) {
        auto it = std::remove_if(notes_.begin(), notes_.end(), [](const MidiNote& n){ 
            return n.isDeleted && n.animationScale <= 0.001f; 
        });
        if (it != notes_.end()) {
            notes_.erase(it, notes_.end());
            commitNotes(); // Notify removal
        }
    }
}

int PianoRollNoteLayer::findNoteAt(float localX, float localY) {
    return findNoteAtLocal(notes_, localX, localY, pixelsPerBeat_, keyHeight_);
}

NUIRect PianoRollNoteLayer::selectionTimeBoundsRect() const {
    size_t selectedCount = 0;
    double startBeat = std::numeric_limits<double>::max();
    double endBeat = 0.0;
    int highestPitch = 0;
    int lowestPitch = 127;

    for (const auto& note : notes_) {
        if (!note.selected || note.isDeleted)
            continue;
        ++selectedCount;
        startBeat = std::min(startBeat, note.startBeat);
        endBeat = std::max(endBeat, note.startBeat + note.durationBeats);
        highestPitch = std::max(highestPitch, note.pitch);
        lowestPitch = std::min(lowestPitch, note.pitch);
    }
    if (selectedCount < 2 || endBeat <= startBeat)
        return NUIRect(0.0f, 0.0f, 0.0f, 0.0f);

    const auto bounds = getBounds();
    const float left = bounds.x + static_cast<float>(startBeat * pixelsPerBeat_) - scrollX_;
    const float right = bounds.x + static_cast<float>(endBeat * pixelsPerBeat_) - scrollX_;
    const float top = bounds.y + static_cast<float>(127 - highestPitch) * keyHeight_ - scrollY_ + 1.0f;
    const float bottom = bounds.y + static_cast<float>(128 - lowestPitch) * keyHeight_ - scrollY_ - 1.0f;
    return NUIRect(left, top, std::max(1.0f, right - left), std::max(1.0f, bottom - top));
}

NUIRect PianoRollNoteLayer::selectionStretchHandleRect() const {
    const NUIRect selection = selectionTimeBoundsRect();
    if (selection.width <= 0.0f || selection.height <= 0.0f)
        return NUIRect(0.0f, 0.0f, 0.0f, 0.0f);
    return NUIRect(selection.right() - SELECTION_STRETCH_HANDLE_WIDTH * 0.5f,
                   selection.y + (selection.height - SELECTION_STRETCH_HANDLE_HEIGHT) * 0.5f,
                   SELECTION_STRETCH_HANDLE_WIDTH, SELECTION_STRETCH_HANDLE_HEIGHT);
}

bool PianoRollNoteLayer::onMouseEvent(const NUIMouseEvent& event) {
    if (state_ == State::None && !getBounds().contains(event.position)) {
        // Reset hover when leaving bounds
        if (hoveredNoteIndex_ != -1) {
            hoveredNoteIndex_ = -1;
            hoverOnRightEdge_ = false;
            hoverOnLeftEdge_ = false;
            if (platformBridge_) platformBridge_->setCursorStyle(NUICursorStyle::Arrow);
        }
        if (m_hoverOnSelectionStretch) {
            m_hoverOnSelectionStretch = false;
            if (platformBridge_)
                platformBridge_->setCursorStyle(NUICursorStyle::Arrow);
            repaint();
        }
        if (hoveredPitch_ != -1) {
            hoveredPitch_ = -1;
            if (onHoveredPitchChanged_) onHoveredPitchChanged_(-1);
        }
        if (hoverBeat_ >= 0.0) {
            hoverBeat_ = -1.0;
            repaint();
        }
        return false;
    }

    auto b = getBounds();
    float localX = event.position.x - b.x + scrollX_;
    float localY = event.position.y - b.y + scrollY_;

    // Holding Alt mid-drag bypasses the grid for fine placement. Clone drags
    // (CopyDragging) are excluded — Alt is what starts them, so it can't
    // double as the fine toggle there. Recomputed every event so it can never
    // go stale between gestures.
    fineDrag_ = (event.modifiers & NUIModifiers::Alt) &&
                (state_ == State::Painting || state_ == State::Moving || state_ == State::Resizing ||
                 state_ == State::ResizingLeft || state_ == State::StretchingSelection);

    // The Note Properties popup is modal within the layer while open.
    if (propNoteIndex_ >= 0) {
        return handleNotePropertiesMouse(event);
    }

    const int cursorPitch = std::clamp(127 - static_cast<int>(localY / keyHeight_), 0, 127);
    if (hoveredPitch_ != cursorPitch) {
        hoveredPitch_ = cursorPitch;
        if (onHoveredPitchChanged_) onHoveredPitchChanged_(cursorPitch);
        // Vertical moves within the same snapped beat leave hoverBeat_
        // unchanged, so without this the pencil's phantom note would linger
        // on the previous pitch row.
        repaint();
    }

    // --- HOVER / SMART CURSOR (no button activity) ---
    if (state_ == State::None && !event.pressed && !event.released) {
        const bool onSelectionStretch =
            tool_ != GlobalTool::Eraser && selectionStretchHandleRect().contains(event.position);
        if (m_hoverOnSelectionStretch != onSelectionStretch) {
            m_hoverOnSelectionStretch = onSelectionStretch;
            repaint();
        }
        if (onSelectionStretch) {
            hoveredNoteIndex_ = -1;
            hoverOnRightEdge_ = false;
            hoverOnLeftEdge_ = false;
            hoverBeat_ = -1.0;
            if (platformBridge_)
                platformBridge_->setCursorStyle(NUICursorStyle::ResizeEW);
            return true;
        }

        int hitIdx = findNoteAt(localX, localY);
        // Track the snapped beat the cursor sits on so the pencil can render a
        // phantom of the note a click would place. Only meaningful over empty
        // space with the pencil active; suppressed elsewhere so it never lingers.
        const double snappedHoverBeat =
            (tool_ == GlobalTool::Pencil && hitIdx == -1)
                ? snapToGrid(std::max(0.0, static_cast<double>(localX) / pixelsPerBeat_))
                : -1.0;
        if (hoverBeat_ != snappedHoverBeat) {
            hoverBeat_ = snappedHoverBeat;
            repaint();
        }
        if (hitIdx == -1) {
            if (hoveredNoteIndex_ != -1) {
                hoveredNoteIndex_ = -1;
                hoverOnRightEdge_ = false;
                hoverOnLeftEdge_ = false;
                repaint();
            }
            if (platformBridge_) {
                platformBridge_->setCursorStyle(
                    tool_ == GlobalTool::Pencil ? NUICursorStyle::Crosshair : NUICursorStyle::Arrow);
            }
        } else {
            const auto& n = notes_[hitIdx];
            float nx = b.x + static_cast<float>(n.startBeat * pixelsPerBeat_) - scrollX_;
            float nw = static_cast<float>(n.durationBeats * pixelsPerBeat_);
            float edgeZone = std::min(10.0f, nw * 0.30f);
            bool onLeftEdge = (event.position.x <= nx + edgeZone);
            bool onRightEdge = (event.position.x >= nx + nw - edgeZone);

            if (hoveredNoteIndex_ != hitIdx || hoverOnRightEdge_ != onRightEdge || hoverOnLeftEdge_ != onLeftEdge) {
                hoveredNoteIndex_ = hitIdx;
                hoverOnRightEdge_ = onRightEdge;
                hoverOnLeftEdge_ = onLeftEdge;
                repaint();
            }
            if (platformBridge_) {
                if (onLeftEdge || onRightEdge)
                    platformBridge_->setCursorStyle(NUICursorStyle::ResizeEW);
                else
                    platformBridge_->setCursorStyle(NUICursorStyle::Grab);
            }
        }
    }

    // --- ALT + WHEEL OVER A NOTE = VELOCITY ---
    // Alt shapes the velocity of the note under the cursor (and the whole
    // selection if that note is part of it). Kept behind a modifier so plain
    // wheel always scrolls — mixing "scroll the grid" and "edit the note" on
    // the same gesture made scrolling feel unpredictable.
    if (event.wheelDelta != 0.0f && (event.modifiers & NUIModifiers::Alt) && hoveredNoteIndex_ >= 0 &&
        hoveredNoteIndex_ < static_cast<int>(notes_.size()) && !notes_[hoveredNoteIndex_].isDeleted) {
        auto oldNotes = notes_;
        const float delta = event.wheelDelta * 0.04f; // ~5 MIDI steps per notch
        const bool editSelection = notes_[hoveredNoteIndex_].selected;
        for (auto& n : notes_) {
            if (n.isDeleted) continue;
            if (editSelection ? n.selected : (&n == &notes_[hoveredNoteIndex_])) {
                n.velocity = std::clamp(n.velocity + delta, 0.0f, 1.0f);
            }
        }
        lastNoteVelocity_ = notes_[hoveredNoteIndex_].velocity; // adopt for new notes
        velocityBubbleIndex_ = hoveredNoteIndex_;
        // Coalesce a continuous scrub into one undo step instead of one per notch.
        if (!undoStack_.empty() && undoStack_.back().description == "Velocity") {
            undoStack_.back().notesAfter = notes_;
        } else {
            pushUndo("Velocity", oldNotes, notes_);
        }
        commitNotes();
        repaint();
        return true;
    }

    // --- RIGHT CLICK / ERASER (FAST ERASE) ---
    if (event.button == NUIMouseButton::Right) {
        if (event.pressed) {
            state_ = State::Erasing;
            dragStartNotes_ = notes_;
            eraseStrokeChanged_ = false;

            const int idx = findNoteAt(localX, localY);
            if (idx != -1) {
                notes_.erase(notes_.begin() + idx);
                eraseStrokeChanged_ = true;
                repaint();
            }
            return true;
        }
        if (event.released && state_ == State::Erasing) {
            if (eraseStrokeChanged_) {
                pushUndo("Erase Stroke", dragStartNotes_, notes_);
                commitNotes();
            }
            state_ = State::None;
            eraseStrokeChanged_ = false;
            repaint();
            return true;
        }
    }
    
    if (state_ == State::Erasing && !event.released) {
        int idx = findNoteAt(localX, localY);
        if (idx != -1 && !notes_[idx].isDeleted) {
            notes_.erase(notes_.begin() + idx);
            eraseStrokeChanged_ = true;
            repaint();
        }
        return true;
    }

    // --- LEFT CLICK HANDLING ---
    if (event.pressed && event.button == NUIMouseButton::Left) {
        setFocused(true); // Gain keyboard focus for shortcuts

        // The shared handle takes precedence over the right edge of the last
        // selected note. Its gesture changes the complete phrase timing,
        // whereas an ordinary note-edge drag still resizes lengths only.
        if (tool_ != GlobalTool::Eraser && selectionStretchHandleRect().contains(event.position)) {
            state_ = State::StretchingSelection;
            m_selectionStretchChanged = false;
            dragStartPos_ = event.position;
            dragStartScrollX_ = scrollX_;
            dragStartNotes_ = notes_;
            m_hoverOnSelectionStretch = true;
            if (platformBridge_)
                platformBridge_->setCursorStyle(NUICursorStyle::ResizeEW);
            repaint();
            return true;
        }

        int clickedIndex = findNoteAt(localX, localY);

        // The platform never sets event.doubleClick, so detect it here: a
        // second left press within 400ms landing within a few pixels.
        const long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch())
                                    .count();
        const bool isDoubleClick =
            event.doubleClick ||
            ((nowMs - lastClickTimeMs_) < 400 &&
             std::abs(event.position.x - lastClickPos_.x) < 5.0f &&
             std::abs(event.position.y - lastClickPos_.y) < 5.0f);
        // Consume the pair so a triple-click can't read as two doubles.
        lastClickTimeMs_ = isDoubleClick ? 0 : nowMs;
        lastClickPos_ = event.position;

        // DOUBLE CLICK: note → precision properties popup; empty space → add.
        // (Deletion stays on right-click / eraser / Delete.)
        if (isDoubleClick) {
            if (clickedIndex != -1) {
                 openNoteProperties(clickedIndex);
            } else {
                 auto oldNotes = notes_;
                 // Create New Note
                 double beat = std::max(0.0, static_cast<double>(localX / pixelsPerBeat_));
                 beat = snapToGrid(beat);
                 
                  int pitch = 127 - static_cast<int>(localY / keyHeight_);
                  pitch = std::clamp(pitch, 0, 127);
                  pitch = snapPitchToScale(pitch);
                 
                 MidiNote newNote;
                 newNote.pitch = pitch;
                 newNote.startBeat = beat;
                 newNote.durationBeats = lastNoteDuration_; 
                 newNote.velocity = lastNoteVelocity_;
                 newNote.unitId = defaultUnitId_;
                 newNote.selected = true;
                 newNote.animationScale = 1.0f; // Instant appearance

                 if (!(event.modifiers & NUIModifiers::Shift)) {
                    for(auto& n : notes_) n.selected = false;
                 }
                 
                 notes_.push_back(newNote);
                 pushUndo("Add Note", oldNotes, notes_);
                 commitNotes();
                 repaint();
            }
            return true;
        }

        // Ctrl+drag on empty space = marquee select, in any tool. This is how a
        // lasso coexists with the pencil (whose plain drag places notes): the
        // pencil keeps drawing, and Ctrl temporarily borrows a selection box.
        if (clickedIndex == -1 && (event.modifiers & NUIModifiers::Ctrl)) {
            state_ = State::SelectingBox;
            dragStartPos_ = event.position;
            selectionRect_ = NUIRect(event.position.x, event.position.y, 0, 0);
            if (!(event.modifiers & NUIModifiers::Shift)) {
                for (auto& n : notes_) n.selected = false;
            }
            repaint();
            return true;
        }

        // 1. Eraser Tool
        if (tool_ == GlobalTool::Eraser) {
            if (clickedIndex != -1) {
                auto oldNotes = notes_;
                notes_.erase(notes_.begin() + clickedIndex);
                pushUndo("Erase", oldNotes, notes_);
                commitNotes();
                repaint();
            }
            return true;
        }
        
        // 2. Pencil / Pointer
        // Smart Logic: If hovering a note, allow manipulation (Move/Resize) unless explicitly blocked
        // User Request: "Pen... placing notes moving notes... place but not extend or move" -> Enable Move/Resize in Pen mode.
        
        bool intentToPaint = (tool_ == GlobalTool::Pencil && clickedIndex == -1);

        // Shift+pencil on empty space starts a paint-brush stroke: notes are
        // stamped into each snap cell the cursor crosses (see drag handling).
        // (Ctrl is reserved for the marquee, so the brush lives on Shift.)
        if (intentToPaint && (event.modifiers & NUIModifiers::Shift)) {
            for (auto& note : notes_) note.selected = false;
            state_ = State::BrushPainting;
            dragStartNotes_ = notes_; // snapshot for a single-stroke undo
            dragStartPos_ = event.position;
            dragStartScrollX_ = scrollX_;
            dragStartScrollY_ = scrollY_;
            paintBrushAt(localX, localY);
            repaint();
            return true;
        }

        // Chord mode: a click stamps the diatonic triad rooted at the cell
        // (discrete — no drag-to-lengthen; the stamped chord lands selected so
        // it can be resized or strummed as a unit right after).
        if (intentToPaint && chordMode_) {
            auto oldNotes = notes_;
            if (!(event.modifiers & NUIModifiers::Shift)) {
                for (auto& note : notes_) note.selected = false;
            }
            const double startBeat = snapToGrid(std::max(0.0, static_cast<double>(localX) / pixelsPerBeat_));
            const int rootPitch = snapPitchToScale(std::clamp(127 - static_cast<int>(localY / keyHeight_), 0, 127));
            for (int p : buildTriad(rootPitch)) {
                bool occupied = false;
                for (const auto& n : notes_) {
                    if (!n.isDeleted && n.pitch == p && std::abs(n.startBeat - startBeat) < 0.001) {
                        occupied = true;
                        break;
                    }
                }
                if (occupied) continue;
                MidiNote note;
                note.pitch = p;
                note.startBeat = startBeat;
                note.durationBeats = lastNoteDuration_;
                note.velocity = lastNoteVelocity_;
                note.unitId = defaultUnitId_;
                note.selected = true;
                note.animationScale = 1.0f;
                notes_.push_back(note);
            }
            // Sound the whole triad — the engine keeps a small pool of
            // audition voices, one command per pitch.
            if (!(isPlayingCallback_ && isPlayingCallback_()) && onPreviewNote_) {
                const int velocity = static_cast<int>(std::lround(lastNoteVelocity_ * 127.0f));
                for (int p : buildTriad(rootPitch)) {
                    onPreviewNote_(p, velocity);
                }
                auditionPitch_ = rootPitch;
            }
            pushUndo("Add Chord", oldNotes, notes_);
            commitNotes();
            repaint();
            return true;
        }

        if (intentToPaint) {
            // --- PAINT NEW NOTE ---
            if (!(event.modifiers & NUIModifiers::Shift)) {
                for (auto& note : notes_) note.selected = false;
            }

            state_ = State::Painting;
            dragStartNotes_ = notes_; 
            
            double beat = std::max(0.0, static_cast<double>(localX / pixelsPerBeat_));
            paintStartBeat_ = snapToGrid(beat);
            
            int pitch = 127 - static_cast<int>(localY / keyHeight_);
            pitch = std::clamp(pitch, 0, 127);
            paintPitch_ = snapPitchToScale(pitch);
            
            MidiNote newNote;
            newNote.pitch = paintPitch_;
            newNote.startBeat = paintStartBeat_;
            newNote.durationBeats = lastNoteDuration_; 
            newNote.velocity = lastNoteVelocity_;
            newNote.unitId = defaultUnitId_;
            newNote.selected = true;
            newNote.animationScale = 1.0f; // Instant appearance
            
            notes_.push_back(newNote);
            paintingNoteIndex_ = static_cast<int>(notes_.size()) - 1;
            dragAnchorIndex_ = paintingNoteIndex_;

            auditionPitch(paintPitch_); // sound the note as it's laid down

            dragStartPos_ = event.position;
            dragStartScrollX_ = scrollX_;
            dragStartScrollY_ = scrollY_;
            repaint();
            return true;
        }
        
        // Interact with Existing Note (Move/Resize/Select)
        if (clickedIndex != -1) {
            bool wasSelected = notes_[clickedIndex].selected;

            // Ctrl = Toggle
            if (event.modifiers & NUIModifiers::Ctrl) {
                notes_[clickedIndex].selected = !wasSelected;
                return true;
            }

            // Alt+drag: clone selection and drag copies — skip selection logic
            if (event.modifiers & NUIModifiers::Alt) {
                // Ensure clicked note is part of the selection
                notes_[clickedIndex].selected = true;
                copyDragIndices_.clear();

                // Clone all selected notes
                std::vector<int> selectedIndices;
                for (int i = 0; i < static_cast<int>(notes_.size()); ++i) {
                    if (notes_[i].selected && !notes_[i].isDeleted)
                        selectedIndices.push_back(i);
                }

                // Deselect originals, create clones, select clones
                for (int idx : selectedIndices) {
                    notes_[idx].selected = false;
                    MidiNote clone = notes_[idx];
                    clone.selected = true;
                    clone.isDeleted = false;
                    notes_.push_back(clone);
                    copyDragIndices_.push_back(static_cast<int>(notes_.size()) - 1);
                }

                if (copyDragIndices_.empty()) return true;

                // Snapshot AFTER cloning so dragStartNotes_ includes clones
                dragStartNotes_ = notes_;

                state_ = State::CopyDragging;
                dragStartPos_ = event.position;
                dragStartScrollX_ = scrollX_;
                dragStartScrollY_ = scrollY_;
                if (platformBridge_) platformBridge_->setCursorStyle(NUICursorStyle::Grabbing);
                repaint();
                return true;
            }

            // Shift = add to selection
            if (event.modifiers & NUIModifiers::Shift) {
                notes_[clickedIndex].selected = true;
            } else if (!wasSelected) {
                // Clicked unselected note without modifiers -> clear others and select this
                for (auto& N : notes_) N.selected = false;
                notes_[clickedIndex].selected = true;
            }
            // If clicked selected note, keep others selected (for group move)

            const auto& n = notes_[clickedIndex];
            float nx = static_cast<float>(n.startBeat * pixelsPerBeat_);
            float nw = static_cast<float>(n.durationBeats * pixelsPerBeat_);

            // Smart Edge Detection — left and right
            float edgeZone = std::min(10.0f, nw * 0.3f);
            bool isLeftEdge = (localX <= nx + edgeZone);
            bool isRightEdge = (localX >= nx + nw - edgeZone);

            if (isLeftEdge)
                state_ = State::ResizingLeft;
            else if (isRightEdge)
                state_ = State::Resizing;
            else
                state_ = State::Moving;

            dragStartPos_ = event.position;
            dragStartScrollX_ = scrollX_;
            dragStartScrollY_ = scrollY_;
            dragStartNotes_ = notes_;

            // Anchor the edit HUD to the grabbed note for move/resize.
            dragAnchorIndex_ = clickedIndex;
            // Grabbing a note to move it sounds its pitch, and keeps sounding
            // the new pitch as you drag it up/down.
            if (state_ == State::Moving) {
                moveAnchorPitch_ = notes_[clickedIndex].pitch;
                auditionPitch(moveAnchorPitch_);
            }

            // Spec 2: adopt clicked note's length for subsequent placement
            lastNoteDuration_ = notes_[clickedIndex].durationBeats;

            if (platformBridge_) {
                platformBridge_->setCursorStyle(
                    (isLeftEdge || isRightEdge) ? NUICursorStyle::ResizeEW : NUICursorStyle::Grabbing);
            }

            repaint();
            return true;
        }
        
        // Empty Click (Pointer or Pencil logic fell through) -> Selection Box
        // Only if NOT pencil (Pencil paints) - handled by intentToPaint
        if (tool_ == GlobalTool::Pointer) {
             state_ = State::SelectingBox;
             dragStartPos_ = event.position;
             selectionRect_ = NUIRect(event.position.x, event.position.y, 0, 0); // Start size 0
             
             if (!(event.modifiers & NUIModifiers::Shift)) {
                 for (auto& n : notes_) n.selected = false;
             }
             repaint();
             return true;
        }
    }
    
    // --- DRAGGING (Left Button) ---
    if (!event.pressed && !event.released && state_ != State::None) {
        auto parent = getParent();
        updateEdgeScrolling(event.position.x, event.position.y, b, [this, parent]() {
            if (parent) {
                parent->repaint();
            }
        });
        
        if (state_ == State::SelectingBox) {
            // Normalize at storage so render sees positive dimensions
            float rawW = event.position.x - dragStartPos_.x;
            float rawH = event.position.y - dragStartPos_.y;
            NUIRect norm(dragStartPos_.x, dragStartPos_.y, rawW, rawH);
            if (norm.width < 0) { norm.x += norm.width; norm.width *= -1; }
            if (norm.height < 0) { norm.y += norm.height; norm.height *= -1; }
            selectionRect_ = norm;
            
            // Select Intersecting Notes (marquee adds to selection during drag)
            for (auto& n : notes_) {
                // Skip deleted notes
                if (n.isDeleted) continue;

                float nx = b.x + static_cast<float>(n.startBeat * pixelsPerBeat_) - scrollX_;
                float ny = b.y + (127 - n.pitch) * keyHeight_ - scrollY_;
                float nw = static_cast<float>(n.durationBeats * pixelsPerBeat_);
                float nh = keyHeight_;

                NUIRect nr(nx, ny, nw, nh);

                // Standard marquee: select notes inside the box
                // Notes outside the box are left unchanged during drag
                // (final selection replacement happens on release)
                if (nr.x < norm.x + norm.width && nr.x + nr.width > norm.x &&
                    nr.y < norm.y + norm.height && nr.y + nr.height > norm.y) {
                    n.selected = true;
                }
            }
            repaint();
            return true;
        }
        
        if (state_ == State::BrushPainting) {
            // Stamp a note in whatever snap cell the cursor is over now.
            if (paintBrushAt(localX, localY)) repaint();
            return true;
        }

        if (state_ == State::Painting && paintingNoteIndex_ != -1) {
            float dx = (event.position.x - dragStartPos_.x) + (scrollX_ - dragStartScrollX_);
            double beatDelta = dx / pixelsPerBeat_;

            double newDur = lastNoteDuration_ + beatDelta;
            newDur = std::max(0.125, snapToGrid(newDur));
            
            notes_[paintingNoteIndex_].durationBeats = newDur;
            repaint();
            return true;
        }
        else if (state_ == State::Moving) {
            float dx = (event.position.x - dragStartPos_.x) + (scrollX_ - dragStartScrollX_);
            float dy = (event.position.y - dragStartPos_.y) + (scrollY_ - dragStartScrollY_);

            double beatDelta = dx / pixelsPerBeat_;
            int pitchDelta = -static_cast<int>(dy / keyHeight_);

            // Clamp beatDelta so the leftmost selected note doesn't go below 0
            double minStart = std::numeric_limits<double>::max();
            for (size_t i = 0; i < notes_.size(); ++i) {
                if (dragStartNotes_[i].selected)
                    minStart = std::min(minStart, dragStartNotes_[i].startBeat);
            }
            if (minStart + beatDelta < 0.0)
                beatDelta = -minStart;

            for (size_t i = 0; i < notes_.size(); ++i) {
                if (dragStartNotes_[i].selected) {
                    notes_[i].startBeat = snapToGrid(dragStartNotes_[i].startBeat + beatDelta);
                    int newPitch = dragStartNotes_[i].pitch + pitchDelta;
                    newPitch = std::clamp(newPitch, 0, 127);
                    notes_[i].pitch = snapPitchToScale(newPitch);
                }
            }
            // Re-audition when the grabbed note lands on a new pitch.
            auditionPitch(snapPitchToScale(std::clamp(moveAnchorPitch_ + pitchDelta, 0, 127)));
            repaint();
            return true;
        }
        else if (state_ == State::Resizing) {
            float dx = (event.position.x - dragStartPos_.x) + (scrollX_ - dragStartScrollX_);
            double beatDelta = dx / pixelsPerBeat_;

            for (size_t i = 0; i < notes_.size(); ++i) {
                if (dragStartNotes_[i].selected) {
                    double newDur = dragStartNotes_[i].durationBeats + beatDelta;
                    notes_[i].durationBeats = std::max(0.125, snapToGrid(newDur));
                }
            }
            repaint();
            return true;
        }
        else if (state_ == State::ResizingLeft) {
            // Left-edge resize: move start, adjust duration to keep end fixed
            float dx = (event.position.x - dragStartPos_.x) + (scrollX_ - dragStartScrollX_);
            double beatDelta = dx / pixelsPerBeat_;

            for (size_t i = 0; i < notes_.size(); ++i) {
                if (dragStartNotes_[i].selected) {
                    double origStart = dragStartNotes_[i].startBeat;
                    double origEnd = origStart + dragStartNotes_[i].durationBeats;
                    double newStart = std::max(0.0, snapToGrid(origStart + beatDelta));
                    double newDur = origEnd - newStart;
                    if (newDur < 0.125) {
                        newDur = 0.125;
                        newStart = origEnd - 0.125;
                        if (newStart < 0.0) newStart = 0.0;
                    }
                    notes_[i].startBeat = newStart;
                    notes_[i].durationBeats = newDur;
                }
            }
            repaint();
            return true;
        } else if (state_ == State::StretchingSelection) {
            double anchorBeat = std::numeric_limits<double>::max();
            double originalEndBeat = 0.0;
            double minimumFactor = 0.0;
            for (const auto& note : dragStartNotes_) {
                if (!note.selected || note.isDeleted)
                    continue;
                anchorBeat = std::min(anchorBeat, note.startBeat);
                originalEndBeat = std::max(originalEndBeat, note.startBeat + note.durationBeats);
                minimumFactor = std::max(minimumFactor, 0.125 / std::max(0.125, note.durationBeats));
            }

            const double originalSpan = originalEndBeat - anchorBeat;
            if (std::isfinite(anchorBeat) && originalSpan > 0.0001) {
                const double cursorBeat = std::max(anchorBeat, static_cast<double>(localX) / pixelsPerBeat_);
                const double targetEndBeat =
                    std::max(anchorBeat + originalSpan * minimumFactor, snapToGrid(cursorBeat));
                const double factor = std::max(minimumFactor, (targetEndBeat - anchorBeat) / originalSpan);
                for (size_t i = 0; i < notes_.size() && i < dragStartNotes_.size(); ++i) {
                    const auto& original = dragStartNotes_[i];
                    if (!original.selected || original.isDeleted)
                        continue;
                    notes_[i].startBeat = anchorBeat + (original.startBeat - anchorBeat) * factor;
                    notes_[i].durationBeats = std::max(0.125, original.durationBeats * factor);
                }
                m_selectionStretchChanged = false;
                for (size_t i = 0; i < notes_.size() && i < dragStartNotes_.size(); ++i) {
                    const auto& original = dragStartNotes_[i];
                    if (!original.selected || original.isDeleted)
                        continue;
                    if (std::abs(notes_[i].startBeat - original.startBeat) > 0.000001 ||
                        std::abs(notes_[i].durationBeats - original.durationBeats) > 0.000001) {
                        m_selectionStretchChanged = true;
                        break;
                    }
                }
            }
            repaint();
            return true;
        } else if (state_ == State::CopyDragging) {
            float dx = (event.position.x - dragStartPos_.x) + (scrollX_ - dragStartScrollX_);
            float dy = (event.position.y - dragStartPos_.y) + (scrollY_ - dragStartScrollY_);

            double beatDelta = dx / pixelsPerBeat_;
            int pitchDelta = -static_cast<int>(dy / keyHeight_);

            // Clamp beatDelta so the leftmost clone doesn't go below 0
            double minStart = std::numeric_limits<double>::max();
            for (int idx : copyDragIndices_) {
                if (idx >= 0 && idx < static_cast<int>(dragStartNotes_.size()))
                    minStart = std::min(minStart, dragStartNotes_[idx].startBeat);
            }
            if (minStart + beatDelta < 0.0)
                beatDelta = -minStart;

            for (int idx : copyDragIndices_) {
                if (idx >= 0 && idx < static_cast<int>(notes_.size())) {
                    notes_[idx].startBeat = snapToGrid(dragStartNotes_[idx].startBeat + beatDelta);
                    int newPitch = dragStartNotes_[idx].pitch + pitchDelta;
                    newPitch = std::clamp(newPitch, 0, 127);
                    notes_[idx].pitch = snapPitchToScale(newPitch);
                }
            }
            commitNotes();
            repaint();
            return true;
        }
    }
    
    // --- RELEASE ---
    if (event.released && event.button == NUIMouseButton::Left) {
        auditionStop(); // release any note sounded while painting/dragging
        dragAnchorIndex_ = -1;
        if (state_ == State::SelectingBox) {
            // Marquee selection done. Selection was cleared on click (without Shift),
            // and notes inside the box were selected during drag.
            state_ = State::None;
            selectionRect_ = NUIRect(0, 0, 0, 0);
            if (platformBridge_) platformBridge_->setCursorStyle(NUICursorStyle::Arrow);
            repaint();
            return true;
        }

        if (state_ != State::None) {
            // Update Memory
            if (state_ == State::Painting && paintingNoteIndex_ != -1) {
                lastNoteDuration_ = notes_[paintingNoteIndex_].durationBeats;
            } else if (state_ == State::Resizing || state_ == State::ResizingLeft ||
                       state_ == State::StretchingSelection) {
                for (const auto& n : notes_) { if (n.selected) { lastNoteDuration_ = n.durationBeats; break; } }
            }

            const std::string description = state_ == State::CopyDragging
                                                ? "Alt+Drag Copy"
                                                : (state_ == State::StretchingSelection ? "Stretch Selection" : "Edit");
            const bool shouldCommit = state_ != State::StretchingSelection || m_selectionStretchChanged;
            if (shouldCommit)
                pushUndo(description, dragStartNotes_, notes_);
            state_ = State::None;
            paintingNoteIndex_ = -1;
            copyDragIndices_.clear();
            m_hoverOnSelectionStretch = false;
            m_selectionStretchChanged = false;
            if (platformBridge_) platformBridge_->setCursorStyle(NUICursorStyle::Arrow);
            if (shouldCommit)
                commitNotes();
            repaint();
            return true;
        }
    }

    return NUIComponent::onMouseEvent(event);
}


// Static clipboard for now (shared across instances is fine/better)
static std::vector<MidiNote> s_noteClipboard;

bool PianoRollNoteLayer::onKeyEvent(const NUIKeyEvent& event) {
    bool ctrl = (event.modifiers & NUIModifiers::Ctrl);

    // Note Properties popup is modal: Enter accepts, Escape cancels, and
    // everything else is swallowed so shortcuts can't edit the note beneath.
    if (propNoteIndex_ >= 0 && event.pressed) {
        if (event.keyCode == NUIKeyCode::Escape) {
            closeNoteProperties(false);
            return true;
        }
        if (event.keyCode == NUIKeyCode::Enter) {
            closeNoteProperties(true);
            return true;
        }
        return true;
    }

    if (event.pressed) {
        // Undo / Redo
        if (ctrl && event.keyCode == NUIKeyCode::Z) {
            bool shift = (event.modifiers & NUIModifiers::Shift);
            if (shift) redo(); else undo();
            return true;
        }
        else if (ctrl && event.keyCode == NUIKeyCode::Y) {
            redo();
            return true;
        }

        // Ctrl+L: elongate selected notes to connect to the next note (legato),
        // or out to the next snap/beat boundary when nothing follows.
        if (ctrl && event.keyCode == NUIKeyCode::L) {
            connectSelectedNotes();
            return true;
        }

        // Q: quantize selected note starts to the grid. Ctrl+G: glue selected
        // notes on the same pitch into one. Both are no-ops without a selection.
        if (!ctrl && event.keyCode == NUIKeyCode::Q) {
            quantizeSelectedNotes();
            return true;
        }
        if (ctrl && event.keyCode == NUIKeyCode::G) {
            glueSelectedNotes();
            return true;
        }

        if (event.keyCode == NUIKeyCode::Delete || event.keyCode == NUIKeyCode::Backspace) {
            auto oldNotes = notes_; // Snapshot

            // Erase-remove: actually delete selected notes from the vector
            notes_.erase(
                std::remove_if(notes_.begin(), notes_.end(),
                    [](const MidiNote& n) { return n.selected; }),
                notes_.end());

            if (notes_.size() != oldNotes.size()) {
                pushUndo("Delete", oldNotes, notes_);
                commitNotes();
                repaint();
            }
            return true;
        }
        else if (ctrl && event.keyCode == NUIKeyCode::C) {
            // Copy
            s_noteClipboard.clear();
            for (const auto& n : notes_) {
                if (n.selected && !n.isDeleted) s_noteClipboard.push_back(n); // Don't copy deleted
            }
            return true;
        }
        else if (ctrl && event.keyCode == NUIKeyCode::V) {
            // Spec 5: Paste at playhead position
            if (s_noteClipboard.empty()) return true;

            auto oldNotes = notes_; // Snapshot

            // Deselect current
            for (auto& n : notes_) n.selected = false;

            // Find earliest note in clipboard, offset so it lands at playhead
            double earliest = s_noteClipboard[0].startBeat;
            for (const auto& n : s_noteClipboard) {
                if (n.startBeat < earliest) earliest = n.startBeat;
            }
            double offset = playheadBeat_ - earliest;

            for (auto n : s_noteClipboard) {
                n.startBeat += offset;
                n.selected = true;
                n.isDeleted = false;
                notes_.push_back(n);
            }
            pushUndo("Paste", oldNotes, notes_);
            commitNotes();
            repaint();
            return true;
        }
        else if (ctrl && event.keyCode == NUIKeyCode::D) {
            // Duplicate (Ctrl+D)
            double minStart = 100000.0;
            double maxEnd = -1.0;
            bool hasSelection = false;
            
            for (const auto& n : notes_) {
                if (n.selected && !n.isDeleted) {
                    hasSelection = true;
                    minStart = std::min(minStart, n.startBeat);
                    maxEnd = std::max(maxEnd, n.startBeat + n.durationBeats);
                }
            }
            
            if (hasSelection && maxEnd > 0) {
                double shift = maxEnd - minStart;
                if (shift < 0.25) shift = 0.25;
                
                
                auto oldNotes = notes_; // Snapshot
                
                for (auto& n : notes_) n.selected = false;

                for (const auto& n : oldNotes) {
                    if (n.selected && !n.isDeleted) {
                        MidiNote clone = n;
                        clone.startBeat += shift;
                        clone.selected = true; 
                        clone.isDeleted = false;
                        notes_.push_back(clone);
                    }
                }
                
                pushUndo("Duplicate", oldNotes, notes_);
                commitNotes();
                repaint();
                return true;
            }
        }
    // Select All (Ctrl+A)
        if (ctrl && event.keyCode == NUIKeyCode::A) {
            for (auto& n : notes_) {
                if (!n.isDeleted) n.selected = true;
            }
            repaint();
            return true;
        }

        // Clear selection on Escape
        if (event.keyCode == NUIKeyCode::Escape) {
            for (auto& n : notes_) n.selected = false;
            state_ = State::None;
            repaint();
            return true;
        }

        // Arrow nudge — only when notes are selected
        {
            bool anySelected = false;
            for (const auto& n : notes_) { if (n.selected && !n.isDeleted) { anySelected = true; break; } }
            if (anySelected) {
                double snapStep = MusicTheory::getSnapDuration(snap_);
                if (snapStep <= 0.0) snapStep = 1.0;

                // Shift modifies behavior: plain = move, Shift = resize
                bool shift = (event.modifiers & NUIModifiers::Shift);

                if (event.keyCode == NUIKeyCode::Left) {
                    auto oldNotes = notes_;
                    if (shift) {
                        // Shrink: reduce duration of selected notes
                        for (auto& n : notes_) {
                            if (n.selected && !n.isDeleted) {
                                n.durationBeats = std::max(0.125, n.durationBeats - snapStep);
                            }
                        }
                    } else {
                        // Nudge left
                        for (auto& n : notes_) {
                            if (n.selected && !n.isDeleted) {
                                n.startBeat = std::max(0.0, n.startBeat - snapStep);
                            }
                        }
                    }
                    pushUndo(shift ? "Resize Left" : "Nudge Left", oldNotes, notes_);
                    commitNotes();
                    repaint();
                    return true;
                }
                else if (event.keyCode == NUIKeyCode::Right) {
                    auto oldNotes = notes_;
                    if (shift) {
                        // Extend: increase duration of selected notes
                        for (auto& n : notes_) {
                            if (n.selected && !n.isDeleted) {
                                n.durationBeats += snapStep;
                            }
                        }
                    } else {
                        // Nudge right
                        for (auto& n : notes_) {
                            if (n.selected && !n.isDeleted) {
                                n.startBeat += snapStep;
                            }
                        }
                    }
                    pushUndo(shift ? "Resize Right" : "Nudge Right", oldNotes, notes_);
                    commitNotes();
                    repaint();
                    return true;
                }
                else if (event.keyCode == NUIKeyCode::Up) {
                    auto oldNotes = notes_;
                    for (auto& n : notes_) {
                        if (n.selected && !n.isDeleted) {
                            if (shift) {
                                n.pitch = std::min(127, n.pitch + 12); // whole octave
                            } else if (snapToScale_ && scaleType_ != ScaleType::Chromatic) {
                                n.pitch = MusicTheory::nextPitchInScale(n.pitch, rootKey_, scaleType_);
                            } else {
                                n.pitch = std::min(127, n.pitch + 1);
                            }
                        }
                    }
                    pushUndo(shift ? "Octave Up" : "Transpose Up", oldNotes, notes_);
                    commitNotes();
                    repaint();
                    return true;
                }
                else if (event.keyCode == NUIKeyCode::Down) {
                    auto oldNotes = notes_;
                    for (auto& n : notes_) {
                        if (n.selected && !n.isDeleted) {
                            if (shift) {
                                n.pitch = std::max(0, n.pitch - 12); // whole octave
                            } else if (snapToScale_ && scaleType_ != ScaleType::Chromatic) {
                                n.pitch = MusicTheory::previousPitchInScale(n.pitch, rootKey_, scaleType_);
                            } else {
                                n.pitch = std::max(0, n.pitch - 1);
                            }
                        }
                    }
                    pushUndo(shift ? "Octave Down" : "Transpose Down", oldNotes, notes_);
                    commitNotes();
                    repaint();
                    return true;
                }
            }
        }
    }
    return false;
}

void PianoRollNoteLayer::setTool(PianoRollTool tool) {
    tool_ = tool;
    // Reset interaction state if needed?
    state_ = State::None;
    repaint();
}

void PianoRollNoteLayer::pushUndo(const std::string& desc, const std::vector<MidiNote>& oldN, const std::vector<MidiNote>& newN) {
    PianoRollCommand cmd;
    cmd.description = desc;
    cmd.notesBefore = oldN;
    cmd.notesAfter = newN;
    undoStack_.push_back(cmd);
    redoStack_.clear();

    // Enforce Limits (Count & Memory)
    // 1. Hard count limit
    if (undoStack_.size() > 50) {
        undoStack_.erase(undoStack_.begin());
    }

    // 2. Memory Cap (100MB) - "Cockroach Chrysalis"
    // Calculate total size and evict from front (LRU)
    size_t totalBytes = 0;
    const size_t kMaxBytes = 100 * 1024 * 1024; // 100MB

    // Reverse iterate to count from newest (keep these)
    // Actually simpler to just calc total and pop front.
    for (const auto& c : undoStack_) {
        totalBytes += c.description.capacity();
        totalBytes += c.notesBefore.capacity() * sizeof(MidiNote);
        totalBytes += c.notesAfter.capacity() * sizeof(MidiNote);
    }

    while (totalBytes > kMaxBytes && !undoStack_.empty()) {
        const auto& c = undoStack_.front();
        size_t cmdSize = c.description.capacity() + 
                         c.notesBefore.capacity() * sizeof(MidiNote) + 
                         c.notesAfter.capacity() * sizeof(MidiNote);
        
        if (totalBytes >= cmdSize) totalBytes -= cmdSize; 
        else totalBytes = 0;

        undoStack_.erase(undoStack_.begin());
    }
}

void PianoRollNoteLayer::undo() {
    if (undoStack_.empty()) return;
    auto cmd = undoStack_.back();
    undoStack_.pop_back();
    redoStack_.push_back(cmd);
    
    notes_ = cmd.notesBefore;
    commitNotes();
    repaint();
}

void PianoRollNoteLayer::redo() {
    if (redoStack_.empty()) return;
    auto cmd = redoStack_.back();
    redoStack_.pop_back();
    undoStack_.push_back(cmd);
    
    notes_ = cmd.notesAfter;
    commitNotes();
    repaint();
}

void PianoRollNoteLayer::commitNotes() {
    // Sort logic to ensure efficient culling
    std::sort(notes_.begin(), notes_.end(), [](const MidiNote& a, const MidiNote& b) {
        return a.startBeat < b.startBeat;
    });

    if (onNotesChanged_) {
        onNotesChanged_(notes_);
    }
}

void PianoRollNoteLayer::setNotes(const std::vector<MidiNote>& notes) {
    notes_ = notes;
    // Ensure sorted as well
    std::sort(notes_.begin(), notes_.end(), [](const MidiNote& a, const MidiNote& b) {
        return a.startBeat < b.startBeat;
    });
    repaint();
}

void PianoRollNoteLayer::setGhostPatterns(const std::vector<GhostPattern>& ghosts) {
    ghostPatterns_ = ghosts;
    repaint();
}

void PianoRollNoteLayer::setPixelsPerBeat(float ppb) { pixelsPerBeat_ = std::max(10.0f, ppb); repaint(); }
void PianoRollNoteLayer::setKeyHeight(float height) { keyHeight_ = std::max(8.0f, height); repaint(); }
void PianoRollNoteLayer::setScrollOffsetX(float offset) { scrollX_ = offset; repaint(); }
void PianoRollNoteLayer::setScrollOffsetY(float offset) { scrollY_ = offset; repaint(); }

void PianoRollNoteLayer::updateEdgeScrolling(float mouseX, float mouseY, const NUIRect& bounds, std::function<void()> syncCallback) {
    if (state_ == State::None) {
        isEdgeScrolling_ = false;
        edgeScrollDir_ = {0.0f, 0.0f};
        return;
    }

    float edgeL = mouseX - bounds.x;
    float edgeR = (bounds.x + bounds.width) - mouseX;
    float edgeT = mouseY - bounds.y;
    float edgeB = (bounds.y + bounds.height) - mouseY;

    float speedX = 0.0f;
    float speedY = 0.0f;

    if (edgeL < kEdgeThreshold) {
        const float t = std::clamp(1.0f - (edgeL / kEdgeThreshold), 0.0f, 1.0f);
        speedX = -kMaxScrollSpeed * t;
    } else if (edgeR < kEdgeThreshold) {
        const float t = std::clamp(1.0f - (edgeR / kEdgeThreshold), 0.0f, 1.0f);
        speedX = kMaxScrollSpeed * t;
    }

    if (edgeT < kEdgeThreshold) {
        const float t = std::clamp(1.0f - (edgeT / kEdgeThreshold), 0.0f, 1.0f);
        speedY = -kMaxScrollSpeed * t;
    } else if (edgeB < kEdgeThreshold) {
        const float t = std::clamp(1.0f - (edgeB / kEdgeThreshold), 0.0f, 1.0f);
        speedY = kMaxScrollSpeed * t;
    }

    if (std::abs(speedX) > 0.1f || std::abs(speedY) > 0.1f) {
        isEdgeScrolling_ = true;
        edgeScrollDir_ = {speedX, speedY};

        float totalH = 128.0f * keyHeight_;
        float maxScrollY = std::max(0.0f, totalH - bounds.height);
        float totalW = static_cast<float>(std::max(4.0, totalDurationBeats_)) * pixelsPerBeat_;
        float maxScrollX = std::max(0.0f, totalW - bounds.width);

        scrollX_ = safeClampScroll(scrollX_ + speedX, maxScrollX);
        scrollY_ = safeClampScroll(scrollY_ + speedY, maxScrollY);

        if (auto* view = dynamic_cast<PianoRollView*>(getParent())) {
            view->applyEdgeAutoScroll(scrollX_, scrollY_);
        }

        repaint();
        
        if (syncCallback) {
            syncCallback();
        }
    } else {
        isEdgeScrolling_ = false;
        edgeScrollDir_ = {0.0f, 0.0f};
    }
}
void PianoRollNoteLayer::setOnNotesChanged(std::function<void(const std::vector<MidiNote>&)> cb) { onNotesChanged_ = cb; }

void PianoRollNoteLayer::setPlatformBridge(NUIPlatformBridge* bridge) { platformBridge_ = bridge; }

} // namespace AestraUI
