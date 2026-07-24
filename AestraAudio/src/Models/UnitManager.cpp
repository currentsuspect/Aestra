#include "UnitManager.h"

#include "GarbageCollector.h"
#include "Models/PatternManager.h"
#include "IO/MetadataParser.h"
#include "IO/MiniAudioDecoder.h"
#include "Plugin/BuiltInPlugins.h"
#include "Plugin/PluginManager.h"
#include "Plugin/SamplerPlugin.h"
#include "AestraJSON.h"
#include "AestraLog.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <limits>
#include <iomanip>
#include <optional>
#include <sstream>

namespace Aestra {
namespace Audio {

bool shouldRestoreArsenalPluginFromProject(const PluginInfo& plugin) noexcept {
    return plugin.format == PluginFormat::Internal;
}

namespace {
bool samplerStateHasSamplePath(const std::vector<uint8_t>& state) {
    if (state.empty()) {
        return false;
    }

    const std::string text(state.begin(), state.end());
    const auto json = Aestra::JSON::parse(text);
    return json.isObject() && json.has("samplePath") && json["samplePath"].isString() &&
           !json["samplePath"].asString().empty();
}

UnitGroup unitGroupForType(UnitType type) {
    switch (type) {
    case UnitType::Instrument: return UnitGroup::Synth;
    case UnitType::Audio: return UnitGroup::Audio;
    case UnitType::Sampler:
    case UnitType::PitchedSampler:
    default: return UnitGroup::Drums;
    }
}

void applySamplerDefaultsForUnitType(const UnitInfo& unit) {
    auto sampler = std::dynamic_pointer_cast<Plugins::SamplerPlugin>(unit.plugin);
    if (!sampler) {
        return;
    }

    if (unit.type == UnitType::PitchedSampler) {
        sampler->setMonoMode(true);
        sampler->setGlideTimeMs(80.0f);
    } else {
        sampler->setMonoMode(false);
    }
}

std::string arsenalRouteModeName(ArsenalRouteMode routeMode) {
    switch (routeMode) {
    case ArsenalRouteMode::PreviewToMaster: return "PreviewToMaster";
    case ArsenalRouteMode::RoutedToTimelineTrack: return "RoutedToTimelineTrack";
    case ArsenalRouteMode::Draft: return "Draft";
    default: return "PreviewToMaster";
    }
}

std::optional<ArsenalRouteMode> arsenalRouteModeFromNumber(const JSON& routeModeJson) {
    if (!routeModeJson.isNumber()) {
        return std::nullopt;
    }

    const double raw = routeModeJson.asNumber();
    if (!std::isfinite(raw) || std::floor(raw) != raw ||
        raw < static_cast<double>(std::numeric_limits<int>::min()) ||
        raw > static_cast<double>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }

    switch (static_cast<int>(raw)) {
    case static_cast<int>(ArsenalRouteMode::PreviewToMaster): return ArsenalRouteMode::PreviewToMaster;
    case static_cast<int>(ArsenalRouteMode::RoutedToTimelineTrack): return ArsenalRouteMode::RoutedToTimelineTrack;
    case static_cast<int>(ArsenalRouteMode::Draft): return ArsenalRouteMode::Draft;
    default: return std::nullopt;
    }
}

std::optional<ArsenalRouteMode> arsenalRouteModeFromJson(const JSON& routeModeJson) {
    if (routeModeJson.isObject()) {
        if (routeModeJson.has("id") && routeModeJson["id"].isNumber()) {
            return arsenalRouteModeFromNumber(routeModeJson["id"]);
        }
        if (routeModeJson.has("name")) {
            const std::string name = routeModeJson["name"].asString();
            if (name == "PreviewToMaster") return ArsenalRouteMode::PreviewToMaster;
            if (name == "RoutedToTimelineTrack") return ArsenalRouteMode::RoutedToTimelineTrack;
            if (name == "Draft") return ArsenalRouteMode::Draft;
        }
    } else if (routeModeJson.isNumber()) {
        return arsenalRouteModeFromNumber(routeModeJson);
    } else if (routeModeJson.isString()) {
        const std::string name = routeModeJson.asString();
        if (name == "PreviewToMaster") return ArsenalRouteMode::PreviewToMaster;
        if (name == "RoutedToTimelineTrack") return ArsenalRouteMode::RoutedToTimelineTrack;
        if (name == "Draft") return ArsenalRouteMode::Draft;
    }
    return std::nullopt;
}

ArsenalBridgeMode bridgeModeFallbackFromRouteId(int routeId) {
    return routeId < 0 ? ArsenalBridgeMode::PreviewToMaster : ArsenalBridgeMode::LinkedRack;
}
} // namespace

std::string unitTypeName(UnitType type) {
    switch (type) {
    case UnitType::Sampler: return "Sampler";
    case UnitType::PitchedSampler: return "PitchedSampler";
    case UnitType::Instrument: return "Instrument";
    case UnitType::Audio: return "Audio";
    default: return "Sampler";
    }
}

UnitType unitTypeFromJson(const JSON& typeJson) {
    if (typeJson.isObject()) {
        if (typeJson.has("id")) {
            return static_cast<UnitType>(typeJson["id"].asInt());
        }
        if (typeJson.has("name")) {
            const std::string name = typeJson["name"].asString();
            if (name == "Sampler") return UnitType::Sampler;
            if (name == "PitchedSampler" || name == "808") return UnitType::PitchedSampler;
            if (name == "Instrument") return UnitType::Instrument;
            if (name == "Audio") return UnitType::Audio;
        }
    } else if (typeJson.isNumber()) {
        return static_cast<UnitType>(typeJson.asInt());
    } else if (typeJson.isString()) {
        const std::string name = typeJson.asString();
        if (name == "Sampler") return UnitType::Sampler;
        if (name == "PitchedSampler" || name == "808") return UnitType::PitchedSampler;
        if (name == "Instrument") return UnitType::Instrument;
        if (name == "Audio") return UnitType::Audio;
    }
    return UnitType::Sampler;
}

UnitType unitTypeFromGroup(UnitGroup group) {
    switch (group) {
    case UnitGroup::Synth: return UnitType::Instrument;
    case UnitGroup::Audio: return UnitType::Audio;
    case UnitGroup::Drums:
    case UnitGroup::Unknown:
    default: return UnitType::Sampler;
    }
}

std::string unitGroupName(UnitGroup group) {
    switch (group) {
    case UnitGroup::Synth: return "Synth";
    case UnitGroup::Drums: return "Drums";
    case UnitGroup::Audio: return "Audio";
    default: return "";
    }
}

UnitGroup unitGroupFromJson(const JSON& groupJson) {
    if (groupJson.isObject()) {
        if (groupJson.has("id")) {
            return static_cast<UnitGroup>(groupJson["id"].asInt());
        }
        if (groupJson.has("name")) {
            const std::string name = groupJson["name"].asString();
            if (name == "Synth") return UnitGroup::Synth;
            if (name == "Drums") return UnitGroup::Drums;
            if (name == "Audio") return UnitGroup::Audio;
        }
    } else if (groupJson.isNumber()) {
        return static_cast<UnitGroup>(groupJson.asInt());
    } else if (groupJson.isString()) {
        const std::string name = groupJson.asString();
        if (name == "Synth") return UnitGroup::Synth;
        if (name == "Drums") return UnitGroup::Drums;
        if (name == "Audio") return UnitGroup::Audio;
    }
    return UnitGroup::Unknown;
}

std::string bytesToHex(const std::vector<uint8_t>& bytes) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (uint8_t byte : bytes) {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    if (hex.size() % 2 != 0) {
        return bytes;
    }

    bytes.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        const char hi = static_cast<char>(std::tolower(static_cast<unsigned char>(hex[i])));
        const char lo = static_cast<char>(std::tolower(static_cast<unsigned char>(hex[i + 1])));
        auto hexValue = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
            return -1;
        };
        const int high = hexValue(hi);
        const int low = hexValue(lo);
        if (high < 0 || low < 0) {
            return {};
        }
        bytes.push_back(static_cast<uint8_t>((high << 4) | low));
    }
    return bytes;
}

