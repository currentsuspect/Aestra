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

enum class UnitGroup : uint32_t {
    Unknown = 0,
    Synth = 1,
    Drums = 2,
    Audio = 3,
};

/**
 * @brief Unit information for audio engine
 */
struct UnitInfo {
    UnitID id{0};
    bool enabled{false};
    int targetMixerRoute{-1}; // -1 = not routed
    std::shared_ptr<IPluginInstance> plugin;
    std::string pluginId;
    std::vector<uint8_t> pluginState;

    // Phase-2-lite UI-facing properties
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

    std::shared_ptr<const AudioArsenalSnapshot> getAudioSnapshot() const;

    UnitInfo* getUnit(UnitID id);
    const UnitInfo* getUnit(UnitID id) const;

    UnitID createUnit();
    UnitID createUnit(const std::string& name, UnitGroup group = UnitGroup::Unknown);

    size_t getUnitCount() const { return m_unitOrder.size(); }
    std::vector<UnitID> getAllUnitIDs() const { return m_unitOrder; }
    void reorderUnit(UnitID id, size_t newIndex);
    void clear();

    void setUnitName(UnitID id, const std::string& name);
    void setUnitMute(UnitID id, bool muted);
    void setUnitSolo(UnitID id, bool solo);
    void setUnitArmed(UnitID id, bool armed);
    void setUnitEnabled(UnitID id, bool enabled);
    void setUnitMixerChannel(UnitID id, int channel);
    void setUnitAudioClip(UnitID id, const std::string& path);
    void setUnitColor(UnitID id, uint32_t color);
    void setUnitGroup(UnitID id, UnitGroup group);

    void attachPlugin(UnitID id, const std::string& pluginId, std::shared_ptr<IPluginInstance> plugin);
    void setUnitPluginState(UnitID id, const std::vector<uint8_t>& state);
    void captureUnitPluginState(UnitID id);
    std::shared_ptr<IPluginInstance> getUnitPlugin(UnitID id) const;
    std::string getUnitPluginId(UnitID id) const;

    JSON saveToJSON() const;
    void loadFromJSON(const JSON& json);

private:
    UnitID nextId{1};
    std::unordered_map<UnitID, UnitInfo> m_units;
    std::vector<UnitID> m_unitOrder;
};

} // namespace Audio
} // namespace Aestra