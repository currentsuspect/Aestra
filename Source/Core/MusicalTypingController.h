// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUITypes.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace Aestra {

/**
 * @brief Converts computer-keyboard events into live MIDI notes.
 *
 * This class owns the key latch and note lifetime independently from UI focus
 * routing. Callers decide when keyboard input is eligible, while this class
 * guarantees one note-on per physical press and a matching note-off for the
 * original unit/note when the key is released.
 */
class MusicalTypingController {
public:
    using EventSink = std::function<bool(uint64_t unitId, uint8_t status, uint8_t data1, uint8_t data2)>;

    explicit MusicalTypingController(EventSink sink = {});

    /** @brief Replace the live-MIDI event destination. Releases held notes first. */
    void setEventSink(EventSink sink);
    /** @brief Select the Arsenal unit that receives future note-ons. */
    void setTargetUnit(uint64_t unitId);
    /** @brief Enable or disable musical typing. Disabling releases all held notes. */
    void setEnabled(bool enabled);
    /** @brief Handle an eligible UI key event. */
    bool handleKeyEvent(const AestraUI::NUIKeyEvent& event);
    /** @brief Send note-offs for every held key, used on focus loss and shutdown. */
    void releaseAllNotes();

    bool isEnabled() const { return m_enabled; }
    uint64_t targetUnit() const { return m_targetUnit; }
    int baseMidiNote() const { return m_baseMidiNote; }
    /** @brief Display octave using Aestra's convention where MIDI 60 is C3. */
    int displayOctave() const { return (m_baseMidiNote / 12) - 2; }
    size_t heldNoteCount() const { return m_activeNotes.size(); }

private:
    struct ActiveNote {
        uint64_t unitId{0};
        uint8_t note{0};
    };

    static int semitoneForKey(AestraUI::NUIKeyCode keyCode);
    bool post(uint64_t unitId, uint8_t status, uint8_t note, uint8_t velocity);
    void shiftOctave(int semitones);

    EventSink m_sink;
    std::unordered_map<int, ActiveNote> m_activeNotes;
    uint64_t m_targetUnit{0};
    int m_baseMidiNote{60};
    bool m_enabled{true};
};

} // namespace Aestra
