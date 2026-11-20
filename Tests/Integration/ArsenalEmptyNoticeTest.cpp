// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The "no sound yet" notice (Arsenal coherence, mock q2 option C): what counts as empty per unit type, the copy for one
// or several units, once per unit per session, the wait while recording, the 6 s timer that holds under the pointer,
// and the room the list gives up so the toast never covers the last row or the add line. Pure headers, no libraries.

#include "ArsenalEmptyNotice.h"
#include "ToastStack.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        std::exit(1);
    }
}

using namespace Aestra::ArsenalEmptyNotice;

void testEmptyPerUnitType() {
    // Sampler / PitchedSampler: empty exactly when no sample is loaded (and no plugin took its place).
    require(isEmpty({Kind::Sampler, false, false}), "a sampler with no sample is empty");
    require(!isEmpty({Kind::Sampler, true, false}), "a sampler with a sample is not empty");
    require(isEmpty({Kind::PitchedSampler, false, false}), "a pitched sampler with no sample is empty");
    require(!isEmpty({Kind::PitchedSampler, true, false}), "a pitched sampler with a sample is not empty");
    require(!isEmpty({Kind::Sampler, false, true}), "a sampler swapped for a loaded plugin still makes sound");
    // Instrument / plugin: a missing or failed plugin is empty; a loaded one is not, preset or no preset.
    require(isEmpty({Kind::Instrument, false, false}), "an instrument whose plugin is missing or failed is empty");
    require(!isEmpty({Kind::Instrument, false, true}), "a loaded plugin with no preset is NOT empty");
    // A sample path means nothing to an instrument unit.
    require(isEmpty({Kind::Instrument, true, false}), "a sample path does not make a missing plugin sound");
    // Audio units are not subject to the notice.
    require(!isEmpty({Kind::Audio, false, false}), "an audio unit is never empty for this notice");
    require(!isEmpty({Kind::Audio, true, true}), "an audio unit is never empty for this notice (loaded)");
}

void testCopy() {
    require(message({"Sampler 4"}) == "Sampler 4 has no sound yet.", "one unit uses its real name");
    require(message({"Kick"}) == "Kick has no sound yet.", "a renamed unit uses its name");
    require(message({"Sampler 4", "Sampler 5", "Sampler 6"}) == "Samplers 4, 5 and 6 have no sound yet.",
            "several of one kind are one grouped sentence");
    require(message({"Sampler 4", "Sampler 5"}) == "Samplers 4 and 5 have no sound yet.", "two read with 'and'");
    require(message({"Kick", "Snare", "Hat"}) == "Kick, Snare and Hat have no sound yet.", "mixed names are listed");
    require(message({"Sampler 1", "Kick"}) == "Sampler 1 and Kick have no sound yet.", "a partial match is not grouped");
    require(message({"Sampler 1", "Sampler 2", "Sampler 3", "Sampler 4", "Sampler 5", "Sampler 6"}) ==
                "Samplers 1, 2, 3, 4 and 2 more have no sound yet.",
            "a long list is cut with a count");
    require(message({}).empty(), "nothing to say about no units");
}

void testOncePerUnitPerSession() {
    Tracker t;
    const auto always = [](uint64_t) { return true; };
    t.note(7);
    require(t.hasPending(), "a noted unit waits");
    auto shown = t.take(false, always);
    require(shown.size() == 1 && shown[0] == 7, "the noted unit is shown");
    require(t.wasShown(7) && !t.hasPending(), "it is remembered and nothing waits");
    t.note(7);
    require(!t.hasPending(), "the same unit is never noted again this session");
    require(t.take(false, always).empty(), "so a second pad tap shows no second toast");
    t.note(8);
    t.note(9);
    shown = t.take(false, always);
    require(shown.size() == 2 && shown[0] == 8 && shown[1] == 9, "several units come out together, in order");
    t.note(8);
    t.note(10);
    shown = t.take(false, always);
    require(shown.size() == 1 && shown[0] == 10, "a mix of seen and new shows only the new one");
}

void testWaitsWhileRecording() {
    Tracker t;
    t.note(3);
    require(t.take(true, [](uint64_t) { return true; }).empty(), "nothing shows while recording");
    require(t.hasPending() && !t.wasShown(3), "it keeps waiting and is not used up");
    const auto shown = t.take(false, [](uint64_t) { return true; });
    require(shown.size() == 1 && shown[0] == 3, "when recording stops it shows");
}

void testDropsWhatIsNoLongerRelevant() {
    Tracker t;
    t.note(3);
    t.note(4);
    const auto shown = t.take(false, [](uint64_t id) { return id == 4; }); // 3 got a sound meanwhile
    require(shown.size() == 1 && shown[0] == 4, "a unit that got a sound while waiting is skipped");
    require(!t.hasPending(), "and the skipped one is not kept");
    require(!t.wasShown(3), "a skipped unit is not used up: it can still be told later");
}

void testTimerAndHover() {
    AestraUI::ActionToastTimer timer;
    require(AestraUI::kActionToastSeconds == 6.0, "the toast lasts 6 seconds");
    require(!timer.update(5.9), "still showing just before 6 s");
    require(timer.update(0.2), "gone after 6 s");

    AestraUI::ActionToastTimer held;
    held.setHovered(true);
    require(!held.update(30.0), "it does not go while the pointer is over it");
    held.setHovered(false);
    require(!held.update(5.5), "the time runs again once the pointer leaves, from where it stopped");
    require(held.update(1.0), "and ends 6 s of unhovered time later");

    // The fades follow the timer's age, which stands still under the pointer: a pill held for a minute stays solid.
    AestraUI::ActionToastTimer fade;
    fade.setHovered(true);
    fade.update(60.0);
    require(fade.age() == 0.0, "a toast held under the pointer does not age, so it never fades out");
    fade.setHovered(false);
    fade.update(2.0);
    require(fade.age() == 2.0, "and ages again once the pointer leaves");
}

void testRoomForTheToast() {
    // Panel bottom 600, toast top 560: the list gives up the overlap plus the gap so its last row stays clear.
    require(bottomInset(600.0f, 560.0f, true, 8.0f) == 48.0f, "overlap 40 + gap 8");
    require(bottomInset(500.0f, 560.0f, true, 8.0f) == 0.0f, "a panel that ends above the toast needs no room");
    require(bottomInset(600.0f, 560.0f, false, 8.0f) == 0.0f, "a toast beside the panel needs no room");
    require(bottomInset(560.0f, 560.0f, true, 8.0f) == 8.0f, "touching still keeps the gap");
}

} // namespace

int main() {
    testEmptyPerUnitType();
    testCopy();
    testOncePerUnitPerSession();
    testWaitsWhileRecording();
    testDropsWhatIsNoLongerRelevant();
    testTimerAndHover();
    testRoomForTheToast();
    std::cout << "ArsenalEmptyNoticeTest: all checks passed\n";
    return 0;
}
