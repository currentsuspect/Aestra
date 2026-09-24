// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

namespace Aestra {
namespace Audio {

/**
 * @brief What the transport is currently rendering, as opposed to what the user is looking at.
 *
 * View focus (Arsenal / Timeline / Audition / RoutingMap) is UI intent; this is the playback side of it.
 * ClipPreview is the one that has no focus of its own: a pattern plays in pattern mode while the Timeline
 * stays in focus.
 */
enum class PlaybackContext : std::uint8_t { Timeline, Arsenal, ClipPreview, Audition };

/**
 * @brief The four playback-mode mirrors, read back from where they actually live.
 */
struct PlaybackMirrors {
    bool enginePattern = false;       ///< AudioEngine pattern flag. While set, every timeline clip is muted.
    bool trackManagerPattern = false; ///< TrackManager pattern flag. Gates timeline scheduling in play().
    bool loopOverride = false;        ///< TrackManager pattern loop override: the loop recording clamps into.
    bool uiPattern = false;           ///< Timeline UI pattern mode (playhead hidden, follow frozen).
    bool audition = false;            ///< AudioEngine exclusive audition mode.

    bool anyPatternMirror() const { return enginePattern || trackManagerPattern || loopOverride || uiPattern; }
    bool operator==(const PlaybackMirrors& o) const {
        return enginePattern == o.enginePattern && trackManagerPattern == o.trackManagerPattern &&
               loopOverride == o.loopOverride && uiPattern == o.uiPattern && audition == o.audition;
    }
    bool operator!=(const PlaybackMirrors& o) const { return !(*this == o); }
};

/**
 * @brief The single writer of the playback-mode mirrors (Playback Context Authority, PR 2).
 *
 * Before this, "which mode are we in" was written from 15 call sites across AestraContent,
 * PianoRollPanel and MuseService. Each wrote its own subset of the mirrors, and nothing forced them to
 * agree. A stale mirror is heard as a transport rolling in silence, and the [ModeProbe] caught one live.
 *
 * PR 2 is deliberately BEHAVIOUR-PRESERVING: each method reproduces the exact sequence its caller used
 * to run inline, including the combinations that look odd:
 *
 *   | context                 | engine | TM pattern | override | UI | audition |
 *   |-------------------------|--------|------------|----------|----|----------|
 *   | Timeline                |   0    |     0      |    0     | 0  |    0     |
 *   | Arsenal, entered only   |   1    |     0      |    0 (*) | 1  |    0     |
 *   | Arsenal, after play     |   1    |     1      |    1     | 1  |    0     |
 *   | ClipPreview             |   0    |     1      |    0     | 1  |    0     |
 *   | Audition                |   0    |     0      |    0     | 0  |    1     |
 *
 *   (*) A pattern-length change while in Arsenal sets the override before any play (applyArsenalLoopLength).
 *
 * Making these rows consistent changes when scheduling happens. Each change gets its own PR with its own
 * evidence. The one intended change here is noted on resizeArsenalLoop(). Moving the context onto the audio
 * command queue is PR 3 (done: every engine-side change is one SetPlaybackContext request, pushed before any
 * transport command of the same transition). The transport sequencing around these calls (panic, the position save and restore,
 * the hot-swap restart) stays with the caller for now.
 *
 * Thread model: main thread only, like every call site it replaces.
 */
class PlaybackContextController {
public:
    explicit PlaybackContextController(TrackManager& trackManager) : m_trackManager(trackManager) {}

    void setEngine(AudioEngine* engine) { m_engine = engine; }
    /** The timeline UI's pattern-mode setter. The UI mirror has no other writer. */
    void setUiPatternMirror(std::function<void(bool)> mirror) { m_uiMirror = std::move(mirror); }

    PlaybackContext context() const { return m_context; }

    /**
     * @brief True once the audio thread has applied the last context this controller requested.
     *
     * Between a request and the next audio block the engine still reports the previous context.
     * That is the queue doing its job (PR 3), not drift, so the [ModeProbe] holds its verdict until
     * this is true.
     */
    bool settled() const {
        return !m_engine || m_engine->getAppliedContextGeneration() >= m_engine->getRequestedContextGeneration();
    }

