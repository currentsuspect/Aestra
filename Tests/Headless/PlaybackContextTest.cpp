// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Playback Context Authority, PR 2: PlaybackContextController is the single writer of
// the playback-mode mirrors (engine pattern flag, TrackManager pattern flag, pattern loop
// override, timeline UI pattern mode, audition mode).
//
// Two kinds of assertion:
//  1. The documented table: each transition leaves the mirrors exactly as the inline
//     code it replaced did, odd rows included. PR 2 is behaviour-preserving, so these
//     pin today's behaviour, not an ideal one.
//  2. The invariant that makes the controller worth having: entering Timeline or
//     Audition clears EVERY pattern mirror from EVERY prior state. The [ModeProbe]
//     caught a stopped Arsenal -> Timeline switch leaving the loop override set. That
//     leak is one of the prior states below.

#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"
#include "Playback/PlaybackContextController.h"

#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cout << "[FAIL] " << message << '\n';
        ++g_failures;
    }
}

std::string describe(const PlaybackMirrors& m) {
    return "{engine=" + std::to_string(m.enginePattern) + " tm=" + std::to_string(m.trackManagerPattern) +
           " override=" + std::to_string(m.loopOverride) + " ui=" + std::to_string(m.uiPattern) +
           " audition=" + std::to_string(m.audition) + "}";
}

PlaybackMirrors mirrors(bool engine, bool tm, bool override, bool ui, bool audition) {
    PlaybackMirrors m;
    m.enginePattern = engine;
    m.trackManagerPattern = tm;
    m.loopOverride = override;
    m.uiPattern = ui;
    m.audition = audition;
    return m;
}

struct Fixture {
    TrackManager tm;
    AudioEngine engine;
    PlaybackContextController ctx{tm};
    PatternID pattern;
    bool uiMirror = false;
    int uiWrites = 0;

    Fixture() {
        tm.setOutputSampleRate(48000.0);
        tm.setCommandSink([](const AudioQueueCommand&) { return true; });
        auto& patterns = tm.getPatternManager();
        pattern = patterns.createPattern();
        if (auto* source = patterns.getPattern(pattern)) {
            source->type = PatternSource::Type::Midi;
            source->lengthBeats = 8.0;
            source->payload = MidiPayload{};
        }
        ctx.setEngine(&engine);
        ctx.setUiPatternMirror([this](bool on) {
            uiMirror = on;
            ++uiWrites;
        });
        ctx.initializeTimeline();
    }

    // The UI mirror has exactly one writer now, so what the controller reports and what
    // the UI was actually told must never differ.
    void expect(const PlaybackMirrors& want, const std::string& step) {
        const PlaybackMirrors got = ctx.observe();
        check(got == want, step + ": expected " + describe(want) + ", got " + describe(got));
        check(uiMirror == got.uiPattern, step + ": the UI was told something the controller does not report");
    }
};

// ------------------------------------------------------------------ the table --

void testDocumentedTable() {
    Fixture f;
    check(f.pattern.isValid(), "fixture pattern exists");
    f.expect(mirrors(false, false, false, false, false), "Timeline at first engine attachment");
    check(f.ctx.context() == PlaybackContext::Timeline, "context starts as Timeline");

    f.ctx.enterArsenal(8.0);
    // The half-switch, preserved on purpose: TrackManager and the override join on play.
    f.expect(mirrors(true, false, false, true, false), "Arsenal, entered only");
    check(f.ctx.context() == PlaybackContext::Arsenal, "context is Arsenal after entering it");

    f.ctx.startArsenalPlayback(f.pattern, 8.0);
    f.expect(mirrors(true, true, true, true, false), "Arsenal, after play");
    check(f.tm.isPlaying(), "Arsenal play actually started the transport");

    f.ctx.enterTimeline();
    f.expect(mirrors(false, false, false, false, false), "Timeline after leaving Arsenal playback");
    check(!f.tm.isPlaying(), "leaving Arsenal playback stopped the transport");

    f.ctx.startClipPreview(f.pattern);
    // The other odd row: pattern playback with the engine left in timeline mode.
    f.expect(mirrors(false, true, false, true, false), "ClipPreview");
    check(f.ctx.context() == PlaybackContext::ClipPreview, "context is ClipPreview");

    check(f.ctx.endClipPreview(true), "ending a playing preview reports a teardown");
    f.expect(mirrors(false, false, false, false, false), "Timeline after the preview ends");

    f.ctx.enterAudition();
    f.expect(mirrors(false, false, false, false, true), "Audition");
    check(f.ctx.context() == PlaybackContext::Audition, "context is Audition");
}

// ------------------------------------------------------------- the invariant --

struct PriorState {
    const char* name;
    std::function<void(Fixture&)> reach;
};

