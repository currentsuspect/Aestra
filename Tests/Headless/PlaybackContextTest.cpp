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

#include "Core/AudioCommandQueue.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"
#include "Playback/PlaybackContextController.h"

#include <functional>
#include <iostream>
#include <memory>
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

    std::vector<float> block = std::vector<float>(512 * 2, 0.0f);

    Fixture() {
        // PR 3: engine-side context changes apply on the audio thread, in queue order, so the
        // engine must actually process blocks for the mirrors to move.
        engine.setSampleRate(48000);
        engine.setBufferConfig(512, 2);
        engine.setMetronomeEnabled(false);
        engine.initialize();
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

    /** One audio block: drains the command queue, applying every requested context. */
    void settle() {
        engine.processBlock(block.data(), nullptr, 512, 0.0);
        check(ctx.settled(), "one block applies every requested context");
    }

    // The UI mirror has exactly one writer now, so what the controller reports and what
    // the UI was actually told must never differ.
    void expect(const PlaybackMirrors& want, const std::string& step) {
        settle();
        const PlaybackMirrors got = ctx.observe();
        check(got == want, step + ": expected " + describe(want) + ", got " + describe(got));
        check(uiMirror == got.uiPattern, step + ": the UI was told something the controller does not report");
    }
};

// A Fixture holds a TrackManager and an AudioEngine BY VALUE, about 324 KB. Several
// on one stack frame overflowed MSVC's 1 MB default stack (a SegFault on the Windows
// lane only; Linux's 8 MB stack hid it). They live on the heap.
std::unique_ptr<Fixture> makeFixture() { return std::make_unique<Fixture>(); }

// ------------------------------------------------------------------ the table --

void testDocumentedTable() {
    auto owned = makeFixture();
    Fixture& f = *owned;
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
            auto owned = makeFixture();
        Fixture& f = *owned;
            prior.reach(f);
            f.ctx.enterTimeline();
            f.expect(mirrors(false, false, false, false, false), std::string(prior.name) + " -> Timeline");
            check(f.ctx.context() == PlaybackContext::Timeline, std::string(prior.name) + " -> Timeline context");
            check(!f.tm.isPatternMode() || !f.tm.isPlaying(),
                  std::string(prior.name) + " -> Timeline: no pattern playback survives");
        }
        {
            auto owned = makeFixture();
        Fixture& f = *owned;
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
        auto owned = makeFixture();
        Fixture& f = *owned;
        f.ctx.enterArsenal(8.0);
        f.ctx.applyArsenalLoopLength(16.0);
        f.expect(mirrors(true, false, true, true, false), "a length edit in Arsenal arms the override before play");
        f.settle();
        check(f.engine.getPatternLengthBeats() == 16.0, "the engine loop follows the edited length");

        f.ctx.resizeArsenalLoop(4.0);
        f.settle();
        check(f.engine.getPatternLengthBeats() == 4.0, "resizeArsenalLoop updates the engine loop in Arsenal");
    }
    {
        // The one intended change: a piano-roll length edit during a timeline clip preview used to arm
        // the engine's pattern mode (TrackManager's flag is set there), muting the whole timeline.
        auto owned = makeFixture();
        Fixture& f = *owned;
        f.ctx.startClipPreview(f.pattern);
        f.ctx.resizeArsenalLoop(16.0);
        f.ctx.applyArsenalLoopLength(16.0);
        f.settle(); // without this the engine read below would pass vacuously on the old context
        check(!f.engine.isPatternPlaybackMode(), "no loop-length path can seize the engine during a clip preview");
        check(!f.tm.isPatternLoopOverrideActive(), "nor arm the pattern loop override there");
    }
    {
        // A preview started under Arsenal focus must not strip the Arsenal context: loop-length
        // edits there still have to reach the engine (review finding on #954).
        auto owned = makeFixture();
        Fixture& f = *owned;
        f.ctx.enterArsenal(8.0);
        f.ctx.startClipPreview(f.pattern);
        check(f.ctx.context() == PlaybackContext::Arsenal, "a preview under Arsenal focus keeps the Arsenal context");
        f.ctx.applyArsenalLoopLength(16.0);
        f.settle();
        check(f.engine.getPatternLengthBeats() == 16.0, "and Arsenal loop-length edits still reach the engine");
    }
    {
        auto owned = makeFixture();
        Fixture& f = *owned;
        f.ctx.resizeArsenalLoop(16.0);
        f.ctx.applyArsenalLoopLength(16.0);
        f.expect(mirrors(false, false, false, false, false), "loop-length paths are inert on the Timeline");
    }
}