    /**
     * Read every mirror from where it actually lives (the UI mirror from the last value pushed).
     * The engine mirrors are what the audio thread has APPLIED; see settled().
     */
    PlaybackMirrors observe() const {
        PlaybackMirrors m;
        m.enginePattern = m_engine && m_engine->isPatternPlaybackMode();
        m.trackManagerPattern = m_trackManager.isPatternMode();
        m.loopOverride = m_trackManager.isPatternLoopOverrideActive();
        m.uiPattern = m_uiPattern;
        m.audition = m_engine && m_engine->isAuditionModeEnabled();
        return m;
    }

    // ----------------------------------------------------------------- transitions --

    /** First engine attachment: Timeline is the default focus. Engine side only, as before. */
    void initializeTimeline() {
        publishEngineContext(false, kTimelineLengthBeats, false);
        m_context = PlaybackContext::Timeline;
    }

    /** Focus entered Arsenal. The engine arms the pattern loop. TrackManager joins on play (see the table). */
    void enterArsenal(double patternLengthBeats) {
        publishEngineContext(true, m_trackManager.arsenalLoopLengthBeats(m_arsenalPattern, patternLengthBeats), false);
        setUi(true);
        m_context = PlaybackContext::Arsenal;
    }

    /** Play pressed in Arsenal. Resumes from the cued position unless startSeconds >= 0. */
    void startArsenalPlayback(PatternID pattern, double patternLengthBeats, double startSeconds = -1.0) {
        // Published before play() pushes its transport command, so both land in one drain.
        // A piano-roll zone on this pattern shortens the loop to the zone (TrackManager::arsenalLoopLengthBeats).
        m_arsenalPattern = pattern;
        const double loopBeats = m_trackManager.arsenalLoopLengthBeats(pattern, patternLengthBeats);
        publishEngineContext(true, loopBeats, m_engineAudition);
        m_trackManager.setPatternLoopOverride(0.0, loopBeats, true);
        m_trackManager.playPatternInArsenal(pattern, startSeconds, /*useLoopZone=*/true);
        m_context = PlaybackContext::Arsenal;
    }

    /**
     * The active pattern's length changed while in Arsenal: the engine loop and the recording loop follow it.
     * A no-op outside Arsenal. Arming the engine anywhere else seized the transport and muted the timeline.
     */
    void applyArsenalLoopLength(double patternLengthBeats) {
        if (m_context != PlaybackContext::Arsenal) {
            return;
        }
        const double loopBeats = m_trackManager.arsenalLoopLengthBeats(m_arsenalPattern, patternLengthBeats);
        publishEngineContext(true, loopBeats, m_engineAudition);
        m_trackManager.setPatternLoopOverride(0.0, loopBeats, true);
    }

    /**
     * The engine loop only: the sequencer re-requesting activation, and piano-roll length edits during
     * pattern playback.
     *
     * The ONE intended behaviour change in PR 2. The piano roll used to decide by TrackManager's pattern
     * flag, which is also set during a ClipPreview, where the Timeline is in focus. A length edit there
     * put the engine into pattern mode and muted the whole timeline, the same seize that
     * updatePatternLoopLength's focus guard exists to prevent. Deciding by context closes that.
     */
    void resizeArsenalLoop(double patternLengthBeats) {
        if (m_context != PlaybackContext::Arsenal) {
            return;
        }
        publishEngineContext(true, m_trackManager.arsenalLoopLengthBeats(m_arsenalPattern, patternLengthBeats),
                             m_engineAudition);
    }

    /**
     * @brief A piano-roll ruler zone: pattern playback loops only that range (SPEC 3 §5.2).
     *
     * Kept whatever the focus, and only for @p pattern; it shapes playback only in Arsenal. There,
     * the loop length is republished at once, and if the pattern is playing with a different range
     * it restarts at the loop's top so the new range is heard immediately. Clearing the zone
     * returns to the whole-pattern loop.
     * @param pattern The pattern the piano roll is editing. It becomes the Arsenal pattern only
     *        when a zone is set on it while no other pattern is playing.
     * @param zone The range; its pattern is set to @p pattern. nullopt clears the zone.
     */
    void setArsenalLoopZone(PatternID pattern, std::optional<TrackManager::ArsenalLoopZone> zone,
                            double patternLengthBeats) {
        if (zone) {
            zone->pattern = pattern;
        }
        m_trackManager.setArsenalLoopZone(zone);
        if (!pattern.isValid()) {
            return;
        }
        if (pattern == m_arsenalPattern) {
            reapplyArsenalLoop(patternLengthBeats);
            return;
        }
        // Another pattern is (or was last) Arsenal's. A zone drawn on this one becomes Arsenal's
        // only when nothing is playing: never reschedule, or publish this pattern's loop length
        // over, a different pattern that is playing. Clearing another pattern's zone changes
        // nothing that is playing, so it never takes the Arsenal pattern over either (#970).
        const bool otherPlaying = m_trackManager.isPlaying() && m_trackManager.isPatternMode() &&
                                  m_arsenalPattern.isValid();
        if (zone && !otherPlaying) {
            m_arsenalPattern = pattern;
            reapplyArsenalLoop(patternLengthBeats);
        }
    }

