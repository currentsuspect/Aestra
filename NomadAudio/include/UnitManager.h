// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ArsenalUnit.h"
#include <mutex>
#include <map>
#include <vector>
#include <memory>
#include <atomic>
#include "../../NomadCore/include/NomadJSON.h"

namespace Nomad {
namespace Audio {

/**
 * @brief Thread-safe snapshot of Arsenal state for the audio engine.
 * Designed to be read without locks.
 */
struct AudioArsenalSnapshot {
    struct UnitState {
        UnitID id;
        bool enabled;
        bool armed;
        bool solo;
        bool muted;
        MixerRouteID routeId;
    };
    
    // Order matches the UI order (for consistent processing if needed)
    std::vector<UnitState> units;
};

/**
 * @brief Manages the lifecycle and state of Arsenal Units.
 * 
 * Owns the "Source of Truth" for all units.
 * Provides thread-safe state modification and audio snapshots.
 */
class UnitManager {
public:
    UnitManager();
    ~UnitManager() = default;

    // === Lifecycle (Main Thread) ===
    UnitID createUnit(const std::string& name, UnitGroup group = UnitGroup::Synth);
    void removeUnit(UnitID id);
    void clear();

    // === Accessors (Main Thread) ===
    ArsenalUnit* getUnit(UnitID id);
    const std::vector<UnitID>& getAllUnitIDs() const { return m_unitOrder; }
    size_t getUnitCount() const { return m_units.size(); }

    // === State Modifiers (Main Thread) ===
    // These update the UI state AND the broadcast to the audio snapshot
    void setUnitEnabled(UnitID id, bool enabled);
    void setUnitArmed(UnitID id, bool armed);
    void setUnitSolo(UnitID id, bool solo);
    void setUnitMute(UnitID id, bool muted);
    void setUnitRoute(UnitID id, MixerRouteID route);
    void setUnitAudioClip(UnitID id, const std::string& clipPath);
    void setUnitMixerChannel(UnitID id, int channelIndex); // Simplified setter for mixer routing
    
    // === Pattern Association (Main Thread) ===
    void setActivePattern(UnitID id, PatternID pid);

    // === Audio Thread Interface ===
    // Returns a lock-free snapshot of the current state
    std::shared_ptr<const AudioArsenalSnapshot> getAudioSnapshot() const;

    // === Persistence ===
    Nomad::JSON saveToJSON() const;
    void loadFromJSON(const Nomad::JSON& json);


private:
    mutable std::mutex m_mutex;
    
    // Primary storage
    std::map<UnitID, ArsenalUnit> m_units;
    std::vector<UnitID> m_unitOrder; // Preserves UI order
    
    UnitID m_nextId = 1;

    // Audio Snapshot (Double Buffered via shared_ptr swap)
    std::shared_ptr<AudioArsenalSnapshot> m_audioSnapshot;

    // Helper to regenerate snapshot
    void updateAudioSnapshot();
};

} // namespace Audio
} // namespace Nomad