std::vector<PriorState> priorStates() {
    return {
        {"Timeline", [](Fixture&) {}},
        {"Arsenal entered", [](Fixture& f) { f.ctx.enterArsenal(8.0); }},
        {"Arsenal playing", [](Fixture& f) {
             f.ctx.enterArsenal(8.0);
             f.ctx.startArsenalPlayback(f.pattern, 8.0);
         }},
        // stopArsenalPlayback(true) keeps pattern mode: a stop pressed inside Arsenal.
        {"Arsenal stopped after play", [](Fixture& f) {
             f.ctx.enterArsenal(8.0);
             f.ctx.startArsenalPlayback(f.pattern, 8.0);
             f.tm.stopArsenalPlayback(true);
         }},
        // The live leak: a pattern edit in Arsenal sets the override with nothing playing.
        {"Arsenal entered, pattern length edited", [](Fixture& f) {
             f.ctx.enterArsenal(8.0);
             f.ctx.applyArsenalLoopLength(16.0);
         }},
        {"ClipPreview", [](Fixture& f) { f.ctx.startClipPreview(f.pattern); }},
        {"Audition", [](Fixture& f) { f.ctx.enterAudition(); }},
    };
}

void testEnteringTimelineOrAuditionClearsEveryPatternMirror() {
    for (const auto& prior : priorStates()) {
        {
            Fixture f;
            prior.reach(f);
            f.ctx.enterTimeline();
            f.expect(mirrors(false, false, false, false, false), std::string(prior.name) + " -> Timeline");
            check(f.ctx.context() == PlaybackContext::Timeline, std::string(prior.name) + " -> Timeline context");
            check(!f.tm.isPatternMode() || !f.tm.isPlaying(),
                  std::string(prior.name) + " -> Timeline: no pattern playback survives");
        }
        {
            Fixture f;
            prior.reach(f);
            f.ctx.enterAudition();
            f.expect(mirrors(false, false, false, false, true), std::string(prior.name) + " -> Audition");
            check(!f.tm.isPlaying(), std::string(prior.name) + " -> Audition: the DAW transport is parked");
        }
    }
}

// -------------------------------------------------------- loop-length paths --

void testLoopLengthOnlyActsInArsenal() {
    {
        Fixture f;
        f.ctx.enterArsenal(8.0);
        f.ctx.applyArsenalLoopLength(16.0);
        f.expect(mirrors(true, false, true, true, false), "a length edit in Arsenal arms the override before play");
        check(f.engine.getPatternLengthBeats() == 16.0, "the engine loop follows the edited length");

        f.ctx.resizeArsenalLoop(4.0);
        check(f.engine.getPatternLengthBeats() == 4.0, "resizeArsenalLoop updates the engine loop in Arsenal");
    }
    {
        // The one intended change: a piano-roll length edit during a timeline clip preview used to arm
        // the engine's pattern mode (TrackManager's flag is set there), muting the whole timeline.
        Fixture f;
        f.ctx.startClipPreview(f.pattern);
        f.ctx.resizeArsenalLoop(16.0);
        f.ctx.applyArsenalLoopLength(16.0);
        check(!f.engine.isPatternPlaybackMode(), "no loop-length path can seize the engine during a clip preview");
        check(!f.tm.isPatternLoopOverrideActive(), "nor arm the pattern loop override there");
    }
    {
        Fixture f;
        f.ctx.resizeArsenalLoop(16.0);
        f.ctx.applyArsenalLoopLength(16.0);
        f.expect(mirrors(false, false, false, false, false), "loop-length paths are inert on the Timeline");
    }
}

// ---------------------------------------------------------------- teardown --

void testTeardownOnlyStopsPatternPlayback() {
    Fixture f;
    f.ctx.playTimeline();
    check(f.tm.isPlaying(), "timeline play started the transport");
    check(!f.ctx.teardownPatternPlayback(), "teardown reports nothing to do during timeline playback");
    check(f.tm.isPlaying(), "and leaves timeline playback running: that transport is not the pattern's to stop");

    f.ctx.enterArsenal(8.0);
    f.ctx.startArsenalPlayback(f.pattern, 8.0);
    check(f.ctx.teardownPatternPlayback(), "teardown reports it stopped pattern playback");
    check(!f.tm.isPatternMode() && !f.tm.isPlaying(), "and pattern playback is really gone");
}

void testTimelinePlayEndsTheOverride() {
    Fixture f;
    f.ctx.enterArsenal(8.0);
    f.ctx.applyArsenalLoopLength(8.0);
    check(f.tm.isPatternLoopOverrideActive(), "precondition: the override is set");
    f.ctx.playTimeline();
    check(!f.tm.isPatternLoopOverrideActive(), "timeline play ends the pattern loop override");
}

} // namespace

int main() {
    testDocumentedTable();
    testEnteringTimelineOrAuditionClearsEveryPatternMirror();
    testLoopLengthOnlyActsInArsenal();
    testTeardownOnlyStopsPatternPlayback();
    testTimelinePlayEndsTheOverride();

    if (g_failures == 0) {
        std::cout << "Playback context tests passed\n";
        return 0;
    }
    std::cout << g_failures << " playback context test(s) failed\n";
    return 1;
}