    /**
     * @brief Arm or disarm recording (TrackManager::record) and keep the Arsenal loop consistent.
     *
     * An armed record ignores the loop zone (TrackManager::activeArsenalLoopZone). So in Arsenal
     * the loop is re-applied at once, not left at the zone's length until the next play, and a
     * pattern already playing is rescheduled at the same pattern position (no jump). Arming
     * reschedules BEFORE record(): a capture session takes its start beat from the transport at
     * arm time, and that must already be the whole-pattern transport, not the zone-relative one.
     */
    void setRecordArmed(bool armed, double patternLengthBeats) {
        if (armed == m_trackManager.isRecordArmed()) {
            return;
        }
        if (armed) {
            reapplyArsenalLoop(patternLengthBeats, /*keepPatternPosition=*/true, /*zoneAllowed=*/false);
            m_trackManager.record();
        } else {
            m_trackManager.record();
            reapplyArsenalLoop(patternLengthBeats, /*keepPatternPosition=*/true);
        }
    }

    /** Timeline play: the pattern loop override ends, so the timeline's own loop applies again. */
    void playTimeline() {
        m_trackManager.setPatternLoopOverride(0.0, 0.0, false);
        m_trackManager.play();
    }

    /**
     * Leave pattern playback: stop, drop the scheduled instances, leave pattern mode. Only if TrackManager
     * is actually in pattern mode. Otherwise the transport is not ours to stop.
     * @return True if it tore anything down.
     */
    bool teardownPatternPlayback() {
        if (!m_trackManager.isPatternMode()) {
            return false;
        }
        m_trackManager.stopArsenalPlayback(false);
        return true;
    }

    /** Focus entered Timeline. Ends any pattern playback still running, then clears every pattern mirror. */
    void enterTimeline() {
        teardownPatternPlayback();
        publishEngineContext(false, kTimelineLengthBeats, false);
        // The model's mirror of the engine flag just cleared. Only timeline PLAY used to clear it, so after a
        // stopped Arsenal -> Timeline switch the effective loop stayed the pattern's, and a count-in recorded
        // from there was clamped into it (#845's clamp). The [ModeProbe] saw this live.
        m_trackManager.setPatternLoopOverride(0.0, 0.0, false);
        setUi(false);
        m_context = PlaybackContext::Timeline;
    }

    /** Focus entered Audition: the DAW transport is parked and exclusive audition takes over. */
    void enterAudition() {
        teardownPatternPlayback();
        publishEngineContext(false, kTimelineLengthBeats, true);
        m_trackManager.setPatternLoopOverride(0.0, 0.0, false);
        setUi(false);
        m_context = PlaybackContext::Audition;
    }

    /** A pattern clip previews from the Timeline: pattern playback without an Arsenal focus. */
    void startClipPreview(PatternID pattern) {
        setUi(true);
        publishEngineContext(m_enginePattern, m_engineLength, false);
        m_trackManager.preparePatternForArsenal(pattern);
        // Clip preview intentionally starts from the top. Every other play path resumes from the cued position.
        m_trackManager.playPatternInArsenal(pattern, 0.0);
        // A preview under Arsenal focus stays Arsenal: its stop path skips endClipPreview, so
        // a ClipPreview context would outlive it and gate off Arsenal loop-length updates,
        // which the inline code this replaced always applied under Arsenal focus.
        // (Not reachable from the UI today: the pattern browser only shows on Timeline focus.)
        if (m_context != PlaybackContext::Arsenal) {
            m_context = PlaybackContext::ClipPreview;
        }
    }

    /**
     * A clip preview ends while focus is NOT Arsenal. (Ending into Arsenal keeps the pattern playing, and
     * the caller skips this.)
     * @param restoreUi False when the caller is about to set the UI mirror itself (a focus change).
     * @return True if pattern playback was torn down. The caller then restores the timeline position.
     */
    bool endClipPreview(bool restoreUi) {
        const bool toreDown = teardownPatternPlayback();
        publishEngineContext(false, kTimelineLengthBeats, m_engineAudition);
        if (restoreUi) {
            setUi(false);
        }
        m_context = PlaybackContext::Timeline;
        return toreDown;
    }