std::vector<float> generatePreviewWaveform(const std::vector<float>& samples, uint32_t numChannels, size_t targetSize = 128) {
    std::vector<float> waveform(targetSize, 0.0f);
    if (samples.empty() || numChannels == 0) {
        return waveform;
    }

    const size_t totalFrames = samples.size() / numChannels;
    const float framesPerBin = static_cast<float>(totalFrames) / static_cast<float>(targetSize);
    for (size_t bin = 0; bin < targetSize; ++bin) {
        const size_t startFrame = static_cast<size_t>(bin * framesPerBin);
        const size_t endFrame = std::min(totalFrames, static_cast<size_t>((bin + 1) * framesPerBin));
        float maxAmp = 0.0f;
        for (size_t frame = startFrame; frame < endFrame; ++frame) {
            float sum = 0.0f;
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                sum += std::abs(samples[frame * numChannels + ch]);
            }
            maxAmp = std::max(maxAmp, sum / static_cast<float>(numChannels));
        }
        waveform[bin] = std::min(1.0f, maxAmp);
    }
    return waveform;
}

void UnitManager::publishSnapshot() {
    auto snapshot = std::make_shared<AudioArsenalSnapshot>();
    snapshot->units.reserve(m_unitOrder.size());

    for (UnitID id : m_unitOrder) {
        const auto* unit = getUnit(id);
        if (!unit) {
            continue;
        }

        UnitState state{};
        state.id = static_cast<int>(unit->id);
        state.enabled = unit->enabled || unit->isEnabled;
        state.plugin = unit->plugin;
        state.routeId = unit->targetMixerRoute;
        state.mixerChannelId = unit->targetMixerChannelId;
        state.routeMode = arsenalRouteModeFromRouteId(state.routeId);
        state.gain = unit->gain;
        state.isMuted = unit->isMuted;
        state.isSolo = unit->isSolo;
        snapshot->units.push_back(state);
    }

    // Retire old snapshot through GC so the audio thread never dereferences freed memory.
    auto old = m_publishedSnapshot.exchange(snapshot, std::memory_order_acq_rel);
    if (old) {
        GarbageCollector::instance().release(old, "UnitManager::AudioArsenalSnapshot");
    }
}

