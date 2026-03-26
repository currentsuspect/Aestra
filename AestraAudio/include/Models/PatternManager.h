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
     */
    PatternID createPattern() {
        PatternID id{nextId++};
        m_patterns[id.value] = std::make_unique<PatternSource>();
        m_patterns[id.value]->id = id;
        return id;
    }

    /**
     * @brief Create an audio pattern
     */
    PatternID createAudioPattern(const std::string& name, double lengthBeats, const AudioSlicePayload& payload) {
        PatternID id{nextId++};
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
     */
    PatternID createMidiPattern(const std::string& name, double lengthBeats, const MidiPayload& payload) {
        PatternID id{nextId++};
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
     */
    void removePattern(PatternID id) {
        m_patterns.erase(id.value);
    }

    /**
     * @brief Get or create a pattern
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

    // STUB: applyPatch — Phase 2 will support undo-aware pattern mutations
    template <typename PatchFn> void applyPatch(PatternID id, PatchFn&& fn) {
        auto* pattern = getOrCreatePattern(id);
        if (pattern)
            fn(*pattern);
    }

private:
    uint64_t nextId{1};
    std::unordered_map<uint64_t, std::unique_ptr<PatternSource>> m_patterns;
};

} // namespace Audio
} // namespace Aestra