// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "MusicalTypingController.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
struct MidiEvent {
    uint64_t unitId;
    uint8_t status;
    uint8_t note;
    uint8_t velocity;
};

[[noreturn]] void fail(const char* message) {
    std::cerr << "[FAIL] " << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

AestraUI::NUIKeyEvent press(AestraUI::NUIKeyCode key, AestraUI::NUIModifiers modifiers = AestraUI::NUIModifiers::None) {
    AestraUI::NUIKeyEvent event;
    event.keyCode = key;
    event.modifiers = modifiers;
    event.pressed = true;
    return event;
}

AestraUI::NUIKeyEvent release(AestraUI::NUIKeyCode key) {
    AestraUI::NUIKeyEvent event;
    event.keyCode = key;
    event.released = true;
    return event;
}
} // namespace

int main() {
    std::vector<MidiEvent> events;
    Aestra::MusicalTypingController typing([&](uint64_t unitId, uint8_t status, uint8_t note, uint8_t velocity) {
        events.push_back({unitId, status, note, velocity});
        return true;
    });
    typing.setTargetUnit(7);

    require(typing.handleKeyEvent(press(AestraUI::NUIKeyCode::Z)), "Z press should be handled");
    require(events.size() == 1 && events[0].unitId == 7 && events[0].status == 0x90 && events[0].note == 60,
            "Z should emit middle-C note-on for selected unit");

    typing.handleKeyEvent(press(AestraUI::NUIKeyCode::Z));
    require(events.size() == 1, "held key must suppress repeated note-ons without relying on repeat flag");
    typing.handleKeyEvent(release(AestraUI::NUIKeyCode::Z));
    require(events.size() == 2 && events[1].status == 0x80 && events[1].note == 60,
            "Z release should emit matching note-off");

    typing.handleKeyEvent(press(AestraUI::NUIKeyCode::Q));
    require(events.back().note == 72, "Q should begin the upper keyboard row one octave above Z");
    typing.setTargetUnit(9);
    require(events.back().unitId == 7 && events.back().status == 0x80 && events.back().note == 72,
            "target changes must release held notes on their original unit");

    typing.handleKeyEvent(press(AestraUI::NUIKeyCode::Up));
    require(typing.displayOctave() == 4, "Up should shift the base octave by one");
    typing.handleKeyEvent(press(AestraUI::NUIKeyCode::M));
    require(events.back().unitId == 9 && events.back().note == 83, "M should follow selected unit and shifted octave");
    typing.releaseAllNotes();
    require(typing.heldNoteCount() == 0 && events.back().status == 0x80,
            "focus-loss panic should release every held key");

    const size_t beforeShortcut = events.size();
    require(!typing.handleKeyEvent(press(AestraUI::NUIKeyCode::Z, AestraUI::NUIModifiers::Ctrl)),
            "modified application shortcut must not be claimed");
    require(events.size() == beforeShortcut, "modified shortcut must not emit MIDI");

    typing.handleKeyEvent(press(AestraUI::NUIKeyCode::Z));
    const size_t beforeDisable = events.size();
    typing.handleKeyEvent(press(AestraUI::NUIKeyCode::CapsLock));
    require(!typing.isEnabled(), "Caps Lock should disable musical typing");
    require(events.size() == beforeDisable + 1 && events.back().status == 0x80 && typing.heldNoteCount() == 0,
            "disabling musical typing must send all-notes-off for held computer keys");
    require(!typing.handleKeyEvent(press(AestraUI::NUIKeyCode::Z)), "disabled mode should not claim note keys");
    typing.handleKeyEvent(press(AestraUI::NUIKeyCode::CapsLock));
    require(typing.isEnabled(), "Caps Lock should re-enable musical typing");

    for (int i = 0; i < 20; ++i) {
        typing.handleKeyEvent(press(AestraUI::NUIKeyCode::Up));
        typing.handleKeyEvent(release(AestraUI::NUIKeyCode::Up));
    }
    require(typing.baseMidiNote() == 96, "octave shift must clamp below MIDI 127 for upper row");

    std::cout << "[PASS] MusicalTypingControllerTest\n";
    return 0;
}
