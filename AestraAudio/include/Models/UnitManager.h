#pragma once
#include "../../AestraCore/include/AestraJSON.h"
#include "PluginHost.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace Aestra {
namespace Audio {

// Forward declarations
using UnitID = uint64_t;

/**
 * @brief Unit information for audio engine
 */
struct UnitInfo {
    UnitID id{0};
    bool enabled{false};
    int targetMixerRoute{-1}; // -1 = not routed
    std::shared_ptr<IPluginInstance> plugin;
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