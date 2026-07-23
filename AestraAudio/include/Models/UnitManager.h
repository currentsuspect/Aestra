#pragma once
#include "../../AestraCore/include/AestraJSON.h"
#include "../Plugin/PluginHost.h"
#include "Models/ArsenalBridgeMode.h"
#include "Models/PatternSource.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include "AestraAtomicSharedPtr.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace Aestra {
namespace Audio {

// Forward declarations
class PatternManager;
using UnitID = uint64_t;

/**
 * @brief Explicit interpretation of current Arsenal route semantics.
 *
 * This enum is scaffolding for context ownership cleanup. It maps to the
 * existing `routeId` / `targetMixerRoute` behavior and does not alter routing.
 */
enum class ArsenalRouteMode : uint8_t {
    PreviewToMaster = 0,
    RoutedToTimelineTrack = 1,
    Draft = 2,
};

/**
 * @brief Map legacy route id semantics to the explicit Arsenal route model.
 *
 * Current behavior mapping:
 * - routeId < 0  -> PreviewToMaster
 * - routeId >= 0 -> RoutedToTimelineTrack
 */
constexpr ArsenalRouteMode arsenalRouteModeFromRouteId(int routeId) noexcept {
    return routeId < 0 ? ArsenalRouteMode::PreviewToMaster : ArsenalRouteMode::RoutedToTimelineTrack;
}

/**
 * @brief Return true when a project-loaded Arsenal plugin may be instantiated automatically.
 */
bool shouldRestoreArsenalPluginFromProject(const PluginInfo& plugin) noexcept;

enum class UnitGroup : uint32_t {
    Unknown = 0,
    Synth = 1,
    Drums = 2,
    Audio = 3,
};

enum class UnitType : uint32_t {
    Sampler = 1,
    PitchedSampler = 2,
    Instrument = 3,
    Audio = 4,
};

/**
 * @brief Unit information for audio engine
 */
struct UnitInfo {
    /** @brief Stable unit identifier. */
    UnitID id{0};
    /** @brief Whether the unit is enabled in the realtime engine snapshot. */
    bool enabled{false};
    /**
     * @brief Timeline lane assignment compatibility field.
     *
     * Current behavior maps this integer directly to route mode:
     * - < 0 routes unit output to master preview path.
     * - >= 0 routes unit output to the Timeline track at that index.
     *
     * Arsenal currently participates in the main engine render path. Timeline
     * remains arrangement/export authority, and export follows live processBlock
     * routing behavior.
     */
    int targetMixerRoute{-1};
    /**
     * @brief Explicit route mode field (Phase 2A scaffolding).
     *
     * Compatibility note: current rendering authority remains legacy
     * @ref targetMixerRoute mapping. This field is kept aligned for explicitness
     * and future migration, but does not change current behavior.
     */
    ArsenalRouteMode routeMode{ArsenalRouteMode::PreviewToMaster};
    /** @brief Effective route mode, preserving legacy route fields as authority until cleared. */
    ArsenalRouteMode getRouteMode() const noexcept { return arsenalRouteModeFromRouteId(targetMixerRoute); }
    /** @brief True when this unit currently routes into the Timeline track path. */
    bool routesToTimelineTrack() const noexcept { return getRouteMode() == ArsenalRouteMode::RoutedToTimelineTrack; }
    /** @brief True when this unit currently routes to master preview path. */
    bool routesToMasterPreview() const noexcept { return getRouteMode() == ArsenalRouteMode::PreviewToMaster; }
    /**
     * @brief Ownership metadata for Arsenal->Timeline bridge semantics.
     *
     * This field is additive metadata only and does not affect current routing
     * or render/export authority.
     */
    ArsenalBridgeMode bridgeMode{ArsenalBridgeMode::PreviewToMaster};
    /** @brief Explicit in-memory bridge ownership mode value. */
    ArsenalBridgeMode getBridgeMode() const noexcept { return bridgeMode; }
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
    /** @brief Linear output gain applied at the unit's mix point (1 = unity). */
    float gain{1.0f};
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
    /** @brief Cached clip duration for audio-style units. */
    double audioDurationSeconds{0.0};
    /** @brief Lightweight preview waveform bins for Arsenal rendering. */
    std::vector<float> audioPreviewWaveform;
    /** @brief High-level group classification used by the Arsenal UI. */
    UnitGroup group;
    /** @brief Unit content type used to select sequencing and editing behavior. */
    UnitType type{UnitType::Sampler};
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
    /**
     * @brief Legacy route id consumed by current render path.
     *
     * - < 0 routes to master preview path
     * - >= 0 routes to Timeline track path
     */
    int routeId;
    /** @brief Linear output gain applied at the unit's mix point. */
    float gain{1.0f};
    /** @brief True when the unit is muted (audio thread visibility). */
    bool isMuted{false};
    /** @brief True when the unit is soloed (audio thread visibility). */
    bool isSolo{false};
    /**
     * @brief Explicit route mode snapshot field (Phase 2A scaffolding).
     *
     * Compatibility note: current rendering authority remains @ref routeId.
     */
    ArsenalRouteMode routeMode{ArsenalRouteMode::PreviewToMaster};
    /** @brief Effective route mode, preserving legacy route fields as authority until cleared. */
    ArsenalRouteMode getRouteMode() const noexcept { return arsenalRouteModeFromRouteId(routeId); }
    /** @brief True when this unit currently routes into the Timeline track path. */
    bool routesToTimelineTrack() const noexcept { return getRouteMode() == ArsenalRouteMode::RoutedToTimelineTrack; }
    /** @brief True when this unit currently routes to master preview path. */
    bool routesToMasterPreview() const noexcept { return getRouteMode() == ArsenalRouteMode::PreviewToMaster; }
};

/**
 * @brief Immutable audio-thread snapshot of Arsenal state.
 */
struct AudioArsenalSnapshot {
    /**
     * @brief Ordered list of unit states visible to the audio engine.
     *
     * Snapshot data currently feeds Arsenal processing that runs inside the main
     * engine render path. Export authority remains tied to Timeline/live
     * processBlock behavior.
     */
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
    UnitID createUnit(const std::string& name, UnitType type);
    /**
     * @brief Create a unit with explicit display name and group.
     * @param name User-facing unit name.
     * @param group Initial unit group classification.
     * @return Identifier of the created unit.
     */
    UnitID createUnit(const std::string& name, UnitGroup group = UnitGroup::Unknown);
    /**
     * @brief Duplicate an existing unit, copying its state and pattern.
     * @param sourceId Unit identifier to duplicate.
     * @return Identifier of the new unit, or 0 if the source is missing.
     */
    UnitID duplicateUnit(UnitID sourceId);

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
    /** @brief Legacy alias for Timeline lane assignment. */
    void setUnitMixerChannel(UnitID id, int channel);
    /** @brief Assign a unit to a Timeline lane for arrangement playback. */
    void assignUnitToTimelineLane(UnitID id, int laneIndex);
    /** @brief Clear any Timeline lane assignment so the unit previews to master. */
    void clearUnitTimelineLane(UnitID id);
    /** @brief Get the current Timeline lane assignment, or -1 when none. */
    int getUnitTimelineLane(UnitID id) const;
    /** @brief Attach an audio clip path to a unit. */
    void setUnitAudioClip(UnitID id, const std::string& path);
    /** @brief Set the unit's linear output gain (published to the audio snapshot). */
    void setUnitGain(UnitID id, float gain);
    /** @brief Publish a pre-decoded audio clip to a unit without doing file I/O on the caller. */
    bool setUnitAudioClipFromDecoded(UnitID id, const std::string& path, std::vector<float> decodedData,
                                     uint32_t sampleRate, uint32_t numChannels, std::vector<float> previewWaveform,
                                     double durationSeconds);
    /** @brief Set the UI accent color for a unit. */
    void setUnitColor(UnitID id, uint32_t color);
    /** @brief Set the unit group classification. */
    void setUnitGroup(UnitID id, UnitGroup group);
    /** @brief Set the unit content type and keep its group classification aligned. */
    bool setUnitType(UnitID id, UnitType type);
    /** @brief Get the current content type for a unit. */
    UnitType getUnitType(UnitID id) const;

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
    /** @brief Get the MIDI note that plays a unit's sample untransposed (60 fallback). */
    int getUnitRootMidiNote(UnitID id) const;

