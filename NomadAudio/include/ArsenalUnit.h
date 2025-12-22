// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <cstdint>
#include "PatternSource.h"

namespace Nomad {
namespace Audio {

using UnitID = uint64_t;
using MixerRouteID = int32_t;

enum class UnitGroup : uint8_t { Drums, Synth, FX, Voice, Aux };

/**
 * @brief Represents a persistent routable entity in The Arsenal.
 * 
 * A Unit owns an instrument/input source and maintains its own state independent of patterns.
 * It serves as the primary "Agent" for sequencing and routing.
 */
struct ArsenalUnit {
    UnitID id = 0;
    std::string name;
    uint32_t color = 0xFF444444; // Default ARGB
    UnitGroup group = UnitGroup::Synth;

    // === UI / Main Thread State ===
    // These are modified by the UI and then synchronized to atomics/snapshots
    bool isEnabled = true;
    bool isArmed = false;
    bool isSolo = false;
    bool isMuted = false;

    // === Audio Path State (Atomic) ===
    // Safe for lock-free reading by audio logic (if checking directly)
    // Primary sync happens via Double-Buffered Snapshots in UnitManager
    std::atomic<bool> runtimeEnabled{true};
    
    // === Routing ===
    // Maps to a Mixer Channel or Voice Pool ID
    MixerRouteID targetMixerRoute = -1;
    
    // === Audio Source ===
    // Path to audio clip (WAV, MP3) loaded into this unit
    std::string audioClipPath;
    
    // Plugin instance ID (0 = no plugin)
    uint64_t pluginInstanceId = 0;

    // === Pattern Persistence ===
    // Tracks which patterns "belong" to or are used by this unit
    std::vector<PatternID> associatedPatterns; 
    
    // The pattern currently active/visible for this unit in the UI (e.g. for Step Grid)
    PatternID activePattern = 0; 
    
    // Per-pattern overrides
    struct UnitPatternState {
        PatternID patternId;
        int transpose = 0;
        float velocityScale = 1.0f;
        bool patternMute = false;
    };
    std::vector<UnitPatternState> perPatternState;

    ArsenalUnit() = default;
    
    // Explicit copy constructor required due to std::atomic member
    ArsenalUnit(const ArsenalUnit& other) {
        copyFrom(other);
    }

    ArsenalUnit& operator=(const ArsenalUnit& other) {
        if (this != &other) {
            copyFrom(other);
        }
        return *this;
    }

private:
    void copyFrom(const ArsenalUnit& other) {
        id = other.id;
        name = other.name;
        color = other.color;
        group = other.group;
        isEnabled = other.isEnabled;
        isArmed = other.isArmed;
        isSolo = other.isSolo;
        isMuted = other.isMuted;
        runtimeEnabled.store(other.runtimeEnabled.load(std::memory_order_relaxed));
        targetMixerRoute = other.targetMixerRoute;
        audioClipPath = other.audioClipPath;
        pluginInstanceId = other.pluginInstanceId;
        associatedPatterns = other.associatedPatterns;
        activePattern = other.activePattern;
        perPatternState = other.perPatternState;
    }
};

} // namespace Audio
} // namespace Nomad
