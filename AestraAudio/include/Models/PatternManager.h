#pragma once
#include "PatternSource.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Simple in-memory pattern manager
 */
class PatternManager {
public:
    /**
     * @brief Construct an empty in-memory pattern store.
     */
    PatternManager() = default;

    /**
     * @brief Get a pattern by ID (returns nullptr if not found)
     */
    PatternSource* getPattern(PatternID id) {
        auto it = m_patterns.find(id.value);
        if (it != m_patterns.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    const PatternSource* getPattern(PatternID id) const {
        auto it = m_patterns.find(id.value);
        if (it != m_patterns.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    /**
     * @brief Create a new pattern and return its ID
     * @return Identifier of the created empty pattern.
     */
    PatternID createPattern() {
        PatternID id{nextId++};
        m_patterns[id.value] = std::make_unique<PatternSource>();
        m_patterns[id.value]->id = id;
        return id;
    }

    /**
     * @brief Create an audio pattern
     * @param name Pattern display name.
     * @param lengthBeats Pattern duration in beats.
     * @param payload Audio pattern payload.
     * @return Identifier of the created audio pattern.
     */
    PatternID createAudioPattern(const std::string& name, double lengthBeats, const AudioSlicePayload& payload) {
        return createAudioPatternWithId(PatternID{}, name, lengthBeats, payload);
    }

    /**
     * @brief Create an audio pattern restoring a serialized identity (#446).
     *
     * When @p requestedId is valid and unused it becomes the pattern's ID and
     * the mint counter advances past it; otherwise a fresh ID is minted.
     * @param requestedId Serialized pattern ID to restore (invalid = mint).
     * @param name Pattern display name.
     * @param lengthBeats Pattern duration in beats.
     * @param payload Audio pattern payload.
     * @return Identifier of the created audio pattern.
     */
    PatternID createAudioPatternWithId(PatternID requestedId, const std::string& name, double lengthBeats,
                                       const AudioSlicePayload& payload) {
        const PatternID id = claimId(requestedId);
        auto pattern = std::make_unique<PatternSource>();
        pattern->id = id;
        pattern->name = name;
        pattern->lengthBeats = lengthBeats;
        pattern->type = PatternSource::Type::Audio;
        pattern->payload = payload;
        m_patterns[id.value] = std::move(pattern);
        return id;
    }

    /**
     * @brief Create a MIDI pattern
     * @param name Pattern display name.
     * @param lengthBeats Pattern duration in beats.
     * @param payload MIDI pattern payload.
     * @return Identifier of the created MIDI pattern.
     */
    PatternID createMidiPattern(const std::string& name, double lengthBeats, const MidiPayload& payload) {
        return createMidiPatternWithId(PatternID{}, name, lengthBeats, payload);
    }

    /**
     * @brief Create a MIDI pattern restoring a serialized identity (#446).
     *
     * When @p requestedId is valid and unused it becomes the pattern's ID and
     * the mint counter advances past it; otherwise a fresh ID is minted.
     * @param requestedId Serialized pattern ID to restore (invalid = mint).
     * @param name Pattern display name.
     * @param lengthBeats Pattern duration in beats.
     * @param payload MIDI pattern payload.
     * @return Identifier of the created MIDI pattern.
     */
    PatternID createMidiPatternWithId(PatternID requestedId, const std::string& name, double lengthBeats,
                                      const MidiPayload& payload) {
        const PatternID id = claimId(requestedId);
        auto pattern = std::make_unique<PatternSource>();
        pattern->id = id;
        pattern->name = name;
        pattern->lengthBeats = lengthBeats;
        pattern->type = PatternSource::Type::Midi;
        pattern->payload = payload;
        m_patterns[id.value] = std::move(pattern);
        return id;
    }

    /**
     * @brief Clone an existing pattern and return the new ID
     * @param sourceId Pattern identifier to duplicate.
     * @return Identifier of the cloned pattern, or an invalid ID if the source is missing.
     */
    PatternID clonePattern(PatternID sourceId) {
        auto* src = getPattern(sourceId);
        if (!src) return PatternID{};
        PatternID id{nextId++};
        auto pattern = std::make_unique<PatternSource>(*src);
        pattern->id = id;
        m_patterns[id.value] = std::move(pattern);
        return id;
    }

    /**
     * @brief Remove a pattern by ID
     * @param id Pattern identifier to erase.
     */
    void removePattern(PatternID id) {
        m_patterns.erase(id.value);
    }

    /**
     * @brief Get or create a pattern
     * @param id Pattern identifier to look up or create.
     * @return Pointer to the requested pattern.
     */
    PatternSource* getOrCreatePattern(PatternID id) {
        auto& ptr = m_patterns[id.value];
        if (!ptr) {
            ptr = std::make_unique<PatternSource>();
            ptr->id = id;
        }
        return ptr.get();
    }

    /**
     * @brief Get all patterns
     * @return Shared-pointer view of all stored patterns.
     */
    std::vector<std::shared_ptr<PatternSource>> getAllPatterns() const {
        std::vector<std::shared_ptr<PatternSource>> result;
        result.reserve(m_patterns.size());
        for (const auto& [id, ptr] : m_patterns) {
            // Note: This creates shared_ptr from unique_ptr - not ideal but works for shim
            result.push_back(std::shared_ptr<PatternSource>(ptr.get(), [](PatternSource*) {}));
        }
        return result;
    }

    /**
     * @brief Apply a mutation lambda to a pattern, creating it on demand.
     * @tparam PatchFn Callable that mutates a PatternSource.
     * @param id Pattern identifier to mutate.
     * @param fn Mutation function invoked with the target pattern.
     */
    template <typename PatchFn> void applyPatch(PatternID id, PatchFn&& fn) {
        auto* pattern = getOrCreatePattern(id);
        if (pattern)
            fn(*pattern);
    }

    /**
     * @brief Clear all stored patterns and reset ID allocation.
     */
    void clear() {
        m_patterns.clear();
        nextId = 1;
    }

private:
    /**
     * @brief Resolve the ID a new pattern should use.
     *
     * Valid + unused requested IDs are restored verbatim and the mint counter
     * jumps past them so later mints can never collide with restored IDs.
     */
    PatternID claimId(PatternID requestedId) {
        if (requestedId.isValid() && m_patterns.find(requestedId.value) == m_patterns.end()) {
            if (requestedId.value >= nextId) {
                nextId = requestedId.value + 1;
            }
            return requestedId;
        }
        return PatternID{nextId++};
    }

    uint64_t nextId{1};
    std::unordered_map<uint64_t, std::unique_ptr<PatternSource>> m_patterns;
};

} // namespace Audio
} // namespace Aestra
