// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../../AestraUI/Core/NUITypes.h"

#include <array>
#include <cstdint>
#include <functional>

namespace Aestra {

/**
 * @brief Musical typing — the QWERTY keyboard as a piano.
 *
 * Pure key→note translation with held-key tracking; no engine or UI
 * dependencies, so it is unit-testable headless. The owner (AestraContent)
 * wires a sink that posts to AudioEngine::postLiveMidiEvent and a tag
 * provider that resolves the current target unit.
 *
 * Layout (Ableton/GarageBand musical typing, restricted to keys that exist
 * in NUIKeyCode):
 *
 *     W E   T Y U   O P        <- black keys (C# D#  F# G# A#  C#' D#')
 *    A S D F G H J K L         <- white keys (C D E F G A B  C' D')
 *     Z = octave down, X = octave up
 *
 * Rules:
 *  - The sounding pitch AND target tag are captured at key-DOWN, so octave or
 *    unit changes while a key is held never orphan the matching note-off.
 *  - Auto-repeat presses of a held key are swallowed (no retrigger).
 *  - Ctrl/Alt/Super-modified keys are never consumed (shortcuts win).
 *  - releaseAll() emits note-offs for everything held (panel close, focus loss).
 */
class KeyboardNoteInput {
public:
    /// note, velocity, on/off, tag (target unit id captured at note-on).
    using NoteSink = std::function<void(uint8_t note, uint8_t velocity, bool on, uint64_t tag)>;
    /// Resolves the target tag (unit id) for a new note-on. 0 = no target; key not consumed.
    using TagProvider = std::function<uint64_t()>;

    void setSink(NoteSink sink) { m_sink = std::move(sink); }
    void setTagProvider(TagProvider provider) { m_tagProvider = std::move(provider); }

    /// Returns true when the event was consumed as musical input.
    bool handleKeyEvent(const AestraUI::NUIKeyEvent& event) {
        using AestraUI::NUIKeyCode;
        using AestraUI::NUIModifiers;

        if ((event.modifiers & NUIModifiers::Ctrl) || (event.modifiers & NUIModifiers::Alt) ||
            (event.modifiers & NUIModifiers::Super)) {
            return false;
        }

        // Octave shift (press only).
        if (event.pressed && !event.repeat) {
            if (event.keyCode == NUIKeyCode::Z) {
                if (m_octave > kMinOctave)
                    --m_octave;
                return true;
            }
            if (event.keyCode == NUIKeyCode::X) {
                if (m_octave < kMaxOctave)
                    ++m_octave;
                return true;
            }
        }

        const int semitone = semitoneForKey(event.keyCode);
        if (semitone < 0) {
            return false;
        }
        const size_t slot = keySlot(event.keyCode);

        if (event.pressed) {
            if (m_held[slot].down) {
                return true; // auto-repeat of a held key: swallow, no retrigger
            }
            const uint64_t tag = m_tagProvider ? m_tagProvider() : 0;
            if (tag == 0) {
                return false; // no target instrument — let others use the key
            }
            const int note = m_octave * 12 + semitone;
            if (note < 0 || note > 127) {
                return true;
            }
            m_held[slot].down = true;
            m_held[slot].note = static_cast<uint8_t>(note);
            m_held[slot].tag = tag;
            ++m_activeCount;
            if (m_sink) {
                m_sink(m_held[slot].note, kVelocity, true, tag);
            }
            return true;
        }

        if (event.released && m_held[slot].down) {
            m_held[slot].down = false;
            if (m_activeCount > 0)
                --m_activeCount;
            if (m_sink) {
                m_sink(m_held[slot].note, 0, false, m_held[slot].tag);
            }
            return true;
        }

        return false;
    }

    /// Note-off everything held (panel closed, focus lost, unit destroyed).
    void releaseAll() {
        for (auto& key : m_held) {
            if (key.down) {
                key.down = false;
                if (m_sink) {
                    m_sink(key.note, 0, false, key.tag);
                }
            }
        }
        m_activeCount = 0;
    }

    int activeNoteCount() const { return m_activeCount; }
    int octave() const { return m_octave; }
    void setOctave(int octave) { m_octave = octave < kMinOctave ? kMinOctave : (octave > kMaxOctave ? kMaxOctave : octave); }

private:
    static constexpr uint8_t kVelocity = 100;
    // Octave addressing is note = octave*12 + semitone, so octave 5 puts the
    // A key on MIDI 60 (middle C). Range keeps the full two-row layout legal.
    static constexpr int kMinOctave = 0;
    static constexpr int kMaxOctave = 9;

    struct HeldKey {
        bool down{false};
        uint8_t note{0};
        uint64_t tag{0};
    };

    // Semitone offset within the octave for each musical key, -1 = not musical.
    static int semitoneForKey(AestraUI::NUIKeyCode key) {
        using AestraUI::NUIKeyCode;
        switch (key) {
        case NUIKeyCode::A: return 0;   // C
        case NUIKeyCode::W: return 1;   // C#
        case NUIKeyCode::S: return 2;   // D
        case NUIKeyCode::E: return 3;   // D#
        case NUIKeyCode::D: return 4;   // E
        case NUIKeyCode::F: return 5;   // F
        case NUIKeyCode::T: return 6;   // F#
        case NUIKeyCode::G: return 7;   // G
        case NUIKeyCode::Y: return 8;   // G#
        case NUIKeyCode::H: return 9;   // A
        case NUIKeyCode::U: return 10;  // A#
        case NUIKeyCode::J: return 11;  // B
        case NUIKeyCode::K: return 12;  // C, next octave
        case NUIKeyCode::O: return 13;  // C#'
        case NUIKeyCode::L: return 14;  // D'
        case NUIKeyCode::P: return 15;  // D#'
        default: return -1;
        }
    }

    static size_t keySlot(AestraUI::NUIKeyCode key) {
        // Letters are contiguous (A=65..Z=90); slot by letter index.
        return static_cast<size_t>(static_cast<int>(key) - static_cast<int>(AestraUI::NUIKeyCode::A));
    }

    std::array<HeldKey, 26> m_held{};
    NoteSink m_sink;
    TagProvider m_tagProvider;
    int m_octave = 5; // C5 = MIDI 60 (middle C) under octave*12 addressing
    int m_activeCount = 0;
};

} // namespace Aestra