    /** @brief Serialize the manager to JSON. */
    JSON saveToJSON() const;
    /** @brief Restore unit state from JSON. */
    void loadFromJSON(const JSON& json);

    /** @brief Set the PatternManager used for auto-creating patterns per unit. */
    void setPatternManager(PatternManager* pm) { m_patternManager = pm; }

    /**
     * @brief Publish the current Arsenal state as an immutable snapshot.
     *
     * Call from the main/control thread whenever Arsenal state changes
     * (unit created/destroyed, route changed, plugin attached, mute/solo/enabled changed).
     * The audio thread reads the published snapshot via getAudioSnapshot() without
     * allocating, locking, or mutating project state.
     */
    void publishSnapshot();

private:
    UnitID nextId{1};
    std::unordered_map<UnitID, UnitInfo> m_units;
    std::vector<UnitID> m_unitOrder;
    std::atomic<double> m_sampleRate{48000.0};
    std::atomic<uint32_t> m_blockSize{512};
    PatternManager* m_patternManager{nullptr};

    /**
     * @brief Published immutable snapshot for the audio thread.
     *
     * Updated by publishSnapshot() on the control thread.
     * Read by getAudioSnapshot() on the audio thread via atomic_load.
     * Release semantics on store ensures the audio thread never sees a partial snapshot.
     */
    mutable AtomicSharedPtr<AudioArsenalSnapshot> m_publishedSnapshot{std::make_shared<AudioArsenalSnapshot>()};

public:
    void setSampleRate(double rate) { m_sampleRate.store(rate, std::memory_order_relaxed); }
    void setBlockSize(uint32_t size) { m_blockSize.store(size, std::memory_order_relaxed); }
};

} // namespace Audio
} // namespace Aestra
