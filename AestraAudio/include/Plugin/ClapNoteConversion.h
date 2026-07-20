// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>

// Shared CLAP note-conversion rules used by BOTH CLAP hosting paths — the
// in-process host (CLAPHost.cpp, real CLAP SDK) and the out-of-process child
// (AestraPluginHostMain.cpp, hand-mirrored CLAP ABI). Keeping the dialect
// selection and MIDI decoding here means the two paths cannot drift (#244).
//
// This header is deliberately SDK-free (the OOP child has no CLAP headers) and
// allocation-free/branch-only, so it is safe to call from the audio thread. It
// deals in plain integers; each caller builds its own event struct (real
// clap_event_note vs. the child's mirror) from the decoded fields.

namespace Aestra {
namespace Audio {
namespace ClapNote {

// clap_note_dialect bits (clap/ext/note-ports.h), mirrored so this header needs
// no SDK. Only CLAP and MIDI are handled here; MPE / MIDI2 are out of scope.
enum : uint32_t {
    kDialectClap = 1u << 0,
    kDialectMidi = 1u << 1,
    kDialectMidiMpe = 1u << 2,
    kDialectMidi2 = 1u << 3,
};

enum class MidiNoteKind : uint8_t { NoteOn, NoteOff, Other };

struct DecodedNote {
    MidiNoteKind kind{MidiNoteKind::Other};
    uint8_t channel{0};  // 0..15
    uint8_t key{0};      // 0..127
    uint8_t velocity{0}; // 0..127 (0 on a note-on means note-off)
};

// Decode a 3-byte MIDI channel-voice message. A note-on with velocity 0 is the
// running-status note-off convention and is reported as NoteOff.
inline DecodedNote decodeMidiMessage(const uint8_t* data) {
    DecodedNote out;
    if (!data) {
        return out;
    }
    const uint8_t status = static_cast<uint8_t>(data[0] & 0xF0);
    out.channel = static_cast<uint8_t>(data[0] & 0x0F);
    out.key = static_cast<uint8_t>(data[1] & 0x7F);
    out.velocity = static_cast<uint8_t>(data[2] & 0x7F);
    if (status == 0x90) { // Note On
        out.kind = out.velocity > 0 ? MidiNoteKind::NoteOn : MidiNoteKind::NoteOff;
    } else if (status == 0x80) { // Note Off
        out.kind = MidiNoteKind::NoteOff;
    }
    return out;
}

// Host-emittable note dialects (Aestra can produce native CLAP and raw MIDI;
// MPE / MIDI2 are out of scope for #244).
constexpr uint32_t kHostSupportedDialects = kDialectClap | kDialectMidi;

// Pick the note dialect to deliver on a plugin note-input port, deterministically.
//   - Honor a validly-advertised preference: if preferred_dialect is a single
//     dialect present in BOTH the plugin's supported mask and the host's, use it.
//     (In particular, a plugin that supports CLAP+MIDI but prefers CLAP gets
//     CLAP — we never pick raw MIDI merely because MIDI is in supported_dialects.)
//   - If the preference is absent (0) or not mutually supported, fall back
//     deterministically to native CLAP when the port supports it, else MIDI.
//     This is a documented tie-break, not a MIDI-by-default.
//   - Return 0 when there is no mutually supported dialect; the caller must then
//     deliver no notes (and log a diagnostic).
// @param supported  supported_dialects bitmask from the note port.
// @param preferred  preferred_dialect from the note port (one bit, or 0).
inline uint32_t selectDialect(uint32_t supported, uint32_t preferred) {
    const uint32_t usable = supported & kHostSupportedDialects;
    if (usable == 0) {
        return 0; // no mutually supported dialect — caller delivers no notes
    }
    // A validly-advertised preference is a single dialect we both support.
    const bool preferredIsSingleBit = preferred != 0 && (preferred & (preferred - 1)) == 0;
    if (preferredIsSingleBit && (preferred & usable) == preferred) {
        return preferred;
    }
    // Absent or unsupported preference: deterministic documented fallback.
    return (usable & kDialectClap) ? kDialectClap : kDialectMidi;
}

// Normalized CLAP note velocity (0..1) from a 7-bit MIDI velocity.
inline double normalizedVelocity(uint8_t midiVelocity) {
    return static_cast<double>(midiVelocity) / 127.0;
}

} // namespace ClapNote
} // namespace Audio
} // namespace Aestra
