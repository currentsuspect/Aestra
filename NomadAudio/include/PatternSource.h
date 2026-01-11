// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <variant>
#include "ClipSource.h"

namespace Nomad {
namespace Audio {

// =============================================================================
// Basic Types
// =============================================================================

struct PatternID {
    uint64_t value = 0;
    PatternID() = default;
    PatternID(uint64_t v) : value(v) {}
    operator uint64_t() const { return value; }
    bool isValid() const { return value != 0; }
    bool operator==(const PatternID& other) const { return value == other.value; }
    bool operator!=(const PatternID& other) const { return value != other.value; }
    bool operator<(const PatternID& other) const { return value < other.value; }
};

#include <atomic>

struct PatternVersion {
    std::atomic<uint64_t> value{0};
    PatternVersion() = default;
    PatternVersion(uint64_t v) { value.store(v, std::memory_order_relaxed); }
    // Atomic types are not copyable, so we provide explicit copy/move logic if needed
    PatternVersion(const PatternVersion& other) { value.store(other.value.load(std::memory_order_relaxed), std::memory_order_relaxed); }
    PatternVersion& operator=(const PatternVersion& other) {
        value.store(other.value.load(std::memory_order_relaxed), std::memory_order_relaxed);
        return *this;
    }
    operator uint64_t() const { return value.load(std::memory_order_relaxed); }
    void increment() { value.fetch_add(1, std::memory_order_release); }
};

// Forward declare UnitID (defined in ArsenalUnit.h)
using UnitID = uint64_t;


struct MidiNote {
    double startBeat;
    double durationBeats;
    uint8_t pitch;
    uint8_t velocity;
    UnitID unitId = 0; // Which unit plays this note (0 = any/all)
};

// =============================================================================
// Payloads
// =============================================================================

struct MidiPayload {
    std::vector<MidiNote> notes;
};

struct AudioSlicePayload {
    ClipSourceID audioSourceId; // Reference to shared media
    struct Slice {
        double startSamples;
        double lengthSamples;
    };
    std::vector<Slice> slices;
};

using PatternPayload = std::variant<MidiPayload, AudioSlicePayload>;

// =============================================================================
// PatternSource - The "Template"
// =============================================================================

class PatternSource {
public:
    PatternID id;
    PatternVersion version;
    std::string name;
    double lengthBeats = 4.0;
    
    PatternPayload payload;
    
    // Mixer routing: -1 = auto (use lane's default), 0+ = specific mixer channel index
    int mixerChannelIndex = -1;
    
    // Optional: Color for visual distinction (ARGB format)
    uint32_t colorRGBA = 0xFFbb86fc;  // Default: Purple accent

    PatternSource() = default;
    
    bool isMidi() const { return std::holds_alternative<MidiPayload>(payload); }
    bool isAudio() const { return std::holds_alternative<AudioSlicePayload>(payload); }
    
    // Mixer routing helpers
    bool hasCustomRouting() const { return mixerChannelIndex >= 0; }
    void setMixerChannel(int index) { mixerChannelIndex = index; }
    int getMixerChannel() const { return mixerChannelIndex; }
};

} // namespace Audio
} // namespace Nomad
