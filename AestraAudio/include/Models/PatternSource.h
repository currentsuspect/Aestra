#pragma once
#include "ClipSource.h"
#include "../Music/ScaleContext.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
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
    float pan{0.0f};           // -1 (left) .. 1 (right), 0 = centre
    uint64_t unitId{0};        // Unit ID for routing
    int8_t pitchOffset{0};     // Semitone offset from root for step-based pitched samplers
    float gate{1.0f};          // Note length as a fraction of one step
    bool slide{false};         // Sustain into the next active step for legato slides
    float chance{1.0f};        // Probability this step plays on a given pass (1 = always)
    uint8_t ratchet{1};        // Hits the step is split into (1..4; 1 = a single hit)
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
    // NOTE: only startSamples/lengthSamples are persisted (ProjectSerializer writes
    // and reads these). startOffset/duration are legacy and are not serialized —
    // do NOT rely on positional aggregate init like {0.0, numFrames}, which fills
    // startOffset/duration and leaves the persisted fields empty. Set the sample
    // fields explicitly. See #447.
    double startOffset{0.0};    // legacy, not persisted
    double duration{0.0};       // legacy, not persisted
    double startSamples{0.0};   // persisted "start" (samples, as double for JSON)
    double lengthSamples{0.0};  // persisted "length" (samples, as double for JSON)
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
    /** Stable mixer destination for audio patterns (0 routes directly to Master). */
    uint32_t m_mixerChannelId{0};
    /** True while a pre-source-routing project awaits lane-based migration. */
    bool legacyMixerRoutePending{false};
    std::optional<ScaleContext> scaleOverride;
    /** Delays every odd 16th step: 0 = straight, 1 = the longest swing (a half step late). */
    float swing{0.0f};
    /** Per-unit swing overrides. An absent entry inherits the pattern-wide swing. */
    std::vector<std::pair<uint64_t, float>> unitSwing;

    float swingForUnit(uint64_t unitId) const {
        for (const auto& [id, amount] : unitSwing) if (id == unitId) return std::clamp(amount, 0.0f, 1.0f);
        return std::clamp(swing, 0.0f, 1.0f);
    }
    std::optional<float> unitSwingOverride(uint64_t unitId) const {
        for (const auto& [id, amount] : unitSwing) if (id == unitId) return amount;
        return std::nullopt;
    }
    void setUnitSwing(uint64_t unitId, float amount) {
        amount = std::clamp(amount, 0.0f, 1.0f);
        for (auto& [id, value] : unitSwing) {
            if (id == unitId) { value = amount; return; }
        }
        unitSwing.emplace_back(unitId, amount);
        std::sort(unitSwing.begin(), unitSwing.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    }
    void restoreUnitSwingOverride(uint64_t unitId, std::optional<float> amount) {
        unitSwing.erase(std::remove_if(unitSwing.begin(), unitSwing.end(), [unitId](const auto& v) { return v.first == unitId; }), unitSwing.end());
        if (amount.has_value()) unitSwing.emplace_back(unitId, std::clamp(*amount, 0.0f, 1.0f));
        std::sort(unitSwing.begin(), unitSwing.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    }

    uint32_t getMixerChannelId() const { return m_mixerChannelId; }
    void setMixerChannelId(uint32_t channelId) { m_mixerChannelId = channelId; }

    bool isMidi() const { return type == Type::Midi && std::holds_alternative<MidiPayload>(payload); }

    bool isAudio() const { return type == Type::Audio && std::holds_alternative<AudioSlicePayload>(payload); }

    // Convenience access to MIDI notes
    std::vector<MidiNote>& getMidiNotes() { return std::get<MidiPayload>(payload).notes; }

    const std::vector<MidiNote>& getMidiNotes() const { return std::get<MidiPayload>(payload).notes; }
};

} // namespace Audio
} // namespace Aestra
