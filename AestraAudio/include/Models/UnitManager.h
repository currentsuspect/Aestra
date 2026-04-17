#pragma once
#include "../../AestraCore/include/AestraJSON.h"
#include "../Plugin/PluginHost.h"
#include "Models/PatternSource.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Aestra {
namespace Audio {

// Forward declarations
class PatternManager;
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
    /** @brief Stable unit identifier. */
    UnitID id{0};
    /** @brief Whether the unit is enabled in the realtime engine snapshot. */
    bool enabled{false};
    /** @brief Mixer route identifier, or -1 when routed directly to master. */
    int targetMixerRoute{-1}; // -1 = not routed
    /** @brief Live plugin instance attached to this unit. */
    std::shared_ptr<IPluginInstance> plugin;
    /** @brief Plugin identifier used to recreate the instance. */
    std::string pluginId;
    /** @brief Serialized plugin state blob. */
    std::vector<uint8_t> pluginState;

    /** @brief User-facing unit name. */
    std::string name;
    /** @brief Accent color used by Arsenal and related UI. */
    uint32_t color{0x808080}; // Default grey
    /** @brief Whether the unit is muted. */
    bool isMuted{false};
    /** @brief Whether the unit is soloed. */
    bool isSolo{false};
    /** @brief Whether the unit is armed for editing or recording. */
    bool isArmed{false};
    /** @brief Whether the unit is enabled in the UI layer. */
    bool isEnabled{false};
    /** @brief Associated audio clip path for clip-based units. */
    std::string audioClipPath;
    /** @brief High-level group classification used by the Arsenal UI. */
    UnitGroup group;
    /** @brief Default pattern associated with this unit for Piano Roll editing. */
    PatternID defaultPatternId;
};

/**
 * @brief Lightweight unit state used by the audio-thread snapshot.
 */
struct UnitState {
    /** @brief Integer unit identifier used by the snapshot format. */
    int id;
    /** @brief Whether the unit is active. */
    bool enabled;
    /** @brief Plugin instance used for rendering. */
    std::shared_ptr<IPluginInstance> plugin;
    /** @brief Mixer route identifier. */
    int routeId;
};

/**
 * @brief Immutable audio-thread snapshot of Arsenal state.
 */
struct AudioArsenalSnapshot {
    /** @brief Ordered list of unit states visible to the audio engine. */
    std::vector<UnitState> units;
};

/**
 * @brief Unit manager for Arsenal system
 */
class UnitManager {
public:
    /**
     * @brief Construct an empty unit manager.
     */
    UnitManager() = default;

    /**
     * @brief Build a snapshot of the current unit state for the audio engine.
     * @return Shared immutable snapshot of unit routing and plugin state.
     */
    std::shared_ptr<const AudioArsenalSnapshot> getAudioSnapshot() const;

    /**
     * @brief Get a mutable unit by identifier.
     * @param id Unit identifier.
     * @return Pointer to the unit, or nullptr when missing.
     */
    UnitInfo* getUnit(UnitID id);
    /**
     * @brief Get a const unit by identifier.
     * @param id Unit identifier.
     * @return Pointer to the unit, or nullptr when missing.
     */
    const UnitInfo* getUnit(UnitID id) const;

    /**
     * @brief Create a unit with generated defaults.
     * @return Identifier of the created unit.
     */
    UnitID createUnit();
    /**
     * @brief Create a unit with explicit display name and group.
     * @param name User-facing unit name.
     * @param group Initial unit group classification.
     * @return Identifier of the created unit.
     */
    UnitID createUnit(const std::string& name, UnitGroup group = UnitGroup::Unknown);

    /**
     * @brief Get the number of units in display order.
     * @return Unit count.
     */
    size_t getUnitCount() const { return m_unitOrder.size(); }
    /**
     * @brief Get all unit identifiers in display order.
     * @return Ordered unit identifier list.
     */
    std::vector<UnitID> getAllUnitIDs() const { return m_unitOrder; }
    /**
     * @brief Reorder a unit inside the display list.
     * @param id Unit identifier to move.
     * @param newIndex Destination index inside the unit order.
     */
    void reorderUnit(UnitID id, size_t newIndex);
    /**
     * @brief Remove a unit by identifier.
     * @param id Unit identifier to erase.
     * @return True when a unit was removed.
     */
    bool removeUnit(UnitID id);
    /**
     * @brief Remove every unit from the manager.
     */
    void clear();

    /** @brief Set the display name for a unit. */
    void setUnitName(UnitID id, const std::string& name);
    /** @brief Set muted state for a unit. */
    void setUnitMute(UnitID id, bool muted);
    /** @brief Set solo state for a unit. */
    void setUnitSolo(UnitID id, bool solo);
    /** @brief Set armed state for a unit. */
    void setUnitArmed(UnitID id, bool armed);
    /** @brief Set enabled state for a unit. */
    void setUnitEnabled(UnitID id, bool enabled);
    /** @brief Route a unit to a mixer channel. */
    void setUnitMixerChannel(UnitID id, int channel);
    /** @brief Attach an audio clip path to a unit. */
    void setUnitAudioClip(UnitID id, const std::string& path);
    /** @brief Set the UI accent color for a unit. */
    void setUnitColor(UnitID id, uint32_t color);
    /** @brief Set the unit group classification. */
    void setUnitGroup(UnitID id, UnitGroup group);

    /**
     * @brief Attach a plugin instance to a unit.
     * @param id Unit identifier to update.
     * @param pluginId Plugin identifier string.
     * @param plugin Plugin instance to attach.
     */
    void attachPlugin(UnitID id, const std::string& pluginId, std::shared_ptr<IPluginInstance> plugin);
    /** @brief Replace the serialized plugin state blob for a unit. */
    void setUnitPluginState(UnitID id, const std::vector<uint8_t>& state);
    /** @brief Capture the current plugin state back into the unit model. */
    void captureUnitPluginState(UnitID id);
    /** @brief Get the plugin instance attached to a unit. */
    std::shared_ptr<IPluginInstance> getUnitPlugin(UnitID id) const;
    /** @brief Get the plugin identifier attached to a unit. */
    std::string getUnitPluginId(UnitID id) const;

    /** @brief Serialize the manager to JSON. */
    JSON saveToJSON() const;
    /** @brief Restore unit state from JSON. */
    void loadFromJSON(const JSON& json);

    /** @brief Set the PatternManager used for auto-creating patterns per unit. */
    void setPatternManager(PatternManager* pm) { m_patternManager = pm; }

private:
    UnitID nextId{1};
    std::unordered_map<UnitID, UnitInfo> m_units;
    std::vector<UnitID> m_unitOrder;
    std::atomic<double> m_sampleRate{48000.0};
    std::atomic<uint32_t> m_blockSize{512};
    PatternManager* m_patternManager{nullptr};

public:
    void setSampleRate(double rate) { m_sampleRate.store(rate, std::memory_order_relaxed); }
    void setBlockSize(uint32_t size) { m_blockSize.store(size, std::memory_order_relaxed); }
};

} // namespace Audio
} // namespace Aestra