    /** A focus self-transition re-asserts the UI mirror only. Engine, transport and scheduler stay untouched. */
    void refreshUiForFocus(bool arsenalFocused) { setUi(arsenalFocused); }

private:
    /**
     * In Arsenal: publish the loop length the zone implies for the Arsenal pattern (context first,
     * so a restart's transport command lands in the same drain, PR 3), and if the pattern is
     * playing with a different range than it was scheduled with, reschedule it: at the loop's top,
     * or, with @p keepPatternPosition, at the transport position that keeps the same pattern beat.
     * @p zoneAllowed false applies no zone (arming a record, before TrackManager knows it).
     */
    void reapplyArsenalLoop(double patternLengthBeats, bool keepPatternPosition = false, bool zoneAllowed = true) {
        if (m_context != PlaybackContext::Arsenal) {
            return;
        }
        const auto wanted = zoneAllowed ? m_trackManager.activeArsenalLoopZone(m_arsenalPattern)
                                        : std::optional<TrackManager::ArsenalLoopZone>{};
        const double loopBeats = wanted ? wanted->endBeat - wanted->startBeat : patternLengthBeats;
        publishEngineContext(true, loopBeats, m_engineAudition);
        m_trackManager.setPatternLoopOverride(0.0, loopBeats, true);
        if (!m_trackManager.isPlaying() || !m_trackManager.isPatternMode() || !m_arsenalPattern.isValid()) {
            return;
        }
        const auto scheduled = m_trackManager.scheduledArsenalLoopZone();
        const bool same = scheduled.has_value() == wanted.has_value() &&
                          (!scheduled || (scheduled->pattern == wanted->pattern &&
                                          scheduled->startBeat == wanted->startBeat &&
                                          scheduled->endBeat == wanted->endBeat));
        if (same) {
            return;
        }
        double restartSeconds = 0.0;
        if (keepPatternPosition) {
            const double bpm = std::max(1.0, m_trackManager.getTimelineClock().getCurrentTempo());
            const double oldStart = scheduled ? scheduled->startBeat : 0.0;
            const double oldLength =
                std::max(1e-6, scheduled ? scheduled->endBeat - scheduled->startBeat : patternLengthBeats);
            double transportBeat = std::fmod(std::max(0.0, m_trackManager.getPosition()) * bpm / 60.0, oldLength);
            const double patternBeat = transportBeat + oldStart;
            const double newStart = wanted ? wanted->startBeat : 0.0;
            transportBeat = patternBeat - newStart;
            if (transportBeat < 0.0 || transportBeat >= loopBeats) {
                transportBeat = 0.0; // outside the new loop: start at its top
            }
            restartSeconds = transportBeat * 60.0 / bpm;
        }
        m_trackManager.playPatternInArsenal(m_arsenalPattern, restartSeconds, zoneAllowed);
    }

    // The engine's neutral pattern length when pattern mode is off, as every call site passed.
    static constexpr double kTimelineLengthBeats = 4.0;

    // One request carries the whole engine-side context (pattern mode, loop length, audition),
    // so the audio thread applies it atomically in queue order (PR 3). The requested values are
    // kept here because the engine's own flags only change when the audio thread applies them.
    void publishEngineContext(bool pattern, double lengthBeats, bool audition) {
        m_enginePattern = pattern;
        m_engineLength = lengthBeats;
        m_engineAudition = audition;
        if (m_engine) {
            m_engine->requestPlaybackContext(pattern, lengthBeats, audition);
        }
    }
    void setUi(bool patternMode) {
        m_uiPattern = patternMode;
        if (m_uiMirror) {
            m_uiMirror(patternMode);
        }
    }

    TrackManager& m_trackManager;
    AudioEngine* m_engine = nullptr;
    std::function<void(bool)> m_uiMirror;
    bool m_uiPattern = false;
    bool m_enginePattern = false;
    double m_engineLength = kTimelineLengthBeats;
    bool m_engineAudition = false;
    // The pattern Arsenal plays (last started, or the piano roll's when it set a zone with nothing
    // else playing). Loop-zone lookups use it, so a zone drawn on one pattern never shapes another.
    PatternID m_arsenalPattern{};
    PlaybackContext m_context = PlaybackContext::Timeline;
};

} // namespace Audio
} // namespace Aestra