std::shared_ptr<const AudioArsenalSnapshot> UnitManager::getAudioSnapshot() const {
    auto snapshot = m_publishedSnapshot.load(std::memory_order_acquire);
    return std::const_pointer_cast<const AudioArsenalSnapshot>(snapshot);
}

UnitInfo* UnitManager::getUnit(UnitID id) {
    auto it = m_units.find(id);
    return it != m_units.end() ? &it->second : nullptr;
}

const UnitInfo* UnitManager::getUnit(UnitID id) const {
    auto it = m_units.find(id);
    return it != m_units.end() ? &it->second : nullptr;
}

UnitID UnitManager::createUnit() {
    return createUnit("", UnitType::Sampler);
}

UnitID UnitManager::createUnit(const std::string& name, UnitType type) {
    UnitID id = nextId++;
    UnitInfo info;
    info.id = id;
    info.name = name;
    info.type = type;
    info.group = unitGroupForType(type);
    info.bridgeMode = bridgeModeFallbackFromRouteId(info.targetMixerRoute);

    if (m_patternManager && type != UnitType::Audio) {
        std::string patternName = name.empty() ? ("Unit " + std::to_string(id) + " Pattern") : (name + " Pattern");
        MidiPayload empty;
        info.defaultPatternId = m_patternManager->createMidiPattern(patternName, 8.0, empty);
    }

    m_units[id] = std::move(info);
    m_unitOrder.push_back(id);
    publishSnapshot();
    return id;
}

UnitID UnitManager::createUnit(const std::string& name, UnitGroup group) {
    return createUnit(name, unitTypeFromGroup(group));
}

UnitID UnitManager::duplicateUnit(UnitID sourceId) {
    const auto* src = getUnit(sourceId);
    if (!src) return 0;

    UnitID newId = createUnit(src->name + " Copy", src->type);
    auto* dst = getUnit(newId);
    if (!dst) return 0;

    // Copy scalar state
    dst->enabled          = src->enabled;
    dst->targetMixerRoute = src->targetMixerRoute;
    dst->targetMixerChannelId = src->targetMixerChannelId;
    dst->legacyMixerRoutePending = false;
    dst->routeMode        = src->routeMode;
    dst->bridgeMode       = src->bridgeMode;
    dst->color            = src->color;
    dst->isMuted          = src->isMuted;
    dst->isSolo           = src->isSolo;
    dst->isArmed          = src->isArmed;
    dst->group            = src->group;
    dst->audioClipPath    = src->audioClipPath;
    dst->audioDurationSeconds  = src->audioDurationSeconds;
    dst->audioPreviewWaveform  = src->audioPreviewWaveform;
    dst->pluginId         = src->pluginId;
    dst->pluginState      = src->pluginState;

    // Duplicate pattern if source has one
    if (src->defaultPatternId.isValid() && m_patternManager) {
        PatternID newPatternId = m_patternManager->clonePattern(src->defaultPatternId);
        dst->defaultPatternId = newPatternId;
    }

    // Re-instantiate plugin if source has one
    if (!src->pluginId.empty() && !src->pluginState.empty()) {
        auto& pluginManager = PluginManager::getInstance();
        auto newPlugin = pluginManager.createInstanceById(src->pluginId);
        if (newPlugin) {
            const double sampleRate = m_sampleRate.load(std::memory_order_relaxed);
            const uint32_t blockSize = m_blockSize.load(std::memory_order_relaxed);
            if (newPlugin->initialize(sampleRate > 0 ? sampleRate : 48000.0,
                                      blockSize > 0 ? blockSize : 512)) {
                if (newPlugin->loadState(src->pluginState)) {
                    dst->plugin = newPlugin;
                    dst->pluginId = src->pluginId;
                    dst->pluginState = src->pluginState;
                    applySamplerDefaultsForUnitType(*dst);
                    if ((dst->enabled || dst->isEnabled) && !newPlugin->isActive()) {
                        newPlugin->activate();
                    }
                } else {
                    Log::warning("[UnitManager] Failed to load plugin state for duplicated unit " +
                                std::to_string(newId));
                }
            } else {
                Log::warning("[UnitManager] Failed to initialize plugin for duplicated unit " +
                            std::to_string(newId));
            }
        } else {
            Log::warning("[UnitManager] Failed to create plugin instance for duplicated unit " +
                        std::to_string(newId) + " with plugin ID: " + src->pluginId);
        }
    }

    publishSnapshot();
    return newId;
}

