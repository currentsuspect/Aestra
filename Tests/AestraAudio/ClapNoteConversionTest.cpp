// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// ClapNoteConversionTest — the shared CLAP note-conversion rules used by both the
// in-process host and the OOP child (#244). Proving them here pins the "both
// paths use the same rules" contract at the shared-logic level (the in-process
// CLAPHost.cpp is SDK-gated and not built in CI, so the shared rules are the
// only place the two paths can be jointly verified).

#include "Plugin/ClapNoteConversion.h"

#include <cstdint>
#include <iostream>
#include <string>

using namespace Aestra::Audio::ClapNote;

namespace {

int g_failures = 0;

void check(bool cond, const std::string& label) {
    std::cout << (cond ? "PASS: " : "FAIL: ") << label << "\n";
    if (!cond) {
        ++g_failures;
    }
}

DecodedNote decode(uint8_t s, uint8_t d1, uint8_t d2) {
    const uint8_t bytes[3] = {s, d1, d2};
    return decodeMidiMessage(bytes);
}

} // namespace

int main() {
    // --- MIDI decode ----------------------------------------------------------
    {
        const DecodedNote on = decode(0x90 | 5, 60, 100); // note on, ch 5, key 60, vel 100
        check(on.kind == MidiNoteKind::NoteOn, "0x90 with velocity>0 is a note-on");
        check(on.channel == 5, "channel decoded");
        check(on.key == 60, "key decoded");
        check(on.velocity == 100, "velocity decoded");

        const DecodedNote off = decode(0x80 | 3, 64, 0); // note off, ch 3
        check(off.kind == MidiNoteKind::NoteOff, "0x80 is a note-off");
        check(off.channel == 3 && off.key == 64, "note-off channel/key decoded");

        const DecodedNote zeroVel = decode(0x90 | 1, 72, 0); // note on, velocity 0
        check(zeroVel.kind == MidiNoteKind::NoteOff, "note-on velocity 0 is a note-off (running status)");

        const DecodedNote cc = decode(0xB0, 7, 127); // control change
        check(cc.kind == MidiNoteKind::Other, "non-note messages are Other");

        check(decodeMidiMessage(nullptr).kind == MidiNoteKind::Other, "null decode is safe");
    }

    // --- normalized velocity --------------------------------------------------
    {
        check(normalizedVelocity(127) == 1.0, "velocity 127 -> 1.0");
        check(normalizedVelocity(0) == 0.0, "velocity 0 -> 0.0");
        check(std::abs(normalizedVelocity(64) - 64.0 / 127.0) < 1e-9, "velocity 64 normalized");
    }

    // --- dialect selection (policy per #244 review) ---------------------------
    {
        // Single-dialect ports.
        check(selectDialect(kDialectClap, 0) == kDialectClap, "CLAP only -> CLAP");
        check(selectDialect(kDialectMidi, 0) == kDialectMidi, "MIDI only -> MIDI");

        // Honor a validly-advertised preference in a dual-dialect port.
        check(selectDialect(kDialectClap | kDialectMidi, kDialectClap) == kDialectClap,
              "CLAP+MIDI, preferred CLAP -> CLAP");
        check(selectDialect(kDialectClap | kDialectMidi, kDialectMidi) == kDialectMidi,
              "CLAP+MIDI, preferred MIDI -> MIDI");

        // A preference honored even though the other dialect is also supported —
        // we must NOT pick MIDI merely because it is in the supported mask.
        check(selectDialect(kDialectClap | kDialectMidi, kDialectClap) != kDialectMidi,
              "preferred CLAP is not overridden by MIDI being supported");

        // Preferred dialect absent from the supported mask -> documented fallback
        // (native CLAP first where supported, else MIDI).
        check(selectDialect(kDialectMidi, kDialectClap) == kDialectMidi,
              "preferred CLAP unsupported (MIDI-only port) -> MIDI fallback");
        check(selectDialect(kDialectClap | kDialectMidi, 0) == kDialectClap,
              "no preference on a dual port -> CLAP fallback (not MIDI-by-default)");
        check(selectDialect(kDialectClap | kDialectMidi, kDialectMidiMpe) == kDialectClap,
              "unsupported (MPE) preference -> CLAP fallback");

        // No mutually supported dialect -> 0 (caller delivers no notes).
        check(selectDialect(0, 0) == 0, "no supported dialect -> 0");
        check(selectDialect(kDialectMidiMpe | kDialectMidi2, kDialectMidiMpe) == 0,
              "only MPE/MIDI2 (out of scope) -> 0, even with a preference");
    }

    std::cout << (g_failures == 0 ? "ALL PASSED\n"
                                  : "FAILURES: " + std::to_string(g_failures) + "\n");
    return g_failures == 0 ? 0 : 1;
}
