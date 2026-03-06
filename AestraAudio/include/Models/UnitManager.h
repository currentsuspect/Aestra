#pragma once
#include "../../AestraCore/include/AestraJSON.h"
#include "PluginHost.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Aestra {
namespace Audio {

// Forward declarations
using UnitID = uint64_t;

// STUB: UnitGroup — Phase 2 will define group routing/bus logic
struct UnitGroup {
    uint64_t id{0};
    std::string name;
};

/**
 * @brief Unit information for audio engine
 */
struct UnitInfo {
    UnitID id{0};
    bool enabled{false};
    int targetMixerRoute{-1}; // -1 = not routed
    std::shared_ptr<IPluginInstance> plugin;

    // STUB: Phase 2 UI-facing properties
    std::string name;
    uint32_t color{0x808080}; // Default grey
    bool isMuted{false};
    bool isSolo{false};
    bool isArmed{false};
    bool isEnabled{false};
    std::string audioClipPath;
    UnitGroup group;
};

struct UnitState {
    int id;
    bool enabled;
    std::shared_ptr<IPluginInstance> plugin;
    int routeId;
};

struct AudioArsenalSnapshot {
    std::vector<UnitState> units;
};

/**
 * @brief Unit manager for Arsenal system
 */
class UnitManager {
public:
    UnitManager() = default;

    std::shared_ptr<const AudioArsenalSnapshot> getAudioSnapshot() const { return nullptr; }

    /**
     * @brief Get unit info by ID (returns nullptr if not found)
     */
    UnitInfo* getUnit(UnitID id) {
        auto it = m_units.find(id);
        if (it != m_units.end()) {
            return &it->second;
        }
        return nullptr;
    }

    const UnitInfo* getUnit(UnitID id) const {
        auto it = m_units.find(id);
        if (it != m_units.end()) {
            return &it->second;
        }
        return nullptr;
    }

    /**
     * @brief Create a new unit and return its ID
     */
    UnitID createUnit() {
        UnitID id = nextId++;
        m_units[id] = UnitInfo{};
        m_units[id].id = id;
        return id;
    }

    // STUB: Phase 2 unit property setters — UI calls these to modify unit state
    void setUnitName(UnitID id, const std::string& name) {
        if (auto* u = getUnit(id))
            u->name = name;
    }
    void setUnitMute(UnitID id, bool muted) {
        if (auto* u = getUnit(id))
            u->isMuted = muted;
    }
    void setUnitSolo(UnitID id, bool solo) {
        if (auto* u = getUnit(id))
            u->isSolo = solo;
    }
    void setUnitArmed(UnitID id, bool armed) {
        if (auto* u = getUnit(id))
            u->isArmed = armed;
    }
    void setUnitEnabled(UnitID id, bool enabled) {
        if (auto* u = getUnit(id))
            u->isEnabled = enabled;
    }
    void setUnitMixerChannel(UnitID id, int channel) {
        if (auto* u = getUnit(id))
            u->targetMixerRoute = channel;
    }
    void setUnitAudioClip(UnitID id, const std::string& path) {
        if (auto* u = getUnit(id))
            u->audioClipPath = path;
    }

    /**
     * @brief Save to JSON (stub)
     */
    JSON saveToJSON() const {
        JSON json;
        // Stub - return empty object
        return json;
    }

    /**
     * @brief Load from JSON (stub)
     */
    void loadFromJSON(const JSON& json) {
        // Stub - do nothing
        (void)json;
    }

private:
    UnitID nextId{1};
    std::unordered_map<UnitID, UnitInfo> m_units;
};

} // namespace Audio
} // namespace Aestra