void UnitManager::reorderUnit(UnitID id, size_t newIndex) {
    auto it = std::find(m_unitOrder.begin(), m_unitOrder.end(), id);
    if (it == m_unitOrder.end()) {
        return;
    }
    const size_t oldIndex = static_cast<size_t>(std::distance(m_unitOrder.begin(), it));
    if (oldIndex == newIndex) {
        return;
    }
    UnitID value = *it;
    m_unitOrder.erase(it);
    if (newIndex > m_unitOrder.size()) {
        newIndex = m_unitOrder.size();
    }
    m_unitOrder.insert(m_unitOrder.begin() + static_cast<std::ptrdiff_t>(newIndex), value);
    publishSnapshot();
}

bool UnitManager::removeUnit(UnitID id) {
    auto orderIt = std::find(m_unitOrder.begin(), m_unitOrder.end(), id);
    if (orderIt == m_unitOrder.end()) {
        return false;
    }

    m_unitOrder.erase(orderIt);
    bool erased = m_units.erase(id) > 0;
    publishSnapshot();
    return erased;
}

void UnitManager::clear() {
    m_units.clear();
    m_unitOrder.clear();
    nextId = 1;
    publishSnapshot();
}

void UnitManager::setUnitName(UnitID id, const std::string& name) { if (auto* u = getUnit(id)) u->name = name; }
void UnitManager::setUnitMute(UnitID id, bool muted) { if (auto* u = getUnit(id)) { u->isMuted = muted; publishSnapshot(); } }
void UnitManager::setUnitSolo(UnitID id, bool solo) { if (auto* u = getUnit(id)) { u->isSolo = solo; publishSnapshot(); } }
void UnitManager::setUnitArmed(UnitID id, bool armed) { if (auto* u = getUnit(id)) u->isArmed = armed; }
void UnitManager::setUnitEnabled(UnitID id, bool enabled) {
    if (auto* u = getUnit(id)) {
        u->isEnabled = enabled;
        u->enabled = enabled;
        if (u->plugin) {
            if (enabled) {
                if (!u->plugin->isActive()) {
                    u->plugin->activate();
                }
            } else {
                if (u->plugin->isActive()) {
                    u->plugin->deactivate();
                }
            }
        }
        publishSnapshot();
    }
}
void UnitManager::setUnitMixerChannel(UnitID id, int64_t channelId) {
    if (auto* u = getUnit(id)) {
        u->targetMixerChannelId = channelId > 0 && channelId < static_cast<int64_t>(UINT32_MAX)
                                      ? static_cast<uint32_t>(channelId)
                                      : MASTER_MIXER_CHANNEL_ID;
        u->legacyMixerRoutePending = false;
        publishSnapshot();
    }
}

uint32_t UnitManager::getUnitMixerChannel(UnitID id) const {
    if (const auto* u = getUnit(id)) {
        return u->targetMixerChannelId;
    }
    return MASTER_MIXER_CHANNEL_ID;
}

void UnitManager::migrateLegacyMixerRoutes(const std::vector<uint32_t>& mixerChannelIds) {
    bool changed = false;
    for (UnitID id : m_unitOrder) {
        auto* unit = getUnit(id);
        if (!unit) {
            continue;
        }

        if (unit->legacyMixerRoutePending) {
            if (unit->targetMixerRoute >= 0 && static_cast<size_t>(unit->targetMixerRoute) < mixerChannelIds.size()) {
                unit->targetMixerChannelId = mixerChannelIds[static_cast<size_t>(unit->targetMixerRoute)];
            } else {
                unit->targetMixerChannelId = MASTER_MIXER_CHANNEL_ID;
            }
            unit->legacyMixerRoutePending = false;
            changed = true;
        } else if (unit->targetMixerChannelId != MASTER_MIXER_CHANNEL_ID &&
                   std::find(mixerChannelIds.begin(), mixerChannelIds.end(), unit->targetMixerChannelId) ==
                       mixerChannelIds.end()) {
            unit->targetMixerChannelId = MASTER_MIXER_CHANNEL_ID;
            changed = true;
        }
    }
    if (changed) {
        publishSnapshot();
    }
}

