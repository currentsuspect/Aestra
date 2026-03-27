#pragma once
#include "ClipSource.h"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace Aestra {
namespace Audio {

// Forward declarations
struct MidiNote;
struct MidiPayload;

/**
 * @brief Type-safe identifier for patterns
 */
struct PatternID {
    uint64_t value{0};
    PatternID() = default;
    explicit PatternID(uint64_t v) : value(v) {}
    explicit operator uint64_t() const { return value; }
    bool operator==(const PatternID& other) const { return value == other.value; }
    bool operator!=(const PatternID& other) const { return value != other.value; }
    // STUB: isValid — Phase 2 will define validity rules
    bool isValid() const { return value != 0; }
};

/**
 * @brief MIDI note structure for audio engine
 */
struct MidiNote {
    int pitch{0};              // MIDI note number (0-127)
    double startBeat{0.0};     // Start position in beats
    double durationBeats{0.0}; // Duration in beats
    float velocity{0.0f};      // 0..1
    uint64_t unitId{0};        // Unit ID for routing
};

/**
 * @brief MIDI payload for pattern source
 */
struct MidiPayload {
    std::vector<MidiNote> notes;
};

/**
 * @brief Audio slice definition
 */
struct AudioSlice {
    double startOffset{0.0};
    double duration{0.0};
    double startSamples{0.0};  // Alternative representation in samples (as double for JSON)
    double lengthSamples{0.0}; // Alternative representation in samples (as double for JSON)
};

/**
 * @brief Audio slice payload for patterns
 */
struct AudioSlicePayload {
    ClipSourceID audioSourceId;
    double startOffsetSeconds{0.0};
    double durationSeconds{0.0};
    std::vector<AudioSlice> slices;
};

/**
 * @brief Pattern source with variant payload
 */
class PatternSource {
public:
    PatternSource() = default;

    enum class Type { Empty, Midi, Audio };

    PatternID id;
    std::string name;
    double lengthBeats{4.0};
    Type type{Type::Empty};
    std::variant<std::monostate, MidiPayload, AudioSlicePayload> payload;
    int m_mixerChannel{-1};

    int getMixerChannel() const { return m_mixerChannel; }
    void setMixerChannel(int ch) { m_mixerChannel = ch; }

    bool isMidi() const { return type == Type::Midi && std::holds_alternative<MidiPayload>(payload); }

    bool isAudio() const { return type == Type::Audio && std::holds_alternative<AudioSlicePayload>(payload); }

    // Convenience access to MIDI notes
    std::vector<MidiNote>& getMidiNotes() { return std::get<MidiPayload>(payload).notes; }

    const std::vector<MidiNote>& getMidiNotes() const { return std::get<MidiPayload>(payload).notes; }
};

} // namespace Audio
} // namespace Aestra