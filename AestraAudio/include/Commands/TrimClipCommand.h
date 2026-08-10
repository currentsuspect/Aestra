// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/ClipInstance.h"
#include "Models/PlaylistModel.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Command to trim a clip's start/end positions
 *
 * Contract (#744): a left-edge trim moves the timeline start AND advances the
 * source read offset by the same musical amount so the right edge stays pinned;
 * a right-edge trim changes only the visible end/duration. Both directions must
 * go through the command history so undo, dirty state, autosave, recovery and
 * serialization agree. For audio clips, durationSeconds is kept canonical
 * (the serializer prefers it over beats) and the source-offset advance lands in
 * sourceOffsetSeconds, from which the legacy beat-domain field is re-derived on
 * load.
 */
class TrimClipCommand : public ICommand {
public:
    /**
     * @brief Lazy variant: original state is captured from the model at first
     *        execute. Used by programmatic/CLI callers that have not mutated
     *        the clip yet.
     * @param newStartBeat New start position (-1 = keep current)
     * @param newEndBeat New end position (-1 = keep current)
     */
    TrimClipCommand(PlaylistModel& model, ClipInstanceID clipId, double newStartBeat, double newEndBeat)
        : m_model(model), m_clipId(clipId), m_newStartBeat(newStartBeat), m_newEndBeat(newEndBeat) {}

    /**
     * @brief Explicit-variant for UI gestures that already applied the trim to
     *        the model live (the timeline edge drag). Original state must be
     *        captured at gesture start, not at execute — by the time the
     *        command is pushed the model no longer holds it.
     */
    TrimClipCommand(PlaylistModel& model, ClipInstanceID clipId, double originalStartBeat, double originalEndBeat,
                    double originalSourceOffsetSeconds, double originalDurationSeconds, double newStartBeat,
                    double newEndBeat)
        : m_model(model), m_clipId(clipId), m_originalStartBeat(originalStartBeat),
          m_originalDuration(originalEndBeat - originalStartBeat),
          m_originalSourceOffsetSeconds(originalSourceOffsetSeconds),
          m_originalDurationSeconds(originalDurationSeconds), m_newStartBeat(newStartBeat), m_newEndBeat(newEndBeat),
          m_originalCaptured(true) {}

    void execute() override {
        if (m_executed)
            return;

        auto* clip = m_model.getClip(m_clipId);
        if (!clip)
            return;

        if (!m_originalCaptured) {
            m_originalStartBeat = clip->startBeat;
            m_originalDuration = clip->durationBeats;
            m_originalDurationSeconds = clip->durationSeconds;
            m_originalSourceOffsetSeconds = clip->sourceOffsetSeconds;
            m_originalCaptured = true;
        }
        if (!m_wasAudioDetermined) {
            // Audio-ness cannot change mid-gesture; capture once, reuse for undo.
            m_originalWasAudio = m_model.isAudioClip(*clip);
            m_wasAudioDetermined = true;
        }

        double newStart = (m_newStartBeat < 0) ? clip->startBeat : m_newStartBeat;
        double newEnd = (m_newEndBeat < 0) ? (clip->startBeat + clip->durationBeats) : m_newEndBeat;

        // Validate: end must be after start
        if (newEnd <= newStart) {
            return; // Invalid trim range - don't apply
        }

        if (m_originalWasAudio) {
            // The source read offset must never go negative: clamp the start to
            // the source-start beat (start - offset), same formula the UI drag
            // uses, so the model can never hold an unrenderable offset.
            const double secondsPerBeat = m_model.beatToSeconds(1.0);
            float rate = clip->edits.playbackRate;
            if (!std::isfinite(rate) || rate <= 0.0f) {
                rate = 1.0f;
            }
            rate = std::clamp(rate, 0.25f, 4.0f);
            const double minStart =
                m_originalStartBeat - m_originalSourceOffsetSeconds / (secondsPerBeat * rate);
            if (newStart < minStart) {
                newStart = minStart;
            }
            if (newEnd <= newStart) {
                return; // Clamped start swallowed the trim range - don't apply
            }

            clip->durationSeconds = m_model.beatToSeconds(newEnd - newStart);
            if (newStart != m_originalStartBeat) {
                // Left-edge trim: advance the source read position by the same
                // musical amount (scaled by playback rate so the right edge
                // stays pinned under varispeed). Right-edge trims leave the
                // source offset untouched.
                clip->sourceOffsetSeconds =
                    m_originalSourceOffsetSeconds + (newStart - m_originalStartBeat) * secondsPerBeat * rate;
            }
        }

        clip->startBeat = newStart;
        clip->durationBeats = newEnd - newStart;

        m_executed = true;
    }

    void undo() override {
        if (!m_executed)
            return;

        auto* clip = m_model.getClip(m_clipId);
        if (!clip)
            return;

        clip->startBeat = m_originalStartBeat;
        clip->durationBeats = m_originalDuration;
        if (m_originalWasAudio) {
            clip->durationSeconds = m_originalDurationSeconds;
            clip->sourceOffsetSeconds = m_originalSourceOffsetSeconds;
        }

        m_executed = false;
    }

    void redo() override { execute(); }

    std::string getName() const override { return "Trim Clip"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    PlaylistModel& m_model;
    ClipInstanceID m_clipId;
    double m_newStartBeat;
    double m_newEndBeat;

    // Original state for undo
    double m_originalStartBeat = 0.0;
    double m_originalDuration = 0.0;
    double m_originalDurationSeconds = 0.0;
    double m_originalSourceOffsetSeconds = 0.0;
    bool m_originalWasAudio = false;
    bool m_originalCaptured = false;
    bool m_wasAudioDetermined = false;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