bool UnitManager::resetMixerChannel(uint32_t channelId) {
    bool changed = false;
    for (UnitID id : m_unitOrder) {
        if (auto* unit = getUnit(id); unit && unit->targetMixerChannelId == channelId) {
            unit->targetMixerChannelId = MASTER_MIXER_CHANNEL_ID;
            unit->legacyMixerRoutePending = false;
            changed = true;
        }
    }
    if (changed) {
        publishSnapshot();
    }
    return changed;
}
void UnitManager::assignUnitToTimelineLane(UnitID id, int laneIndex) {
    if (auto* u = getUnit(id)) {
        u->targetMixerRoute = laneIndex;
        u->routeMode = arsenalRouteModeFromRouteId(laneIndex);
        publishSnapshot();
        // bridgeMode is intentionally NOT updated here.
        // bridgeMode represents ownership metadata (e.g. DraftOnly, LinkedRack)
        // and is independent of the current routing assignment.
        // routeId/routeMode remain authoritative for render/export decisions.
    }
}

void UnitManager::clearUnitTimelineLane(UnitID id) {
    if (auto* u = getUnit(id)) {
        u->targetMixerRoute = -1;
        u->routeMode = ArsenalRouteMode::PreviewToMaster;
        publishSnapshot();
    }
}

int UnitManager::getUnitTimelineLane(UnitID id) const {
    if (const auto* u = getUnit(id)) {
        return u->targetMixerRoute;
    }
    return -1;
}
void UnitManager::setUnitGain(UnitID id, float gain) {
    if (auto* u = getUnit(id)) {
        u->gain = gain;
        publishSnapshot();
    }
}

void UnitManager::setUnitAudioClip(UnitID id, const std::string& path) {
    auto* u = getUnit(id);
    if (!u) {
        return;
    }

    u->audioClipPath = path;
    u->audioDurationSeconds = 0.0;
    u->audioPreviewWaveform.clear();

    if (path.empty()) {
        return;
    }

    const auto metadata = MetadataParser::parse(path);
    if (metadata.durationSeconds > 0.0) {
        u->audioDurationSeconds = metadata.durationSeconds;
    }

    std::vector<float> previewAudio;
    uint32_t previewRate = 0;
    uint32_t previewChannels = 0;
    constexpr uint64_t kPreviewMaxFrames = 48000 * 24;
    constexpr double kPreviewMaxSeconds = static_cast<double>(kPreviewMaxFrames) / 48000.0;
    if (decodeAudioPreview(path, previewAudio, previewRate, previewChannels, kPreviewMaxSeconds)) {
        u->audioPreviewWaveform = generatePreviewWaveform(previewAudio, previewChannels);
        if (u->audioDurationSeconds <= 0.0 && previewRate > 0 && previewChannels > 0) {
            u->audioDurationSeconds = static_cast<double>(previewAudio.size()) / static_cast<double>(previewRate * previewChannels);
        }
    }

    if (u->type == UnitType::Audio) {
        u->group = UnitGroup::Audio;
        publishSnapshot();
        return;
    }

    const std::string samplerId = BuiltInPlugins::samplerInfo().id;
    if (!u->plugin || u->pluginId != samplerId) {
        auto& pluginManager = PluginManager::getInstance();
        auto samplerInstance = pluginManager.createInstanceById(samplerId);
        if (samplerInstance) {
            const double sr = m_sampleRate.load(std::memory_order_relaxed);
            const uint32_t blockSize = m_blockSize.load(std::memory_order_relaxed);
            if (samplerInstance->initialize(sr > 0 ? sr : 48000.0, blockSize > 0 ? blockSize : 512)) {
                if ((u->enabled || u->isEnabled) && !samplerInstance->isActive()) {
                    samplerInstance->activate();
                }
                u->pluginId = samplerId;
                u->plugin = std::move(samplerInstance);
                applySamplerDefaultsForUnitType(*u);
            }
        }
    }

    if (auto sampler = std::dynamic_pointer_cast<Plugins::SamplerPlugin>(u->plugin)) {
        if (sampler->loadSample(path)) {
            u->pluginState = sampler->saveState();
        }
    }
    publishSnapshot();
}

