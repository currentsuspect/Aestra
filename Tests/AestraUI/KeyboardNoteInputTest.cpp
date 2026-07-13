// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// KeyboardNoteInputTest — musical-typing translation logic, headless.
//
// Pins the QWERTY→note contract the UI relies on: layout mapping, octave
// shifts, note-on/off pairing across octave and unit changes (tag captured at
// note-on), auto-repeat swallowing, modifier passthrough, releaseAll, and the
// no-target behavior (keys not consumed so shortcuts keep working).

#include "../../Source/Core/KeyboardNoteInput.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void require(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        ++g_failures;
    }
}

struct SinkEvent {
    uint8_t note;
    uint8_t velocity;
    bool on;
    uint64_t tag;
};

AestraUI::NUIKeyEvent key(AestraUI::NUIKeyCode code, bool pressed, bool repeat = false,
                          AestraUI::NUIModifiers mods = AestraUI::NUIModifiers::None) {
    AestraUI::NUIKeyEvent ev;
    ev.keyCode = code;
    ev.pressed = pressed;
    ev.released = !pressed;
    ev.repeat = repeat;
    ev.modifiers = mods;
    return ev;
}

} // namespace

int main() {
    using AestraUI::NUIKeyCode;
    using AestraUI::NUIModifiers;

    Aestra::KeyboardNoteInput input;
    std::vector<SinkEvent> events;
    uint64_t currentUnit = 7;
    input.setSink([&](uint8_t note, uint8_t velocity, bool on, uint64_t tag) {
        events.push_back({note, velocity, on, tag});
    });
    input.setTagProvider([&]() { return currentUnit; });

    // ---------------- Layout: A = C at the default octave (middle C = 60).
    require(input.handleKeyEvent(key(NUIKeyCode::A, true)), "A press not consumed");
    require(events.size() == 1 && events[0].on && events[0].note == 60, "A != middle C note-on");
    require(events[0].tag == 7, "tag not captured from provider");
    require(input.handleKeyEvent(key(NUIKeyCode::A, false)), "A release not consumed");
    require(events.size() == 2 && !events[1].on && events[1].note == 60, "A note-off mismatch");

    // Black key and next-octave keys.
    events.clear();
    input.handleKeyEvent(key(NUIKeyCode::W, true));
    input.handleKeyEvent(key(NUIKeyCode::K, true));
    require(events.size() == 2 && events[0].note == 61 && events[1].note == 72, "W/K mapping (C#, C+1)");
    input.handleKeyEvent(key(NUIKeyCode::W, false));
    input.handleKeyEvent(key(NUIKeyCode::K, false));

    // ---------------- Auto-repeat of a held key: swallowed, no retrigger.
    events.clear();
    input.handleKeyEvent(key(NUIKeyCode::A, true));
    require(input.handleKeyEvent(key(NUIKeyCode::A, true, /*repeat=*/true)), "repeat not consumed");
    require(events.size() == 1, "auto-repeat retriggered the note");
    input.handleKeyEvent(key(NUIKeyCode::A, false));

    // ---------------- Octave shift moves NEW notes; held notes keep their pitch.
    events.clear();
    input.handleKeyEvent(key(NUIKeyCode::A, true)); // 60 held
    require(input.handleKeyEvent(key(NUIKeyCode::X, true)), "octave-up not consumed");
    input.handleKeyEvent(key(NUIKeyCode::S, true)); // D at the NEW octave: 62+12=74
    require(events.size() == 2 && events[1].note == 74, "post-shift note not in new octave");
    input.handleKeyEvent(key(NUIKeyCode::A, false));
    require(events.size() == 3 && !events[2].on && events[2].note == 60,
            "held note released at its original pitch after octave change");
    input.handleKeyEvent(key(NUIKeyCode::S, false));
    input.handleKeyEvent(key(NUIKeyCode::Z, true)); // restore octave

    // ---------------- Unit switch mid-hold: note-off goes to the ORIGINAL unit.
    events.clear();
    input.handleKeyEvent(key(NUIKeyCode::A, true));
    currentUnit = 9;
    input.handleKeyEvent(key(NUIKeyCode::S, true));
    input.handleKeyEvent(key(NUIKeyCode::A, false));
    require(events.size() == 3, "unit-switch sequence event count");
    if (events.size() == 3) {
        require(events[0].tag == 7, "first note tagged with original unit");
        require(events[1].tag == 9, "second note tagged with new unit");
        require(events[2].tag == 7 && !events[2].on, "note-off routed to the unit that got the note-on");
    }
    input.handleKeyEvent(key(NUIKeyCode::S, false));

    // ---------------- Modifiers pass through (shortcuts win).
    events.clear();
    require(!input.handleKeyEvent(key(NUIKeyCode::A, true, false, NUIModifiers::Ctrl)),
            "Ctrl+A consumed by musical typing");
    require(!input.handleKeyEvent(key(NUIKeyCode::Z, true, false, NUIModifiers::Ctrl)),
            "Ctrl+Z consumed by musical typing");
    require(events.empty(), "modified keys produced notes");

    // ---------------- Non-musical keys pass through.
    require(!input.handleKeyEvent(key(NUIKeyCode::I, true)), "non-musical letter consumed");
    require(!input.handleKeyEvent(key(NUIKeyCode::F5, true)), "function key consumed");

    // ---------------- No target unit: keys not consumed, no events.
    events.clear();
    currentUnit = 0;
    require(!input.handleKeyEvent(key(NUIKeyCode::A, true)), "key consumed with no target unit");
    require(events.empty(), "events emitted with no target unit");
    currentUnit = 7;

    // ---------------- releaseAll: note-offs for everything held, right tags.
    events.clear();
    input.handleKeyEvent(key(NUIKeyCode::A, true));
    input.handleKeyEvent(key(NUIKeyCode::D, true));
    require(input.activeNoteCount() == 2, "active count before releaseAll");
    input.releaseAll();
    require(input.activeNoteCount() == 0, "active count after releaseAll");
    int offs = 0;
    for (const auto& e : events) {
        if (!e.on)
            ++offs;
    }
    require(offs == 2, "releaseAll emitted wrong number of note-offs");
    // Releases after releaseAll are not consumed (keys no longer held).
    require(!input.handleKeyEvent(key(NUIKeyCode::A, false)), "stale release consumed after releaseAll");

    if (g_failures != 0) {
        std::cerr << "[FAIL] KeyboardNoteInputTest: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "[PASS] KeyboardNoteInputTest\n";
    return 0;
}
