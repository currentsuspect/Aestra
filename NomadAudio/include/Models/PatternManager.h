// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PatternSource.h"
#include <map>
#include <mutex>
#include <memory>
#include <functional>
#include <vector>


namespace Nomad {
namespace Audio {

/**
 * @brief Manages a pool of reusable PatternSource objects.
 * 
 * PatternManager handles the persistence, lookup, and versioning of patterns.
 * It provides thread-safe access for the UI/Worker threads and ensures 
 * RT-safety for the Audio thread via version markers and snapshots.
 */
class PatternManager {
public:
    PatternManager() = default;
    ~PatternManager() = default;

    // === Pattern Management ===
    
    /// Create a new MIDI pattern and add it to the pool
    PatternID createMidiPattern(const std::string& name, double lengthBeats, const MidiPayload& payload);
    
    /// Create a new Audio Slice pattern and add it to the pool
    PatternID createAudioPattern(const std::string& name, double lengthBeats, const AudioSlicePayload& payload);
    
    /// Clone an existing pattern (for "Make Unique" / Detach)
    PatternID clonePattern(PatternID originalId);
    
    /// Remove a pattern from the pool
    void removePattern(PatternID id);

    /// Clear all patterns
    void clear();

    // === Pattern Access ===

    /// Get a pointer to a pattern (UI thread only)
    PatternSource* getPattern(PatternID id);
    const PatternSource* getPattern(PatternID id) const;

    /// Apply an edit to a pattern (increments version)
    void applyPatch(PatternID id, std::function<void(PatternSource&)> patcher);

    // === Snapshots (Audio Thread) ===
    
    /// Return an immutable snapshot of a pattern for real-time processing
    std::shared_ptr<const PatternSource> getSafeSnapshot(PatternID id) const;

    /// Get all patterns (for snapshot generation)
    std::vector<std::shared_ptr<PatternSource>> getAllPatterns() const;


private:
    uint64_t m_nextPatternId = 1;
    std::map<PatternID, std::shared_ptr<PatternSource>> m_pool;
    mutable std::mutex m_mutex;

    PatternID generateNextID();

};

} // namespace Audio
} // namespace Nomad