bool UnitManager::setUnitAudioClipFromDecoded(UnitID id, const std::string& path, std::vector<float> decodedData,
                                              uint32_t sampleRate, uint32_t numChannels,
                                              std::vector<float> previewWaveform, double durationSeconds) {
    auto* u = getUnit(id);
    if (!u) {
        return false;
    }

    u->audioClipPath = path;
    u->audioDurationSeconds = std::isfinite(durationSeconds) && durationSeconds > 0.0 ? durationSeconds : 0.0;
    u->audioPreviewWaveform = std::move(previewWaveform);

    if (path.empty()) {
        publishSnapshot();
        return true;
    }

    if (u->type == UnitType::Audio) {
        u->group = UnitGroup::Audio;
        publishSnapshot();
        return true;
    }

    const std::string samplerId = BuiltInPlugins::samplerInfo().id;
    if (!u->plugin || u->pluginId != samplerId) {
        auto& pluginManager = PluginManager::getInstance();
        auto samplerInstance = pluginManager.createInstanceById(samplerId);
        if (samplerInstance) {
            const double sr = m_sampleRate.load(std::memory_order_relaxed);
            const uint32_t blockSize = m_blockSize.load(std::memory_order_relaxed);
            if (samplerInstance->initialize(sr > 0 ? sr : 48000.0, blockSize > 0 ? blockSize : 512)) {
                if ((u->enabled || u->isEnabled) && !samplerInstance->isActive()) {
                    samplerInstance->activate();
                }
                u->pluginId = samplerId;
                u->plugin = std::move(samplerInstance);
                applySamplerDefaultsForUnitType(*u);
            }
        }
    }

    bool loaded = false;
    if (auto sampler = std::dynamic_pointer_cast<Plugins::SamplerPlugin>(u->plugin)) {
        loaded = sampler->loadSampleData(path, std::move(decodedData), sampleRate, numChannels);
        if (loaded) {
            u->pluginState = sampler->saveState();
        }
    }

    publishSnapshot();
    return loaded;
}
void UnitManager::setUnitColor(UnitID id, uint32_t color) { if (auto* u = getUnit(id)) u->color = color; }
void UnitManager::setUnitGroup(UnitID id, UnitGroup group) { if (auto* u = getUnit(id)) u->group = std::move(group); }
bool UnitManager::setUnitType(UnitID id, UnitType type) {
    auto* unit = getUnit(id);
    if (!unit) {
        return false;
    }

    unit->type = type;
    unit->group = unitGroupForType(type);
    applySamplerDefaultsForUnitType(*unit);
    publishSnapshot();
    return true;
}
UnitType UnitManager::getUnitType(UnitID id) const {
    if (const auto* u = getUnit(id)) {
        return u->type;
    }
    return UnitType::Sampler;
}

void UnitManager::attachPlugin(UnitID id, const std::string& pluginId, std::shared_ptr<IPluginInstance> plugin) {
    if (auto* u = getUnit(id)) {
        u->pluginId = pluginId;
        u->plugin = std::move(plugin);
        applySamplerDefaultsForUnitType(*u);
        if (u->plugin) {
            if ((u->enabled || u->isEnabled) && !u->plugin->isActive()) {
                u->plugin->activate();
            }
            u->pluginState = u->plugin->saveState();
        } else {
            u->pluginState.clear();
        }
        publishSnapshot();
    }
}

void UnitManager::setUnitPluginState(UnitID id, const std::vector<uint8_t>& state) {
    if (auto* u = getUnit(id)) {
        u->pluginState = state;
        if (u->plugin && !state.empty()) {
            u->plugin->loadState(state);
        }
        publishSnapshot();
    }
}

void UnitManager::captureUnitPluginState(UnitID id) {
    if (auto* u = getUnit(id); u && u->plugin) {
        u->pluginState = u->plugin->saveState();
    }
}

std::shared_ptr<IPluginInstance> UnitManager::getUnitPlugin(UnitID id) const {
    const auto* u = getUnit(id);
    return u ? u->plugin : nullptr;
}

std::string UnitManager::getUnitPluginId(UnitID id) const {
    const auto* u = getUnit(id);
    return u ? u->pluginId : std::string{};
}

