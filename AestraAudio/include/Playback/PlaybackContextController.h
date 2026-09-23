// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"

#include <cstdint>
#include <functional>
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
 * command queue is PR 3. The transport sequencing around these calls (panic, the position save and restore,
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

    /** Read every mirror from where it actually lives (the UI mirror from the last value pushed). */
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
        setEnginePattern(false, kTimelineLengthBeats);
        setAudition(false);
        m_context = PlaybackContext::Timeline;
    }

    /** Focus entered Arsenal. The engine arms the pattern loop. TrackManager joins on play (see the table). */
    void enterArsenal(double patternLengthBeats) {
        setEnginePattern(true, patternLengthBeats);
        setAudition(false);
        setUi(true);
        m_context = PlaybackContext::Arsenal;
    }

    /** Play pressed in Arsenal. Resumes from the cued position unless startSeconds >= 0. */
    void startArsenalPlayback(PatternID pattern, double patternLengthBeats, double startSeconds = -1.0) {
        setEnginePattern(true, patternLengthBeats);
        m_trackManager.setPatternLoopOverride(0.0, patternLengthBeats, true);
        m_trackManager.playPatternInArsenal(pattern, startSeconds);
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
        setEnginePattern(true, patternLengthBeats);
        m_trackManager.setPatternLoopOverride(0.0, patternLengthBeats, true);
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
        setEnginePattern(true, patternLengthBeats);
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
        setEnginePattern(false, kTimelineLengthBeats);
        // The model's mirror of the engine flag just cleared. Only timeline PLAY used to clear it, so after a
        // stopped Arsenal -> Timeline switch the effective loop stayed the pattern's, and a count-in recorded
        // from there was clamped into it (#845's clamp). The [ModeProbe] saw this live.
        m_trackManager.setPatternLoopOverride(0.0, 0.0, false);
        setAudition(false);
        setUi(false);
        m_context = PlaybackContext::Timeline;
    }

    /** Focus entered Audition: the DAW transport is parked and exclusive audition takes over. */
    void enterAudition() {
        teardownPatternPlayback();
        setEnginePattern(false, kTimelineLengthBeats);
        m_trackManager.setPatternLoopOverride(0.0, 0.0, false);
        setUi(false);
        setAudition(true);
        m_context = PlaybackContext::Audition;
    }

    /** A pattern clip previews from the Timeline: pattern playback without an Arsenal focus. */
    void startClipPreview(PatternID pattern) {
        setUi(true);
        setAudition(false);
        m_trackManager.preparePatternForArsenal(pattern);
        // Clip preview intentionally starts from the top. Every other play path resumes from the cued position.
        m_trackManager.playPatternInArsenal(pattern, 0.0);
        m_context = PlaybackContext::ClipPreview;
    }

    /**
     * A clip preview ends while focus is NOT Arsenal. (Ending into Arsenal keeps the pattern playing, and
     * the caller skips this.)
     * @param restoreUi False when the caller is about to set the UI mirror itself (a focus change).
     * @return True if pattern playback was torn down. The caller then restores the timeline position.
     */
    bool endClipPreview(bool restoreUi) {
        const bool toreDown = teardownPatternPlayback();
        setEnginePattern(false, kTimelineLengthBeats);
        if (restoreUi) {
            setUi(false);
        }
        m_context = PlaybackContext::Timeline;
        return toreDown;
    }

    /** A focus self-transition re-asserts the UI mirror only. Engine, transport and scheduler stay untouched. */
    void refreshUiForFocus(bool arsenalFocused) { setUi(arsenalFocused); }

private:
    // The engine's neutral pattern length when pattern mode is off, as every call site passed.
    static constexpr double kTimelineLengthBeats = 4.0;

    void setEnginePattern(bool enabled, double lengthBeats) {
        if (m_engine) {
            m_engine->setPatternPlaybackMode(enabled, lengthBeats);
        }
    }
    void setAudition(bool enabled) {
        if (m_engine) {
            m_engine->setAuditionModeEnabled(enabled);
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
    PlaybackContext m_context = PlaybackContext::Timeline;
};

} // namespace Audio
} // namespace Aestra