// ---------------------------------------------------------------- teardown --

void testTeardownOnlyStopsPatternPlayback() {
    auto owned = makeFixture();
    Fixture& f = *owned;
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
    auto owned = makeFixture();
    Fixture& f = *owned;
    f.ctx.enterArsenal(8.0);
    f.ctx.applyArsenalLoopLength(8.0);
    check(f.tm.isPatternLoopOverrideActive(), "precondition: the override is set");
    f.ctx.playTimeline();
    check(!f.tm.isPatternLoopOverrideActive(), "timeline play ends the pattern loop override");
}

// ------------------------------------------------------------ PR 3: the queue --

// The context rides the audio command queue: nothing changes until the block that applies it.
// A direct store would pass the first check below and fail this test.
void testContextAppliesAtTheBlockNotBefore() {
    auto owned = makeFixture();
    Fixture& f = *owned;
    f.settle();
    f.engine.requestPlaybackContext(true, 8.0, false);
    check(!f.engine.isPatternPlaybackMode(), "a requested context is not applied before the audio thread drains it");
    check(f.engine.getAppliedContextGeneration() < f.engine.getRequestedContextGeneration(),
          "and the applied generation trails the requested one until then");
    f.engine.processBlock(f.block.data(), nullptr, 512, 0.0);
    check(f.engine.isPatternPlaybackMode() && f.engine.getPatternLengthBeats() == 8.0,
          "the next block applies it");
    check(f.engine.getAppliedContextGeneration() == f.engine.getRequestedContextGeneration(), "and settles");
}

// The point of PR 3: context and transport pushed together are applied in the SAME block.
void testContextAndTransportLandInOneBlock() {
    auto owned = makeFixture();
    Fixture& f = *owned;
    f.settle();
    f.engine.requestPlaybackContext(true, 8.0, false);
    AudioQueueCommand play{};
    play.type = AudioQueueCommandType::SetTransportState;
    play.value1 = 1.0f;
    play.samplePos = 0;
    f.engine.commandQueue().push(play);
    check(!f.engine.isPatternPlaybackMode() && !f.engine.isTransportPlaying(), "neither applies before the block");
    f.engine.processBlock(f.block.data(), nullptr, 512, 0.0);
    check(f.engine.isPatternPlaybackMode() && f.engine.isTransportPlaying(),
          "the block that starts the transport already renders in the new context");
}

// Generations are monotonic: a context superseded while still queued must not be re-applied.
void testSupersededContextIsNotReapplied() {
    auto owned = makeFixture();
    Fixture& f = *owned;
    f.engine.requestPlaybackContext(true, 8.0, false);
    f.settle();
    const uint64_t applied = f.engine.getAppliedContextGeneration();
    AudioQueueCommand stale{};
    stale.type = AudioQueueCommandType::SetPlaybackContext;
    stale.value1 = 0.0f;
    stale.value2 = 4.0f;
    stale.samplePos = applied; // not newer than what is already applied
    f.engine.commandQueue().push(stale);
    f.engine.processBlock(f.block.data(), nullptr, 512, 0.0);
    check(f.engine.isPatternPlaybackMode(), "a stale-generation context is ignored");
}

AudioQueueCommand metronome(bool on) {
    AudioQueueCommand cmd{};
    cmd.type = AudioQueueCommandType::SetMetronomeEnabled;
    cmd.value1 = on ? 1.0f : 0.0f;
    return cmd;
}