JSON UnitManager::saveToJSON() const {
    JSON root = JSON::object();
    root.set("nextId", JSON(static_cast<double>(nextId)));

    JSON units = JSON::array();
    for (UnitID id : m_unitOrder) {
        const auto* unit = getUnit(id);
        if (!unit) continue;

        JSON u = JSON::object();
        u.set("id", JSON(static_cast<double>(unit->id)));
        u.set("name", JSON(unit->name));
        u.set("enabled", JSON(unit->enabled || unit->isEnabled));
        u.set("targetMixerRoute", JSON(static_cast<double>(unit->targetMixerRoute)));
        u.set("timelineLaneAssignment", JSON(static_cast<double>(unit->targetMixerRoute)));
        u.set("targetMixerChannelId", JSON(static_cast<double>(unit->targetMixerChannelId)));
        u.set("color", JSON(std::to_string(unit->color)));
        u.set("muted", JSON(unit->isMuted));
        u.set("solo", JSON(unit->isSolo));
        u.set("armed", JSON(unit->isArmed));
        u.set("audioClipPath", JSON(unit->audioClipPath));
        u.set("gain", JSON(static_cast<double>(unit->gain)));
        u.set("audioDurationSeconds", JSON(unit->audioDurationSeconds));
        u.set("defaultPatternId", JSON(static_cast<double>(unit->defaultPatternId.value)));
        const ArsenalRouteMode resolvedRouteMode = arsenalRouteModeFromRouteId(unit->targetMixerRoute);
        JSON routeMode = JSON::object();
        routeMode.set("id", JSON(static_cast<double>(static_cast<uint8_t>(resolvedRouteMode))));
        routeMode.set("name", JSON(arsenalRouteModeName(resolvedRouteMode)));
        u.set("routeMode", routeMode);
        u.set("bridgeMode", JSON(std::string(toString(unit->bridgeMode))));

        JSON group = JSON::object();
        group.set("id", JSON(static_cast<double>(static_cast<uint32_t>(unit->group))));
        group.set("name", JSON(unitGroupName(unit->group)));
        u.set("group", group);

        JSON type = JSON::object();
        type.set("id", JSON(static_cast<double>(static_cast<uint32_t>(unit->type))));
        type.set("name", JSON(unitTypeName(unit->type)));
        u.set("type", type);

        if (!unit->pluginId.empty()) {
            u.set("pluginId", JSON(unit->pluginId));
        }

        std::vector<uint8_t> state = unit->pluginState;
        if (unit->plugin) {
            state = unit->plugin->saveState();
        }
        if (!state.empty()) {
            u.set("pluginStateHex", JSON(bytesToHex(state)));
        }

        units.push(u);
    }

    root.set("units", units);
    return root;
}

