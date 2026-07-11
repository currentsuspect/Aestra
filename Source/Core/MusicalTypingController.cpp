// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "MusicalTypingController.h"

#include <algorithm>
#include <utility>

namespace Aestra {

namespace {
constexpr uint8_t kNoteOff = 0x80;
constexpr uint8_t kNoteOn = 0x90;
constexpr uint8_t kDefaultVelocity = 100;
constexpr int kMinimumBaseNote = 0;
constexpr int kMaximumBaseNote = 96; // Upper row tops out at MIDI 124.
} // namespace

MusicalTypingController::MusicalTypingController(EventSink sink)
    : m_sink(std::move(sink)) {}

void MusicalTypingController::setEventSink(EventSink sink) {
    releaseAllNotes();
    m_sink = std::move(sink);
}

void MusicalTypingController::setTargetUnit(uint64_t unitId) {
    if (m_targetUnit == unitId) {
        return;
    }
    releaseAllNotes();
    m_targetUnit = unitId;
}

void MusicalTypingController::setEnabled(bool enabled) {
    if (m_enabled == enabled) {
        return;
    }
    if (!enabled) {
        releaseAllNotes();
    }
    m_enabled = enabled;
}

bool MusicalTypingController::post(uint64_t unitId, uint8_t status, uint8_t note, uint8_t velocity) {
    return m_sink && unitId != 0 && m_sink(unitId, status, note, velocity);
}

int MusicalTypingController::semitoneForKey(AestraUI::NUIKeyCode keyCode) {
    using Key = AestraUI::NUIKeyCode;
    switch (keyCode) {
    // Lower row: Z-M, one chromatic octave.
    case Key::Z: return 0;
    case Key::S: return 1;
    case Key::X: return 2;
    case Key::D: return 3;
    case Key::C: return 4;
    case Key::V: return 5;
    case Key::G: return 6;
    case Key::B: return 7;
    case Key::H: return 8;
    case Key::N: return 9;
    case Key::J: return 10;
    case Key::M: return 11;

    // Upper row: Q-P with number-row black keys, matching common DAW layouts.
    case Key::Q: return 12;
    case Key::Num2: return 13;
    case Key::W: return 14;
    case Key::Num3: return 15;
    case Key::E: return 16;
    case Key::R: return 17;
    case Key::Num5: return 18;
    case Key::T: return 19;
    case Key::Num6: return 20;
    case Key::Y: return 21;
    case Key::Num7: return 22;
    case Key::U: return 23;
    case Key::I: return 24;
    case Key::Num9: return 25;
    case Key::O: return 26;
    case Key::Num0: return 27;
    case Key::P: return 28;
    default: return -1;
    }
}

bool MusicalTypingController::shiftOctave(int semitones) {
    const int next = std::clamp(m_baseMidiNote + semitones, kMinimumBaseNote, kMaximumBaseNote);
    if (next == m_baseMidiNote) {
        return true;
    }
    releaseAllNotes();
    m_baseMidiNote = next;
    return true;
}

bool MusicalTypingController::handleKeyEvent(const AestraUI::NUIKeyEvent& event) {
    using Key = AestraUI::NUIKeyCode;

    // Caps Lock is the explicit mode toggle and remains available while off.
    if (event.keyCode == Key::CapsLock) {
        if (event.pressed && !event.repeat) {
            setEnabled(!m_enabled);
        }
        return event.pressed || event.released;
    }

    if (!m_enabled) {
        return false;
    }

    // Modified keys belong to application shortcuts, never musical typing.
    if (event.modifiers & AestraUI::NUIModifiers::Ctrl || event.modifiers & AestraUI::NUIModifiers::Alt ||
        event.modifiers & AestraUI::NUIModifiers::Super) {
        return false;
    }

    if (event.keyCode == Key::Up || event.keyCode == Key::Down) {
        if (event.pressed && !event.repeat) {
            shiftOctave(event.keyCode == Key::Up ? 12 : -12);
        }
        return event.pressed || event.released;
    }

    const int semitone = semitoneForKey(event.keyCode);
    if (semitone < 0) {
        return false;
    }

    const int key = static_cast<int>(event.keyCode);
    if (event.released) {
        const auto it = m_activeNotes.find(key);
        if (it != m_activeNotes.end()) {
            const ActiveNote active = it->second;
            if (post(active.unitId, kNoteOff, active.note, 0)) {
                m_activeNotes.erase(it);
            }
        }
        return true;
    }

    if (!event.pressed) {
        return false;
    }

    // The active-key latch also suppresses OS repeat on platforms that do not
    // populate NUIKeyEvent::repeat.
    if (event.repeat || m_activeNotes.find(key) != m_activeNotes.end()) {
        return true;
    }
    if (m_targetUnit == 0) {
        return true;
    }

    const auto note = static_cast<uint8_t>(m_baseMidiNote + semitone);
    if (post(m_targetUnit, kNoteOn, note, kDefaultVelocity)) {
        m_activeNotes.emplace(key, ActiveNote{m_targetUnit, note});
    }
    return true;
}

void MusicalTypingController::releaseAllNotes() {
    for (auto it = m_activeNotes.begin(); it != m_activeNotes.end();) {
        const ActiveNote active = it->second;
        if (post(active.unitId, kNoteOff, active.note, 0)) {
            it = m_activeNotes.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace Aestra