AudioQueueCommand playFromZero() {
    AudioQueueCommand cmd{};
    cmd.type = AudioQueueCommandType::SetTransportState;
    cmd.value1 = 1.0f;
    cmd.samplePos = 0;
    return cmd;
}

// Review, #957: the drain takes 16 commands per block. With 15 queued ahead, a transition's
// context is pop 16 and its transport pop 17. Split across blocks, the context rendered a
// block under the previous transport state.
void testDrainLimitKeepsThePairTogether() {
    auto owned = makeFixture();
    Fixture& f = *owned;
    f.settle();
    for (int i = 0; i < 15; ++i) {
        f.engine.commandQueue().push(metronome(false));
    }
    f.engine.requestPlaybackContext(true, 8.0, false); // pop 16
    f.engine.commandQueue().push(playFromZero());      // pop 17
    f.engine.processBlock(f.block.data(), nullptr, 512, 0.0);
    check(f.engine.isPatternPlaybackMode() && f.engine.isTransportPlaying(),
          "a context/transport pair straddling the drain limit lands in ONE block");
}

// The extra command taken at the limit is applied only if it is the pair's other half; anything
// else is carried to the front of the next block, in order, never dropped.
void testNonPartnerAtTheLimitIsCarriedNotDropped() {
    auto owned = makeFixture();
    Fixture& f = *owned;
    f.settle();
    for (int i = 0; i < 15; ++i) {
        f.engine.commandQueue().push(metronome(false));
    }
    f.engine.commandQueue().push(playFromZero()); // pop 16: half a pair
    f.engine.commandQueue().push(metronome(true)); // pop 17: not its partner
    f.engine.processBlock(f.block.data(), nullptr, 512, 0.0);
    check(f.engine.isTransportPlaying(), "the transport command at the limit applies");
    check(!f.engine.isMetronomeEnabled(), "the non-partner behind it waits for the next block");
    f.engine.processBlock(f.block.data(), nullptr, 512, 0.0);
    check(f.engine.isMetronomeEnabled(), "and is applied there, not dropped");
}

// Review, #957: a saturated queue used to make the PRODUCER write the context directly, racing
// an older command the audio thread was applying (stale context, applied generation going
// backwards). Now it is posted to a slot that only the audio thread applies.
void testSaturatedFallbackIsAppliedByTheAudioThread() {
    auto owned = makeFixture();
    Fixture& f = *owned;
    f.settle();
    int queued = 0;
    while (f.engine.commandQueue().push(metronome(false))) {
        ++queued;
    }
    check(queued > 0, "precondition: the queue fills");
    f.engine.requestPlaybackContext(true, 8.0, false); // reliable push times out: fallback slot
    check(!f.engine.isPatternPlaybackMode(), "the requesting thread does not write the context itself");
    f.engine.processBlock(f.block.data(), nullptr, 512, 0.0);
    check(f.engine.isPatternPlaybackMode() && f.engine.getPatternLengthBeats() == 8.0,
          "the audio thread applies the fallback at its next block");
    check(f.engine.getAppliedContextGeneration() == f.engine.getRequestedContextGeneration(),
          "and the context settles");
}

} // namespace

int main() {
    testDocumentedTable();
    testEnteringTimelineOrAuditionClearsEveryPatternMirror();
    testLoopLengthOnlyActsInArsenal();
    testTeardownOnlyStopsPatternPlayback();
    testTimelinePlayEndsTheOverride();
    testContextAppliesAtTheBlockNotBefore();
    testContextAndTransportLandInOneBlock();
    testSupersededContextIsNotReapplied();
    testDrainLimitKeepsThePairTogether();
    testNonPartnerAtTheLimitIsCarriedNotDropped();
    testSaturatedFallbackIsAppliedByTheAudioThread();

    if (g_failures == 0) {
        std::cout << "Playback context tests passed\n";
        return 0;
    }
    std::cout << g_failures << " playback context test(s) failed\n";
    return 1;
}