void UnitManager::loadFromJSON(const JSON& json) {
    clear();
    if (!json.isObject()) {
        return;
    }

    if (json.has("nextId")) {
        nextId = static_cast<UnitID>(json["nextId"].asNumber());
        if (nextId == 0) nextId = 1;
    }

    if (!json.has("units") || !json["units"].isArray()) {
        return;
    }

    auto& pluginManager = PluginManager::getInstance();
    const JSON& units = json["units"];
    UnitID maxSeenId = 0;

    for (size_t i = 0; i < units.size(); ++i) {
        const JSON& ju = units[i];
        if (!ju.isObject() || !ju.has("id")) {
            continue;
        }

        UnitInfo unit;
        unit.id = static_cast<UnitID>(ju["id"].asNumber());
        unit.name = ju.has("name") ? ju["name"].asString() : std::string{};
        unit.enabled = ju.has("enabled") ? ju["enabled"].asBool() : false;
        unit.isEnabled = unit.enabled;
        if (ju.has("timelineLaneAssignment")) {
            unit.targetMixerRoute = ju["timelineLaneAssignment"].asInt();
        } else {
            unit.targetMixerRoute = ju.has("targetMixerRoute") ? ju["targetMixerRoute"].asInt() : -1;
        }
        if (ju.has("targetMixerChannelId") && ju["targetMixerChannelId"].isNumber()) {
            const double rawMixerChannelId = ju["targetMixerChannelId"].asNumber();
            if (std::isfinite(rawMixerChannelId) && rawMixerChannelId >= 0.0 &&
                rawMixerChannelId < static_cast<double>(UINT32_MAX)) {
                unit.targetMixerChannelId = static_cast<uint32_t>(rawMixerChannelId);
            }
        } else {
            unit.legacyMixerRoutePending = unit.targetMixerRoute >= 0;
        }
        const ArsenalRouteMode legacyResolvedRouteMode = arsenalRouteModeFromRouteId(unit.targetMixerRoute);
        unit.routeMode = legacyResolvedRouteMode;
        if (ju.has("routeMode")) {
            const auto loadedRouteMode = arsenalRouteModeFromJson(ju["routeMode"]);
            if (loadedRouteMode.has_value() && loadedRouteMode.value() != legacyResolvedRouteMode) {
                Log::warning("[UnitManager] routeMode disagrees with routeId for unit " + std::to_string(unit.id) +
                             "; keeping routeId-compatible behavior.");
            } else if (!loadedRouteMode.has_value()) {
                Log::warning("[UnitManager] Invalid routeMode value for unit " + std::to_string(unit.id) +
                             "; falling back to routeId-compatible behavior.");
            }
        }
        unit.bridgeMode = bridgeModeFallbackFromRouteId(unit.targetMixerRoute);
        if (ju.has("bridgeMode")) {
            if (ju["bridgeMode"].isString()) {
                const auto loadedBridgeMode = arsenalBridgeModeFromString(ju["bridgeMode"].asString());
                if (loadedBridgeMode.has_value()) {
                    unit.bridgeMode = loadedBridgeMode.value();
                } else {
                    Log::warning("[UnitManager] Invalid bridgeMode value for unit " + std::to_string(unit.id) +
                                 "; falling back to routeId-compatible bridge metadata.");
                }
            } else {
                Log::warning("[UnitManager] Non-string bridgeMode for unit " + std::to_string(unit.id) +
                             "; falling back to routeId-compatible bridge metadata.");
            }
        }
        if (ju.has("color")) {
            if (ju["color"].isString()) {
                try {
                    unit.color = static_cast<uint32_t>(std::stoul(ju["color"].asString()));
                } catch (const std::exception&) {
                    unit.color = 0xFFFFFFFF; // Default white on parse error
                }
            } else {
                unit.color = static_cast<uint32_t>(ju["color"].asNumber());
            }
        }
        unit.isMuted = ju.has("muted") ? ju["muted"].asBool() : false;
        unit.isSolo = ju.has("solo") ? ju["solo"].asBool() : false;
        unit.isArmed = ju.has("armed") ? ju["armed"].asBool() : false;
        unit.audioClipPath = ju.has("audioClipPath") ? ju["audioClipPath"].asString() : std::string{};
        unit.gain = ju.has("gain") ? static_cast<float>(ju["gain"].asNumber()) : 1.0f;
        unit.audioDurationSeconds = ju.has("audioDurationSeconds") ? ju["audioDurationSeconds"].asNumber() : 0.0;
        if (ju.has("defaultPatternId")) {
            unit.defaultPatternId = PatternID(static_cast<uint64_t>(ju["defaultPatternId"].asNumber()));
        }

        if (ju.has("group")) {
            unit.group = unitGroupFromJson(ju["group"]);
        }
        unit.type = ju.has("type") ? unitTypeFromJson(ju["type"]) : unitTypeFromGroup(unit.group);
        unit.group = unitGroupForType(unit.type);

        unit.pluginId = ju.has("pluginId") ? ju["pluginId"].asString() : std::string{};
        if (ju.has("pluginStateHex") && ju["pluginStateHex"].isString()) {
            unit.pluginState = hexToBytes(ju["pluginStateHex"].asString());
        }

        if (!unit.pluginId.empty()) {
            const PluginInfo* pluginInfo = pluginManager.findPlugin(unit.pluginId);
            if (pluginInfo && shouldRestoreArsenalPluginFromProject(*pluginInfo)) {
                unit.plugin = pluginManager.createInstance(*pluginInfo);
            } else if (pluginInfo) {
                Aestra::Log::warning("[UnitManager] Skipping external Arsenal plugin restore from project for unit " +
                                     std::to_string(unit.id) + ": " + unit.pluginId);
            } else {
                Aestra::Log::warning("[UnitManager] Missing Arsenal plugin during project restore for unit " +
                                     std::to_string(unit.id) + ": " + unit.pluginId);
            }
            if (unit.plugin) {
                double sr = m_sampleRate.load(std::memory_order_relaxed);
                uint32_t blockSize = m_blockSize.load(std::memory_order_relaxed);
                unit.plugin->initialize(sr > 0 ? sr : 48000.0, blockSize > 0 ? blockSize : 512);

                // Load state BEFORE activation to ensure plugin is ready before processing audio.
                // This matches EffectChain lifecycle: create -> initialize -> loadState -> activate.
                bool stateLoaded = true;
                const bool samplerStateIncludesSamplePath = samplerStateHasSamplePath(unit.pluginState);
                if (!unit.pluginState.empty()) {
                    if (!unit.plugin->loadState(unit.pluginState)) {
                        stateLoaded = false;
                        Aestra::Log::warning("[UnitManager] Failed to load state for unit " +
                                             std::to_string(unit.id) + " — using default state");
                    }
                }

                // Older project saves kept the sampler file only in UnitInfo::audioClipPath.
                // Rehydrate that path so restored MIDI has sample data even when pluginState lacks samplePath.
                if (!unit.audioClipPath.empty() && (!samplerStateIncludesSamplePath || !stateLoaded)) {
                    if (auto sampler = std::dynamic_pointer_cast<Plugins::SamplerPlugin>(unit.plugin)) {
                        if (sampler->loadSample(unit.audioClipPath)) {
                            unit.pluginState = sampler->saveState();
                        } else {
                            Aestra::Log::warning("[UnitManager] Failed to reload sampler audio for unit " +
                                                 std::to_string(unit.id) + ": " + unit.audioClipPath);
                        }
                    }
                }

                if (unit.enabled || unit.isEnabled) {
                    unit.plugin->activate();
                }

                applySamplerDefaultsForUnitType(unit);
            }
        }

        maxSeenId = std::max(maxSeenId, unit.id);
        m_unitOrder.push_back(unit.id);
        m_units[unit.id] = std::move(unit);
    }

    if (nextId <= maxSeenId) {
        nextId = maxSeenId + 1;
    }
    publishSnapshot();
}

} // namespace Audio
} // namespace Aestra
