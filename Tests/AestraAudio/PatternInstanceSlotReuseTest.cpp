// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Regression: the pattern scheduler treated every schedulePatternInstance() call as a NEW
// occurrence and appended it, while nothing ever erased entries — flush() only rewinds
// (scheduledThroughFrame = 0) so instances re-emit from the top, which is what a loop
// restart needs but not what leaving Arsenal needs.
//
// Live consequence: the Arsenal preview always re-arms slot 1, so a session accumulated
// dozens of overlapping copies of the same instance (31 observed in one sitting), each
// emitting its own events into the same units — layered audio, climbing peaks, and an
// Arsenal instance that survived into timeline playback and kept sounding under a linear
// transport.
//
// These assertions fail on the pre-fix engine: re-arming slot 1 five times left five live
// instances, and clearInstances() did not exist.

#include "Models/PatternManager.h"
#include "Models/TrackManager.h"
#include "Models/UnitManager.h"
#include "Playback/PatternPlaybackEngine.h"
#include "Playback/TimelineClock.h"

#include <cstdint>
#include <iostream>
#include <memory>

using namespace Aestra::Audio;

namespace {

int failures = 0;

void check(bool cond, const std::string& what) {
    if (cond) {
        std::cout << "PASS: " << what << "\n";
    } else {
        std::cout << "FAIL: " << what << "\n";
        ++failures;
    }
}

PatternID makePattern(PatternManager& pm, const std::string& name) {
    MidiPayload payload;
    MidiNote note;
    note.pitch = 60;
    note.startBeat = 0.0;
    note.durationBeats = 1.0;
    note.velocity = 0.8f;
    note.unitId = 1;
    payload.notes.push_back(note);
    return pm.createMidiPattern(name, 4.0, payload);
}

} // namespace

int main() {
    TimelineClock clock(120.0);
    PatternManager patterns;
    UnitManager units;
    PatternPlaybackEngine engine(&clock, &patterns, &units);

    const PatternID patternA = makePattern(patterns, "A");
    const PatternID patternB = makePattern(patterns, "B");

    check(engine.getActiveInstanceCount() == 0, "engine starts with no instances");

    // --- The bug: re-arming the SAME slot must replace, not accumulate. ---
    for (int i = 0; i < 5; ++i) {
        engine.schedulePatternInstance(patternA, 0.0, 1);
    }
    check(engine.getActiveInstanceCount() == 1,
          "re-arming slot 1 five times leaves exactly 1 instance (was 5 before the fix)");

    // Replacement must take the NEW content, not keep the stale entry.
    engine.schedulePatternInstance(patternB, 0.0, 1);
    check(engine.getActiveInstanceCount() == 1, "replacing slot 1 with another pattern still leaves 1 instance");

    // --- Distinct slots are genuinely distinct and must still accumulate. ---
    engine.schedulePatternInstance(patternA, 0.0, 2);
    engine.schedulePatternInstance(patternA, 4.0, 3);
    check(engine.getActiveInstanceCount() == 3, "distinct instance ids accumulate (1, 2, 3)");

    // Re-arming one slot must not disturb the others.
    engine.schedulePatternInstance(patternB, 8.0, 2);
    check(engine.getActiveInstanceCount() == 3, "re-arming slot 2 does not change the census");

    // --- flush() REWINDS, it does not remove. Guard that distinction explicitly:
    // conflating the two is what let an Arsenal instance survive into timeline playback.
    engine.flush();
    check(engine.getActiveInstanceCount() == 3, "flush() rewinds and must NOT drop instances");

    // --- clearInstances() removes outright. ---
    engine.clearInstances();
    check(engine.getActiveInstanceCount() == 0, "clearInstances() drops every instance");

    // Usable again afterwards.
    engine.schedulePatternInstance(patternA, 0.0, 1);
    check(engine.getActiveInstanceCount() == 1, "scheduler still usable after clearInstances()");

    // Out-of-range ids are rejected without corrupting the census.
    engine.schedulePatternInstance(patternA, 0.0, 999);
    check(engine.getActiveInstanceCount() == 1, "out-of-range instance id is rejected, census unchanged");

    // --- ISOLATION: an Arsenal preview must not carry timeline clip instances. ---
    //
    // scheduleTimelinePatternInstances() allocates ids from 2 upward and reserves 1 for the
    // Arsenal preview, so a timeline play() leaves ids 2..N scheduled. playPatternInArsenal()
    // used to call flush(), which only rewinds — those clip instances stayed live and were
    // mixed into the preview AND into offline render_pattern output. Measured directly: a
    // render that asked for one pattern emitted three instances, inflating its peak from
    // 0.094194 to 0.133211 (~3 dB) and breaking MuseServiceTest's half-gain ratio.
    //
    // Arsenal preview means "play THIS pattern alone". Assert that boundary directly rather
    // than inferring it from a rendered level.
    {
        auto trackManager = std::make_shared<TrackManager>();
        auto& tmEngine = trackManager->getPatternPlaybackEngine();
        auto& tmPatterns = trackManager->getPatternManager();

        const PatternID arsenalPattern = makePattern(tmPatterns, "arsenal");
        const PatternID timelinePattern = makePattern(tmPatterns, "timeline");

        // Stand in for what a timeline play() leaves behind: clip instances at ids 2+.
        tmEngine.schedulePatternInstance(timelinePattern, 0.0, 2);
        tmEngine.schedulePatternInstance(timelinePattern, 8.0, 3);
        check(tmEngine.getActiveInstanceCount() == 2, "timeline clip instances are scheduled at ids 2+");

        trackManager->playPatternInArsenal(arsenalPattern, 0.0);
        check(tmEngine.getActiveInstanceCount() == 1,
              "Arsenal preview starts from an EMPTY scheduler — timeline clip instances cannot "
              "contribute to it (was 3 before the fix)");

        // And the reverse boundary still holds: leaving Arsenal drops its instance too, so
        // nothing bleeds the other way into timeline playback.
        trackManager->stopArsenalPlayback(false);
        check(tmEngine.getActiveInstanceCount() == 0, "leaving Arsenal drops its preview instance");
    }

    if (failures == 0) {
        std::cout << "All pattern-instance slot-reuse checks passed\n";
        return 0;
    }
    std::cout << failures << " check(s) failed\n";
    return 1;
}
